#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace baileys_native::common::safe_copy {

inline bool CheckedAddSize(size_t left, size_t right, size_t* out) {
	if (right > std::numeric_limits<size_t>::max() - left) {
		return false;
	}
	*out = left + right;
	return true;
}

inline bool AppendBytes(std::vector<uint8_t>* out, const uint8_t* data, size_t length) {
	if (out == nullptr) {
		return false;
	}
	if (length == 0) {
		return true;
	}
	if (data == nullptr) {
		return false;
	}

	size_t targetSize = 0;
	if (!CheckedAddSize(out->size(), length, &targetSize)) {
		return false;
	}

	const size_t offset = out->size();
	out->resize(targetSize);
	std::memcpy(out->data() + offset, data, length);
	return true;
}

inline bool ShiftLeft(std::vector<uint8_t>* data, size_t consumedPrefixBytes) {
	if (data == nullptr) {
		return false;
	}
	if (consumedPrefixBytes == 0) {
		return true;
	}
	if (consumedPrefixBytes > data->size()) {
		return false;
	}

	const size_t remaining = data->size() - consumedPrefixBytes;
	if (remaining > 0) {
		std::memmove(data->data(), data->data() + consumedPrefixBytes, remaining);
	}
	data->resize(remaining);
	return true;
}

} // namespace baileys_native::common::safe_copy
