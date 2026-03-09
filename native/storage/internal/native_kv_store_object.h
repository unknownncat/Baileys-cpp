#pragma once

#include <napi.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace baileys_native::storage_internal {

inline constexpr uint8_t kOpPut = 1;
inline constexpr uint8_t kOpDelete = 2;
inline constexpr size_t kRecordHeaderSize = 1 + 4 + 4;
inline constexpr size_t kMagicSize = 4;
inline constexpr char kStoreMagic[kMagicSize] = {'B', 'K', 'V', '1'};
inline constexpr size_t kDefaultCompactThresholdBytes = 32u * 1024u * 1024u;
inline constexpr double kDefaultCompactRatio = 2.0;
inline constexpr size_t kDefaultMaxQueuedAppendBytes = 64u * 1024u * 1024u;

inline size_t RecordSize(size_t keyLen, size_t valueLen) {
	return kRecordHeaderSize + keyLen + valueLen;
}

inline void WriteU32LE(uint32_t value, uint8_t* out) {
	out[0] = static_cast<uint8_t>(value & 0xffu);
	out[1] = static_cast<uint8_t>((value >> 8u) & 0xffu);
	out[2] = static_cast<uint8_t>((value >> 16u) & 0xffu);
	out[3] = static_cast<uint8_t>((value >> 24u) & 0xffu);
}

inline uint32_t ReadU32LE(const uint8_t* in) {
	return static_cast<uint32_t>(in[0]) | (static_cast<uint32_t>(in[1]) << 8u) |
		(static_cast<uint32_t>(in[2]) << 16u) | (static_cast<uint32_t>(in[3]) << 24u);
}

struct Mutation {
	std::string key;
	bool isDelete = false;
	std::vector<uint8_t> value;
};

bool ParseMutationFromValue(const Napi::Env& env, const Napi::Value& value, Mutation* out);

class NativeKVStore : public Napi::ObjectWrap<NativeKVStore> {
public:
	static Napi::Function Init(Napi::Env env, Napi::Object exports);

	explicit NativeKVStore(const Napi::CallbackInfo& info);
	~NativeKVStore() override;

	static Napi::FunctionReference constructor_;

private:
	enum class WriterTaskType {
		Append,
		Compact
	};

	struct WriterTask {
		WriterTaskType type = WriterTaskType::Append;
		std::vector<uint8_t> encoded;
	};

	struct WriterMetrics {
		uint64_t appendTasksEnqueued = 0;
		uint64_t compactionTasksEnqueued = 0;
		uint64_t writerTasksProcessed = 0;
		uint64_t drainWaitCount = 0;
		uint64_t queueOverflowCount = 0;
		uint64_t persistentErrorCount = 0;
		size_t maxQueueDepth = 0;
		size_t maxQueuedAppendBytes = 0;
		double lastTaskLatencyMs = 0.0;
		double maxTaskLatencyMs = 0.0;
		double lastDrainWaitMs = 0.0;
		double maxDrainWaitMs = 0.0;
	};

	bool EnsureParentDirectoryLocked();
	bool WriteEmptyStoreLocked(const std::string& targetPath);
	void ApplyPutToMemory(const std::string& key, std::vector<uint8_t>&& value);
	void ApplyDeleteToMemory(const std::string& key);
	void ApplyMutationsToMemory(std::vector<Mutation>& mutations);
	bool EncodeMutations(const std::vector<Mutation>& mutations, std::vector<uint8_t>* out);
	bool AppendEncodedToDisk(const std::vector<uint8_t>& encoded);
	bool RewriteAllDataLocked(const std::string& targetPath);
	bool CompactLocked();
	bool ShouldScheduleCompactionLocked() const;
	void EnqueueCompactionLocked();
	bool EnqueueAppendLocked(std::vector<uint8_t>&& encoded);
	bool WaitForWriterDrainLocked(std::unique_lock<std::mutex>* lock);
	bool EnsureWriterHealthyLocked(const Napi::Env& env);
	void UpdateQueueMetricsLocked();
	void ObserveWriterTaskLatencyLocked(double latencyMs);
	void WriterThreadMain();
	bool LoadFromDiskLocked();

	Napi::Value Get(const Napi::CallbackInfo& info);
	Napi::Value GetMany(const Napi::CallbackInfo& info);
	Napi::Value SetMany(const Napi::CallbackInfo& info);
	Napi::Value DeleteMany(const Napi::CallbackInfo& info);
	Napi::Value Compact(const Napi::CallbackInfo& info);
	Napi::Value Clear(const Napi::CallbackInfo& info);
	Napi::Value Size(const Napi::CallbackInfo& info);

	std::mutex mutex_;
	std::string path_;
	size_t compactThresholdBytes_ = kDefaultCompactThresholdBytes;
	double compactRatio_ = kDefaultCompactRatio;
	size_t maxQueuedAppendBytes_ = kDefaultMaxQueuedAppendBytes;
	size_t fileBytes_ = 0;
	size_t liveBytes_ = 0;
	size_t queuedAppendBytes_ = 0;
	std::unordered_map<std::string, std::vector<uint8_t>> data_;

	std::deque<WriterTask> writerTasks_;
	std::thread writerThread_;
	std::condition_variable writerCv_;
	std::condition_variable writerDrainCv_;
	bool writerStop_ = false;
	bool writerBusy_ = false;
	bool compactQueued_ = false;
	std::string writerError_;
	WriterMetrics writerMetrics_{};
};

} // namespace baileys_native::storage_internal
