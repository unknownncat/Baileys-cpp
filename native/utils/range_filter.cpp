#include "range_filter.h"

#include "common.h"

#include <cstdint>
#include <cstring>
#include <limits>

namespace baileys_native {

namespace {

class NativeRangeFilter : public Napi::ObjectWrap<NativeRangeFilter> {
public:
	static Napi::Function Init(Napi::Env env, Napi::Object exports) {
		Napi::Function func = DefineClass(
			env,
			"NativeRangeFilter",
			{
				InstanceMethod("push", &NativeRangeFilter::Push),
				InstanceMethod("reset", &NativeRangeFilter::Reset),
				InstanceMethod("offset", &NativeRangeFilter::Offset)
			}
		);

		constructor_ = Napi::Persistent(func);
		constructor_.SuppressDestruct();
		exports.Set("NativeRangeFilter", func);
		return func;
	}

	NativeRangeFilter(const Napi::CallbackInfo& info) : Napi::ObjectWrap<NativeRangeFilter>(info) {
		Napi::Env env = info.Env();
		if (info.Length() > 0 && !info[0].IsUndefined() && !info[0].IsNull()) {
			uint32_t parsed = 0;
			if (!common::ReadUInt32FromValue(env, info[0], "startByte", &parsed)) {
				return;
			}
			startByte_ = parsed;
			hasStart_ = true;
		}

		if (info.Length() > 1 && !info[1].IsUndefined() && !info[1].IsNull()) {
			uint32_t parsed = 0;
			if (!common::ReadUInt32FromValue(env, info[1], "endByte", &parsed)) {
				return;
			}
			endByte_ = parsed;
			hasEnd_ = true;
		}

		if (info.Length() > 2 && !info[2].IsUndefined() && !info[2].IsNull()) {
			uint32_t parsed = 0;
			if (!common::ReadUInt32FromValue(env, info[2], "initialOffset", &parsed)) {
				return;
			}
			bytesSeen_ = parsed;
		}
	}

private:
	static Napi::FunctionReference constructor_;

	uint64_t bytesSeen_ = 0;
	uint64_t startByte_ = 0;
	uint64_t endByte_ = 0;
	bool hasStart_ = false;
	bool hasEnd_ = false;

	Napi::Value Push(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		if (info.Length() < 1) {
			Napi::TypeError::New(env, "push(bytes) requires bytes").ThrowAsJavaScriptException();
			return env.Null();
		}

		common::ByteView input;
		if (!common::GetByteViewFromValue(env, info[0], "bytes", &input)) {
			return env.Null();
		}

		const uint64_t chunkStart = bytesSeen_;
		const uint64_t chunkEnd = bytesSeen_ + input.length;
		bytesSeen_ = chunkEnd;

		if (!hasStart_ && !hasEnd_) {
			return Napi::Buffer<uint8_t>::Copy(env, input.data, input.length);
		}

		const uint64_t wantedStart = hasStart_ ? startByte_ : 0;
		const uint64_t wantedEnd = hasEnd_ ? endByte_ : std::numeric_limits<uint64_t>::max();

		if (chunkEnd <= wantedStart || chunkStart >= wantedEnd) {
			return Napi::Buffer<uint8_t>::New(env, 0);
		}

		const uint64_t localStart = wantedStart > chunkStart ? wantedStart - chunkStart : 0;
		const uint64_t localEnd = wantedEnd < chunkEnd ? wantedEnd - chunkStart : input.length;
		if (localEnd <= localStart) {
			return Napi::Buffer<uint8_t>::New(env, 0);
		}

		const size_t outLen = static_cast<size_t>(localEnd - localStart);
		return Napi::Buffer<uint8_t>::Copy(env, input.data + static_cast<size_t>(localStart), outLen);
	}

	Napi::Value Reset(const Napi::CallbackInfo& info) {
		bytesSeen_ = 0;
		return info.Env().Undefined();
	}

	Napi::Value Offset(const Napi::CallbackInfo& info) {
		return Napi::Number::New(info.Env(), static_cast<double>(bytesSeen_));
	}
};

Napi::FunctionReference NativeRangeFilter::constructor_;

} // namespace

Napi::Function InitNativeRangeFilter(Napi::Env env, Napi::Object exports) {
	return NativeRangeFilter::Init(env, exports);
}

} // namespace baileys_native
