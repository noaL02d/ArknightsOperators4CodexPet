#pragma once

#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace mini {

struct Json {
  enum class Type { Null, Bool, Number, String, Array, Object };

  Type type = Type::Null;
  bool boolean = false;
  double number = 0.0;
  std::string text;
  std::vector<Json> items;
  std::map<std::string, Json> fields;

  const Json* find(const char* key) const {
    if (type != Type::Object) return nullptr;
    auto it = fields.find(key);
    return it == fields.end() ? nullptr : &it->second;
  }

  const Json* at(size_t index) const {
    if (type != Type::Array || index >= items.size()) return nullptr;
    return &items[index];
  }

  std::string asText(const std::string& fallback = std::string()) const {
    return type == Type::String ? text : fallback;
  }

  double asNumber(double fallback = 0.0) const {
    return type == Type::Number ? number : fallback;
  }

  bool asBool(bool fallback = false) const {
    return type == Type::Bool ? boolean : fallback;
  }
};

class Parser {
 public:
  explicit Parser(const std::string& source)
      : cursor_(source.data()), end_(source.data() + source.size()) {}

  bool parse(Json& out) {
    skipSpace();
    if (!parseValue(out)) return false;
    skipSpace();
    return cursor_ == end_;
  }

  const std::string& error() const { return error_; }

 private:
  const char* cursor_;
  const char* end_;
  std::string error_;
  int depth_ = 0;

  bool fail(const char* message) {
    if (error_.empty()) error_ = message;
    return false;
  }

  void skipSpace() {
    while (cursor_ < end_ &&
           (*cursor_ == ' ' || *cursor_ == '\t' || *cursor_ == '\r' || *cursor_ == '\n')) {
      ++cursor_;
    }
  }

  bool literal(const char* word) {
    size_t length = 0;
    while (word[length]) ++length;
    if (static_cast<size_t>(end_ - cursor_) < length) return false;
    for (size_t i = 0; i < length; ++i) {
      if (cursor_[i] != word[i]) return false;
    }
    cursor_ += length;
    return true;
  }

  bool parseValue(Json& out) {
    if (cursor_ >= end_) return fail("unexpected end of input");
    switch (*cursor_) {
      case '{':
        return parseObject(out);
      case '[':
        return parseArray(out);
      case '"':
        out.type = Json::Type::String;
        return parseString(out.text);
      case 't':
        if (!literal("true")) return fail("invalid literal");
        out.type = Json::Type::Bool;
        out.boolean = true;
        return true;
      case 'f':
        if (!literal("false")) return fail("invalid literal");
        out.type = Json::Type::Bool;
        out.boolean = false;
        return true;
      case 'n':
        if (!literal("null")) return fail("invalid literal");
        out.type = Json::Type::Null;
        return true;
      default:
        return parseNumber(out);
    }
  }

  bool parseNumber(Json& out) {
    const char* start = cursor_;
    if (cursor_ < end_ && (*cursor_ == '-' || *cursor_ == '+')) ++cursor_;
    bool digits = false;
    while (cursor_ < end_ && *cursor_ >= '0' && *cursor_ <= '9') {
      ++cursor_;
      digits = true;
    }
    if (cursor_ < end_ && *cursor_ == '.') {
      ++cursor_;
      while (cursor_ < end_ && *cursor_ >= '0' && *cursor_ <= '9') {
        ++cursor_;
        digits = true;
      }
    }
    if (digits && cursor_ < end_ && (*cursor_ == 'e' || *cursor_ == 'E')) {
      ++cursor_;
      if (cursor_ < end_ && (*cursor_ == '-' || *cursor_ == '+')) ++cursor_;
      while (cursor_ < end_ && *cursor_ >= '0' && *cursor_ <= '9') ++cursor_;
    }
    if (!digits) return fail("expected number");
    std::string token(start, cursor_);
    out.type = Json::Type::Number;
    out.number = std::strtod(token.c_str(), nullptr);
    return true;
  }

  static void appendUtf8(std::string& out, unsigned int code) {
    if (code < 0x80) {
      out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (code >> 6)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (code >> 12)));
      out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (code >> 18)));
      out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
  }

  bool readHex4(unsigned int& value) {
    if (end_ - cursor_ < 4) return false;
    value = 0;
    for (int i = 0; i < 4; ++i) {
      char c = cursor_[i];
      value <<= 4;
      if (c >= '0' && c <= '9') value |= static_cast<unsigned int>(c - '0');
      else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned int>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned int>(c - 'A' + 10);
      else return false;
    }
    cursor_ += 4;
    return true;
  }

  bool parseString(std::string& out) {
    if (cursor_ >= end_ || *cursor_ != '"') return fail("expected string");
    ++cursor_;
    out.clear();
    while (cursor_ < end_) {
      char c = *cursor_++;
      if (c == '"') return true;
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (cursor_ >= end_) return fail("unterminated escape");
      char escape = *cursor_++;
      switch (escape) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          unsigned int code = 0;
          if (!readHex4(code)) return fail("invalid unicode escape");
          if (code >= 0xD800 && code <= 0xDBFF && end_ - cursor_ >= 6 && cursor_[0] == '\\' &&
              cursor_[1] == 'u') {
            cursor_ += 2;
            unsigned int low = 0;
            if (!readHex4(low)) return fail("invalid unicode escape");
            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
          }
          appendUtf8(out, code);
          break;
        }
        default:
          return fail("invalid escape");
      }
    }
    return fail("unterminated string");
  }

  bool parseArray(Json& out) {
    if (++depth_ > 64) return fail("nesting too deep");
    ++cursor_;
    out.type = Json::Type::Array;
    skipSpace();
    if (cursor_ < end_ && *cursor_ == ']') {
      ++cursor_;
      --depth_;
      return true;
    }
    while (true) {
      Json item;
      skipSpace();
      if (!parseValue(item)) return false;
      out.items.push_back(std::move(item));
      skipSpace();
      if (cursor_ >= end_) return fail("unterminated array");
      if (*cursor_ == ',') {
        ++cursor_;
        continue;
      }
      if (*cursor_ == ']') {
        ++cursor_;
        --depth_;
        return true;
      }
      return fail("invalid array");
    }
  }

  bool parseObject(Json& out) {
    if (++depth_ > 64) return fail("nesting too deep");
    ++cursor_;
    out.type = Json::Type::Object;
    skipSpace();
    if (cursor_ < end_ && *cursor_ == '}') {
      ++cursor_;
      --depth_;
      return true;
    }
    while (true) {
      skipSpace();
      std::string key;
      if (!parseString(key)) return false;
      skipSpace();
      if (cursor_ >= end_ || *cursor_ != ':') return fail("expected ':'");
      ++cursor_;
      skipSpace();
      Json value;
      if (!parseValue(value)) return false;
      out.fields[key] = std::move(value);
      skipSpace();
      if (cursor_ >= end_) return fail("unterminated object");
      if (*cursor_ == ',') {
        ++cursor_;
        continue;
      }
      if (*cursor_ == '}') {
        ++cursor_;
        --depth_;
        return true;
      }
      return fail("invalid object");
    }
  }
};

inline bool parseJson(const std::string& source, Json& out, std::string* error = nullptr) {
  Parser parser(source);
  if (parser.parse(out)) return true;
  if (error) *error = parser.error();
  return false;
}

}  // namespace mini
