#pragma once

#include <napi.h>

#if defined(BAILEYS_HAS_NATIVE_WAPROTO) && BAILEYS_HAS_NATIVE_WAPROTO

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace baileys_native::proto_reflection::from_js_internal {

constexpr uint32_t kMaxProtoDepth = 128;

bool ThrowTypeError(const Napi::Env& env, const std::string& message);
bool ThrowRangeError(const Napi::Env& env, const std::string& message);

bool ParseDecimalUnsigned(std::string_view text, uint64_t* out);
bool ParseDecimalSigned(std::string_view text, int64_t* out);

bool ReadInt64FromValue(const Napi::Env& env, const Napi::Value& value, const char* fieldName, int64_t* out);
bool ReadUInt64FromValue(const Napi::Env& env, const Napi::Value& value, const char* fieldName, uint64_t* out);
bool ReadDoubleFromValue(const Napi::Env& env, const Napi::Value& value, const char* fieldName, double* out);
bool ReadBoolFromValue(const Napi::Env& env, const Napi::Value& value, const char* fieldName, bool* out);
bool ReadStringFromValue(const Napi::Env& env, const Napi::Value& value, const char* fieldName, std::string* out);
bool ReadBytesFromValue(const Napi::Env& env, const Napi::Value& value, const char* fieldName, std::string* out);
bool ReadInt32FromString(std::string_view text, int32_t* out);
bool ReadUInt32FromString(std::string_view text, uint32_t* out);

bool SetMapKeyFromString(
	const Napi::Env& env,
	const std::string& keyText,
	google::protobuf::Message* mapEntry,
	const google::protobuf::FieldDescriptor* keyField
);

bool SetFieldFromValue(
	const Napi::Env& env,
	const Napi::Value& value,
	google::protobuf::Message* message,
	const google::protobuf::FieldDescriptor* field,
	uint32_t depth
);

bool TryGetOwnFieldValue(
	const Napi::Object& source,
	const google::protobuf::FieldDescriptor* field,
	Napi::Value* out
);

} // namespace baileys_native::proto_reflection::from_js_internal

#endif

