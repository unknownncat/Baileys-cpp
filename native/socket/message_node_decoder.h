#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value DecodeMessageNodeFast(const Napi::CallbackInfo& info);

} // namespace baileys_native

