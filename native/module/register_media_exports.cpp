#include "register_exports.h"

#include "audio_wav_analyzer.h"
#include "media_crypto.h"

namespace baileys_native {

void RegisterMediaExports(Napi::Env env, Napi::Object exports) {
	exports.Set("analyzeWavAudioFast", Napi::Function::New(env, AnalyzeWavAudioFast));
	InitNativeHashSpoolWriter(env, exports);
	InitNativeMediaEncryptor(env, exports);
	InitNativeMediaEncryptToFile(env, exports);
	InitNativeMediaDecryptor(env, exports);
	InitNativeMediaDecryptPipeline(env, exports);
}

} // namespace baileys_native
