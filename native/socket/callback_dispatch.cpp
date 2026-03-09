#include "callback_dispatch.h"

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

std::string GetFirstChildTag(const Napi::Value& contentValue) {
	if (!contentValue.IsArray()) {
		return std::string();
	}

	const Napi::Array content = contentValue.As<Napi::Array>();
	if (content.Length() == 0) {
		return std::string();
	}

	const Napi::Value firstChild = content.Get(static_cast<uint32_t>(0));
	if (!firstChild.IsObject()) {
		return std::string();
	}

	std::string childTag;
	ReadStringLike(firstChild.As<Napi::Object>().Get("tag"), &childTag);
	return childTag;
}

void AppendEvent(std::vector<std::string>* out, const std::string& prefix, const std::string& suffix) {
	std::string key;
	key.reserve(prefix.size() + suffix.size());
	key.append(prefix);
	key.append(suffix);
	out->push_back(std::move(key));
}

} // namespace

Napi::Value BuildSocketCallbackEventKeys(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1 || !info[0].IsObject()) {
		return Napi::Array::New(env, 0);
	}

	const Napi::Object frame = info[0].As<Napi::Object>();
	std::string callbackPrefix = "CB:";
	if (info.Length() > 1 && info[1].IsString()) {
		callbackPrefix = info[1].As<Napi::String>().Utf8Value();
	}

	std::string l0;
	if (!ReadStringLike(frame.Get("tag"), &l0) || l0.empty()) {
		return Napi::Array::New(env, 0);
	}

	const std::string l2 = GetFirstChildTag(frame.Get("content"));
	std::vector<std::string> eventKeys;

	const Napi::Value attrsValue = frame.Get("attrs");
	if (attrsValue.IsObject()) {
		const Napi::Object attrs = attrsValue.As<Napi::Object>();
		const Napi::Array attrKeys = attrs.GetPropertyNames();
		eventKeys.reserve(static_cast<size_t>(attrKeys.Length()) * 3u + 2u);

		for (uint32_t i = 0; i < attrKeys.Length(); ++i) {
			const std::string key = attrKeys.Get(i).ToString().Utf8Value();
			if (key.empty()) {
				continue;
			}

			std::string value;
			if (!ReadStringLike(attrs.Get(key), &value)) {
				continue;
			}

			std::string s1;
			s1.reserve(l0.size() + key.size() + value.size() + l2.size() + 3u);
			s1.append(l0);
			s1.push_back(',');
			s1.append(key);
			s1.push_back(':');
			s1.append(value);
			s1.push_back(',');
			s1.append(l2);
			AppendEvent(&eventKeys, callbackPrefix, s1);

			std::string s2;
			s2.reserve(l0.size() + key.size() + value.size() + 2u);
			s2.append(l0);
			s2.push_back(',');
			s2.append(key);
			s2.push_back(':');
			s2.append(value);
			AppendEvent(&eventKeys, callbackPrefix, s2);

			std::string s3;
			s3.reserve(l0.size() + key.size() + 1u);
			s3.append(l0);
			s3.push_back(',');
			s3.append(key);
			AppendEvent(&eventKeys, callbackPrefix, s3);
		}
	} else {
		eventKeys.reserve(2);
	}

	std::string s4;
	s4.reserve(l0.size() + l2.size() + 2u);
	s4.append(l0);
	s4.append(",,");
	s4.append(l2);
	AppendEvent(&eventKeys, callbackPrefix, s4);

	AppendEvent(&eventKeys, callbackPrefix, l0);

	Napi::Array out = Napi::Array::New(env, static_cast<uint32_t>(eventKeys.size()));
	for (uint32_t i = 0; i < static_cast<uint32_t>(eventKeys.size()); ++i) {
		out.Set(i, Napi::String::New(env, eventKeys[i]));
	}

	return out;
}

Napi::Value EmitSocketCallbackEvents(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsObject()) {
		Napi::TypeError::New(env, "emitSocketCallbackEvents(emitter, frame, [callbackPrefix], [tagPrefix]) requires emitter and frame objects")
			.ThrowAsJavaScriptException();
		return env.Null();
	}

	const Napi::Object emitter = info[0].As<Napi::Object>();
	const Napi::Object frame = info[1].As<Napi::Object>();

	Napi::Value emitValue = emitter.Get("emit");
	if (!emitValue.IsFunction()) {
		Napi::TypeError::New(env, "emitSocketCallbackEvents expected emitter.emit function").ThrowAsJavaScriptException();
		return env.Null();
	}
	Napi::Function emitFn = emitValue.As<Napi::Function>();

	std::string callbackPrefix = "CB:";
	if (info.Length() > 2 && info[2].IsString()) {
		callbackPrefix = info[2].As<Napi::String>().Utf8Value();
	}

	std::string tagPrefix = "TAG:";
	if (info.Length() > 3 && info[3].IsString()) {
		tagPrefix = info[3].As<Napi::String>().Utf8Value();
	}

	std::string l0;
	if (!ReadStringLike(frame.Get("tag"), &l0) || l0.empty()) {
		return Napi::Boolean::New(env, false);
	}

	bool anyTriggered = false;
	auto emitEvent = [&](const std::string& key) {
		Napi::Value emitted = emitFn.Call(emitter, {Napi::String::New(env, key), frame});
		if (env.IsExceptionPending()) {
			return;
		}
		if (emitted.ToBoolean().Value()) {
			anyTriggered = true;
		}
	};

	const Napi::Value attrsValue = frame.Get("attrs");
	if (attrsValue.IsObject()) {
		const Napi::Object attrs = attrsValue.As<Napi::Object>();
		std::string msgId;
		if (ReadStringLike(attrs.Get("id"), &msgId) && !msgId.empty()) {
			std::string msgEvent;
			msgEvent.reserve(tagPrefix.size() + msgId.size());
			msgEvent.append(tagPrefix);
			msgEvent.append(msgId);
			emitEvent(msgEvent);
			if (env.IsExceptionPending()) {
				return env.Null();
			}
		}
	}

	const std::string l2 = GetFirstChildTag(frame.Get("content"));
	if (attrsValue.IsObject()) {
		const Napi::Object attrs = attrsValue.As<Napi::Object>();
		const Napi::Array attrKeys = attrs.GetPropertyNames();

		for (uint32_t i = 0; i < attrKeys.Length(); ++i) {
			const std::string key = attrKeys.Get(i).ToString().Utf8Value();
			if (key.empty()) {
				continue;
			}

			std::string value;
			if (!ReadStringLike(attrs.Get(key), &value)) {
				continue;
			}

			std::string s1;
			s1.reserve(l0.size() + key.size() + value.size() + l2.size() + 3u);
			s1.append(l0);
			s1.push_back(',');
			s1.append(key);
			s1.push_back(':');
			s1.append(value);
			s1.push_back(',');
			s1.append(l2);

			std::string e1;
			e1.reserve(callbackPrefix.size() + s1.size());
			e1.append(callbackPrefix);
			e1.append(s1);
			emitEvent(e1);
			if (env.IsExceptionPending()) {
				return env.Null();
			}

			std::string s2;
			s2.reserve(l0.size() + key.size() + value.size() + 2u);
			s2.append(l0);
			s2.push_back(',');
			s2.append(key);
			s2.push_back(':');
			s2.append(value);

			std::string e2;
			e2.reserve(callbackPrefix.size() + s2.size());
			e2.append(callbackPrefix);
			e2.append(s2);
			emitEvent(e2);
			if (env.IsExceptionPending()) {
				return env.Null();
			}

			std::string s3;
			s3.reserve(l0.size() + key.size() + 1u);
			s3.append(l0);
			s3.push_back(',');
			s3.append(key);

			std::string e3;
			e3.reserve(callbackPrefix.size() + s3.size());
			e3.append(callbackPrefix);
			e3.append(s3);
			emitEvent(e3);
			if (env.IsExceptionPending()) {
				return env.Null();
			}
		}
	}

	std::string s4;
	s4.reserve(l0.size() + l2.size() + 2u);
	s4.append(l0);
	s4.append(",,");
	s4.append(l2);

	std::string e4;
	e4.reserve(callbackPrefix.size() + s4.size());
	e4.append(callbackPrefix);
	e4.append(s4);
	emitEvent(e4);
	if (env.IsExceptionPending()) {
		return env.Null();
	}

	std::string e5;
	e5.reserve(callbackPrefix.size() + l0.size());
	e5.append(callbackPrefix);
	e5.append(l0);
	emitEvent(e5);
	if (env.IsExceptionPending()) {
		return env.Null();
	}

	return Napi::Boolean::New(env, anyTriggered);
}

} // namespace baileys_native
