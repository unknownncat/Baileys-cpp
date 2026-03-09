#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value EncodeGroupParticipantStubsFast(const Napi::CallbackInfo& info);
Napi::Value ParseGroupParticipantStubsFast(const Napi::CallbackInfo& info);

} // namespace baileys_native
