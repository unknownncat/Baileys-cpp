#include "group_stub_codec.h"

#include "common.h"
#include "common/napi_guard.h"
#include "socket/internal/group_stub_flat_json.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace baileys_native {

Napi::Value EncodeGroupParticipantStubsFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireArrayArg(
			info,
			0,
			"encodeGroupParticipantStubsFast(participants) expects participants array"
		)) {
		return env.Null();
	}

	const Napi::Array participants = info[0].As<Napi::Array>();
	const uint32_t count = participants.Length();
	Napi::Array out = Napi::Array::New(env, count);

	for (uint32_t i = 0; i < count; ++i) {
		const Napi::Value value = participants.Get(i);
		if (!value.IsObject()) {
			return common::napi_guard::ThrowTypeValue(env, "participants entries must be objects");
		}

		const Napi::Object item = value.As<Napi::Object>();
		std::string id;
		if (!common::ReadStringFromValue(env, item.Get("id"), "participants[].id", &id)) {
			return env.Null();
		}

		std::vector<socket_internal::FlatField> fields;
		fields.reserve(4);
		fields.push_back({"id", id});

		std::optional<std::string> phoneNumber;
		const Napi::Value phoneValue = item.Get("phoneNumber");
		if (!phoneValue.IsUndefined() && !phoneValue.IsNull()) {
			if (!phoneValue.IsString()) {
				return common::napi_guard::ThrowTypeValue(env, "participants[].phoneNumber must be string when provided");
			}
			phoneNumber = phoneValue.As<Napi::String>().Utf8Value();
			fields.push_back({"phoneNumber", phoneNumber});
		}

		std::optional<std::string> lid;
		const Napi::Value lidValue = item.Get("lid");
		if (!lidValue.IsUndefined() && !lidValue.IsNull()) {
			if (!lidValue.IsString()) {
				return common::napi_guard::ThrowTypeValue(env, "participants[].lid must be string when provided");
			}
			lid = lidValue.As<Napi::String>().Utf8Value();
			fields.push_back({"lid", lid});
		}

		std::string admin;
		const Napi::Value adminValue = item.Get("admin");
		if (adminValue.IsString()) {
			admin = adminValue.As<Napi::String>().Utf8Value();
			fields.push_back({"admin", admin});
		} else if (!adminValue.IsUndefined() && !adminValue.IsNull()) {
			return common::napi_guard::ThrowTypeValue(env, "participants[].admin must be string or null");
		} else {
			fields.push_back({"admin", std::nullopt});
		}

		const std::string json = socket_internal::EncodeFlatObject(fields);
		out.Set(i, Napi::String::New(env, json));
	}

	return out;
}

Napi::Value ParseGroupParticipantStubsFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireArrayArg(
			info,
			0,
			"parseGroupParticipantStubsFast(items) expects items array"
		)) {
		return env.Null();
	}

	const Napi::Array items = info[0].As<Napi::Array>();
	const uint32_t count = items.Length();
	Napi::Array out = Napi::Array::New(env, count);

	for (uint32_t i = 0; i < count; ++i) {
		const Napi::Value item = items.Get(i);
		if (!item.IsString()) {
			return common::napi_guard::ThrowTypeValue(env, "parseGroupParticipantStubsFast items must be strings");
		}

		const std::string raw = item.As<Napi::String>().Utf8Value();
		std::vector<socket_internal::FlatField> fields;
		if (!socket_internal::ParseFlatObject(raw, &fields)) {
			return env.Null();
		}

		Napi::Object parsed = Napi::Object::New(env);
		for (const auto& [key, value] : fields) {
			if (value.has_value()) {
				parsed.Set(key, Napi::String::New(env, value.value()));
			} else {
				parsed.Set(key, env.Null());
			}
		}

		out.Set(i, parsed);
	}

	return out;
}

} // namespace baileys_native
