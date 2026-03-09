#include "storage/internal/native_kv_store_object.h"

#include "common.h"
#include "common/native_error_log.h"

#include <string>
#include <utility>
#include <vector>

namespace baileys_native::storage_internal {

Napi::Function NativeKVStore::Init(Napi::Env env, Napi::Object exports) {
	Napi::Function func = DefineClass(
		env,
		"NativeKVStore",
		{
			InstanceMethod("get", &NativeKVStore::Get),
			InstanceMethod("getMany", &NativeKVStore::GetMany),
			InstanceMethod("setMany", &NativeKVStore::SetMany),
			InstanceMethod("deleteMany", &NativeKVStore::DeleteMany),
			InstanceMethod("compact", &NativeKVStore::Compact),
			InstanceMethod("clear", &NativeKVStore::Clear),
			InstanceMethod("size", &NativeKVStore::Size)
		}
	);

	constructor_ = Napi::Persistent(func);
	constructor_.SuppressDestruct();
	exports.Set("NativeKVStore", func);
	return func;
}

Napi::Value NativeKVStore::Get(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1) {
		return common::native_error_log::ThrowTypeValue(env, "storage.kv", "get", "get(key) requires key");
	}

	std::string key;
	if (!common::ReadStringFromValue(env, info[0], "key", &key)) {
		return env.Null();
	}

	std::lock_guard<std::mutex> lock(mutex_);
	const auto it = data_.find(key);
	if (it == data_.end()) return env.Null();

	return Napi::Buffer<uint8_t>::Copy(env, it->second.data(), it->second.size());
}

Napi::Value NativeKVStore::GetMany(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1 || !info[0].IsArray()) {
		return common::native_error_log::ThrowTypeValue(
			env,
			"storage.kv",
			"getMany",
			"getMany(keys) requires a keys array"
		);
	}

	Napi::Array keys = info[0].As<Napi::Array>();
	Napi::Array out = Napi::Array::New(env, keys.Length());

	std::vector<std::string> parsedKeys;
	parsedKeys.reserve(keys.Length());
	for (uint32_t i = 0; i < keys.Length(); ++i) {
		std::string key;
		if (!common::ReadStringFromValue(env, keys.Get(i), "keys[]", &key)) {
			return env.Null();
		}
		parsedKeys.push_back(std::move(key));
	}

	std::lock_guard<std::mutex> lock(mutex_);
	for (uint32_t i = 0; i < parsedKeys.size(); ++i) {
		const auto it = data_.find(parsedKeys[i]);
		if (it == data_.end()) {
			out.Set(i, env.Null());
		} else {
			out.Set(i, Napi::Buffer<uint8_t>::Copy(env, it->second.data(), it->second.size()));
		}
	}

	return out;
}

Napi::Value NativeKVStore::SetMany(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1 || !info[0].IsArray()) {
		return common::native_error_log::ThrowTypeValue(
			env,
			"storage.kv",
			"setMany",
			"setMany(entries) requires an entries array"
		);
	}

	Napi::Array entries = info[0].As<Napi::Array>();
	std::vector<Mutation> mutations;
	mutations.reserve(entries.Length());

	for (uint32_t i = 0; i < entries.Length(); ++i) {
		Mutation mutation;
		if (!ParseMutationFromValue(env, entries.Get(i), &mutation)) {
			return env.Null();
		}
		mutations.push_back(std::move(mutation));
	}

	std::vector<uint8_t> encoded;
	if (!EncodeMutations(mutations, &encoded)) {
		return common::native_error_log::ThrowErrorValue(
			env,
			"storage.kv",
			"setMany.encode",
			"NativeKVStore failed to encode entries"
		);
	}

	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!EnsureWriterHealthyLocked(env)) {
			return env.Null();
		}

		if (!EnqueueAppendLocked(std::move(encoded))) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"storage.kv",
				"setMany.enqueue",
				"NativeKVStore write queue overflow"
			);
		}
		ApplyMutationsToMemory(mutations);
		if (ShouldScheduleCompactionLocked()) {
			EnqueueCompactionLocked();
		}
	}

	return env.Undefined();
}

Napi::Value NativeKVStore::DeleteMany(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1 || !info[0].IsArray()) {
		return common::native_error_log::ThrowTypeValue(
			env,
			"storage.kv",
			"deleteMany",
			"deleteMany(keys) requires a keys array"
		);
	}

	Napi::Array keys = info[0].As<Napi::Array>();
	std::vector<Mutation> mutations;
	mutations.reserve(keys.Length());

	for (uint32_t i = 0; i < keys.Length(); ++i) {
		std::string key;
		if (!common::ReadStringFromValue(env, keys.Get(i), "keys[]", &key)) {
			return env.Null();
		}
		Mutation mutation;
		mutation.key = std::move(key);
		mutation.isDelete = true;
		mutations.push_back(std::move(mutation));
	}

	std::vector<uint8_t> encoded;
	if (!EncodeMutations(mutations, &encoded)) {
		return common::native_error_log::ThrowErrorValue(
			env,
			"storage.kv",
			"deleteMany.encode",
			"NativeKVStore failed to encode delete entries"
		);
	}

	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!EnsureWriterHealthyLocked(env)) {
			return env.Null();
		}

		if (!EnqueueAppendLocked(std::move(encoded))) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"storage.kv",
				"deleteMany.enqueue",
				"NativeKVStore write queue overflow"
			);
		}
		ApplyMutationsToMemory(mutations);
		if (ShouldScheduleCompactionLocked()) {
			EnqueueCompactionLocked();
		}
	}

	return env.Undefined();
}

Napi::Value NativeKVStore::Compact(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	std::unique_lock<std::mutex> lock(mutex_);
	if (!EnsureWriterHealthyLocked(env)) {
		return env.Null();
	}

	EnqueueCompactionLocked();
	if (!WaitForWriterDrainLocked(&lock)) {
		EnsureWriterHealthyLocked(env);
		return env.Null();
	}
	return Napi::Boolean::New(env, true);
}

Napi::Value NativeKVStore::Clear(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	std::unique_lock<std::mutex> lock(mutex_);
	if (!EnsureWriterHealthyLocked(env)) {
		return env.Null();
	}
	if (!WaitForWriterDrainLocked(&lock)) {
		EnsureWriterHealthyLocked(env);
		return env.Null();
	}

	data_.clear();
	liveBytes_ = 0;
	if (!WriteEmptyStoreLocked(path_)) {
		return common::native_error_log::ThrowErrorValue(
			env,
			"storage.kv",
			"clear.write",
			"NativeKVStore clear failed"
		);
	}
	fileBytes_ = kMagicSize;
	queuedAppendBytes_ = 0;
	writerTasks_.clear();
	compactQueued_ = false;
	return env.Undefined();
}

Napi::Value NativeKVStore::Size(const Napi::CallbackInfo& info) {
	std::lock_guard<std::mutex> lock(mutex_);
	return Napi::Number::New(info.Env(), static_cast<double>(data_.size()));
}

Napi::FunctionReference NativeKVStore::constructor_;

} // namespace baileys_native::storage_internal
