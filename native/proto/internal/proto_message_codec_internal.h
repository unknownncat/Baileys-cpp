#pragma once

#include <napi.h>

#include "common.h"
#include "common/feature_gates.h"

#include <string>

namespace baileys_native::proto_codec_internal {

extern Napi::FunctionReference gProtoEncode;
extern Napi::FunctionReference gProtoDecode;
extern bool gUseNativeCodec;

bool EnsureFallbackCodecReady(const Napi::Env& env);

bool ReadEncodedMessageBytes(
	const Napi::Env& env,
	const Napi::Value& messageValue,
	common::ByteView* out
);

Napi::Value DecodeRawBytesFallback(const Napi::Env& env, const common::ByteView& bytes);

bool GetUnpaddedView(const Napi::Env& env, const common::ByteView& data, common::ByteView* out);

bool DecodeBatch(const Napi::Env& env, const Napi::Array& items, bool fromPadded, Napi::Array* out);

#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
bool EncodeRawBytesNative(const Napi::Env& env, const Napi::Value& messageValue, std::string* encoded);

Napi::Value DecodeRawBytesNative(const Napi::Env& env, const common::ByteView& bytes);
#endif

} // namespace baileys_native::proto_codec_internal
