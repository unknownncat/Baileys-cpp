#include "e2e_session_extractor.h"

#include "common.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace baileys_native {

namespace {

constexpr uint8_t kSignalKeyBundleType = 5;

bool ReadStringProperty(const Napi::Object& obj, const char* key, std::string* out) {
	const Napi::Value value = obj.Get(key);
	if (!value.IsString()) {
		return false;
	}

	*out = value.As<Napi::String>().Utf8Value();
	return true;
}

bool ReadNodeTag(const Napi::Object& node, std::string* out) {
	return ReadStringProperty(node, "tag", out);
}

bool ReadNodeContentArray(const Napi::Object& node, Napi::Array* out) {
	const Napi::Value content = node.Get("content");
	if (!content.IsArray()) {
		return false;
	}

	*out = content.As<Napi::Array>();
	return true;
}

bool ReadNodeBytes(const Napi::Env& env, const Napi::Object& node, std::vector<uint8_t>* out) {
	const Napi::Value content = node.Get("content");
	return common::CopyBytesFromValue(env, content, "node.content", out);
}

bool FindChildNodeByTag(const Napi::Array& content, const char* tag, Napi::Object* outNode) {
	for (uint32_t i = 0; i < content.Length(); ++i) {
		const Napi::Value item = content.Get(i);
		if (!item.IsObject()) {
			continue;
		}

		Napi::Object child = item.As<Napi::Object>();
		std::string childTag;
		if (!ReadNodeTag(child, &childTag)) {
			continue;
		}

		if (childTag == tag) {
			*outNode = child;
			return true;
		}
	}

	return false;
}

bool ReadUIntFromBigEndian(const std::vector<uint8_t>& bytes, uint32_t* out) {
	if (bytes.empty()) {
		return false;
	}

	uint32_t value = 0;
	for (uint8_t byte : bytes) {
		value = (value << 8u) | static_cast<uint32_t>(byte);
	}

	*out = value;
	return true;
}

void EnsureSignalPubKey(std::vector<uint8_t>* key) {
	if (key->size() == 33) {
		return;
	}

	std::vector<uint8_t> prefixed;
	prefixed.reserve(key->size() + 1);
	prefixed.push_back(kSignalKeyBundleType);
	prefixed.insert(prefixed.end(), key->begin(), key->end());
	key->swap(prefixed);
}

bool ParseKeyNode(
	const Napi::Env& env,
	const Napi::Object& keyNode,
	uint32_t* keyId,
	std::vector<uint8_t>* publicKey,
	std::vector<uint8_t>* signature,
	bool requireSignature
) {
	Napi::Array keyContent;
	if (!ReadNodeContentArray(keyNode, &keyContent)) {
		return false;
	}

	Napi::Object idNode;
	Napi::Object valueNode;
	if (!FindChildNodeByTag(keyContent, "id", &idNode) || !FindChildNodeByTag(keyContent, "value", &valueNode)) {
		return false;
	}

	std::vector<uint8_t> idBytes;
	if (!ReadNodeBytes(env, idNode, &idBytes) || !ReadUIntFromBigEndian(idBytes, keyId)) {
		return false;
	}

	if (!ReadNodeBytes(env, valueNode, publicKey)) {
		return false;
	}
	EnsureSignalPubKey(publicKey);

	signature->clear();
	Napi::Object signatureNode;
	if (FindChildNodeByTag(keyContent, "signature", &signatureNode)) {
		if (!ReadNodeBytes(env, signatureNode, signature)) {
			return false;
		}
	}

	if (requireSignature && signature->empty()) {
		return false;
	}

	return true;
}

Napi::Object BuildPreKeyObject(
	const Napi::Env& env,
	uint32_t keyId,
	std::vector<uint8_t>&& publicKey
) {
	Napi::Object out = Napi::Object::New(env);
	out.Set("keyId", Napi::Number::New(env, static_cast<double>(keyId)));
	out.Set("publicKey", common::MoveVectorToBuffer(env, std::move(publicKey)));
	return out;
}

Napi::Object BuildSignedPreKeyObject(
	const Napi::Env& env,
	uint32_t keyId,
	std::vector<uint8_t>&& publicKey,
	std::vector<uint8_t>&& signature
) {
	Napi::Object out = BuildPreKeyObject(env, keyId, std::move(publicKey));
	out.Set("signature", common::MoveVectorToBuffer(env, std::move(signature)));
	return out;
}

} // namespace

Napi::Value ExtractE2ESessionBundlesFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1 || !info[0].IsArray()) {
		Napi::TypeError::New(env, "extractE2ESessionBundlesFast(users) requires an array")
			.ThrowAsJavaScriptException();
		return env.Null();
	}

	const Napi::Array users = info[0].As<Napi::Array>();
	Napi::Array out = Napi::Array::New(env, users.Length());

	for (uint32_t i = 0; i < users.Length(); ++i) {
		const Napi::Value value = users.Get(i);
		if (!value.IsObject()) {
			Napi::TypeError::New(env, "users[] must be objects").ThrowAsJavaScriptException();
			return env.Null();
		}

		Napi::Object userNode = value.As<Napi::Object>();
		Napi::Value attrsValue = userNode.Get("attrs");
		if (!attrsValue.IsObject()) {
			Napi::TypeError::New(env, "users[].attrs must be object").ThrowAsJavaScriptException();
			return env.Null();
		}

		Napi::Object attrs = attrsValue.As<Napi::Object>();
		std::string jid;
		if (!ReadStringProperty(attrs, "jid", &jid) || jid.empty()) {
			Napi::TypeError::New(env, "users[].attrs.jid missing").ThrowAsJavaScriptException();
			return env.Null();
		}

		Napi::Array userContent;
		if (!ReadNodeContentArray(userNode, &userContent)) {
			Napi::TypeError::New(env, "users[].content must be array").ThrowAsJavaScriptException();
			return env.Null();
		}

		Napi::Object signedKeyNode;
		Napi::Object keyNode;
		Napi::Object identityNode;
		Napi::Object registrationNode;
		if (!FindChildNodeByTag(userContent, "skey", &signedKeyNode) ||
			!FindChildNodeByTag(userContent, "key", &keyNode) ||
			!FindChildNodeByTag(userContent, "identity", &identityNode) ||
			!FindChildNodeByTag(userContent, "registration", &registrationNode)) {
			Napi::TypeError::New(env, "users[] missing required key nodes").ThrowAsJavaScriptException();
			return env.Null();
		}

		std::vector<uint8_t> identityKey;
		if (!ReadNodeBytes(env, identityNode, &identityKey)) {
			Napi::TypeError::New(env, "invalid identity key bytes").ThrowAsJavaScriptException();
			return env.Null();
		}
		EnsureSignalPubKey(&identityKey);

		std::vector<uint8_t> registrationBytes;
		uint32_t registrationId = 0;
		if (!ReadNodeBytes(env, registrationNode, &registrationBytes) ||
			!ReadUIntFromBigEndian(registrationBytes, &registrationId)) {
			Napi::TypeError::New(env, "invalid registration bytes").ThrowAsJavaScriptException();
			return env.Null();
		}

		uint32_t signedKeyId = 0;
		std::vector<uint8_t> signedKeyPublic;
		std::vector<uint8_t> signedKeySignature;
		if (!ParseKeyNode(
				env,
				signedKeyNode,
				&signedKeyId,
				&signedKeyPublic,
				&signedKeySignature,
				true
			)) {
			Napi::TypeError::New(env, "invalid signed pre key node").ThrowAsJavaScriptException();
			return env.Null();
		}

		uint32_t preKeyId = 0;
		std::vector<uint8_t> preKeyPublic;
		std::vector<uint8_t> preKeySignatureUnused;
		if (!ParseKeyNode(
				env,
				keyNode,
				&preKeyId,
				&preKeyPublic,
				&preKeySignatureUnused,
				false
			)) {
			Napi::TypeError::New(env, "invalid pre key node").ThrowAsJavaScriptException();
			return env.Null();
		}

		Napi::Object session = Napi::Object::New(env);
		session.Set("registrationId", Napi::Number::New(env, static_cast<double>(registrationId)));
		session.Set("identityKey", common::MoveVectorToBuffer(env, std::move(identityKey)));
		session.Set(
			"signedPreKey",
			BuildSignedPreKeyObject(env, signedKeyId, std::move(signedKeyPublic), std::move(signedKeySignature))
		);
		session.Set("preKey", BuildPreKeyObject(env, preKeyId, std::move(preKeyPublic)));

		Napi::Object item = Napi::Object::New(env);
		item.Set("jid", jid);
		item.Set("session", session);
		out.Set(i, item);
	}

	return out;
}

} // namespace baileys_native
