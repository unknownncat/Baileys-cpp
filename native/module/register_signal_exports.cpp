#include "register_exports.h"

#include "e2e_session_extractor.h"
#include "message_key_store.h"
#include "sender_key_codec.h"

namespace baileys_native {

void RegisterSignalExports(Napi::Env env, Napi::Object exports) {
	exports.Set("encodeSenderKeyStates", Napi::Function::New(env, EncodeSenderKeyStates));
	exports.Set("decodeSenderKeyStates", Napi::Function::New(env, DecodeSenderKeyStates));
	exports.Set("isSenderKeyBinary", Napi::Function::New(env, IsSenderKeyBinary));
	exports.Set("extractE2ESessionBundlesFast", Napi::Function::New(env, ExtractE2ESessionBundlesFast));
	InitNativeMessageKeyStore(env, exports);
}

} // namespace baileys_native

