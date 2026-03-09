Napi::Value NormalizeLidPnMappingsFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireArrayArg(info, 0, "normalizeLidPnMappingsFast(pairs) requires an array")) {
		return env.Null();
	}

	const Napi::Array input = info[0].As<Napi::Array>();
	const uint32_t count = input.Length();
	if (count == 0) {
		return EmptyArray(env);
	}

	std::vector<std::pair<std::string, std::string>> normalized;
	normalized.reserve(count);
	std::unordered_set<std::string> dedupe;
	dedupe.reserve(static_cast<size_t>(count) * 2u);

	for (uint32_t i = 0; i < count; ++i) {
		const Napi::Value itemValue = input.Get(i);
		if (!itemValue.IsObject()) {
			continue;
		}

		const Napi::Object item = itemValue.As<Napi::Object>();
		std::string lid;
		std::string pn;
		if (!common::ReadStringFromValue(env, item.Get("lid"), "pairs[].lid", &lid)) {
			return env.Null();
		}
		if (!common::ReadStringFromValue(env, item.Get("pn"), "pairs[].pn", &pn)) {
			return env.Null();
		}

		ParsedJid first{};
		ParsedJid second{};
		if (!ParseJidBasic(lid, &first) || !ParseJidBasic(pn, &second)) {
			continue;
		}

		std::string pnUser;
		std::string lidUser;
		if (IsLidDomain(first.domain) && IsPnDomain(second.domain)) {
			lidUser = std::move(first.user);
			pnUser = std::move(second.user);
		} else if (IsPnDomain(first.domain) && IsLidDomain(second.domain)) {
			pnUser = std::move(first.user);
			lidUser = std::move(second.user);
		} else {
			continue;
		}

		if (pnUser.empty() || lidUser.empty()) {
			continue;
		}

		std::string dedupeKey = pnUser;
		dedupeKey.push_back('\x1f');
		dedupeKey.append(lidUser);
		if (!dedupe.insert(dedupeKey).second) {
			continue;
		}

		normalized.emplace_back(std::move(pnUser), std::move(lidUser));
	}

	Napi::Array out = Napi::Array::New(env, normalized.size());
	for (size_t i = 0; i < normalized.size(); ++i) {
		Napi::Object row = Napi::Object::New(env);
		row.Set("pnUser", Napi::String::New(env, normalized[i].first));
		row.Set("lidUser", Napi::String::New(env, normalized[i].second));
		out.Set(static_cast<uint32_t>(i), row);
	}

	return out;
}

Napi::Value ExtractBotListV2Fast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireArrayArg(info, 0, "extractBotListV2Fast(sections) requires an array")) {
		return env.Null();
	}

	const Napi::Array sections = info[0].As<Napi::Array>();
	std::vector<std::pair<std::string, std::string>> bots;
	bots.reserve(sections.Length());

	for (uint32_t i = 0; i < sections.Length(); ++i) {
		const Napi::Value sectionValue = sections.Get(i);
		if (!sectionValue.IsObject()) {
			continue;
		}

		const Napi::Object section = sectionValue.As<Napi::Object>();
		const Napi::Value attrsValue = section.Get("attrs");
		if (!attrsValue.IsObject()) {
			continue;
		}
		const Napi::Object attrs = attrsValue.As<Napi::Object>();
		const Napi::Value typeValue = attrs.Get("type");
		if (!typeValue.IsString() || typeValue.As<Napi::String>().Utf8Value() != "all") {
			continue;
		}

		const Napi::Value contentValue = section.Get("content");
		if (!contentValue.IsArray()) {
			continue;
		}

		const Napi::Array content = contentValue.As<Napi::Array>();
		for (uint32_t j = 0; j < content.Length(); ++j) {
			const Napi::Value botValue = content.Get(j);
			if (!botValue.IsObject()) {
				continue;
			}

			const Napi::Object botNode = botValue.As<Napi::Object>();
			const Napi::Value tagValue = botNode.Get("tag");
			if (!tagValue.IsString() || tagValue.As<Napi::String>().Utf8Value() != "bot") {
				continue;
			}

			const Napi::Value botAttrsValue = botNode.Get("attrs");
			if (!botAttrsValue.IsObject()) {
				continue;
			}

			const Napi::Object botAttrs = botAttrsValue.As<Napi::Object>();
			if (!botAttrs.Get("jid").IsString() || !botAttrs.Get("persona_id").IsString()) {
				continue;
			}

			bots.emplace_back(
				botAttrs.Get("jid").As<Napi::String>().Utf8Value(),
				botAttrs.Get("persona_id").As<Napi::String>().Utf8Value()
			);
		}
	}

	Napi::Array out = Napi::Array::New(env, bots.size());
	for (size_t i = 0; i < bots.size(); ++i) {
		Napi::Object item = Napi::Object::New(env);
		item.Set("jid", Napi::String::New(env, bots[i].first));
		item.Set("personaId", Napi::String::New(env, bots[i].second));
		out.Set(static_cast<uint32_t>(i), item);
	}

	return out;
}

Napi::Value PickFirstExistingKeyFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsArray()) {
		return common::napi_guard::ThrowTypeValue(env, "pickFirstExistingKeyFast(target, keys) requires object and keys array");
	}

	const Napi::Object target = info[0].As<Napi::Object>();
	const Napi::Array keys = info[1].As<Napi::Array>();
	for (uint32_t i = 0; i < keys.Length(); ++i) {
		const Napi::Value keyValue = keys.Get(i);
		if (!keyValue.IsString()) {
			continue;
		}

		const std::string key = keyValue.As<Napi::String>().Utf8Value();
		if (!target.Has(key)) {
			continue;
		}

		const Napi::Value value = target.Get(key);
		if (value.IsUndefined()) {
			continue;
		}

		return Napi::String::New(env, key);
	}

	return env.Null();
}

Napi::Value IsRecoverableSignalTxErrorFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireMinArgs(
			info,
			3,
			"isRecoverableSignalTxErrorFast(name, message, statusCode) requires 3 args"
		)) {
		return env.Null();
	}

	std::string errorName;
	std::string errorMessage;
	uint32_t statusCode = 0;
	if (!common::ReadStringFromValue(env, info[0], "name", &errorName)) {
		return env.Null();
	}
	if (!common::ReadStringFromValue(env, info[1], "message", &errorMessage)) {
		return env.Null();
	}
	if (!common::ReadUInt32FromValue(env, info[2], "statusCode", &statusCode)) {
		return env.Null();
	}

	errorName = ToLowerCopy(std::move(errorName));
	errorMessage = ToLowerCopy(std::move(errorMessage));

	const bool recoverable = errorName == "missingsenderkeysessionerror" || statusCode == 428 ||
		errorMessage.find("connection closed") != std::string::npos ||
		errorMessage.find("invalid prekey") != std::string::npos ||
		errorMessage.find("no session found to decrypt message") != std::string::npos ||
		errorMessage.find("no valid sessions") != std::string::npos ||
		errorMessage.find("invalidmessageexception") != std::string::npos ||
		errorMessage.find("bad mac") != std::string::npos;

	return Napi::Boolean::New(env, recoverable);
}

Napi::Value GenerateSignalPubKeyFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireMinArgs(info, 1, "generateSignalPubKeyFast(pubKey, prefix?) requires pubKey")) {
		return env.Null();
	}

	common::ByteView pubView{};
	if (!common::GetByteViewFromValue(env, info[0], "pubKey", &pubView)) {
		return env.Null();
	}

	uint32_t prefix = 5;
	if (info.Length() > 1 && !info[1].IsUndefined() && !info[1].IsNull()) {
		if (!common::ReadUInt32FromValue(env, info[1], "prefix", &prefix)) {
			return env.Null();
		}
	}

	if (pubView.length == 33) {
		return Napi::Buffer<uint8_t>::Copy(env, pubView.data, pubView.length);
	}

	std::vector<uint8_t> out;
	out.reserve(pubView.length + 1);
	out.push_back(static_cast<uint8_t>(prefix & 0xffu));
	if (!common::safe_copy::AppendBytes(&out, pubView.data, pubView.length)) {
		return common::napi_guard::ThrowRangeValue(env, "generateSignalPubKeyFast(pubKey, prefix?) requires pubKey");
	}
	return common::MoveVectorToBuffer(env, std::move(out));
}

Napi::Value DedupeStringListFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireArrayArg(info, 0, "dedupeStringListFast(items) requires an array")) {
		return env.Null();
	}

	const Napi::Array items = info[0].As<Napi::Array>();
	const uint32_t count = items.Length();
	if (count == 0) {
		return EmptyArray(env);
	}

	std::vector<std::string> out;
	out.reserve(count);
	std::unordered_set<std::string> seen;
	seen.reserve(static_cast<size_t>(count) * 2u);

	for (uint32_t i = 0; i < count; ++i) {
		const Napi::Value value = items.Get(i);
		if (!value.IsString()) {
			continue;
		}

		std::string current = value.As<Napi::String>().Utf8Value();
		if (current.empty()) {
			continue;
		}

		if (!seen.insert(current).second) {
			continue;
		}

		out.push_back(std::move(current));
	}

	Napi::Array result = Napi::Array::New(env, out.size());
	for (size_t i = 0; i < out.size(); ++i) {
		result.Set(static_cast<uint32_t>(i), Napi::String::New(env, out[i]));
	}

	return result;
}

Napi::Value ResolveSignalAddressFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireMinArgs(info, 1, "resolveSignalAddressFast(jid) requires jid")) {
		return env.Null();
	}

	std::string jid;
	if (!common::ReadStringFromValue(env, info[0], "jid", &jid)) {
		return env.Null();
	}

	ParsedJidDetailed parsed{};
	if (!ParseJidDetailed(jid, &parsed)) {
		return env.Null();
	}

	int32_t domainType = 0;
	if (parsed.server == "lid") {
		domainType = 1;
	} else if (parsed.server == "hosted") {
		domainType = 128;
	} else if (parsed.server == "hosted.lid") {
		domainType = 129;
	} else if (!parsed.agent.empty()) {
		if (!ParseInt32Strict(parsed.agent, &domainType)) {
			domainType = 0;
		}
	}

	int32_t device = 0;
	if (!parsed.device.empty()) {
		if (!ParseInt32Strict(parsed.device, &device)) {
			return env.Null();
		}
	}

	Napi::Object out = Napi::Object::New(env);
	const std::string signalUser = domainType == 0 ? parsed.user : (parsed.user + "_" + std::to_string(domainType));
	out.Set("signalUser", Napi::String::New(env, signalUser));
	out.Set("user", Napi::String::New(env, parsed.user));
	out.Set("server", Napi::String::New(env, parsed.server));
	out.Set("device", Napi::Number::New(env, device));
	out.Set("domainType", Napi::Number::New(env, domainType));
	return out;
}

Napi::Value BuildRetryMessageKeyFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireMinArgs(info, 2, "buildRetryMessageKeyFast(to, id, separator?) requires to and id")) {
		return env.Null();
	}

	std::string to;
	std::string id;
	if (!common::ReadStringFromValue(env, info[0], "to", &to)) {
		return env.Null();
	}
	if (!common::ReadStringFromValue(env, info[1], "id", &id)) {
		return env.Null();
	}

	std::string separator("\0", 1);
	if (info.Length() > 2 && !info[2].IsUndefined() && !info[2].IsNull()) {
		if (!common::ReadStringFromValue(env, info[2], "separator", &separator)) {
			return env.Null();
		}
	}

	return Napi::String::New(env, to + separator + id);
}

Napi::Value ParseRetryErrorCodeFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireMinArgs(info, 2, "parseRetryErrorCodeFast(errorAttr, maxCode) requires 2 args")) {
		return env.Null();
	}

	const Napi::Value errorAttr = info[0];
	if (errorAttr.IsUndefined() || errorAttr.IsNull()) {
		return env.Null();
	}

	std::string raw;
	if (!common::ReadStringFromValue(env, errorAttr, "errorAttr", &raw)) {
		return env.Null();
	}
	if (raw.empty()) {
		return env.Null();
	}

	uint32_t maxCode = 0;
	if (!common::ReadUInt32FromValue(env, info[1], "maxCode", &maxCode)) {
		return env.Null();
	}

	errno = 0;
	char* endPtr = nullptr;
	const long parsed = std::strtol(raw.c_str(), &endPtr, 10);
	if (endPtr == raw.c_str()) {
		return env.Null();
	}

	if (errno == ERANGE || parsed < 0 || parsed > static_cast<long>(maxCode)) {
		return Napi::Number::New(env, 0);
	}

	return Napi::Number::New(env, parsed);
}

Napi::Value IsMacRetryErrorCodeFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireMinArgs(info, 1, "isMacRetryErrorCodeFast(code) requires code")) {
		return env.Null();
	}

	uint32_t code = 0;
	if (!common::ReadUInt32FromValue(env, info[0], "code", &code)) {
		return env.Null();
	}

	const bool isMac = code == 4 || code == 7;
	return Napi::Boolean::New(env, isMac);
}
