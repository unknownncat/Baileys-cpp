#pragma once

#include <napi.h>

#include <cstdint>
#include <string>
#include <vector>

namespace baileys_native::common {

struct ByteView {
	const uint8_t* data = nullptr;
	size_t length = 0;
};

bool ReadUInt32FromValue(const Napi::Env& env, const Napi::Value& value, const char* field, uint32_t* out);

bool ReadDoubleFromValue(const Napi::Env& env, const Napi::Value& value, const char* field, double* out);

bool ReadStringFromValue(const Napi::Env& env, const Napi::Value& value, const char* field, std::string* out);

bool GetByteViewFromValue(const Napi::Env& env, const Napi::Value& value, const char* field, ByteView* out);

bool CopyBytesFromValue(
	const Napi::Env& env,
	const Napi::Value& value,
	const char* field,
	std::vector<uint8_t>* out
);

bool CopyOptionalBytesFromValue(
	const Napi::Env& env,
	const Napi::Value& value,
	const char* field,
	std::vector<uint8_t>* out
);

bool CheckFitsU16(const Napi::Env& env, size_t len, const char* field);

Napi::Buffer<uint8_t> CopyVectorToBuffer(const Napi::Env& env, const std::vector<uint8_t>& data);
Napi::Buffer<uint8_t> MoveVectorToBuffer(const Napi::Env& env, std::vector<uint8_t>&& data);

} // namespace baileys_native::common
