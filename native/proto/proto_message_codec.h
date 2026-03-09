#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value InitProtoMessageCodec(const Napi::CallbackInfo& info);
Napi::Value EncodeProtoMessageRaw(const Napi::CallbackInfo& info);
Napi::Value EncodeProtoMessageWithPad(const Napi::CallbackInfo& info);
Napi::Value DecodeProtoMessageRaw(const Napi::CallbackInfo& info);
Napi::Value DecodeProtoMessageFromPadded(const Napi::CallbackInfo& info);
Napi::Value DecodeProtoMessagesRawBatch(const Napi::CallbackInfo& info);
Napi::Value DecodeProtoMessagesFromPaddedBatch(const Napi::CallbackInfo& info);

} // namespace baileys_native
