#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value DecodeJidFast(const Napi::CallbackInfo& info);
Napi::Value NormalizeJidUserFast(const Napi::CallbackInfo& info);
Napi::Value AreJidsSameUserFast(const Napi::CallbackInfo& info);

} // namespace baileys_native
