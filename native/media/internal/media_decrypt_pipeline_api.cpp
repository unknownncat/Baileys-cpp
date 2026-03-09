#include "media/internal/media_decrypt_pipeline_object.h"

#include "common.h"
#include "common/native_error_log.h"
#include "common/safe_copy.h"
#include "utils/secure_memory.h"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace baileys_native::media_internal {

Napi::Function NativeMediaDecryptPipeline::Init(Napi::Env env, Napi::Object exports) {
	Napi::Function fn = DefineClass(
		env,
		"NativeMediaDecryptPipeline",
		{
			InstanceMethod("update", &NativeMediaDecryptPipeline::Update),
			InstanceMethod("final", &NativeMediaDecryptPipeline::Final)
		}
	);
	constructor_ = Napi::Persistent(fn);
	constructor_.SuppressDestruct();
	exports.Set("NativeMediaDecryptPipeline", fn);
	return fn;
}

Napi::Value NativeMediaDecryptPipeline::Update(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
#ifdef _WIN32
	if (!ready_ || finalized_) {
		return common::native_error_log::ThrowErrorValue(
			env,
			"media.decrypt_pipeline",
			"update.state",
			"NativeMediaDecryptPipeline is not ready"
		);
	}
	if (info.Length() < 1) {
		return common::native_error_log::ThrowTypeValue(
			env,
			"media.decrypt_pipeline",
			"update.args",
			"update(chunk) requires chunk"
		);
	}

	common::ByteView chunk;
	if (!common::GetByteViewFromValue(env, info[0], "chunk", &chunk)) {
		return env.Null();
	}

	if (chunk.length > 0) {
		if (!AppendBytes(&pendingCipher_, chunk.data, chunk.length)) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"media.decrypt_pipeline",
				"update.pending_append",
				"NativeMediaDecryptPipeline pending buffer overflow"
			);
		}
	}

	if (!decryptInitialized_ && firstBlockIsIv_) {
		if (pendingCipher_.size() < kBlockSize) {
			return Napi::Buffer<uint8_t>::New(env, 0);
		}

		std::string initError;
		if (!InitializeDecryptor(pendingCipher_.data(), &initError)) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"media.decrypt_pipeline",
				"update.init",
				initError
			);
		}

		if (!common::safe_copy::ShiftLeft(&pendingCipher_, kBlockSize)) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"media.decrypt_pipeline",
				"update.pending_shift",
				"NativeMediaDecryptPipeline pending buffer shift failed"
			);
		}
	}

	std::vector<uint8_t> plain;
	std::string decryptError;
	if (!DecryptAlignedBytes(&plain, &decryptError)) {
		return common::native_error_log::ThrowErrorValue(
			env,
			"media.decrypt_pipeline",
			"update.decrypt",
			decryptError
		);
	}

	std::vector<uint8_t> produced;
	if (!ProcessPlain(plain, false, &produced, &decryptError)) {
		return common::native_error_log::ThrowErrorValue(
			env,
			"media.decrypt_pipeline",
			"update.process_plain",
			decryptError
		);
	}

	std::vector<uint8_t> filtered;
	ApplyRange(produced, &filtered);
	return common::MoveVectorToBuffer(env, std::move(filtered));
#else
	(void)info;
	return common::native_error_log::ThrowErrorValue(
		env,
		"media.decrypt_pipeline",
		"update.platform",
		"NativeMediaDecryptPipeline is not supported on this platform"
	);
#endif
}

Napi::Value NativeMediaDecryptPipeline::Final(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
#ifdef _WIN32
	(void)info;
	if (!ready_ || finalized_) {
		return common::native_error_log::ThrowErrorValue(
			env,
			"media.decrypt_pipeline",
			"final.state",
			"NativeMediaDecryptPipeline is not ready"
		);
	}
	finalized_ = true;

	if (!decryptInitialized_) {
		return common::native_error_log::ThrowErrorValue(
			env,
			"media.decrypt_pipeline",
			"final.init_state",
			"failed to initialize media decipher"
		);
	}
	if ((pendingCipher_.size() % kBlockSize) != 0) {
		return common::native_error_log::ThrowErrorValue(
			env,
			"media.decrypt_pipeline",
			"final.alignment",
			"NativeMediaDecryptPipeline final chunk is not block-aligned"
		);
	}

	std::vector<uint8_t> plain;
	if (!pendingCipher_.empty()) {
		std::string decryptError;
		if (!aes_.Crypt(false, pendingCipher_.data(), pendingCipher_.size(), false, &plain, &decryptError)) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"media.decrypt_pipeline",
				"final.decrypt",
				decryptError
			);
		}
	}
	utils::SecureZeroVector(&pendingCipher_);

	std::vector<uint8_t> produced;
	std::string processError;
	if (!ProcessPlain(plain, true, &produced, &processError)) {
		return common::native_error_log::ThrowErrorValue(
			env,
			"media.decrypt_pipeline",
			"final.process_plain",
			processError
		);
	}

	std::vector<uint8_t> filtered;
	ApplyRange(produced, &filtered);
	return common::MoveVectorToBuffer(env, std::move(filtered));
#else
	(void)info;
	return common::native_error_log::ThrowErrorValue(
		env,
		"media.decrypt_pipeline",
		"final.platform",
		"NativeMediaDecryptPipeline is not supported on this platform"
	);
#endif
}

Napi::FunctionReference NativeMediaDecryptPipeline::constructor_;

} // namespace baileys_native::media_internal
