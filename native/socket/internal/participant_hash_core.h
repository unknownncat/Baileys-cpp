#pragma once

#include <string>
#include <vector>

namespace baileys_native::socket_internal {

bool GenerateParticipantHashV2(const std::vector<std::string>& participants, std::string* out, std::string* error);

} // namespace baileys_native::socket_internal
