#include "json_batch_parser.h"

#include <string>
#include <vector>

namespace baileys_native {

Napi::Value ParseJsonStringArrayFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1 || !info[0].IsArray()) {
		Napi::TypeError::New(env, "parseJsonStringArrayFast(items) requires an array").ThrowAsJavaScriptException();
		return env.Null();
	}

	const Napi::Array items = info[0].As<Napi::Array>();
	const uint32_t count = items.Length();
	if (count == 0) {
		return Napi::Array::New(env, 0);
	}

	std::vector<std::string> rawItems;
	rawItems.reserve(count);

	size_t totalChars = 2; // '[' + ']'
	for (uint32_t i = 0; i < count; ++i) {
		const Napi::Value value = items.Get(i);
		if (!value.IsString()) {
			Napi::TypeError::New(env, "parseJsonStringArrayFast items must be strings").ThrowAsJavaScriptException();
			return env.Null();
		}

		std::string current = value.As<Napi::String>().Utf8Value();
		totalChars += current.size();
		if (i + 1 < count) {
			totalChars += 1; // comma
		}
		rawItems.push_back(std::move(current));
	}

	std::string jsonArray;
	jsonArray.reserve(totalChars);
	jsonArray.push_back('[');
	for (uint32_t i = 0; i < count; ++i) {
		jsonArray.append(rawItems[i]);
		if (i + 1 < count) {
			jsonArray.push_back(',');
		}
	}
	jsonArray.push_back(']');

	Napi::Object global = env.Global();
	Napi::Value jsonValue = global.Get("JSON");
	if (!jsonValue.IsObject()) {
		Napi::Error::New(env, "global JSON object is unavailable").ThrowAsJavaScriptException();
		return env.Null();
	}

	Napi::Object jsonObject = jsonValue.As<Napi::Object>();
	Napi::Value parseValue = jsonObject.Get("parse");
	if (!parseValue.IsFunction()) {
		Napi::Error::New(env, "JSON.parse is unavailable").ThrowAsJavaScriptException();
		return env.Null();
	}

	Napi::Function parseFn = parseValue.As<Napi::Function>();
	return parseFn.Call(jsonObject, {Napi::String::New(env, jsonArray)});
}

} // namespace baileys_native
