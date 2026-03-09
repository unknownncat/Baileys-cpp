#include "protobuf_fast_path.h"

#include "common.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace baileys_native {

Napi::Value PadMessageWithLength(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 2) {
		Napi::TypeError::New(env, "padMessageWithLength(data, padLength) requires data and padLength")
			.ThrowAsJavaScriptException();
		return env.Null();
	}

	common::ByteView data;
	if (!common::GetByteViewFromValue(env, info[0], "data", &data)) {
		return env.Null();
	}

	uint32_t padLength = 0;
	if (!common::ReadUInt32FromValue(env, info[1], "padLength", &padLength)) {
		return env.Null();
	}
	if (padLength == 0 || padLength > 16) {
		Napi::RangeError::New(env, "padLength must be between 1 and 16").ThrowAsJavaScriptException();
		return env.Null();
	}

	Napi::Buffer<uint8_t> out = Napi::Buffer<uint8_t>::New(env, data.length + static_cast<size_t>(padLength));
	if (data.length > 0) {
		std::memcpy(out.Data(), data.data, data.length);
	}
	std::memset(out.Data() + data.length, static_cast<int>(padLength), static_cast<size_t>(padLength));
	return out;
}

Napi::Value GetUnpaddedLengthMax16(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1) {
		Napi::TypeError::New(env, "getUnpaddedLengthMax16(data) requires data").ThrowAsJavaScriptException();
		return env.Null();
	}

	common::ByteView data;
	if (!common::GetByteViewFromValue(env, info[0], "data", &data)) {
		return env.Null();
	}
	if (data.length == 0) {
		Napi::Error::New(env, "unpadPkcs7 given empty bytes").ThrowAsJavaScriptException();
		return env.Null();
	}

	const uint8_t pad = data.data[data.length - 1];
	if (pad == 0 || static_cast<size_t>(pad) > data.length) {
		Napi::Error::New(
			env,
			std::string("unpad given ") + std::to_string(data.length) + " bytes, but pad is " + std::to_string(pad)
		)
			.ThrowAsJavaScriptException();
		return env.Null();
	}

	const size_t unpaddedLen = data.length - static_cast<size_t>(pad);
	return Napi::Number::New(env, static_cast<double>(unpaddedLen));
}

} // namespace baileys_native
