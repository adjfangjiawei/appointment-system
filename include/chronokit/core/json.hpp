#pragma once

#include <cstdint>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace chronokit {

class JsonValue {
public:
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue>;
    using Storage = std::variant<std::nullptr_t, bool, int64_t, double, std::string, Array, Object>;

    JsonValue() : data_(nullptr) {}
    JsonValue(std::nullptr_t) : data_(nullptr) {}
    JsonValue(bool v) : data_(v) {}
    JsonValue(int v) : data_(static_cast<int64_t>(v)) {}
    JsonValue(int64_t v) : data_(v) {}
    JsonValue(size_t v) : data_(static_cast<int64_t>(v)) {}
    JsonValue(double v) : data_(v) {}
    JsonValue(const char* v) : data_(std::string(v ? v : "")) {}
    JsonValue(std::string v) : data_(std::move(v)) {}
    JsonValue(std::string_view v) : data_(std::string(v)) {}
    JsonValue(Array v) : data_(std::move(v)) {}
    JsonValue(Object v) : data_(std::move(v)) {}

    static JsonValue array( std::initializer_list<JsonValue> init = {} ) {
        return JsonValue(Array(init));
    }

    static JsonValue object( std::initializer_list<std::pair<const std::string, JsonValue>> init = {} ) {
        return JsonValue(Object(init));
    }

    bool is_null() const { return std::holds_alternative<std::nullptr_t>(data_); }
    bool is_bool() const { return std::holds_alternative<bool>(data_); }
    bool is_int() const { return std::holds_alternative<int64_t>(data_); }
    bool is_double() const { return std::holds_alternative<double>(data_); }
    bool is_number() const { return is_int() || is_double(); }
    bool is_string() const { return std::holds_alternative<std::string>(data_); }
    bool is_array() const { return std::holds_alternative<Array>(data_); }
    bool is_object() const { return std::holds_alternative<Object>(data_); }

    bool as_bool(bool fallback = false) const {
        if (is_bool()) return std::get<bool>(data_);
        if (is_int()) return std::get<int64_t>(data_) != 0;
        return fallback;
    }

    int64_t as_int(int64_t fallback = 0) const {
        if (is_int()) return std::get<int64_t>(data_);
        if (is_double()) return static_cast<int64_t>(std::get<double>(data_));
        if (is_string()) {
            try { return std::stoll(std::get<std::string>(data_)); } catch (...) { return fallback; }
        }
        return fallback;
    }

    double as_double(double fallback = 0.0) const {
        if (is_double()) return std::get<double>(data_);
        if (is_int()) return static_cast<double>(std::get<int64_t>(data_));
        if (is_string()) {
            try { return std::stod(std::get<std::string>(data_)); } catch (...) { return fallback; }
        }
        return fallback;
    }

    std::string as_string(const std::string& fallback = "") const {
        if (is_string()) return std::get<std::string>(data_);
        if (is_int()) return std::to_string(std::get<int64_t>(data_));
        if (is_double()) return std::to_string(std::get<double>(data_));
        if (is_bool()) return std::get<bool>(data_) ? "true" : "false";
        return fallback;
    }

    const Array& as_array() const {
        static const Array empty;
        return is_array() ? std::get<Array>(data_) : empty;
    }

    Array& as_array_mut() {
        if (!is_array()) data_ = Array{};
        return std::get<Array>(data_);
    }

    const Object& as_object() const {
        static const Object empty;
        return is_object() ? std::get<Object>(data_) : empty;
    }

    Object& as_object_mut() {
        if (!is_object()) data_ = Object{};
        return std::get<Object>(data_);
    }

    void push_back(JsonValue val) {
        as_array_mut().push_back(std::move(val));
    }

    size_t size() const {
        if (is_array()) return std::get<Array>(data_).size();
        if (is_object()) return std::get<Object>(data_).size();
        return 0;
    }

    bool contains(const std::string& key) const {
        if (!is_object()) return false;
        const auto& obj = std::get<Object>(data_);
        return obj.find(key) != obj.end();
    }

    JsonValue& operator[](const std::string& key) {
        return as_object_mut()[key];
    }

    const JsonValue& operator[](const std::string& key) const {
        static const JsonValue null_val;
        if (!is_object()) return null_val;
        const auto& obj = std::get<Object>(data_);
        auto it = obj.find(key);
        return it != obj.end() ? it->second : null_val;
    }

    const JsonValue& operator[](size_t idx) const {
        static const JsonValue null_val;
        if (!is_array()) return null_val;
        const auto& arr = std::get<Array>(data_);
        return idx < arr.size() ? arr[idx] : null_val;
    }

    std::string dump(int indent = -1) const;
    static JsonValue parse(std::string_view input);

private:
    Storage data_;
    void dump_impl(std::string& out, int indent, int depth) const;
};

} // namespace chronokit
