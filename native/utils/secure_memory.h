#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace baileys_native::utils {

void SecureZero(void* data, size_t length);
void SecureZeroVector(std::vector<uint8_t>* bytes);
void SecureZeroString(std::string* value);

template <size_t N>
inline void SecureZeroArray(std::array<uint8_t, N>* value) {
	if (value == nullptr) {
		return;
	}
	SecureZero(value->data(), N);
}

} // namespace baileys_native::utils
