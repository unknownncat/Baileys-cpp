#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value DecodeHistorySyncRaw(const Napi::CallbackInfo& info);
Napi::Value DecodeCompressedHistorySyncRaw(const Napi::CallbackInfo& info);

} // namespace baileys_native
