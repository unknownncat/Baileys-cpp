#include "value_codec.h"

#include "common.h"
#include "common/napi_guard.h"
#include "common/safe_copy.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace baileys_native {

namespace {

constexpr uint32_t kMaxDepth = 256;

enum ValueTag : uint8_t {
	kTagNull = 0,
	kTagUndefined = 1,
	kTagFalse = 2,
	kTagTrue = 3,
	kTagNumber = 4,
	kTagString = 5,
	kTagBytes = 6,
	kTagArray = 7,
	kTagObject = 8
};

inline void WriteU32LE(uint32_t value, std::vector<uint8_t>* out) {
	out->push_back(static_cast<uint8_t>(value & 0xffu));
	out->push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
	out->push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
	out->push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
}

inline bool ReadU32LE(const uint8_t* data, size_t length, size_t* offset, uint32_t* out) {
	if (*offset + 4 > length) return false;
	const uint8_t* ptr = data + *offset;
	*out = static_cast<uint32_t>(ptr[0]) | (static_cast<uint32_t>(ptr[1]) << 8u) |
		(static_cast<uint32_t>(ptr[2]) << 16u) | (static_cast<uint32_t>(ptr[3]) << 24u);
	*offset += 4;
	return true;
}

bool EncodeValue(const Napi::Env& env, const Napi::Value& value, std::vector<uint8_t>* out, uint32_t depth) {
	if (depth > kMaxDepth) {
		return common::napi_guard::ThrowError(env, "encodeAuthValue max depth exceeded");
	}

	if (value.IsNull()) {
		out->push_back(kTagNull);
		return true;
	}
	if (value.IsUndefined()) {
		out->push_back(kTagUndefined);
		return true;
	}
	if (value.IsBoolean()) {
		out->push_back(value.As<Napi::Boolean>().Value() ? kTagTrue : kTagFalse);
		return true;
	}
	if (value.IsNumber()) {
		out->push_back(kTagNumber);
		const double num = value.As<Napi::Number>().DoubleValue();
		uint8_t bytes[sizeof(double)];
		std::memcpy(bytes, &num, sizeof(double));
		out->insert(out->end(), bytes, bytes + sizeof(double));
		return true;
	}
	if (value.IsString()) {
		out->push_back(kTagString);
		const std::string str = value.As<Napi::String>().Utf8Value();
		if (str.size() > std::numeric_limits<uint32_t>::max()) {
			return common::napi_guard::ThrowRange(env, "encodeAuthValue string too large");
		}
		WriteU32LE(static_cast<uint32_t>(str.size()), out);
		if (!common::safe_copy::AppendBytes(
				out,
				reinterpret_cast<const uint8_t*>(str.data()),
				str.size()
			)) {
			return common::napi_guard::ThrowRange(env, "encodeAuthValue string too large");
		}
		return true;
	}
	if (value.IsBuffer()) {
		out->push_back(kTagBytes);
		const auto buff = value.As<Napi::Buffer<uint8_t>>();
		if (buff.Length() > std::numeric_limits<uint32_t>::max()) {
			return common::napi_guard::ThrowRange(env, "encodeAuthValue bytes too large");
		}
		WriteU32LE(static_cast<uint32_t>(buff.Length()), out);
		if (!common::safe_copy::AppendBytes(out, buff.Data(), buff.Length())) {
			return common::napi_guard::ThrowRange(env, "encodeAuthValue bytes too large");
		}
		return true;
	}
	if (value.IsTypedArray()) {
		common::ByteView view;
		if (!common::GetByteViewFromValue(env, value, "value", &view)) {
			return false;
		}
		out->push_back(kTagBytes);
		if (view.length > std::numeric_limits<uint32_t>::max()) {
			return common::napi_guard::ThrowRange(env, "encodeAuthValue bytes too large");
		}
		WriteU32LE(static_cast<uint32_t>(view.length), out);
		if (!common::safe_copy::AppendBytes(out, view.data, view.length)) {
			return common::napi_guard::ThrowRange(env, "encodeAuthValue bytes too large");
		}
		return true;
	}
	if (value.IsArray()) {
		out->push_back(kTagArray);
		Napi::Array arr = value.As<Napi::Array>();
		const uint32_t len = arr.Length();
		WriteU32LE(len, out);
		for (uint32_t i = 0; i < len; ++i) {
			if (!EncodeValue(env, arr.Get(i), out, depth + 1)) {
				return false;
			}
		}
		return true;
	}
	if (value.IsObject()) {
		out->push_back(kTagObject);
		Napi::Object obj = value.As<Napi::Object>();
		Napi::Array keys = obj.GetPropertyNames();
		const uint32_t len = keys.Length();
		WriteU32LE(len, out);
		for (uint32_t i = 0; i < len; ++i) {
			Napi::Value keyValue = keys.Get(i);
			if (!keyValue.IsString()) {
				return common::napi_guard::ThrowType(env, "encodeAuthValue object keys must be strings");
			}
			const std::string key = keyValue.As<Napi::String>().Utf8Value();
			if (key.size() > std::numeric_limits<uint32_t>::max()) {
				return common::napi_guard::ThrowRange(env, "encodeAuthValue key too large");
			}
			WriteU32LE(static_cast<uint32_t>(key.size()), out);
			if (!common::safe_copy::AppendBytes(
					out,
					reinterpret_cast<const uint8_t*>(key.data()),
					key.size()
				)) {
				return common::napi_guard::ThrowRange(env, "encodeAuthValue key too large");
			}
			if (!EncodeValue(env, obj.Get(keyValue), out, depth + 1)) {
				return false;
			}
		}
		return true;
	}

	return common::napi_guard::ThrowType(env, "encodeAuthValue unsupported value type");
}

bool DecodeValue(
	const Napi::Env& env,
	const uint8_t* data,
	size_t length,
	size_t* offset,
	Napi::Value* out,
	uint32_t depth
) {
	if (depth > kMaxDepth) {
		return common::napi_guard::ThrowError(env, "decodeAuthValue max depth exceeded");
	}
	if (*offset >= length) {
		return common::napi_guard::ThrowError(env, "decodeAuthValue malformed payload");
	}

	const uint8_t tag = data[(*offset)++];
	switch (tag) {
		case kTagNull:
			*out = env.Null();
			return true;
		case kTagUndefined:
			*out = env.Undefined();
			return true;
		case kTagFalse:
			*out = Napi::Boolean::New(env, false);
			return true;
		case kTagTrue:
			*out = Napi::Boolean::New(env, true);
			return true;
		case kTagNumber: {
			if (*offset + sizeof(double) > length) {
				return common::napi_guard::ThrowError(env, "decodeAuthValue malformed number");
			}
			double num = 0.0;
			std::memcpy(&num, data + *offset, sizeof(double));
			*offset += sizeof(double);
			*out = Napi::Number::New(env, num);
			return true;
		}
		case kTagString: {
			uint32_t strLen = 0;
			if (!ReadU32LE(data, length, offset, &strLen) || *offset + strLen > length) {
				return common::napi_guard::ThrowError(env, "decodeAuthValue malformed string");
			}
			*out = Napi::String::New(env, reinterpret_cast<const char*>(data + *offset), strLen);
			*offset += strLen;
			return true;
		}
		case kTagBytes: {
			uint32_t bytesLen = 0;
			if (!ReadU32LE(data, length, offset, &bytesLen) || *offset + bytesLen > length) {
				return common::napi_guard::ThrowError(env, "decodeAuthValue malformed bytes");
			}
			*out = Napi::Buffer<uint8_t>::Copy(env, data + *offset, bytesLen);
			*offset += bytesLen;
			return true;
		}
		case kTagArray: {
			uint32_t len = 0;
			if (!ReadU32LE(data, length, offset, &len)) {
				return common::napi_guard::ThrowError(env, "decodeAuthValue malformed array");
			}
			Napi::Array arr = Napi::Array::New(env, len);
			for (uint32_t i = 0; i < len; ++i) {
				Napi::Value item;
				if (!DecodeValue(env, data, length, offset, &item, depth + 1)) {
					return false;
				}
				arr.Set(i, item);
			}
			*out = arr;
			return true;
		}
		case kTagObject: {
			uint32_t count = 0;
			if (!ReadU32LE(data, length, offset, &count)) {
				return common::napi_guard::ThrowError(env, "decodeAuthValue malformed object");
			}
			Napi::Object obj = Napi::Object::New(env);
			for (uint32_t i = 0; i < count; ++i) {
				uint32_t keyLen = 0;
				if (!ReadU32LE(data, length, offset, &keyLen) || *offset + keyLen > length) {
					return common::napi_guard::ThrowError(env, "decodeAuthValue malformed object key");
				}
				Napi::String key = Napi::String::New(env, reinterpret_cast<const char*>(data + *offset), keyLen);
				*offset += keyLen;

				Napi::Value value;
				if (!DecodeValue(env, data, length, offset, &value, depth + 1)) {
					return false;
				}
				obj.Set(key, value);
			}
			*out = obj;
			return true;
		}
		default:
			return common::napi_guard::ThrowError(env, "decodeAuthValue unknown tag");
	}
}

} // namespace

Napi::Value EncodeAuthValue(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireMinArgs(info, 1, "encodeAuthValue(value) requires value")) {
		return env.Null();
	}

	std::vector<uint8_t> out;
	out.reserve(256);
	if (!EncodeValue(env, info[0], &out, 0)) {
		return env.Null();
	}
	return common::MoveVectorToBuffer(env, std::move(out));
}

Napi::Value DecodeAuthValue(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireMinArgs(info, 1, "decodeAuthValue(data) requires data")) {
		return env.Null();
	}

	common::ByteView encoded;
	if (!common::GetByteViewFromValue(env, info[0], "data", &encoded)) {
		return env.Null();
	}

	size_t offset = 0;
	Napi::Value decoded;
	if (!DecodeValue(env, encoded.data, encoded.length, &offset, &decoded, 0)) {
		return env.Null();
	}

	if (offset != encoded.length) {
		return common::napi_guard::ThrowErrorValue(env, "decodeAuthValue trailing bytes");
	}

	return decoded;
}

} // namespace baileys_native
