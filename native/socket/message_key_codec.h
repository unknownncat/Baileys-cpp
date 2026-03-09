#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value StringifyMessageKeyFast(const Napi::CallbackInfo& info);
Napi::Value StringifyMessageKeysFast(const Napi::CallbackInfo& info);
Napi::Value StringifyMessageKeysFromMessagesFast(const Napi::CallbackInfo& info);
Napi::Value StringifyMessageKeysFromEntriesFast(const Napi::CallbackInfo& info);

} // namespace baileys_native
