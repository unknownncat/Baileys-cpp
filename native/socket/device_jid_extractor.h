#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value ExtractDeviceJidsFast(const Napi::CallbackInfo& info);

} // namespace baileys_native

