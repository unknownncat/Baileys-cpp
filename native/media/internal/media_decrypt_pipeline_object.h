#pragma once

#include <napi.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#include "media/internal/cng_crypto.h"
#endif

namespace baileys_native::media_internal {

class NativeMediaDecryptPipeline : public Napi::ObjectWrap<NativeMediaDecryptPipeline> {
public:
	static Napi::Function Init(Napi::Env env, Napi::Object exports);

	explicit NativeMediaDecryptPipeline(const Napi::CallbackInfo& info);
	~NativeMediaDecryptPipeline() override;

	static Napi::FunctionReference constructor_;

private:
	Napi::Value Update(const Napi::CallbackInfo& info);
	Napi::Value Final(const Napi::CallbackInfo& info);

#ifdef _WIN32
	static constexpr size_t kBlockSize = 16;

	media_internal::CngAesCbcState aes_;
	std::vector<uint8_t> cipherKey_;
	std::array<uint8_t, kBlockSize> baseIv_{};
	std::vector<uint8_t> pendingCipher_;
	std::vector<uint8_t> pendingPlain_;

	bool firstBlockIsIv_ = false;
	bool autoPadding_ = true;
	bool hasStart_ = false;
	bool hasEnd_ = false;
	uint64_t startByte_ = 0;
	uint64_t endByte_ = 0;
	uint64_t bytesSeen_ = 0;
	bool ready_ = false;
	bool decryptInitialized_ = false;
	bool finalized_ = false;

	bool InitializeDecryptor(const uint8_t* iv, std::string* error);
	bool AppendBytes(std::vector<uint8_t>* out, const uint8_t* data, size_t length);
	bool DecryptAlignedBytes(std::vector<uint8_t>* plainOut, std::string* error);
	bool ProcessPlain(const std::vector<uint8_t>& plain, bool finalCall, std::vector<uint8_t>* out, std::string* error);
	void ApplyRange(const std::vector<uint8_t>& input, std::vector<uint8_t>* out);
#endif
};

} // namespace baileys_native::media_internal
