#include "appstate/internal/mutation_engine_internal.h"

#include "common.h"

#include <array>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace baileys_native::appstate_internal {

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

bool ReadDerivedMutationKey(
	const Napi::Env& env,
	const Napi::Value& value,
	const char* field,
	DerivedMutationKey* out
) {
	if (!value.IsObject()) {
		Napi::TypeError::New(env, std::string("Expected object for field: ") + field).ThrowAsJavaScriptException();
		return false;
	}

	Napi::Object obj = value.As<Napi::Object>();
	if (!common::CopyBytesFromValue(env, obj.Get("indexKey"), "key.indexKey", &out->indexKey)) {
		return false;
	}
	if (!common::CopyBytesFromValue(
			env,
			obj.Get("valueEncryptionKey"),
			"key.valueEncryptionKey",
			&out->valueEncryptionKey
		)) {
		return false;
	}
	if (!common::CopyBytesFromValue(env, obj.Get("valueMacKey"), "key.valueMacKey", &out->valueMacKey)) {
		return false;
	}

	if (out->indexKey.size() != 32 || out->valueEncryptionKey.size() != 32 || out->valueMacKey.size() != 32) {
		Napi::RangeError::New(env, "Derived mutation keys must be 32 bytes each").ThrowAsJavaScriptException();
		return false;
	}

	return true;
}

bool ReadMutationInput(const Napi::Env& env, const Napi::Value& value, MutationInput* out) {
	if (!value.IsObject()) {
		Napi::TypeError::New(env, "mutations[] must be objects").ThrowAsJavaScriptException();
		return false;
	}

	Napi::Object obj = value.As<Napi::Object>();
	Napi::Value operationValue = obj.Get("operation");
	if (!operationValue.IsUndefined() && !operationValue.IsNull()) {
		if (!common::ReadUInt32FromValue(env, operationValue, "mutations[].operation", &out->operation)) {
			return false;
		}
	}

	if (!common::CopyBytesFromValue(env, obj.Get("indexMac"), "mutations[].indexMac", &out->indexMac)) {
		return false;
	}
	if (!common::CopyBytesFromValue(env, obj.Get("valueBlob"), "mutations[].valueBlob", &out->valueBlob)) {
		return false;
	}
	if (!common::CopyBytesFromValue(env, obj.Get("keyId"), "mutations[].keyId", &out->keyId)) {
		return false;
	}

	if (out->indexMac.size() != 32) {
		Napi::RangeError::New(env, "mutations[].indexMac must be 32 bytes").ThrowAsJavaScriptException();
		return false;
	}
	if (out->valueBlob.size() < 48) {
		Napi::RangeError::New(env, "mutations[].valueBlob is too short").ThrowAsJavaScriptException();
		return false;
	}
	if (out->keyId.empty()) {
		Napi::RangeError::New(env, "mutations[].keyId must not be empty").ThrowAsJavaScriptException();
		return false;
	}

	return true;
}

} // namespace

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

bool ReadDerivedMutationKeyMap(
	const Napi::Env& env,
	const Napi::Value& value,
	std::unordered_map<std::string, DerivedMutationKey>* out
) {
	out->clear();
	if (!value.IsObject()) {
		Napi::TypeError::New(env, "keyMap must be an object").ThrowAsJavaScriptException();
		return false;
	}

	Napi::Object obj = value.As<Napi::Object>();
	Napi::Array keys = obj.GetPropertyNames();
	out->reserve(keys.Length());
	for (uint32_t i = 0; i < keys.Length(); ++i) {
		Napi::Value keyValue = keys.Get(i);
		if (!keyValue.IsString()) {
			continue;
		}
		std::string base64Key = keyValue.As<Napi::String>().Utf8Value();
		DerivedMutationKey keyMaterial;
		if (!ReadDerivedMutationKey(env, obj.Get(keyValue), "keyMap[key]", &keyMaterial)) {
			return false;
		}
		out->emplace(std::move(base64Key), std::move(keyMaterial));
	}

	return true;
}

bool ReadMutationInputs(const Napi::Env& env, const Napi::Value& value, std::vector<MutationInput>* out) {
	out->clear();
	if (!value.IsArray()) {
		Napi::TypeError::New(env, "mutations must be an array").ThrowAsJavaScriptException();
		return false;
	}

	Napi::Array arr = value.As<Napi::Array>();
	out->reserve(arr.Length());
	for (uint32_t i = 0; i < arr.Length(); ++i) {
		MutationInput mutation;
		if (!ReadMutationInput(env, arr.Get(i), &mutation)) {
			return false;
		}
		out->push_back(std::move(mutation));
	}

	return true;
}

} // namespace baileys_native::appstate_internal

