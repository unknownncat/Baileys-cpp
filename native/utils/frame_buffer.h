#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Function InitNativeFrameBuffer(Napi::Env env, Napi::Object exports);

} // namespace baileys_native
