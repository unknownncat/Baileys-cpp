#include "message_key_codec.h"

#include <string>
#include <vector>

namespace baileys_native {

namespace {

bool ReadStringLike(const Napi::Value& value, std::string* out) {
	if (value.IsUndefined() || value.IsNull()) {
		return false;
	}

	if (!value.IsString() && !value.IsNumber() && !value.IsBoolean()) {
		return false;
	}

	*out = value.ToString().Utf8Value();
	return true;
}

bool BuildMessageKeyString(const Napi::Object& key, std::string* out) {
	std::string remoteJid;
	std::string id;
	if (!ReadStringLike(key.Get("remoteJid"), &remoteJid) || remoteJid.empty()) {
		return false;
	}
	if (!ReadStringLike(key.Get("id"), &id) || id.empty()) {
		return false;
	}

	const bool fromMe = key.Get("fromMe").ToBoolean().Value();
	out->clear();
	out->reserve(remoteJid.size() + id.size() + 3u);
	out->append(remoteJid);
	out->push_back(',');
	out->append(id);
	out->push_back(',');
	out->push_back(fromMe ? '1' : '0');
	return true;
}

bool BuildMessageKeyStringFromValue(const Napi::Value& value, std::string* out) {
	if (!value.IsObject()) {
		return false;
	}

	return BuildMessageKeyString(value.As<Napi::Object>(), out);
}

bool BuildMessageKeyStringFromMessage(const Napi::Value& value, std::string* out) {
	if (!value.IsObject()) {
		return false;
	}

	const Napi::Object message = value.As<Napi::Object>();
	const Napi::Value keyValue = message.Get("key");
	return BuildMessageKeyStringFromValue(keyValue, out);
}

bool BuildMessageKeyStringFromEntry(const Napi::Value& value, std::string* out) {
	if (!value.IsObject()) {
		return false;
	}

	const Napi::Object entry = value.As<Napi::Object>();
	const Napi::Value keyValue = entry.Get("key");
	return BuildMessageKeyStringFromValue(keyValue, out);
}

Napi::Value BuildMessageKeyStringArray(
	const Napi::Env& env,
	const Napi::Array& items,
	bool (*extractor)(const Napi::Value&, std::string*)
) {
	const uint32_t count = items.Length();
	Napi::Array out = Napi::Array::New(env, count);

	for (uint32_t i = 0; i < count; ++i) {
		std::string key;
		if (!extractor(items.Get(i), &key)) {
			out.Set(i, Napi::String::New(env, ""));
			continue;
		}

		out.Set(i, Napi::String::New(env, key));
	}

	return out;
}

} // namespace

Napi::Value StringifyMessageKeyFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1 || !info[0].IsObject()) {
		return Napi::String::New(env, "");
	}

	std::string key;
	if (!BuildMessageKeyString(info[0].As<Napi::Object>(), &key)) {
		return Napi::String::New(env, "");
	}

	return Napi::String::New(env, key);
}

Napi::Value StringifyMessageKeysFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1 || !info[0].IsArray()) {
		Napi::TypeError::New(env, "stringifyMessageKeysFast(keys) requires an array").ThrowAsJavaScriptException();
		return env.Null();
	}

	return BuildMessageKeyStringArray(env, info[0].As<Napi::Array>(), BuildMessageKeyStringFromValue);
}

Napi::Value StringifyMessageKeysFromMessagesFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1 || !info[0].IsArray()) {
		Napi::TypeError::New(env, "stringifyMessageKeysFromMessagesFast(messages) requires an array")
			.ThrowAsJavaScriptException();
		return env.Null();
	}

	return BuildMessageKeyStringArray(env, info[0].As<Napi::Array>(), BuildMessageKeyStringFromMessage);
}

Napi::Value StringifyMessageKeysFromEntriesFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1 || !info[0].IsArray()) {
		Napi::TypeError::New(env, "stringifyMessageKeysFromEntriesFast(entries) requires an array")
			.ThrowAsJavaScriptException();
		return env.Null();
	}

	return BuildMessageKeyStringArray(env, info[0].As<Napi::Array>(), BuildMessageKeyStringFromEntry);
}

} // namespace baileys_native
