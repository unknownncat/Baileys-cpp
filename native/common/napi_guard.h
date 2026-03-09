#pragma once

#include <napi.h>

#include <string>

namespace baileys_native::common::napi_guard {

inline bool ThrowType(const Napi::Env& env, const std::string& message) {
	Napi::TypeError::New(env, message).ThrowAsJavaScriptException();
	return false;
}

inline bool ThrowRange(const Napi::Env& env, const std::string& message) {
	Napi::RangeError::New(env, message).ThrowAsJavaScriptException();
	return false;
}

inline bool ThrowError(const Napi::Env& env, const std::string& message) {
	Napi::Error::New(env, message).ThrowAsJavaScriptException();
	return false;
}

inline Napi::Value ThrowTypeValue(const Napi::Env& env, const std::string& message) {
	Napi::TypeError::New(env, message).ThrowAsJavaScriptException();
	return env.Null();
}

inline Napi::Value ThrowRangeValue(const Napi::Env& env, const std::string& message) {
	Napi::RangeError::New(env, message).ThrowAsJavaScriptException();
	return env.Null();
}

inline Napi::Value ThrowErrorValue(const Napi::Env& env, const std::string& message) {
	Napi::Error::New(env, message).ThrowAsJavaScriptException();
	return env.Null();
}

inline bool RequireMinArgs(const Napi::CallbackInfo& info, size_t minArgs, const std::string& message) {
	if (info.Length() >= minArgs) {
		return true;
	}
	return ThrowType(info.Env(), message);
}

inline bool RequireArrayArg(const Napi::CallbackInfo& info, size_t index, const std::string& message) {
	if (info.Length() > index && info[index].IsArray()) {
		return true;
	}
	return ThrowType(info.Env(), message);
}

inline bool RequireObjectArg(const Napi::CallbackInfo& info, size_t index, const std::string& message) {
	if (info.Length() > index && info[index].IsObject()) {
		return true;
	}
	return ThrowType(info.Env(), message);
}

} // namespace baileys_native::common::napi_guard
