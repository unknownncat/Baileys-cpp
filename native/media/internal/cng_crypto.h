#pragma once

#include <napi.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <bcrypt.h>
#endif

namespace baileys_native::media_internal {

Napi::Buffer<uint8_t> CopyToBuffer(const Napi::Env& env, const std::vector<uint8_t>& bytes);
Napi::Buffer<uint8_t> CopyToBuffer(const Napi::Env& env, std::vector<uint8_t>&& bytes);

#ifdef _WIN32

std::string NtStatusToString(NTSTATUS status);

class CngAesCbcState {
public:
	CngAesCbcState() = default;
	~CngAesCbcState();

	CngAesCbcState(const CngAesCbcState&) = delete;
	CngAesCbcState& operator=(const CngAesCbcState&) = delete;

	void Reset();
	bool Init(const uint8_t* key, size_t keyLen, const uint8_t* iv, size_t ivLen, std::string* error);
	bool Crypt(
		bool encrypt,
		const uint8_t* input,
		size_t inputLen,
		bool finalWithPadding,
		std::vector<uint8_t>* out,
		std::string* error
	);

private:
	BCRYPT_ALG_HANDLE alg_ = nullptr;
	BCRYPT_KEY_HANDLE key_ = nullptr;
	std::vector<uint8_t> keyObject_;
	std::vector<uint8_t> iv_;
};

class CngHashState {
public:
	CngHashState() = default;
	~CngHashState();

	CngHashState(const CngHashState&) = delete;
	CngHashState& operator=(const CngHashState&) = delete;

	void Reset();
	bool InitSha256(std::string* error);
	bool InitHmacSha256(const uint8_t* key, size_t keyLen, std::string* error);
	bool InitHmacSha512(const uint8_t* key, size_t keyLen, std::string* error);
	bool Update(const uint8_t* data, size_t length, std::string* error);
	bool Final(std::vector<uint8_t>* out, std::string* error);

private:
	bool InitInternal(
		LPCWSTR algorithm,
		const uint8_t* key,
		size_t keyLen,
		bool hmac,
		std::string* error
	);

	BCRYPT_ALG_HANDLE alg_ = nullptr;
	BCRYPT_HASH_HANDLE hash_ = nullptr;
	std::vector<uint8_t> hashObject_;
	ULONG digestLength_ = 0;
};

#endif

} // namespace baileys_native::media_internal
