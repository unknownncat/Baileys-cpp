#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value EncodeAuthValue(const Napi::CallbackInfo& info);
Napi::Value DecodeAuthValue(const Napi::CallbackInfo& info);

} // namespace baileys_native
