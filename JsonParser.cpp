#include "pch.h"
#include "JsonParser.h"
#include <cstdlib>
#include <cstring>
#include <stdexcept>

const JsonValue JsonValue::s_null;

const JsonValue& JsonValue::operator[](const std::string& key) const
{
    if (m_type == Object)
    {
        auto it = m_object.find(key);
        if (it != m_object.end())
            return it->second;
    }
    return s_null;
}

const JsonValue& JsonValue::operator[](size_t index) const
{
    if (m_type == Array && index < m_array.size())
        return m_array[index];
    return s_null;
}

bool JsonValue::Has(const std::string& key) const
{
    if (m_type == Object)
        return m_object.find(key) != m_object.end();
    return false;
}

// -------- JSON Parser --------

namespace {

class JsonParser
{
public:
    JsonParser(const char* data, size_t len) : m_data(data), m_len(len), m_pos(0) {}

    JsonValue Parse()
    {
        SkipWhitespace();
        if (m_pos >= m_len) return JsonValue();
        JsonValue val = ParseValue();
        SkipWhitespace();
        return val;
    }

private:
    const char* m_data;
    size_t m_len;
    size_t m_pos;

    char Peek() const { return m_pos < m_len ? m_data[m_pos] : '\0'; }
    char Next() { return m_pos < m_len ? m_data[m_pos++] : '\0'; }
    bool HasMore() const { return m_pos < m_len; }

    void SkipWhitespace()
    {
        while (m_pos < m_len && (m_data[m_pos] == ' ' || m_data[m_pos] == '\t'
               || m_data[m_pos] == '\n' || m_data[m_pos] == '\r'))
            m_pos++;
    }

    JsonValue ParseValue()
    {
        SkipWhitespace();
        char c = Peek();
        switch (c)
        {
        case '"': return ParseString();
        case '{': return ParseObject();
        case '[': return ParseArray();
        case 't': case 'f': return ParseBool();
        case 'n': return ParseNull();
        default:
            if (c == '-' || (c >= '0' && c <= '9'))
                return ParseNumber();
            return JsonValue();
        }
    }

    JsonValue ParseString()
    {
        Next(); // skip opening quote
        std::string result;
        while (HasMore())
        {
            char c = Next();
            if (c == '"') return JsonValue(result);
            if (c == '\\')
            {
                if (!HasMore()) return JsonValue(result);
                char esc = Next();
                switch (esc)
                {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'u':
                {
                    // Parse \uXXXX (simplified — just store raw for ASCII subset)
                    if (m_pos + 4 <= m_len)
                    {
                        char hex[5] = {0};
                        for (int i = 0; i < 4; i++) hex[i] = Next();
                        unsigned int codepoint = static_cast<unsigned int>(strtoul(hex, nullptr, 16));
                        if (codepoint < 0x80)
                        {
                            result += static_cast<char>(codepoint);
                        }
                        else if (codepoint < 0x800)
                        {
                            result += static_cast<char>(0xC0 | (codepoint >> 6));
                            result += static_cast<char>(0x80 | (codepoint & 0x3F));
                        }
                        else
                        {
                            result += static_cast<char>(0xE0 | (codepoint >> 12));
                            result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                            result += static_cast<char>(0x80 | (codepoint & 0x3F));
                        }
                    }
                    break;
                }
                default: result += esc; break;
                }
            }
            else
            {
                result += c;
            }
        }
        return JsonValue(result);
    }

    JsonValue ParseNumber()
    {
        size_t start = m_pos;
        if (Peek() == '-') Next();
        while (HasMore() && m_data[m_pos] >= '0' && m_data[m_pos] <= '9') Next();
        if (Peek() == '.')
        {
            Next();
            while (HasMore() && m_data[m_pos] >= '0' && m_data[m_pos] <= '9') Next();
        }
        if (Peek() == 'e' || Peek() == 'E')
        {
            Next();
            if (Peek() == '+' || Peek() == '-') Next();
            while (HasMore() && m_data[m_pos] >= '0' && m_data[m_pos] <= '9') Next();
        }
        std::string numStr(m_data + start, m_pos - start);
        double val = strtod(numStr.c_str(), nullptr);
        return JsonValue(val);
    }

    JsonValue ParseBool()
    {
        if (m_pos + 4 <= m_len && strncmp(m_data + m_pos, "true", 4) == 0)
        {
            m_pos += 4;
            return JsonValue(true);
        }
        if (m_pos + 5 <= m_len && strncmp(m_data + m_pos, "false", 5) == 0)
        {
            m_pos += 5;
            return JsonValue(false);
        }
        return JsonValue();
    }

    JsonValue ParseNull()
    {
        if (m_pos + 4 <= m_len && strncmp(m_data + m_pos, "null", 4) == 0)
        {
            m_pos += 4;
            return JsonValue();
        }
        return JsonValue();
    }

    JsonValue ParseObject()
    {
        Next(); // skip '{'
        std::unordered_map<std::string, JsonValue> obj;
        SkipWhitespace();
        if (Peek() == '}')
        {
            Next();
            return JsonValue(std::move(obj));
        }
        while (HasMore())
        {
            SkipWhitespace();
            if (Peek() != '"') break;
            std::string key = ParseString().AsString();
            SkipWhitespace();
            if (Peek() == ':') Next();
            SkipWhitespace();
            JsonValue val = ParseValue();
            obj[key] = std::move(val);
            SkipWhitespace();
            char c = Next();
            if (c == '}') return JsonValue(std::move(obj));
            if (c != ',') break;
        }
        return JsonValue(std::move(obj));
    }

    JsonValue ParseArray()
    {
        Next(); // skip '['
        std::vector<JsonValue> arr;
        SkipWhitespace();
        if (Peek() == ']')
        {
            Next();
            return JsonValue(std::move(arr));
        }
        while (HasMore())
        {
            SkipWhitespace();
            arr.push_back(ParseValue());
            SkipWhitespace();
            char c = Next();
            if (c == ']') return JsonValue(std::move(arr));
            if (c != ',') break;
        }
        return JsonValue(std::move(arr));
    }
};

} // namespace

JsonValue JsonValue::Parse(const std::string& json)
{
    return Parse(json.data(), json.size());
}

JsonValue JsonValue::Parse(const char* json, size_t len)
{
    JsonParser parser(json, len);
    return parser.Parse();
}

const JsonValue* WalkPath(const JsonValue& root, const std::string& dottedPath)
{
    const JsonValue* current = &root;
    if (current->IsNull()) return nullptr;

    size_t start = 0;
    while (start < dottedPath.size())
    {
        size_t dot = dottedPath.find('.', start);
        std::string key = (dot == std::string::npos)
                          ? dottedPath.substr(start)
                          : dottedPath.substr(start, dot - start);

        if (!current->IsObject()) return nullptr;
        if (!current->Has(key)) return nullptr;

        // Navigate into the object — returns pointer to map value (stable)
        current = &(*current)[key];

        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return current;
}
