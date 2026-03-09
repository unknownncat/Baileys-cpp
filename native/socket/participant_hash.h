#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value GenerateParticipantHashV2Fast(const Napi::CallbackInfo& info);

} // namespace baileys_native
