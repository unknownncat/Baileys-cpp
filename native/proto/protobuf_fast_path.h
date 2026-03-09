#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value PadMessageWithLength(const Napi::CallbackInfo& info);
Napi::Value GetUnpaddedLengthMax16(const Napi::CallbackInfo& info);

} // namespace baileys_native
