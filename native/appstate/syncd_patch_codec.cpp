#include "syncd_patch_codec.h"

#include "common.h"
#include "common/feature_gates.h"

#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
#include "generated/wa_proto_adapter.h"
#include "protobuf_reflection_codec.h"
#endif

#include <limits>
#include <string>

namespace baileys_native {

#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD

namespace {

using google::protobuf::Message;

bool EnsureObjectArgument(const Napi::Env& env, const Napi::Value& value, const char* fnName) {
	if (!value.IsObject()) {
		Napi::TypeError::New(env, std::string(fnName) + " expects a protobuf object").ThrowAsJavaScriptException();
		return false;
	}
	return true;
}

bool EnsureBytesArgument(const Napi::Env& env, const Napi::Value& value, const char* fnName, common::ByteView* out) {
	if (!common::GetByteViewFromValue(env, value, "data", out)) {
		return false;
	}
	if (out->length > static_cast<size_t>(std::numeric_limits<int>::max())) {
		Napi::RangeError::New(env, std::string(fnName) + " payload too large").ThrowAsJavaScriptException();
		return false;
	}
	return true;
}

template <typename TMessage>
Napi::Value DecodeMessageRaw(const Napi::Env& env, const common::ByteView& bytes, const char* typeName) {
	TMessage message;
	if (!message.ParseFromArray(bytes.data, static_cast<int>(bytes.length))) {
		Napi::Error::New(env, std::string("failed to parse ") + typeName).ThrowAsJavaScriptException();
		return env.Null();
	}

	return proto_reflection::ProtoToJsObject(env, message);
}

template <typename TMessage>
Napi::Value EncodeMessageRaw(const Napi::Env& env, const Napi::Value& value, const char* typeName) {
	if (!EnsureObjectArgument(env, value, typeName)) {
		return env.Null();
	}

	TMessage message;
	if (!proto_reflection::JsToProtoMessage(env, value, &message)) {
		return env.Null();
	}

	std::string encoded;
	if (!message.SerializeToString(&encoded)) {
		Napi::Error::New(env, std::string("failed to serialize ") + typeName).ThrowAsJavaScriptException();
		return env.Null();
	}

	return Napi::Buffer<uint8_t>::Copy(env, reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size());
}

} // namespace

#endif

Napi::Value EncodeSyncdPatchRaw(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1) {
		Napi::TypeError::New(env, "encodeSyncdPatchRaw(patch) requires patch").ThrowAsJavaScriptException();
		return env.Null();
	}

#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
	return EncodeMessageRaw<proto::SyncdPatch>(env, info[0], "SyncdPatch");
#else
	Napi::Error::New(env, "native WAProto is unavailable in this build").ThrowAsJavaScriptException();
	return env.Null();
#endif
}

Napi::Value DecodeSyncdPatchRaw(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1) {
		Napi::TypeError::New(env, "decodeSyncdPatchRaw(data) requires data").ThrowAsJavaScriptException();
		return env.Null();
	}

#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
	common::ByteView data;
	if (!EnsureBytesArgument(env, info[0], "decodeSyncdPatchRaw", &data)) {
		return env.Null();
	}
	return DecodeMessageRaw<proto::SyncdPatch>(env, data, "SyncdPatch");
#else
	Napi::Error::New(env, "native WAProto is unavailable in this build").ThrowAsJavaScriptException();
	return env.Null();
#endif
}

Napi::Value DecodeSyncdSnapshotRaw(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1) {
		Napi::TypeError::New(env, "decodeSyncdSnapshotRaw(data) requires data").ThrowAsJavaScriptException();
		return env.Null();
	}

#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
	common::ByteView data;
	if (!EnsureBytesArgument(env, info[0], "decodeSyncdSnapshotRaw", &data)) {
		return env.Null();
	}
	return DecodeMessageRaw<proto::SyncdSnapshot>(env, data, "SyncdSnapshot");
#else
	Napi::Error::New(env, "native WAProto is unavailable in this build").ThrowAsJavaScriptException();
	return env.Null();
#endif
}

Napi::Value DecodeSyncdMutationsRaw(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1) {
		Napi::TypeError::New(env, "decodeSyncdMutationsRaw(data) requires data").ThrowAsJavaScriptException();
		return env.Null();
	}

#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
	common::ByteView data;
	if (!EnsureBytesArgument(env, info[0], "decodeSyncdMutationsRaw", &data)) {
		return env.Null();
	}
	return DecodeMessageRaw<proto::SyncdMutations>(env, data, "SyncdMutations");
#else
	Napi::Error::New(env, "native WAProto is unavailable in this build").ThrowAsJavaScriptException();
	return env.Null();
#endif
}

} // namespace baileys_native
