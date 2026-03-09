#pragma once

#include <napi.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace baileys_native::appstate_internal {

struct DerivedMutationKey {
	std::vector<uint8_t> indexKey;
	std::vector<uint8_t> valueEncryptionKey;
	std::vector<uint8_t> valueMacKey;
};

struct MutationInput {
	uint32_t operation = 0;
	std::vector<uint8_t> indexMac;
	std::vector<uint8_t> valueBlob;
	std::vector<uint8_t> keyId;
};

std::string Base64Encode(const uint8_t* data, size_t length);

bool ReadDerivedMutationKeyMap(
	const Napi::Env& env,
	const Napi::Value& value,
	std::unordered_map<std::string, DerivedMutationKey>* out
);

bool ReadMutationInputs(const Napi::Env& env, const Napi::Value& value, std::vector<MutationInput>* out);

bool DecodeMutationToJs(
	const Napi::Env& env,
	const MutationInput& mutation,
	const std::unordered_map<std::string, DerivedMutationKey>& keyMap,
	bool validateMacs,
	bool encodeValueAsWire,
	Napi::Object* out
);

} // namespace baileys_native::appstate_internal

