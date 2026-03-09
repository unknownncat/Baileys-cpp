#include "mutation_engine.h"

#include "appstate/internal/mutation_engine_internal.h"

#include <unordered_map>
#include <vector>

namespace baileys_native {

namespace {

Napi::Value DecodeSyncdMutationsFastImpl(const Napi::CallbackInfo& info, bool encodeValueAsWire) {
	Napi::Env env = info.Env();
	if (info.Length() < 3) {
		Napi::TypeError::New(env, "decodeSyncdMutationsFast(mutations, keyMap, validateMacs) requires 3 arguments")
			.ThrowAsJavaScriptException();
		return env.Null();
	}

#if !defined(BAILEYS_HAS_NATIVE_WAPROTO) || !BAILEYS_HAS_NATIVE_WAPROTO
	Napi::Error::New(env, "decodeSyncdMutationsFast requires native WAProto support").ThrowAsJavaScriptException();
	return env.Null();
#elif !defined(_WIN32)
	Napi::Error::New(env, "decodeSyncdMutationsFast currently supports only Windows builds")
		.ThrowAsJavaScriptException();
	return env.Null();
#else
	std::vector<appstate_internal::MutationInput> mutations;
	if (!appstate_internal::ReadMutationInputs(env, info[0], &mutations)) {
		return env.Null();
	}

	std::unordered_map<std::string, appstate_internal::DerivedMutationKey> keyMap;
	if (!appstate_internal::ReadDerivedMutationKeyMap(env, info[1], &keyMap)) {
		return env.Null();
	}

	if (!info[2].IsBoolean()) {
		Napi::TypeError::New(env, "validateMacs must be a boolean").ThrowAsJavaScriptException();
		return env.Null();
	}
	const bool validateMacs = info[2].As<Napi::Boolean>().Value();

	Napi::Array out = Napi::Array::New(env, mutations.size());
	for (size_t i = 0; i < mutations.size(); ++i) {
		Napi::Object decoded = Napi::Object::New(env);
		if (!appstate_internal::DecodeMutationToJs(
				env,
				mutations[i],
				keyMap,
				validateMacs,
				encodeValueAsWire,
				&decoded
			)) {
			return env.Null();
		}
		out.Set(static_cast<uint32_t>(i), decoded);
	}

	return out;
#endif
}

} // namespace

Napi::Value DecodeSyncdMutationsFast(const Napi::CallbackInfo& info) {
	return DecodeSyncdMutationsFastImpl(info, false);
}

Napi::Value DecodeSyncdMutationsFastWire(const Napi::CallbackInfo& info) {
	return DecodeSyncdMutationsFastImpl(info, true);
}

} // namespace baileys_native

