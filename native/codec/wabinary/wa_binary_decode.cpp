#include "wa_binary_internal.h"

#include <string>

namespace baileys_native::wabinary {

namespace {

bool ReadListSize(Reader* reader, const CodecConfig& codec, uint32_t tag, uint32_t* out, std::string* err) {
	if (tag == codec.tags.listEmpty) {
		*out = 0;
		return true;
	}
	if (tag == codec.tags.list8) {
		uint8_t value = 0;
		if (!reader->ReadU8(&value)) {
			*err = "end of stream";
			return false;
		}
		*out = value;
		return true;
	}
	if (tag == codec.tags.list16) {
		uint32_t value = 0;
		if (!reader->ReadInt(2, false, &value)) {
			*err = "end of stream";
			return false;
		}
		*out = value;
		return true;
	}
	*err = "invalid tag for list size: " + std::to_string(tag);
	return false;
}

uint8_t UnpackHex(uint8_t value, bool* ok) {
	if (value < 16) {
		*ok = true;
		return static_cast<uint8_t>(value < 10 ? ('0' + value) : ('A' + value - 10));
	}
	*ok = false;
	return 0;
}

uint8_t UnpackNibble(uint8_t value, bool* ok) {
	if (value <= 9) {
		*ok = true;
		return static_cast<uint8_t>('0' + value);
	}
	switch (value) {
		case 10:
			*ok = true;
			return static_cast<uint8_t>('-');
		case 11:
			*ok = true;
			return static_cast<uint8_t>('.');
		case 15:
			*ok = true;
			return 0;
		default:
			*ok = false;
			return 0;
	}
}

bool ReadPacked8(Reader* reader, const CodecConfig& codec, uint32_t tag, std::string* out, std::string* err) {
	uint8_t startByte = 0;
	if (!reader->ReadU8(&startByte)) {
		*err = "end of stream";
		return false;
	}
	const uint32_t pairs = startByte & 127u;
	std::string value;
	value.reserve(pairs * 2);
	for (uint32_t i = 0; i < pairs; ++i) {
		uint8_t curByte = 0;
		if (!reader->ReadU8(&curByte)) {
			*err = "end of stream";
			return false;
		}

		bool okHi = false;
		bool okLo = false;
		uint8_t hi = 0;
		uint8_t lo = 0;

		if (tag == codec.tags.nibble8) {
			hi = UnpackNibble(static_cast<uint8_t>((curByte & 0xf0u) >> 4u), &okHi);
			lo = UnpackNibble(static_cast<uint8_t>(curByte & 0x0fu), &okLo);
		} else if (tag == codec.tags.hex8) {
			hi = UnpackHex(static_cast<uint8_t>((curByte & 0xf0u) >> 4u), &okHi);
			lo = UnpackHex(static_cast<uint8_t>(curByte & 0x0fu), &okLo);
		} else {
			*err = "unknown packed tag";
			return false;
		}

		if (!okHi || !okLo) {
			*err = "invalid packed byte";
			return false;
		}
		value.push_back(static_cast<char>(hi));
		value.push_back(static_cast<char>(lo));
	}

	if ((startByte >> 7u) != 0 && !value.empty()) {
		value.pop_back();
	}
	*out = std::move(value);
	return true;
}

bool GetTokenDouble(const CodecConfig& codec, uint32_t dict, uint32_t index, std::string* out, std::string* err) {
	if (dict >= codec.doubleByteTokens.size()) {
		*err = "invalid double token dict";
		return false;
	}
	const auto& table = codec.doubleByteTokens[dict];
	if (index >= table.size()) {
		*err = "invalid double token";
		return false;
	}
	*out = table[index];
	return true;
}

bool ReadString(Reader* reader, const CodecConfig& codec, uint32_t tag, std::string* out, std::string* err);

bool ReadStringTag(Reader* reader, const CodecConfig& codec, std::string* out, std::string* err) {
	uint8_t tag = 0;
	if (!reader->ReadU8(&tag)) {
		*err = "end of stream";
		return false;
	}
	return ReadString(reader, codec, tag, out, err);
}

bool ReadString(Reader* reader, const CodecConfig& codec, uint32_t tag, std::string* out, std::string* err) {
	if (tag >= 1 && tag < codec.singleByteTokens.size()) {
		*out = codec.singleByteTokens[tag];
		return true;
	}

	if (tag == codec.tags.dictionary0 || tag == codec.tags.dictionary1 || tag == codec.tags.dictionary2 ||
		tag == codec.tags.dictionary3) {
		uint8_t idx = 0;
		if (!reader->ReadU8(&idx)) {
			*err = "end of stream";
			return false;
		}
		return GetTokenDouble(codec, tag - codec.tags.dictionary0, idx, out, err);
	}

	if (tag == codec.tags.listEmpty) {
		out->clear();
		return true;
	}

	if (tag == codec.tags.binary8) {
		uint8_t len = 0;
		if (!reader->ReadU8(&len)) {
			*err = "end of stream";
			return false;
		}
		const uint8_t* bytes = nullptr;
		if (!reader->ReadBytes(len, &bytes)) {
			*err = "end of stream";
			return false;
		}
		out->assign(reinterpret_cast<const char*>(bytes), len);
		return true;
	}

	if (tag == codec.tags.binary20) {
		uint32_t raw = 0;
		if (!reader->ReadInt(3, false, &raw)) {
			*err = "end of stream";
			return false;
		}
		const uint32_t len = ((raw >> 16u) & 0x0fu) << 16u | (raw & 0xffffu);
		const uint8_t* bytes = nullptr;
		if (!reader->ReadBytes(len, &bytes)) {
			*err = "end of stream";
			return false;
		}
		out->assign(reinterpret_cast<const char*>(bytes), len);
		return true;
	}

	if (tag == codec.tags.binary32) {
		uint32_t len = 0;
		if (!reader->ReadInt(4, false, &len)) {
			*err = "end of stream";
			return false;
		}
		const uint8_t* bytes = nullptr;
		if (!reader->ReadBytes(len, &bytes)) {
			*err = "end of stream";
			return false;
		}
		out->assign(reinterpret_cast<const char*>(bytes), len);
		return true;
	}

	if (tag == codec.tags.jidPair) {
		std::string i;
		std::string j;
		if (!ReadStringTag(reader, codec, &i, err)) return false;
		if (!ReadStringTag(reader, codec, &j, err)) return false;
		if (j.empty()) {
			*err = "invalid jid pair";
			return false;
		}
		*out = i + "@" + j;
		return true;
	}

	if (tag == codec.tags.adJid) {
		uint8_t rawDomainType = 0;
		uint8_t device = 0;
		if (!reader->ReadU8(&rawDomainType) || !reader->ReadU8(&device)) {
			*err = "end of stream";
			return false;
		}
		std::string user;
		if (!ReadStringTag(reader, codec, &user, err)) return false;
		std::string server = "s.whatsapp.net";
		if (rawDomainType == 1) {
			server = "lid";
		} else if (rawDomainType == 128) {
			server = "hosted";
		} else if (rawDomainType == 129) {
			server = "hosted.lid";
		}

		*out = user;
		if (device != 0) {
			*out += ":" + std::to_string(device);
		}
		*out += "@" + server;
		return true;
	}

	if (tag == codec.tags.fbJid) {
		std::string user;
		std::string server;
		uint32_t device = 0;
		if (!ReadStringTag(reader, codec, &user, err)) return false;
		if (!reader->ReadInt(2, false, &device)) {
			*err = "end of stream";
			return false;
		}
		if (!ReadStringTag(reader, codec, &server, err)) return false;
		*out = user + ":" + std::to_string(device) + "@" + server;
		return true;
	}

	if (tag == codec.tags.interopJid) {
		std::string user;
		uint32_t device = 0;
		uint32_t integrator = 0;
		if (!ReadStringTag(reader, codec, &user, err)) return false;
		if (!reader->ReadInt(2, false, &device) || !reader->ReadInt(2, false, &integrator)) {
			*err = "end of stream";
			return false;
		}

		std::string server = "interop";
		const size_t before = reader->Index();
		if (reader->CanRead(1)) {
			std::string localErr;
			std::string maybeServer;
			if (ReadStringTag(reader, codec, &maybeServer, &localErr)) {
				server = maybeServer;
			} else {
				reader->SetIndex(before);
			}
		}

		*out = std::to_string(integrator) + "-" + user + ":" + std::to_string(device) + "@" + server;
		return true;
	}

	if (tag == codec.tags.hex8 || tag == codec.tags.nibble8) {
		return ReadPacked8(reader, codec, tag, out, err);
	}

	*err = "invalid string with tag";
	return false;
}

} // namespace

bool DecodeNode(
	const Napi::Env& env,
	Reader* reader,
	const CodecConfig& codec,
	Napi::Value* out,
	std::string* err
) {
	uint8_t listTag = 0;
	if (!reader->ReadU8(&listTag)) {
		*err = "end of stream";
		return false;
	}

	uint32_t listSize = 0;
	if (!ReadListSize(reader, codec, listTag, &listSize, err)) {
		return false;
	}

	std::string header;
	if (!ReadStringTag(reader, codec, &header, err)) {
		return false;
	}
	if (listSize == 0 || header.empty()) {
		*err = "invalid node";
		return false;
	}

	Napi::Object attrs = Napi::Object::New(env);
	const uint32_t attributesLength = (listSize - 1) >> 1;
	for (uint32_t i = 0; i < attributesLength; ++i) {
		std::string key;
		std::string value;
		if (!ReadStringTag(reader, codec, &key, err)) return false;
		if (!ReadStringTag(reader, codec, &value, err)) return false;
		attrs.Set(key, Napi::String::New(env, value));
	}

	Napi::Object node = Napi::Object::New(env);
	node.Set("tag", Napi::String::New(env, header));
	node.Set("attrs", attrs);

	if ((listSize % 2u) == 0u) {
		uint8_t tag = 0;
		if (!reader->ReadU8(&tag)) {
			*err = "end of stream";
			return false;
		}

		if (IsListTag(codec, tag)) {
			uint32_t count = 0;
			if (!ReadListSize(reader, codec, tag, &count, err)) return false;
			Napi::Array children = Napi::Array::New(env, count);
			for (uint32_t i = 0; i < count; ++i) {
				Napi::Value child;
				if (!DecodeNode(env, reader, codec, &child, err)) return false;
				children.Set(i, child);
			}
			node.Set("content", children);
		} else if (tag == codec.tags.binary8 || tag == codec.tags.binary20 || tag == codec.tags.binary32) {
			uint32_t len = 0;
			if (tag == codec.tags.binary8) {
				uint8_t x = 0;
				if (!reader->ReadU8(&x)) {
					*err = "end of stream";
					return false;
				}
				len = x;
			} else if (tag == codec.tags.binary20) {
				uint32_t raw = 0;
				if (!reader->ReadInt(3, false, &raw)) {
					*err = "end of stream";
					return false;
				}
				len = ((raw >> 16u) & 0x0fu) << 16u | (raw & 0xffffu);
			} else {
				if (!reader->ReadInt(4, false, &len)) {
					*err = "end of stream";
					return false;
				}
			}

			const uint8_t* bytes = nullptr;
			if (!reader->ReadBytes(len, &bytes)) {
				*err = "end of stream";
				return false;
			}
			node.Set("content", Napi::Buffer<uint8_t>::Copy(env, bytes, len));
		} else {
			std::string decoded;
			if (!ReadString(reader, codec, tag, &decoded, err)) return false;
			node.Set("content", Napi::String::New(env, decoded));
		}
	}

	*out = node;
	return true;
}

} // namespace baileys_native::wabinary

