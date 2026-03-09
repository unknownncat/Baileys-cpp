Napi::Value BuildParticipantNodesFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireArrayArg(info, 0, "buildParticipantNodesFast(participants) requires participants array")) {
		return env.Null();
	}

	const Napi::Array participants = info[0].As<Napi::Array>();
	Napi::Array out = Napi::Array::New(env, participants.Length());
	for (uint32_t i = 0; i < participants.Length(); ++i) {
		const Napi::Value participant = participants.Get(i);
		if (!participant.IsString()) {
			return common::napi_guard::ThrowTypeValue(env, "participants entries must be strings");
		}

		const Napi::Object attrs = Napi::Object::New(env);
		attrs.Set("jid", participant.As<Napi::String>());

		const Napi::Object node = Napi::Object::New(env);
		node.Set("tag", Napi::String::New(env, "participant"));
		node.Set("attrs", attrs);
		out.Set(i, node);
	}

	return out;
}

Napi::Value ExtractNodeAttrsFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireArrayArg(info, 0, "extractNodeAttrsFast(nodes) requires nodes array")) {
		return env.Null();
	}

	const Napi::Array nodes = info[0].As<Napi::Array>();
	Napi::Array out = Napi::Array::New(env, nodes.Length());
	for (uint32_t i = 0; i < nodes.Length(); ++i) {
		const Napi::Value nodeValue = nodes.Get(i);
		if (!nodeValue.IsObject()) {
			out.Set(i, Napi::Object::New(env));
			continue;
		}

		const Napi::Object node = nodeValue.As<Napi::Object>();
		const Napi::Value attrsValue = node.Get("attrs");
		if (!attrsValue.IsObject()) {
			out.Set(i, Napi::Object::New(env));
			continue;
		}

		out.Set(i, attrsValue.As<Napi::Object>());
	}

	return out;
}

Napi::Value MapParticipantActionResultsFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireArrayArg(
			info,
			0,
			"mapParticipantActionResultsFast(nodes, includeContent?) requires nodes array"
		)) {
		return env.Null();
	}

	bool includeContent = false;
	if (info.Length() > 1 && info[1].IsBoolean()) {
		includeContent = info[1].As<Napi::Boolean>().Value();
	}

	const Napi::Array nodes = info[0].As<Napi::Array>();
	Napi::Array out = Napi::Array::New(env, nodes.Length());
	for (uint32_t i = 0; i < nodes.Length(); ++i) {
		const Napi::Value nodeValue = nodes.Get(i);
		if (!nodeValue.IsObject()) {
			continue;
		}

		const Napi::Object node = nodeValue.As<Napi::Object>();
		const Napi::Object attrs = node.Get("attrs").IsObject() ? node.Get("attrs").As<Napi::Object>() : Napi::Object::New(env);

		std::string status = "200";
		if (attrs.Get("error").IsString()) {
			status = attrs.Get("error").As<Napi::String>().Utf8Value();
		}

		std::string jid;
		if (attrs.Get("jid").IsString()) {
			jid = attrs.Get("jid").As<Napi::String>().Utf8Value();
		}

		const Napi::Object row = Napi::Object::New(env);
		row.Set("status", Napi::String::New(env, status));
		row.Set("jid", Napi::String::New(env, jid));
		if (includeContent) {
			row.Set("content", node);
		}

		out.Set(i, row);
	}

	return out;
}

Napi::Value ExtractCommunityLinkedGroupsFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireArrayArg(
			info,
			0,
			"extractCommunityLinkedGroupsFast(groupNodes) requires groupNodes array"
		)) {
		return env.Null();
	}

	const Napi::Array nodes = info[0].As<Napi::Array>();
	Napi::Array out = Napi::Array::New(env, nodes.Length());
	uint32_t outIndex = 0;
	for (uint32_t i = 0; i < nodes.Length(); ++i) {
		const Napi::Value nodeValue = nodes.Get(i);
		if (!nodeValue.IsObject()) {
			continue;
		}

		const Napi::Object node = nodeValue.As<Napi::Object>();
		if (!node.Get("attrs").IsObject()) {
			continue;
		}

		const Napi::Object attrs = node.Get("attrs").As<Napi::Object>();
		const Napi::Object row = Napi::Object::New(env);

		if (attrs.Get("id").IsString()) {
			const std::string id = attrs.Get("id").As<Napi::String>().Utf8Value();
			if (!id.empty()) {
				row.Set("id", Napi::String::New(env, id + "@g.us"));
			}
		}

		std::string subject;
		if (attrs.Get("subject").IsString()) {
			subject = attrs.Get("subject").As<Napi::String>().Utf8Value();
		}
		row.Set("subject", Napi::String::New(env, subject));

		if (attrs.Get("creation").IsString()) {
			const std::string creationRaw = attrs.Get("creation").As<Napi::String>().Utf8Value();
			row.Set("creation", Napi::Number::New(env, ParseDoubleOrZero(creationRaw)));
		} else if (attrs.Get("creation").IsNumber()) {
			row.Set("creation", attrs.Get("creation").As<Napi::Number>());
		}

		if (attrs.Get("creator").IsString()) {
			const std::string creator = attrs.Get("creator").As<Napi::String>().Utf8Value();
			row.Set("owner", Napi::String::New(env, NormalizeJidUserLocal(creator)));
		}

		if (attrs.Get("size").IsString()) {
			const std::string sizeRaw = attrs.Get("size").As<Napi::String>().Utf8Value();
			row.Set("size", Napi::Number::New(env, ParseDoubleOrZero(sizeRaw)));
		} else if (attrs.Get("size").IsNumber()) {
			row.Set("size", attrs.Get("size").As<Napi::Number>());
		}

		out.Set(outIndex++, row);
	}

	if (outIndex == nodes.Length()) {
		return out;
	}

	Napi::Array compact = Napi::Array::New(env, outIndex);
	for (uint32_t i = 0; i < outIndex; ++i) {
		compact.Set(i, out.Get(i));
	}
	return compact;
}
