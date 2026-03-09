#include "media_crypto.h"

#include "common.h"
#include "common/native_error_log.h"
#include "common/safe_copy.h"
#include "media/internal/cng_crypto.h"
#include "utils/secure_memory.h"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace baileys_native {

namespace {

class NativeMediaEncryptor : public Napi::ObjectWrap<NativeMediaEncryptor> {
public:
	static Napi::Function Init(Napi::Env env, Napi::Object exports) {
		Napi::Function fn = DefineClass(
			env,
			"NativeMediaEncryptor",
			{
				InstanceMethod("update", &NativeMediaEncryptor::Update),
				InstanceMethod("final", &NativeMediaEncryptor::Final)
			}
		);
		constructor_ = Napi::Persistent(fn);
		constructor_.SuppressDestruct();
		exports.Set("NativeMediaEncryptor", fn);
		return fn;
	}

	NativeMediaEncryptor(const Napi::CallbackInfo& info) : Napi::ObjectWrap<NativeMediaEncryptor>(info) {
#ifdef _WIN32
		Napi::Env env = info.Env();
		if (info.Length() < 3) {
			common::native_error_log::ThrowType(
				env,
				"media.encryptor",
				"constructor",
				"NativeMediaEncryptor(cipherKey, iv, macKey) requires 3 arguments"
			);
			return;
		}

		common::ByteView cipherKey;
		common::ByteView iv;
		common::ByteView macKey;
		if (!common::GetByteViewFromValue(env, info[0], "cipherKey", &cipherKey) ||
			!common::GetByteViewFromValue(env, info[1], "iv", &iv) ||
			!common::GetByteViewFromValue(env, info[2], "macKey", &macKey)) {
			return;
		}
		if (cipherKey.length != 32 || iv.length != 16 || macKey.length != 32) {
			common::native_error_log::ThrowRange(
				env,
				"media.encryptor",
				"constructor",
				"NativeMediaEncryptor expects 32-byte key, 16-byte iv, 32-byte macKey"
			);
			return;
		}

		std::string error;
		if (!aes_.Init(cipherKey.data, cipherKey.length, iv.data, iv.length, &error) ||
			!hmac_.InitHmacSha256(macKey.data, macKey.length, &error) ||
			!plainSha_.InitSha256(&error) ||
			!encSha_.InitSha256(&error) ||
			!hmac_.Update(iv.data, iv.length, &error)) {
			common::native_error_log::ThrowError(env, "media.encryptor", "constructor.init", error);
			return;
		}

		ready_ = true;
#else
		(void)info;
		info.Env().Undefined();
#endif
	}
	~NativeMediaEncryptor() override {
#ifdef _WIN32
		utils::SecureZeroVector(&pending_);
#endif
	}

private:
	static Napi::FunctionReference constructor_;

#ifdef _WIN32
	media_internal::CngAesCbcState aes_;
	media_internal::CngHashState hmac_;
	media_internal::CngHashState plainSha_;
	media_internal::CngHashState encSha_;
	std::vector<uint8_t> pending_;
	bool ready_ = false;
	bool finalized_ = false;
#endif

	Napi::Value Update(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
#ifdef _WIN32
		if (!ready_ || finalized_) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"media.encryptor",
				"update.state",
				"NativeMediaEncryptor is not ready"
			);
		}
		if (info.Length() < 1) {
			return common::native_error_log::ThrowTypeValue(
				env,
				"media.encryptor",
				"update.args",
				"update(chunk) requires chunk"
			);
		}

		common::ByteView chunk;
		if (!common::GetByteViewFromValue(env, info[0], "chunk", &chunk)) {
			return env.Null();
		}

		std::string error;
		if (!plainSha_.Update(chunk.data, chunk.length, &error)) {
			return common::native_error_log::ThrowErrorValue(env, "media.encryptor", "update.plain_sha", error);
		}

		if (chunk.length > 0) {
			if (!common::safe_copy::AppendBytes(&pending_, chunk.data, chunk.length)) {
				return common::native_error_log::ThrowErrorValue(
					env,
					"media.encryptor",
					"update.pending_append",
					"NativeMediaEncryptor pending buffer overflow"
				);
			}
		}

		const size_t aligned = pending_.size() - (pending_.size() % 16u);
		if (aligned == 0) {
			return Napi::Buffer<uint8_t>::New(env, 0);
		}

		std::vector<uint8_t> encrypted;
		if (!aes_.Crypt(true, pending_.data(), aligned, false, &encrypted, &error) ||
			!hmac_.Update(encrypted.data(), encrypted.size(), &error) ||
			!encSha_.Update(encrypted.data(), encrypted.size(), &error)) {
			return common::native_error_log::ThrowErrorValue(env, "media.encryptor", "update.encrypt", error);
		}

		if (!common::safe_copy::ShiftLeft(&pending_, aligned)) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"media.encryptor",
				"update.pending_shift",
				"NativeMediaEncryptor pending buffer shift failed"
			);
		}
		return media_internal::CopyToBuffer(env, std::move(encrypted));
#else
		return common::native_error_log::ThrowErrorValue(
			env,
			"media.encryptor",
			"update.platform",
			"NativeMediaEncryptor is not supported on this platform"
		);
#endif
	}

	Napi::Value Final(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
#ifdef _WIN32
		(void)info;
		if (!ready_ || finalized_) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"media.encryptor",
				"final.state",
				"NativeMediaEncryptor is not ready"
			);
		}

		std::string error;
		std::vector<uint8_t> finalChunk;
		if (!aes_.Crypt(true, pending_.data(), pending_.size(), true, &finalChunk, &error) ||
			!hmac_.Update(finalChunk.data(), finalChunk.size(), &error) ||
			!encSha_.Update(finalChunk.data(), finalChunk.size(), &error)) {
			return common::native_error_log::ThrowErrorValue(env, "media.encryptor", "final.encrypt", error);
		}
		utils::SecureZeroVector(&pending_);

		std::vector<uint8_t> hmacDigest;
		if (!hmac_.Final(&hmacDigest, &error)) {
			return common::native_error_log::ThrowErrorValue(env, "media.encryptor", "final.hmac", error);
		}
		std::vector<uint8_t> mac(hmacDigest.begin(), hmacDigest.begin() + 10);
		utils::SecureZeroVector(&hmacDigest);
		if (!encSha_.Update(mac.data(), mac.size(), &error)) {
			return common::native_error_log::ThrowErrorValue(env, "media.encryptor", "final.enc_sha", error);
		}

		std::vector<uint8_t> fileSha256;
		std::vector<uint8_t> fileEncSha256;
		if (!plainSha_.Final(&fileSha256, &error) || !encSha_.Final(&fileEncSha256, &error)) {
			return common::native_error_log::ThrowErrorValue(env, "media.encryptor", "final.digest", error);
		}

		finalized_ = true;
		Napi::Object out = Napi::Object::New(env);
		out.Set("finalChunk", media_internal::CopyToBuffer(env, std::move(finalChunk)));
		out.Set("mac", media_internal::CopyToBuffer(env, std::move(mac)));
		out.Set("fileSha256", media_internal::CopyToBuffer(env, std::move(fileSha256)));
		out.Set("fileEncSha256", media_internal::CopyToBuffer(env, std::move(fileEncSha256)));
		return out;
#else
		(void)info;
		return common::native_error_log::ThrowErrorValue(
			env,
			"media.encryptor",
			"final.platform",
			"NativeMediaEncryptor is not supported on this platform"
		);
#endif
	}
};

Napi::FunctionReference NativeMediaEncryptor::constructor_;

} // namespace

Napi::Function InitNativeMediaEncryptor(Napi::Env env, Napi::Object exports) {
	return NativeMediaEncryptor::Init(env, exports);
}

} // namespace baileys_native
