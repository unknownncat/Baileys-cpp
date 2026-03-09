#include "message_key_store.h"

#include "common.h"
#include "internal/sender_key_codec_shared.h"

#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

namespace baileys_native {

namespace {

constexpr size_t kMaxMessageKeysDefault = 2000;

struct MessageKeyEntry {
	uint32_t iteration = 0;
	std::vector<uint8_t> seed;
};

Napi::Object BuildMessageEntryObject(const Napi::Env& env, const MessageKeyEntry& entry) {
	Napi::Object obj = Napi::Object::New(env);
	obj.Set("iteration", Napi::Number::New(env, entry.iteration));
	obj.Set("seed", Napi::Buffer<uint8_t>::Copy(env, entry.seed.data(), entry.seed.size()));
	return obj;
}

bool ParseMessageEntry(const Napi::Env& env, const Napi::Value& value, MessageKeyEntry* out) {
	if (!value.IsObject()) {
		Napi::TypeError::New(env, "senderMessageKeys entry must be an object").ThrowAsJavaScriptException();
		return false;
	}

	Napi::Object obj = value.As<Napi::Object>();
	if (!common::ReadUInt32FromValue(env, obj.Get("iteration"), "senderMessageKeys[].iteration", &out->iteration)) {
		return false;
	}

	if (!common::CopyBytesFromValue(env, obj.Get("seed"), "senderMessageKeys[].seed", &out->seed)) {
		return false;
	}
	return true;
}

class NativeMessageKeyStore : public Napi::ObjectWrap<NativeMessageKeyStore> {
public:
	static Napi::Function Init(Napi::Env env, Napi::Object exports) {
		Napi::Function func = DefineClass(
			env,
			"NativeMessageKeyStore",
			{
				InstanceMethod("has", &NativeMessageKeyStore::Has),
				InstanceMethod("add", &NativeMessageKeyStore::Add),
				InstanceMethod("remove", &NativeMessageKeyStore::Remove),
				InstanceMethod("toArray", &NativeMessageKeyStore::ToArray),
				InstanceMethod("encodeSenderKeyRecord", &NativeMessageKeyStore::EncodeSenderKeyRecord),
				InstanceMethod("size", &NativeMessageKeyStore::Size)
			}
		);

		constructor_ = Napi::Persistent(func);
		constructor_.SuppressDestruct();
		exports.Set("NativeMessageKeyStore", func);
		return func;
	}

	NativeMessageKeyStore(const Napi::CallbackInfo& info) : Napi::ObjectWrap<NativeMessageKeyStore>(info) {
		Napi::Env env = info.Env();
		maxKeys_ = kMaxMessageKeysDefault;

		if (info.Length() > 1 && info[1].IsNumber()) {
			uint32_t parsed = 0;
			if (!common::ReadUInt32FromValue(env, info[1], "maxKeys", &parsed)) {
				return;
			}
			if (parsed == 0) {
				Napi::RangeError::New(env, "maxKeys must be > 0").ThrowAsJavaScriptException();
				return;
			}
			maxKeys_ = parsed;
		}

		if (info.Length() > 0 && !info[0].IsUndefined() && !info[0].IsNull()) {
			if (!info[0].IsArray()) {
				Napi::TypeError::New(env, "NativeMessageKeyStore expects an array as first argument")
					.ThrowAsJavaScriptException();
				return;
			}
			Napi::Array items = info[0].As<Napi::Array>();
			const uint32_t length = items.Length();
			for (uint32_t i = 0; i < length; ++i) {
				MessageKeyEntry entry;
				if (!ParseMessageEntry(env, items.Get(i), &entry)) {
					return;
				}
				InsertOrReplace(entry.iteration, std::move(entry.seed));
			}
			TrimIfNeeded();
		}
	}

private:
	using EntryList = std::list<MessageKeyEntry>;
	using IndexMap = std::unordered_map<uint32_t, EntryList::iterator>;
	static Napi::FunctionReference constructor_;

	EntryList entries_;
	IndexMap index_;
	size_t maxKeys_ = kMaxMessageKeysDefault;

	void InsertOrReplace(uint32_t iteration, std::vector<uint8_t> seed) {
		auto existing = index_.find(iteration);
		if (existing != index_.end()) {
			entries_.erase(existing->second);
			index_.erase(existing);
		}

		entries_.push_back(MessageKeyEntry{iteration, std::move(seed)});
		auto it = entries_.end();
		--it;
		index_[iteration] = it;
	}

	void TrimIfNeeded() {
		while (entries_.size() > maxKeys_) {
			const auto& front = entries_.front();
			index_.erase(front.iteration);
			entries_.pop_front();
		}
	}

	Napi::Value Has(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		if (info.Length() < 1) {
			Napi::TypeError::New(env, "has(iteration) requires iteration").ThrowAsJavaScriptException();
			return env.Null();
		}

		uint32_t iteration = 0;
		if (!common::ReadUInt32FromValue(env, info[0], "iteration", &iteration)) {
			return env.Null();
		}

		return Napi::Boolean::New(env, index_.find(iteration) != index_.end());
	}

	Napi::Value Add(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		if (info.Length() < 2) {
			Napi::TypeError::New(env, "add(iteration, seed, [maxKeys]) requires iteration and seed")
				.ThrowAsJavaScriptException();
			return env.Null();
		}

		uint32_t iteration = 0;
		if (!common::ReadUInt32FromValue(env, info[0], "iteration", &iteration)) {
			return env.Null();
		}

		std::vector<uint8_t> seed;
		if (!common::CopyBytesFromValue(env, info[1], "seed", &seed)) {
			return env.Null();
		}

		if (info.Length() > 2 && info[2].IsNumber()) {
			uint32_t maxKeys = 0;
			if (!common::ReadUInt32FromValue(env, info[2], "maxKeys", &maxKeys)) {
				return env.Null();
			}
			if (maxKeys == 0) {
				Napi::RangeError::New(env, "maxKeys must be > 0").ThrowAsJavaScriptException();
				return env.Null();
			}
			maxKeys_ = maxKeys;
		}

		InsertOrReplace(iteration, std::move(seed));
		TrimIfNeeded();
		return env.Undefined();
	}

	Napi::Value Remove(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		if (info.Length() < 1) {
			Napi::TypeError::New(env, "remove(iteration) requires iteration").ThrowAsJavaScriptException();
			return env.Null();
		}

		uint32_t iteration = 0;
		if (!common::ReadUInt32FromValue(env, info[0], "iteration", &iteration)) {
			return env.Null();
		}

		auto it = index_.find(iteration);
		if (it == index_.end()) {
			return env.Null();
		}

		const MessageKeyEntry& entry = *(it->second);
		Napi::Object result = BuildMessageEntryObject(env, entry);
		entries_.erase(it->second);
		index_.erase(it);
		return result;
	}

	Napi::Value ToArray(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		Napi::Array out = Napi::Array::New(env, entries_.size());
		uint32_t i = 0;
		for (const auto& entry : entries_) {
			out.Set(i++, BuildMessageEntryObject(env, entry));
		}
		return out;
	}

	Napi::Value EncodeSenderKeyRecord(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		if (info.Length() < 4) {
			Napi::TypeError::New(
				env,
				"encodeSenderKeyRecord(senderKeyId, chainIteration, chainSeed, signingPublic, [signingPrivate]) requires senderKeyId, chainIteration, chainSeed and signingPublic"
			).ThrowAsJavaScriptException();
			return env.Null();
		}

		uint32_t senderKeyId = 0;
		if (!common::ReadUInt32FromValue(env, info[0], "senderKeyId", &senderKeyId)) {
			return env.Null();
		}

		uint32_t chainIteration = 0;
		if (!common::ReadUInt32FromValue(env, info[1], "chainIteration", &chainIteration)) {
			return env.Null();
		}

		std::vector<uint8_t> chainSeed;
		if (!common::CopyBytesFromValue(env, info[2], "chainSeed", &chainSeed)) {
			return env.Null();
		}

		std::vector<uint8_t> signingPublic;
		if (!common::CopyBytesFromValue(env, info[3], "signingPublic", &signingPublic)) {
			return env.Null();
		}

		std::vector<uint8_t> signingPrivate;
		if (
			info.Length() > 4 &&
			!common::CopyOptionalBytesFromValue(env, info[4], "signingPrivate", &signingPrivate)
		) {
			return env.Null();
		}

		if (!common::CheckFitsU16(env, chainSeed.size(), "senderChainKey.seed")) return env.Null();
		if (!common::CheckFitsU16(env, signingPublic.size(), "senderSigningKey.public")) return env.Null();
		if (!common::CheckFitsU16(env, signingPrivate.size(), "senderSigningKey.private")) return env.Null();
		if (!common::CheckFitsU16(env, entries_.size(), "senderMessageKeys.length")) return env.Null();

		std::vector<uint8_t> out;
		out.reserve(64 + chainSeed.size() + signingPublic.size() + signingPrivate.size() + (entries_.size() * 40));
		out.insert(out.end(), std::begin(sender_key_codec::kMagic), std::end(sender_key_codec::kMagic));
		out.push_back(sender_key_codec::kFormatVersion);
		sender_key_codec::WriteU16LE(&out, 1);
		sender_key_codec::WriteU32LE(&out, senderKeyId);
		sender_key_codec::WriteU32LE(&out, chainIteration);
		sender_key_codec::WriteU16LE(&out, static_cast<uint16_t>(chainSeed.size()));
		out.insert(out.end(), chainSeed.begin(), chainSeed.end());
		sender_key_codec::WriteU16LE(&out, static_cast<uint16_t>(signingPublic.size()));
		out.insert(out.end(), signingPublic.begin(), signingPublic.end());
		sender_key_codec::WriteU16LE(&out, static_cast<uint16_t>(signingPrivate.size()));
		out.insert(out.end(), signingPrivate.begin(), signingPrivate.end());
		sender_key_codec::WriteU16LE(&out, static_cast<uint16_t>(entries_.size()));

		for (const auto& entry : entries_) {
			if (!common::CheckFitsU16(env, entry.seed.size(), "senderMessageKeys[].seed")) return env.Null();
			sender_key_codec::WriteU32LE(&out, entry.iteration);
			sender_key_codec::WriteU16LE(&out, static_cast<uint16_t>(entry.seed.size()));
			out.insert(out.end(), entry.seed.begin(), entry.seed.end());
		}

		return common::MoveVectorToBuffer(env, std::move(out));
	}

	Napi::Value Size(const Napi::CallbackInfo& info) {
		return Napi::Number::New(info.Env(), static_cast<double>(entries_.size()));
	}
};

Napi::FunctionReference NativeMessageKeyStore::constructor_;

} // namespace

Napi::Function InitNativeMessageKeyStore(Napi::Env env, Napi::Object exports) {
	return NativeMessageKeyStore::Init(env, exports);
}

} // namespace baileys_native
