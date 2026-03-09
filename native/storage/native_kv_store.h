#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Function InitNativeKVStore(Napi::Env env, Napi::Object exports);

} // namespace baileys_native
