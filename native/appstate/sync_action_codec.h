#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value EncodeSyncActionData(const Napi::CallbackInfo& info);
Napi::Value DecodeSyncActionData(const Napi::CallbackInfo& info);

} // namespace baileys_native
