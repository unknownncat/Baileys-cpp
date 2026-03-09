#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value AnalyzeWavAudioFast(const Napi::CallbackInfo& info);

} // namespace baileys_native

