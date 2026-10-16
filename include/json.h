// json.h - A tiny JSON value type with a parser and serializer.
// Enough for API requests/responses; no external dependency needed.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace memopt {

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    JsonValue() = default;                       // null
    JsonValue(std::nullptr_t) {}
    JsonValue(bool b) : type_(Type::Bool), bool_(b) {}
    JsonValue(int v) : type_(Type::Number), num_(v) {}
    JsonValue(long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    JsonValue(long long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    JsonValue(unsigned v) : type_(Type::Number), num_(v) {}
    JsonValue(unsigned long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    JsonValue(unsigned long long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    JsonValue(double v) : type_(Type::Number), num_(v) {}
    JsonValue(const char* s) : type_(Type::String), str_(s) {}
    JsonValue(std::string s) : type_(Type::String), str_(std::move(s)) {}
    template <typename T>
    JsonValue(const std::optional<T>& o) {
        if (o) *this = JsonValue(*o);
    }

    static JsonValue array() { JsonValue v; v.type_ = Type::Array; return v; }
    static JsonValue object() { JsonValue v; v.type_ = Type::Object; return v; }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool() const { return bool_; }
    double asNumber() const { return num_; }
    const std::string& asString() const { return str_; }
    const std::vector<JsonValue>& items() const { return arr_; }
    const std::vector<std::pair<std::string, JsonValue>>& members() const { return obj_; }

    // Builders
    JsonValue& push(JsonValue v);                          // array append
    JsonValue& set(const std::string& key, JsonValue v);   // object set (replaces)

    // Lookup (object). Returns nullptr when missing or not an object.
    const JsonValue* get(const std::string& key) const;
    bool has(const std::string& key) const { return get(key) != nullptr; }

    std::string dump() const;

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    std::vector<JsonValue> arr_;
    std::vector<std::pair<std::string, JsonValue>> obj_;
};

struct JsonParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Throws JsonParseError on malformed input. Nesting depth is limited.
JsonValue parseJson(const std::string& text);

std::string jsonEscape(const std::string& s);

}  // namespace memopt
