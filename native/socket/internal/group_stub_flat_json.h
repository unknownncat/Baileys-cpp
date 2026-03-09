#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace baileys_native::socket_internal {

using FlatField = std::pair<std::string, std::optional<std::string>>;

std::string EncodeFlatObject(const std::vector<FlatField>& fields);
bool ParseFlatObject(const std::string& input, std::vector<FlatField>* out);

} // namespace baileys_native::socket_internal
