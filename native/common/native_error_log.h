#pragma once

#include <napi.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace baileys_native::common::native_error_log {

namespace detail {

inline bool IsEnabled() {
	const char* raw = std::getenv("BAILEYS_NATIVE_ERROR_LOG");
	if (raw == nullptr) {
		return false;
	}

	const std::string value(raw);
	return value == "1" || value == "true" || value == "TRUE" || value == "stderr" || value == "STDERR";
}

inline std::string Sanitize(std::string_view input) {
	std::string out;
	out.reserve(std::min<size_t>(input.size(), 240u));

	for (char ch : input) {
		if (out.size() >= 240u) {
			break;
		}

		const unsigned char uch = static_cast<unsigned char>(ch);
		if (uch < 0x20u || ch == '\x7f') {
			out.push_back(' ');
			continue;
		}

		out.push_back(ch);
	}

	return out;
}

inline void Emit(const char* component, const char* operation, std::string_view cause) {
	if (!IsEnabled()) {
		return;
	}

	const std::string safeCause = Sanitize(cause);
	std::fprintf(
		stderr,
		"[baileys-native][%s] op=%s cause=%s\n",
		component != nullptr ? component : "unknown",
		operation != nullptr ? operation : "unknown",
		safeCause.c_str()
	);
	std::fflush(stderr);
}

} // namespace detail

inline void LogError(const char* component, const char* operation, std::string_view cause) {
	detail::Emit(component, operation, cause);
}

inline void ThrowType(const Napi::Env& env, const char* component, const char* operation, std::string_view message) {
	LogError(component, operation, message);
	Napi::TypeError::New(env, std::string(message)).ThrowAsJavaScriptException();
}

inline void ThrowRange(const Napi::Env& env, const char* component, const char* operation, std::string_view message) {
	LogError(component, operation, message);
	Napi::RangeError::New(env, std::string(message)).ThrowAsJavaScriptException();
}

inline void ThrowError(const Napi::Env& env, const char* component, const char* operation, std::string_view message) {
	LogError(component, operation, message);
	Napi::Error::New(env, std::string(message)).ThrowAsJavaScriptException();
}

inline Napi::Value ThrowTypeValue(
	const Napi::Env& env,
	const char* component,
	const char* operation,
	std::string_view message
) {
	ThrowType(env, component, operation, message);
	return env.Null();
}

inline Napi::Value ThrowRangeValue(
	const Napi::Env& env,
	const char* component,
	const char* operation,
	std::string_view message
) {
	ThrowRange(env, component, operation, message);
	return env.Null();
}

inline Napi::Value ThrowErrorValue(
	const Napi::Env& env,
	const char* component,
	const char* operation,
	std::string_view message
) {
	ThrowError(env, component, operation, message);
	return env.Null();
}

} // namespace baileys_native::common::native_error_log
