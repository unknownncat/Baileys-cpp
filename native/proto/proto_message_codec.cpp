#include "proto_message_codec.h"

#include "common.h"
#include "common/feature_gates.h"
#include "common/native_error_log.h"
#include "common/napi_guard.h"
#include "proto/internal/proto_message_codec_internal.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace baileys_native {

namespace proto_codec = proto_codec_internal;

Napi::Value InitProtoMessageCodec(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();

	if (info.Length() == 0) {
#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
		proto_codec::gUseNativeCodec = true;
		return Napi::Boolean::New(env, true);
#else
		if (!proto_codec::gProtoEncode.IsEmpty() && !proto_codec::gProtoDecode.IsEmpty()) {
			return Napi::Boolean::New(env, true);
		}
		return common::napi_guard::ThrowTypeValue(env, "initProtoMessageCodec requires encode/decode functions");
#endif
	}

	if (info.Length() < 2 || !info[0].IsFunction() || !info[1].IsFunction()) {
		return common::napi_guard::ThrowTypeValue(env, "initProtoMessageCodec(encodeFn, decodeFn) expects 2 functions");
	}

	proto_codec::gProtoEncode = Napi::Persistent(info[0].As<Napi::Function>());
	proto_codec::gProtoEncode.SuppressDestruct();
	proto_codec::gProtoDecode = Napi::Persistent(info[1].As<Napi::Function>());
	proto_codec::gProtoDecode.SuppressDestruct();
	proto_codec::gUseNativeCodec = false;
	return Napi::Boolean::New(env, true);
}

Napi::Value EncodeProtoMessageRaw(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireMinArgs(info, 1, "encodeProtoMessageRaw(message) requires message")) {
		return env.Null();
	}

	if (proto_codec::gUseNativeCodec) {
#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
		std::string encoded;
		if (!proto_codec::EncodeRawBytesNative(env, info[0], &encoded)) {
			return env.Null();
		}
		return Napi::Buffer<uint8_t>::Copy(env, reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size());
#else
		return common::native_error_log::ThrowErrorValue(
			env,
			"proto.codec",
			"encode_raw.unavailable",
			"native proto codec is unavailable in this build"
		);
#endif
	}

	if (!proto_codec::EnsureFallbackCodecReady(env)) {
		return env.Null();
	}

	common::ByteView encoded;
	if (!proto_codec::ReadEncodedMessageBytes(env, info[0], &encoded)) {
		return env.Null();
	}

	return Napi::Buffer<uint8_t>::Copy(env, encoded.data, encoded.length);
}

Napi::Value EncodeProtoMessageWithPad(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireMinArgs(
			info,
			2,
			"encodeProtoMessageWithPad(message, padLength) requires 2 arguments"
		)) {
		return env.Null();
	}

	uint32_t padLength = 0;
	if (!common::ReadUInt32FromValue(env, info[1], "padLength", &padLength)) {
		return env.Null();
	}
	if (padLength == 0 || padLength > 16) {
		return common::napi_guard::ThrowRangeValue(env, "padLength must be between 1 and 16");
	}

	std::string encodedStorage;
	common::ByteView encoded;
	if (proto_codec::gUseNativeCodec) {
#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
		if (!proto_codec::EncodeRawBytesNative(env, info[0], &encodedStorage)) {
			return env.Null();
		}
		encoded.data = reinterpret_cast<const uint8_t*>(encodedStorage.data());
		encoded.length = encodedStorage.size();
#else
		return common::native_error_log::ThrowErrorValue(
			env,
			"proto.codec",
			"encode_pad.unavailable",
			"native proto codec is unavailable in this build"
		);
#endif
	} else {
		if (!proto_codec::EnsureFallbackCodecReady(env)) {
			return env.Null();
		}
		if (!proto_codec::ReadEncodedMessageBytes(env, info[0], &encoded)) {
			return env.Null();
		}
	}

	const size_t totalLength = encoded.length + static_cast<size_t>(padLength);
	Napi::Buffer<uint8_t> out = Napi::Buffer<uint8_t>::New(env, totalLength);
	if (encoded.length > 0) {
		std::memcpy(out.Data(), encoded.data, encoded.length);
	}
	std::memset(out.Data() + encoded.length, static_cast<int>(padLength), static_cast<size_t>(padLength));
	return out;
}

Napi::Value DecodeProtoMessageRaw(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireMinArgs(info, 1, "decodeProtoMessageRaw(data) requires data")) {
		return env.Null();
	}

	common::ByteView data;
	if (!common::GetByteViewFromValue(env, info[0], "data", &data)) {
		return env.Null();
	}

	if (proto_codec::gUseNativeCodec) {
#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
		return proto_codec::DecodeRawBytesNative(env, data);
#else
		return common::native_error_log::ThrowErrorValue(
			env,
			"proto.codec",
			"decode_raw.unavailable",
			"native proto codec is unavailable in this build"
		);
#endif
	}

	if (!proto_codec::EnsureFallbackCodecReady(env)) {
		return env.Null();
	}

	return proto_codec::DecodeRawBytesFallback(env, data);
}

Napi::Value DecodeProtoMessageFromPadded(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireMinArgs(info, 1, "decodeProtoMessageFromPadded(data) requires data")) {
		return env.Null();
	}

	common::ByteView data;
	if (!common::GetByteViewFromValue(env, info[0], "data", &data)) {
		return env.Null();
	}
	common::ByteView unpadded;
	if (!proto_codec::GetUnpaddedView(env, data, &unpadded)) {
		return env.Null();
	}

	if (proto_codec::gUseNativeCodec) {
#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
		return proto_codec::DecodeRawBytesNative(env, unpadded);
#else
		return common::native_error_log::ThrowErrorValue(
			env,
			"proto.codec",
			"decode_padded.unavailable",
			"native proto codec is unavailable in this build"
		);
#endif
	}

	if (!proto_codec::EnsureFallbackCodecReady(env)) {
		return env.Null();
	}

	return proto_codec::DecodeRawBytesFallback(env, unpadded);
}

Napi::Value DecodeProtoMessagesRawBatch(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireArrayArg(info, 0, "decodeProtoMessagesRawBatch(items) requires an array")) {
		return env.Null();
	}

	Napi::Array items = info[0].As<Napi::Array>();
	Napi::Array out;
	if (!proto_codec::DecodeBatch(env, items, false, &out)) {
		return env.Null();
	}
	return out;
}

Napi::Value DecodeProtoMessagesFromPaddedBatch(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireArrayArg(
			info,
			0,
			"decodeProtoMessagesFromPaddedBatch(items) requires an array"
		)) {
		return env.Null();
	}

	Napi::Array items = info[0].As<Napi::Array>();
	Napi::Array out;
	if (!proto_codec::DecodeBatch(env, items, true, &out)) {
		return env.Null();
	}
	return out;
}

} // namespace baileys_native
