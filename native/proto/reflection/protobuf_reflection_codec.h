#pragma once

#include <napi.h>

#if defined(BAILEYS_HAS_NATIVE_WAPROTO) && BAILEYS_HAS_NATIVE_WAPROTO
#include <google/protobuf/message.h>

namespace baileys_native::proto_reflection {

bool JsToProtoMessage(
	const Napi::Env& env,
	const Napi::Value& value,
	google::protobuf::Message* message,
	uint32_t depth = 0
);

Napi::Object ProtoToJsObject(
	const Napi::Env& env,
	const google::protobuf::Message& message,
	uint32_t depth = 0
);

} // namespace baileys_native::proto_reflection
#endif

