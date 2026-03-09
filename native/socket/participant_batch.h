#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value BuildParticipantNodesBatch(const Napi::CallbackInfo& info);

} // namespace baileys_native