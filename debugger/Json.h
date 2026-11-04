#pragma once

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>


namespace Debugger {


// A JSON value; objects keep their member order.
class JsonValue {
public:
	enum class Type {
		Null,
		Bool,
		Number,
		String,
		Array,
		Object,
	};

	using Array = std::vector<JsonValue>;
	using Object = std::vector<std::pair<std::string, JsonValue>>;

private:
	std::variant<std::nullptr_t, bool, double, std::string, Array, Object> fValue;

	void Serialize(std::string &out) const;

public:
	JsonValue(): fValue(nullptr) {}
	JsonValue(std::nullptr_t): fValue(nullptr) {}
	JsonValue(bool value): fValue(value) {}
	JsonValue(int value): fValue(static_cast<double>(value)) {}
	JsonValue(int64_t value): fValue(static_cast<double>(value)) {}
	JsonValue(double value): fValue(value) {}
	JsonValue(const char *value): fValue(std::string(value)) {}
	JsonValue(std::string value): fValue(std::move(value)) {}
	JsonValue(Array value): fValue(std::move(value)) {}
	JsonValue(Object value): fValue(std::move(value)) {}

	static JsonValue MakeObject() {return JsonValue(Object());}
	static JsonValue MakeArray() {return JsonValue(Array());}

	Type GetType() const {return static_cast<Type>(fValue.index());}
	bool IsNull() const {return GetType() == Type::Null;}
	bool IsObject() const {return GetType() == Type::Object;}
	bool IsArray() const {return GetType() == Type::Array;}
	bool IsString() const {return GetType() == Type::String;}
	bool IsNumber() const {return GetType() == Type::Number;}

	// Accessors return the fallback (or an empty value) for other types.
	bool AsBool(bool fallback = false) const;
	double AsNumber(double fallback = 0) const;
	int64_t AsInt(int64_t fallback = 0) const;
	const std::string &AsString() const;
	const Array &AsArray() const;
	const Object &AsObject() const;

	// Member or element; a null value if missing.
	const JsonValue &operator[](std::string_view key) const;
	const JsonValue &operator[](size_t index) const;
	bool Has(std::string_view key) const;
	size_t Size() const;

	// Builders; turn a null value into an object or array first.
	JsonValue &Set(std::string key, JsonValue value);
	JsonValue &Append(JsonValue value);

	std::string Serialize() const;
	// Nullopt for invalid JSON or trailing garbage.
	static std::optional<JsonValue> Parse(std::string_view text);
};


} // Debugger
