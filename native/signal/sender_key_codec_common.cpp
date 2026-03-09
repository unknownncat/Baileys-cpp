#include "internal/sender_key_codec_shared.h"

#include "common.h"

#include <string>
#include <utility>

namespace baileys_native::sender_key_codec {

namespace {

Napi::Object BuildMessageEntryObject(const Napi::Env& env, const MessageKeyEntry& entry) {
	Napi::Object obj = Napi::Object::New(env);
	obj.Set("iteration", Napi::Number::New(env, entry.iteration));
	obj.Set("seed", Napi::Buffer<uint8_t>::Copy(env, entry.seed.data(), entry.seed.size()));
	return obj;
}

bool ParseMessageEntry(const Napi::Env& env, const Napi::Value& value, MessageKeyEntry* out) {
	if (!value.IsObject()) {
		Napi::TypeError::New(env, "senderMessageKeys entry must be an object").ThrowAsJavaScriptException();
		return false;
	}

	Napi::Object obj = value.As<Napi::Object>();
	if (!common::ReadUInt32FromValue(env, obj.Get("iteration"), "senderMessageKeys[].iteration", &out->iteration)) {
		return false;
	}

	if (!common::CopyBytesFromValue(env, obj.Get("seed"), "senderMessageKeys[].seed", &out->seed)) {
		return false;
	}
	return true;
}

bool ParseState(const Napi::Env& env, const Napi::Value& value, SenderKeyStateNative* out) {
	if (!value.IsObject()) {
		Napi::TypeError::New(env, "sender-key state must be an object").ThrowAsJavaScriptException();
		return false;
	}

	Napi::Object obj = value.As<Napi::Object>();
	if (!common::ReadUInt32FromValue(env, obj.Get("senderKeyId"), "senderKeyId", &out->senderKeyId)) {
		return false;
	}

	Napi::Value chainKeyVal = obj.Get("senderChainKey");
	if (!chainKeyVal.IsObject()) {
		Napi::TypeError::New(env, "senderChainKey must be an object").ThrowAsJavaScriptException();
		return false;
	}
	Napi::Object chainKey = chainKeyVal.As<Napi::Object>();
	if (!common::ReadUInt32FromValue(env, chainKey.Get("iteration"), "senderChainKey.iteration", &out->chainIteration)) {
		return false;
	}
	if (!common::CopyBytesFromValue(env, chainKey.Get("seed"), "senderChainKey.seed", &out->chainSeed)) {
		return false;
	}

	Napi::Value signingVal = obj.Get("senderSigningKey");
	if (!signingVal.IsObject()) {
		Napi::TypeError::New(env, "senderSigningKey must be an object").ThrowAsJavaScriptException();
		return false;
	}
	Napi::Object signing = signingVal.As<Napi::Object>();
	if (!common::CopyBytesFromValue(env, signing.Get("public"), "senderSigningKey.public", &out->signingPublic)) {
		return false;
	}
	if (!common::CopyOptionalBytesFromValue(env, signing.Get("private"), "senderSigningKey.private", &out->signingPrivate)) {
		return false;
	}

	Napi::Value messageKeysVal = obj.Get("senderMessageKeys");
	if (!messageKeysVal.IsArray()) {
		Napi::TypeError::New(env, "senderMessageKeys must be an array").ThrowAsJavaScriptException();
		return false;
	}

	Napi::Array messageKeys = messageKeysVal.As<Napi::Array>();
	const uint32_t length = messageKeys.Length();
	out->senderMessageKeys.clear();
	out->senderMessageKeys.reserve(length);

	for (uint32_t i = 0; i < length; ++i) {
		MessageKeyEntry msg;
		if (!ParseMessageEntry(env, messageKeys.Get(i), &msg)) {
			return false;
		}
		out->senderMessageKeys.push_back(std::move(msg));
	}

	return true;
}

} // namespace

bool IsBinarySenderKeyRaw(const uint8_t* data, size_t len) {
	if (len < sizeof(kMagic) + 1) return false;
	if (std::memcmp(data, kMagic, sizeof(kMagic)) != 0) return false;
	return data[sizeof(kMagic)] == kFormatVersion;
}

Napi::Object BuildStateObject(const Napi::Env& env, const SenderKeyStateNative& state) {
	Napi::Object root = Napi::Object::New(env);
	root.Set("senderKeyId", Napi::Number::New(env, state.senderKeyId));

	Napi::Object chainKey = Napi::Object::New(env);
	chainKey.Set("iteration", Napi::Number::New(env, state.chainIteration));
	chainKey.Set("seed", Napi::Buffer<uint8_t>::Copy(env, state.chainSeed.data(), state.chainSeed.size()));
	root.Set("senderChainKey", chainKey);

	Napi::Object signing = Napi::Object::New(env);
	signing.Set("public", Napi::Buffer<uint8_t>::Copy(env, state.signingPublic.data(), state.signingPublic.size()));
	if (!state.signingPrivate.empty()) {
		signing.Set("private", Napi::Buffer<uint8_t>::Copy(env, state.signingPrivate.data(), state.signingPrivate.size()));
	}
	root.Set("senderSigningKey", signing);

	Napi::Array messageKeys = Napi::Array::New(env, state.senderMessageKeys.size());
	for (size_t i = 0; i < state.senderMessageKeys.size(); ++i) {
		messageKeys.Set(i, BuildMessageEntryObject(env, state.senderMessageKeys[i]));
	}
	root.Set("senderMessageKeys", messageKeys);
	return root;
}

bool ParseStatesArray(const Napi::Env& env, const Napi::Value& value, std::vector<SenderKeyStateNative>* out) {
	if (!value.IsArray()) {
		Napi::TypeError::New(env, "states must be an array").ThrowAsJavaScriptException();
		return false;
	}

	Napi::Array states = value.As<Napi::Array>();
	const uint32_t length = states.Length();
	out->clear();
	out->reserve(length);

	for (uint32_t i = 0; i < length; ++i) {
		SenderKeyStateNative state;
		if (!ParseState(env, states.Get(i), &state)) {
			return false;
		}
		out->push_back(std::move(state));
	}

	return true;
}

} // namespace baileys_native::sender_key_codec