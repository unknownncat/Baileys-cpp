#include "native_kv_store.h"

#include "storage/internal/native_kv_store_object.h"

namespace baileys_native {

Napi::Function InitNativeKVStore(Napi::Env env, Napi::Object exports) {
	return storage_internal::NativeKVStore::Init(env, exports);
}

} // namespace baileys_native

