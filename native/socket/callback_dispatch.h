#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value BuildSocketCallbackEventKeys(const Napi::CallbackInfo& info);
Napi::Value EmitSocketCallbackEvents(const Napi::CallbackInfo& info);

} // namespace baileys_native
