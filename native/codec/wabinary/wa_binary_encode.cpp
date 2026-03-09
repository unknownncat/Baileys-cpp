#include "wa_binary_internal.h"

#include "common.h"

#include <charconv>
#include <limits>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace baileys_native::wabinary {

namespace {

inline bool IsNibble(const CodecConfig& codec, const std::string& str) {
	if (str.empty() || str.size() > codec.tags.packedMax) return false;
	for (char c : str) {
		if (!(c >= '0' && c <= '9') && c != '-' && c != '.') {
			return false;
		}
	}
	return true;
}

inline bool IsHex(const CodecConfig& codec, const std::string& str) {
	if (str.empty() || str.size() > codec.tags.packedMax) return false;
	for (char c : str) {
		const bool isNum = c >= '0' && c <= '9';
		const bool isHexUpper = c >= 'A' && c <= 'F';
		if (!isNum && !isHexUpper) {
			return false;
		}
	}
	return true;
}

uint8_t PackNibble(char c, bool* ok) {
	switch (c) {
		case '-':
			*ok = true;
			return 10;
		case '.':
			*ok = true;
			return 11;
		case '\0':
			*ok = true;
			return 15;
		default:
			if (c >= '0' && c <= '9') {
				*ok = true;
				return static_cast<uint8_t>(c - '0');
			}
			*ok = false;
			return 0;
	}
}

uint8_t PackHex(char c, bool* ok) {
	if (c >= '0' && c <= '9') {
		*ok = true;
		return static_cast<uint8_t>(c - '0');
	}
	if (c >= 'A' && c <= 'F') {
		*ok = true;
		return static_cast<uint8_t>(10 + (c - 'A'));
	}
	if (c >= 'a' && c <= 'f') {
		*ok = true;
		return static_cast<uint8_t>(10 + (c - 'a'));
	}
	if (c == '\0') {
		*ok = true;
		return 15;
	}
	*ok = false;
	return 0;
}

bool ParseUInt32Decimal(const std::string& value, uint32_t* out) {
	if (value.empty()) {
		return false;
	}

	uint32_t parsed = 0;
	const char* begin = value.data();
	const char* end = begin + value.size();
	const auto result = std::from_chars(begin, end, parsed, 10);
	if (result.ec != std::errc{} || result.ptr != end) {
		return false;
	}

	*out = parsed;
	return true;
}

bool TryDecodeJid(const std::string& jid, DecodedJid* out) {
	const size_t sep = jid.find('@');
	if (sep == std::string::npos) return false;

	out->server = jid.substr(sep + 1);
	std::string userCombined = jid.substr(0, sep);

	const size_t colon = userCombined.find(':');
	std::string userAgent = userCombined;
	std::string devicePart;
	if (colon != std::string::npos) {
		userAgent = userCombined.substr(0, colon);
		devicePart = userCombined.substr(colon + 1);
		uint32_t parsedDevice = 0;
		if (ParseUInt32Decimal(devicePart, &parsedDevice)) {
			out->hasDevice = true;
			out->device = parsedDevice;
		}
	}

	const size_t underscore = userAgent.find('_');
	out->user = underscore == std::string::npos ? userAgent : userAgent.substr(0, underscore);
	std::string agent = underscore == std::string::npos ? std::string() : userAgent.substr(underscore + 1);

	out->domainType = 0;
	if (out->server == "lid") {
		out->domainType = 1;
	} else if (out->server == "hosted") {
		out->domainType = 128;
	} else if (out->server == "hosted.lid") {
		out->domainType = 129;
	} else if (!agent.empty()) {
		uint32_t parsedAgent = 0;
		if (ParseUInt32Decimal(agent, &parsedAgent)) {
			out->domainType = parsedAgent;
		}
	}

	return true;
}

void WriteByteLength(Writer* writer, const CodecConfig& codec, uint32_t length) {
	if (length >= (1u << 20u)) {
		writer->PushByte(static_cast<uint8_t>(codec.tags.binary32));
		writer->PushInt(length, 4, false);
	} else if (length >= 256u) {
		writer->PushByte(static_cast<uint8_t>(codec.tags.binary20));
		writer->PushByte(static_cast<uint8_t>((length >> 16u) & 0x0fu));
		writer->PushByte(static_cast<uint8_t>((length >> 8u) & 0xffu));
		writer->PushByte(static_cast<uint8_t>(length & 0xffu));
	} else {
		writer->PushByte(static_cast<uint8_t>(codec.tags.binary8));
		writer->PushByte(static_cast<uint8_t>(length));
	}
}

void WriteStringRaw(Writer* writer, const CodecConfig& codec, const std::string& str) {
	WriteByteLength(writer, codec, static_cast<uint32_t>(str.size()));
	writer->PushBytes(reinterpret_cast<const uint8_t*>(str.data()), str.size());
}

void WriteListStart(Writer* writer, const CodecConfig& codec, uint32_t listSize) {
	if (listSize == 0) {
		writer->PushByte(static_cast<uint8_t>(codec.tags.listEmpty));
	} else if (listSize < 256u) {
		writer->PushByte(static_cast<uint8_t>(codec.tags.list8));
		writer->PushByte(static_cast<uint8_t>(listSize));
	} else {
		writer->PushByte(static_cast<uint8_t>(codec.tags.list16));
		writer->PushByte(static_cast<uint8_t>((listSize >> 8u) & 0xffu));
		writer->PushByte(static_cast<uint8_t>(listSize & 0xffu));
	}
}

bool WriteString(Writer* writer, const CodecConfig& codec, const std::string* value, std::string* err);

bool WriteJid(Writer* writer, const CodecConfig& codec, const DecodedJid& jid, std::string* err) {
	if (jid.hasDevice) {
		if (jid.device > 255u) {
			*err = "jid device out of range (max 255)";
			return false;
		}
		writer->PushByte(static_cast<uint8_t>(codec.tags.adJid));
		writer->PushByte(static_cast<uint8_t>(jid.domainType & 0xffu));
		writer->PushByte(static_cast<uint8_t>(jid.device & 0xffu));
		return WriteString(writer, codec, &jid.user, err);
	}

	writer->PushByte(static_cast<uint8_t>(codec.tags.jidPair));
	if (!jid.user.empty()) {
		if (!WriteString(writer, codec, &jid.user, err)) return false;
	} else {
		writer->PushByte(static_cast<uint8_t>(codec.tags.listEmpty));
	}
	return WriteString(writer, codec, &jid.server, err);
}

bool WritePackedBytes(Writer* writer, const CodecConfig& codec, const std::string& str, bool nibble, std::string* err) {
	if (str.size() > codec.tags.packedMax) {
		*err = "too many bytes to pack";
		return false;
	}

	writer->PushByte(static_cast<uint8_t>(nibble ? codec.tags.nibble8 : codec.tags.hex8));
	uint32_t roundedLength = static_cast<uint32_t>((str.size() + 1u) / 2u);
	if ((str.size() & 1u) != 0u) {
		roundedLength |= 128u;
	}
	writer->PushByte(static_cast<uint8_t>(roundedLength));

	size_t i = 0;
	for (; i + 1 < str.size(); i += 2) {
		bool okA = false;
		bool okB = false;
		const uint8_t pa = nibble ? PackNibble(str[i], &okA) : PackHex(str[i], &okA);
		const uint8_t pb = nibble ? PackNibble(str[i + 1], &okB) : PackHex(str[i + 1], &okB);
		if (!okA || !okB) {
			*err = "invalid packed character";
			return false;
		}
		writer->PushByte(static_cast<uint8_t>((pa << 4u) | pb));
	}

	if (i < str.size()) {
		bool okA = false;
		bool okB = false;
		const uint8_t pa = nibble ? PackNibble(str.back(), &okA) : PackHex(str.back(), &okA);
		const uint8_t pb = nibble ? PackNibble('\0', &okB) : PackHex('\0', &okB);
		if (!okA || !okB) {
			*err = "invalid packed character";
			return false;
		}
		writer->PushByte(static_cast<uint8_t>((pa << 4u) | pb));
	}
	return true;
}

bool WriteString(Writer* writer, const CodecConfig& codec, const std::string* value, std::string* err) {
	if (value == nullptr) {
		writer->PushByte(static_cast<uint8_t>(codec.tags.listEmpty));
		return true;
	}

	const std::string& str = *value;
	if (str.empty()) {
		WriteStringRaw(writer, codec, str);
		return true;
	}

	const auto token = codec.tokenMap.find(str);
	if (token != codec.tokenMap.end()) {
		if (token->second.hasDict) {
			writer->PushByte(static_cast<uint8_t>(codec.tags.dictionary0 + token->second.dict));
		}
		writer->PushByte(static_cast<uint8_t>(token->second.index));
		return true;
	}

	if (IsNibble(codec, str)) {
		return WritePackedBytes(writer, codec, str, true, err);
	}
	if (IsHex(codec, str)) {
		return WritePackedBytes(writer, codec, str, false, err);
	}

	DecodedJid jid;
	if (TryDecodeJid(str, &jid)) {
		return WriteJid(writer, codec, jid, err);
	}

	WriteStringRaw(writer, codec, str);
	return true;
}

bool IsU8TypedArray(const Napi::Value& value) {
	if (!value.IsTypedArray()) return false;
	auto ta = value.As<Napi::TypedArray>();
	return ta.TypedArrayType() == napi_uint8_array;
}

} // namespace

bool EncodeNode(
	const Napi::Env& env,
	Writer* writer,
	const CodecConfig& codec,
	const Napi::Value& nodeVal,
	std::string* err
) {
	if (!nodeVal.IsObject()) {
		*err = "invalid node object";
		return false;
	}

	Napi::Object node = nodeVal.As<Napi::Object>();
	Napi::Value tagVal = node.Get("tag");
	if (!tagVal.IsString()) {
		*err = "invalid node tag";
		return false;
	}
	const std::string tag = tagVal.As<Napi::String>().Utf8Value();
	if (tag.empty()) {
		*err = "invalid node tag";
		return false;
	}

	Napi::Value attrsVal = node.Get("attrs");
	std::vector<std::pair<std::string, std::string>> attrs;
	if (attrsVal.IsObject()) {
		Napi::Object attrsObj = attrsVal.As<Napi::Object>();
		Napi::Array names = attrsObj.GetPropertyNames();
		const uint32_t len = names.Length();
		attrs.reserve(len);
		for (uint32_t i = 0; i < len; ++i) {
			Napi::Value keyVal = names.Get(i);
			if (!keyVal.IsString()) continue;
			const std::string key = keyVal.As<Napi::String>().Utf8Value();
			Napi::Value value = attrsObj.Get(keyVal);
			if (value.IsUndefined() || value.IsNull()) continue;
			if (!value.IsString()) continue;
			attrs.emplace_back(key, value.As<Napi::String>().Utf8Value());
		}
	}

	Napi::Value contentVal = node.Get("content");
	const bool hasContent = !contentVal.IsUndefined();
	const uint32_t listSize = static_cast<uint32_t>(2u * attrs.size() + 1u + (hasContent ? 1u : 0u));

	WriteListStart(writer, codec, listSize);
	if (!WriteString(writer, codec, &tag, err)) return false;

	for (const auto& attr : attrs) {
		if (!WriteString(writer, codec, &attr.first, err)) return false;
		if (!WriteString(writer, codec, &attr.second, err)) return false;
	}

	if (!hasContent) {
		return true;
	}

	if (contentVal.IsString()) {
		const std::string content = contentVal.As<Napi::String>().Utf8Value();
		return WriteString(writer, codec, &content, err);
	}

	if (contentVal.IsBuffer() || IsU8TypedArray(contentVal)) {
		common::ByteView bytes;
		if (!common::GetByteViewFromValue(env, contentVal, "node.content", &bytes)) {
			*err = "invalid binary content";
			return false;
		}
		if (bytes.length > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
			*err = "binary content too large";
			return false;
		}
		WriteByteLength(writer, codec, static_cast<uint32_t>(bytes.length));
		writer->PushBytes(bytes.data, bytes.length);
		return true;
	}

	if (contentVal.IsArray()) {
		Napi::Array arr = contentVal.As<Napi::Array>();
		const uint32_t len = arr.Length();
		std::vector<Napi::Value> valid;
		valid.reserve(len);
		for (uint32_t i = 0; i < len; ++i) {
			Napi::Value item = arr.Get(i);
			if (item.IsUndefined() || item.IsNull()) continue;
			if (item.IsString() || item.IsBuffer() || IsU8TypedArray(item)) {
				valid.push_back(item);
				continue;
			}
			if (item.IsObject()) {
				Napi::Object obj = item.As<Napi::Object>();
				Napi::Value itemTag = obj.Get("tag");
				if (!itemTag.IsUndefined() && !itemTag.IsNull() && itemTag.ToBoolean()) {
					valid.push_back(item);
				}
			}
		}

		WriteListStart(writer, codec, static_cast<uint32_t>(valid.size()));
		for (const auto& item : valid) {
			if (item.IsString()) {
				const std::string s = item.As<Napi::String>().Utf8Value();
				if (!WriteString(writer, codec, &s, err)) return false;
			} else if (item.IsBuffer() || IsU8TypedArray(item)) {
				common::ByteView bytes;
				if (!common::GetByteViewFromValue(env, item, "node.content[]", &bytes)) {
					*err = "invalid binary child";
					return false;
				}
				if (bytes.length > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
					*err = "binary child too large";
					return false;
				}
				WriteByteLength(writer, codec, static_cast<uint32_t>(bytes.length));
				writer->PushBytes(bytes.data, bytes.length);
			} else {
				if (!EncodeNode(env, writer, codec, item, err)) return false;
			}
		}
		return true;
	}

	*err = "invalid children for header";
	return false;
}

} // namespace baileys_native::wabinary
