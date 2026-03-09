#include "media_crypto.h"

#include "media/internal/media_decrypt_pipeline_object.h"

namespace baileys_native {

Napi::Function InitNativeMediaDecryptPipeline(Napi::Env env, Napi::Object exports) {
	return media_internal::NativeMediaDecryptPipeline::Init(env, exports);
}

} // namespace baileys_native

