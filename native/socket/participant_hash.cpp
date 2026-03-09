#include "participant_hash.h"

#include "common.h"
#include "socket/internal/participant_hash_core.h"

#include <string>
#include <vector>

namespace baileys_native {

namespace {

bool ReadParticipants(const Napi::Env& env, const Napi::Value& value, std::vector<std::string>* out) {
	out->clear();
	if (!value.IsArray()) {
		Napi::TypeError::New(env, "participants must be an array").ThrowAsJavaScriptException();
		return false;
	}

	Napi::Array arr = value.As<Napi::Array>();
	out->reserve(arr.Length());
	for (uint32_t i = 0; i < arr.Length(); ++i) {
		std::string participant;
		if (!common::ReadStringFromValue(
				env,
				arr.Get(i),
				("participants[" + std::to_string(i) + "]").c_str(),
				&participant
			)) {
			return false;
		}
		out->push_back(std::move(participant));
	}

	return true;
}

} // namespace

Napi::Value GenerateParticipantHashV2Fast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1) {
		Napi::TypeError::New(env, "generateParticipantHashV2Fast(participants) requires participants")
			.ThrowAsJavaScriptException();
		return env.Null();
	}

	std::vector<std::string> participants;
	if (!ReadParticipants(env, info[0], &participants)) {
		return env.Null();
	}

	std::string hashError;
	std::string out;
	if (!socket_internal::GenerateParticipantHashV2(participants, &out, &hashError)) {
		Napi::Error::New(env, hashError).ThrowAsJavaScriptException();
		return env.Null();
	}

	return Napi::String::New(env, out);
}

} // namespace baileys_native
