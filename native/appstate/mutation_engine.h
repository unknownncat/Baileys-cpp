#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value DecodeSyncdMutationsFast(const Napi::CallbackInfo& info);
Napi::Value DecodeSyncdMutationsFastWire(const Napi::CallbackInfo& info);

} // namespace baileys_native
