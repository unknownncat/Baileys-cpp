#include "register_exports.h"

#include "app_state_payload.h"
#include "mutation_engine.h"
#include "reporting_token.h"
#include "sync_action_codec.h"
#include "syncd_patch_codec.h"

namespace baileys_native {

void RegisterAppStateExports(Napi::Env env, Napi::Object exports) {
	exports.Set("extractReportingTokenContent", Napi::Function::New(env, ExtractReportingTokenContent));
	exports.Set("buildReportingTokenV2", Napi::Function::New(env, BuildReportingTokenV2));
	exports.Set("buildMutationMacPayload", Napi::Function::New(env, BuildMutationMacPayload));
	exports.Set("buildSnapshotMacPayload", Napi::Function::New(env, BuildSnapshotMacPayload));
	exports.Set("buildPatchMacPayload", Napi::Function::New(env, BuildPatchMacPayload));
	exports.Set("encodeSyncActionData", Napi::Function::New(env, EncodeSyncActionData));
	exports.Set("decodeSyncActionData", Napi::Function::New(env, DecodeSyncActionData));
	exports.Set("encodeSyncdPatchRaw", Napi::Function::New(env, EncodeSyncdPatchRaw));
	exports.Set("decodeSyncdPatchRaw", Napi::Function::New(env, DecodeSyncdPatchRaw));
	exports.Set("decodeSyncdSnapshotRaw", Napi::Function::New(env, DecodeSyncdSnapshotRaw));
	exports.Set("decodeSyncdMutationsRaw", Napi::Function::New(env, DecodeSyncdMutationsRaw));
	exports.Set("decodeSyncdMutationsFast", Napi::Function::New(env, DecodeSyncdMutationsFast));
	exports.Set("decodeSyncdMutationsFastWire", Napi::Function::New(env, DecodeSyncdMutationsFastWire));
}

} // namespace baileys_native

