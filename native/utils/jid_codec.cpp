#include "jid_codec.h"

#include "common.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

namespace baileys_native {

namespace {

struct ParsedJid {
	std::string user;
	std::string server;
	std::string agent;
	std::string device;
};

bool ParseJidString(const std::string& jid, ParsedJid* out) {
	const size_t sepIdx = jid.find('@');
	if (sepIdx == std::string::npos) {
		return false;
	}

	out->server = jid.substr(sepIdx + 1);
	const std::string userCombined = jid.substr(0, sepIdx);

	const size_t colonIdx = userCombined.find(':');
	const std::string userAgent = colonIdx == std::string::npos ? userCombined : userCombined.substr(0, colonIdx);
	out->device = colonIdx == std::string::npos ? std::string() : userCombined.substr(colonIdx + 1);

	const size_t underscoreIdx = userAgent.find('_');
	out->user = underscoreIdx == std::string::npos ? userAgent : userAgent.substr(0, underscoreIdx);
	out->agent = underscoreIdx == std::string::npos ? std::string() : userAgent.substr(underscoreIdx + 1);

	return true;
}

double ParseNumberLikeJs(const std::string& value, bool* ok) {
	*ok = false;
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
		if (parsed > 0) {
			*ok = true;
			return std::numeric_limits<double>::infinity();
		}
		*ok = true;
		return -std::numeric_limits<double>::infinity();
	}

	*ok = true;
	return parsed;
}

double ParseIntLikeJs(const std::string& value, bool* ok) {
	*ok = false;
	if (value.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}

	errno = 0;
	char* endPtr = nullptr;
	const long parsed = std::strtol(value.c_str(), &endPtr, 10);
	if (endPtr == value.c_str()) {
		return std::numeric_limits<double>::quiet_NaN();
	}

	*ok = true;
	if (errno == ERANGE) {
		return parsed < 0 ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
	}

	return static_cast<double>(parsed);
}

Napi::Value BuildDecodedJidObject(const Napi::Env& env, const ParsedJid& parsed) {
	Napi::Object out = Napi::Object::New(env);
	out.Set("server", parsed.server);
	out.Set("user", parsed.user);

	double domainType = 0.0;
	if (parsed.server == "lid") {
		domainType = 1.0;
	} else if (parsed.server == "hosted") {
		domainType = 128.0;
	} else if (parsed.server == "hosted.lid") {
		domainType = 129.0;
	} else if (!parsed.agent.empty()) {
		bool parsedOk = false;
		const double parsedType = ParseIntLikeJs(parsed.agent, &parsedOk);
		domainType = parsedOk ? parsedType : std::numeric_limits<double>::quiet_NaN();
	}

	out.Set("domainType", Napi::Number::New(env, domainType));
	if (!parsed.device.empty()) {
		bool parsedOk = false;
		const double parsedDevice = ParseNumberLikeJs(parsed.device, &parsedOk);
		out.Set(
			"device",
			Napi::Number::New(
				env,
				parsedOk ? parsedDevice : std::numeric_limits<double>::quiet_NaN()
			)
		);
	}

	return out;
}

} // namespace

Napi::Value DecodeJidFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1) {
		return env.Null();
	}

	std::string jid;
	if (!common::ReadStringFromValue(env, info[0], "jid", &jid)) {
		if (env.IsExceptionPending()) {
			env.GetAndClearPendingException();
		}
		return env.Null();
	}

	ParsedJid parsed;
	if (!ParseJidString(jid, &parsed)) {
		return env.Null();
	}

	return BuildDecodedJidObject(env, parsed);
}

Napi::Value NormalizeJidUserFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1) {
		return Napi::String::New(env, "");
	}

	std::string jid;
	if (!common::ReadStringFromValue(env, info[0], "jid", &jid)) {
		if (env.IsExceptionPending()) {
			env.GetAndClearPendingException();
		}
		return Napi::String::New(env, "");
	}

	ParsedJid parsed;
	if (!ParseJidString(jid, &parsed)) {
		return Napi::String::New(env, "");
	}

	const std::string normalizedServer = parsed.server == "c.us" ? "s.whatsapp.net" : parsed.server;
	return Napi::String::New(env, parsed.user + "@" + normalizedServer);
}

Napi::Value AreJidsSameUserFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 2) {
		return Napi::Boolean::New(env, true);
	}

	ParsedJid firstParsed;
	ParsedJid secondParsed;
	bool hasFirst = false;
	bool hasSecond = false;

	if (info[0].IsString()) {
		std::string first;
		if (common::ReadStringFromValue(env, info[0], "jid1", &first)) {
			hasFirst = ParseJidString(first, &firstParsed);
		} else if (env.IsExceptionPending()) {
			env.GetAndClearPendingException();
		}
	}

	if (info[1].IsString()) {
		std::string second;
		if (common::ReadStringFromValue(env, info[1], "jid2", &second)) {
			hasSecond = ParseJidString(second, &secondParsed);
		} else if (env.IsExceptionPending()) {
			env.GetAndClearPendingException();
		}
	}

	if (!hasFirst && !hasSecond) {
		return Napi::Boolean::New(env, true);
	}
	if (!hasFirst || !hasSecond) {
		return Napi::Boolean::New(env, false);
	}

	return Napi::Boolean::New(env, firstParsed.user == secondParsed.user);
}

} // namespace baileys_native
