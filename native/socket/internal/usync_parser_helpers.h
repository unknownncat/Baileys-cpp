#pragma once

#include <napi.h>

#include <string>

namespace baileys_native::socket_internal {

bool ReadStringLike(const Napi::Value& value, std::string* out);
bool HasChildWithTag(const Napi::Value& contentValue, const char* tag);
Napi::Object FindChildByTag(const Napi::Env& env, const Napi::Value& contentValue, const char* tag);
Napi::Value ParseDeviceInfo(const Napi::Env& env, const Napi::Object& node);
Napi::Value ParseStatusInfo(const Napi::Env& env, const Napi::Object& node);
Napi::Value ParseDisappearingModeInfo(const Napi::Env& env, const Napi::Object& node);
Napi::Value ParseBotProfileInfo(const Napi::Env& env, const Napi::Object& node);

template <typename Visitor>
inline void ForEachTaggedChild(const Napi::Value& contentValue, Visitor&& visitor) {
	if (!contentValue.IsArray()) {
		return;
	}

	const Napi::Array content = contentValue.As<Napi::Array>();
	for (uint32_t i = 0; i < content.Length(); ++i) {
		const Napi::Value childValue = content.Get(i);
		if (!childValue.IsObject()) {
			continue;
		}

		const Napi::Object childNode = childValue.As<Napi::Object>();
		std::string childTag;
		if (!ReadStringLike(childNode.Get("tag"), &childTag) || childTag.empty()) {
			continue;
		}

		visitor(childNode, childTag);
	}
}

} // namespace baileys_native::socket_internal
