#include "common.h"

#include <cmath>
#include <limits>
#include <string>

namespace baileys_native::common {

namespace {

inline bool IsSafeInteger(double value) {
	if (!std::isfinite(value)) return false;
	if (value < 0) return false;
	if (std::floor(value) != value) return false;
	return value <= static_cast<double>(std::numeric_limits<uint32_t>::max());
}

} // namespace

bool ReadUInt32FromValue(const Napi::Env& env, const Napi::Value& value, const char* field, uint32_t* out) {
	if (!value.IsNumber()) {
		Napi::TypeError::New(env, std::string("Expected numeric field: ") + field).ThrowAsJavaScriptException();
		return false;
	}

	const double number = value.As<Napi::Number>().DoubleValue();
	if (!IsSafeInteger(number)) {
		Napi::TypeError::New(env, std::string("Invalid integer for field: ") + field).ThrowAsJavaScriptException();
		return false;
	}

	*out = static_cast<uint32_t>(number);
	return true;
}

bool ReadDoubleFromValue(const Napi::Env& env, const Napi::Value& value, const char* field, double* out) {
	if (!value.IsNumber()) {
		Napi::TypeError::New(env, std::string("Expected numeric field: ") + field).ThrowAsJavaScriptException();
		return false;
	}

	*out = value.As<Napi::Number>().DoubleValue();
	if (!std::isfinite(*out)) {
		Napi::TypeError::New(env, std::string("Invalid numeric value for field: ") + field).ThrowAsJavaScriptException();
		return false;
	}

	return true;
}

bool ReadStringFromValue(const Napi::Env& env, const Napi::Value& value, const char* field, std::string* out) {
	if (!value.IsString()) {
		Napi::TypeError::New(env, std::string("Expected string field: ") + field).ThrowAsJavaScriptException();
		return false;
	}
	*out = value.As<Napi::String>().Utf8Value();
	return true;
}

bool GetByteViewFromValue(const Napi::Env& env, const Napi::Value& value, const char* field, ByteView* out) {
	if (value.IsUndefined() || value.IsNull()) {
		Napi::TypeError::New(env, std::string("Missing byte array field: ") + field).ThrowAsJavaScriptException();
		return false;
	}

	if (value.IsBuffer()) {
		auto buffer = value.As<Napi::Buffer<uint8_t>>();
		out->data = buffer.Data();
		out->length = buffer.Length();
		return true;
	}

	if (value.IsTypedArray()) {
		auto ta = value.As<Napi::TypedArray>();
		if (ta.TypedArrayType() != napi_uint8_array) {
			Napi::TypeError::New(env, std::string("Expected Uint8Array for field: ") + field)
				.ThrowAsJavaScriptException();
			return false;
		}

		auto u8 = value.As<Napi::Uint8Array>();
		out->data = u8.Data();
		out->length = u8.ByteLength();
		return true;
	}

	Napi::TypeError::New(env, std::string("Expected Buffer/Uint8Array for field: ") + field)
		.ThrowAsJavaScriptException();
	return false;
}

bool CopyBytesFromValue(
	const Napi::Env& env,
	const Napi::Value& value,
	const char* field,
	std::vector<uint8_t>* out
) {
	ByteView view;
	if (!GetByteViewFromValue(env, value, field, &view)) {
		return false;
	}
	out->assign(view.data, view.data + view.length);
	return true;
}

bool CopyOptionalBytesFromValue(
	const Napi::Env& env,
	const Napi::Value& value,
	const char* field,
	std::vector<uint8_t>* out
) {
	out->clear();
	if (value.IsUndefined() || value.IsNull()) {
		return true;
	}
	return CopyBytesFromValue(env, value, field, out);
}

bool CheckFitsU16(const Napi::Env& env, size_t len, const char* field) {
	if (len > std::numeric_limits<uint16_t>::max()) {
		Napi::RangeError::New(env, std::string("Field too large to serialize: ") + field).ThrowAsJavaScriptException();
		return false;
	}
	return true;
}

Napi::Buffer<uint8_t> CopyVectorToBuffer(const Napi::Env& env, const std::vector<uint8_t>& data) {
	if (data.empty()) {
		return Napi::Buffer<uint8_t>::New(env, 0);
	}
	return Napi::Buffer<uint8_t>::Copy(env, data.data(), data.size());
}

Napi::Buffer<uint8_t> MoveVectorToBuffer(const Napi::Env& env, std::vector<uint8_t>&& data) {
	if (data.empty()) {
		return Napi::Buffer<uint8_t>::New(env, 0);
	}

	auto* holder = new std::vector<uint8_t>(std::move(data));
	return Napi::Buffer<uint8_t>::New(
		env,
		holder->data(),
		holder->size(),
		[](Napi::Env /*unused*/, uint8_t* /*unused*/, std::vector<uint8_t>* finalized) { delete finalized; },
		holder
	);
}

} // namespace baileys_native::common
