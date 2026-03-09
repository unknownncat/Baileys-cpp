#include "aligned_chunker.h"

#include "common.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace baileys_native {

namespace {

class NativeAlignedChunker : public Napi::ObjectWrap<NativeAlignedChunker> {
public:
	static Napi::Function Init(Napi::Env env, Napi::Object exports) {
		Napi::Function func = DefineClass(
			env,
			"NativeAlignedChunker",
			{
				InstanceMethod("push", &NativeAlignedChunker::Push),
				InstanceMethod("takeRemaining", &NativeAlignedChunker::TakeRemaining),
				InstanceMethod("size", &NativeAlignedChunker::Size),
				InstanceMethod("clear", &NativeAlignedChunker::Clear)
			}
		);

		constructor_ = Napi::Persistent(func);
		constructor_.SuppressDestruct();
		exports.Set("NativeAlignedChunker", func);
		return func;
	}

	NativeAlignedChunker(const Napi::CallbackInfo& info) : Napi::ObjectWrap<NativeAlignedChunker>(info) {
		Napi::Env env = info.Env();
		if (info.Length() > 0 && !info[0].IsUndefined() && !info[0].IsNull()) {
			uint32_t parsed = 0;
			if (!common::ReadUInt32FromValue(env, info[0], "blockSize", &parsed)) {
				return;
			}
			if (parsed == 0) {
				Napi::RangeError::New(env, "blockSize must be > 0").ThrowAsJavaScriptException();
				return;
			}
			blockSize_ = parsed;
		}
		remaining_.reserve(blockSize_);
	}

private:
	static Napi::FunctionReference constructor_;

	uint32_t blockSize_ = 16;
	std::vector<uint8_t> remaining_;

	Napi::Value Push(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		if (info.Length() < 1) {
			Napi::TypeError::New(env, "push(chunk) requires chunk").ThrowAsJavaScriptException();
			return env.Null();
		}

		common::ByteView chunk;
		if (!common::GetByteViewFromValue(env, info[0], "chunk", &chunk)) {
			return env.Null();
		}

		const size_t oldRemaining = remaining_.size();
		const size_t total = oldRemaining + chunk.length;
		const size_t blockSize = static_cast<size_t>(blockSize_);
		const size_t aligned = total - (total % blockSize);

		if (aligned == 0) {
			if (chunk.length > 0) {
				const size_t previous = remaining_.size();
				remaining_.resize(previous + chunk.length);
				std::memcpy(remaining_.data() + previous, chunk.data, chunk.length);
			}
			return Napi::Buffer<uint8_t>::New(env, 0);
		}

		Napi::Buffer<uint8_t> out = Napi::Buffer<uint8_t>::New(env, aligned);

		size_t copied = 0;
		if (oldRemaining > 0) {
			std::memcpy(out.Data(), remaining_.data(), oldRemaining);
			copied = oldRemaining;
		}

		const size_t needFromChunk = aligned - copied;
		if (needFromChunk > 0) {
			std::memcpy(out.Data() + copied, chunk.data, needFromChunk);
		}

		const size_t consumedFromChunk = needFromChunk;
		const size_t tailLength = chunk.length - consumedFromChunk;

		if (tailLength == 0) {
			remaining_.clear();
		} else {
			remaining_.resize(tailLength);
			std::memcpy(remaining_.data(), chunk.data + consumedFromChunk, tailLength);
		}

		return out;
	}

	Napi::Value TakeRemaining(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		const uint8_t* ptr = remaining_.empty() ? reinterpret_cast<const uint8_t*>("") : remaining_.data();
		Napi::Buffer<uint8_t> out = Napi::Buffer<uint8_t>::Copy(env, ptr, remaining_.size());
		remaining_.clear();
		return out;
	}

	Napi::Value Size(const Napi::CallbackInfo& info) {
		return Napi::Number::New(info.Env(), static_cast<double>(remaining_.size()));
	}

	Napi::Value Clear(const Napi::CallbackInfo& info) {
		remaining_.clear();
		return info.Env().Undefined();
	}
};

Napi::FunctionReference NativeAlignedChunker::constructor_;

} // namespace

Napi::Function InitNativeAlignedChunker(Napi::Env env, Napi::Object exports) {
	return NativeAlignedChunker::Init(env, exports);
}

} // namespace baileys_native
