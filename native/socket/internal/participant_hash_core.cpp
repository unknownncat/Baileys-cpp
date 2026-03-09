#include "socket/internal/participant_hash_core.h"

#include "media/internal/cng_crypto.h"
#include "utils/sha256.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace baileys_native::socket_internal {

namespace {

constexpr std::array<char, 64> kBase64Alphabet = {
	'A',
	'B',
	'C',
	'D',
	'E',
	'F',
	'G',
	'H',
	'I',
	'J',
	'K',
	'L',
	'M',
	'N',
	'O',
	'P',
	'Q',
	'R',
	'S',
	'T',
	'U',
	'V',
	'W',
	'X',
	'Y',
	'Z',
	'a',
	'b',
	'c',
	'd',
	'e',
	'f',
	'g',
	'h',
	'i',
	'j',
	'k',
	'l',
	'm',
	'n',
	'o',
	'p',
	'q',
	'r',
	's',
	't',
	'u',
	'v',
	'w',
	'x',
	'y',
	'z',
	'0',
	'1',
	'2',
	'3',
	'4',
	'5',
	'6',
	'7',
	'8',
	'9',
	'+',
	'/'
};

std::string Base64Encode(const uint8_t* data, size_t length) {
	if (length == 0) {
		return std::string();
	}

	std::string out;
	out.reserve(((length + 2) / 3) * 4);

	size_t i = 0;
	while (i + 2 < length) {
		const uint32_t chunk = (static_cast<uint32_t>(data[i]) << 16u) |
			(static_cast<uint32_t>(data[i + 1]) << 8u) |
			static_cast<uint32_t>(data[i + 2]);
		out.push_back(kBase64Alphabet[(chunk >> 18u) & 0x3fu]);
		out.push_back(kBase64Alphabet[(chunk >> 12u) & 0x3fu]);
		out.push_back(kBase64Alphabet[(chunk >> 6u) & 0x3fu]);
		out.push_back(kBase64Alphabet[chunk & 0x3fu]);
		i += 3;
	}

	if (i < length) {
		const uint32_t chunk = static_cast<uint32_t>(data[i]) << 16u;
		out.push_back(kBase64Alphabet[(chunk >> 18u) & 0x3fu]);
		if (i + 1 < length) {
			const uint32_t withSecond = chunk | (static_cast<uint32_t>(data[i + 1]) << 8u);
			out.push_back(kBase64Alphabet[(withSecond >> 12u) & 0x3fu]);
			out.push_back(kBase64Alphabet[(withSecond >> 6u) & 0x3fu]);
			out.push_back('=');
		} else {
			out.push_back(kBase64Alphabet[(chunk >> 12u) & 0x3fu]);
			out.push_back('=');
			out.push_back('=');
		}
	}

	return out;
}

} // namespace

bool GenerateParticipantHashV2(const std::vector<std::string>& participants, std::string* out, std::string* error) {
	if (out == nullptr || error == nullptr) {
		return false;
	}

	std::vector<std::string> sorted = participants;
	std::sort(sorted.begin(), sorted.end());

	size_t totalLength = 0;
	for (const auto& participant : sorted) {
		totalLength += participant.size();
	}

	std::string joined;
	joined.reserve(totalLength);
	for (const auto& participant : sorted) {
		joined.append(participant);
	}

	std::array<uint8_t, 32> digestBytes{};
	const uint8_t* joinedBytes = reinterpret_cast<const uint8_t*>(joined.data());

#ifdef _WIN32
	media_internal::CngHashState hashState;
	if (!hashState.InitSha256(error)) {
		return false;
	}

	if (!hashState.Update(joinedBytes, joined.size(), error)) {
		return false;
	}

	std::vector<uint8_t> digestVec;
	if (!hashState.Final(&digestVec, error)) {
		return false;
	}
	if (digestVec.size() != digestBytes.size()) {
		*error = "unexpected SHA-256 digest size";
		return false;
	}
	std::memcpy(digestBytes.data(), digestVec.data(), digestBytes.size());
#else
	if (!utils::Sha256(joinedBytes, joined.size(), &digestBytes)) {
		*error = "failed to compute SHA-256 digest";
		return false;
	}
#endif

	const std::string hashB64 = Base64Encode(digestBytes.data(), digestBytes.size());
	*out = "2:" + hashB64.substr(0, std::min<size_t>(6, hashB64.size()));
	return true;
}

} // namespace baileys_native::socket_internal
