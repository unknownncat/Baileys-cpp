#include "sender_key_codec.h"

#include "common.h"
#include "internal/sender_key_codec_shared.h"

#include <vector>

namespace baileys_native {

Napi::Value DecodeSenderKeyStates(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1) {
		Napi::TypeError::New(env, "decodeSenderKeyStates(data) requires data").ThrowAsJavaScriptException();
		return env.Null();
	}

	std::vector<uint8_t> bytes;
	if (!common::CopyBytesFromValue(env, info[0], "data", &bytes)) {
		return env.Null();
	}

	if (!sender_key_codec::IsBinarySenderKeyRaw(bytes.data(), bytes.size())) {
		Napi::TypeError::New(env, "Input is not in sender-key binary format").ThrowAsJavaScriptException();
		return env.Null();
	}

	sender_key_codec::DataReader reader(bytes.data(), bytes.size());
	uint8_t magic0 = 0;
	uint8_t magic1 = 0;
	uint8_t magic2 = 0;
	uint8_t magic3 = 0;
	uint8_t version = 0;
	uint16_t statesCount = 0;

	if (!reader.ReadU8(&magic0) || !reader.ReadU8(&magic1) || !reader.ReadU8(&magic2) || !reader.ReadU8(&magic3)) {
		Napi::TypeError::New(env, "Malformed sender-key binary header").ThrowAsJavaScriptException();
		return env.Null();
	}

	if (!reader.ReadU8(&version) || version != sender_key_codec::kFormatVersion) {
		Napi::TypeError::New(env, "Unsupported sender-key binary version").ThrowAsJavaScriptException();
		return env.Null();
	}

	if (!reader.ReadU16LE(&statesCount)) {
		Napi::TypeError::New(env, "Malformed sender-key binary state count").ThrowAsJavaScriptException();
		return env.Null();
	}

	Napi::Array out = Napi::Array::New(env, statesCount);

	for (uint16_t i = 0; i < statesCount; ++i) {
		sender_key_codec::SenderKeyStateNative state;
		uint16_t chainSeedLen = 0;
		uint16_t signingPubLen = 0;
		uint16_t signingPrivLen = 0;
		uint16_t msgCount = 0;

		if (!reader.ReadU32LE(&state.senderKeyId) || !reader.ReadU32LE(&state.chainIteration)) {
			Napi::TypeError::New(env, "Malformed sender-key state header").ThrowAsJavaScriptException();
			return env.Null();
		}

		if (!reader.ReadU16LE(&chainSeedLen) || !reader.ReadBytes(chainSeedLen, &state.chainSeed)) {
			Napi::TypeError::New(env, "Malformed sender-chain seed").ThrowAsJavaScriptException();
			return env.Null();
		}

		if (!reader.ReadU16LE(&signingPubLen) || !reader.ReadBytes(signingPubLen, &state.signingPublic)) {
			Napi::TypeError::New(env, "Malformed sender signing public key").ThrowAsJavaScriptException();
			return env.Null();
		}

		if (!reader.ReadU16LE(&signingPrivLen) || !reader.ReadBytes(signingPrivLen, &state.signingPrivate)) {
			Napi::TypeError::New(env, "Malformed sender signing private key").ThrowAsJavaScriptException();
			return env.Null();
		}

		if (!reader.ReadU16LE(&msgCount)) {
			Napi::TypeError::New(env, "Malformed sender message key count").ThrowAsJavaScriptException();
			return env.Null();
		}

		state.senderMessageKeys.reserve(msgCount);
		for (uint16_t j = 0; j < msgCount; ++j) {
			sender_key_codec::MessageKeyEntry msg;
			uint16_t seedLen = 0;
			if (!reader.ReadU32LE(&msg.iteration) || !reader.ReadU16LE(&seedLen) || !reader.ReadBytes(seedLen, &msg.seed)) {
				Napi::TypeError::New(env, "Malformed sender message key").ThrowAsJavaScriptException();
				return env.Null();
			}
			state.senderMessageKeys.push_back(std::move(msg));
		}

		out.Set(i, sender_key_codec::BuildStateObject(env, state));
	}

	if (reader.Remaining() != 0) {
		Napi::TypeError::New(env, "Malformed sender-key binary payload: trailing bytes").ThrowAsJavaScriptException();
		return env.Null();
	}

	return out;
}

Napi::Value IsSenderKeyBinary(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1) {
		return Napi::Boolean::New(env, false);
	}

	std::vector<uint8_t> bytes;
	if (!common::CopyBytesFromValue(env, info[0], "data", &bytes)) {
		return Napi::Boolean::New(env, false);
	}
	return Napi::Boolean::New(env, sender_key_codec::IsBinarySenderKeyRaw(bytes.data(), bytes.size()));
}

} // namespace baileys_native