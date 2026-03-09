#include "runtime_fast_paths.h"

#include "common.h"
#include "common/napi_guard.h"
#include "common/safe_copy.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace baileys_native {

namespace {

std::string ToLowerCopy(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});
	return value;
}

struct ParsedJid {
	std::string user;
	std::string domain;
};

bool ParseJidBasic(const std::string& jid, ParsedJid* out) {
	const size_t atPos = jid.find('@');
	if (atPos == std::string::npos || atPos == 0 || atPos + 1 >= jid.size()) {
		return false;
	}

	std::string userPart = jid.substr(0, atPos);
	const size_t deviceSep = userPart.find(':');
	if (deviceSep != std::string::npos) {
		userPart = userPart.substr(0, deviceSep);
	}
	if (userPart.empty()) {
		return false;
	}

	out->user = std::move(userPart);
	out->domain = ToLowerCopy(jid.substr(atPos + 1));
	return !out->domain.empty();
}

bool IsLidDomain(const std::string& domain) {
	return domain == "lid" || domain == "hosted.lid";
}

bool IsPnDomain(const std::string& domain) {
	return domain == "s.whatsapp.net" || domain == "hosted";
}

Napi::Array EmptyArray(const Napi::Env& env) {
	return Napi::Array::New(env, 0);
}

struct ParsedJidDetailed {
	std::string user;
	std::string server;
	std::string agent;
	std::string device;
};

bool ParseJidDetailed(const std::string& jid, ParsedJidDetailed* out) {
	const size_t atPos = jid.find('@');
	if (atPos == std::string::npos || atPos == 0 || atPos + 1 >= jid.size()) {
		return false;
	}

	out->server = jid.substr(atPos + 1);
	const std::string userCombined = jid.substr(0, atPos);

	const size_t colonPos = userCombined.find(':');
	const std::string userAgent = colonPos == std::string::npos ? userCombined : userCombined.substr(0, colonPos);
	out->device = colonPos == std::string::npos ? std::string() : userCombined.substr(colonPos + 1);

	const size_t underscorePos = userAgent.find('_');
	out->user = underscorePos == std::string::npos ? userAgent : userAgent.substr(0, underscorePos);
	out->agent = underscorePos == std::string::npos ? std::string() : userAgent.substr(underscorePos + 1);
	return !out->user.empty() && !out->server.empty();
}

bool ParseInt32Strict(const std::string& text, int32_t* out) {
	if (text.empty()) {
		return false;
	}

	errno = 0;
	char* endPtr = nullptr;
	const long parsed = std::strtol(text.c_str(), &endPtr, 10);
	if (endPtr == text.c_str() || *endPtr != '\0' || errno == ERANGE) {
		return false;
	}

	if (parsed < std::numeric_limits<int32_t>::min() || parsed > std::numeric_limits<int32_t>::max()) {
		return false;
	}

	*out = static_cast<int32_t>(parsed);
	return true;
}

double ParseDoubleOrZero(const std::string& text) {
	if (text.empty()) {
		return 0.0;
	}

	errno = 0;
	char* endPtr = nullptr;
	const double parsed = std::strtod(text.c_str(), &endPtr);
	if (endPtr == text.c_str() || errno == ERANGE) {
		return 0.0;
	}

	return parsed;
}

Napi::Buffer<uint8_t> BufferFromAscii(const Napi::Env& env, const std::string& value) {
	return Napi::Buffer<uint8_t>::Copy(env, reinterpret_cast<const uint8_t*>(value.data()), value.size());
}

bool ReadNodeContentAsString(const Napi::Value& value, std::string* out) {
	if (value.IsString()) {
		*out = value.As<Napi::String>().Utf8Value();
		return true;
	}

	if (value.IsBuffer()) {
		const Napi::Buffer<uint8_t> buffer = value.As<Napi::Buffer<uint8_t>>();
		out->assign(reinterpret_cast<const char*>(buffer.Data()), buffer.Length());
		return true;
	}

	if (value.IsTypedArray()) {
		const Napi::Uint8Array view = value.As<Napi::Uint8Array>();
		out->assign(reinterpret_cast<const char*>(view.Data()), view.ByteLength());
		return true;
	}

	return false;
}

bool ReadChildNodeByTag(const Napi::Object& node, const std::string& tag, Napi::Object* out) {
	const Napi::Value contentValue = node.Get("content");
	if (!contentValue.IsArray()) {
		return false;
	}

	const Napi::Array content = contentValue.As<Napi::Array>();
	for (uint32_t i = 0; i < content.Length(); ++i) {
		const Napi::Value item = content.Get(i);
		if (!item.IsObject()) {
			continue;
		}

		const Napi::Object child = item.As<Napi::Object>();
		const Napi::Value childTag = child.Get("tag");
		if (!childTag.IsString()) {
			continue;
		}
		if (childTag.As<Napi::String>().Utf8Value() != tag) {
			continue;
		}

		*out = child;
		return true;
	}

	return false;
}

std::vector<Napi::Object> ReadChildrenByTag(const Napi::Object& node, const std::string& tag) {
	std::vector<Napi::Object> out;
	const Napi::Value contentValue = node.Get("content");
	if (!contentValue.IsArray()) {
		return out;
	}

	const Napi::Array content = contentValue.As<Napi::Array>();
	out.reserve(content.Length());
	for (uint32_t i = 0; i < content.Length(); ++i) {
		const Napi::Value item = content.Get(i);
		if (!item.IsObject()) {
			continue;
		}

		const Napi::Object child = item.As<Napi::Object>();
		const Napi::Value childTag = child.Get("tag");
		if (!childTag.IsString()) {
			continue;
		}
		if (childTag.As<Napi::String>().Utf8Value() != tag) {
			continue;
		}

		out.push_back(child);
	}

	return out;
}

bool ReadChildStringByTag(const Napi::Object& node, const std::string& tag, std::string* out) {
	Napi::Object child;
	if (!ReadChildNodeByTag(node, tag, &child)) {
		return false;
	}

	const Napi::Value contentValue = child.Get("content");
	return ReadNodeContentAsString(contentValue, out);
}

std::string NormalizeJidUserLocal(const std::string& jid) {
	ParsedJidDetailed parsed{};
	if (!ParseJidDetailed(jid, &parsed)) {
		return "";
	}

	const std::string normalizedServer = parsed.server == "c.us" ? "s.whatsapp.net" : parsed.server;
	return parsed.user + "@" + normalizedServer;
}

Napi::Object BuildProductObject(const Napi::Env& env, const Napi::Object& productNode) {
	const Napi::Object out = Napi::Object::New(env);
	const Napi::Object attrs = productNode.Get("attrs").IsObject() ? productNode.Get("attrs").As<Napi::Object>()
																		: Napi::Object::New(env);
	const bool isHidden = attrs.Get("is_hidden").IsString() && attrs.Get("is_hidden").As<Napi::String>().Utf8Value() == "true";

	std::string id;
	std::string name;
	std::string retailerId;
	std::string url;
	std::string description;
	std::string priceRaw;
	std::string currency;

	ReadChildStringByTag(productNode, "id", &id);
	ReadChildStringByTag(productNode, "name", &name);
	ReadChildStringByTag(productNode, "retailer_id", &retailerId);
	ReadChildStringByTag(productNode, "url", &url);
	ReadChildStringByTag(productNode, "description", &description);
	ReadChildStringByTag(productNode, "price", &priceRaw);
	ReadChildStringByTag(productNode, "currency", &currency);

	std::string requestedImage;
	std::string originalImage;
	Napi::Object mediaNode;
	if (ReadChildNodeByTag(productNode, "media", &mediaNode)) {
		Napi::Object imageNode;
		if (ReadChildNodeByTag(mediaNode, "image", &imageNode)) {
			ReadChildStringByTag(imageNode, "request_image_url", &requestedImage);
			ReadChildStringByTag(imageNode, "original_image_url", &originalImage);
		}
	}

	std::string reviewStatus;
	Napi::Object statusInfoNode;
	if (ReadChildNodeByTag(productNode, "status_info", &statusInfoNode)) {
		ReadChildStringByTag(statusInfoNode, "status", &reviewStatus);
	}

	Napi::Object imageUrls = Napi::Object::New(env);
	imageUrls.Set("requested", Napi::String::New(env, requestedImage));
	imageUrls.Set("original", Napi::String::New(env, originalImage));

	Napi::Object review = Napi::Object::New(env);
	review.Set("whatsapp", Napi::String::New(env, reviewStatus));

	out.Set("id", Napi::String::New(env, id));
	out.Set("imageUrls", imageUrls);
	out.Set("reviewStatus", review);
	out.Set("availability", Napi::String::New(env, "in stock"));
	out.Set("name", Napi::String::New(env, name));
	out.Set("description", Napi::String::New(env, description));
	out.Set("price", Napi::Number::New(env, ParseDoubleOrZero(priceRaw)));
	out.Set("currency", Napi::String::New(env, currency));
	out.Set("isHidden", Napi::Boolean::New(env, isHidden));

	if (!retailerId.empty()) {
		out.Set("retailerId", Napi::String::New(env, retailerId));
	}
	if (!url.empty()) {
		out.Set("url", Napi::String::New(env, url));
	}

	return out;
}

} // namespace


#include "utils/runtime_fast_paths/jid_retry.inc"

#include "utils/runtime_fast_paths/participant_attrs.inc"

#include "utils/runtime_fast_paths/business.inc"

#include "utils/runtime_fast_paths/sender_key.inc"

} // namespace baileys_native
