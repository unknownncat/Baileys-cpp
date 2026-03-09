#include "protobuf_reflection_codec.h"

#if defined(BAILEYS_HAS_NATIVE_WAPROTO) && BAILEYS_HAS_NATIVE_WAPROTO

#include "common.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/reflection.h>

namespace baileys_native::proto_reflection {

namespace {

using google::protobuf::FieldDescriptor;
using google::protobuf::Message;
using google::protobuf::Reflection;

constexpr uint32_t kMaxProtoDepth = 128;
constexpr double kMaxSafeInteger = 9007199254740991.0;
constexpr double kMinSafeInteger = -9007199254740991.0;

bool ThrowTypeError(const Napi::Env& env, const std::string& message) {
	Napi::TypeError::New(env, message).ThrowAsJavaScriptException();
	return false;
}

bool ThrowRangeError(const Napi::Env& env, const std::string& message) {
	Napi::RangeError::New(env, message).ThrowAsJavaScriptException();
	return false;
}
Napi::Value Int64ToJsValue(const Napi::Env& env, int64_t value) {
	const double asDouble = static_cast<double>(value);
	if (asDouble >= kMinSafeInteger && asDouble <= kMaxSafeInteger) {
		return Napi::Number::New(env, asDouble);
	}

	return Napi::String::New(env, std::to_string(value));
}

Napi::Value UInt64ToJsValue(const Napi::Env& env, uint64_t value) {
	const double asDouble = static_cast<double>(value);
	if (asDouble <= kMaxSafeInteger) {
		return Napi::Number::New(env, asDouble);
	}

	return Napi::String::New(env, std::to_string(value));
}

Napi::Value SingularFieldToJsValue(
	const Napi::Env& env,
	const Message& message,
	const Reflection* reflection,
	const FieldDescriptor* field,
	uint32_t depth
);

Napi::Value MapFieldToJsObject(
	const Napi::Env& env,
	const Message& message,
	const Reflection* reflection,
	const FieldDescriptor* field,
	uint32_t depth
) {
	Napi::Object out = Napi::Object::New(env);
	const int count = reflection->FieldSize(message, field);
	for (int i = 0; i < count; ++i) {
		const Message& entry = reflection->GetRepeatedMessage(message, field, i);
		const Reflection* entryReflection = entry.GetReflection();
		const auto* entryDescriptor = entry.GetDescriptor();
		const auto* keyField = entryDescriptor->FindFieldByName("key");
		const auto* valueField = entryDescriptor->FindFieldByName("value");
		if (!keyField || !valueField) {
			continue;
		}

		std::string key;
		switch (keyField->cpp_type()) {
			case FieldDescriptor::CPPTYPE_INT32:
				key = std::to_string(entryReflection->GetInt32(entry, keyField));
				break;
			case FieldDescriptor::CPPTYPE_INT64:
				key = std::to_string(entryReflection->GetInt64(entry, keyField));
				break;
			case FieldDescriptor::CPPTYPE_UINT32:
				key = std::to_string(entryReflection->GetUInt32(entry, keyField));
				break;
			case FieldDescriptor::CPPTYPE_UINT64:
				key = std::to_string(entryReflection->GetUInt64(entry, keyField));
				break;
			case FieldDescriptor::CPPTYPE_BOOL:
				key = entryReflection->GetBool(entry, keyField) ? "true" : "false";
				break;
			case FieldDescriptor::CPPTYPE_STRING:
				key = entryReflection->GetString(entry, keyField);
				break;
			case FieldDescriptor::CPPTYPE_ENUM:
				key = std::to_string(entryReflection->GetEnumValue(entry, keyField));
				break;
			case FieldDescriptor::CPPTYPE_DOUBLE:
			case FieldDescriptor::CPPTYPE_FLOAT:
			case FieldDescriptor::CPPTYPE_MESSAGE:
				continue;
		}

		out.Set(key, SingularFieldToJsValue(env, entry, entryReflection, valueField, depth + 1));
	}

	return out;
}

Napi::Value RepeatedFieldToJsValue(
	const Napi::Env& env,
	const Message& message,
	const Reflection* reflection,
	const FieldDescriptor* field,
	uint32_t depth
) {
	if (field->is_map()) {
		return MapFieldToJsObject(env, message, reflection, field, depth);
	}

	const int count = reflection->FieldSize(message, field);
	Napi::Array out = Napi::Array::New(env, static_cast<uint32_t>(count));
	for (int i = 0; i < count; ++i) {
		switch (field->cpp_type()) {
			case FieldDescriptor::CPPTYPE_INT32:
				out.Set(static_cast<uint32_t>(i), Napi::Number::New(env, reflection->GetRepeatedInt32(message, field, i)));
				break;
			case FieldDescriptor::CPPTYPE_INT64:
				out.Set(static_cast<uint32_t>(i), Int64ToJsValue(env, reflection->GetRepeatedInt64(message, field, i)));
				break;
			case FieldDescriptor::CPPTYPE_UINT32:
				out.Set(static_cast<uint32_t>(i), Napi::Number::New(env, reflection->GetRepeatedUInt32(message, field, i)));
				break;
			case FieldDescriptor::CPPTYPE_UINT64:
				out.Set(static_cast<uint32_t>(i), UInt64ToJsValue(env, reflection->GetRepeatedUInt64(message, field, i)));
				break;
			case FieldDescriptor::CPPTYPE_DOUBLE:
				out.Set(static_cast<uint32_t>(i), Napi::Number::New(env, reflection->GetRepeatedDouble(message, field, i)));
				break;
			case FieldDescriptor::CPPTYPE_FLOAT:
				out.Set(static_cast<uint32_t>(i), Napi::Number::New(env, reflection->GetRepeatedFloat(message, field, i)));
				break;
			case FieldDescriptor::CPPTYPE_BOOL:
				out.Set(static_cast<uint32_t>(i), Napi::Boolean::New(env, reflection->GetRepeatedBool(message, field, i)));
				break;
			case FieldDescriptor::CPPTYPE_ENUM:
				out.Set(
					static_cast<uint32_t>(i),
					Napi::Number::New(env, reflection->GetRepeatedEnumValue(message, field, i))
				);
				break;
			case FieldDescriptor::CPPTYPE_STRING: {
				std::string scratch;
				const std::string& value = reflection->GetRepeatedStringReference(message, field, i, &scratch);
				if (field->type() == FieldDescriptor::TYPE_BYTES) {
					out.Set(
						static_cast<uint32_t>(i),
						Napi::Buffer<uint8_t>::Copy(env, reinterpret_cast<const uint8_t*>(value.data()), value.size())
					);
				} else {
					out.Set(static_cast<uint32_t>(i), Napi::String::New(env, value));
				}
				break;
			}
			case FieldDescriptor::CPPTYPE_MESSAGE: {
				const Message& nested = reflection->GetRepeatedMessage(message, field, i);
				out.Set(static_cast<uint32_t>(i), ProtoToJsObject(env, nested, depth + 1));
				break;
			}
		}
	}

	return out;
}

Napi::Value SingularFieldToJsValue(
	const Napi::Env& env,
	const Message& message,
	const Reflection* reflection,
	const FieldDescriptor* field,
	uint32_t depth
) {
	switch (field->cpp_type()) {
		case FieldDescriptor::CPPTYPE_INT32:
			return Napi::Number::New(env, reflection->GetInt32(message, field));
		case FieldDescriptor::CPPTYPE_INT64:
			return Int64ToJsValue(env, reflection->GetInt64(message, field));
		case FieldDescriptor::CPPTYPE_UINT32:
			return Napi::Number::New(env, reflection->GetUInt32(message, field));
		case FieldDescriptor::CPPTYPE_UINT64:
			return UInt64ToJsValue(env, reflection->GetUInt64(message, field));
		case FieldDescriptor::CPPTYPE_DOUBLE:
			return Napi::Number::New(env, reflection->GetDouble(message, field));
		case FieldDescriptor::CPPTYPE_FLOAT:
			return Napi::Number::New(env, reflection->GetFloat(message, field));
		case FieldDescriptor::CPPTYPE_BOOL:
			return Napi::Boolean::New(env, reflection->GetBool(message, field));
		case FieldDescriptor::CPPTYPE_ENUM:
			return Napi::Number::New(env, reflection->GetEnumValue(message, field));
		case FieldDescriptor::CPPTYPE_STRING: {
			std::string scratch;
			const std::string& value = reflection->GetStringReference(message, field, &scratch);
			if (field->type() == FieldDescriptor::TYPE_BYTES) {
				return Napi::Buffer<uint8_t>::Copy(env, reinterpret_cast<const uint8_t*>(value.data()), value.size());
			}
			return Napi::String::New(env, value);
		}
		case FieldDescriptor::CPPTYPE_MESSAGE: {
			const Message& nested = reflection->GetMessage(message, field);
			return ProtoToJsObject(env, nested, depth + 1);
		}
	}

	return env.Undefined();
}

bool ShouldEmitSingularField(
	const Message& message,
	const Reflection* reflection,
	const FieldDescriptor* field
) {
	if (field->containing_oneof()) {
		return reflection->HasField(message, field);
	}

	if (field->has_presence()) {
		return reflection->HasField(message, field);
	}

	switch (field->cpp_type()) {
		case FieldDescriptor::CPPTYPE_INT32:
			return reflection->GetInt32(message, field) != 0;
		case FieldDescriptor::CPPTYPE_INT64:
			return reflection->GetInt64(message, field) != 0;
		case FieldDescriptor::CPPTYPE_UINT32:
			return reflection->GetUInt32(message, field) != 0;
		case FieldDescriptor::CPPTYPE_UINT64:
			return reflection->GetUInt64(message, field) != 0;
		case FieldDescriptor::CPPTYPE_DOUBLE:
			return reflection->GetDouble(message, field) != 0.0;
		case FieldDescriptor::CPPTYPE_FLOAT:
			return reflection->GetFloat(message, field) != 0.0f;
		case FieldDescriptor::CPPTYPE_BOOL:
			return reflection->GetBool(message, field);
		case FieldDescriptor::CPPTYPE_ENUM:
			return reflection->GetEnumValue(message, field) != field->default_value_enum()->number();
		case FieldDescriptor::CPPTYPE_STRING: {
			std::string scratch;
			return !reflection->GetStringReference(message, field, &scratch).empty();
		}
		case FieldDescriptor::CPPTYPE_MESSAGE:
			return reflection->HasField(message, field);
	}

	return false;
}

} // namespace
Napi::Object ProtoToJsObject(const Napi::Env& env, const Message& message, uint32_t depth) {
	Napi::Object out = Napi::Object::New(env);
	if (depth > kMaxProtoDepth) {
		ThrowRangeError(env, "protobuf conversion max depth exceeded");
		return out;
	}

	const auto* descriptor = message.GetDescriptor();
	const Reflection* reflection = message.GetReflection();
	if (!descriptor || !reflection) {
		return out;
	}

	for (int i = 0; i < descriptor->field_count(); ++i) {
		const FieldDescriptor* field = descriptor->field(i);

		if (field->is_repeated()) {
			if (reflection->FieldSize(message, field) == 0) {
				continue;
			}
			out.Set(std::string(field->json_name()), RepeatedFieldToJsValue(env, message, reflection, field, depth + 1));
			continue;
		}

		if (!ShouldEmitSingularField(message, reflection, field)) {
			continue;
		}

		out.Set(std::string(field->json_name()), SingularFieldToJsValue(env, message, reflection, field, depth + 1));
	}

	return out;
}

} // namespace baileys_native::proto_reflection

#endif
