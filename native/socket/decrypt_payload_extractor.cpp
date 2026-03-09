#include "decrypt_payload_extractor.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

namespace baileys_native {

namespace {

bool ReadStringLike(const Napi::Value& value, std::string* out) {
	if (value.IsUndefined() || value.IsNull()) {
		return false;
	}

	if (!value.IsString() && !value.IsNumber() && !value.IsBoolean()) {
		return false;
	}

	*out = value.ToString().Utf8Value();
	return true;
}

double ParseNumberLikeJs(const std::string& value) {
	if (value.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}

	errno = 0;
	char* endPtr = nullptr;
	const double parsed = std::strtod(value.c_str(), &endPtr);
	if (endPtr == value.c_str()) {
		return std::numeric_limits<double>::quiet_NaN();
	}

	while (*endPtr != '\0') {
		if (*endPtr != ' ' && *endPtr != '\t' && *endPtr != '\n' && *endPtr != '\r' && *endPtr != '\f' &&
			*endPtr != '\v') {
			return std::numeric_limits<double>::quiet_NaN();
		}
		++endPtr;
	}

	if (errno == ERANGE) {
		return parsed > 0 ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity();
	}

	return parsed;
}

bool CopyBytesFromValue(const Napi::Env& env, const Napi::Value& value, Napi::Value* out) {
	if (value.IsBuffer()) {
		const Napi::Buffer<uint8_t> input = value.As<Napi::Buffer<uint8_t>>();
		*out = Napi::Buffer<uint8_t>::Copy(env, input.Data(), input.Length());
		return true;
	}

	if (value.IsTypedArray()) {
		const Napi::Uint8Array input = value.As<Napi::Uint8Array>();
		*out = Napi::Buffer<uint8_t>::Copy(env, input.Data(), input.ByteLength());
		return true;
	}

	return false;
}

} // namespace

Napi::Value ExtractDecryptPayloadsFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1 || !info[0].IsObject()) {
		return env.Null();
	}

	const Napi::Object stanza = info[0].As<Napi::Object>();
	const Napi::Value contentValue = stanza.Get("content");
	if (!contentValue.IsArray()) {
		return env.Null();
	}

	const Napi::Array content = contentValue.As<Napi::Array>();
	Napi::Array payloads = Napi::Array::New(env);
	uint32_t payloadCount = 0;
	bool hasViewOnceUnavailable = false;
	Napi::Value retryCountValue = env.Undefined();

	for (uint32_t i = 0; i < content.Length(); ++i) {
		const Napi::Value childValue = content.Get(i);
		if (!childValue.IsObject()) {
			continue;
		}

		const Napi::Object child = childValue.As<Napi::Object>();
		std::string tag;
		if (!ReadStringLike(child.Get("tag"), &tag) || tag.empty()) {
			continue;
		}

		const Napi::Value attrsValue = child.Get("attrs");
		Napi::Object attrs = attrsValue.IsObject() ? attrsValue.As<Napi::Object>() : Napi::Object::New(env);

		if (tag == "unavailable") {
			std::string unavailableType;
			ReadStringLike(attrs.Get("type"), &unavailableType);
			if (unavailableType == "view_once") {
				hasViewOnceUnavailable = true;
			}
			continue;
		}

		if (tag == "enc") {
			std::string countRaw;
			if (ReadStringLike(attrs.Get("count"), &countRaw)) {
				const double parsedCount = ParseNumberLikeJs(countRaw);
				if (!std::isnan(parsedCount) && std::isfinite(parsedCount)) {
					retryCountValue = Napi::Number::New(env, parsedCount);
				}
			}
		}

		if (tag != "enc" && tag != "plaintext") {
			continue;
		}

		Napi::Value payloadBufferValue = env.Undefined();
		if (!CopyBytesFromValue(env, child.Get("content"), &payloadBufferValue)) {
			continue;
		}

		std::string type;
		if (tag == "plaintext") {
			type = "plaintext";
		} else if (!ReadStringLike(attrs.Get("type"), &type) || type.empty()) {
			type = "unknown";
		}

		Napi::Object payload = Napi::Object::New(env);
		payload.Set("messageType", type);
		payload.Set("content", payloadBufferValue);
		payload.Set("padded", Napi::Boolean::New(env, type != "plaintext"));
		payloads.Set(payloadCount++, payload);
	}

	Napi::Object result = Napi::Object::New(env);
	result.Set("payloads", payloads);
	result.Set("hasViewOnceUnavailable", Napi::Boolean::New(env, hasViewOnceUnavailable));
	if (!retryCountValue.IsUndefined()) {
		result.Set("retryCount", retryCountValue);
	}

	return result;
}

} // namespace baileys_native
