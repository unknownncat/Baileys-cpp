#include "register_exports.h"

#include "history_sync_codec.h"
#include "history_sync_stream_decoder.h"
#include "poll_event_decoder.h"
#include "proto_message_codec.h"
#include "protobuf_fast_path.h"

namespace baileys_native {

void RegisterProtoExports(Napi::Env env, Napi::Object exports) {
	exports.Set("padMessageWithLength", Napi::Function::New(env, PadMessageWithLength));
	exports.Set("getUnpaddedLengthMax16", Napi::Function::New(env, GetUnpaddedLengthMax16));
	exports.Set("initProtoMessageCodec", Napi::Function::New(env, InitProtoMessageCodec));
	exports.Set("encodeProtoMessageRaw", Napi::Function::New(env, EncodeProtoMessageRaw));
	exports.Set("encodeProtoMessageWithPad", Napi::Function::New(env, EncodeProtoMessageWithPad));
	exports.Set("decodeProtoMessageRaw", Napi::Function::New(env, DecodeProtoMessageRaw));
	exports.Set("decodeProtoMessageFromPadded", Napi::Function::New(env, DecodeProtoMessageFromPadded));
	exports.Set("decodeProtoMessagesRawBatch", Napi::Function::New(env, DecodeProtoMessagesRawBatch));
	exports.Set("decodeProtoMessagesFromPaddedBatch", Napi::Function::New(env, DecodeProtoMessagesFromPaddedBatch));
	exports.Set("decodePollVoteMessageFast", Napi::Function::New(env, DecodePollVoteMessageFast));
	exports.Set("decodeEventResponseMessageFast", Napi::Function::New(env, DecodeEventResponseMessageFast));
	exports.Set("decodeHistorySyncRaw", Napi::Function::New(env, DecodeHistorySyncRaw));
	exports.Set("decodeCompressedHistorySyncRaw", Napi::Function::New(env, DecodeCompressedHistorySyncRaw));
	InitNativeHistorySyncCompressedDecoder(env, exports);
}

} // namespace baileys_native
