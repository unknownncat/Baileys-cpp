#include "media/internal/cng_crypto.h"

#include "common.h"
#include "utils/secure_memory.h"

#include <cstdio>
#include <limits>
#include <utility>

namespace baileys_native::media_internal {

Napi::Buffer<uint8_t> CopyToBuffer(const Napi::Env& env, const std::vector<uint8_t>& bytes) {
	return bytes.empty()
		? Napi::Buffer<uint8_t>::New(env, 0)
		: Napi::Buffer<uint8_t>::Copy(env, bytes.data(), bytes.size());
}

Napi::Buffer<uint8_t> CopyToBuffer(const Napi::Env& env, std::vector<uint8_t>&& bytes) {
	return common::MoveVectorToBuffer(env, std::move(bytes));
}

#ifdef _WIN32

std::string NtStatusToString(NTSTATUS status) {
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "0x%08X", static_cast<unsigned int>(status));
	return std::string(buffer);
}

CngAesCbcState::~CngAesCbcState() {
	Reset();
}

void CngAesCbcState::Reset() {
	if (key_ != nullptr) {
		BCryptDestroyKey(key_);
		key_ = nullptr;
	}
	if (alg_ != nullptr) {
		BCryptCloseAlgorithmProvider(alg_, 0);
		alg_ = nullptr;
	}
	utils::SecureZeroVector(&keyObject_);
	utils::SecureZeroVector(&iv_);
}

bool CngAesCbcState::Init(const uint8_t* key, size_t keyLen, const uint8_t* iv, size_t ivLen, std::string* error) {
	Reset();
	if (keyLen == 0 || ivLen == 0) {
		*error = "invalid key/iv length";
		return false;
	}
	if (keyLen > static_cast<size_t>(std::numeric_limits<ULONG>::max()) ||
		ivLen > static_cast<size_t>(std::numeric_limits<ULONG>::max())) {
		*error = "key/iv length too large";
		return false;
	}

	NTSTATUS status = BCryptOpenAlgorithmProvider(&alg_, BCRYPT_AES_ALGORITHM, nullptr, 0);
	if (status < 0) {
		*error = "BCryptOpenAlgorithmProvider(aes) failed: " + NtStatusToString(status);
		return false;
	}

	const ULONG chainModeSize = static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_CBC) + 1u) * sizeof(wchar_t));
	status = BCryptSetProperty(
		alg_,
		BCRYPT_CHAINING_MODE,
		reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
		chainModeSize,
		0
	);
	if (status < 0) {
		*error = "BCryptSetProperty(chaining mode) failed: " + NtStatusToString(status);
		return false;
	}

	ULONG objectLength = 0;
	ULONG resultLength = 0;
	status = BCryptGetProperty(
		alg_,
		BCRYPT_OBJECT_LENGTH,
		reinterpret_cast<PUCHAR>(&objectLength),
		sizeof(objectLength),
		&resultLength,
		0
	);
	if (status < 0 || objectLength == 0) {
		*error = "BCryptGetProperty(object length) failed: " + NtStatusToString(status);
		return false;
	}

	keyObject_.resize(objectLength);
	status = BCryptGenerateSymmetricKey(
		alg_,
		&key_,
		keyObject_.data(),
		objectLength,
		const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(key)),
		static_cast<ULONG>(keyLen),
		0
	);
	if (status < 0) {
		*error = "BCryptGenerateSymmetricKey failed: " + NtStatusToString(status);
		return false;
	}

	iv_.assign(iv, iv + ivLen);
	return true;
}

bool CngAesCbcState::Crypt(
	bool encrypt,
	const uint8_t* input,
	size_t inputLen,
	bool finalWithPadding,
	std::vector<uint8_t>* out,
	std::string* error
) {
	out->clear();
	if (key_ == nullptr || iv_.empty()) {
		*error = "aes state is not initialized";
		return false;
	}
	if (inputLen > 0 && input == nullptr) {
		*error = "input is null";
		return false;
	}
	if (inputLen > static_cast<size_t>(std::numeric_limits<ULONG>::max())) {
		*error = "input too large";
		return false;
	}

	uint8_t zeroByte = 0;
	PUCHAR inputBytes = const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(inputLen == 0 ? &zeroByte : input));
	const ULONG inLen = static_cast<ULONG>(inputLen);
	const ULONG flags = finalWithPadding ? BCRYPT_BLOCK_PADDING : 0;
	ULONG outLen = 0;
	NTSTATUS status = encrypt
		? BCryptEncrypt(
				key_,
				inputBytes,
				inLen,
				nullptr,
				iv_.data(),
				static_cast<ULONG>(iv_.size()),
				nullptr,
				0,
				&outLen,
				flags
			)
		: BCryptDecrypt(
				key_,
				const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(input)),
				inLen,
				nullptr,
				iv_.data(),
				static_cast<ULONG>(iv_.size()),
				nullptr,
				0,
				&outLen,
				flags
			);
	if (status < 0) {
		*error = std::string(encrypt ? "BCryptEncrypt(size) failed: " : "BCryptDecrypt(size) failed: ") +
			NtStatusToString(status);
		return false;
	}

	out->resize(outLen);
	PUCHAR outputBytes = outLen == 0 ? &zeroByte : out->data();
	ULONG written = 0;
	status = encrypt
		? BCryptEncrypt(
				key_,
				inputBytes,
				inLen,
				nullptr,
				iv_.data(),
				static_cast<ULONG>(iv_.size()),
				outputBytes,
				outLen,
				&written,
				flags
			)
		: BCryptDecrypt(
				key_,
				inputBytes,
				inLen,
				nullptr,
				iv_.data(),
				static_cast<ULONG>(iv_.size()),
				outputBytes,
				outLen,
				&written,
				flags
			);
	if (status < 0) {
		*error = std::string(encrypt ? "BCryptEncrypt failed: " : "BCryptDecrypt failed: ") + NtStatusToString(status);
		return false;
	}
	if (written > outLen) {
		*error = "BCrypt output length mismatch";
		return false;
	}

	out->resize(written);
	return true;
}

CngHashState::~CngHashState() {
	Reset();
}

void CngHashState::Reset() {
	if (hash_ != nullptr) {
		BCryptDestroyHash(hash_);
		hash_ = nullptr;
	}
	if (alg_ != nullptr) {
		BCryptCloseAlgorithmProvider(alg_, 0);
		alg_ = nullptr;
	}
	utils::SecureZeroVector(&hashObject_);
	digestLength_ = 0;
}

bool CngHashState::InitSha256(std::string* error) {
	return InitInternal(BCRYPT_SHA256_ALGORITHM, nullptr, 0, false, error);
}

bool CngHashState::InitHmacSha256(const uint8_t* key, size_t keyLen, std::string* error) {
	return InitInternal(BCRYPT_SHA256_ALGORITHM, key, keyLen, true, error);
}

bool CngHashState::InitHmacSha512(const uint8_t* key, size_t keyLen, std::string* error) {
	return InitInternal(BCRYPT_SHA512_ALGORITHM, key, keyLen, true, error);
}

bool CngHashState::Update(const uint8_t* data, size_t length, std::string* error) {
	if (hash_ == nullptr) {
		*error = "hash state is not initialized";
		return false;
	}
	if (length == 0) {
		return true;
	}
	if (data == nullptr) {
		*error = "hash input is null";
		return false;
	}
	if (length > static_cast<size_t>(std::numeric_limits<ULONG>::max())) {
		*error = "hash input too large";
		return false;
	}

	const NTSTATUS status = BCryptHashData(
		hash_,
		const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(data)),
		static_cast<ULONG>(length),
		0
	);
	if (status < 0) {
		*error = "BCryptHashData failed: " + NtStatusToString(status);
		return false;
	}
	return true;
}

bool CngHashState::Final(std::vector<uint8_t>* out, std::string* error) {
	if (hash_ == nullptr || digestLength_ == 0) {
		*error = "hash state is not initialized";
		return false;
	}
	if (out == nullptr) {
		*error = "hash output is null";
		return false;
	}

	out->resize(digestLength_);
	const NTSTATUS status = BCryptFinishHash(hash_, out->data(), digestLength_, 0);
	if (status < 0) {
		*error = "BCryptFinishHash failed: " + NtStatusToString(status);
		return false;
	}
	return true;
}

bool CngHashState::InitInternal(
	LPCWSTR algorithm,
	const uint8_t* key,
	size_t keyLen,
	bool hmac,
	std::string* error
) {
	Reset();
	if (keyLen > static_cast<size_t>(std::numeric_limits<ULONG>::max())) {
		*error = "hmac key too large";
		return false;
	}

	const ULONG flags = hmac ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0;
	NTSTATUS status = BCryptOpenAlgorithmProvider(&alg_, algorithm, nullptr, flags);
	if (status < 0) {
		*error = "BCryptOpenAlgorithmProvider(hash) failed: " + NtStatusToString(status);
		return false;
	}

	ULONG objectLength = 0;
	ULONG resultLength = 0;
	status = BCryptGetProperty(
		alg_,
		BCRYPT_OBJECT_LENGTH,
		reinterpret_cast<PUCHAR>(&objectLength),
		sizeof(objectLength),
		&resultLength,
		0
	);
	if (status < 0 || objectLength == 0) {
		*error = "BCryptGetProperty(hash object length) failed: " + NtStatusToString(status);
		return false;
	}

	status = BCryptGetProperty(
		alg_,
		BCRYPT_HASH_LENGTH,
		reinterpret_cast<PUCHAR>(&digestLength_),
		sizeof(digestLength_),
		&resultLength,
		0
	);
	if (status < 0 || digestLength_ == 0) {
		*error = "BCryptGetProperty(hash length) failed: " + NtStatusToString(status);
		return false;
	}

	hashObject_.resize(objectLength);
	status = BCryptCreateHash(
		alg_,
		&hash_,
		hashObject_.data(),
		objectLength,
		const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(key)),
		static_cast<ULONG>(keyLen),
		0
	);
	if (status < 0) {
		*error = "BCryptCreateHash failed: " + NtStatusToString(status);
		return false;
	}

	return true;
}

#endif

} // namespace baileys_native::media_internal
