#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Function InitNativeMediaEncryptor(Napi::Env env, Napi::Object exports);
Napi::Function InitNativeHashSpoolWriter(Napi::Env env, Napi::Object exports);
Napi::Function InitNativeMediaEncryptToFile(Napi::Env env, Napi::Object exports);
Napi::Function InitNativeMediaDecryptor(Napi::Env env, Napi::Object exports);
Napi::Function InitNativeMediaDecryptPipeline(Napi::Env env, Napi::Object exports);

} // namespace baileys_native
