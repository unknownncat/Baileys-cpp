#include "socket/internal/usync_parser_helpers.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

namespace baileys_native::socket_internal {

namespace {

double ParseNumberLikeJs(const std::string& value) {
	if (value.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}

	errno = 0;
	char* endPtr = nullptr;
	const double parsed = std::strtod(value.c_str(), &endPtr);
	if (endPtr == value.c_str()) {
		return std::numeric_limits<double>::quiet_NaN();
	}

	while (*endPtr != '\0') {
		if (*endPtr != ' ' && *endPtr != '\t' && *endPtr != '\n' && *endPtr != '\r' && *endPtr != '\f' &&
			*endPtr != '\v') {
			return std::numeric_limits<double>::quiet_NaN();
		}
		++endPtr;
	}

	if (errno == ERANGE) {
		return parsed > 0 ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity();
	}

	return parsed;
}

bool ReadNodeContentUtf8(const Napi::Env& env, const Napi::Object& node, std::string* out) {
	out->clear();
	const Napi::Value contentValue = node.Get("content");
	if (contentValue.IsString()) {
		*out = contentValue.As<Napi::String>().Utf8Value();
		return true;
	}

	if (contentValue.IsBuffer()) {
		const Napi::Buffer<uint8_t> buff = contentValue.As<Napi::Buffer<uint8_t>>();
		out->assign(reinterpret_cast<const char*>(buff.Data()), buff.Length());
		return true;
	}

	if (contentValue.IsTypedArray()) {
		const Napi::Uint8Array arr = contentValue.As<Napi::Uint8Array>();
		out->assign(reinterpret_cast<const char*>(arr.Data()), arr.ByteLength());
		return true;
	}

	if (contentValue.IsNull() || contentValue.IsUndefined()) {
		return true;
	}

	return false;
}

bool ReadChildText(const Napi::Env& env, const Napi::Object& node, const char* tag, std::string* out) {
	const Napi::Object child = FindChildByTag(env, node.Get("content"), tag);
	if (child.IsEmpty()) {
		out->clear();
		return false;
	}

	return ReadNodeContentUtf8(env, child, out);
}

} // namespace

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

bool HasChildWithTag(const Napi::Value& contentValue, const char* tag) {
	bool found = false;
	ForEachTaggedChild(contentValue, [&](const Napi::Object& /*unused*/, const std::string& childTag) {
		if (childTag == tag) {
			found = true;
		}
	});
	return found;
}

Napi::Object FindChildByTag(const Napi::Env& env, const Napi::Value& contentValue, const char* tag) {
	(void)env;
	Napi::Object result;
	ForEachTaggedChild(contentValue, [&](const Napi::Object& childNode, const std::string& childTag) {
		if (!result.IsEmpty()) {
			return;
		}
		if (childTag == tag) {
			result = childNode;
		}
	});
	if (!result.IsEmpty()) {
		return result;
	}
	return Napi::Object();
}

Napi::Value ParseDeviceInfo(const Napi::Env& env, const Napi::Object& node) {
	if (HasChildWithTag(node.Get("content"), "error")) {
		return env.Null();
	}

	const Napi::Object parsed = Napi::Object::New(env);
	const Napi::Array deviceListOut = Napi::Array::New(env);
	uint32_t deviceCount = 0;

	const Napi::Object deviceListNode = FindChildByTag(env, node.Get("content"), "device-list");
	if (!deviceListNode.IsEmpty()) {
		const Napi::Value deviceListContentValue = deviceListNode.Get("content");
		if (deviceListContentValue.IsArray()) {
			const Napi::Array deviceList = deviceListContentValue.As<Napi::Array>();
			for (uint32_t i = 0; i < deviceList.Length(); ++i) {
				const Napi::Value deviceValue = deviceList.Get(i);
				if (!deviceValue.IsObject()) {
					continue;
				}

				const Napi::Object deviceNode = deviceValue.As<Napi::Object>();
				std::string deviceTag;
				if (!ReadStringLike(deviceNode.Get("tag"), &deviceTag) || deviceTag != "device") {
					continue;
				}

				const Napi::Value attrsValue = deviceNode.Get("attrs");
				if (!attrsValue.IsObject()) {
					continue;
				}

				const Napi::Object attrs = attrsValue.As<Napi::Object>();
				std::string idRaw;
				std::string keyIndexRaw;
				std::string hostedRaw;
				ReadStringLike(attrs.Get("id"), &idRaw);
				ReadStringLike(attrs.Get("key-index"), &keyIndexRaw);
				ReadStringLike(attrs.Get("is_hosted"), &hostedRaw);

				Napi::Object parsedDevice = Napi::Object::New(env);
				parsedDevice.Set("id", Napi::Number::New(env, ParseNumberLikeJs(idRaw)));
				parsedDevice.Set("keyIndex", Napi::Number::New(env, ParseNumberLikeJs(keyIndexRaw)));
				parsedDevice.Set("isHosted", Napi::Boolean::New(env, hostedRaw == "true"));
				deviceListOut.Set(deviceCount++, parsedDevice);
			}
		}
	}

	parsed.Set("deviceList", deviceListOut);

	const Napi::Object keyIndexNode = FindChildByTag(env, node.Get("content"), "key-index-list");
	if (!keyIndexNode.IsEmpty()) {
		const Napi::Value attrsValue = keyIndexNode.Get("attrs");
		if (attrsValue.IsObject()) {
			const Napi::Object attrs = attrsValue.As<Napi::Object>();
			std::string tsRaw;
			std::string expectedTsRaw;
			ReadStringLike(attrs.Get("ts"), &tsRaw);
			ReadStringLike(attrs.Get("expected_ts"), &expectedTsRaw);

			Napi::Object keyIndex = Napi::Object::New(env);
			keyIndex.Set("timestamp", Napi::Number::New(env, ParseNumberLikeJs(tsRaw)));

			const Napi::Value contentValue = keyIndexNode.Get("content");
			if (contentValue.IsBuffer()) {
				const Napi::Buffer<uint8_t> buff = contentValue.As<Napi::Buffer<uint8_t>>();
				keyIndex.Set("signedKeyIndex", Napi::Buffer<uint8_t>::Copy(env, buff.Data(), buff.Length()));
			} else if (contentValue.IsTypedArray()) {
				const Napi::Uint8Array arr = contentValue.As<Napi::Uint8Array>();
				keyIndex.Set(
					"signedKeyIndex",
					Napi::Buffer<uint8_t>::Copy(env, arr.Data(), arr.ByteLength())
				);
			}

			if (!expectedTsRaw.empty()) {
				keyIndex.Set("expectedTimestamp", Napi::Number::New(env, ParseNumberLikeJs(expectedTsRaw)));
			}

			parsed.Set("keyIndex", keyIndex);
		}
	}

	return parsed;
}

Napi::Value ParseStatusInfo(const Napi::Env& env, const Napi::Object& node) {
	if (HasChildWithTag(node.Get("content"), "error")) {
		return env.Null();
	}

	const Napi::Value attrsValue = node.Get("attrs");
	Napi::Object attrs = attrsValue.IsObject() ? attrsValue.As<Napi::Object>() : Napi::Object::New(env);

	std::string status;
	if (!ReadNodeContentUtf8(env, node, &status)) {
		return env.Null();
	}

	bool useNullStatus = status.empty();
	if (useNullStatus) {
		std::string codeRaw;
		ReadStringLike(attrs.Get("code"), &codeRaw);
		if (codeRaw == "401") {
			useNullStatus = false;
		}
	}

	std::string timestampRaw;
	ReadStringLike(attrs.Get("t"), &timestampRaw);
	const double timestampSeconds = ParseNumberLikeJs(timestampRaw);
	const double timestampMs = std::isfinite(timestampSeconds) ? (timestampSeconds * 1000.0) : 0.0;

	Napi::Object parsed = Napi::Object::New(env);
	if (useNullStatus) {
		parsed.Set("status", env.Null());
	} else {
		parsed.Set("status", status);
	}
	parsed.Set("setAt", Napi::Date::New(env, timestampMs));
	return parsed;
}

Napi::Value ParseDisappearingModeInfo(const Napi::Env& env, const Napi::Object& node) {
	if (HasChildWithTag(node.Get("content"), "error")) {
		return env.Null();
	}

	const Napi::Value attrsValue = node.Get("attrs");
	Napi::Object attrs = attrsValue.IsObject() ? attrsValue.As<Napi::Object>() : Napi::Object::New(env);

	std::string durationRaw;
	std::string timestampRaw;
	ReadStringLike(attrs.Get("duration"), &durationRaw);
	ReadStringLike(attrs.Get("t"), &timestampRaw);

	const double duration = ParseNumberLikeJs(durationRaw);
	if (!std::isfinite(duration)) {
		return env.Null();
	}
	const double timestampSeconds = ParseNumberLikeJs(timestampRaw);
	const double timestampMs = std::isfinite(timestampSeconds) ? (timestampSeconds * 1000.0) : 0.0;

	Napi::Object parsed = Napi::Object::New(env);
	parsed.Set("duration", Napi::Number::New(env, duration));
	parsed.Set("setAt", Napi::Date::New(env, timestampMs));
	return parsed;
}

Napi::Value ParseBotProfileInfo(const Napi::Env& env, const Napi::Object& node) {
	if (HasChildWithTag(node.Get("content"), "error")) {
		return env.Null();
	}

	Napi::Object botNode = FindChildByTag(env, node.Get("content"), "bot");
	if (botNode.IsEmpty()) {
		std::string nodeTag;
		if (ReadStringLike(node.Get("tag"), &nodeTag) && nodeTag == "bot") {
			botNode = node;
		}
	}
	if (botNode.IsEmpty()) {
		return env.Null();
	}

	Napi::Object profileNode = FindChildByTag(env, botNode.Get("content"), "profile");
	if (profileNode.IsEmpty()) {
		profileNode = FindChildByTag(env, node.Get("content"), "profile");
	}
	if (profileNode.IsEmpty()) {
		return env.Null();
	}

	Napi::Object parsed = Napi::Object::New(env);
	Napi::Value attrsValue = node.Get("attrs");
	if (attrsValue.IsObject()) {
		std::string jid;
		if (ReadStringLike(attrsValue.As<Napi::Object>().Get("jid"), &jid) && !jid.empty()) {
			parsed.Set("jid", jid);
		}
	}

	const Napi::Object defaultNode = FindChildByTag(env, profileNode.Get("content"), "default");
	parsed.Set("isDefault", Napi::Boolean::New(env, !defaultNode.IsEmpty()));

	std::string value;
	if (ReadChildText(env, profileNode, "name", &value)) {
		parsed.Set("name", value);
	}
	if (ReadChildText(env, profileNode, "attributes", &value)) {
		parsed.Set("attributes", value);
	}
	if (ReadChildText(env, profileNode, "description", &value)) {
		parsed.Set("description", value);
	}
	if (ReadChildText(env, profileNode, "category", &value)) {
		parsed.Set("category", value);
	}

	Napi::Value profileAttrsValue = profileNode.Get("attrs");
	if (profileAttrsValue.IsObject()) {
		std::string personaId;
		if (ReadStringLike(profileAttrsValue.As<Napi::Object>().Get("persona_id"), &personaId)) {
			parsed.Set("personaId", personaId);
		}
	}

	Napi::Object commandsNode = FindChildByTag(env, profileNode.Get("content"), "commands");
	Napi::Array commandsOut = Napi::Array::New(env);
	uint32_t commandCount = 0;
	if (!commandsNode.IsEmpty()) {
		if (ReadChildText(env, commandsNode, "description", &value)) {
			parsed.Set("commandsDescription", value);
		}

		const Napi::Value commandsContentValue = commandsNode.Get("content");
		if (commandsContentValue.IsArray()) {
			const Napi::Array commandsContent = commandsContentValue.As<Napi::Array>();
			for (uint32_t i = 0; i < commandsContent.Length(); ++i) {
				const Napi::Value commandValue = commandsContent.Get(i);
				if (!commandValue.IsObject()) {
					continue;
				}

				const Napi::Object commandNode = commandValue.As<Napi::Object>();
				std::string commandTag;
				if (!ReadStringLike(commandNode.Get("tag"), &commandTag) || commandTag != "command") {
					continue;
				}

				Napi::Object parsedCommand = Napi::Object::New(env);
				if (ReadChildText(env, commandNode, "name", &value)) {
					parsedCommand.Set("name", value);
				}
				if (ReadChildText(env, commandNode, "description", &value)) {
					parsedCommand.Set("description", value);
				}
				commandsOut.Set(commandCount++, parsedCommand);
			}
		}
	}
	parsed.Set("commands", commandsOut);

	Napi::Object promptsNode = FindChildByTag(env, profileNode.Get("content"), "prompts");
	Napi::Array promptsOut = Napi::Array::New(env);
	uint32_t promptCount = 0;
	if (!promptsNode.IsEmpty()) {
		const Napi::Value promptsContentValue = promptsNode.Get("content");
		if (promptsContentValue.IsArray()) {
			const Napi::Array promptsContent = promptsContentValue.As<Napi::Array>();
			for (uint32_t i = 0; i < promptsContent.Length(); ++i) {
				const Napi::Value promptValue = promptsContent.Get(i);
				if (!promptValue.IsObject()) {
					continue;
				}

				const Napi::Object promptNode = promptValue.As<Napi::Object>();
				std::string promptTag;
				if (!ReadStringLike(promptNode.Get("tag"), &promptTag) || promptTag != "prompt") {
					continue;
				}

				std::string emoji;
				std::string text;
				const bool hasEmoji = ReadChildText(env, promptNode, "emoji", &emoji);
				const bool hasText = ReadChildText(env, promptNode, "text", &text);
				if (!hasEmoji && !hasText) {
					continue;
				}

				promptsOut.Set(promptCount++, Napi::String::New(env, emoji + " " + text));
			}
		}
	}
	parsed.Set("prompts", promptsOut);

	return parsed;
}

} // namespace baileys_native::socket_internal
