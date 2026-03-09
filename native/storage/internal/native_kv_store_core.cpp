#include "storage/internal/native_kv_store_object.h"

#include "common.h"
#include "common/native_error_log.h"
#include "common/safe_copy.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace baileys_native::storage_internal {

namespace {

using KVDataSnapshot = std::unordered_map<std::string, std::vector<uint8_t>>;

constexpr size_t kMaxKeyLengthBytes = 4u * 1024u * 1024u;
constexpr size_t kMaxValueLengthBytes = 256u * 1024u * 1024u;

inline bool AddWouldOverflowSizeT(size_t a, size_t b) {
	return b > std::numeric_limits<size_t>::max() - a;
}

size_t ComputeSnapshotFileBytes(const KVDataSnapshot& data) {
	size_t total = kMagicSize;
	for (const auto& [key, value] : data) {
		if (AddWouldOverflowSizeT(total, RecordSize(key.size(), value.size()))) {
			return std::numeric_limits<size_t>::max();
		}
		total += RecordSize(key.size(), value.size());
	}
	return total;
}

bool RewriteSnapshotToPath(const std::string& targetPath, const KVDataSnapshot& snapshot) {
	std::ofstream out(targetPath, std::ios::binary | std::ios::trunc);
	if (!out) return false;

	out.write(kStoreMagic, static_cast<std::streamsize>(kMagicSize));
	if (!out) return false;

	for (const auto& [key, value] : snapshot) {
		if (key.size() > std::numeric_limits<uint32_t>::max()) return false;
		if (value.size() > std::numeric_limits<uint32_t>::max()) return false;

		uint8_t header[kRecordHeaderSize];
		header[0] = kOpPut;
		WriteU32LE(static_cast<uint32_t>(key.size()), header + 1);
		WriteU32LE(static_cast<uint32_t>(value.size()), header + 5);

		out.write(reinterpret_cast<const char*>(header), static_cast<std::streamsize>(kRecordHeaderSize));
		if (!key.empty()) {
			out.write(key.data(), static_cast<std::streamsize>(key.size()));
		}
		if (!value.empty()) {
			out.write(reinterpret_cast<const char*>(value.data()), static_cast<std::streamsize>(value.size()));
		}
		if (!out) return false;
	}

	return static_cast<bool>(out);
}

bool CompactPathFromSnapshot(const std::string& path, const KVDataSnapshot& snapshot, size_t* outFileBytes) {
	const std::string tempPath = path + ".tmp";
	const std::string backupPath = path + ".bak";

	if (!RewriteSnapshotToPath(tempPath, snapshot)) {
		return false;
	}

	std::error_code ec;
	bool movedOriginal = false;

	std::filesystem::remove(backupPath, ec);
	ec.clear();

	if (std::filesystem::exists(path, ec) && !ec) {
		std::filesystem::rename(path, backupPath, ec);
		if (ec) {
			std::filesystem::remove(tempPath, ec);
			return false;
		}
		movedOriginal = true;
	}

	ec.clear();
	std::filesystem::rename(tempPath, path, ec);
	if (ec) {
		if (movedOriginal) {
			std::error_code restoreEc;
			std::filesystem::rename(backupPath, path, restoreEc);
		}
		std::filesystem::remove(tempPath, ec);
		return false;
	}

	if (movedOriginal) {
		std::filesystem::remove(backupPath, ec);
	}

	std::error_code sizeEc;
	const uintmax_t pathSize = std::filesystem::file_size(path, sizeEc);
	if (!sizeEc && pathSize <= static_cast<uintmax_t>(std::numeric_limits<size_t>::max())) {
		*outFileBytes = static_cast<size_t>(pathSize);
		return true;
	}

	const size_t computed = ComputeSnapshotFileBytes(snapshot);
	if (computed == std::numeric_limits<size_t>::max()) {
		return false;
	}
	*outFileBytes = computed;
	return true;
}

} // namespace

bool ParseMutationFromValue(const Napi::Env& env, const Napi::Value& value, Mutation* out) {
	if (!value.IsObject()) {
		Napi::TypeError::New(env, "setMany entries must be objects").ThrowAsJavaScriptException();
		return false;
	}

	Napi::Object obj = value.As<Napi::Object>();
	if (!common::ReadStringFromValue(env, obj.Get("key"), "entries[].key", &out->key)) {
		return false;
	}

	Napi::Value data = obj.Get("value");
	if (data.IsUndefined() || data.IsNull()) {
		out->isDelete = true;
		out->value.clear();
		return true;
	}

	out->isDelete = false;
	return common::CopyBytesFromValue(env, data, "entries[].value", &out->value);
}

NativeKVStore::NativeKVStore(const Napi::CallbackInfo& info) : Napi::ObjectWrap<NativeKVStore>(info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1) {
		common::native_error_log::ThrowType(
			env,
			"storage.kv",
			"constructor",
			"NativeKVStore(path, [options]) requires a path"
		);
		return;
	}
	if (!common::ReadStringFromValue(env, info[0], "path", &path_)) {
		return;
	}

	if (info.Length() > 1 && !info[1].IsUndefined() && !info[1].IsNull()) {
		if (!info[1].IsObject()) {
			common::native_error_log::ThrowType(env, "storage.kv", "constructor", "options must be an object");
			return;
		}

		Napi::Object options = info[1].As<Napi::Object>();
		Napi::Value thresholdValue = options.Get("compactThresholdBytes");
		if (!thresholdValue.IsUndefined() && !thresholdValue.IsNull()) {
			uint32_t parsedThreshold = 0;
			if (!common::ReadUInt32FromValue(env, thresholdValue, "compactThresholdBytes", &parsedThreshold)) {
				return;
			}
			if (parsedThreshold == 0) {
				common::native_error_log::ThrowRange(
					env,
					"storage.kv",
					"constructor",
					"compactThresholdBytes must be > 0"
				);
				return;
			}
			compactThresholdBytes_ = parsedThreshold;
		}

		Napi::Value ratioValue = options.Get("compactRatio");
		if (!ratioValue.IsUndefined() && !ratioValue.IsNull()) {
			double parsedRatio = 0.0;
			if (!common::ReadDoubleFromValue(env, ratioValue, "compactRatio", &parsedRatio)) {
				return;
			}
			if (parsedRatio < 1.0) {
				common::native_error_log::ThrowRange(
					env,
					"storage.kv",
					"constructor",
					"compactRatio must be >= 1"
				);
				return;
			}
			compactRatio_ = parsedRatio;
		}

		Napi::Value maxQueueValue = options.Get("maxQueuedBytes");
		if (!maxQueueValue.IsUndefined() && !maxQueueValue.IsNull()) {
			uint32_t parsedMaxQueue = 0;
			if (!common::ReadUInt32FromValue(env, maxQueueValue, "maxQueuedBytes", &parsedMaxQueue)) {
				return;
			}
			if (parsedMaxQueue == 0) {
				common::native_error_log::ThrowRange(
					env,
					"storage.kv",
					"constructor",
					"maxQueuedBytes must be > 0"
				);
				return;
			}
			maxQueuedAppendBytes_ = parsedMaxQueue;
		}
	}

	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!LoadFromDiskLocked()) {
			common::native_error_log::ThrowError(
				env,
				"storage.kv",
				"constructor.load_from_disk",
				"Failed to initialize NativeKVStore"
			);
			return;
		}
	}

	writerThread_ = std::thread(&NativeKVStore::WriterThreadMain, this);
}

NativeKVStore::~NativeKVStore() {
	{
		std::lock_guard<std::mutex> lock(mutex_);
		writerStop_ = true;
	}
	writerCv_.notify_all();
	writerDrainCv_.notify_all();
	if (writerThread_.joinable()) {
		writerThread_.join();
	}
}

bool NativeKVStore::EnsureParentDirectoryLocked() {
	std::error_code ec;
	const std::filesystem::path fsPath(path_);
	const std::filesystem::path parent = fsPath.parent_path();
	if (parent.empty()) return true;
	std::filesystem::create_directories(parent, ec);
	return !ec;
}

bool NativeKVStore::WriteEmptyStoreLocked(const std::string& targetPath) {
	std::ofstream out(targetPath, std::ios::binary | std::ios::trunc);
	if (!out) return false;
	out.write(kStoreMagic, static_cast<std::streamsize>(kMagicSize));
	return static_cast<bool>(out);
}

void NativeKVStore::ApplyPutToMemory(const std::string& key, std::vector<uint8_t>&& value) {
	const size_t keyLen = key.size();
	auto existing = data_.find(key);
	if (existing != data_.end()) {
		liveBytes_ -= RecordSize(keyLen, existing->second.size());
		existing->second = std::move(value);
		liveBytes_ += RecordSize(keyLen, existing->second.size());
		return;
	}

	liveBytes_ += RecordSize(keyLen, value.size());
	data_.emplace(key, std::move(value));
}

void NativeKVStore::ApplyDeleteToMemory(const std::string& key) {
	auto existing = data_.find(key);
	if (existing == data_.end()) return;
	liveBytes_ -= RecordSize(key.size(), existing->second.size());
	data_.erase(existing);
}

void NativeKVStore::ApplyMutationsToMemory(std::vector<Mutation>& mutations) {
	for (auto& mutation : mutations) {
		if (mutation.isDelete) {
			ApplyDeleteToMemory(mutation.key);
		} else {
			ApplyPutToMemory(mutation.key, std::move(mutation.value));
		}
	}
}

bool NativeKVStore::EncodeMutations(const std::vector<Mutation>& mutations, std::vector<uint8_t>* out) {
	out->clear();

	size_t totalAppendBytes = 0;
	for (const auto& mutation : mutations) {
		const size_t valueLen = mutation.isDelete ? 0 : mutation.value.size();
		const size_t record = RecordSize(mutation.key.size(), valueLen);
		if (AddWouldOverflowSizeT(totalAppendBytes, record)) {
			return false;
		}
		totalAppendBytes += record;
	}
	if (totalAppendBytes == 0) {
		return true;
	}

	out->reserve(totalAppendBytes);

	for (const auto& mutation : mutations) {
		if (mutation.key.size() > std::numeric_limits<uint32_t>::max()) return false;
		if (!mutation.isDelete && mutation.value.size() > std::numeric_limits<uint32_t>::max()) return false;

		const uint32_t keyLen = static_cast<uint32_t>(mutation.key.size());
		const uint32_t valueLen = mutation.isDelete ? 0u : static_cast<uint32_t>(mutation.value.size());

		uint8_t header[kRecordHeaderSize];
		header[0] = mutation.isDelete ? kOpDelete : kOpPut;
		WriteU32LE(keyLen, header + 1);
		WriteU32LE(valueLen, header + 5);
		if (!common::safe_copy::AppendBytes(out, header, kRecordHeaderSize)) {
			return false;
		}
		if (!common::safe_copy::AppendBytes(
				out,
				reinterpret_cast<const uint8_t*>(mutation.key.data()),
				keyLen
			)) {
			return false;
		}
		if (!mutation.isDelete &&
			!common::safe_copy::AppendBytes(out, mutation.value.data(), valueLen)) {
			return false;
		}
	}

	return true;
}

bool NativeKVStore::AppendEncodedToDisk(const std::vector<uint8_t>& encoded) {
	if (encoded.empty()) return true;
	std::ofstream file(path_, std::ios::binary | std::ios::app);
	if (!file) return false;
	file.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
	return static_cast<bool>(file);
}

bool NativeKVStore::RewriteAllDataLocked(const std::string& targetPath) {
	return RewriteSnapshotToPath(targetPath, data_);
}

bool NativeKVStore::CompactLocked() {
	const KVDataSnapshot snapshot = data_;
	size_t compactedBytes = 0;
	if (!CompactPathFromSnapshot(path_, snapshot, &compactedBytes)) {
		return false;
	}
	fileBytes_ = compactedBytes;
	return true;
}

bool NativeKVStore::ShouldScheduleCompactionLocked() const {
	size_t projectedFileBytes = fileBytes_;
	if (AddWouldOverflowSizeT(projectedFileBytes, queuedAppendBytes_)) {
		return true;
	}
	projectedFileBytes += queuedAppendBytes_;

	if (projectedFileBytes <= compactThresholdBytes_) return false;
	if (liveBytes_ == 0) return true;
	return static_cast<double>(projectedFileBytes) >= static_cast<double>(liveBytes_) * compactRatio_;
}

void NativeKVStore::UpdateQueueMetricsLocked() {
	if (writerTasks_.size() > writerMetrics_.maxQueueDepth) {
		writerMetrics_.maxQueueDepth = writerTasks_.size();
	}
	if (queuedAppendBytes_ > writerMetrics_.maxQueuedAppendBytes) {
		writerMetrics_.maxQueuedAppendBytes = queuedAppendBytes_;
	}
}

void NativeKVStore::ObserveWriterTaskLatencyLocked(double latencyMs) {
	writerMetrics_.writerTasksProcessed += 1;
	writerMetrics_.lastTaskLatencyMs = latencyMs;
	if (latencyMs > writerMetrics_.maxTaskLatencyMs) {
		writerMetrics_.maxTaskLatencyMs = latencyMs;
	}
}

void NativeKVStore::EnqueueCompactionLocked() {
	if (compactQueued_) return;
	compactQueued_ = true;
	writerTasks_.push_back(WriterTask{WriterTaskType::Compact, {}});
	writerMetrics_.compactionTasksEnqueued += 1;
	UpdateQueueMetricsLocked();
	writerCv_.notify_one();
}

bool NativeKVStore::EnqueueAppendLocked(std::vector<uint8_t>&& encoded) {
	if (encoded.empty()) return true;
	size_t projectedQueued = 0;
	if (!common::safe_copy::CheckedAddSize(queuedAppendBytes_, encoded.size(), &projectedQueued) ||
		projectedQueued > maxQueuedAppendBytes_) {
		writerMetrics_.queueOverflowCount += 1;
		return false;
	}
	queuedAppendBytes_ = projectedQueued;
	writerTasks_.push_back(WriterTask{WriterTaskType::Append, std::move(encoded)});
	writerMetrics_.appendTasksEnqueued += 1;
	UpdateQueueMetricsLocked();
	writerCv_.notify_one();
	return true;
}

bool NativeKVStore::WaitForWriterDrainLocked(std::unique_lock<std::mutex>* lock) {
	const auto waitStart = std::chrono::steady_clock::now();
	writerDrainCv_.wait(*lock, [this]() { return !writerBusy_ && writerTasks_.empty(); });
	const double waitMs =
		std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - waitStart).count();
	writerMetrics_.drainWaitCount += 1;
	writerMetrics_.lastDrainWaitMs = waitMs;
	if (waitMs > writerMetrics_.maxDrainWaitMs) {
		writerMetrics_.maxDrainWaitMs = waitMs;
	}
	return writerError_.empty();
}

bool NativeKVStore::EnsureWriterHealthyLocked(const Napi::Env& env) {
	if (writerError_.empty()) {
		return true;
	}
	common::native_error_log::ThrowError(
		env,
		"storage.kv",
		"writer.health",
		std::string("NativeKVStore writer failed: ") + writerError_
	);
	return false;
}

void NativeKVStore::WriterThreadMain() {
	while (true) {
		WriterTask task;
		auto taskStart = std::chrono::steady_clock::now();
		{
			std::unique_lock<std::mutex> lock(mutex_);
			writerCv_.wait(lock, [this]() { return writerStop_ || !writerTasks_.empty(); });

			if (writerStop_ && writerTasks_.empty()) {
				break;
			}
			if (writerTasks_.empty()) {
				continue;
			}

			task = std::move(writerTasks_.front());
			writerTasks_.pop_front();
			if (task.type == WriterTaskType::Append) {
				queuedAppendBytes_ -= task.encoded.size();
			} else {
				compactQueued_ = false;
			}
			writerBusy_ = true;
			taskStart = std::chrono::steady_clock::now();
		}

		bool ok = true;
		std::string error;
		size_t compactedBytes = 0;

		if (task.type == WriterTaskType::Append) {
			if (!AppendEncodedToDisk(task.encoded)) {
				ok = false;
				error = "append write failed";
			}
		} else {
			KVDataSnapshot snapshot;
			std::string path;
			{
				std::lock_guard<std::mutex> lock(mutex_);
				snapshot = data_;
				path = path_;
			}

			if (!CompactPathFromSnapshot(path, snapshot, &compactedBytes)) {
				ok = false;
				error = "compaction failed";
			}
		}

		{
			std::lock_guard<std::mutex> lock(mutex_);
			const double taskLatencyMs =
				std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - taskStart).count();
			ObserveWriterTaskLatencyLocked(taskLatencyMs);

			if (ok) {
				if (task.type == WriterTaskType::Append) {
					if (!AddWouldOverflowSizeT(fileBytes_, task.encoded.size())) {
						fileBytes_ += task.encoded.size();
					} else {
						ok = false;
						error = "file size overflow after append";
					}
				} else {
					fileBytes_ = compactedBytes;
				}
			}

			if (!ok && writerError_.empty()) {
				common::native_error_log::LogError(
					"storage.kv",
					task.type == WriterTaskType::Append ? "writer.append" : "writer.compact",
					error
				);
				writerError_ = error;
				writerTasks_.clear();
				queuedAppendBytes_ = 0;
				compactQueued_ = false;
				writerMetrics_.persistentErrorCount += 1;
			}

			writerBusy_ = false;
		}

		writerDrainCv_.notify_all();
		if (!ok) {
			return;
		}
	}

	{
		std::lock_guard<std::mutex> lock(mutex_);
		writerBusy_ = false;
	}
	writerDrainCv_.notify_all();
}

bool NativeKVStore::LoadFromDiskLocked() {
	data_.clear();
	liveBytes_ = 0;
	fileBytes_ = 0;
	queuedAppendBytes_ = 0;
	compactQueued_ = false;
	writerError_.clear();
	writerMetrics_ = {};

	if (!EnsureParentDirectoryLocked()) {
		return false;
	}

	std::error_code existsEc;
	const bool exists = std::filesystem::exists(path_, existsEc);
	if (existsEc) return false;

	if (!exists) {
		if (!WriteEmptyStoreLocked(path_)) return false;
	}

	std::ifstream in(path_, std::ios::binary);
	if (!in) return false;

	char magic[kMagicSize];
	in.read(magic, static_cast<std::streamsize>(kMagicSize));
	if (!in) {
		if (!WriteEmptyStoreLocked(path_)) return false;
		fileBytes_ = kMagicSize;
		return true;
	}
	if (std::memcmp(magic, kStoreMagic, kMagicSize) != 0) {
		return false;
	}

	std::error_code sizeEc;
	const uintmax_t rawFileSize = std::filesystem::file_size(path_, sizeEc);
	if (sizeEc || rawFileSize < kMagicSize || rawFileSize > static_cast<uintmax_t>(std::numeric_limits<size_t>::max())) {
		return false;
	}

	fileBytes_ = static_cast<size_t>(rawFileSize);
	size_t remainingBytes = fileBytes_ - kMagicSize;
	bool hasTrailingGarbage = false;

	while (remainingBytes > 0) {
		if (remainingBytes < kRecordHeaderSize) {
			hasTrailingGarbage = true;
			break;
		}

		uint8_t op = 0;
		in.read(reinterpret_cast<char*>(&op), 1);
		if (!in) {
			hasTrailingGarbage = true;
			break;
		}

		uint8_t lenBytes[8];
		in.read(reinterpret_cast<char*>(lenBytes), 8);
		if (!in) {
			hasTrailingGarbage = true;
			break;
		}
		remainingBytes -= kRecordHeaderSize;

		const uint32_t keyLen = ReadU32LE(lenBytes);
		const uint32_t valueLen = ReadU32LE(lenBytes + 4);
		if (keyLen == 0 || keyLen > kMaxKeyLengthBytes || valueLen > kMaxValueLengthBytes) {
			hasTrailingGarbage = true;
			break;
		}

		if (AddWouldOverflowSizeT(static_cast<size_t>(keyLen), static_cast<size_t>(valueLen))) {
			hasTrailingGarbage = true;
			break;
		}
		const size_t payloadSize = static_cast<size_t>(keyLen) + static_cast<size_t>(valueLen);
		if (payloadSize > remainingBytes) {
			hasTrailingGarbage = true;
			break;
		}

		std::string key;
		key.resize(keyLen);
		in.read(key.data(), static_cast<std::streamsize>(keyLen));
		if (!in) {
			hasTrailingGarbage = true;
			break;
		}

		if (op == kOpPut) {
			std::vector<uint8_t> value;
			value.resize(valueLen);
			if (valueLen > 0) {
				in.read(reinterpret_cast<char*>(value.data()), static_cast<std::streamsize>(valueLen));
				if (!in) {
					hasTrailingGarbage = true;
					break;
				}
			}
			ApplyPutToMemory(key, std::move(value));
		} else if (op == kOpDelete) {
			if (valueLen > 0) {
				in.ignore(static_cast<std::streamsize>(valueLen));
				if (!in) {
					hasTrailingGarbage = true;
					break;
				}
			}
			ApplyDeleteToMemory(key);
		} else {
			hasTrailingGarbage = true;
			break;
		}

		remainingBytes -= payloadSize;
	}

	if (hasTrailingGarbage) {
		if (!CompactLocked()) {
			return false;
		}
	}

	return true;
}

} // namespace baileys_native::storage_internal
