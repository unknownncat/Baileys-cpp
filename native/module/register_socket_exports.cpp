#include "register_exports.h"

#include "callback_dispatch.h"
#include "decrypt_payload_extractor.h"
#include "device_jid_extractor.h"
#include "group_stub_codec.h"
#include "message_key_codec.h"
#include "message_node_decoder.h"
#include "message_waiter_registry.h"
#include "participant_batch.h"
#include "participant_hash.h"
#include "usync_parser.h"

namespace baileys_native {

void RegisterSocketExports(Napi::Env env, Napi::Object exports) {
	exports.Set("decodeMessageNodeFast", Napi::Function::New(env, DecodeMessageNodeFast));
	exports.Set("buildSocketCallbackEventKeys", Napi::Function::New(env, BuildSocketCallbackEventKeys));
	exports.Set("emitSocketCallbackEvents", Napi::Function::New(env, EmitSocketCallbackEvents));
	exports.Set("parseUSyncQueryResultFast", Napi::Function::New(env, ParseUSyncQueryResultFast));
	exports.Set("extractDecryptPayloadsFast", Napi::Function::New(env, ExtractDecryptPayloadsFast));
	exports.Set("extractDeviceJidsFast", Napi::Function::New(env, ExtractDeviceJidsFast));
	exports.Set("stringifyMessageKeyFast", Napi::Function::New(env, StringifyMessageKeyFast));
	exports.Set("stringifyMessageKeysFast", Napi::Function::New(env, StringifyMessageKeysFast));
	exports.Set("stringifyMessageKeysFromMessagesFast", Napi::Function::New(env, StringifyMessageKeysFromMessagesFast));
	exports.Set("stringifyMessageKeysFromEntriesFast", Napi::Function::New(env, StringifyMessageKeysFromEntriesFast));
	exports.Set("encodeGroupParticipantStubsFast", Napi::Function::New(env, EncodeGroupParticipantStubsFast));
	exports.Set("parseGroupParticipantStubsFast", Napi::Function::New(env, ParseGroupParticipantStubsFast));
	exports.Set("buildParticipantNodesBatch", Napi::Function::New(env, BuildParticipantNodesBatch));
	exports.Set("generateParticipantHashV2Fast", Napi::Function::New(env, GenerateParticipantHashV2Fast));
	InitNativeMessageWaiterRegistry(env, exports);
}

} // namespace baileys_native
