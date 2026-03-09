#pragma once

#include <napi.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace baileys_native::sender_key_codec {

inline constexpr uint8_t kMagic[] = {'B', 'S', 'K', 'R'};
inline constexpr uint8_t kFormatVersion = 1;

struct MessageKeyEntry {
	uint32_t iteration = 0;
	std::vector<uint8_t> seed;
};

struct SenderKeyStateNative {
	uint32_t senderKeyId = 0;
	uint32_t chainIteration = 0;
	std::vector<uint8_t> chainSeed;
	std::vector<uint8_t> signingPublic;
	std::vector<uint8_t> signingPrivate;
	std::vector<MessageKeyEntry> senderMessageKeys;
};

class DataReader {
public:
	DataReader(const uint8_t* data, size_t length) : data_(data), length_(length) {}

	bool ReadU8(uint8_t* out) {
		if (!CanRead(1)) return false;
		*out = data_[offset_++];
		return true;
	}

	bool ReadU16LE(uint16_t* out) {
		if (!CanRead(2)) return false;
		*out = static_cast<uint16_t>(data_[offset_]) |
			static_cast<uint16_t>(data_[offset_ + 1] << 8);
		offset_ += 2;
		return true;
	}

	bool ReadU32LE(uint32_t* out) {
		if (!CanRead(4)) return false;
		*out = static_cast<uint32_t>(data_[offset_]) |
			static_cast<uint32_t>(data_[offset_ + 1] << 8) |
			static_cast<uint32_t>(data_[offset_ + 2] << 16) |
			static_cast<uint32_t>(data_[offset_ + 3] << 24);
		offset_ += 4;
		return true;
	}

	bool ReadBytes(size_t len, std::vector<uint8_t>* out) {
		if (!CanRead(len)) return false;
		out->assign(data_ + offset_, data_ + offset_ + len);
		offset_ += len;
		return true;
	}

	size_t Remaining() const {
		return length_ - offset_;
	}

private:
	bool CanRead(size_t bytes) const {
		return offset_ <= length_ && bytes <= (length_ - offset_);
	}

	const uint8_t* data_ = nullptr;
	size_t length_ = 0;
	size_t offset_ = 0;
};

inline void WriteU16LE(std::vector<uint8_t>* out, uint16_t value) {
	out->push_back(static_cast<uint8_t>(value & 0xffu));
	out->push_back(static_cast<uint8_t>((value >> 8) & 0xffu));
}

inline void WriteU32LE(std::vector<uint8_t>* out, uint32_t value) {
	out->push_back(static_cast<uint8_t>(value & 0xffu));
	out->push_back(static_cast<uint8_t>((value >> 8) & 0xffu));
	out->push_back(static_cast<uint8_t>((value >> 16) & 0xffu));
	out->push_back(static_cast<uint8_t>((value >> 24) & 0xffu));
}

bool IsBinarySenderKeyRaw(const uint8_t* data, size_t len);
Napi::Object BuildStateObject(const Napi::Env& env, const SenderKeyStateNative& state);
bool ParseStatesArray(const Napi::Env& env, const Napi::Value& value, std::vector<SenderKeyStateNative>* out);

} // namespace baileys_native::sender_key_codec