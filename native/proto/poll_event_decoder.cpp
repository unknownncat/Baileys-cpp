#include "poll_event_decoder.h"

#include "common.h"
#include "common/feature_gates.h"
#include "common/native_error_log.h"
#include "media/internal/cng_crypto.h"
#include "utils/secure_memory.h"

#if BAILEYS_NATIVE_HAS_WAPROTO_BUILD
#include "generated/wa_proto_adapter.h"
#include "protobuf_reflection_codec.h"
#endif

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include <cwchar>

#if BAILEYS_NATIVE_IS_WINDOWS_BUILD
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <bcrypt.h>
#endif

namespace baileys_native {

namespace {

constexpr size_t kGcmTagLength = 16;
constexpr std::array<uint8_t, 32> kZeroSalt = {};

struct SensitiveVectorsScope {
	std::vector<uint8_t>* first = nullptr;
	std::vector<uint8_t>* second = nullptr;
	std::vector<uint8_t>* third = nullptr;

	~SensitiveVectorsScope() {
		if (first != nullptr) {
			utils::SecureZeroVector(first);
		}
		if (second != nullptr) {
			utils::SecureZeroVector(second);
		}
		if (third != nullptr) {
			utils::SecureZeroVector(third);
		}
	}
};

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

bool AesGcmDecrypt(
	const uint8_t* encryptedPayload,
	size_t encryptedPayloadLength,
	const std::vector<uint8_t>& key,
	const uint8_t* iv,
	size_t ivLength,
	const uint8_t* aad,
	size_t aadLength,
	std::vector<uint8_t>* out,
	std::string* error
) {
	out->clear();
	if (encryptedPayloadLength <= kGcmTagLength) {
		*error = "encrypted payload too short";
		return false;
	}
	if (key.empty() || ivLength == 0) {
		*error = "invalid key/iv";
		return false;
	}
	if (key.size() > static_cast<size_t>(std::numeric_limits<ULONG>::max()) ||
		ivLength > static_cast<size_t>(std::numeric_limits<ULONG>::max()) ||
		aadLength > static_cast<size_t>(std::numeric_limits<ULONG>::max())) {
		*error = "input too large";
		return false;
	}

	const size_t cipherLength = encryptedPayloadLength - kGcmTagLength;
	const uint8_t* cipherBytes = encryptedPayload;
	const uint8_t* tagBytes = encryptedPayload + cipherLength;
	if (cipherLength > static_cast<size_t>(std::numeric_limits<ULONG>::max())) {
		*error = "ciphertext too large";
		return false;
	}

	BCRYPT_ALG_HANDLE algHandle = nullptr;
	BCRYPT_KEY_HANDLE keyHandle = nullptr;
	std::vector<uint8_t> keyObject;

	NTSTATUS status = BCryptOpenAlgorithmProvider(&algHandle, BCRYPT_AES_ALGORITHM, nullptr, 0);
	if (status < 0) {
		*error = "BCryptOpenAlgorithmProvider(aes) failed: " + media_internal::NtStatusToString(status);
		return false;
	}

	const ULONG chainModeSize = static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_GCM) + 1u) * sizeof(wchar_t));
	status = BCryptSetProperty(
		algHandle,
		BCRYPT_CHAINING_MODE,
		reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
		chainModeSize,
		0
	);
	if (status < 0) {
		*error = "BCryptSetProperty(gcm) failed: " + media_internal::NtStatusToString(status);
		BCryptCloseAlgorithmProvider(algHandle, 0);
		return false;
	}

	ULONG objectLength = 0;
	ULONG resultLength = 0;
	status = BCryptGetProperty(
		algHandle,
		BCRYPT_OBJECT_LENGTH,
		reinterpret_cast<PUCHAR>(&objectLength),
		sizeof(objectLength),
		&resultLength,
		0
	);
	if (status < 0 || objectLength == 0) {
		*error = "BCryptGetProperty(object length) failed: " + media_internal::NtStatusToString(status);
		BCryptCloseAlgorithmProvider(algHandle, 0);
		return false;
	}

	keyObject.resize(objectLength);
	status = BCryptGenerateSymmetricKey(
		algHandle,
		&keyHandle,
		keyObject.data(),
		objectLength,
		const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(key.data())),
		static_cast<ULONG>(key.size()),
		0
	);
	if (status < 0) {
		*error = "BCryptGenerateSymmetricKey failed: " + media_internal::NtStatusToString(status);
		BCryptCloseAlgorithmProvider(algHandle, 0);
		return false;
	}

	BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
	BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
	authInfo.pbNonce = const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(iv));
	authInfo.cbNonce = static_cast<ULONG>(ivLength);
	authInfo.pbAuthData = const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(aad));
	authInfo.cbAuthData = static_cast<ULONG>(aadLength);
	authInfo.pbTag = const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(tagBytes));
	authInfo.cbTag = static_cast<ULONG>(kGcmTagLength);

	ULONG plainLength = 0;
	status = BCryptDecrypt(
		keyHandle,
		const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(cipherBytes)),
		static_cast<ULONG>(cipherLength),
		&authInfo,
		nullptr,
		0,
		nullptr,
		0,
		&plainLength,
		0
	);
	if (status < 0) {
		*error = "BCryptDecrypt(size) failed: " + media_internal::NtStatusToString(status);
		BCryptDestroyKey(keyHandle);
		BCryptCloseAlgorithmProvider(algHandle, 0);
		return false;
	}

	out->resize(plainLength);
	ULONG written = 0;
	status = BCryptDecrypt(
		keyHandle,
		const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(cipherBytes)),
		static_cast<ULONG>(cipherLength),
		&authInfo,
		nullptr,
		0,
		out->data(),
		plainLength,
		&written,
		0
	);
	BCryptDestroyKey(keyHandle);
	BCryptCloseAlgorithmProvider(algHandle, 0);

	if (status < 0) {
		*error = "BCryptDecrypt failed: " + media_internal::NtStatusToString(status);
		return false;
	}

	out->resize(written);
	return true;
}
#endif

void AppendStringBytes(std::vector<uint8_t>* out, const std::string& value) {
	if (!value.empty()) {
		out->insert(out->end(), value.begin(), value.end());
	}
}

Napi::Value DecodePollLikeMessage(const Napi::CallbackInfo& info, bool pollVote) {
	Napi::Env env = info.Env();
	if (info.Length() < 6) {
		return common::native_error_log::ThrowTypeValue(
			env,
			"proto.poll_event",
			"decode.args",
			"decodePollLikeMessage(encPayload, encIv, msgId, creatorJid, responderJid, encKey) requires 6 arguments"
		);
	}

#if !BAILEYS_NATIVE_HAS_WAPROTO_BUILD
	return common::native_error_log::ThrowErrorValue(
		env,
		"proto.poll_event",
		"decode.unavailable",
		"native WAProto is unavailable in this build"
	);
#elif !BAILEYS_NATIVE_IS_WINDOWS_BUILD
	return common::native_error_log::ThrowErrorValue(
		env,
		"proto.poll_event",
		"decode.platform",
		"poll/event native decoder currently supports only Windows builds"
	);
#else
	common::ByteView encPayload;
	common::ByteView encIv;
	common::ByteView encKey;
	if (!common::GetByteViewFromValue(env, info[0], "encPayload", &encPayload) ||
		!common::GetByteViewFromValue(env, info[1], "encIv", &encIv) ||
		!common::GetByteViewFromValue(env, info[5], "encKey", &encKey)) {
		return env.Null();
	}

	std::string msgId;
	std::string creatorJid;
	std::string responderJid;
	if (!common::ReadStringFromValue(env, info[2], "msgId", &msgId) ||
		!common::ReadStringFromValue(env, info[3], "creatorJid", &creatorJid) ||
		!common::ReadStringFromValue(env, info[4], "responderJid", &responderJid)) {
		return env.Null();
	}

	const std::string suffix = pollVote ? "Poll Vote" : "Event Response";
	std::vector<uint8_t> sign;
	sign.reserve(msgId.size() + creatorJid.size() + responderJid.size() + suffix.size() + 1);
	AppendStringBytes(&sign, msgId);
	AppendStringBytes(&sign, creatorJid);
	AppendStringBytes(&sign, responderJid);
	AppendStringBytes(&sign, suffix);
	sign.push_back(1);

	std::vector<uint8_t> keyBuffer(encKey.data, encKey.data + encKey.length);
	std::vector<uint8_t> key0;
	std::vector<uint8_t> decKey;
	SensitiveVectorsScope sensitive{&keyBuffer, &key0, &decKey};
	std::string error;
	if (!ComputeHmacSha256(keyBuffer, kZeroSalt.data(), kZeroSalt.size(), &key0, &error)) {
		return common::native_error_log::ThrowErrorValue(
			env,
			"proto.poll_event",
			"decode.key0_hmac",
			"failed to compute poll/event key0 hmac: " + error
		);
	}

	if (!ComputeHmacSha256(key0, sign.data(), sign.size(), &decKey, &error)) {
		return common::native_error_log::ThrowErrorValue(
			env,
			"proto.poll_event",
			"decode.decrypt_key_hmac",
			"failed to compute poll/event decrypt key hmac: " + error
		);
	}

	std::string aadString = msgId;
	aadString.push_back('\0');
	aadString.append(responderJid);

	std::vector<uint8_t> decrypted;
	if (!AesGcmDecrypt(
			encPayload.data,
			encPayload.length,
			decKey,
			encIv.data,
			encIv.length,
			reinterpret_cast<const uint8_t*>(aadString.data()),
			aadString.size(),
			&decrypted,
			&error
		)) {
		return common::native_error_log::ThrowErrorValue(
			env,
			"proto.poll_event",
			"decode.decrypt",
			"failed to decrypt poll/event payload: " + error
		);
	}

	if (decrypted.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
		return common::native_error_log::ThrowRangeValue(
			env,
			"proto.poll_event",
			"decode.size",
			"decoded poll/event payload too large"
		);
	}

	if (pollVote) {
		proto::Message_PollVoteMessage message;
		if (!message.ParseFromArray(decrypted.data(), static_cast<int>(decrypted.size()))) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"proto.poll_event",
				"decode.poll_vote",
				"failed to decode poll vote message"
			);
		}
		return proto_reflection::ProtoToJsObject(env, message);
	}

	proto::Message_EventResponseMessage message;
	if (!message.ParseFromArray(decrypted.data(), static_cast<int>(decrypted.size()))) {
		return common::native_error_log::ThrowErrorValue(
			env,
			"proto.poll_event",
			"decode.event_response",
			"failed to decode event response message"
		);
	}
	return proto_reflection::ProtoToJsObject(env, message);
#endif
}

} // namespace

Napi::Value DecodePollVoteMessageFast(const Napi::CallbackInfo& info) {
	return DecodePollLikeMessage(info, true);
}

Napi::Value DecodeEventResponseMessageFast(const Napi::CallbackInfo& info) {
	return DecodePollLikeMessage(info, false);
}

} // namespace baileys_native
