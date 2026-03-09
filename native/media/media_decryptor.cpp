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

class NativeMediaDecryptor : public Napi::ObjectWrap<NativeMediaDecryptor> {
public:
	static Napi::Function Init(Napi::Env env, Napi::Object exports) {
		Napi::Function fn = DefineClass(
			env,
			"NativeMediaDecryptor",
			{
				InstanceMethod("update", &NativeMediaDecryptor::Update),
				InstanceMethod("final", &NativeMediaDecryptor::Final)
			}
		);
		constructor_ = Napi::Persistent(fn);
		constructor_.SuppressDestruct();
		exports.Set("NativeMediaDecryptor", fn);
		return fn;
	}

	NativeMediaDecryptor(const Napi::CallbackInfo& info) : Napi::ObjectWrap<NativeMediaDecryptor>(info) {
#ifdef _WIN32
		Napi::Env env = info.Env();
		if (info.Length() < 2) {
			common::native_error_log::ThrowType(
				env,
				"media.decryptor",
				"constructor",
				"NativeMediaDecryptor(cipherKey, iv, [autoPadding]) requires 2+ arguments"
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
				"media.decryptor",
				"constructor",
				"NativeMediaDecryptor expects 32-byte key and 16-byte iv"
			);
			return;
		}

		if (info.Length() > 2 && info[2].IsBoolean()) {
			autoPadding_ = info[2].As<Napi::Boolean>().Value();
		}

		std::string error;
		if (!aes_.Init(cipherKey.data, cipherKey.length, iv.data, iv.length, &error)) {
			common::native_error_log::ThrowError(env, "media.decryptor", "constructor.init", error);
			return;
		}
		ready_ = true;
#else
		(void)info;
#endif
	}
	~NativeMediaDecryptor() override {
#ifdef _WIN32
		utils::SecureZeroVector(&pendingPlain_);
#endif
	}

private:
	static Napi::FunctionReference constructor_;

#ifdef _WIN32
	media_internal::CngAesCbcState aes_;
	std::vector<uint8_t> pendingPlain_;
	bool autoPadding_ = true;
	bool ready_ = false;
	bool finalized_ = false;
#endif

	Napi::Value Update(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
#ifdef _WIN32
		if (!ready_ || finalized_) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"media.decryptor",
				"update.state",
				"NativeMediaDecryptor is not ready"
			);
		}
		if (info.Length() < 1) {
			return common::native_error_log::ThrowTypeValue(
				env,
				"media.decryptor",
				"update.args",
				"update(chunk) requires chunk"
			);
		}

		common::ByteView chunk;
		if (!common::GetByteViewFromValue(env, info[0], "chunk", &chunk)) {
			return env.Null();
		}
		if ((chunk.length % 16u) != 0) {
			return common::native_error_log::ThrowRangeValue(
				env,
				"media.decryptor",
				"update.alignment",
				"NativeMediaDecryptor.update expects block-aligned chunk"
			);
		}

		std::string error;
		std::vector<uint8_t> plain;
		if (!aes_.Crypt(false, chunk.data, chunk.length, false, &plain, &error)) {
			return common::native_error_log::ThrowErrorValue(env, "media.decryptor", "update.decrypt", error);
		}

		if (!autoPadding_) {
			return media_internal::CopyToBuffer(env, std::move(plain));
		}

		if (!plain.empty()) {
			if (!common::safe_copy::AppendBytes(&pendingPlain_, plain.data(), plain.size())) {
				return common::native_error_log::ThrowErrorValue(
					env,
					"media.decryptor",
					"update.pending_append",
					"NativeMediaDecryptor pending buffer overflow"
				);
			}
		}

		if (pendingPlain_.size() <= 16u) {
			return Napi::Buffer<uint8_t>::New(env, 0);
		}

		const size_t outLen = pendingPlain_.size() - 16u;
		std::vector<uint8_t> out(pendingPlain_.begin(), pendingPlain_.begin() + static_cast<std::ptrdiff_t>(outLen));
		if (!common::safe_copy::ShiftLeft(&pendingPlain_, outLen)) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"media.decryptor",
				"update.pending_shift",
				"NativeMediaDecryptor pending buffer shift failed"
			);
		}
		return media_internal::CopyToBuffer(env, std::move(out));
#else
		return common::native_error_log::ThrowErrorValue(
			env,
			"media.decryptor",
			"update.platform",
			"NativeMediaDecryptor is not supported on this platform"
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
				"media.decryptor",
				"final.state",
				"NativeMediaDecryptor is not ready"
			);
		}
		finalized_ = true;

		if (!autoPadding_) {
			return Napi::Buffer<uint8_t>::New(env, 0);
		}
		if (pendingPlain_.empty() || (pendingPlain_.size() % 16u) != 0u) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"media.decryptor",
				"final.padding_state",
				"invalid padded plaintext"
			);
		}

		const uint8_t pad = pendingPlain_.back();
		if (pad == 0 || pad > 16u || static_cast<size_t>(pad) > pendingPlain_.size()) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"media.decryptor",
				"final.padding",
				"invalid PKCS7 padding"
			);
		}

		const size_t beginPad = pendingPlain_.size() - static_cast<size_t>(pad);
		for (size_t i = beginPad; i < pendingPlain_.size(); ++i) {
			if (pendingPlain_[i] != pad) {
				return common::native_error_log::ThrowErrorValue(
					env,
					"media.decryptor",
					"final.padding",
					"invalid PKCS7 padding"
				);
			}
		}

		std::vector<uint8_t> out(pendingPlain_.begin(), pendingPlain_.begin() + static_cast<std::ptrdiff_t>(beginPad));
		utils::SecureZeroVector(&pendingPlain_);
		return media_internal::CopyToBuffer(env, std::move(out));
#else
		(void)info;
		return common::native_error_log::ThrowErrorValue(
			env,
			"media.decryptor",
			"final.platform",
			"NativeMediaDecryptor is not supported on this platform"
		);
#endif
	}
};

Napi::FunctionReference NativeMediaDecryptor::constructor_;

} // namespace

Napi::Function InitNativeMediaDecryptor(Napi::Env env, Napi::Object exports) {
	return NativeMediaDecryptor::Init(env, exports);
}

} // namespace baileys_native
