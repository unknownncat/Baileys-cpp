#include "sender_key_codec.h"

#include "common.h"
#include "internal/sender_key_codec_shared.h"

#include <utility>
#include <vector>

namespace baileys_native {

Napi::Value EncodeSenderKeyStates(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1) {
		Napi::TypeError::New(env, "encodeSenderKeyStates(states) requires states").ThrowAsJavaScriptException();
		return env.Null();
	}

	std::vector<sender_key_codec::SenderKeyStateNative> states;
	if (!sender_key_codec::ParseStatesArray(env, info[0], &states)) {
		return env.Null();
	}

	if (!common::CheckFitsU16(env, states.size(), "states.length")) {
		return env.Null();
	}

	std::vector<uint8_t> out;
	out.reserve(256);
	out.insert(out.end(), std::begin(sender_key_codec::kMagic), std::end(sender_key_codec::kMagic));
	out.push_back(sender_key_codec::kFormatVersion);
	sender_key_codec::WriteU16LE(&out, static_cast<uint16_t>(states.size()));

	for (const auto& state : states) {
		if (!common::CheckFitsU16(env, state.chainSeed.size(), "senderChainKey.seed")) return env.Null();
		if (!common::CheckFitsU16(env, state.signingPublic.size(), "senderSigningKey.public")) return env.Null();
		if (!common::CheckFitsU16(env, state.signingPrivate.size(), "senderSigningKey.private")) return env.Null();
		if (!common::CheckFitsU16(env, state.senderMessageKeys.size(), "senderMessageKeys.length")) return env.Null();

		sender_key_codec::WriteU32LE(&out, state.senderKeyId);
		sender_key_codec::WriteU32LE(&out, state.chainIteration);

		sender_key_codec::WriteU16LE(&out, static_cast<uint16_t>(state.chainSeed.size()));
		out.insert(out.end(), state.chainSeed.begin(), state.chainSeed.end());

		sender_key_codec::WriteU16LE(&out, static_cast<uint16_t>(state.signingPublic.size()));
		out.insert(out.end(), state.signingPublic.begin(), state.signingPublic.end());

		sender_key_codec::WriteU16LE(&out, static_cast<uint16_t>(state.signingPrivate.size()));
		out.insert(out.end(), state.signingPrivate.begin(), state.signingPrivate.end());

		sender_key_codec::WriteU16LE(&out, static_cast<uint16_t>(state.senderMessageKeys.size()));
		for (const auto& msg : state.senderMessageKeys) {
			if (!common::CheckFitsU16(env, msg.seed.size(), "senderMessageKeys[].seed")) return env.Null();
			sender_key_codec::WriteU32LE(&out, msg.iteration);
			sender_key_codec::WriteU16LE(&out, static_cast<uint16_t>(msg.seed.size()));
			out.insert(out.end(), msg.seed.begin(), msg.seed.end());
		}
	}

	return common::MoveVectorToBuffer(env, std::move(out));
}

} // namespace baileys_native
