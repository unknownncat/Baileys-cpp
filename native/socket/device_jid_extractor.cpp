#include "device_jid_extractor.h"

#include <cerrno>
#include <cstdlib>
#include <string>
#include <vector>

namespace baileys_native {

namespace {

constexpr int kDomainWhatsApp = 0;
constexpr int kDomainLid = 1;
constexpr int kDomainHosted = 128;
constexpr int kDomainHostedLid = 129;

struct ParsedJid {
	std::string user;
	std::string server;
	int domainType = kDomainWhatsApp;
	int device = 0;
	bool hasDevice = false;
};

struct FullJidOut {
	std::string user;
	int device = 0;
	int domainType = kDomainWhatsApp;
	std::string server;
};

bool EndsWith(const std::string& value, const char* suffix) {
	const size_t suffixLen = std::char_traits<char>::length(suffix);
	return value.size() >= suffixLen && value.compare(value.size() - suffixLen, suffixLen, suffix) == 0;
}

bool ParseInt(const std::string& value, int* out) {
	if (value.empty()) {
		return false;
	}

	errno = 0;
	char* endPtr = nullptr;
	const long parsed = std::strtol(value.c_str(), &endPtr, 10);
	if (endPtr == value.c_str() || *endPtr != '\0' || errno == ERANGE) {
		return false;
	}

	*out = static_cast<int>(parsed);
	return true;
}

bool ParseJid(const std::string& jid, ParsedJid* out) {
	const size_t sepIdx = jid.find('@');
	if (sepIdx == std::string::npos) {
		return false;
	}

	out->server = jid.substr(sepIdx + 1);
	const std::string userCombined = jid.substr(0, sepIdx);

	const size_t colonIdx = userCombined.find(':');
	const std::string userAgent = colonIdx == std::string::npos ? userCombined : userCombined.substr(0, colonIdx);
	const std::string device = colonIdx == std::string::npos ? std::string() : userCombined.substr(colonIdx + 1);

	const size_t underscoreIdx = userAgent.find('_');
	out->user = underscoreIdx == std::string::npos ? userAgent : userAgent.substr(0, underscoreIdx);
	const std::string agent = underscoreIdx == std::string::npos ? std::string() : userAgent.substr(underscoreIdx + 1);

	out->domainType = kDomainWhatsApp;
	if (out->server == "lid") {
		out->domainType = kDomainLid;
	} else if (out->server == "hosted") {
		out->domainType = kDomainHosted;
	} else if (out->server == "hosted.lid") {
		out->domainType = kDomainHostedLid;
	} else if (!agent.empty()) {
		int parsedAgent = kDomainWhatsApp;
		if (ParseInt(agent, &parsedAgent)) {
			out->domainType = parsedAgent;
		}
	}

	out->hasDevice = false;
	out->device = 0;
	if (!device.empty()) {
		int parsedDevice = 0;
		if (ParseInt(device, &parsedDevice)) {
			out->hasDevice = true;
			out->device = parsedDevice;
		}
	}

	return true;
}

bool ReadString(const Napi::Value& value, std::string* out) {
	if (value.IsUndefined() || value.IsNull() || !value.IsString()) {
		return false;
	}

	*out = value.As<Napi::String>().Utf8Value();
	return true;
}

bool ReadIntNumber(const Napi::Value& value, int* out) {
	if (!value.IsNumber()) {
		return false;
	}

	*out = static_cast<int>(value.As<Napi::Number>().Int64Value());
	return true;
}

bool IsTruthy(const Napi::Value& value) {
	if (value.IsUndefined() || value.IsNull()) {
		return false;
	}
	if (value.IsBoolean()) {
		return value.As<Napi::Boolean>().Value();
	}
	if (value.IsNumber()) {
		return value.As<Napi::Number>().DoubleValue() != 0.0;
	}
	if (value.IsString()) {
		return !value.As<Napi::String>().Utf8Value().empty();
	}

	return value.IsObject();
}

std::string GetServerFromDomainType(const std::string& initialServer, int domainType) {
	switch (domainType) {
		case kDomainLid:
			return "lid";
		case kDomainHosted:
			return "hosted";
		case kDomainHostedLid:
			return "hosted.lid";
		default:
			return initialServer;
	}
}

} // namespace

Napi::Value ExtractDeviceJidsFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 4 || !info[0].IsArray() || !info[1].IsString()) {
		return env.Null();
	}

	const Napi::Array resultList = info[0].As<Napi::Array>();
	const std::string myJid = info[1].As<Napi::String>().Utf8Value();
	std::string myLid;
	if (info[2].IsString()) {
		myLid = info[2].As<Napi::String>().Utf8Value();
	}
	const bool excludeZeroDevices = info[3].IsBoolean() ? info[3].As<Napi::Boolean>().Value() : false;

	ParsedJid myParsed;
	if (!ParseJid(myJid, &myParsed)) {
		return env.Null();
	}

	std::vector<FullJidOut> extracted;
	extracted.reserve(resultList.Length());

	for (uint32_t i = 0; i < resultList.Length(); ++i) {
		const Napi::Value userValue = resultList.Get(i);
		if (!userValue.IsObject()) {
			continue;
		}

		const Napi::Object userResult = userValue.As<Napi::Object>();
		std::string id;
		if (!ReadString(userResult.Get("id"), &id)) {
			continue;
		}

		ParsedJid decoded;
		if (!ParseJid(id, &decoded)) {
			continue;
		}

		int domainType = decoded.domainType;

		const Napi::Value devicesValue = userResult.Get("devices");
		if (!devicesValue.IsObject()) {
			continue;
		}

		const Napi::Object devices = devicesValue.As<Napi::Object>();
		const Napi::Value deviceListValue = devices.Get("deviceList");
		if (!deviceListValue.IsArray()) {
			continue;
		}

		const Napi::Array deviceList = deviceListValue.As<Napi::Array>();
		for (uint32_t j = 0; j < deviceList.Length(); ++j) {
			const Napi::Value deviceValue = deviceList.Get(j);
			if (!deviceValue.IsObject()) {
				continue;
			}

			const Napi::Object deviceObj = deviceValue.As<Napi::Object>();
			int device = 0;
			if (!ReadIntNumber(deviceObj.Get("id"), &device)) {
				continue;
			}

			const bool hasKeyIndex = IsTruthy(deviceObj.Get("keyIndex"));
			const bool isHosted = IsTruthy(deviceObj.Get("isHosted"));

			if ((excludeZeroDevices && device == 0) ||
				(((myParsed.user == decoded.user) || (myLid == decoded.user)) &&
					((myParsed.hasDevice ? myParsed.device : -1) == device)) ||
				(device != 0 && !hasKeyIndex)) {
				continue;
			}

			if (isHosted) {
				domainType = domainType == kDomainLid ? kDomainHostedLid : kDomainHosted;
			}

			FullJidOut out;
			out.user = decoded.user;
			out.device = device;
			out.domainType = domainType;
			out.server = GetServerFromDomainType(decoded.server, domainType);
			extracted.push_back(std::move(out));
		}
	}

	Napi::Array out = Napi::Array::New(env, extracted.size());
	for (size_t i = 0; i < extracted.size(); ++i) {
		const FullJidOut& item = extracted[i];
		Napi::Object entry = Napi::Object::New(env);
		entry.Set("user", item.user);
		entry.Set("device", Napi::Number::New(env, item.device));
		entry.Set("domainType", Napi::Number::New(env, item.domainType));
		entry.Set("server", item.server);
		out.Set(static_cast<uint32_t>(i), entry);
	}

	return out;
}

} // namespace baileys_native

