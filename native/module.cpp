#include <napi.h>

#include "module/register_exports.h"

namespace baileys_native {

Napi::Object Init(Napi::Env env, Napi::Object exports) {
	RegisterSignalExports(env, exports);
	RegisterSocketExports(env, exports);
	RegisterProtoExports(env, exports);
	RegisterAppStateExports(env, exports);
	RegisterStorageExports(env, exports);
	RegisterMediaExports(env, exports);
	RegisterUtilityExports(env, exports);
	RegisterWABinaryExports(env, exports);
	return exports;
}

} // namespace baileys_native

Napi::Object InitModule(Napi::Env env, Napi::Object exports) {
	return baileys_native::Init(env, exports);
}

NODE_API_MODULE(baileys_native, InitModule)
