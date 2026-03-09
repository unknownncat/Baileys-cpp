#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace baileys_native::utils {

bool Sha256(const uint8_t* data, size_t length, std::array<uint8_t, 32>* out);

} // namespace baileys_native::utils
