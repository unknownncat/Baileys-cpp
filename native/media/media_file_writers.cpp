#include "media_crypto.h"

#include "common.h"
#include "common/native_error_log.h"
#include "common/safe_copy.h"
#include "media/internal/cng_crypto.h"
#include "utils/secure_memory.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace baileys_native {

namespace {

bool OpenBinaryTruncateFile(const std::string& path, std::ofstream* out) {
	if (out == nullptr) {
		return false;
	}

	out->open(path, std::ios::binary | std::ios::trunc);
	return static_cast<bool>(*out);
}

bool CloseFile(std::ofstream* out) {
	if (out == nullptr || !out->is_open()) {
		return true;
	}

	out->flush();
	out->close();
	return true;
}

void RemoveFileIfPresent(const std::string& path) {
	if (path.empty()) {
		return;
	}

	std::error_code ec;
	std::filesystem::remove(path, ec);
}

bool WriteBytes(std::ofstream* out, const uint8_t* data, size_t length) {
	if (length == 0) {
		return true;
	}
	if (out == nullptr || !out->is_open() || data == nullptr) {
		return false;
	}

	out->write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(length));
	return static_cast<bool>(*out);
}

#ifdef _WIN32

class NativeHashSpoolWriter : public Napi::ObjectWrap<NativeHashSpoolWriter> {
public:
	static Napi::Function Init(Napi::Env env, Napi::Object exports) {
		Napi::Function fn = DefineClass(
			env,
			"NativeHashSpoolWriter",
			{
				InstanceMethod("update", &NativeHashSpoolWriter::Update),
				InstanceMethod("final", &NativeHashSpoolWriter::Final),
				InstanceMethod("abort", &NativeHashSpoolWriter::Abort)
			}
		);
		constructor_ = Napi::Persistent(fn);
		constructor_.SuppressDestruct();
		exports.Set("NativeHashSpoolWriter", fn);
		return fn;
	}

	NativeHashSpoolWriter(const Napi::CallbackInfo& info) : Napi::ObjectWrap<NativeHashSpoolWriter>(info) {
		Napi::Env env = info.Env();
		if (info.Length() < 1) {
			common::native_error_log::ThrowType(
				env,
				"media.hash_spool_writer",
				"constructor.args",
				"NativeHashSpoolWriter(path) requires path"
			);
			return;
		}
		if (!common::ReadStringFromValue(env, info[0], "path", &path_)) {
			return;
		}
		if (!OpenBinaryTruncateFile(path_, &file_)) {
			common::native_error_log::ThrowError(
				env,
				"media.hash_spool_writer",
				"constructor.open",
				"failed opening output file"
			);
			return;
		}

		std::string error;
		if (!hash_.InitSha256(&error)) {
			CloseFile(&file_);
			RemoveFileIfPresent(path_);
			common::native_error_log::ThrowError(env, "media.hash_spool_writer", "constructor.hash", error);
			return;
		}

		ready_ = true;
	}

	~NativeHashSpoolWriter() override {
		CloseFile(&file_);
	}

private:
	static Napi::FunctionReference constructor_;

	std::string path_;
	std::ofstream file_;
	media_internal::CngHashState hash_;
	uint64_t fileLength_ = 0;
	bool ready_ = false;
	bool finalized_ = false;
	bool aborted_ = false;

	Napi::Value Update(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		if (!ready_ || finalized_ || aborted_) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"media.hash_spool_writer",
				"update.state",
				"NativeHashSpoolWriter is not ready"
			);
		}
		if (info.Length() < 1) {
			return common::native_error_log::ThrowTypeValue(
				env,
				"media.hash_spool_writer",
				"update.args",
				"update(chunk) requires chunk"
			);
		}

		common::ByteView chunk;
		if (!common::GetByteViewFromValue(env, info[0], "chunk", &chunk)) {
			return env.Null();
		}
		if (fileLength_ > std::numeric_limits<uint64_t>::max() - static_cast<uint64_t>(chunk.length)) {
			return common::native_error_log::ThrowRangeValue(
				env,
				"media.hash_spool_writer",
				"update.length",
				"NativeHashSpoolWriter length overflow"
			);
		}

		std::string error;
		if (!hash_.Update(chunk.data, chunk.length, &error)) {
			return common::native_error_log::ThrowErrorValue(env, "media.hash_spool_writer", "update.hash", error);
		}
		if (!WriteBytes(&file_, chunk.data, chunk.length)) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"media.hash_spool_writer",
				"update.write",
				"failed writing chunk to output file"
			);
		}

		fileLength_ += static_cast<uint64_t>(chunk.length);
		return env.Undefined();
	}

	Napi::Value Final(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		(void)info;
		if (!ready_ || finalized_ || aborted_) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"media.hash_spool_writer",
				"final.state",
				"NativeHashSpoolWriter is not ready"
			);
		}

		std::vector<uint8_t> digest;
		std::string error;
		if (!hash_.Final(&digest, &error)) {
			return common::native_error_log::ThrowErrorValue(env, "media.hash_spool_writer", "final.hash", error);
		}
		if (!CloseFile(&file_)) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"media.hash_spool_writer",
				"final.close",
				"failed closing output file"
			);
		}

		finalized_ = true;
		Napi::Object out = Napi::Object::New(env);
		out.Set("fileSha256", common::MoveVectorToBuffer(env, std::move(digest)));
		out.Set("fileLength", Napi::Number::New(env, static_cast<double>(fileLength_)));
		return out;
	}

	Napi::Value Abort(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		CloseFile(&file_);
		RemoveFileIfPresent(path_);
		aborted_ = true;
		ready_ = false;
		return env.Undefined();
	}
};

Napi::FunctionReference NativeHashSpoolWriter::constructor_;

class NativeMediaEncryptToFile : public Napi::ObjectWrap<NativeMediaEncryptToFile> {
public:
	static Napi::Function Init(Napi::Env env, Napi::Object exports) {
		Napi::Function fn = DefineClass(
			env,
			"NativeMediaEncryptToFile",
			{
				InstanceMethod("update", &NativeMediaEncryptToFile::Update),
				InstanceMethod("final", &NativeMediaEncryptToFile::Final),
				InstanceMethod("abort", &NativeMediaEncryptToFile::Abort)
			}
		);
		constructor_ = Napi::Persistent(fn);
		constructor_.SuppressDestruct();
		exports.Set("NativeMediaEncryptToFile", fn);
		return fn;
	}

	NativeMediaEncryptToFile(const Napi::CallbackInfo& info) : Napi::ObjectWrap<NativeMediaEncryptToFile>(info) {
		Napi::Env env = info.Env();
		if (info.Length() < 4) {
			common::native_error_log::ThrowType(
				env,
				"media.encrypt_to_file",
				"constructor.args",
				"NativeMediaEncryptToFile(cipherKey, iv, macKey, encPath, [originalPath]) requires 4 arguments"
			);
			return;
		}

		common::ByteView cipherKey;
		common::ByteView iv;
		common::ByteView macKey;
		if (!common::GetByteViewFromValue(env, info[0], "cipherKey", &cipherKey) ||
			!common::GetByteViewFromValue(env, info[1], "iv", &iv) ||
			!common::GetByteViewFromValue(env, info[2], "macKey", &macKey) ||
			!common::ReadStringFromValue(env, info[3], "encPath", &encPath_)) {
			return;
		}
		if (info.Length() > 4 && !info[4].IsUndefined() && !info[4].IsNull()) {
			if (!common::ReadStringFromValue(env, info[4], "originalPath", &originalPath_)) {
				return;
			}
		}
		if (cipherKey.length != 32 || iv.length != 16 || macKey.length != 32) {
			common::native_error_log::ThrowRange(
				env,
				"media.encrypt_to_file",
				"constructor.lengths",
				"NativeMediaEncryptToFile expects 32-byte key, 16-byte iv, 32-byte macKey"
			);
			return;
		}
		if (!OpenBinaryTruncateFile(encPath_, &encFile_)) {
			common::native_error_log::ThrowError(
				env,
				"media.encrypt_to_file",
				"constructor.enc_open",
				"failed opening encrypted output file"
			);
			return;
		}
		if (!originalPath_.empty() && !OpenBinaryTruncateFile(originalPath_, &originalFile_)) {
			CloseFile(&encFile_);
			RemoveFileIfPresent(encPath_);
			common::native_error_log::ThrowError(
				env,
				"media.encrypt_to_file",
				"constructor.original_open",
				"failed opening original output file"
			);
			return;
		}

		std::string error;
		if (!aes_.Init(cipherKey.data, cipherKey.length, iv.data, iv.length, &error) ||
			!hmac_.InitHmacSha256(macKey.data, macKey.length, &error) ||
			!plainSha_.InitSha256(&error) ||
			!encSha_.InitSha256(&error) ||
			!hmac_.Update(iv.data, iv.length, &error)) {
			CloseFile(&encFile_);
			CloseFile(&originalFile_);
			RemoveFileIfPresent(encPath_);
			RemoveFileIfPresent(originalPath_);
			common::native_error_log::ThrowError(env, "media.encrypt_to_file", "constructor.init", error);
			return;
		}

		ready_ = true;
	}

	~NativeMediaEncryptToFile() override {
		utils::SecureZeroVector(&pending_);
		CloseFile(&encFile_);
		CloseFile(&originalFile_);
	}

private:
	static Napi::FunctionReference constructor_;

	std::string encPath_;
	std::string originalPath_;
	std::ofstream encFile_;
	std::ofstream originalFile_;
	media_internal::CngAesCbcState aes_;
	media_internal::CngHashState hmac_;
	media_internal::CngHashState plainSha_;
	media_internal::CngHashState encSha_;
	std::vector<uint8_t> pending_;
	uint64_t fileLength_ = 0;
	bool ready_ = false;
	bool finalized_ = false;
	bool aborted_ = false;

	Napi::Value Update(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		if (!ready_ || finalized_ || aborted_) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"media.encrypt_to_file",
				"update.state",
				"NativeMediaEncryptToFile is not ready"
			);
		}
		if (info.Length() < 1) {
			return common::native_error_log::ThrowTypeValue(
				env,
				"media.encrypt_to_file",
				"update.args",
				"update(chunk) requires chunk"
			);
		}

		common::ByteView chunk;
		if (!common::GetByteViewFromValue(env, info[0], "chunk", &chunk)) {
			return env.Null();
		}
		if (fileLength_ > std::numeric_limits<uint64_t>::max() - static_cast<uint64_t>(chunk.length)) {
			return common::native_error_log::ThrowRangeValue(
				env,
				"media.encrypt_to_file",
				"update.length",
				"NativeMediaEncryptToFile length overflow"
			);
		}

		std::string error;
		if (!plainSha_.Update(chunk.data, chunk.length, &error)) {
			return common::native_error_log::ThrowErrorValue(env, "media.encrypt_to_file", "update.plain_sha", error);
		}
		if (originalFile_.is_open() && !WriteBytes(&originalFile_, chunk.data, chunk.length)) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"media.encrypt_to_file",
				"update.original_write",
				"failed writing original chunk to file"
			);
		}
		if (!common::safe_copy::AppendBytes(&pending_, chunk.data, chunk.length)) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"media.encrypt_to_file",
				"update.pending_append",
				"NativeMediaEncryptToFile pending buffer overflow"
			);
		}

		const size_t aligned = pending_.size() - (pending_.size() % 16u);
		if (aligned > 0) {
			std::vector<uint8_t> encrypted;
			if (!aes_.Crypt(true, pending_.data(), aligned, false, &encrypted, &error) ||
				!hmac_.Update(encrypted.data(), encrypted.size(), &error) ||
				!encSha_.Update(encrypted.data(), encrypted.size(), &error)) {
				return common::native_error_log::ThrowErrorValue(env, "media.encrypt_to_file", "update.encrypt", error);
			}
			if (!WriteBytes(&encFile_, encrypted.data(), encrypted.size())) {
				return common::native_error_log::ThrowErrorValue(
					env,
					"media.encrypt_to_file",
					"update.enc_write",
					"failed writing encrypted chunk to file"
				);
			}
			if (!common::safe_copy::ShiftLeft(&pending_, aligned)) {
				return common::native_error_log::ThrowErrorValue(
					env,
					"media.encrypt_to_file",
					"update.pending_shift",
					"NativeMediaEncryptToFile pending buffer shift failed"
				);
			}
		}

		fileLength_ += static_cast<uint64_t>(chunk.length);
		return env.Undefined();
	}

	Napi::Value Final(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		(void)info;
		if (!ready_ || finalized_ || aborted_) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"media.encrypt_to_file",
				"final.state",
				"NativeMediaEncryptToFile is not ready"
			);
		}

		std::string error;
		std::vector<uint8_t> finalChunk;
		if (!aes_.Crypt(true, pending_.data(), pending_.size(), true, &finalChunk, &error) ||
			!hmac_.Update(finalChunk.data(), finalChunk.size(), &error) ||
			!encSha_.Update(finalChunk.data(), finalChunk.size(), &error)) {
			return common::native_error_log::ThrowErrorValue(env, "media.encrypt_to_file", "final.encrypt", error);
		}
		if (!WriteBytes(&encFile_, finalChunk.data(), finalChunk.size())) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"media.encrypt_to_file",
				"final.chunk_write",
				"failed writing final encrypted chunk to file"
			);
		}
		utils::SecureZeroVector(&pending_);

		std::vector<uint8_t> hmacDigest;
		if (!hmac_.Final(&hmacDigest, &error)) {
			return common::native_error_log::ThrowErrorValue(env, "media.encrypt_to_file", "final.hmac", error);
		}
		if (hmacDigest.size() < 10u) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"media.encrypt_to_file",
				"final.hmac_size",
				"NativeMediaEncryptToFile invalid hmac digest size"
			);
		}
		std::vector<uint8_t> mac(hmacDigest.begin(), hmacDigest.begin() + 10);
		utils::SecureZeroVector(&hmacDigest);
		if (!encSha_.Update(mac.data(), mac.size(), &error)) {
			return common::native_error_log::ThrowErrorValue(env, "media.encrypt_to_file", "final.enc_sha", error);
		}
		if (!WriteBytes(&encFile_, mac.data(), mac.size())) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"media.encrypt_to_file",
				"final.mac_write",
				"failed writing media mac to file"
			);
		}

		std::vector<uint8_t> fileSha256;
		std::vector<uint8_t> fileEncSha256;
		if (!plainSha_.Final(&fileSha256, &error) || !encSha_.Final(&fileEncSha256, &error)) {
			return common::native_error_log::ThrowErrorValue(env, "media.encrypt_to_file", "final.digest", error);
		}
		if (!CloseFile(&encFile_) || !CloseFile(&originalFile_)) {
			return common::native_error_log::ThrowErrorValue(
				env,
				"media.encrypt_to_file",
				"final.close",
				"failed closing output files"
			);
		}

		finalized_ = true;
		Napi::Object out = Napi::Object::New(env);
		out.Set("mac", common::MoveVectorToBuffer(env, std::move(mac)));
		out.Set("fileSha256", common::MoveVectorToBuffer(env, std::move(fileSha256)));
		out.Set("fileEncSha256", common::MoveVectorToBuffer(env, std::move(fileEncSha256)));
		out.Set("fileLength", Napi::Number::New(env, static_cast<double>(fileLength_)));
		return out;
	}

	Napi::Value Abort(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		CloseFile(&encFile_);
		CloseFile(&originalFile_);
		utils::SecureZeroVector(&pending_);
		RemoveFileIfPresent(encPath_);
		RemoveFileIfPresent(originalPath_);
		aborted_ = true;
		ready_ = false;
		return env.Undefined();
	}
};

Napi::FunctionReference NativeMediaEncryptToFile::constructor_;

#endif

} // namespace

Napi::Function InitNativeHashSpoolWriter(Napi::Env env, Napi::Object exports) {
#ifdef _WIN32
	return NativeHashSpoolWriter::Init(env, exports);
#else
	(void)env;
	(void)exports;
	return Napi::Function();
#endif
}

Napi::Function InitNativeMediaEncryptToFile(Napi::Env env, Napi::Object exports) {
#ifdef _WIN32
	return NativeMediaEncryptToFile::Init(env, exports);
#else
	(void)env;
	(void)exports;
	return Napi::Function();
#endif
}

} // namespace baileys_native
