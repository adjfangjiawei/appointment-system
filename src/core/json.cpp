#include "chronokit/core/json.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace chronokit {

namespace {

void append_escaped_string(std::string& out, const std::string& s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    out.push_back('"');
}

class Parser {
public:
    explicit Parser(std::string_view src) : src_(src) {}

    JsonValue parse_root() {
        skip_ws();
        JsonValue v = parse_value();
        skip_ws();
        if (pos_ != src_.size()) {
            throw std::runtime_error("JSON trailing characters at position " + std::to_string(pos_));
        }
        return v;
    }

private:
    std::string_view src_;
    size_t pos_ = 0;

    void skip_ws() {
        while (pos_ < src_.size() && std::isspace(static_cast<unsigned char>(src_[pos_]))) {
            ++pos_;
        }
    }

    char peek() const {
        if (pos_ >= src_.size()) throw std::runtime_error("Unexpected end of JSON input");
        return src_[pos_];
    }

    char get() {
        char c = peek();
        ++pos_;
        return c;
    }

    bool match(std::string_view kw) {
        if (src_.substr(pos_, kw.size()) == kw) {
            pos_ += kw.size();
            return true;
        }
        return false;
    }

    JsonValue parse_value() {
        skip_ws();
        char c = peek();
        if (c == 'n') {
            if (match("null")) return JsonValue(nullptr);
            throw std::runtime_error("Invalid token in JSON");
        }
        if (c == 't') {
            if (match("true")) return JsonValue(true);
            throw std::runtime_error("Invalid token in JSON");
        }
        if (c == 'f') {
            if (match("false")) return JsonValue(false);
            throw std::runtime_error("Invalid token in JSON");
        }
        if (c == '"') return JsonValue(parse_string());
        if (c == '[') return parse_array();
        if (c == '{') return parse_object();
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number();
        throw std::runtime_error(std::string("Unexpected character in JSON: ") + c);
    }

    static void append_utf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    uint32_t parse_hex4() {
        if (pos_ + 4 > src_.size()) throw std::runtime_error("Incomplete \\u escape");
        uint32_t val = 0;
        for (int i = 0; i < 4; ++i) {
            char c = src_[pos_++];
            val <<= 4;
            if (c >= '0' && c <= '9') val |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') val |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') val |= static_cast<uint32_t>(c - 'A' + 10);
            else throw std::runtime_error("Invalid hex digit in \\u escape");
        }
        return val;
    }

    std::string parse_string() {
        get(); // consume '"'
        std::string out;
        while (pos_ < src_.size()) {
            char c = get();
            if (c == '"') return out;
            if (c == '\\') {
                char esc = get();
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        uint32_t cp = parse_hex4();
                        if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 2 <= src_.size() &&
                            src_[pos_] == '\\' && src_[pos_ + 1] == 'u') {
                            pos_ += 2;
                            uint32_t low = parse_hex4();
                            cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
                        }
                        append_utf8(out, cp);
                        break;
                    }
                    default:
                        out.push_back(esc);
                        break;
                }
            } else {
                out.push_back(c);
            }
        }
        throw std::runtime_error("Unterminated string in JSON");
    }

    JsonValue parse_number() {
        size_t start = pos_;
        if (src_[pos_] == '-') ++pos_;
        bool is_float = false;
        while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) ++pos_;
        if (pos_ < src_.size() && src_[pos_] == '.') {
            is_float = true;
            ++pos_;
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) ++pos_;
        }
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            is_float = true;
            ++pos_;
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) ++pos_;
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) ++pos_;
        }
        std::string token(src_.substr(start, pos_ - start));
        if (is_float) {
            return JsonValue(std::stod(token));
        }
        return JsonValue(static_cast<int64_t>(std::stoll(token)));
    }

    JsonValue parse_array() {
        get(); // '['
        JsonValue::Array arr;
        skip_ws();
        if (peek() == ']') {
            get();
            return JsonValue(std::move(arr));
        }
        while (true) {
            arr.push_back(parse_value());
            skip_ws();
            char c = get();
            if (c == ']') break;
            if (c != ',') throw std::runtime_error("Expected ',' or ']' in JSON array");
        }
        return JsonValue(std::move(arr));
    }

    JsonValue parse_object() {
        get(); // '{'
        JsonValue::Object obj;
        skip_ws();
        if (peek() == '}') {
            get();
            return JsonValue(std::move(obj));
        }
        while (true) {
            skip_ws();
            if (peek() != '"') throw std::runtime_error("Expected string key in JSON object");
            std::string key = parse_string();
            skip_ws();
            if (get() != ':') throw std::runtime_error("Expected ':' after object key");
            obj[std::move(key)] = parse_value();
            skip_ws();
            char c = get();
            if (c == '}') break;
            if (c != ',') throw std::runtime_error("Expected ',' or '}' in JSON object");
        }
        return JsonValue(std::move(obj));
    }
};

} // namespace

std::string JsonValue::dump(int indent) const {
    std::string out;
    dump_impl(out, indent, 0);
    return out;
}

void JsonValue::dump_impl(std::string& out, int indent, int depth) const {
    if (is_null()) {
        out += "null";
    } else if (is_bool()) {
        out += std::get<bool>(data_) ? "true" : "false";
    } else if (is_int()) {
        out += std::to_string(std::get<int64_t>(data_));
    } else if (is_double()) {
        double v = std::get<double>(data_);
        if (std::isnan(v) || std::isinf(v)) {
            out += "null";
        } else {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.6g", v);
            out += buf;
        }
    } else if (is_string()) {
        append_escaped_string(out, std::get<std::string>(data_));
    } else if (is_array()) {
        const auto& arr = std::get<Array>(data_);
        if (arr.empty()) {
            out += "[]";
            return;
        }
        out.push_back('[');
        for (size_t i = 0; i < arr.size(); ++i) {
            if (indent >= 0) {
                out.push_back('\n');
                out.append(static_cast<size_t>((depth + 1) * indent), ' ');
            }
            arr[i].dump_impl(out, indent, depth + 1);
            if (i + 1 < arr.size()) out.push_back(',');
        }
        if (indent >= 0) {
            out.push_back('\n');
            out.append(static_cast<size_t>(depth * indent), ' ');
        }
        out.push_back(']');
    } else if (is_object()) {
        const auto& obj = std::get<Object>(data_);
        if (obj.empty()) {
            out += "{}";
            return;
        }
        out.push_back('{');
        size_t idx = 0;
        for (const auto& [k, v] : obj) {
            if (indent >= 0) {
                out.push_back('\n');
                out.append(static_cast<size_t>((depth + 1) * indent), ' ');
            }
            append_escaped_string(out, k);
            out.push_back(':');
            if (indent >= 0) out.push_back(' ');
            v.dump_impl(out, indent, depth + 1);
            if (++idx < obj.size()) out.push_back(',');
        }
        if (indent >= 0) {
            out.push_back('\n');
            out.append(static_cast<size_t>(depth * indent), ' ');
        }
        out.push_back('}');
    }
}

JsonValue JsonValue::parse(std::string_view input) {
    Parser p(input);
    return p.parse_root();
}

} // namespace chronokit
