#include "appstate/internal/mutation_engine_internal.h"

#include "common/feature_gates.h"

#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
#include "generated/wa_proto_adapter.h"
#include "media/internal/cng_crypto.h"
#include "protobuf_reflection_codec.h"
#endif

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace baileys_native::appstate_internal {

namespace {

bool SecureEquals(const uint8_t* a, const uint8_t* b, size_t length) {
	uint8_t diff = 0;
	for (size_t i = 0; i < length; ++i) {
		diff |= (a[i] ^ b[i]);
	}
	return diff == 0;
}

void WriteU64BE(uint64_t value, uint8_t* out) {
	for (int i = 7; i >= 0; --i) {
		out[7 - i] = static_cast<uint8_t>((value >> (i * 8)) & 0xffu);
	}
}

void BuildMutationMacPayload(
	uint32_t operation,
	const uint8_t* encryptedContent,
	size_t encryptedLength,
	const std::vector<uint8_t>& keyId,
	std::vector<uint8_t>* out
) {
	const uint8_t opByte = operation == 0 ? 0x01 : 0x02;
	const uint64_t keyDataLength = 1 + keyId.size();
	const size_t totalLength = static_cast<size_t>(keyDataLength) + encryptedLength + 8;

	out->assign(totalLength, 0);
	(*out)[0] = opByte;
	if (!keyId.empty()) {
		std::memcpy(out->data() + 1, keyId.data(), keyId.size());
	}
	if (encryptedLength > 0) {
		std::memcpy(out->data() + static_cast<size_t>(keyDataLength), encryptedContent, encryptedLength);
	}
	WriteU64BE(keyDataLength, out->data() + static_cast<size_t>(keyDataLength) + encryptedLength);
}

#ifdef _WIN32
bool ComputeHmacSha256(
	const std::vector<uint8_t>& key,
	const uint8_t* data,
	size_t dataLength,
	std::vector<uint8_t>* out,
	std::string* error
) {
	media_internal::CngHashState hmac;
	if (!hmac.InitHmacSha256(key.data(), key.size(), error)) {
		return false;
	}
	if (!hmac.Update(data, dataLength, error)) {
		return false;
	}
	return hmac.Final(out, error);
}

bool ComputeHmacSha512(
	const std::vector<uint8_t>& key,
	const uint8_t* data,
	size_t dataLength,
	std::vector<uint8_t>* out,
	std::string* error
) {
	media_internal::CngHashState hmac;
	if (!hmac.InitHmacSha512(key.data(), key.size(), error)) {
		return false;
	}
	if (!hmac.Update(data, dataLength, error)) {
		return false;
	}
	return hmac.Final(out, error);
}
#endif

struct MutationDecodeContext {
	const DerivedMutationKey* keys = nullptr;
	const uint8_t* encryptedContent = nullptr;
	size_t encryptedLength = 0;
	const uint8_t* valueMac = nullptr;
	const uint8_t* iv = nullptr;
	const uint8_t* cipherText = nullptr;
	size_t cipherLength = 0;
};

bool ResolveMutationContext(
	const Napi::Env& env,
	const MutationInput& mutation,
	const std::unordered_map<std::string, DerivedMutationKey>& keyMap,
	MutationDecodeContext* out
) {
	if (mutation.valueBlob.size() < 32) {
		Napi::Error::New(env, "invalid encrypted mutation blob length").ThrowAsJavaScriptException();
		return false;
	}

	out->encryptedLength = mutation.valueBlob.size() - 32;
	out->encryptedContent = mutation.valueBlob.data();
	out->valueMac = mutation.valueBlob.data() + out->encryptedLength;

	const std::string keyIdBase64 = Base64Encode(mutation.keyId.data(), mutation.keyId.size());
	auto keyIt = keyMap.find(keyIdBase64);
	if (keyIt == keyMap.end()) {
		Napi::Error::New(env, std::string("missing derived key for keyId: ") + keyIdBase64)
			.ThrowAsJavaScriptException();
		return false;
	}
	out->keys = &keyIt->second;

	if (out->encryptedLength < 32 || ((out->encryptedLength - 16) % 16) != 0) {
		Napi::Error::New(env, "invalid encrypted mutation blob length").ThrowAsJavaScriptException();
		return false;
	}

	out->iv = out->encryptedContent;
	out->cipherText = out->encryptedContent + 16;
	out->cipherLength = out->encryptedLength - 16;
	return true;
}

bool VerifyContentMac(
	const Napi::Env& env,
	const MutationInput& mutation,
	const MutationDecodeContext& context
) {
	std::vector<uint8_t> payload;
	BuildMutationMacPayload(
		mutation.operation,
		context.encryptedContent,
		context.encryptedLength,
		mutation.keyId,
		&payload
	);

	std::vector<uint8_t> digest;
	std::string hashError;
	if (!ComputeHmacSha512(context.keys->valueMacKey, payload.data(), payload.size(), &digest, &hashError)) {
		Napi::Error::New(env, hashError).ThrowAsJavaScriptException();
		return false;
	}
	if (digest.size() < 32 || !SecureEquals(digest.data(), context.valueMac, 32)) {
		Napi::Error::New(env, "HMAC content verification failed").ThrowAsJavaScriptException();
		return false;
	}

	return true;
}

bool DecryptSyncAction(
	const Napi::Env& env,
	const MutationDecodeContext& context,
	proto::SyncActionData* out
) {
	media_internal::CngAesCbcState aes;
	std::string aesError;
	if (!aes.Init(context.keys->valueEncryptionKey.data(), context.keys->valueEncryptionKey.size(), context.iv, 16, &aesError)) {
		Napi::Error::New(env, aesError).ThrowAsJavaScriptException();
		return false;
	}

	std::vector<uint8_t> plain;
	if (!aes.Crypt(false, context.cipherText, context.cipherLength, true, &plain, &aesError)) {
		Napi::Error::New(env, aesError).ThrowAsJavaScriptException();
		return false;
	}

	if (plain.size() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
		!out->ParseFromArray(plain.data(), static_cast<int>(plain.size()))) {
		Napi::Error::New(env, "failed to decode SyncActionData").ThrowAsJavaScriptException();
		return false;
	}
	if (!out->has_index() || !out->has_value()) {
		Napi::Error::New(env, "decoded SyncActionData missing index/value").ThrowAsJavaScriptException();
		return false;
	}

	return true;
}

bool VerifyIndexMac(
	const Napi::Env& env,
	const MutationInput& mutation,
	const DerivedMutationKey& keys,
	const std::string& indexBlob
) {
	std::vector<uint8_t> digest;
	std::string hashError;
	if (!ComputeHmacSha256(keys.indexKey, reinterpret_cast<const uint8_t*>(indexBlob.data()), indexBlob.size(), &digest, &hashError)) {
		Napi::Error::New(env, hashError).ThrowAsJavaScriptException();
		return false;
	}
	if (digest.size() != mutation.indexMac.size() ||
		!SecureEquals(digest.data(), mutation.indexMac.data(), mutation.indexMac.size())) {
		Napi::Error::New(env, "HMAC index verification failed").ThrowAsJavaScriptException();
		return false;
	}

	return true;
}

bool BuildSyncActionObject(
	const Napi::Env& env,
	const MutationInput& mutation,
	const proto::SyncActionData& action,
	bool encodeValueAsWire,
	const uint8_t* valueMac,
	Napi::Object* out
) {
	const std::string& indexBlob = action.index();
	Napi::Object syncAction = Napi::Object::New(env);
	syncAction.Set(
		"index",
		Napi::Buffer<uint8_t>::Copy(
			env,
			reinterpret_cast<const uint8_t*>(indexBlob.data()),
			indexBlob.size()
		)
	);
	syncAction.Set("indexString", Napi::String::New(env, indexBlob));

	if (encodeValueAsWire) {
		std::string valueWire;
		if (!action.value().SerializeToString(&valueWire)) {
			Napi::Error::New(env, "failed to encode SyncActionValue").ThrowAsJavaScriptException();
			return false;
		}
		syncAction.Set(
			"value",
			Napi::Buffer<uint8_t>::Copy(
				env,
				reinterpret_cast<const uint8_t*>(valueWire.data()),
				valueWire.size()
			)
		);
	} else {
		syncAction.Set("value", proto_reflection::ProtoToJsObject(env, action.value()));
	}

	if (action.has_padding()) {
		const std::string& padding = action.padding();
		syncAction.Set(
			"padding",
			Napi::Buffer<uint8_t>::Copy(
				env,
				reinterpret_cast<const uint8_t*>(padding.data()),
				padding.size()
			)
		);
	}
	if (action.has_version()) {
		syncAction.Set("version", Napi::Number::New(env, static_cast<double>(action.version())));
	}

	out->Set("operation", Napi::Number::New(env, static_cast<double>(mutation.operation)));
	out->Set(
		"indexMac",
		Napi::Buffer<uint8_t>::Copy(env, mutation.indexMac.data(), mutation.indexMac.size())
	);
	out->Set("valueMac", Napi::Buffer<uint8_t>::Copy(env, valueMac, 32));
	out->Set("syncAction", syncAction);
	return true;
}

} // namespace

bool DecodeMutationToJs(
	const Napi::Env& env,
	const MutationInput& mutation,
	const std::unordered_map<std::string, DerivedMutationKey>& keyMap,
	bool validateMacs,
	bool encodeValueAsWire,
	Napi::Object* out
) {
#if !BAILEYS_NATIVE_HAS_WAPROTO_BUILD
	Napi::Error::New(env, "decodeSyncdMutationsFast requires native WAProto support").ThrowAsJavaScriptException();
	return false;
#elif !defined(_WIN32)
	Napi::Error::New(env, "decodeSyncdMutationsFast currently supports only Windows builds")
		.ThrowAsJavaScriptException();
	return false;
#else
	MutationDecodeContext context;
	if (!ResolveMutationContext(env, mutation, keyMap, &context)) {
		return false;
	}

	if (validateMacs && !VerifyContentMac(env, mutation, context)) {
		return false;
	}

	proto::SyncActionData action;
	if (!DecryptSyncAction(env, context, &action)) {
		return false;
	}

	if (validateMacs && !VerifyIndexMac(env, mutation, *context.keys, action.index())) {
		return false;
	}

	return BuildSyncActionObject(env, mutation, action, encodeValueAsWire, context.valueMac, out);
#endif
}

} // namespace baileys_native::appstate_internal
