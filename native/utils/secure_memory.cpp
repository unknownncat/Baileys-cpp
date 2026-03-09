#include "utils/secure_memory.h"

#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace baileys_native::utils {

void SecureZero(void* data, size_t length) {
	if (data == nullptr || length == 0) {
		return;
	}

#ifdef _WIN32
	SecureZeroMemory(data, length);
#else
	volatile uint8_t* ptr = static_cast<volatile uint8_t*>(data);
	for (size_t i = 0; i < length; ++i) {
		ptr[i] = 0;
	}
#endif
}

void SecureZeroVector(std::vector<uint8_t>* bytes) {
	if (bytes == nullptr) {
		return;
	}
	if (!bytes->empty()) {
		SecureZero(bytes->data(), bytes->size());
	}
	bytes->clear();
}

void SecureZeroString(std::string* value) {
	if (value == nullptr) {
		return;
	}
	if (!value->empty()) {
		SecureZero(value->data(), value->size());
	}
	value->clear();
}

} // namespace baileys_native::utils
