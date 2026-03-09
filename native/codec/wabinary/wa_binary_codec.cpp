#include "wa_binary_codec.h"

#include "common.h"
#include "wa_binary_internal.h"

#include <memory>
#include <string>
#include <utility>

namespace baileys_native::wabinary {

std::unique_ptr<CodecConfig> g_codec;

bool ReadTagField(const Napi::Env& env, const Napi::Object& tagsObj, const char* key, uint32_t* out) {
	return common::ReadUInt32FromValue(env, tagsObj.Get(key), key, out);
}

bool InitCodecFromOptions(const Napi::Env& env, const Napi::Object& opts, CodecConfig* out) {
	Napi::Value tagsVal = opts.Get("TAGS");
	Napi::Value singleVal = opts.Get("SINGLE_BYTE_TOKENS");
	Napi::Value doubleVal = opts.Get("DOUBLE_BYTE_TOKENS");

	if (!tagsVal.IsObject() || !singleVal.IsArray() || !doubleVal.IsArray()) {
		Napi::TypeError::New(env, "Invalid WABinary options object").ThrowAsJavaScriptException();
		return false;
	}

	Napi::Object tagsObj = tagsVal.As<Napi::Object>();
	if (!ReadTagField(env, tagsObj, "LIST_EMPTY", &out->tags.listEmpty)) return false;
	if (!ReadTagField(env, tagsObj, "DICTIONARY_0", &out->tags.dictionary0)) return false;
	if (!ReadTagField(env, tagsObj, "DICTIONARY_1", &out->tags.dictionary1)) return false;
	if (!ReadTagField(env, tagsObj, "DICTIONARY_2", &out->tags.dictionary2)) return false;
	if (!ReadTagField(env, tagsObj, "DICTIONARY_3", &out->tags.dictionary3)) return false;
	if (!ReadTagField(env, tagsObj, "INTEROP_JID", &out->tags.interopJid)) return false;
	if (!ReadTagField(env, tagsObj, "FB_JID", &out->tags.fbJid)) return false;
	if (!ReadTagField(env, tagsObj, "AD_JID", &out->tags.adJid)) return false;
	if (!ReadTagField(env, tagsObj, "LIST_8", &out->tags.list8)) return false;
	if (!ReadTagField(env, tagsObj, "LIST_16", &out->tags.list16)) return false;
	if (!ReadTagField(env, tagsObj, "JID_PAIR", &out->tags.jidPair)) return false;
	if (!ReadTagField(env, tagsObj, "HEX_8", &out->tags.hex8)) return false;
	if (!ReadTagField(env, tagsObj, "BINARY_8", &out->tags.binary8)) return false;
	if (!ReadTagField(env, tagsObj, "BINARY_20", &out->tags.binary20)) return false;
	if (!ReadTagField(env, tagsObj, "BINARY_32", &out->tags.binary32)) return false;
	if (!ReadTagField(env, tagsObj, "NIBBLE_8", &out->tags.nibble8)) return false;
	if (!ReadTagField(env, tagsObj, "PACKED_MAX", &out->tags.packedMax)) return false;

	Napi::Array singleArray = singleVal.As<Napi::Array>();
	const uint32_t singleLen = singleArray.Length();
	out->singleByteTokens.clear();
	out->singleByteTokens.reserve(singleLen);
	for (uint32_t i = 0; i < singleLen; ++i) {
		Napi::Value tokenVal = singleArray.Get(i);
		if (!tokenVal.IsString()) {
			Napi::TypeError::New(env, "SINGLE_BYTE_TOKENS must contain strings").ThrowAsJavaScriptException();
			return false;
		}
		out->singleByteTokens.push_back(tokenVal.As<Napi::String>().Utf8Value());
	}

	Napi::Array doubleArray = doubleVal.As<Napi::Array>();
	const uint32_t doubleLen = doubleArray.Length();
	out->doubleByteTokens.clear();
	out->doubleByteTokens.reserve(doubleLen);
	for (uint32_t i = 0; i < doubleLen; ++i) {
		Napi::Value dictVal = doubleArray.Get(i);
		if (!dictVal.IsArray()) {
			Napi::TypeError::New(env, "DOUBLE_BYTE_TOKENS must contain arrays").ThrowAsJavaScriptException();
			return false;
		}
		Napi::Array dict = dictVal.As<Napi::Array>();
		const uint32_t dictLen = dict.Length();
		std::vector<std::string> entries;
		entries.reserve(dictLen);
		for (uint32_t j = 0; j < dictLen; ++j) {
			Napi::Value tokenVal = dict.Get(j);
			if (!tokenVal.IsString()) {
				Napi::TypeError::New(env, "DOUBLE_BYTE_TOKENS entries must be strings").ThrowAsJavaScriptException();
				return false;
			}
			entries.push_back(tokenVal.As<Napi::String>().Utf8Value());
		}
		out->doubleByteTokens.push_back(std::move(entries));
	}

	out->tokenMap.clear();
	out->tokenMap.reserve(out->singleByteTokens.size() + (out->doubleByteTokens.size() * 256));
	for (size_t i = 0; i < out->singleByteTokens.size(); ++i) {
		out->tokenMap[out->singleByteTokens[i]] = TokenIndex{static_cast<uint16_t>(i), 0, false};
	}
	for (size_t d = 0; d < out->doubleByteTokens.size(); ++d) {
		const auto& dict = out->doubleByteTokens[d];
		for (size_t j = 0; j < dict.size(); ++j) {
			out->tokenMap[dict[j]] = TokenIndex{static_cast<uint16_t>(j), static_cast<uint16_t>(d), true};
		}
	}
	return true;
}

} // namespace baileys_native::wabinary

namespace baileys_native {

Napi::Value InitWABinaryCodec(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1 || !info[0].IsObject()) {
		Napi::TypeError::New(env, "initWABinaryCodec(options) requires options object").ThrowAsJavaScriptException();
		return env.Null();
	}

	auto codec = std::make_unique<wabinary::CodecConfig>();
	if (!wabinary::InitCodecFromOptions(env, info[0].As<Napi::Object>(), codec.get())) {
		return env.Null();
	}

	wabinary::g_codec = std::move(codec);
	return Napi::Boolean::New(env, true);
}

Napi::Value EncodeWABinaryNode(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!wabinary::g_codec) {
		Napi::Error::New(env, "WABinary codec is not initialized").ThrowAsJavaScriptException();
		return env.Null();
	}
	if (info.Length() < 1) {
		Napi::TypeError::New(env, "encodeWABinaryNode(node, [includePrefix]) requires node").ThrowAsJavaScriptException();
		return env.Null();
	}

	const bool includePrefix = info.Length() > 1 && !info[1].IsUndefined() && info[1].ToBoolean();

	wabinary::Writer writer;
	if (includePrefix) {
		writer.PushByte(0);
	}

	std::string err;
	if (!wabinary::EncodeNode(env, &writer, *wabinary::g_codec, info[0], &err)) {
		if (env.IsExceptionPending()) {
			return env.Null();
		}
		Napi::Error::New(env, err.empty() ? "Failed to encode node" : err).ThrowAsJavaScriptException();
		return env.Null();
	}

	return common::MoveVectorToBuffer(env, writer.TakeData());
}

Napi::Value DecodeWABinaryNode(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!wabinary::g_codec) {
		Napi::Error::New(env, "WABinary codec is not initialized").ThrowAsJavaScriptException();
		return env.Null();
	}
	if (info.Length() < 1) {
		Napi::TypeError::New(env, "decodeWABinaryNode(buffer, [startIndex]) requires buffer")
			.ThrowAsJavaScriptException();
		return env.Null();
	}

	common::ByteView bytes;
	if (!common::GetByteViewFromValue(env, info[0], "buffer", &bytes)) {
		return env.Null();
	}

	uint32_t startIndex = 0;
	if (info.Length() > 1 && !info[1].IsUndefined() && !info[1].IsNull()) {
		if (!common::ReadUInt32FromValue(env, info[1], "startIndex", &startIndex)) {
			return env.Null();
		}
	}
	if (startIndex > bytes.length) {
		Napi::RangeError::New(env, "startIndex out of range").ThrowAsJavaScriptException();
		return env.Null();
	}

	wabinary::Reader reader(bytes.data, bytes.length, startIndex);
	Napi::Value node;
	std::string err;
	if (!wabinary::DecodeNode(env, &reader, *wabinary::g_codec, &node, &err)) {
		Napi::Error::New(env, err.empty() ? "Failed to decode node" : err).ThrowAsJavaScriptException();
		return env.Null();
	}

	Napi::Object out = Napi::Object::New(env);
	out.Set("node", node);
	out.Set("nextIndex", Napi::Number::New(env, static_cast<double>(reader.Index())));
	return out;
}

} // namespace baileys_native
