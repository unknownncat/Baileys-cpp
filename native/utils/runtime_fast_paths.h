#pragma once

#include <napi.h>

namespace baileys_native {

Napi::Value NormalizeLidPnMappingsFast(const Napi::CallbackInfo& info);
Napi::Value ExtractBotListV2Fast(const Napi::CallbackInfo& info);
Napi::Value PickFirstExistingKeyFast(const Napi::CallbackInfo& info);
Napi::Value IsRecoverableSignalTxErrorFast(const Napi::CallbackInfo& info);
Napi::Value GenerateSignalPubKeyFast(const Napi::CallbackInfo& info);
Napi::Value DedupeStringListFast(const Napi::CallbackInfo& info);
Napi::Value ResolveSignalAddressFast(const Napi::CallbackInfo& info);
Napi::Value BuildRetryMessageKeyFast(const Napi::CallbackInfo& info);
Napi::Value ParseRetryErrorCodeFast(const Napi::CallbackInfo& info);
Napi::Value IsMacRetryErrorCodeFast(const Napi::CallbackInfo& info);
Napi::Value BuildParticipantNodesFast(const Napi::CallbackInfo& info);
Napi::Value ExtractNodeAttrsFast(const Napi::CallbackInfo& info);
Napi::Value MapParticipantActionResultsFast(const Napi::CallbackInfo& info);
Napi::Value ExtractCommunityLinkedGroupsFast(const Napi::CallbackInfo& info);
Napi::Value BuildBusinessProfileNodesFast(const Napi::CallbackInfo& info);
Napi::Value BuildCatalogQueryParamsFast(const Napi::CallbackInfo& info);
Napi::Value ParseProductNodesFast(const Napi::CallbackInfo& info);
Napi::Value ParseCollectionNodesFast(const Napi::CallbackInfo& info);
Napi::Value ParseOrderProductNodesFast(const Napi::CallbackInfo& info);
Napi::Value SplitSenderKeySerializedFast(const Napi::CallbackInfo& info);

} // namespace baileys_native
