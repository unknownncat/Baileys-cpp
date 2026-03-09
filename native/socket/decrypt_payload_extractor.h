#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value ExtractDecryptPayloadsFast(const Napi::CallbackInfo& info);

} // namespace baileys_native

