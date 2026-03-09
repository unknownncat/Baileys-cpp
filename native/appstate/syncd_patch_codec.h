#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value EncodeSyncdPatchRaw(const Napi::CallbackInfo& info);
Napi::Value DecodeSyncdPatchRaw(const Napi::CallbackInfo& info);
Napi::Value DecodeSyncdSnapshotRaw(const Napi::CallbackInfo& info);
Napi::Value DecodeSyncdMutationsRaw(const Napi::CallbackInfo& info);

} // namespace baileys_native