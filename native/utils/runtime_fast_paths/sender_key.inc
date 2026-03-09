Napi::Value SplitSenderKeySerializedFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireMinArgs(
			info,
			1,
			"splitSenderKeySerializedFast(serialized, signatureLength?) requires serialized"
		)) {
		return env.Null();
	}

	common::ByteView serialized{};
	if (!common::GetByteViewFromValue(env, info[0], "serialized", &serialized)) {
		return env.Null();
	}

	uint32_t signatureLength = 64;
	if (info.Length() > 1 && !info[1].IsUndefined() && !info[1].IsNull()) {
		if (!common::ReadUInt32FromValue(env, info[1], "signatureLength", &signatureLength)) {
			return env.Null();
		}
	}

	if (serialized.length <= static_cast<size_t>(signatureLength + 1u)) {
		return env.Null();
	}

	const size_t messageStart = 1;
	const size_t signatureStart = serialized.length - signatureLength;
	const size_t messageLength = signatureStart - messageStart;

	Napi::Object out = Napi::Object::New(env);
	out.Set("version", Napi::Number::New(env, serialized.data[0]));
	out.Set("message", Napi::Buffer<uint8_t>::Copy(env, serialized.data + messageStart, messageLength));
	out.Set("signature", Napi::Buffer<uint8_t>::Copy(env, serialized.data + signatureStart, signatureLength));
	return out;
}

