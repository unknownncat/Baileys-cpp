#include "proto/reflection/internal/protobuf_reflection_from_js_internal.h"

#if defined(BAILEYS_HAS_NATIVE_WAPROTO) && BAILEYS_HAS_NATIVE_WAPROTO

#include "protobuf_reflection_codec.h"

#include <limits>
#include <string>

namespace baileys_native::proto_reflection::from_js_internal {

namespace {

using google::protobuf::FieldDescriptor;
using google::protobuf::Message;
using google::protobuf::Reflection;

bool SetSingularFieldFromValue(
	const Napi::Env& env,
	const Napi::Value& value,
	Message* message,
	const FieldDescriptor* field,
	uint32_t depth
);

bool SetRepeatedFieldFromValue(
	const Napi::Env& env,
	const Napi::Value& value,
	Message* message,
	const FieldDescriptor* field,
	uint32_t depth
) {
	if (!value.IsArray()) {
		return ThrowTypeError(env, std::string("Expected array for repeated field: ") + std::string(field->name()));
	}

	Napi::Array array = value.As<Napi::Array>();
	const uint32_t length = array.Length();
	const Reflection* reflection = message->GetReflection();
	const std::string fieldNameStorage = std::string(field->name());
	const char* fieldName = fieldNameStorage.c_str();

	for (uint32_t i = 0; i < length; ++i) {
		const Napi::Value item = array.Get(i);
		if (item.IsUndefined() || item.IsNull()) {
			return ThrowTypeError(env, std::string("Invalid null/undefined repeated element for field: ") + fieldName);
		}

		switch (field->cpp_type()) {
			case FieldDescriptor::CPPTYPE_INT32: {
				int64_t parsed = 0;
				if (!ReadInt64FromValue(env, item, fieldName, &parsed)) {
					return false;
				}
				if (parsed < std::numeric_limits<int32_t>::min() || parsed > std::numeric_limits<int32_t>::max()) {
					return ThrowRangeError(env, std::string("int32 out of range for field: ") + fieldName);
				}
				reflection->AddInt32(message, field, static_cast<int32_t>(parsed));
				break;
			}
			case FieldDescriptor::CPPTYPE_INT64: {
				int64_t parsed = 0;
				if (!ReadInt64FromValue(env, item, fieldName, &parsed)) {
					return false;
				}
				reflection->AddInt64(message, field, parsed);
				break;
			}
			case FieldDescriptor::CPPTYPE_UINT32: {
				uint64_t parsed = 0;
				if (!ReadUInt64FromValue(env, item, fieldName, &parsed)) {
					return false;
				}
				if (parsed > std::numeric_limits<uint32_t>::max()) {
					return ThrowRangeError(env, std::string("uint32 out of range for field: ") + fieldName);
				}
				reflection->AddUInt32(message, field, static_cast<uint32_t>(parsed));
				break;
			}
			case FieldDescriptor::CPPTYPE_UINT64: {
				uint64_t parsed = 0;
				if (!ReadUInt64FromValue(env, item, fieldName, &parsed)) {
					return false;
				}
				reflection->AddUInt64(message, field, parsed);
				break;
			}
			case FieldDescriptor::CPPTYPE_DOUBLE: {
				double parsed = 0.0;
				if (!ReadDoubleFromValue(env, item, fieldName, &parsed)) {
					return false;
				}
				reflection->AddDouble(message, field, parsed);
				break;
			}
			case FieldDescriptor::CPPTYPE_FLOAT: {
				double parsed = 0.0;
				if (!ReadDoubleFromValue(env, item, fieldName, &parsed)) {
					return false;
				}
				reflection->AddFloat(message, field, static_cast<float>(parsed));
				break;
			}
			case FieldDescriptor::CPPTYPE_BOOL: {
				bool parsed = false;
				if (!ReadBoolFromValue(env, item, fieldName, &parsed)) {
					return false;
				}
				reflection->AddBool(message, field, parsed);
				break;
			}
			case FieldDescriptor::CPPTYPE_ENUM: {
				int32_t enumNumber = 0;
				if (item.IsString()) {
					const std::string enumName = item.As<Napi::String>().Utf8Value();
					const auto* enumValue = field->enum_type()->FindValueByName(enumName);
					if (enumValue) {
						enumNumber = enumValue->number();
					} else if (!ReadInt32FromString(enumName, &enumNumber)) {
						return ThrowTypeError(env, std::string("Invalid enum value for field: ") + fieldName);
					}
				} else {
					int64_t parsed = 0;
					if (!ReadInt64FromValue(env, item, fieldName, &parsed)) {
						return false;
					}
					if (parsed < std::numeric_limits<int32_t>::min() || parsed > std::numeric_limits<int32_t>::max()) {
						return ThrowRangeError(env, std::string("enum out of range for field: ") + fieldName);
					}
					enumNumber = static_cast<int32_t>(parsed);
				}
				reflection->AddEnumValue(message, field, enumNumber);
				break;
			}
			case FieldDescriptor::CPPTYPE_STRING: {
				std::string parsed;
				if (field->type() == FieldDescriptor::TYPE_BYTES) {
					if (!ReadBytesFromValue(env, item, fieldName, &parsed)) {
						return false;
					}
				} else {
					if (!ReadStringFromValue(env, item, fieldName, &parsed)) {
						return false;
					}
				}
				reflection->AddString(message, field, parsed);
				break;
			}
			case FieldDescriptor::CPPTYPE_MESSAGE: {
				if (!item.IsObject()) {
					return ThrowTypeError(env, std::string("Expected object for message field: ") + fieldName);
				}
				Message* nested = reflection->AddMessage(message, field);
				if (!JsToProtoMessage(env, item, nested, depth + 1)) {
					return false;
				}
				break;
			}
		}
	}

	return true;
}

bool SetMapFieldFromValue(
	const Napi::Env& env,
	const Napi::Value& value,
	Message* message,
	const FieldDescriptor* field,
	uint32_t depth
) {
	if (!value.IsObject()) {
		return ThrowTypeError(env, std::string("Expected object for map field: ") + std::string(field->name()));
	}

	Napi::Object mapObject = value.As<Napi::Object>();
	Napi::Array keys = mapObject.GetPropertyNames();
	const Reflection* reflection = message->GetReflection();

	const auto* mapDescriptor = field->message_type();
	const auto* keyField = mapDescriptor->FindFieldByName("key");
	const auto* valueField = mapDescriptor->FindFieldByName("value");
	if (!keyField || !valueField) {
		return ThrowTypeError(
			env,
			std::string("Invalid protobuf map descriptor for field: ") + std::string(field->name())
		);
	}

	for (uint32_t i = 0; i < keys.Length(); ++i) {
		const Napi::Value keyValue = keys.Get(i);
		const std::string keyText = keyValue.ToString().Utf8Value();
		const Napi::Value mapValue = mapObject.Get(keyValue);
		if (mapValue.IsUndefined() || mapValue.IsNull()) {
			continue;
		}

		Message* mapEntry = reflection->AddMessage(message, field);
		if (!SetMapKeyFromString(env, keyText, mapEntry, keyField)) {
			return false;
		}
		if (!SetSingularFieldFromValue(env, mapValue, mapEntry, valueField, depth + 1)) {
			return false;
		}
	}

	return true;
}

bool SetSingularFieldFromValue(
	const Napi::Env& env,
	const Napi::Value& value,
	Message* message,
	const FieldDescriptor* field,
	uint32_t depth
) {
	const Reflection* reflection = message->GetReflection();
	const std::string fieldNameStorage = std::string(field->name());
	const char* fieldName = fieldNameStorage.c_str();

	switch (field->cpp_type()) {
		case FieldDescriptor::CPPTYPE_INT32: {
			int64_t parsed = 0;
			if (!ReadInt64FromValue(env, value, fieldName, &parsed)) {
				return false;
			}
			if (parsed < std::numeric_limits<int32_t>::min() || parsed > std::numeric_limits<int32_t>::max()) {
				return ThrowRangeError(env, std::string("int32 out of range for field: ") + fieldName);
			}
			reflection->SetInt32(message, field, static_cast<int32_t>(parsed));
			return true;
		}
		case FieldDescriptor::CPPTYPE_INT64: {
			int64_t parsed = 0;
			if (!ReadInt64FromValue(env, value, fieldName, &parsed)) {
				return false;
			}
			reflection->SetInt64(message, field, parsed);
			return true;
		}
		case FieldDescriptor::CPPTYPE_UINT32: {
			uint64_t parsed = 0;
			if (!ReadUInt64FromValue(env, value, fieldName, &parsed)) {
				return false;
			}
			if (parsed > std::numeric_limits<uint32_t>::max()) {
				return ThrowRangeError(env, std::string("uint32 out of range for field: ") + fieldName);
			}
			reflection->SetUInt32(message, field, static_cast<uint32_t>(parsed));
			return true;
		}
		case FieldDescriptor::CPPTYPE_UINT64: {
			uint64_t parsed = 0;
			if (!ReadUInt64FromValue(env, value, fieldName, &parsed)) {
				return false;
			}
			reflection->SetUInt64(message, field, parsed);
			return true;
		}
		case FieldDescriptor::CPPTYPE_DOUBLE: {
			double parsed = 0.0;
			if (!ReadDoubleFromValue(env, value, fieldName, &parsed)) {
				return false;
			}
			reflection->SetDouble(message, field, parsed);
			return true;
		}
		case FieldDescriptor::CPPTYPE_FLOAT: {
			double parsed = 0.0;
			if (!ReadDoubleFromValue(env, value, fieldName, &parsed)) {
				return false;
			}
			reflection->SetFloat(message, field, static_cast<float>(parsed));
			return true;
		}
		case FieldDescriptor::CPPTYPE_BOOL: {
			bool parsed = false;
			if (!ReadBoolFromValue(env, value, fieldName, &parsed)) {
				return false;
			}
			reflection->SetBool(message, field, parsed);
			return true;
		}
		case FieldDescriptor::CPPTYPE_ENUM: {
			int32_t enumNumber = 0;
			if (value.IsString()) {
				const std::string enumName = value.As<Napi::String>().Utf8Value();
				const auto* enumValue = field->enum_type()->FindValueByName(enumName);
				if (enumValue) {
					enumNumber = enumValue->number();
				} else if (!ReadInt32FromString(enumName, &enumNumber)) {
					return ThrowTypeError(env, std::string("Invalid enum value for field: ") + fieldName);
				}
			} else {
				int64_t parsed = 0;
				if (!ReadInt64FromValue(env, value, fieldName, &parsed)) {
					return false;
				}
				if (parsed < std::numeric_limits<int32_t>::min() || parsed > std::numeric_limits<int32_t>::max()) {
					return ThrowRangeError(env, std::string("enum out of range for field: ") + fieldName);
				}
				enumNumber = static_cast<int32_t>(parsed);
			}
			reflection->SetEnumValue(message, field, enumNumber);
			return true;
		}
		case FieldDescriptor::CPPTYPE_STRING: {
			std::string parsed;
			if (field->type() == FieldDescriptor::TYPE_BYTES) {
				if (!ReadBytesFromValue(env, value, fieldName, &parsed)) {
					return false;
				}
			} else {
				if (!ReadStringFromValue(env, value, fieldName, &parsed)) {
					return false;
				}
			}
			reflection->SetString(message, field, parsed);
			return true;
		}
		case FieldDescriptor::CPPTYPE_MESSAGE: {
			if (!value.IsObject()) {
				return ThrowTypeError(env, std::string("Expected object for message field: ") + fieldName);
			}
			Message* nested = reflection->MutableMessage(message, field);
			return JsToProtoMessage(env, value, nested, depth + 1);
		}
	}

	return ThrowTypeError(env, std::string("Unsupported field type for field: ") + fieldName);
}

} // namespace

bool SetFieldFromValue(
	const Napi::Env& env,
	const Napi::Value& value,
	Message* message,
	const FieldDescriptor* field,
	uint32_t depth
) {
	if (field->is_repeated()) {
		if (field->is_map()) {
			return SetMapFieldFromValue(env, value, message, field, depth);
		}
		return SetRepeatedFieldFromValue(env, value, message, field, depth);
	}
	return SetSingularFieldFromValue(env, value, message, field, depth);
}

bool TryGetOwnFieldValue(const Napi::Object& source, const FieldDescriptor* field, Napi::Value* out) {
	const std::string jsonName(field->json_name());
	if (source.HasOwnProperty(jsonName.c_str())) {
		*out = source.Get(jsonName.c_str());
		return true;
	}

	const std::string rawName(field->name());
	if (rawName != jsonName && source.HasOwnProperty(rawName.c_str())) {
		*out = source.Get(rawName.c_str());
		return true;
	}

	return false;
}

} // namespace baileys_native::proto_reflection::from_js_internal

#endif

