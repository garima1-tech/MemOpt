#include "json.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace memopt {

JsonValue& JsonValue::push(JsonValue v) {
    if (type_ == Type::Null) type_ = Type::Array;
    arr_.push_back(std::move(v));
    return arr_.back();
}

JsonValue& JsonValue::set(const std::string& key, JsonValue v) {
    if (type_ == Type::Null) type_ = Type::Object;
    for (auto& kv : obj_) {
        if (kv.first == key) {
            kv.second = std::move(v);
            return kv.second;
        }
    }
    obj_.emplace_back(key, std::move(v));
    return obj_.back().second;
}

const JsonValue* JsonValue::get(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& kv : obj_)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

static void dumpTo(const JsonValue& v, std::string& out) {
    switch (v.type()) {
        case JsonValue::Type::Null: out += "null"; break;
        case JsonValue::Type::Bool: out += v.asBool() ? "true" : "false"; break;
        case JsonValue::Type::Number: {
            double d = v.asNumber();
            if (!std::isfinite(d)) { out += "null"; break; }
            if (d == std::floor(d) && std::fabs(d) < 1e15) {
                out += std::to_string(static_cast<long long>(d));
            } else {
                char buf[64];
                std::snprintf(buf, sizeof buf, "%.6g", d);
                out += buf;
            }
            break;
        }
        case JsonValue::Type::String:
            out += '"';
            out += jsonEscape(v.asString());
            out += '"';
            break;
        case JsonValue::Type::Array: {
            out += '[';
            bool first = true;
            for (const auto& item : v.items()) {
                if (!first) out += ',';
                first = false;
                dumpTo(item, out);
            }
            out += ']';
            break;
        }
        case JsonValue::Type::Object: {
            out += '{';
            bool first = true;
            for (const auto& kv : v.members()) {
                if (!first) out += ',';
                first = false;
                out += '"';
                out += jsonEscape(kv.first);
                out += "\":";
                dumpTo(kv.second, out);
            }
            out += '}';
            break;
        }
    }
}

std::string JsonValue::dump() const {
    std::string out;
    dumpTo(*this, out);
    return out;
}

// ------------------------------------------------------------- parser ----
namespace {
class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}

    JsonValue parseRoot() {
        JsonValue v = parseValue(0);
        skipWs();
        if (pos_ != s_.size()) fail("unexpected trailing characters");
        return v;
    }

private:
    const std::string& s_;
    size_t pos_ = 0;

    [[noreturn]] void fail(const std::string& msg) {
        throw JsonParseError("invalid JSON at position " + std::to_string(pos_) + ": " + msg);
    }
    void skipWs() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\r' || s_[pos_] == '\t')) ++pos_;
    }
    bool consume(const char* lit) {
        size_t n = std::char_traits<char>::length(lit);
        if (s_.compare(pos_, n, lit) == 0) { pos_ += n; return true; }
        return false;
    }

    JsonValue parseValue(int depth) {
        if (depth > 32) fail("nesting too deep");
        skipWs();
        if (pos_ >= s_.size()) fail("unexpected end");
        char c = s_[pos_];
        if (c == '{') return parseObject(depth);
        if (c == '[') return parseArray(depth);
        if (c == '"') return JsonValue(parseString());
        if (consume("true")) return JsonValue(true);
        if (consume("false")) return JsonValue(false);
        if (consume("null")) return JsonValue();
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        fail(std::string("unexpected character '") + c + "'");
    }

    JsonValue parseNumber() {
        size_t start = pos_;
        if (s_[pos_] == '-') ++pos_;
        while (pos_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '.' ||
                                    s_[pos_] == 'e' || s_[pos_] == 'E' || s_[pos_] == '+' || s_[pos_] == '-'))
            ++pos_;
        std::string tok = s_.substr(start, pos_ - start);
        char* end = nullptr;
        double d = std::strtod(tok.c_str(), &end);
        if (end == tok.c_str() || *end != '\0') fail("bad number");
        return JsonValue(d);
    }

    std::string parseString() {
        ++pos_;  // opening quote
        std::string out;
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return out;
            if (c == '\\') {
                if (pos_ >= s_.size()) fail("bad escape");
                char e = s_[pos_++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        if (pos_ + 4 > s_.size()) fail("bad unicode escape");
                        unsigned cp = static_cast<unsigned>(std::stoul(s_.substr(pos_, 4), nullptr, 16));
                        pos_ += 4;
                        if (cp < 0x80) out += static_cast<char>(cp);
                        else if (cp < 0x800) {
                            out += static_cast<char>(0xC0 | (cp >> 6));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            out += static_cast<char>(0xE0 | (cp >> 12));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: fail("bad escape");
                }
            } else {
                out += c;
            }
        }
        fail("unterminated string");
    }

    JsonValue parseArray(int depth) {
        ++pos_;
        JsonValue arr = JsonValue::array();
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return arr; }
        while (true) {
            arr.push(parseValue(depth + 1));
            skipWs();
            if (pos_ >= s_.size()) fail("unterminated array");
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == ']') { ++pos_; return arr; }
            fail("expected ',' or ']'");
        }
    }

    JsonValue parseObject(int depth) {
        ++pos_;
        JsonValue obj = JsonValue::object();
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return obj; }
        while (true) {
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != '"') fail("expected key string");
            std::string key = parseString();
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != ':') fail("expected ':'");
            ++pos_;
            obj.set(key, parseValue(depth + 1));
            skipWs();
            if (pos_ >= s_.size()) fail("unterminated object");
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == '}') { ++pos_; return obj; }
            fail("expected ',' or '}'");
        }
    }
};
}  // namespace

JsonValue parseJson(const std::string& text) {
    Parser p(text);
    return p.parseRoot();
}

}  // namespace memopt
