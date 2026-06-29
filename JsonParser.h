#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <cstdint>

// Minimal JSON parser for Clash API responses
class JsonValue
{
public:
    enum Type { Null, Bool, Number, String, Object, Array };

    JsonValue() : m_type(Null), m_number(0) {}
    JsonValue(bool b) : m_type(Bool), m_bool(b), m_number(0) {}
    JsonValue(double n) : m_type(Number), m_number(n) {}
    JsonValue(int64_t n) : m_type(Number), m_number(static_cast<double>(n)) {}
    JsonValue(const std::string& s) : m_type(String), m_string(s), m_number(0) {}
    JsonValue(std::vector<JsonValue> arr) : m_type(Array), m_array(std::move(arr)), m_number(0) {}
    JsonValue(std::unordered_map<std::string, JsonValue> obj)
        : m_type(Object), m_object(std::move(obj)), m_number(0) {}

    Type GetType() const { return m_type; }

    bool IsNull()    const { return m_type == Null; }
    bool IsBool()    const { return m_type == Bool; }
    bool IsNumber()  const { return m_type == Number; }
    bool IsString()  const { return m_type == String; }
    bool IsObject()  const { return m_type == Object; }
    bool IsArray()   const { return m_type == Array; }

    bool AsBool() const { return m_bool; }
    double AsNumber() const { return m_number; }
    int64_t AsInt() const { return static_cast<int64_t>(m_number); }
    const std::string& AsString() const { return m_string; }
    const std::vector<JsonValue>& AsArray() const { return m_array; }
    const std::unordered_map<std::string, JsonValue>& AsObject() const { return m_object; }

    const JsonValue& operator[](const std::string& key) const;
    const JsonValue& operator[](size_t index) const;
    bool Has(const std::string& key) const;

    // Parse JSON string, returns null on error
    static JsonValue Parse(const std::string& json);
    static JsonValue Parse(const char* json, size_t len);

private:
    Type m_type;
    bool m_bool = false;
    double m_number;
    std::string m_string;
    std::vector<JsonValue> m_array;
    std::unordered_map<std::string, JsonValue> m_object;

    static const JsonValue s_null;
};

// Helper: walk nested path like "proxies.Proxy.now"
const JsonValue* WalkPath(const JsonValue& root, const std::string& dottedPath);
