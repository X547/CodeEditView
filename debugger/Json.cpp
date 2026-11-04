#include "Json.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>


namespace Debugger {


static constexpr int kMaxDepth = 256;


static const JsonValue &NullValue()
{
	static const JsonValue value;
	return value;
}


bool JsonValue::AsBool(bool fallback) const
{
	const bool *value = std::get_if<bool>(&fValue);
	return value != nullptr ? *value : fallback;
}


double JsonValue::AsNumber(double fallback) const
{
	const double *value = std::get_if<double>(&fValue);
	return value != nullptr ? *value : fallback;
}


int64_t JsonValue::AsInt(int64_t fallback) const
{
	const double *value = std::get_if<double>(&fValue);
	return value != nullptr ? static_cast<int64_t>(*value) : fallback;
}


const std::string &JsonValue::AsString() const
{
	static const std::string empty;
	const std::string *value = std::get_if<std::string>(&fValue);
	return value != nullptr ? *value : empty;
}


const JsonValue::Array &JsonValue::AsArray() const
{
	static const Array empty;
	const Array *value = std::get_if<Array>(&fValue);
	return value != nullptr ? *value : empty;
}


const JsonValue::Object &JsonValue::AsObject() const
{
	static const Object empty;
	const Object *value = std::get_if<Object>(&fValue);
	return value != nullptr ? *value : empty;
}


const JsonValue &JsonValue::operator[](std::string_view key) const
{
	for (const auto &[name, value] : AsObject()) {
		if (name == key) {
			return value;
		}
	}
	return NullValue();
}


const JsonValue &JsonValue::operator[](size_t index) const
{
	const Array &array = AsArray();
	return index < array.size() ? array[index] : NullValue();
}


bool JsonValue::Has(std::string_view key) const
{
	for (const auto &member : AsObject()) {
		if (member.first == key) {
			return true;
		}
	}
	return false;
}


size_t JsonValue::Size() const
{
	if (const Array *array = std::get_if<Array>(&fValue)) {
		return array->size();
	}
	if (const Object *object = std::get_if<Object>(&fValue)) {
		return object->size();
	}
	return 0;
}


JsonValue &JsonValue::Set(std::string key, JsonValue value)
{
	if (IsNull()) {
		fValue = Object();
	}
	Object &object = std::get<Object>(fValue);
	for (auto &member : object) {
		if (member.first == key) {
			member.second = std::move(value);
			return *this;
		}
	}
	object.emplace_back(std::move(key), std::move(value));
	return *this;
}


JsonValue &JsonValue::Append(JsonValue value)
{
	if (IsNull()) {
		fValue = Array();
	}
	std::get<Array>(fValue).push_back(std::move(value));
	return *this;
}


//#pragma mark - Serialization

static void SerializeString(std::string_view text, std::string &out)
{
	out += '"';
	for (const char ch : text) {
		switch (ch) {
			case '"':
				out += "\\\"";
				break;
			case '\\':
				out += "\\\\";
				break;
			case '\n':
				out += "\\n";
				break;
			case '\r':
				out += "\\r";
				break;
			case '\t':
				out += "\\t";
				break;
			case '\b':
				out += "\\b";
				break;
			case '\f':
				out += "\\f";
				break;
			default:
				if (static_cast<unsigned char>(ch) < 0x20) {
					char buffer[8];
					snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(ch));
					out += buffer;
				} else {
					out += ch;
				}
				break;
		}
	}
	out += '"';
}


void JsonValue::Serialize(std::string &out) const
{
	switch (GetType()) {
		case Type::Null:
			out += "null";
			break;
		case Type::Bool:
			out += AsBool() ? "true" : "false";
			break;
		case Type::Number: {
			const double value = AsNumber();
			char buffer[32];
			if (!isfinite(value)) {
				out += "null";
				break;
			}
			if (value == floor(value) && fabs(value) < 1e15) {
				snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
			} else {
				snprintf(buffer, sizeof(buffer), "%.17g", value);
			}
			out += buffer;
			break;
		}
		case Type::String:
			SerializeString(AsString(), out);
			break;
		case Type::Array: {
			out += '[';
			bool first = true;
			for (const JsonValue &value : AsArray()) {
				if (!first) {
					out += ',';
				}
				first = false;
				value.Serialize(out);
			}
			out += ']';
			break;
		}
		case Type::Object: {
			out += '{';
			bool first = true;
			for (const auto &[key, value] : AsObject()) {
				if (!first) {
					out += ',';
				}
				first = false;
				SerializeString(key, out);
				out += ':';
				value.Serialize(out);
			}
			out += '}';
			break;
		}
	}
}


std::string JsonValue::Serialize() const
{
	std::string out;
	Serialize(out);
	return out;
}


//#pragma mark - Parsing

namespace {


class Parser {
private:
	std::string_view fText;
	size_t fPos {};

	void SkipWhitespace()
	{
		while (fPos < fText.size()
			&& (fText[fPos] == ' ' || fText[fPos] == '\t' || fText[fPos] == '\n' || fText[fPos] == '\r')) {
			fPos++;
		}
	}

	bool Consume(std::string_view token)
	{
		if (fText.substr(fPos, token.size()) != token) {
			return false;
		}
		fPos += token.size();
		return true;
	}

	static void AppendUtf8(char32_t codePoint, std::string &out)
	{
		if (codePoint < 0x80) {
			out += static_cast<char>(codePoint);
		} else if (codePoint < 0x800) {
			out += static_cast<char>(0xc0 | (codePoint >> 6));
			out += static_cast<char>(0x80 | (codePoint & 0x3f));
		} else if (codePoint < 0x10000) {
			out += static_cast<char>(0xe0 | (codePoint >> 12));
			out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f));
			out += static_cast<char>(0x80 | (codePoint & 0x3f));
		} else {
			out += static_cast<char>(0xf0 | (codePoint >> 18));
			out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f));
			out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f));
			out += static_cast<char>(0x80 | (codePoint & 0x3f));
		}
	}

	std::optional<char32_t> ParseHex4()
	{
		if (fPos + 4 > fText.size()) {
			return std::nullopt;
		}
		char32_t value = 0;
		for (int i = 0; i < 4; i++) {
			const char ch = fText[fPos++];
			value <<= 4;
			if (ch >= '0' && ch <= '9') {
				value |= ch - '0';
			} else if (ch >= 'a' && ch <= 'f') {
				value |= ch - 'a' + 10;
			} else if (ch >= 'A' && ch <= 'F') {
				value |= ch - 'A' + 10;
			} else {
				return std::nullopt;
			}
		}
		return value;
	}

	std::optional<std::string> ParseString()
	{
		if (!Consume("\"")) {
			return std::nullopt;
		}
		std::string out;
		while (fPos < fText.size()) {
			const char ch = fText[fPos++];
			if (ch == '"') {
				return out;
			}
			if (ch != '\\') {
				out += ch;
				continue;
			}
			if (fPos >= fText.size()) {
				return std::nullopt;
			}
			const char escape = fText[fPos++];
			switch (escape) {
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'u': {
					std::optional<char32_t> codePoint = ParseHex4();
					if (!codePoint.has_value()) {
						return std::nullopt;
					}
					// A surrogate pair encodes one code point.
					if (*codePoint >= 0xd800 && *codePoint < 0xdc00 && Consume("\\u")) {
						const std::optional<char32_t> low = ParseHex4();
						if (!low.has_value() || *low < 0xdc00 || *low >= 0xe000) {
							return std::nullopt;
						}
						*codePoint = 0x10000 + ((*codePoint - 0xd800) << 10) + (*low - 0xdc00);
					}
					AppendUtf8(*codePoint, out);
					break;
				}
				default:
					return std::nullopt;
			}
		}
		return std::nullopt;
	}

	std::optional<JsonValue> ParseNumber()
	{
		const size_t start = fPos;
		if (fPos < fText.size() && fText[fPos] == '-') {
			fPos++;
		}
		while (fPos < fText.size() && (isdigit(static_cast<unsigned char>(fText[fPos]))
				|| fText[fPos] == '.' || fText[fPos] == 'e' || fText[fPos] == 'E'
				|| fText[fPos] == '+' || fText[fPos] == '-')) {
			fPos++;
		}
		const std::string number(fText.substr(start, fPos - start));
		char *end = nullptr;
		const double value = strtod(number.c_str(), &end);
		if (number.empty() || end != number.c_str() + number.size()) {
			return std::nullopt;
		}
		return JsonValue(value);
	}

public:
	explicit Parser(std::string_view text): fText(text) {}

	std::optional<JsonValue> ParseValue(int depth)
	{
		if (depth > kMaxDepth) {
			return std::nullopt;
		}
		SkipWhitespace();
		if (fPos >= fText.size()) {
			return std::nullopt;
		}
		const char ch = fText[fPos];
		if (ch == '{') {
			fPos++;
			JsonValue object = JsonValue::MakeObject();
			SkipWhitespace();
			if (Consume("}")) {
				return object;
			}
			for (;;) {
				SkipWhitespace();
				std::optional<std::string> key = ParseString();
				SkipWhitespace();
				if (!key.has_value() || !Consume(":")) {
					return std::nullopt;
				}
				std::optional<JsonValue> value = ParseValue(depth + 1);
				if (!value.has_value()) {
					return std::nullopt;
				}
				object.Set(std::move(*key), std::move(*value));
				SkipWhitespace();
				if (Consume("}")) {
					return object;
				}
				if (!Consume(",")) {
					return std::nullopt;
				}
			}
		}
		if (ch == '[') {
			fPos++;
			JsonValue array = JsonValue::MakeArray();
			SkipWhitespace();
			if (Consume("]")) {
				return array;
			}
			for (;;) {
				std::optional<JsonValue> value = ParseValue(depth + 1);
				if (!value.has_value()) {
					return std::nullopt;
				}
				array.Append(std::move(*value));
				SkipWhitespace();
				if (Consume("]")) {
					return array;
				}
				if (!Consume(",")) {
					return std::nullopt;
				}
			}
		}
		if (ch == '"') {
			std::optional<std::string> text = ParseString();
			if (!text.has_value()) {
				return std::nullopt;
			}
			return JsonValue(std::move(*text));
		}
		if (Consume("true")) {
			return JsonValue(true);
		}
		if (Consume("false")) {
			return JsonValue(false);
		}
		if (Consume("null")) {
			return JsonValue();
		}
		return ParseNumber();
	}

	bool AtEnd()
	{
		SkipWhitespace();
		return fPos == fText.size();
	}
};


} // namespace


std::optional<JsonValue> JsonValue::Parse(std::string_view text)
{
	Parser parser(text);
	std::optional<JsonValue> value = parser.ParseValue(0);
	if (!value.has_value() || !parser.AtEnd()) {
		return std::nullopt;
	}
	return value;
}


} // Debugger
