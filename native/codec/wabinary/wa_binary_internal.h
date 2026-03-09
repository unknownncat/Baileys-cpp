#pragma once

#include <napi.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace baileys_native::wabinary {

struct CodecTags {
	uint32_t listEmpty = 0;
	uint32_t dictionary0 = 236;
	uint32_t dictionary1 = 237;
	uint32_t dictionary2 = 238;
	uint32_t dictionary3 = 239;
	uint32_t interopJid = 245;
	uint32_t fbJid = 246;
	uint32_t adJid = 247;
	uint32_t list8 = 248;
	uint32_t list16 = 249;
	uint32_t jidPair = 250;
	uint32_t hex8 = 251;
	uint32_t binary8 = 252;
	uint32_t binary20 = 253;
	uint32_t binary32 = 254;
	uint32_t nibble8 = 255;
	uint32_t packedMax = 127;
};

struct TokenIndex {
	uint16_t index = 0;
	uint16_t dict = 0;
	bool hasDict = false;
};

struct CodecConfig {
	CodecTags tags;
	std::vector<std::string> singleByteTokens;
	std::vector<std::vector<std::string>> doubleByteTokens;
	std::unordered_map<std::string, TokenIndex> tokenMap;
};

struct DecodedJid {
	std::string user;
	std::string server;
	uint32_t device = 0;
	uint32_t domainType = 0;
	bool hasDevice = false;
};

class Reader {
public:
	Reader(const uint8_t* data, size_t len, size_t index = 0) : data_(data), len_(len), index_(index) {}

	bool ReadU8(uint8_t* out) {
		if (!CanRead(1)) return false;
		*out = data_[index_++];
		return true;
	}

	bool ReadBytes(size_t n, const uint8_t** out) {
		if (!CanRead(n)) return false;
		*out = data_ + index_;
		index_ += n;
		return true;
	}

	bool ReadInt(size_t n, bool littleEndian, uint32_t* out) {
		if (!CanRead(n) || n > 4) return false;
		uint32_t val = 0;
		for (size_t i = 0; i < n; ++i) {
			const size_t shift = littleEndian ? i : (n - 1 - i);
			val |= static_cast<uint32_t>(data_[index_++]) << (shift * 8);
		}
		*out = val;
		return true;
	}

	size_t Index() const {
		return index_;
	}

	void SetIndex(size_t value) {
		index_ = value;
	}

	bool CanRead(size_t n) const {
		return index_ <= len_ && n <= (len_ - index_);
	}

private:
	const uint8_t* data_ = nullptr;
	size_t len_ = 0;
	size_t index_ = 0;
};

class Writer {
public:
	void PushByte(uint8_t value) {
		data_.push_back(value);
	}

	void PushBytes(const uint8_t* data, size_t len) {
		data_.insert(data_.end(), data, data + len);
	}

	void PushBytes(const std::vector<uint8_t>& data) {
		data_.insert(data_.end(), data.begin(), data.end());
	}

	void PushInt(uint32_t value, size_t n, bool littleEndian = false) {
		for (size_t i = 0; i < n; ++i) {
			const size_t curShift = littleEndian ? i : n - 1 - i;
			data_.push_back(static_cast<uint8_t>((value >> (curShift * 8)) & 0xffu));
		}
	}

	const std::vector<uint8_t>& Data() const {
		return data_;
	}

	std::vector<uint8_t> TakeData() {
		return std::move(data_);
	}

private:
	std::vector<uint8_t> data_;
};

inline bool IsListTag(const CodecConfig& codec, uint32_t tag) {
	return tag == codec.tags.listEmpty || tag == codec.tags.list8 || tag == codec.tags.list16;
}

extern std::unique_ptr<CodecConfig> g_codec;

bool InitCodecFromOptions(const Napi::Env& env, const Napi::Object& opts, CodecConfig* out);
bool DecodeNode(const Napi::Env& env, Reader* reader, const CodecConfig& codec, Napi::Value* out, std::string* err);
bool EncodeNode(
	const Napi::Env& env,
	Writer* writer,
	const CodecConfig& codec,
	const Napi::Value& nodeVal,
	std::string* err
);

} // namespace baileys_native::wabinary
