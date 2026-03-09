#include "register_exports.h"

#include "native_kv_store.h"
#include "value_codec.h"

namespace baileys_native {

void RegisterStorageExports(Napi::Env env, Napi::Object exports) {
	exports.Set("encodeAuthValue", Napi::Function::New(env, EncodeAuthValue));
	exports.Set("decodeAuthValue", Napi::Function::New(env, DecodeAuthValue));
	InitNativeKVStore(env, exports);
}

} // namespace baileys_native

