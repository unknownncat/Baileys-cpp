#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value DecodePollVoteMessageFast(const Napi::CallbackInfo& info);
Napi::Value DecodeEventResponseMessageFast(const Napi::CallbackInfo& info);

} // namespace baileys_native

