#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value ExtractE2ESessionBundlesFast(const Napi::CallbackInfo& info);

} // namespace baileys_native

