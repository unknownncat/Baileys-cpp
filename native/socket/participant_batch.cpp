#include "participant_batch.h"

#include "common.h"

#include <string>
#include <utility>
#include <vector>

namespace baileys_native {

namespace {

bool ReadExtraAttrs(
	const Napi::Env& env,
	const Napi::Value& value,
	std::vector<std::pair<std::string, std::string>>* out
) {
	out->clear();
	if (value.IsUndefined() || value.IsNull()) {
		return true;
	}
	if (!value.IsObject()) {
		Napi::TypeError::New(env, "extraAttrs must be an object").ThrowAsJavaScriptException();
		return false;
	}

	Napi::Object obj = value.As<Napi::Object>();
	Napi::Array keys = obj.GetPropertyNames();
	const uint32_t len = keys.Length();
	out->reserve(len);
	for (uint32_t i = 0; i < len; ++i) {
		Napi::Value keyValue = keys.Get(i);
		if (!keyValue.IsString()) {
			continue;
		}
		std::string key = keyValue.As<Napi::String>().Utf8Value();
		Napi::Value attrVal = obj.Get(keyValue);
		if (attrVal.IsUndefined() || attrVal.IsNull()) {
			continue;
		}
		if (!attrVal.IsString() && !attrVal.IsNumber() && !attrVal.IsBoolean()) {
			Napi::TypeError::New(env, ("extraAttrs." + key + " must be string/number/boolean").c_str())
				.ThrowAsJavaScriptException();
			return false;
		}
		out->push_back({std::move(key), attrVal.ToString().Utf8Value()});
	}

	return true;
}

} // namespace

Napi::Value BuildParticipantNodesBatch(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1 || !info[0].IsArray()) {
		Napi::TypeError::New(env, "buildParticipantNodesBatch(encryptedItems, [extraAttrs]) expects encryptedItems array")
			.ThrowAsJavaScriptException();
		return env.Null();
	}

	Napi::Array encryptedItems = info[0].As<Napi::Array>();
	const uint32_t count = encryptedItems.Length();

	std::vector<std::pair<std::string, std::string>> extraAttrs;
	if (!ReadExtraAttrs(env, info.Length() > 1 ? info[1] : env.Undefined(), &extraAttrs)) {
		return env.Null();
	}

	bool shouldIncludeDeviceIdentity = false;
	Napi::Array nodes = Napi::Array::New(env, count);
	for (uint32_t i = 0; i < count; ++i) {
		const Napi::Value itemValue = encryptedItems.Get(i);
		if (!itemValue.IsObject()) {
			Napi::TypeError::New(env, "encryptedItems entries must be objects").ThrowAsJavaScriptException();
			return env.Null();
		}

		const Napi::Object item = itemValue.As<Napi::Object>();
		std::string jid;
		std::string type;
		common::ByteView ciphertext;
		if (!common::ReadStringFromValue(env, item.Get("jid"), "encryptedItems[].jid", &jid)) {
			return env.Null();
		}
		if (!common::ReadStringFromValue(env, item.Get("type"), "encryptedItems[].type", &type)) {
			return env.Null();
		}
		if (!common::GetByteViewFromValue(env, item.Get("ciphertext"), "encryptedItems[].ciphertext", &ciphertext)) {
			return env.Null();
		}

		if (type == "pkmsg") {
			shouldIncludeDeviceIdentity = true;
		}

		Napi::Object encAttrs = Napi::Object::New(env);
		encAttrs.Set("v", "2");
		encAttrs.Set("type", type);
		for (const auto& [key, value] : extraAttrs) {
			encAttrs.Set(key, value);
		}

		Napi::Object encNode = Napi::Object::New(env);
		encNode.Set("tag", "enc");
		encNode.Set("attrs", encAttrs);
		encNode.Set("content", Napi::Buffer<uint8_t>::Copy(env, ciphertext.data, ciphertext.length));

		Napi::Array toContent = Napi::Array::New(env, 1);
		toContent.Set(uint32_t{0}, encNode);

		Napi::Object toAttrs = Napi::Object::New(env);
		toAttrs.Set("jid", jid);

		Napi::Object toNode = Napi::Object::New(env);
		toNode.Set("tag", "to");
		toNode.Set("attrs", toAttrs);
		toNode.Set("content", toContent);
		nodes.Set(i, toNode);
	}

	Napi::Object out = Napi::Object::New(env);
	out.Set("nodes", nodes);
	out.Set("shouldIncludeDeviceIdentity", Napi::Boolean::New(env, shouldIncludeDeviceIdentity));
	return out;
}

} // namespace baileys_native
