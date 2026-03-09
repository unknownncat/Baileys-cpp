#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value InitWABinaryCodec(const Napi::CallbackInfo& info);
Napi::Value EncodeWABinaryNode(const Napi::CallbackInfo& info);
Napi::Value DecodeWABinaryNode(const Napi::CallbackInfo& info);

} // namespace baileys_native
