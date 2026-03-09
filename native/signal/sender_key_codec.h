#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value EncodeSenderKeyStates(const Napi::CallbackInfo& info);
Napi::Value DecodeSenderKeyStates(const Napi::CallbackInfo& info);
Napi::Value IsSenderKeyBinary(const Napi::CallbackInfo& info);

} // namespace baileys_native
