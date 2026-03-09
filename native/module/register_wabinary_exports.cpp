#include "register_exports.h"

#include "wa_binary_codec.h"

namespace baileys_native {

void RegisterWABinaryExports(Napi::Env env, Napi::Object exports) {
	exports.Set("initWABinaryCodec", Napi::Function::New(env, InitWABinaryCodec));
	exports.Set("encodeWABinaryNode", Napi::Function::New(env, EncodeWABinaryNode));
	exports.Set("decodeWABinaryNode", Napi::Function::New(env, DecodeWABinaryNode));
}

} // namespace baileys_native

