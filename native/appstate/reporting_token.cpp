#include "reporting_token.h"

#include "common.h"
#include "media/internal/cng_crypto.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace baileys_native {

namespace {

struct FieldSpec {
	uint32_t f = 0;
	bool m = false;
	std::vector<FieldSpec> s;
};

struct FieldCfg {
	bool m = false;
	std::unordered_map<uint32_t, FieldCfg> children;
};

struct Varint {
	uint32_t value = 0;
	size_t bytes = 0;
	bool ok = false;
};

struct FieldBytes {
	uint32_t num = 0;
	std::vector<uint8_t> bytes;
};

using FieldMap = std::unordered_map<uint32_t, FieldCfg>;

constexpr uint32_t kWireVarint = 0;
constexpr uint32_t kWireFixed64 = 1;
constexpr uint32_t kWireBytes = 2;
constexpr uint32_t kWireFixed32 = 5;

Varint DecodeVarint(const uint8_t* data, size_t length, size_t offset) {
	uint32_t value = 0;
	size_t bytes = 0;
	uint32_t shift = 0;
	while (offset + bytes < length) {
		const uint8_t current = data[offset + bytes];
		value |= static_cast<uint32_t>(current & 0x7f) << shift;
		bytes++;
		if ((current & 0x80) == 0) {
			return Varint{value, bytes, true};
		}
		shift += 7;
		if (shift > 35) {
			return Varint{};
		}
	}
	return Varint{};
}

void EncodeVarint(uint32_t value, std::vector<uint8_t>* out) {
	uint32_t remaining = value;
	while (remaining > 0x7f) {
		out->push_back(static_cast<uint8_t>((remaining & 0x7f) | 0x80));
		remaining >>= 7;
	}
	out->push_back(static_cast<uint8_t>(remaining));
}

FieldMap CompileSpecs(const std::vector<FieldSpec>& specs) {
	FieldMap map;
	map.reserve(specs.size());
	for (const auto& spec : specs) {
		FieldCfg cfg;
		cfg.m = spec.m;
		cfg.children = CompileSpecs(spec.s);
		map.emplace(spec.f, std::move(cfg));
	}
	return map;
}

const FieldMap& EmptyFieldMap() {
	static const FieldMap kEmpty;
	return kEmpty;
}

const FieldMap& ReportingFieldConfig() {
	static const FieldMap kConfig = CompileSpecs({
		{1, false, {}},
		{3, false, {{2, false, {}}, {3, false, {}}, {8, false, {}}, {11, false, {}}, {17, false, {{21, false, {}}, {22, false, {}}}}, {25, false, {}}}},
		{4, false, {{1, false, {}}, {16, false, {}}, {17, false, {{21, false, {}}, {22, false, {}}}}}},
		{5, false, {{3, false, {}}, {4, false, {}}, {5, false, {}}, {16, false, {}}, {17, false, {{21, false, {}}, {22, false, {}}}}}},
		{6, false, {{1, false, {}}, {17, false, {{21, false, {}}, {22, false, {}}}}, {30, false, {}}}},
		{7, false, {{2, false, {}}, {7, false, {}}, {10, false, {}}, {17, false, {{21, false, {}}, {22, false, {}}}}, {20, false, {}}}},
		{8, false, {{2, false, {}}, {7, false, {}}, {9, false, {}}, {17, false, {{21, false, {}}, {22, false, {}}}}, {21, false, {}}}},
		{9, false, {{2, false, {}}, {6, false, {}}, {7, false, {}}, {13, false, {}}, {17, false, {{21, false, {}}, {22, false, {}}}}, {20, false, {}}}},
		{12, false, {{1, false, {}}, {2, false, {}}, {14, true, {}}, {15, false, {}}}},
		{18, false, {{6, false, {}}, {16, false, {}}, {17, false, {{21, false, {}}, {22, false, {}}}}}},
		{26, false, {{4, false, {}}, {5, false, {}}, {8, false, {}}, {13, false, {}}, {17, false, {{21, false, {}}, {22, false, {}}}}}},
		{28, false, {{1, false, {}}, {2, false, {}}, {4, false, {}}, {5, false, {}}, {6, false, {}}, {7, false, {{21, false, {}}, {22, false, {}}}}}},
		{37, false, {{1, true, {}}}},
		{49, false, {{2, false, {}}, {3, false, {{1, false, {}}, {2, false, {}}}}, {5, false, {{21, false, {}}, {22, false, {}}}}, {8, false, {{1, false, {}}, {2, false, {}}}}}},
		{53, false, {{1, true, {}}}},
		{55, false, {{1, true, {}}}},
		{58, false, {{1, true, {}}}},
		{59, false, {{1, true, {}}}},
		{60, false, {{2, false, {}}, {3, false, {{1, false, {}}, {2, false, {}}}}, {5, false, {{21, false, {}}, {22, false, {}}}}, {8, false, {{1, false, {}}, {2, false, {}}}}}},
		{64, false, {{2, false, {}}, {3, false, {{1, false, {}}, {2, false, {}}}}, {5, false, {{21, false, {}}, {22, false, {}}}}, {8, false, {{1, false, {}}, {2, false, {}}}}}},
		{66, false, {{2, false, {}}, {6, false, {}}, {7, false, {}}, {13, false, {}}, {17, false, {{21, false, {}}, {22, false, {}}}}, {20, false, {}}}},
		{74, false, {{1, true, {}}}},
		{87, false, {{1, true, {}}}},
		{88, false, {{1, false, {}}, {2, false, {{1, false, {}}}}, {3, false, {{21, false, {}}, {22, false, {}}}}}},
		{92, false, {{1, true, {}}}},
		{93, false, {{1, true, {}}}},
		{94, false, {{1, true, {}}}}
	});
	return kConfig;
}

bool ExtractReportingFields(const uint8_t* data, size_t length, const FieldMap& cfg, std::vector<uint8_t>* out) {
	std::vector<FieldBytes> fields;
	size_t i = 0;

	while (i < length) {
		const Varint tag = DecodeVarint(data, length, i);
		if (!tag.ok) return false;

		const uint32_t fieldNum = tag.value >> 3;
		const uint32_t wireType = tag.value & 0x7;

		const size_t fieldStart = i;
		i += tag.bytes;

		const auto cfgIt = cfg.find(fieldNum);
		const FieldCfg* fieldCfg = cfgIt != cfg.end() ? &cfgIt->second : nullptr;

		auto pushSlice = [&](size_t end) -> bool {
			if (end > length) return false;
			FieldBytes fb;
			fb.num = fieldNum;
			fb.bytes.assign(data + fieldStart, data + end);
			fields.push_back(std::move(fb));
			i = end;
			return true;
		};

		auto skip = [&](size_t end) -> bool {
			if (end > length) return false;
			i = end;
			return true;
		};

		if (wireType == kWireVarint) {
			const Varint val = DecodeVarint(data, length, i);
			if (!val.ok) return false;
			const size_t end = i + val.bytes;
			if (!fieldCfg) {
				if (!skip(end)) return false;
				continue;
			}
			if (!pushSlice(end)) return false;
			continue;
		}

		if (wireType == kWireFixed64) {
			const size_t end = i + 8;
			if (!fieldCfg) {
				if (!skip(end)) return false;
				continue;
			}
			if (!pushSlice(end)) return false;
			continue;
		}

		if (wireType == kWireFixed32) {
			const size_t end = i + 4;
			if (!fieldCfg) {
				if (!skip(end)) return false;
				continue;
			}
			if (!pushSlice(end)) return false;
			continue;
		}

		if (wireType == kWireBytes) {
			const Varint len = DecodeVarint(data, length, i);
			if (!len.ok) return false;

			const size_t valueStart = i + len.bytes;
			const size_t valueEnd = valueStart + len.value;
			if (valueEnd > length) return false;

			if (!fieldCfg) {
				i = valueEnd;
				continue;
			}

			if (fieldCfg->m || !fieldCfg->children.empty()) {
				std::vector<uint8_t> sub;
				const FieldMap& childCfg = !fieldCfg->children.empty() ? fieldCfg->children : EmptyFieldMap();
				if (!ExtractReportingFields(data + valueStart, len.value, childCfg, &sub)) {
					return false;
				}

				if (!sub.empty()) {
					FieldBytes fb;
					fb.num = fieldNum;
					EncodeVarint(tag.value, &fb.bytes);
					EncodeVarint(static_cast<uint32_t>(sub.size()), &fb.bytes);
					fb.bytes.insert(fb.bytes.end(), sub.begin(), sub.end());
					fields.push_back(std::move(fb));
				}

				i = valueEnd;
				continue;
			}

			if (!pushSlice(valueEnd)) return false;
			continue;
		}

		return false;
	}

	if (fields.empty()) {
		out->clear();
		return true;
	}

	std::sort(fields.begin(), fields.end(), [](const FieldBytes& a, const FieldBytes& b) { return a.num < b.num; });
	size_t total = 0;
	for (const auto& field : fields) total += field.bytes.size();
	out->clear();
	out->reserve(total);
	for (const auto& field : fields) out->insert(out->end(), field.bytes.begin(), field.bytes.end());
	return true;
}

} // namespace

Napi::Value ExtractReportingTokenContent(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1) {
		Napi::TypeError::New(env, "extractReportingTokenContent(data) requires data").ThrowAsJavaScriptException();
		return env.Null();
	}

	common::ByteView data;
	if (!common::GetByteViewFromValue(env, info[0], "data", &data)) {
		return env.Null();
	}

	std::vector<uint8_t> out;
	const bool ok = ExtractReportingFields(data.data, data.length, ReportingFieldConfig(), &out);
	if (!ok) {
		return env.Null();
	}

	const uint8_t* ptr = out.empty() ? reinterpret_cast<const uint8_t*>("") : out.data();
	return Napi::Buffer<uint8_t>::Copy(env, ptr, out.size());
}

Napi::Value BuildReportingTokenV2(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 2) {
		Napi::TypeError::New(env, "buildReportingTokenV2(data, reportingSecret) requires 2 arguments")
			.ThrowAsJavaScriptException();
		return env.Null();
	}

	common::ByteView data;
	if (!common::GetByteViewFromValue(env, info[0], "data", &data)) {
		return env.Null();
	}

	common::ByteView reportingSecret;
	if (!common::GetByteViewFromValue(env, info[1], "reportingSecret", &reportingSecret)) {
		return env.Null();
	}

	if (reportingSecret.length == 0) {
		return env.Null();
	}

	std::vector<uint8_t> content;
	const bool ok = ExtractReportingFields(data.data, data.length, ReportingFieldConfig(), &content);
	if (!ok || content.empty()) {
		return env.Null();
	}

#ifdef _WIN32
	media_internal::CngHashState hmac;
	std::string error;
	if (!hmac.InitHmacSha256(reportingSecret.data, reportingSecret.length, &error)) {
		Napi::Error::New(env, "buildReportingTokenV2 init failed: " + error).ThrowAsJavaScriptException();
		return env.Null();
	}

	if (!hmac.Update(content.data(), content.size(), &error)) {
		Napi::Error::New(env, "buildReportingTokenV2 update failed: " + error).ThrowAsJavaScriptException();
		return env.Null();
	}

	std::vector<uint8_t> digest;
	if (!hmac.Final(&digest, &error)) {
		Napi::Error::New(env, "buildReportingTokenV2 final failed: " + error).ThrowAsJavaScriptException();
		return env.Null();
	}

	if (digest.size() < 16) {
		Napi::Error::New(env, "buildReportingTokenV2 invalid digest length").ThrowAsJavaScriptException();
		return env.Null();
	}

	return Napi::Buffer<uint8_t>::Copy(env, digest.data(), 16);
#else
	return env.Null();
#endif
}

} // namespace baileys_native
