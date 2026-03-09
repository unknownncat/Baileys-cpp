#include "zlib_helper.h"

#include "common.h"
#include "utils/zlib_inflate.h"

#include <cstdint>
#include <string>
#include <vector>

namespace baileys_native {

Napi::Value InflateZlibBuffer(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1) {
		Napi::TypeError::New(env, "inflateZlibBuffer(data) requires data").ThrowAsJavaScriptException();
		return env.Null();
	}

	common::ByteView input;
	if (!common::GetByteViewFromValue(env, info[0], "data", &input)) {
		return env.Null();
	}

	if (input.length == 0) {
		return Napi::Buffer<uint8_t>::New(env, 0);
	}

	std::vector<uint8_t> output;
	std::string inflateError;
	if (!utils::InflateZlibBytes(input.data, input.length, &output, &inflateError)) {
		Napi::Error::New(env, inflateError).ThrowAsJavaScriptException();
		return env.Null();
	}
	return common::MoveVectorToBuffer(env, std::move(output));
}

} // namespace baileys_native
