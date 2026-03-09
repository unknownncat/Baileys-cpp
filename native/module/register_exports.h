#pragma once

#include <napi.h>

namespace baileys_native {

void RegisterSignalExports(Napi::Env env, Napi::Object exports);
void RegisterSocketExports(Napi::Env env, Napi::Object exports);
void RegisterProtoExports(Napi::Env env, Napi::Object exports);
void RegisterAppStateExports(Napi::Env env, Napi::Object exports);
void RegisterStorageExports(Napi::Env env, Napi::Object exports);
void RegisterMediaExports(Napi::Env env, Napi::Object exports);
void RegisterUtilityExports(Napi::Env env, Napi::Object exports);
void RegisterWABinaryExports(Napi::Env env, Napi::Object exports);

} // namespace baileys_native

