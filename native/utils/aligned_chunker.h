#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Function InitNativeAlignedChunker(Napi::Env env, Napi::Object exports);

} // namespace baileys_native
