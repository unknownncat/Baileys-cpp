#include "proto/internal/proto_message_codec_internal.h"

#include "common/feature_gates.h"
#include "common/native_error_log.h"

#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
#include "generated/wa_proto_adapter.h"
#include "protobuf_reflection_codec.h"
#endif

#include <cstdint>
#include <limits>
#include <string>

namespace baileys_native::proto_codec_internal {

Napi::FunctionReference gProtoEncode;
Napi::FunctionReference gProtoDecode;
bool gUseNativeCodec =
#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
	true;
#else
	false;
#endif

bool EnsureFallbackCodecReady(const Napi::Env& env) {
	if (!gProtoEncode.IsEmpty() && !gProtoDecode.IsEmpty()) {
		return true;
	}

	common::native_error_log::ThrowError(env, "proto.codec", "fallback.ready", "Proto message codec not initialized");
	return false;
}

bool ReadEncodedMessageBytes(
	const Napi::Env& env,
	const Napi::Value& messageValue,
	common::ByteView* out
) {
	Napi::Value writerValue = gProtoEncode.Call(env.Global(), {messageValue});
	if (!writerValue.IsObject()) {
		common::native_error_log::ThrowType(
			env,
			"proto.codec",
			"fallback.encode_writer",
			"proto encode returned non-object writer"
		);
		return false;
	}

	Napi::Object writer = writerValue.As<Napi::Object>();
	Napi::Value finishValue = writer.Get("finish");
	if (!finishValue.IsFunction()) {
		common::native_error_log::ThrowType(
			env,
			"proto.codec",
			"fallback.finish",
			"proto writer missing finish()"
		);
		return false;
	}

	Napi::Function finishFn = finishValue.As<Napi::Function>();
	Napi::Value encodedValue = finishFn.Call(writer, {});
	if (!common::GetByteViewFromValue(env, encodedValue, "encoded", out)) {
		return false;
	}

	return true;
}

Napi::Value DecodeRawBytesFallback(const Napi::Env& env, const common::ByteView& bytes) {
	const Napi::Buffer<uint8_t> payload = Napi::Buffer<uint8_t>::Copy(env, bytes.data, bytes.length);
	return gProtoDecode.Call(env.Global(), {payload});
}

bool GetUnpaddedView(const Napi::Env& env, const common::ByteView& data, common::ByteView* out) {
	if (data.length == 0) {
		common::native_error_log::ThrowError(
			env,
			"proto.codec",
			"unpadded.empty",
			"unpadPkcs7 given empty bytes"
		);
		return false;
	}

	const uint8_t pad = data.data[data.length - 1];
	if (pad == 0 || static_cast<size_t>(pad) > data.length) {
		common::native_error_log::ThrowError(
			env,
			"proto.codec",
			"unpadded.invalid_pad",
			std::string("unpad given ") + std::to_string(data.length) + " bytes, but pad is " + std::to_string(pad)
		);
		return false;
	}

	out->data = data.data;
	out->length = data.length - static_cast<size_t>(pad);
	return true;
}

bool DecodeBatch(const Napi::Env& env, const Napi::Array& items, bool fromPadded, Napi::Array* out) {
	const uint32_t count = items.Length();
	*out = Napi::Array::New(env, count);

	if (!gUseNativeCodec && !EnsureFallbackCodecReady(env)) {
		return false;
	}

	for (uint32_t i = 0; i < count; ++i) {
		common::ByteView data;
		const std::string field = "items[" + std::to_string(i) + "]";
		if (!common::GetByteViewFromValue(env, items.Get(i), field.c_str(), &data)) {
			return false;
		}

		common::ByteView inputView = data;
		if (fromPadded && !GetUnpaddedView(env, data, &inputView)) {
			return false;
		}

		Napi::Value decoded;
		if (gUseNativeCodec) {
#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
			decoded = DecodeRawBytesNative(env, inputView);
#else
			common::native_error_log::ThrowError(
				env,
				"proto.codec",
				"decode_batch.unavailable",
				"native proto codec is unavailable in this build"
			);
			return false;
#endif
		} else {
			decoded = DecodeRawBytesFallback(env, inputView);
		}

		if (env.IsExceptionPending()) {
			return false;
		}

		out->Set(i, decoded);
	}

	return true;
}

#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
bool EncodeRawBytesNative(const Napi::Env& env, const Napi::Value& messageValue, std::string* encoded) {
	if (!messageValue.IsObject()) {
		common::native_error_log::ThrowType(
			env,
			"proto.codec",
			"encode_native.args",
			"encodeProtoMessageRaw expects a protobuf object"
		);
		return false;
	}

	proto::Message message;
	if (!proto_reflection::JsToProtoMessage(env, messageValue, &message)) {
		return false;
	}
	if (!message.SerializeToString(encoded)) {
		common::native_error_log::ThrowError(
			env,
			"proto.codec",
			"encode_native.serialize",
			"failed to serialize proto::Message"
		);
		return false;
	}
	return true;
}

Napi::Value DecodeRawBytesNative(const Napi::Env& env, const common::ByteView& bytes) {
	if (bytes.length > static_cast<size_t>(std::numeric_limits<int>::max())) {
		return common::native_error_log::ThrowRangeValue(
			env,
			"proto.codec",
			"decode_native.size",
			"decodeProtoMessageRaw payload too large"
		);
	}

	proto::Message message;
	if (!message.ParseFromArray(bytes.data, static_cast<int>(bytes.length))) {
		return common::native_error_log::ThrowErrorValue(
			env,
			"proto.codec",
			"decode_native.parse",
			"failed to parse proto::Message"
		);
	}

	return proto_reflection::ProtoToJsObject(env, message);
}
#endif

} // namespace baileys_native::proto_codec_internal
