#include "wam_encode.h"

#include "common.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace baileys_native {

namespace {

constexpr uint8_t kFlagByte = 8;

enum class ValueKind {
	kNull,
	kNumber,
	kString
};

struct WamRecord {
	uint32_t key = 0;
	uint8_t flag = 0;
	ValueKind kind = ValueKind::kNull;
	double numberValue = 0.0;
	std::string stringValue;
};

void AppendUInt16LE(std::vector<uint8_t>* out, uint16_t value) {
	out->push_back(static_cast<uint8_t>(value & 0xffu));
	out->push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
}

void AppendInt16LE(std::vector<uint8_t>* out, int16_t value) {
	AppendUInt16LE(out, static_cast<uint16_t>(value));
}

void AppendUInt32LE(std::vector<uint8_t>* out, uint32_t value) {
	out->push_back(static_cast<uint8_t>(value & 0xffu));
	out->push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
	out->push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
	out->push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
}

void AppendInt32LE(std::vector<uint8_t>* out, int32_t value) {
	AppendUInt32LE(out, static_cast<uint32_t>(value));
}

void AppendDoubleLE(std::vector<uint8_t>* out, double value) {
	uint64_t raw = 0;
	static_assert(sizeof(raw) == sizeof(value), "Unexpected double size");
	std::memcpy(&raw, &value, sizeof(raw));
	for (int i = 0; i < 8; ++i) {
		out->push_back(static_cast<uint8_t>((raw >> (8 * i)) & 0xffu));
	}
}

void AppendHeader(std::vector<uint8_t>* out, uint32_t key, uint8_t flag) {
	if (key < 256u) {
		out->push_back(flag);
		out->push_back(static_cast<uint8_t>(key));
		return;
	}

	out->push_back(static_cast<uint8_t>(flag | kFlagByte));
	AppendUInt16LE(out, static_cast<uint16_t>(key));
}

bool ParseRecord(const Napi::Env& env, const Napi::Value& value, uint32_t index, WamRecord* out) {
	if (!value.IsObject()) {
		Napi::TypeError::New(env, "entries must contain only objects").ThrowAsJavaScriptException();
		return false;
	}

	const Napi::Object obj = value.As<Napi::Object>();
	uint32_t key = 0;
	if (!common::ReadUInt32FromValue(env, obj.Get("key"), "entries[].key", &key)) {
		return false;
	}
	if (key > std::numeric_limits<uint16_t>::max()) {
		Napi::RangeError::New(env, "entries[].key must be <= 65535").ThrowAsJavaScriptException();
		return false;
	}

	uint32_t flag = 0;
	if (!common::ReadUInt32FromValue(env, obj.Get("flag"), "entries[].flag", &flag)) {
		return false;
	}
	if (flag > std::numeric_limits<uint8_t>::max()) {
		Napi::RangeError::New(env, "entries[].flag must be <= 255").ThrowAsJavaScriptException();
		return false;
	}

	const Napi::Value itemValue = obj.Get("value");
	out->key = key;
	out->flag = static_cast<uint8_t>(flag);
	out->stringValue.clear();
	out->numberValue = 0.0;

	if (itemValue.IsNull() || itemValue.IsUndefined()) {
		out->kind = ValueKind::kNull;
		return true;
	}

	if (itemValue.IsBoolean()) {
		out->kind = ValueKind::kNumber;
		out->numberValue = itemValue.As<Napi::Boolean>().Value() ? 1.0 : 0.0;
		return true;
	}

	if (itemValue.IsNumber()) {
		out->kind = ValueKind::kNumber;
		out->numberValue = itemValue.As<Napi::Number>().DoubleValue();
		return true;
	}

	if (itemValue.IsString()) {
		out->kind = ValueKind::kString;
		out->stringValue = itemValue.As<Napi::String>().Utf8Value();
		return true;
	}

	Napi::TypeError::New(
		env,
		(std::string("entries[") + std::to_string(index) + "].value must be number/string/null").c_str()
	)
		.ThrowAsJavaScriptException();
	return false;
}

bool SerializeRecord(const Napi::Env& env, const WamRecord& record, std::vector<uint8_t>* out) {
	const uint8_t flag = record.flag;

	if (record.kind == ValueKind::kNull) {
		if (flag == 0) {
			AppendHeader(out, record.key, flag);
			return true;
		}

		Napi::Error::New(env, "missing").ThrowAsJavaScriptException();
		return false;
	}

	if (record.kind == ValueKind::kNumber) {
		const double value = record.numberValue;
		const bool isInteger = std::isfinite(value) && std::floor(value) == value;
		if (isInteger) {
			if (value == 0.0 || value == 1.0) {
				AppendHeader(out, record.key, static_cast<uint8_t>(flag | ((static_cast<int>(value) + 1) << 4)));
				return true;
			}

			if (value >= -128.0 && value < 128.0) {
				AppendHeader(out, record.key, static_cast<uint8_t>(flag | (3 << 4)));
				out->push_back(static_cast<uint8_t>(static_cast<int8_t>(value)));
				return true;
			}

			if (value >= -32768.0 && value < 32768.0) {
				AppendHeader(out, record.key, static_cast<uint8_t>(flag | (4 << 4)));
				AppendInt16LE(out, static_cast<int16_t>(value));
				return true;
			}

			if (value >= -2147483648.0 && value < 2147483648.0) {
				AppendHeader(out, record.key, static_cast<uint8_t>(flag | (5 << 4)));
				AppendInt32LE(out, static_cast<int32_t>(value));
				return true;
			}
		}

		AppendHeader(out, record.key, static_cast<uint8_t>(flag | (7 << 4)));
		AppendDoubleLE(out, value);
		return true;
	}

	if (record.kind == ValueKind::kString) {
		const size_t utf8Bytes = record.stringValue.size();
		if (utf8Bytes > std::numeric_limits<uint32_t>::max()) {
			Napi::RangeError::New(env, "string value too large").ThrowAsJavaScriptException();
			return false;
		}

		if (utf8Bytes < 256u) {
			AppendHeader(out, record.key, static_cast<uint8_t>(flag | (8 << 4)));
			out->push_back(static_cast<uint8_t>(utf8Bytes));
		} else if (utf8Bytes < 65536u) {
			AppendHeader(out, record.key, static_cast<uint8_t>(flag | (9 << 4)));
			AppendUInt16LE(out, static_cast<uint16_t>(utf8Bytes));
		} else {
			AppendHeader(out, record.key, static_cast<uint8_t>(flag | (10 << 4)));
			AppendUInt32LE(out, static_cast<uint32_t>(utf8Bytes));
		}

		out->insert(out->end(), record.stringValue.begin(), record.stringValue.end());
		return true;
	}

	Napi::TypeError::New(env, "unsupported value type").ThrowAsJavaScriptException();
	return false;
}

} // namespace

Napi::Value EncodeWAMFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 3 || !info[2].IsArray()) {
		Napi::TypeError::New(env, "encodeWAMFast(protocolVersion, sequence, entries) expects entries array")
			.ThrowAsJavaScriptException();
		return env.Null();
	}

	uint32_t protocolVersion = 0;
	if (!common::ReadUInt32FromValue(env, info[0], "protocolVersion", &protocolVersion)) {
		return env.Null();
	}
	if (protocolVersion > std::numeric_limits<uint8_t>::max()) {
		Napi::RangeError::New(env, "protocolVersion must be <= 255").ThrowAsJavaScriptException();
		return env.Null();
	}

	uint32_t sequence = 0;
	if (!common::ReadUInt32FromValue(env, info[1], "sequence", &sequence)) {
		return env.Null();
	}
	if (sequence > std::numeric_limits<uint16_t>::max()) {
		Napi::RangeError::New(env, "sequence must be <= 65535").ThrowAsJavaScriptException();
		return env.Null();
	}

	const Napi::Array entries = info[2].As<Napi::Array>();
	const uint32_t count = entries.Length();
	std::vector<uint8_t> out;
	out.reserve(8 + (count * 8));
	out.push_back(static_cast<uint8_t>('W'));
	out.push_back(static_cast<uint8_t>('A'));
	out.push_back(static_cast<uint8_t>('M'));
	out.push_back(static_cast<uint8_t>(protocolVersion));
	out.push_back(1u);
	out.push_back(static_cast<uint8_t>((sequence >> 8u) & 0xffu));
	out.push_back(static_cast<uint8_t>(sequence & 0xffu));
	out.push_back(0u);

	for (uint32_t i = 0; i < count; ++i) {
		WamRecord record;
		if (!ParseRecord(env, entries.Get(i), i, &record)) {
			return env.Null();
		}

		if (!SerializeRecord(env, record, &out)) {
			return env.Null();
		}
	}

	return common::MoveVectorToBuffer(env, std::move(out));
}

} // namespace baileys_native
