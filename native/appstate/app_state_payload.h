#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value BuildMutationMacPayload(const Napi::CallbackInfo& info);
Napi::Value BuildSnapshotMacPayload(const Napi::CallbackInfo& info);
Napi::Value BuildPatchMacPayload(const Napi::CallbackInfo& info);

} // namespace baileys_native
