Napi::Value BuildBusinessProfileNodesFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireObjectArg(info, 0, "buildBusinessProfileNodesFast(args) requires args object")) {
		return env.Null();
	}

	const Napi::Object args = info[0].As<Napi::Object>();
	Napi::Array out = Napi::Array::New(env);
	uint32_t outIndex = 0;

	const std::vector<std::string> simpleFields = {"address", "email", "description"};
	for (const std::string& field : simpleFields) {
		const Napi::Value value = args.Get(field);
		if (value.IsUndefined() || value.IsNull()) {
			continue;
		}
		if (!value.IsString()) {
			return common::napi_guard::ThrowTypeValue(env, field + " must be string");
		}

		Napi::Object node = Napi::Object::New(env);
		node.Set("tag", Napi::String::New(env, field));
		node.Set("attrs", Napi::Object::New(env));
		node.Set("content", value.As<Napi::String>());
		out.Set(outIndex++, node);
	}

	const Napi::Value websitesValue = args.Get("websites");
	if (!websitesValue.IsUndefined() && !websitesValue.IsNull()) {
		if (!websitesValue.IsArray()) {
			return common::napi_guard::ThrowTypeValue(env, "websites must be an array");
		}

		const Napi::Array websites = websitesValue.As<Napi::Array>();
		for (uint32_t i = 0; i < websites.Length(); ++i) {
			const Napi::Value website = websites.Get(i);
			if (!website.IsString()) {
				return common::napi_guard::ThrowTypeValue(env, "websites entries must be strings");
			}

			Napi::Object node = Napi::Object::New(env);
			node.Set("tag", Napi::String::New(env, "website"));
			node.Set("attrs", Napi::Object::New(env));
			node.Set("content", website.As<Napi::String>());
			out.Set(outIndex++, node);
		}
	}

	const Napi::Value hoursValue = args.Get("hours");
	if (!hoursValue.IsUndefined() && !hoursValue.IsNull()) {
		if (!hoursValue.IsObject()) {
			return common::napi_guard::ThrowTypeValue(env, "hours must be object");
		}

		const Napi::Object hours = hoursValue.As<Napi::Object>();
		if (!hours.Get("timezone").IsString() || !hours.Get("days").IsArray()) {
			return common::napi_guard::ThrowTypeValue(env, "hours must include timezone and days[]");
		}

		const Napi::Object businessHoursNode = Napi::Object::New(env);
		businessHoursNode.Set("tag", Napi::String::New(env, "business_hours"));

		const Napi::Object businessHoursAttrs = Napi::Object::New(env);
		businessHoursAttrs.Set("timezone", hours.Get("timezone"));
		businessHoursNode.Set("attrs", businessHoursAttrs);

		const Napi::Array days = hours.Get("days").As<Napi::Array>();
		Napi::Array daysContent = Napi::Array::New(env, days.Length());
		for (uint32_t i = 0; i < days.Length(); ++i) {
			const Napi::Value dayValue = days.Get(i);
			if (!dayValue.IsObject()) {
				return common::napi_guard::ThrowTypeValue(env, "hours.days entries must be objects");
			}

			const Napi::Object day = dayValue.As<Napi::Object>();
			if (day.Get("day").IsUndefined() || day.Get("mode").IsUndefined()) {
				return common::napi_guard::ThrowTypeValue(env, "hours.days entries must include day and mode");
			}

			const Napi::Object dayNode = Napi::Object::New(env);
			dayNode.Set("tag", Napi::String::New(env, "business_hours_config"));

			const Napi::Object dayAttrs = Napi::Object::New(env);
			dayAttrs.Set("day_of_week", day.Get("day"));
			dayAttrs.Set("mode", day.Get("mode"));

			if (day.Get("mode").IsString() && day.Get("mode").As<Napi::String>().Utf8Value() == "specific_hours") {
				dayAttrs.Set("open_time", day.Get("openTimeInMinutes"));
				dayAttrs.Set("close_time", day.Get("closeTimeInMinutes"));
			}

			dayNode.Set("attrs", dayAttrs);
			daysContent.Set(i, dayNode);
		}

		businessHoursNode.Set("content", daysContent);
		out.Set(outIndex++, businessHoursNode);
	}

	return out;
}

Napi::Value BuildCatalogQueryParamsFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	uint32_t limit = 10;

	if (info.Length() > 0 && !info[0].IsUndefined() && !info[0].IsNull()) {
		if (!common::ReadUInt32FromValue(env, info[0], "limit", &limit)) {
			return env.Null();
		}
	}

	std::optional<std::string> cursor;
	if (info.Length() > 1 && !info[1].IsUndefined() && !info[1].IsNull()) {
		std::string cursorValue;
		if (!common::ReadStringFromValue(env, info[1], "cursor", &cursorValue)) {
			return env.Null();
		}
		if (!cursorValue.empty()) {
			cursor = std::move(cursorValue);
		}
	}

	Napi::Array out = Napi::Array::New(env, cursor.has_value() ? 4 : 3);
	const auto makeNode = [&](const std::string& tag, const Napi::Value& contentValue) {
		Napi::Object node = Napi::Object::New(env);
		node.Set("tag", Napi::String::New(env, tag));
		node.Set("attrs", Napi::Object::New(env));
		node.Set("content", contentValue);
		return node;
	};

	out.Set(static_cast<uint32_t>(0), makeNode("limit", BufferFromAscii(env, std::to_string(limit))));
	out.Set(static_cast<uint32_t>(1), makeNode("width", BufferFromAscii(env, "100")));
	out.Set(static_cast<uint32_t>(2), makeNode("height", BufferFromAscii(env, "100")));
	if (cursor.has_value()) {
		out.Set(static_cast<uint32_t>(3), makeNode("after", Napi::String::New(env, cursor.value())));
	}

	return out;
}

Napi::Value ParseProductNodesFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireArrayArg(
			info,
			0,
			"parseProductNodesFast(productNodes) requires productNodes array"
		)) {
		return env.Null();
	}

	const Napi::Array nodes = info[0].As<Napi::Array>();
	Napi::Array out = Napi::Array::New(env);
	uint32_t outIndex = 0;
	for (uint32_t i = 0; i < nodes.Length(); ++i) {
		const Napi::Value nodeValue = nodes.Get(i);
		if (!nodeValue.IsObject()) {
			continue;
		}
		out.Set(outIndex++, BuildProductObject(env, nodeValue.As<Napi::Object>()));
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

Napi::Value ParseCollectionNodesFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireArrayArg(
			info,
			0,
			"parseCollectionNodesFast(collectionNodes) requires collectionNodes array"
		)) {
		return env.Null();
	}

	const Napi::Array nodes = info[0].As<Napi::Array>();
	Napi::Array out = Napi::Array::New(env);
	uint32_t outIndex = 0;
	for (uint32_t i = 0; i < nodes.Length(); ++i) {
		const Napi::Value nodeValue = nodes.Get(i);
		if (!nodeValue.IsObject()) {
			continue;
		}

		const Napi::Object collectionNode = nodeValue.As<Napi::Object>();
		std::string id;
		std::string name;
		ReadChildStringByTag(collectionNode, "id", &id);
		ReadChildStringByTag(collectionNode, "name", &name);

		const std::vector<Napi::Object> productNodes = ReadChildrenByTag(collectionNode, "product");
		Napi::Array products = Napi::Array::New(env, productNodes.size());
		for (size_t p = 0; p < productNodes.size(); ++p) {
			products.Set(static_cast<uint32_t>(p), BuildProductObject(env, productNodes[p]));
		}

		std::string status;
		std::string canAppealRaw;
		Napi::Object statusInfoNode;
		if (ReadChildNodeByTag(collectionNode, "status_info", &statusInfoNode)) {
			ReadChildStringByTag(statusInfoNode, "status", &status);
			ReadChildStringByTag(statusInfoNode, "can_appeal", &canAppealRaw);
		}

		Napi::Object statusObj = Napi::Object::New(env);
		statusObj.Set("status", Napi::String::New(env, status));
		statusObj.Set("canAppeal", Napi::Boolean::New(env, canAppealRaw == "true"));

		Napi::Object row = Napi::Object::New(env);
		row.Set("id", Napi::String::New(env, id));
		row.Set("name", Napi::String::New(env, name));
		row.Set("products", products);
		row.Set("status", statusObj);
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

Napi::Value ParseOrderProductNodesFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (!common::napi_guard::RequireArrayArg(
			info,
			0,
			"parseOrderProductNodesFast(productNodes) requires productNodes array"
		)) {
		return env.Null();
	}

	const Napi::Array nodes = info[0].As<Napi::Array>();
	Napi::Array out = Napi::Array::New(env);
	uint32_t outIndex = 0;
	for (uint32_t i = 0; i < nodes.Length(); ++i) {
		const Napi::Value nodeValue = nodes.Get(i);
		if (!nodeValue.IsObject()) {
			continue;
		}

		const Napi::Object productNode = nodeValue.As<Napi::Object>();
		std::string id;
		std::string name;
		std::string priceRaw;
		std::string currency;
		std::string quantityRaw;
		ReadChildStringByTag(productNode, "id", &id);
		ReadChildStringByTag(productNode, "name", &name);
		ReadChildStringByTag(productNode, "price", &priceRaw);
		ReadChildStringByTag(productNode, "currency", &currency);
		ReadChildStringByTag(productNode, "quantity", &quantityRaw);

		std::string imageUrl;
		Napi::Object imageNode;
		if (ReadChildNodeByTag(productNode, "image", &imageNode)) {
			ReadChildStringByTag(imageNode, "url", &imageUrl);
		}

		Napi::Object row = Napi::Object::New(env);
		row.Set("id", Napi::String::New(env, id));
		row.Set("name", Napi::String::New(env, name));
		row.Set("imageUrl", Napi::String::New(env, imageUrl));
		row.Set("price", Napi::Number::New(env, ParseDoubleOrZero(priceRaw)));
		row.Set("currency", Napi::String::New(env, currency));
		row.Set("quantity", Napi::Number::New(env, ParseDoubleOrZero(quantityRaw)));
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
