#include "media/internal/media_decrypt_pipeline_object.h"

#include "common.h"
#include "common/native_error_log.h"
#include "common/safe_copy.h"
#include "utils/secure_memory.h"

#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace baileys_native::media_internal {

NativeMediaDecryptPipeline::NativeMediaDecryptPipeline(const Napi::CallbackInfo& info)
	: Napi::ObjectWrap<NativeMediaDecryptPipeline>(info) {
#ifdef _WIN32
	Napi::Env env = info.Env();
	if (info.Length() < 2) {
		common::native_error_log::ThrowType(
			env,
			"media.decrypt_pipeline",
			"constructor",
			"NativeMediaDecryptPipeline(cipherKey, iv, [options]) requires 2+ arguments"
		);
		return;
	}

	common::ByteView cipherKey;
	common::ByteView iv;
	if (!common::GetByteViewFromValue(env, info[0], "cipherKey", &cipherKey) ||
		!common::GetByteViewFromValue(env, info[1], "iv", &iv)) {
		return;
	}
	if (cipherKey.length != 32 || iv.length != 16) {
		common::native_error_log::ThrowRange(
			env,
			"media.decrypt_pipeline",
			"constructor",
			"NativeMediaDecryptPipeline expects 32-byte key and 16-byte iv"
		);
		return;
	}

	cipherKey_.assign(cipherKey.data, cipherKey.data + cipherKey.length);
	std::memcpy(baseIv_.data(), iv.data, iv.length);

	if (info.Length() > 2 && info[2].IsObject()) {
		Napi::Object options = info[2].As<Napi::Object>();

		Napi::Value firstBlockValue = options.Get("firstBlockIsIV");
		if (!firstBlockValue.IsUndefined() && !firstBlockValue.IsNull()) {
			if (!firstBlockValue.IsBoolean()) {
				common::native_error_log::ThrowType(
					env,
					"media.decrypt_pipeline",
					"constructor.options.firstBlockIsIV",
					"options.firstBlockIsIV must be a boolean"
				);
				return;
			}
			firstBlockIsIv_ = firstBlockValue.As<Napi::Boolean>().Value();
		}

		Napi::Value autoPaddingValue = options.Get("autoPadding");
		if (!autoPaddingValue.IsUndefined() && !autoPaddingValue.IsNull()) {
			if (!autoPaddingValue.IsBoolean()) {
				common::native_error_log::ThrowType(
					env,
					"media.decrypt_pipeline",
					"constructor.options.autoPadding",
					"options.autoPadding must be a boolean"
				);
				return;
			}
			autoPadding_ = autoPaddingValue.As<Napi::Boolean>().Value();
		}

		Napi::Value startByteValue = options.Get("startByte");
		if (!startByteValue.IsUndefined() && !startByteValue.IsNull()) {
			uint32_t parsed = 0;
			if (!common::ReadUInt32FromValue(env, startByteValue, "options.startByte", &parsed)) {
				return;
			}
			hasStart_ = true;
			startByte_ = parsed;
		}

		Napi::Value endByteValue = options.Get("endByte");
		if (!endByteValue.IsUndefined() && !endByteValue.IsNull()) {
			uint32_t parsed = 0;
			if (!common::ReadUInt32FromValue(env, endByteValue, "options.endByte", &parsed)) {
				return;
			}
			hasEnd_ = true;
			endByte_ = parsed;
		}

		Napi::Value initialOffsetValue = options.Get("initialOffset");
		if (!initialOffsetValue.IsUndefined() && !initialOffsetValue.IsNull()) {
			uint32_t parsed = 0;
			if (!common::ReadUInt32FromValue(env, initialOffsetValue, "options.initialOffset", &parsed)) {
				return;
			}
			bytesSeen_ = parsed;
		}
	}

	if (!firstBlockIsIv_) {
		std::string error;
		if (!InitializeDecryptor(baseIv_.data(), &error)) {
			common::native_error_log::ThrowError(env, "media.decrypt_pipeline", "constructor.init", error);
			return;
		}
	}

	ready_ = true;
#else
	(void)info;
#endif
}

NativeMediaDecryptPipeline::~NativeMediaDecryptPipeline() {
#ifdef _WIN32
	utils::SecureZeroVector(&cipherKey_);
	utils::SecureZeroArray(&baseIv_);
	utils::SecureZeroVector(&pendingCipher_);
	utils::SecureZeroVector(&pendingPlain_);
#endif
}

#ifdef _WIN32
bool NativeMediaDecryptPipeline::InitializeDecryptor(const uint8_t* iv, std::string* error) {
	const bool initialized = aes_.Init(cipherKey_.data(), cipherKey_.size(), iv, kBlockSize, error);
	utils::SecureZeroVector(&cipherKey_);
	if (!initialized) {
		return false;
	}
	decryptInitialized_ = true;
	return true;
}

bool NativeMediaDecryptPipeline::AppendBytes(std::vector<uint8_t>* out, const uint8_t* data, size_t length) {
	if (length == 0) {
		return true;
	}
	return common::safe_copy::AppendBytes(out, data, length);
}

bool NativeMediaDecryptPipeline::DecryptAlignedBytes(std::vector<uint8_t>* plainOut, std::string* error) {
	plainOut->clear();
	const size_t aligned = pendingCipher_.size() - (pendingCipher_.size() % kBlockSize);
	if (aligned == 0) {
		return true;
	}

	if (!aes_.Crypt(false, pendingCipher_.data(), aligned, false, plainOut, error)) {
		return false;
	}

	if (!common::safe_copy::ShiftLeft(&pendingCipher_, aligned)) {
		*error = "NativeMediaDecryptPipeline pending buffer shift failed";
		return false;
	}
	return true;
}

bool NativeMediaDecryptPipeline::ProcessPlain(
	const std::vector<uint8_t>& plain,
	bool finalCall,
	std::vector<uint8_t>* out,
	std::string* error
) {
	out->clear();
	if (!autoPadding_) {
		*out = plain;
		return true;
	}

	if (!plain.empty()) {
		if (!AppendBytes(&pendingPlain_, plain.data(), plain.size())) {
			*error = "NativeMediaDecryptPipeline pending plain buffer overflow";
			return false;
		}
	}

	if (!finalCall) {
		if (pendingPlain_.size() <= kBlockSize) {
			return true;
		}

		const size_t emitLength = pendingPlain_.size() - kBlockSize;
		out->assign(pendingPlain_.begin(), pendingPlain_.begin() + static_cast<std::ptrdiff_t>(emitLength));
		if (!common::safe_copy::ShiftLeft(&pendingPlain_, emitLength)) {
			*error = "NativeMediaDecryptPipeline pending plain buffer shift failed";
			return false;
		}
		return true;
	}

	if (pendingPlain_.empty() || (pendingPlain_.size() % kBlockSize) != 0) {
		*error = "invalid padded plaintext";
		return false;
	}

	const uint8_t pad = pendingPlain_.back();
	if (pad == 0 || pad > kBlockSize || static_cast<size_t>(pad) > pendingPlain_.size()) {
		*error = "invalid PKCS7 padding";
		return false;
	}

	const size_t beginPad = pendingPlain_.size() - static_cast<size_t>(pad);
	for (size_t i = beginPad; i < pendingPlain_.size(); ++i) {
		if (pendingPlain_[i] != pad) {
			*error = "invalid PKCS7 padding";
			return false;
		}
	}

	out->assign(pendingPlain_.begin(), pendingPlain_.begin() + static_cast<std::ptrdiff_t>(beginPad));
	utils::SecureZeroVector(&pendingPlain_);
	return true;
}

void NativeMediaDecryptPipeline::ApplyRange(const std::vector<uint8_t>& input, std::vector<uint8_t>* out) {
	out->clear();
	if (input.empty()) {
		return;
	}

	const uint64_t chunkStart = bytesSeen_;
	const uint64_t max = std::numeric_limits<uint64_t>::max();
	if (static_cast<uint64_t>(input.size()) > max - chunkStart) {
		bytesSeen_ = max;
		return;
	}
	const uint64_t chunkEnd = bytesSeen_ + input.size();
	bytesSeen_ = chunkEnd;

	if (!hasStart_ && !hasEnd_) {
		*out = input;
		return;
	}

	const uint64_t wantedStart = hasStart_ ? startByte_ : 0;
	const uint64_t wantedEnd = hasEnd_ ? endByte_ : std::numeric_limits<uint64_t>::max();
	if (chunkEnd <= wantedStart || chunkStart >= wantedEnd) {
		return;
	}

	const uint64_t localStart = wantedStart > chunkStart ? wantedStart - chunkStart : 0;
	const uint64_t localEnd = wantedEnd < chunkEnd ? wantedEnd - chunkStart : input.size();
	if (localEnd <= localStart) {
		return;
	}

	out->assign(
		input.begin() + static_cast<std::ptrdiff_t>(localStart),
		input.begin() + static_cast<std::ptrdiff_t>(localEnd)
	);
}
#endif

} // namespace baileys_native::media_internal
