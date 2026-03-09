#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace baileys_native::utils {

bool InflateZlibBytes(const uint8_t* input, size_t length, std::vector<uint8_t>* out, std::string* error);

} // namespace baileys_native::utils
