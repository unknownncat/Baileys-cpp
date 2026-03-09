#include "history_sync_stream_decoder.h"

#include "common.h"
#include "common/feature_gates.h"
#include "common/native_error_log.h"
#include "third_party/miniz_adapter.h"

#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
#include "generated/wa_proto_adapter.h"
#include "protobuf_reflection_codec.h"
#endif

#include <array>
#include <cstring>
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
			"proto.history_sync_stream",
			"parse.size",
			"history payload too large"
		);
		return false;
	}

	proto::HistorySync message;
	if (!message.ParseFromArray(data.data, static_cast<int>(data.length))) {
		common::native_error_log::ThrowError(
			env,
			"proto.history_sync_stream",
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
		"proto.history_sync_stream",
		"parse.unavailable",
		"native WAProto is unavailable in this build"
	);
	return false;
#endif
}

const char* MinizInflateStatusText(int status) {
	switch (status) {
		case MZ_OK:
			return "ok";
		case MZ_STREAM_END:
			return "stream end";
		case MZ_NEED_DICT:
			return "need dictionary";
		case MZ_STREAM_ERROR:
			return "stream error";
		case MZ_DATA_ERROR:
			return "data error";
		case MZ_MEM_ERROR:
			return "memory error";
		case MZ_BUF_ERROR:
			return "buffer too small";
		case MZ_VERSION_ERROR:
			return "version error";
		case MZ_PARAM_ERROR:
			return "invalid parameter";
		default:
			return "unknown";
	}
}

class NativeHistorySyncCompressedDecoder : public Napi::ObjectWrap<NativeHistorySyncCompressedDecoder> {
public:
	static Napi::Function Init(Napi::Env env, Napi::Object exports) {
		Napi::Function func = DefineClass(
			env,
			"NativeHistorySyncCompressedDecoder",
			{
				InstanceMethod("append", &NativeHistorySyncCompressedDecoder::Append),
				InstanceMethod("decode", &NativeHistorySyncCompressedDecoder::Decode),
				InstanceMethod("size", &NativeHistorySyncCompressedDecoder::Size),
				InstanceMethod("clear", &NativeHistorySyncCompressedDecoder::Clear)
			}
		);
		constructor_ = Napi::Persistent(func);
		constructor_.SuppressDestruct();
		exports.Set("NativeHistorySyncCompressedDecoder", func);
		return func;
	}

	NativeHistorySyncCompressedDecoder(const Napi::CallbackInfo& info)
		: Napi::ObjectWrap<NativeHistorySyncCompressedDecoder>(info) {}
	~NativeHistorySyncCompressedDecoder() override {
		ResetStream();
	}

private:
	static Napi::FunctionReference constructor_;
	static constexpr size_t kInflateChunkSize = 64u * 1024u;
	static constexpr size_t kMaxInflatedBytes = 512u * 1024u * 1024u;

	std::vector<uint8_t> inflated_;
	size_t compressedSize_ = 0;
	mz_stream inflateStream_{};
	bool inflateInitialized_ = false;
	bool streamFinished_ = false;

	void ResetStream() {
		if (inflateInitialized_) {
			mz_inflateEnd(&inflateStream_);
		}
		std::memset(&inflateStream_, 0, sizeof(inflateStream_));
		inflateInitialized_ = false;
		streamFinished_ = false;
	}

	bool EnsureStream(std::string* error) {
		if (inflateInitialized_) {
			return true;
		}

		std::memset(&inflateStream_, 0, sizeof(inflateStream_));
		const int status = mz_inflateInit(&inflateStream_);
		if (status != MZ_OK) {
			*error = std::string("zlib stream init failed: ") + MinizInflateStatusText(status);
			return false;
		}

		inflateInitialized_ = true;
		return true;
	}

	bool AppendCompressedChunk(const uint8_t* data, size_t length, std::string* error) {
		if (length == 0) {
			return true;
		}
		if (streamFinished_) {
			*error = "zlib stream already finalized";
			return false;
		}
		if (!EnsureStream(error)) {
			return false;
		}

		size_t offset = 0;
		std::array<uint8_t, kInflateChunkSize> outChunk{};
		const size_t maxAvailIn = static_cast<size_t>(std::numeric_limits<mz_uint>::max());
		const size_t maxAvailOut = static_cast<size_t>(std::numeric_limits<mz_uint>::max());
		const size_t usableOutChunk = outChunk.size() > maxAvailOut ? maxAvailOut : outChunk.size();

		while (offset < length) {
			const size_t remaining = length - offset;
			const size_t currentInput = remaining > maxAvailIn ? maxAvailIn : remaining;

			inflateStream_.next_in = const_cast<mz_uint8*>(data + offset);
			inflateStream_.avail_in = static_cast<mz_uint>(currentInput);

			while (inflateStream_.avail_in > 0) {
				inflateStream_.next_out = outChunk.data();
				inflateStream_.avail_out = static_cast<mz_uint>(usableOutChunk);

				const int status = mz_inflate(&inflateStream_, MZ_NO_FLUSH);
				if (status != MZ_OK && status != MZ_STREAM_END) {
					*error = std::string("zlib stream inflate failed: ") + MinizInflateStatusText(status);
					return false;
				}

				const size_t produced = usableOutChunk - static_cast<size_t>(inflateStream_.avail_out);
				if (produced > 0) {
					if (inflated_.size() > kMaxInflatedBytes - produced) {
						*error = "history sync inflate exceeded max output size";
						return false;
					}
					const size_t oldSize = inflated_.size();
					inflated_.resize(oldSize + produced);
					std::memcpy(inflated_.data() + oldSize, outChunk.data(), produced);
				}

				if (status == MZ_STREAM_END) {
					streamFinished_ = true;
					if (inflateStream_.avail_in > 0 || (offset + currentInput) < length) {
						*error = "zlib stream contains trailing data";
						return false;
					}
					return true;
				}

				if (produced == 0 && inflateStream_.avail_out > 0) {
					break;
				}
			}

			offset += currentInput;
		}

		return true;
	}

	Napi::Value Append(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		if (info.Length() < 1) {
			return common::native_error_log::ThrowTypeValue(
				env,
				"proto.history_sync_stream",
				"append.args",
				"append(chunk) requires chunk"
			);
		}

		common::ByteView chunk;
		if (!common::GetByteViewFromValue(env, info[0], "chunk", &chunk)) {
			return env.Null();
		}

		if (chunk.length == 0) {
			return env.Undefined();
		}

		if (compressedSize_ > std::numeric_limits<size_t>::max() - chunk.length) {
			return common::native_error_log::ThrowRangeValue(
				env,
				"proto.history_sync_stream",
				"append.size",
				"compressed history stream too large"
			);
		}

		std::string inflateError;
		if (!AppendCompressedChunk(chunk.data, chunk.length, &inflateError)) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"proto.history_sync_stream",
				"append.inflate",
				inflateError
			);
		}
		compressedSize_ += chunk.length;
		return env.Undefined();
	}

	Napi::Value Decode(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		if (inflated_.empty()) {
			return env.Null();
		}

		const bool reset = info.Length() == 0 || info[0].ToBoolean().Value();

#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
		common::ByteView inflatedBytes;
		inflatedBytes.data = inflated_.data();
		inflatedBytes.length = inflated_.size();

		Napi::Value out = env.Null();
		if (!ParseHistorySyncToJs(env, inflatedBytes, &out)) {
			return env.Null();
		}

		if (reset) {
			inflated_.clear();
			compressedSize_ = 0;
			ResetStream();
		}

		return out;
#else
		(void)reset;
		return common::native_error_log::ThrowErrorValue(
			env,
			"proto.history_sync_stream",
			"decode.unavailable",
			"native WAProto is unavailable in this build"
		);
#endif
	}

	Napi::Value Size(const Napi::CallbackInfo& info) {
		return Napi::Number::New(info.Env(), static_cast<double>(compressedSize_));
	}

	Napi::Value Clear(const Napi::CallbackInfo& info) {
		inflated_.clear();
		compressedSize_ = 0;
		ResetStream();
		return info.Env().Undefined();
	}
};

Napi::FunctionReference NativeHistorySyncCompressedDecoder::constructor_;

} // namespace

Napi::Function InitNativeHistorySyncCompressedDecoder(Napi::Env env, Napi::Object exports) {
	return NativeHistorySyncCompressedDecoder::Init(env, exports);
}

} // namespace baileys_native
