#include "proto/reflection/internal/protobuf_reflection_from_js_internal.h"

#if defined(BAILEYS_HAS_NATIVE_WAPROTO) && BAILEYS_HAS_NATIVE_WAPROTO

#include "common.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace baileys_native::proto_reflection::from_js_internal {

namespace {

using google::protobuf::FieldDescriptor;
using google::protobuf::Message;
using google::protobuf::Reflection;

constexpr double kMaxSafeInteger = 9007199254740991.0;
constexpr double kMinSafeInteger = -9007199254740991.0;

inline bool IsIntegralDouble(double value) {
	return std::isfinite(value) && std::floor(value) == value;
}

bool TryReadInt64FromLowHighObject(const Napi::Object& value, int64_t* out) {
	if (!value.Has("low") || !value.Has("high")) {
		return false;
	}

	const Napi::Value lowValue = value.Get("low");
	const Napi::Value highValue = value.Get("high");
	if (!lowValue.IsNumber() || !highValue.IsNumber()) {
		return false;
	}

	const double lowNumber = lowValue.As<Napi::Number>().DoubleValue();
	const double highNumber = highValue.As<Napi::Number>().DoubleValue();
	if (!IsIntegralDouble(lowNumber) || !IsIntegralDouble(highNumber)) {
		return false;
	}
	if (lowNumber < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
		lowNumber > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
		return false;
	}
	if (highNumber < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
		highNumber > static_cast<double>(std::numeric_limits<int32_t>::max())) {
		return false;
	}

	const int64_t lowSigned = static_cast<int64_t>(lowNumber);
	const uint32_t lowBits = static_cast<uint32_t>(lowSigned);
	const int32_t highBits = static_cast<int32_t>(highNumber);
	*out = (static_cast<int64_t>(highBits) << 32u) | static_cast<int64_t>(lowBits);
	return true;
}

bool TryReadUInt64FromLowHighObject(const Napi::Object& value, uint64_t* out) {
	if (!value.Has("low") || !value.Has("high")) {
		return false;
	}

	const Napi::Value lowValue = value.Get("low");
	const Napi::Value highValue = value.Get("high");
	if (!lowValue.IsNumber() || !highValue.IsNumber()) {
		return false;
	}

	const double lowNumber = lowValue.As<Napi::Number>().DoubleValue();
	const double highNumber = highValue.As<Napi::Number>().DoubleValue();
	if (!IsIntegralDouble(lowNumber) || !IsIntegralDouble(highNumber)) {
		return false;
	}
	if (lowNumber < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
		lowNumber > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
		return false;
	}
	if (highNumber < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
		highNumber > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
		return false;
	}

	const int64_t lowSigned = static_cast<int64_t>(lowNumber);
	const int64_t highSigned = static_cast<int64_t>(highNumber);
	const uint32_t lowBits = static_cast<uint32_t>(lowSigned);
	const uint32_t highBits = static_cast<uint32_t>(highSigned);
	*out = (static_cast<uint64_t>(highBits) << 32u) | static_cast<uint64_t>(lowBits);
	return true;
}

bool TryReadObjectAsDecimalString(const Napi::Env& env, const Napi::Object& value, std::string* out) {
	const Napi::Value toStringValue = value.Get("toString");
	if (!toStringValue.IsFunction()) {
		return false;
	}

	const Napi::Value stringValue = toStringValue.As<Napi::Function>().Call(value, {});
	if (env.IsExceptionPending()) {
		return false;
	}
	if (!stringValue.IsString()) {
		return false;
	}

	*out = stringValue.As<Napi::String>().Utf8Value();
	return true;
}

} // namespace

bool ThrowTypeError(const Napi::Env& env, const std::string& message) {
	Napi::TypeError::New(env, message).ThrowAsJavaScriptException();
	return false;
}

bool ThrowRangeError(const Napi::Env& env, const std::string& message) {
	Napi::RangeError::New(env, message).ThrowAsJavaScriptException();
	return false;
}

bool ParseDecimalUnsigned(std::string_view text, uint64_t* out) {
	if (text.empty()) {
		return false;
	}

	size_t i = 0;
	if (text[0] == '+') {
		i = 1;
	}
	if (i >= text.size()) {
		return false;
	}

	uint64_t value = 0;
	for (; i < text.size(); ++i) {
		const char c = text[i];
		if (c < '0' || c > '9') {
			return false;
		}
		const uint8_t digit = static_cast<uint8_t>(c - '0');
		if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10u) {
			return false;
		}
		value = value * 10u + digit;
	}

	*out = value;
	return true;
}

bool ParseDecimalSigned(std::string_view text, int64_t* out) {
	if (text.empty()) {
		return false;
	}

	bool negative = false;
	size_t i = 0;
	if (text[0] == '-') {
		negative = true;
		i = 1;
	} else if (text[0] == '+') {
		i = 1;
	}
	if (i >= text.size()) {
		return false;
	}

	uint64_t magnitude = 0;
	for (; i < text.size(); ++i) {
		const char c = text[i];
		if (c < '0' || c > '9') {
			return false;
		}
		const uint8_t digit = static_cast<uint8_t>(c - '0');
		if (magnitude > (std::numeric_limits<uint64_t>::max() - digit) / 10u) {
			return false;
		}
		magnitude = magnitude * 10u + digit;
	}

	if (!negative) {
		if (magnitude > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
			return false;
		}
		*out = static_cast<int64_t>(magnitude);
		return true;
	}

	const uint64_t maxNegativeMagnitude = static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1u;
	if (magnitude > maxNegativeMagnitude) {
		return false;
	}
	if (magnitude == maxNegativeMagnitude) {
		*out = std::numeric_limits<int64_t>::min();
		return true;
	}

	*out = -static_cast<int64_t>(magnitude);
	return true;
}

bool ReadInt64FromValue(const Napi::Env& env, const Napi::Value& value, const char* fieldName, int64_t* out) {
	if (value.IsNumber()) {
		const double number = value.As<Napi::Number>().DoubleValue();
		if (!IsIntegralDouble(number)) {
			return ThrowTypeError(env, std::string("Expected integer for field: ") + fieldName);
		}
		if (number < kMinSafeInteger || number > kMaxSafeInteger) {
			return ThrowRangeError(
				env,
				std::string("Unsafe int64 number for field: ") + fieldName + " (use string or bigint)"
			);
		}
		*out = static_cast<int64_t>(number);
		return true;
	}

	if (value.IsBigInt()) {
		bool lossless = false;
		const int64_t parsed = value.As<Napi::BigInt>().Int64Value(&lossless);
		if (!lossless) {
			return ThrowRangeError(env, std::string("int64 bigint out of range for field: ") + fieldName);
		}
		*out = parsed;
		return true;
	}

	if (value.IsString()) {
		const std::string text = value.As<Napi::String>().Utf8Value();
		if (!ParseDecimalSigned(text, out)) {
			return ThrowTypeError(env, std::string("Invalid int64 string for field: ") + fieldName);
		}
		return true;
	}

	if (value.IsObject()) {
		const Napi::Object objectValue = value.As<Napi::Object>();
		if (TryReadInt64FromLowHighObject(objectValue, out)) {
			return true;
		}

		std::string text;
		if (TryReadObjectAsDecimalString(env, objectValue, &text)) {
			if (ParseDecimalSigned(text, out)) {
				return true;
			}
		}
	}

	return ThrowTypeError(env, std::string("Expected int64-compatible value for field: ") + fieldName);
}

bool ReadUInt64FromValue(const Napi::Env& env, const Napi::Value& value, const char* fieldName, uint64_t* out) {
	if (value.IsNumber()) {
		const double number = value.As<Napi::Number>().DoubleValue();
		if (!IsIntegralDouble(number) || number < 0.0) {
			return ThrowTypeError(env, std::string("Expected unsigned integer for field: ") + fieldName);
		}
		if (number > kMaxSafeInteger) {
			return ThrowRangeError(
				env,
				std::string("Unsafe uint64 number for field: ") + fieldName + " (use string or bigint)"
			);
		}
		*out = static_cast<uint64_t>(number);
		return true;
	}

	if (value.IsBigInt()) {
		bool lossless = false;
		const uint64_t parsed = value.As<Napi::BigInt>().Uint64Value(&lossless);
		if (!lossless) {
			return ThrowRangeError(env, std::string("uint64 bigint out of range for field: ") + fieldName);
		}
		*out = parsed;
		return true;
	}

	if (value.IsString()) {
		const std::string text = value.As<Napi::String>().Utf8Value();
		if (!ParseDecimalUnsigned(text, out)) {
			return ThrowTypeError(env, std::string("Invalid uint64 string for field: ") + fieldName);
		}
		return true;
	}

	if (value.IsObject()) {
		const Napi::Object objectValue = value.As<Napi::Object>();
		if (TryReadUInt64FromLowHighObject(objectValue, out)) {
			return true;
		}

		std::string text;
		if (TryReadObjectAsDecimalString(env, objectValue, &text)) {
			if (ParseDecimalUnsigned(text, out)) {
				return true;
			}
		}
	}

	return ThrowTypeError(env, std::string("Expected uint64-compatible value for field: ") + fieldName);
}

bool ReadDoubleFromValue(const Napi::Env& env, const Napi::Value& value, const char* fieldName, double* out) {
	if (!value.IsNumber()) {
		return ThrowTypeError(env, std::string("Expected numeric field: ") + fieldName);
	}

	const double parsed = value.As<Napi::Number>().DoubleValue();
	if (!std::isfinite(parsed)) {
		return ThrowTypeError(env, std::string("Expected finite number for field: ") + fieldName);
	}

	*out = parsed;
	return true;
}

bool ReadBoolFromValue(const Napi::Env& env, const Napi::Value& value, const char* fieldName, bool* out) {
	if (!value.IsBoolean()) {
		return ThrowTypeError(env, std::string("Expected boolean field: ") + fieldName);
	}
	*out = value.As<Napi::Boolean>().Value();
	return true;
}

bool ReadStringFromValue(const Napi::Env& env, const Napi::Value& value, const char* fieldName, std::string* out) {
	if (!value.IsString()) {
		return ThrowTypeError(env, std::string("Expected string field: ") + fieldName);
	}

	*out = value.As<Napi::String>().Utf8Value();
	return true;
}

bool ReadBytesFromValue(const Napi::Env& env, const Napi::Value& value, const char* fieldName, std::string* out) {
	common::ByteView view;
	if (!common::GetByteViewFromValue(env, value, fieldName, &view)) {
		return false;
	}
	out->assign(reinterpret_cast<const char*>(view.data), view.length);
	return true;
}

bool ReadInt32FromString(std::string_view text, int32_t* out) {
	int64_t parsed = 0;
	if (!ParseDecimalSigned(text, &parsed)) {
		return false;
	}
	if (parsed < std::numeric_limits<int32_t>::min() || parsed > std::numeric_limits<int32_t>::max()) {
		return false;
	}
	*out = static_cast<int32_t>(parsed);
	return true;
}

bool ReadUInt32FromString(std::string_view text, uint32_t* out) {
	uint64_t parsed = 0;
	if (!ParseDecimalUnsigned(text, &parsed)) {
		return false;
	}
	if (parsed > std::numeric_limits<uint32_t>::max()) {
		return false;
	}
	*out = static_cast<uint32_t>(parsed);
	return true;
}

bool SetMapKeyFromString(const Napi::Env& env, const std::string& keyText, Message* mapEntry, const FieldDescriptor* keyField) {
	const Reflection* entryReflection = mapEntry->GetReflection();
	const std::string fieldNameStorage = std::string(keyField->name());
	const char* fieldName = fieldNameStorage.c_str();

	switch (keyField->cpp_type()) {
		case FieldDescriptor::CPPTYPE_INT32: {
			int32_t value = 0;
			if (!ReadInt32FromString(keyText, &value)) {
				return ThrowTypeError(env, std::string("Invalid int32 map key for field: ") + fieldName);
			}
			entryReflection->SetInt32(mapEntry, keyField, value);
			return true;
		}
		case FieldDescriptor::CPPTYPE_INT64: {
			int64_t value = 0;
			if (!ParseDecimalSigned(keyText, &value)) {
				return ThrowTypeError(env, std::string("Invalid int64 map key for field: ") + fieldName);
			}
			entryReflection->SetInt64(mapEntry, keyField, value);
			return true;
		}
		case FieldDescriptor::CPPTYPE_UINT32: {
			uint32_t value = 0;
			if (!ReadUInt32FromString(keyText, &value)) {
				return ThrowTypeError(env, std::string("Invalid uint32 map key for field: ") + fieldName);
			}
			entryReflection->SetUInt32(mapEntry, keyField, value);
			return true;
		}
		case FieldDescriptor::CPPTYPE_UINT64: {
			uint64_t value = 0;
			if (!ParseDecimalUnsigned(keyText, &value)) {
				return ThrowTypeError(env, std::string("Invalid uint64 map key for field: ") + fieldName);
			}
			entryReflection->SetUInt64(mapEntry, keyField, value);
			return true;
		}
		case FieldDescriptor::CPPTYPE_BOOL: {
			if (keyText == "true" || keyText == "1") {
				entryReflection->SetBool(mapEntry, keyField, true);
				return true;
			}
			if (keyText == "false" || keyText == "0") {
				entryReflection->SetBool(mapEntry, keyField, false);
				return true;
			}
			return ThrowTypeError(env, std::string("Invalid bool map key for field: ") + fieldName);
		}
		case FieldDescriptor::CPPTYPE_STRING:
			entryReflection->SetString(mapEntry, keyField, keyText);
			return true;
		case FieldDescriptor::CPPTYPE_ENUM: {
			const auto* enumType = keyField->enum_type();
			const auto* enumValue = enumType->FindValueByName(keyText);
			if (enumValue) {
				entryReflection->SetEnum(mapEntry, keyField, enumValue);
				return true;
			}

			int32_t numericValue = 0;
			if (!ReadInt32FromString(keyText, &numericValue)) {
				return ThrowTypeError(env, std::string("Invalid enum map key for field: ") + fieldName);
			}
			entryReflection->SetEnumValue(mapEntry, keyField, numericValue);
			return true;
		}
		case FieldDescriptor::CPPTYPE_FLOAT:
		case FieldDescriptor::CPPTYPE_DOUBLE:
		case FieldDescriptor::CPPTYPE_MESSAGE:
			return ThrowTypeError(env, std::string("Unsupported map key type for field: ") + fieldName);
	}

	return ThrowTypeError(env, std::string("Unknown map key type for field: ") + fieldName);
}

} // namespace baileys_native::proto_reflection::from_js_internal

#endif

