#include "protobuf_reflection_codec.h"

#if defined(BAILEYS_HAS_NATIVE_WAPROTO) && BAILEYS_HAS_NATIVE_WAPROTO

#include "proto/reflection/internal/protobuf_reflection_from_js_internal.h"

namespace baileys_native::proto_reflection {

bool JsToProtoMessage(const Napi::Env& env, const Napi::Value& value, google::protobuf::Message* message, uint32_t depth) {
	using from_js_internal::kMaxProtoDepth;

	if (depth > kMaxProtoDepth) {
		return from_js_internal::ThrowRangeError(env, "protobuf conversion max depth exceeded");
	}
	if (!value.IsObject()) {
		return from_js_internal::ThrowTypeError(env, "expected protobuf object");
	}

	Napi::Object source = value.As<Napi::Object>();
	const auto* descriptor = message->GetDescriptor();
	if (!descriptor) {
		return from_js_internal::ThrowTypeError(env, "protobuf descriptor missing");
	}

	message->Clear();

	for (int i = 0; i < descriptor->field_count(); ++i) {
		const auto* field = descriptor->field(i);
		Napi::Value fieldValue;
		if (!from_js_internal::TryGetOwnFieldValue(source, field, &fieldValue)) {
			continue;
		}
		if (fieldValue.IsUndefined() || fieldValue.IsNull()) {
			continue;
		}

		if (!from_js_internal::SetFieldFromValue(env, fieldValue, message, field, depth + 1)) {
			return false;
		}
	}

	return true;
}

} // namespace baileys_native::proto_reflection

#endif

