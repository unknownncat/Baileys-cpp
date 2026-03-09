#include "usync_parser.h"

#include "socket/internal/usync_parser_helpers.h"

#include <string>

namespace baileys_native {

namespace {

enum ProtocolMask : uint32_t {
	kProtocolContact = 1u << 0u,
	kProtocolLid = 1u << 1u,
	kProtocolDevices = 1u << 2u,
	kProtocolStatus = 1u << 3u,
	kProtocolDisappearingMode = 1u << 4u,
	kProtocolBot = 1u << 5u
};

uint32_t ParseProtocolMask(const Napi::Array& protocolArray) {
	uint32_t mask = 0;
	for (uint32_t i = 0; i < protocolArray.Length(); ++i) {
		std::string name;
		if (!socket_internal::ReadStringLike(protocolArray.Get(i), &name) || name.empty()) {
			continue;
		}

		if (name == "contact") {
			mask |= kProtocolContact;
			continue;
		}
		if (name == "lid") {
			mask |= kProtocolLid;
			continue;
		}
		if (name == "devices") {
			mask |= kProtocolDevices;
			continue;
		}
		if (name == "status") {
			mask |= kProtocolStatus;
			continue;
		}
		if (name == "disappearing_mode") {
			mask |= kProtocolDisappearingMode;
			continue;
		}
		if (name == "bot") {
			mask |= kProtocolBot;
			continue;
		}
	}

	return mask;
}

} // namespace

Napi::Value ParseUSyncQueryResultFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsArray()) {
		return env.Null();
	}

	const Napi::Object resultNode = info[0].As<Napi::Object>();
	const Napi::Array protocolArray = info[1].As<Napi::Array>();
	const uint32_t protocolMask = ParseProtocolMask(protocolArray);

	const Napi::Value resultAttrsValue = resultNode.Get("attrs");
	if (!resultAttrsValue.IsObject()) {
		return env.Null();
	}

	std::string resultType;
	socket_internal::ReadStringLike(resultAttrsValue.As<Napi::Object>().Get("type"), &resultType);
	if (resultType != "result") {
		return env.Null();
	}

	const Napi::Object usyncNode = socket_internal::FindChildByTag(env, resultNode.Get("content"), "usync");
	if (usyncNode.IsEmpty()) {
		return env.Null();
	}

	const Napi::Object listNode = socket_internal::FindChildByTag(env, usyncNode.Get("content"), "list");
	if (listNode.IsEmpty()) {
		return env.Null();
	}

	const Napi::Value listContentValue = listNode.Get("content");
	if (!listContentValue.IsArray()) {
		return env.Null();
	}

	const Napi::Array users = listContentValue.As<Napi::Array>();
	Napi::Array parsedList = Napi::Array::New(env);
	uint32_t parsedCount = 0;

	for (uint32_t i = 0; i < users.Length(); ++i) {
		const Napi::Value userValue = users.Get(i);
		if (!userValue.IsObject()) {
			continue;
		}

		const Napi::Object userNode = userValue.As<Napi::Object>();
		const Napi::Value userAttrsValue = userNode.Get("attrs");
		if (!userAttrsValue.IsObject()) {
			continue;
		}

		std::string id;
		if (!socket_internal::ReadStringLike(userAttrsValue.As<Napi::Object>().Get("jid"), &id) || id.empty()) {
			continue;
		}

		Napi::Object parsedUser = Napi::Object::New(env);
		parsedUser.Set("id", id);
		bool parseFailed = false;

		const Napi::Value userContentValue = userNode.Get("content");
		socket_internal::ForEachTaggedChild(
			userContentValue,
			[&](const Napi::Object& contentNode, const std::string& protocolTag) {
				if (protocolTag == "contact") {
					if ((protocolMask & kProtocolContact) == 0) {
						return;
					}
					if (socket_internal::HasChildWithTag(contentNode.Get("content"), "error")) {
						parseFailed = true;
						return;
					}
					const Napi::Value attrsValue = contentNode.Get("attrs");
					std::string type;
					if (attrsValue.IsObject()) {
						socket_internal::ReadStringLike(attrsValue.As<Napi::Object>().Get("type"), &type);
					}
					parsedUser.Set("contact", Napi::Boolean::New(env, type == "in"));
					return;
				}

				if (protocolTag == "lid") {
					if ((protocolMask & kProtocolLid) == 0) {
						return;
					}
					const Napi::Value attrsValue = contentNode.Get("attrs");
					if (attrsValue.IsObject()) {
						std::string lid;
						if (socket_internal::ReadStringLike(attrsValue.As<Napi::Object>().Get("val"), &lid)) {
							parsedUser.Set("lid", lid);
						}
					}
					return;
				}

				if (protocolTag == "devices") {
					if ((protocolMask & kProtocolDevices) == 0) {
						return;
					}
					const Napi::Value parsedDevices = socket_internal::ParseDeviceInfo(env, contentNode);
					if (parsedDevices.IsNull()) {
						parseFailed = true;
						return;
					}
					parsedUser.Set("devices", parsedDevices);
					return;
				}

				if (protocolTag == "status") {
					if ((protocolMask & kProtocolStatus) == 0) {
						return;
					}
					const Napi::Value parsedStatus = socket_internal::ParseStatusInfo(env, contentNode);
					if (parsedStatus.IsNull()) {
						parseFailed = true;
						return;
					}
					parsedUser.Set("status", parsedStatus);
					return;
				}

				if (protocolTag == "disappearing_mode") {
					if ((protocolMask & kProtocolDisappearingMode) == 0) {
						return;
					}
					const Napi::Value parsedDisappearingMode = socket_internal::ParseDisappearingModeInfo(env, contentNode);
					if (parsedDisappearingMode.IsNull()) {
						parseFailed = true;
						return;
					}
					parsedUser.Set("disappearing_mode", parsedDisappearingMode);
					return;
				}

				if (protocolTag == "bot") {
					if ((protocolMask & kProtocolBot) == 0) {
						return;
					}
					const Napi::Value parsedBot = socket_internal::ParseBotProfileInfo(env, contentNode);
					if (parsedBot.IsNull()) {
						parseFailed = true;
						return;
					}
					parsedUser.Set("bot", parsedBot);
				}
			}
		);
		if (parseFailed || env.IsExceptionPending()) {
			return env.Null();
		}

		parsedList.Set(parsedCount++, parsedUser);
	}

	Napi::Object out = Napi::Object::New(env);
	out.Set("list", parsedList);
	out.Set("sideList", Napi::Array::New(env, 0));
	return out;
}

} // namespace baileys_native
