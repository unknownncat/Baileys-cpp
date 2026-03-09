#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value ExtractReportingTokenContent(const Napi::CallbackInfo& info);
Napi::Value BuildReportingTokenV2(const Napi::CallbackInfo& info);

} // namespace baileys_native
