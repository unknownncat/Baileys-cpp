#include "socket/internal/group_stub_flat_json.h"

#include <cctype>
#include <cstdint>
#include <cstdio>

namespace baileys_native::socket_internal {

namespace {

void AppendEscapedJsonString(const std::string& input, std::string* out) {
	out->push_back('"');
	for (unsigned char ch : input) {
		switch (ch) {
		case '"':
			out->append("\\\"");
			break;
		case '\\':
			out->append("\\\\");
			break;
		case '\b':
			out->append("\\b");
			break;
		case '\f':
			out->append("\\f");
			break;
		case '\n':
			out->append("\\n");
			break;
		case '\r':
			out->append("\\r");
			break;
		case '\t':
			out->append("\\t");
			break;
		default:
			if (ch < 0x20u) {
				char escape[7];
				std::snprintf(escape, sizeof(escape), "\\u%04x", static_cast<unsigned int>(ch));
				out->append(escape);
			} else {
				out->push_back(static_cast<char>(ch));
			}
			break;
		}
	}
	out->push_back('"');
}

void SkipWhitespace(const std::string& input, size_t* cursor) {
	while (*cursor < input.size() && std::isspace(static_cast<unsigned char>(input[*cursor]))) {
		++(*cursor);
	}
}

bool ParseHex4(const std::string& input, size_t* cursor, uint32_t* out) {
	if (*cursor + 4 > input.size()) {
		return false;
	}

	uint32_t value = 0;
	for (int i = 0; i < 4; ++i) {
		const char ch = input[*cursor + i];
		uint32_t digit = 0;
		if (ch >= '0' && ch <= '9') {
			digit = static_cast<uint32_t>(ch - '0');
		} else if (ch >= 'a' && ch <= 'f') {
			digit = static_cast<uint32_t>(ch - 'a' + 10);
		} else if (ch >= 'A' && ch <= 'F') {
			digit = static_cast<uint32_t>(ch - 'A' + 10);
		} else {
			return false;
		}
		value = (value << 4u) | digit;
	}

	*cursor += 4;
	*out = value;
	return true;
}

void AppendCodePointUtf8(uint32_t codePoint, std::string* out) {
	if (codePoint <= 0x7fu) {
		out->push_back(static_cast<char>(codePoint));
		return;
	}

	if (codePoint <= 0x7ffu) {
		out->push_back(static_cast<char>(0xc0u | ((codePoint >> 6u) & 0x1fu)));
		out->push_back(static_cast<char>(0x80u | (codePoint & 0x3fu)));
		return;
	}

	if (codePoint <= 0xffffu) {
		out->push_back(static_cast<char>(0xe0u | ((codePoint >> 12u) & 0x0fu)));
		out->push_back(static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3fu)));
		out->push_back(static_cast<char>(0x80u | (codePoint & 0x3fu)));
		return;
	}

	out->push_back(static_cast<char>(0xf0u | ((codePoint >> 18u) & 0x07u)));
	out->push_back(static_cast<char>(0x80u | ((codePoint >> 12u) & 0x3fu)));
	out->push_back(static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3fu)));
	out->push_back(static_cast<char>(0x80u | (codePoint & 0x3fu)));
}

bool ParseJsonString(const std::string& input, size_t* cursor, std::string* out) {
	if (*cursor >= input.size() || input[*cursor] != '"') {
		return false;
	}

	++(*cursor);
	out->clear();

	while (*cursor < input.size()) {
		const char ch = input[*cursor];
		++(*cursor);

		if (ch == '"') {
			return true;
		}

		if (ch == '\\') {
			if (*cursor >= input.size()) {
				return false;
			}

			const char esc = input[*cursor];
			++(*cursor);
			switch (esc) {
			case '"':
			case '\\':
			case '/':
				out->push_back(esc);
				break;
			case 'b':
				out->push_back('\b');
				break;
			case 'f':
				out->push_back('\f');
				break;
			case 'n':
				out->push_back('\n');
				break;
			case 'r':
				out->push_back('\r');
				break;
			case 't':
				out->push_back('\t');
				break;
			case 'u': {
				uint32_t first = 0;
				if (!ParseHex4(input, cursor, &first)) {
					return false;
				}

				if (first >= 0xd800u && first <= 0xdbffu) {
					if (*cursor + 1 >= input.size() || input[*cursor] != '\\' || input[*cursor + 1] != 'u') {
						return false;
					}
					*cursor += 2;
					uint32_t second = 0;
					if (!ParseHex4(input, cursor, &second)) {
						return false;
					}
					if (second < 0xdc00u || second > 0xdfffu) {
						return false;
					}

					const uint32_t codePoint = 0x10000u + (((first - 0xd800u) << 10u) | (second - 0xdc00u));
					AppendCodePointUtf8(codePoint, out);
					break;
				}

				if (first >= 0xdc00u && first <= 0xdfffu) {
					return false;
				}

				AppendCodePointUtf8(first, out);
				break;
			}
			default:
				return false;
			}
			continue;
		}

		if (static_cast<unsigned char>(ch) < 0x20u) {
			return false;
		}

		out->push_back(ch);
	}

	return false;
}

} // namespace

std::string EncodeFlatObject(const std::vector<FlatField>& fields) {
	std::string out;
	out.reserve(fields.size() * 12u);
	out.push_back('{');

	for (size_t i = 0; i < fields.size(); ++i) {
		if (i > 0) {
			out.push_back(',');
		}

		AppendEscapedJsonString(fields[i].first, &out);
		out.push_back(':');
		if (fields[i].second.has_value()) {
			AppendEscapedJsonString(fields[i].second.value(), &out);
		} else {
			out.append("null");
		}
	}

	out.push_back('}');
	return out;
}

bool ParseFlatObject(const std::string& input, std::vector<FlatField>* out) {
	out->clear();
	size_t cursor = 0;
	SkipWhitespace(input, &cursor);
	if (cursor >= input.size() || input[cursor] != '{') {
		return false;
	}

	++cursor;
	SkipWhitespace(input, &cursor);
	if (cursor < input.size() && input[cursor] == '}') {
		++cursor;
		SkipWhitespace(input, &cursor);
		return cursor == input.size();
	}

	while (cursor < input.size()) {
		std::string key;
		if (!ParseJsonString(input, &cursor, &key)) {
			return false;
		}

		SkipWhitespace(input, &cursor);
		if (cursor >= input.size() || input[cursor] != ':') {
			return false;
		}

		++cursor;
		SkipWhitespace(input, &cursor);

		std::optional<std::string> value;
		if (cursor < input.size() && input[cursor] == '"') {
			std::string parsed;
			if (!ParseJsonString(input, &cursor, &parsed)) {
				return false;
			}
			value = std::move(parsed);
		} else if (cursor + 4 <= input.size() && input.compare(cursor, 4, "null") == 0) {
			cursor += 4;
			value.reset();
		} else {
			return false;
		}

		out->push_back({std::move(key), std::move(value)});
		SkipWhitespace(input, &cursor);
		if (cursor >= input.size()) {
			return false;
		}

		if (input[cursor] == ',') {
			++cursor;
			SkipWhitespace(input, &cursor);
			continue;
		}

		if (input[cursor] == '}') {
			++cursor;
			SkipWhitespace(input, &cursor);
			return cursor == input.size();
		}

		return false;
	}

	return false;
}

} // namespace baileys_native::socket_internal
