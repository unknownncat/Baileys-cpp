#include "app_state_payload.h"

#include "common.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace baileys_native {

namespace {

void WriteU32BE(uint32_t value, uint8_t* out) {
	out[0] = static_cast<uint8_t>((value >> 24u) & 0xffu);
	out[1] = static_cast<uint8_t>((value >> 16u) & 0xffu);
	out[2] = static_cast<uint8_t>((value >> 8u) & 0xffu);
	out[3] = static_cast<uint8_t>(value & 0xffu);
}

void WriteVersionAs64BitNetworkOrder(uint32_t version, uint8_t* out8) {
	std::memset(out8, 0, 8);
	WriteU32BE(version, out8 + 4);
}

} // namespace

Napi::Value BuildMutationMacPayload(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 3) {
		Napi::TypeError::New(env, "buildMutationMacPayload(operation, data, keyId) requires 3 arguments")
			.ThrowAsJavaScriptException();
		return env.Null();
	}

	uint32_t operation = 0;
	if (!common::ReadUInt32FromValue(env, info[0], "operation", &operation)) {
		return env.Null();
	}
	const uint8_t opByte = operation == 0 ? 0x01 : 0x02;

	common::ByteView data;
	if (!common::GetByteViewFromValue(env, info[1], "data", &data)) {
		return env.Null();
	}

	common::ByteView keyId;
	if (!common::GetByteViewFromValue(env, info[2], "keyId", &keyId)) {
		return env.Null();
	}
	if (keyId.length > std::numeric_limits<uint8_t>::max()) {
		Napi::RangeError::New(env, "keyId length must fit in uint8").ThrowAsJavaScriptException();
		return env.Null();
	}

	const size_t keyDataLen = 1 + keyId.length;
	const size_t totalLen = keyDataLen + data.length + 8;

	Napi::Buffer<uint8_t> out = Napi::Buffer<uint8_t>::New(env, totalLen);
	uint8_t* ptr = out.Data();
	ptr[0] = opByte;
	if (keyId.length > 0) {
		std::memcpy(ptr + 1, keyId.data, keyId.length);
	}
	if (data.length > 0) {
		std::memcpy(ptr + keyDataLen, data.data, data.length);
	}

	uint8_t last[8];
	WriteVersionAs64BitNetworkOrder(static_cast<uint32_t>(keyDataLen), last);
	std::memcpy(ptr + keyDataLen + data.length, last, 8);
	return out;
}

Napi::Value BuildSnapshotMacPayload(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 3) {
		Napi::TypeError::New(env, "buildSnapshotMacPayload(lthash, version, name) requires 3 arguments")
			.ThrowAsJavaScriptException();
		return env.Null();
	}

	common::ByteView lthash;
	if (!common::GetByteViewFromValue(env, info[0], "lthash", &lthash)) {
		return env.Null();
	}

	uint32_t version = 0;
	if (!common::ReadUInt32FromValue(env, info[1], "version", &version)) {
		return env.Null();
	}

	std::string name;
	if (!common::ReadStringFromValue(env, info[2], "name", &name)) {
		return env.Null();
	}

	const size_t totalLen = lthash.length + 8 + name.size();
	Napi::Buffer<uint8_t> out = Napi::Buffer<uint8_t>::New(env, totalLen);
	uint8_t* ptr = out.Data();

	if (lthash.length > 0) {
		std::memcpy(ptr, lthash.data, lthash.length);
	}

	WriteVersionAs64BitNetworkOrder(version, ptr + lthash.length);

	if (!name.empty()) {
		std::memcpy(ptr + lthash.length + 8, name.data(), name.size());
	}

	return out;
}

Napi::Value BuildPatchMacPayload(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 4) {
		Napi::TypeError::New(env, "buildPatchMacPayload(snapshotMac, valueMacs, version, type) requires 4 arguments")
			.ThrowAsJavaScriptException();
		return env.Null();
	}

	common::ByteView snapshotMac;
	if (!common::GetByteViewFromValue(env, info[0], "snapshotMac", &snapshotMac)) {
		return env.Null();
	}

	if (!info[1].IsArray()) {
		Napi::TypeError::New(env, "valueMacs must be an array").ThrowAsJavaScriptException();
		return env.Null();
	}
	Napi::Array valueMacs = info[1].As<Napi::Array>();

	uint32_t version = 0;
	if (!common::ReadUInt32FromValue(env, info[2], "version", &version)) {
		return env.Null();
	}

	std::string type;
	if (!common::ReadStringFromValue(env, info[3], "type", &type)) {
		return env.Null();
	}

	std::vector<common::ByteView> macViews;
	macViews.reserve(valueMacs.Length());
	size_t macBytes = 0;
	for (uint32_t i = 0; i < valueMacs.Length(); ++i) {
		common::ByteView view;
		if (!common::GetByteViewFromValue(env, valueMacs.Get(i), "valueMacs[]", &view)) {
			return env.Null();
		}
		macViews.push_back(view);
		macBytes += view.length;
	}

	const size_t totalLen = snapshotMac.length + macBytes + 8 + type.size();
	Napi::Buffer<uint8_t> out = Napi::Buffer<uint8_t>::New(env, totalLen);
	uint8_t* ptr = out.Data();
	size_t offset = 0;

	if (snapshotMac.length > 0) {
		std::memcpy(ptr + offset, snapshotMac.data, snapshotMac.length);
		offset += snapshotMac.length;
	}

	for (const auto& mac : macViews) {
		if (mac.length > 0) {
			std::memcpy(ptr + offset, mac.data, mac.length);
			offset += mac.length;
		}
	}

	WriteVersionAs64BitNetworkOrder(version, ptr + offset);
	offset += 8;

	if (!type.empty()) {
		std::memcpy(ptr + offset, type.data(), type.size());
		offset += type.size();
	}

	return out;
}

} // namespace baileys_native
