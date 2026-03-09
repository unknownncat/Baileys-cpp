#include "sync_action_codec.h"

#include "common.h"
#include "common/feature_gates.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
#include "generated/wa_proto_adapter.h"
#endif

namespace baileys_native {

namespace {

constexpr uint64_t kTagIndex = (1u << 3u) | 2u;
constexpr uint64_t kTagValue = (2u << 3u) | 2u;
constexpr uint64_t kTagPadding = (3u << 3u) | 2u;
constexpr uint64_t kTagVersion = (4u << 3u) | 0u;

constexpr uint32_t kWireVarint = 0;
constexpr uint32_t kWireFixed64 = 1;
constexpr uint32_t kWireBytes = 2;
constexpr uint32_t kWireFixed32 = 5;

void EncodeVarint(uint64_t value, std::vector<uint8_t>* out) {
	uint64_t remaining = value;
	while (remaining > 0x7f) {
		out->push_back(static_cast<uint8_t>((remaining & 0x7f) | 0x80));
		remaining >>= 7;
	}
	out->push_back(static_cast<uint8_t>(remaining));
}

bool DecodeVarint(const uint8_t* data, size_t length, size_t* offset, uint64_t* out) {
	uint64_t value = 0;
	uint32_t shift = 0;
	size_t i = *offset;

	while (i < length) {
		const uint8_t byte = data[i++];
		value |= static_cast<uint64_t>(byte & 0x7f) << shift;
		if ((byte & 0x80) == 0) {
			*offset = i;
			*out = value;
			return true;
		}
		shift += 7;
		if (shift > 63) {
			return false;
		}
	}

	return false;
}

bool SkipField(const uint8_t* data, size_t length, size_t* offset, uint32_t wireType) {
	if (wireType == kWireVarint) {
		uint64_t ignored = 0;
		return DecodeVarint(data, length, offset, &ignored);
	}

	if (wireType == kWireFixed64) {
		if (*offset + 8 > length) {
			return false;
		}
		*offset += 8;
		return true;
	}

	if (wireType == kWireFixed32) {
		if (*offset + 4 > length) {
			return false;
		}
		*offset += 4;
		return true;
	}

	if (wireType == kWireBytes) {
		uint64_t fieldLen = 0;
		if (!DecodeVarint(data, length, offset, &fieldLen)) {
			return false;
		}
		if (fieldLen > std::numeric_limits<size_t>::max() - *offset) {
			return false;
		}
		const size_t len = static_cast<size_t>(fieldLen);
		if (*offset + len > length) {
			return false;
		}
		*offset += len;
		return true;
	}

	return false;
}

void EncodeLengthDelimited(uint64_t tag, const common::ByteView& view, std::vector<uint8_t>* out) {
	EncodeVarint(tag, out);
	EncodeVarint(view.length, out);
	out->insert(out->end(), view.data, view.data + view.length);
}

} // namespace

Napi::Value EncodeSyncActionData(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 3) {
		Napi::TypeError::New(env, "encodeSyncActionData(index, value, version, [padding]) requires 3+ arguments")
			.ThrowAsJavaScriptException();
		return env.Null();
	}

	common::ByteView index;
	if (!common::GetByteViewFromValue(env, info[0], "index", &index)) {
		return env.Null();
	}

	common::ByteView value;
	if (!common::GetByteViewFromValue(env, info[1], "value", &value)) {
		return env.Null();
	}

	uint32_t version = 0;
	if (!common::ReadUInt32FromValue(env, info[2], "version", &version)) {
		return env.Null();
	}

	common::ByteView padding;
	bool hasPadding = false;
	if (info.Length() > 3 && !info[3].IsUndefined() && !info[3].IsNull()) {
		if (!common::GetByteViewFromValue(env, info[3], "padding", &padding)) {
			return env.Null();
		}
		hasPadding = true;
	}

#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
	proto::SyncActionData action;
	action.set_index(reinterpret_cast<const char*>(index.data), index.length);
	if (value.length > static_cast<size_t>(std::numeric_limits<int>::max())) {
		return env.Null();
	}
	if (!action.mutable_value()->ParseFromArray(value.data, static_cast<int>(value.length))) {
		return env.Null();
	}
	if (hasPadding) {
		action.set_padding(reinterpret_cast<const char*>(padding.data), padding.length);
	}
	action.set_version(version);

	std::string encoded;
	if (!action.SerializeToString(&encoded)) {
		Napi::Error::New(env, "failed to serialize SyncActionData").ThrowAsJavaScriptException();
		return env.Null();
	}

	return Napi::Buffer<uint8_t>::Copy(env, reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size());
#else
	std::vector<uint8_t> out;
	out.reserve(
		index.length + value.length + (hasPadding ? padding.length : 0) + 32
	);

	EncodeLengthDelimited(kTagIndex, index, &out);
	EncodeLengthDelimited(kTagValue, value, &out);
	if (hasPadding) {
		EncodeLengthDelimited(kTagPadding, padding, &out);
	}
	EncodeVarint(kTagVersion, &out);
	EncodeVarint(version, &out);

	return Napi::Buffer<uint8_t>::Copy(env, out.data(), out.size());
#endif
}

Napi::Value DecodeSyncActionData(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1) {
		Napi::TypeError::New(env, "decodeSyncActionData(data) requires data").ThrowAsJavaScriptException();
		return env.Null();
	}

	common::ByteView encoded;
	if (!common::GetByteViewFromValue(env, info[0], "data", &encoded)) {
		return env.Null();
	}

#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
	proto::SyncActionData action;
	if (encoded.length > static_cast<size_t>(std::numeric_limits<int>::max())) {
		return env.Null();
	}
	if (!action.ParseFromArray(encoded.data, static_cast<int>(encoded.length))) {
		return env.Null();
	}
	if (!action.has_index() || !action.has_value()) {
		return env.Null();
	}

	Napi::Object out = Napi::Object::New(env);
	const std::string& index = action.index();
	std::string value;
	if (!action.value().SerializeToString(&value)) {
		return env.Null();
	}
	out.Set(
		"index",
		Napi::Buffer<uint8_t>::Copy(env, reinterpret_cast<const uint8_t*>(index.data()), index.size())
	);
	out.Set(
		"value",
		Napi::Buffer<uint8_t>::Copy(env, reinterpret_cast<const uint8_t*>(value.data()), value.size())
	);

	if (action.has_padding()) {
		const std::string& padding = action.padding();
		out.Set(
			"padding",
			Napi::Buffer<uint8_t>::Copy(env, reinterpret_cast<const uint8_t*>(padding.data()), padding.size())
		);
	}

	if (action.has_version()) {
		out.Set("version", Napi::Number::New(env, static_cast<double>(action.version())));
	}

	return out;
#else
	std::vector<uint8_t> index;
	std::vector<uint8_t> value;
	std::vector<uint8_t> padding;
	uint32_t version = 0;
	bool hasIndex = false;
	bool hasValue = false;
	bool hasVersion = false;
	bool hasPadding = false;

	size_t offset = 0;
	while (offset < encoded.length) {
		uint64_t tag = 0;
		if (!DecodeVarint(encoded.data, encoded.length, &offset, &tag)) {
			return env.Null();
		}
		if (tag == 0) {
			return env.Null();
		}

		const uint32_t fieldNumber = static_cast<uint32_t>(tag >> 3u);
		const uint32_t wireType = static_cast<uint32_t>(tag & 0x7u);

		if (fieldNumber == 4 && wireType == kWireVarint) {
			uint64_t parsedVersion = 0;
			if (!DecodeVarint(encoded.data, encoded.length, &offset, &parsedVersion)) {
				return env.Null();
			}
			if (parsedVersion > std::numeric_limits<uint32_t>::max()) {
				return env.Null();
			}
			version = static_cast<uint32_t>(parsedVersion);
			hasVersion = true;
			continue;
		}

		if ((fieldNumber == 1 || fieldNumber == 2 || fieldNumber == 3) && wireType == kWireBytes) {
			uint64_t lenRaw = 0;
			if (!DecodeVarint(encoded.data, encoded.length, &offset, &lenRaw)) {
				return env.Null();
			}
			if (lenRaw > std::numeric_limits<size_t>::max() - offset) {
				return env.Null();
			}
			const size_t len = static_cast<size_t>(lenRaw);
			if (offset + len > encoded.length) {
				return env.Null();
			}

			const uint8_t* ptr = encoded.data + offset;
			if (fieldNumber == 1) {
				index.assign(ptr, ptr + len);
				hasIndex = true;
			} else if (fieldNumber == 2) {
				value.assign(ptr, ptr + len);
				hasValue = true;
			} else {
				padding.assign(ptr, ptr + len);
				hasPadding = true;
			}

			offset += len;
			continue;
		}

		if (!SkipField(encoded.data, encoded.length, &offset, wireType)) {
			return env.Null();
		}
	}

	if (!hasIndex || !hasValue) {
		return env.Null();
	}

	Napi::Object out = Napi::Object::New(env);
	const uint8_t* indexPtr = index.empty() ? reinterpret_cast<const uint8_t*>("") : index.data();
	const uint8_t* valuePtr = value.empty() ? reinterpret_cast<const uint8_t*>("") : value.data();
	out.Set("index", Napi::Buffer<uint8_t>::Copy(env, indexPtr, index.size()));
	out.Set("value", Napi::Buffer<uint8_t>::Copy(env, valuePtr, value.size()));
	if (hasPadding) {
		const uint8_t* paddingPtr = padding.empty() ? reinterpret_cast<const uint8_t*>("") : padding.data();
		out.Set("padding", Napi::Buffer<uint8_t>::Copy(env, paddingPtr, padding.size()));
	}
	if (hasVersion) {
		out.Set("version", Napi::Number::New(env, static_cast<double>(version)));
	}

	return out;
#endif
}

} // namespace baileys_native
