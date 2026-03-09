#include "history_sync_codec.h"

#include "common.h"
#include "common/feature_gates.h"
#include "common/native_error_log.h"
#include "utils/zlib_inflate.h"

#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
#include "generated/wa_proto_adapter.h"
#include "protobuf_reflection_codec.h"
#endif

#include <limits>
#include <string>
#include <vector>

namespace baileys_native {

namespace {

bool ParseHistorySyncToJs(const Napi::Env& env, const common::ByteView& data, Napi::Value* out) {
#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
	if (data.length > static_cast<size_t>(std::numeric_limits<int>::max())) {
		common::native_error_log::ThrowRange(
			env,
			"proto.history_sync",
			"parse.size",
			"decodeHistorySyncRaw payload too large"
		);
		return false;
	}

	proto::HistorySync message;
	if (!message.ParseFromArray(data.data, static_cast<int>(data.length))) {
		common::native_error_log::ThrowError(
			env,
			"proto.history_sync",
			"parse.protobuf",
			"failed to parse HistorySync"
		);
		return false;
	}

	*out = proto_reflection::ProtoToJsObject(env, message);
	return true;
#else
	common::native_error_log::ThrowError(
		env,
		"proto.history_sync",
		"parse.unavailable",
		"native WAProto is unavailable in this build"
	);
	return false;
#endif
}

} // namespace

Napi::Value DecodeHistorySyncRaw(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1) {
		return common::native_error_log::ThrowTypeValue(
			env,
			"proto.history_sync",
			"decode_raw.args",
			"decodeHistorySyncRaw(data) requires data"
		);
	}

	common::ByteView data;
	if (!common::GetByteViewFromValue(env, info[0], "data", &data)) {
		return env.Null();
	}

	Napi::Value out = env.Null();
	if (!ParseHistorySyncToJs(env, data, &out)) {
		return env.Null();
	}
	return out;
}

Napi::Value DecodeCompressedHistorySyncRaw(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1) {
		return common::native_error_log::ThrowTypeValue(
			env,
			"proto.history_sync",
			"decode_compressed.args",
			"decodeCompressedHistorySyncRaw(data) requires data"
		);
	}

	common::ByteView input;
	if (!common::GetByteViewFromValue(env, info[0], "data", &input)) {
		return env.Null();
	}
	if (input.length == 0) {
		return common::native_error_log::ThrowErrorValue(
			env,
			"proto.history_sync",
			"decode_compressed.empty",
			"decodeCompressedHistorySyncRaw given empty bytes"
		);
	}

#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
	std::vector<uint8_t> inflated;
	std::string inflateError;
	if (!utils::InflateZlibBytes(input.data, input.length, &inflated, &inflateError)) {
		return common::native_error_log::ThrowErrorValue(
			env,
			"proto.history_sync",
			"decode_compressed.inflate",
			inflateError
		);
	}

	common::ByteView inflatedBytes;
	inflatedBytes.data = inflated.data();
	inflatedBytes.length = inflated.size();

	Napi::Value out = env.Null();
	if (!ParseHistorySyncToJs(env, inflatedBytes, &out)) {
		return env.Null();
	}
	return out;
#else
	return common::native_error_log::ThrowErrorValue(
		env,
		"proto.history_sync",
		"decode_compressed.unavailable",
		"native WAProto is unavailable in this build"
	);
#endif
}

} // namespace baileys_native
