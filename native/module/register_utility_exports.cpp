#include "register_exports.h"

#include "aligned_chunker.h"
#include "buffer_builder.h"
#include "chunk_split.h"
#include "frame_buffer.h"
#include "jid_codec.h"
#include "json_batch_parser.h"
#include "range_filter.h"
#include "runtime_fast_paths.h"
#include "wam_encode.h"
#include "zlib_helper.h"

namespace baileys_native {

void RegisterUtilityExports(Napi::Env env, Napi::Object exports) {
	exports.Set("splitAlignedChunk", Napi::Function::New(env, SplitAlignedChunk));
	exports.Set("inflateZlibBuffer", Napi::Function::New(env, InflateZlibBuffer));
	exports.Set("decodeJidFast", Napi::Function::New(env, DecodeJidFast));
	exports.Set("normalizeJidUserFast", Napi::Function::New(env, NormalizeJidUserFast));
	exports.Set("areJidsSameUserFast", Napi::Function::New(env, AreJidsSameUserFast));
	exports.Set("parseJsonStringArrayFast", Napi::Function::New(env, ParseJsonStringArrayFast));
	exports.Set("encodeWAMFast", Napi::Function::New(env, EncodeWAMFast));
	exports.Set("normalizeLidPnMappingsFast", Napi::Function::New(env, NormalizeLidPnMappingsFast));
	exports.Set("extractBotListV2Fast", Napi::Function::New(env, ExtractBotListV2Fast));
	exports.Set("pickFirstExistingKeyFast", Napi::Function::New(env, PickFirstExistingKeyFast));
	exports.Set("isRecoverableSignalTxErrorFast", Napi::Function::New(env, IsRecoverableSignalTxErrorFast));
	exports.Set("generateSignalPubKeyFast", Napi::Function::New(env, GenerateSignalPubKeyFast));
	exports.Set("dedupeStringListFast", Napi::Function::New(env, DedupeStringListFast));
	exports.Set("resolveSignalAddressFast", Napi::Function::New(env, ResolveSignalAddressFast));
	exports.Set("buildRetryMessageKeyFast", Napi::Function::New(env, BuildRetryMessageKeyFast));
	exports.Set("parseRetryErrorCodeFast", Napi::Function::New(env, ParseRetryErrorCodeFast));
	exports.Set("isMacRetryErrorCodeFast", Napi::Function::New(env, IsMacRetryErrorCodeFast));
	exports.Set("buildParticipantNodesFast", Napi::Function::New(env, BuildParticipantNodesFast));
	exports.Set("extractNodeAttrsFast", Napi::Function::New(env, ExtractNodeAttrsFast));
	exports.Set("mapParticipantActionResultsFast", Napi::Function::New(env, MapParticipantActionResultsFast));
	exports.Set("extractCommunityLinkedGroupsFast", Napi::Function::New(env, ExtractCommunityLinkedGroupsFast));
	exports.Set("buildBusinessProfileNodesFast", Napi::Function::New(env, BuildBusinessProfileNodesFast));
	exports.Set("buildCatalogQueryParamsFast", Napi::Function::New(env, BuildCatalogQueryParamsFast));
	exports.Set("parseProductNodesFast", Napi::Function::New(env, ParseProductNodesFast));
	exports.Set("parseCollectionNodesFast", Napi::Function::New(env, ParseCollectionNodesFast));
	exports.Set("parseOrderProductNodesFast", Napi::Function::New(env, ParseOrderProductNodesFast));
	exports.Set("splitSenderKeySerializedFast", Napi::Function::New(env, SplitSenderKeySerializedFast));
	InitNativeAlignedChunker(env, exports);
	InitNativeBufferBuilder(env, exports);
	InitNativeRangeFilter(env, exports);
	InitNativeFrameBuffer(env, exports);
}

} // namespace baileys_native
