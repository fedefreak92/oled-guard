#include "json.h"

#include <charconv>
#include <cmath>
#include <cstdint>

namespace oled {
namespace {

constexpr int kMaxDepth = 64;

class Parser {
 public:
  explicit Parser(std::string_view text) : text_(text) {}

  JsonParseResult Run() {
    JsonParseResult result;
    if (text_.substr(0, 3) == "\xEF\xBB\xBF") pos_ = 3;
    SkipSpace();
    JsonValue value;
    if (ParseValue(value, 0)) {
      SkipSpace();
      if (pos_ == text_.size()) {
        result.value = std::move(value);
        return result;
      }
      Fail("caratteri in eccesso dopo il valore");
    }
    result.error = error_;
    result.offset = error_offset_;
    return result;
  }

 private:
  bool Fail(const char* message) {
    if (error_.empty()) {
      error_ = std::string(message) + " (posizione " + std::to_string(pos_) + ")";
      error_offset_ = pos_;
    }
    return false;
  }

  void SkipSpace() {
    while (pos_ < text_.size() &&
           (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r')) {
      ++pos_;
    }
  }

  bool Consume(std::string_view word) {
    if (text_.substr(pos_, word.size()) != word) return false;
    pos_ += word.size();
    return true;
  }

  bool ParseValue(JsonValue& out, int depth) {
    if (pos_ >= text_.size()) return Fail("fine del testo inattesa");
    const char c = text_[pos_];
    if (c == '{') return ParseObject(out, depth + 1);
    if (c == '[') return ParseArray(out, depth + 1);
    if (c == '"') {
      std::string s;
      if (!ParseString(s)) return false;
      out = JsonValue(std::move(s));
      return true;
    }
    if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(out);
    if (Consume("true")) { out = JsonValue(true); return true; }
    if (Consume("false")) { out = JsonValue(false); return true; }
    if (Consume("null")) { out = JsonValue(nullptr); return true; }
    return Fail("valore non valido");
  }

  bool ParseObject(JsonValue& out, int depth) {
    if (depth > kMaxDepth) return Fail("annidamento troppo profondo");
    ++pos_;  // {
    out = JsonValue::MakeObject();
    SkipSpace();
    if (pos_ < text_.size() && text_[pos_] == '}') { ++pos_; return true; }
    for (;;) {
      SkipSpace();
      if (pos_ >= text_.size() || text_[pos_] != '"') return Fail("attesa una chiave tra virgolette");
      std::string key;
      if (!ParseString(key)) return false;
      SkipSpace();
      if (pos_ >= text_.size() || text_[pos_] != ':') return Fail("atteso ':'");
      ++pos_;
      SkipSpace();
      JsonValue value;
      if (!ParseValue(value, depth)) return false;
      out.Set(std::move(key), std::move(value));
      SkipSpace();
      if (pos_ < text_.size() && text_[pos_] == ',') { ++pos_; continue; }
      if (pos_ < text_.size() && text_[pos_] == '}') { ++pos_; return true; }
      return Fail("atteso ',' o '}'");
    }
  }

  bool ParseArray(JsonValue& out, int depth) {
    if (depth > kMaxDepth) return Fail("annidamento troppo profondo");
    ++pos_;  // [
    out = JsonValue::MakeArray();
    SkipSpace();
    if (pos_ < text_.size() && text_[pos_] == ']') { ++pos_; return true; }
    for (;;) {
      SkipSpace();
      JsonValue value;
      if (!ParseValue(value, depth)) return false;
      out.Push(std::move(value));
      SkipSpace();
      if (pos_ < text_.size() && text_[pos_] == ',') { ++pos_; continue; }
      if (pos_ < text_.size() && text_[pos_] == ']') { ++pos_; return true; }
      return Fail("atteso ',' o ']'");
    }
  }

  bool ParseHex4(uint32_t& out) {
    if (pos_ + 4 > text_.size()) return Fail("sequenza \\u incompleta");
    out = 0;
    for (int i = 0; i < 4; ++i) {
      const char h = text_[pos_++];
      out <<= 4;
      if (h >= '0' && h <= '9') out |= static_cast<uint32_t>(h - '0');
      else if (h >= 'a' && h <= 'f') out |= static_cast<uint32_t>(h - 'a' + 10);
      else if (h >= 'A' && h <= 'F') out |= static_cast<uint32_t>(h - 'A' + 10);
      else return Fail("cifra esadecimale non valida");
    }
    return true;
  }

  static void AppendUtf8(std::string& s, uint32_t cp) {
    if (cp < 0x80) {
      s += static_cast<char>(cp);
    } else if (cp < 0x800) {
      s += static_cast<char>(0xC0 | (cp >> 6));
      s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      s += static_cast<char>(0xE0 | (cp >> 12));
      s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      s += static_cast<char>(0xF0 | (cp >> 18));
      s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      s += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  bool ParseString(std::string& out) {
    ++pos_;  // "
    for (;;) {
      if (pos_ >= text_.size()) return Fail("stringa non terminata");
      const char c = text_[pos_++];
      if (c == '"') return true;
      if (static_cast<unsigned char>(c) < 0x20) return Fail("carattere di controllo nella stringa");
      if (c != '\\') {
        out += c;
        continue;
      }
      if (pos_ >= text_.size()) return Fail("stringa non terminata");
      const char e = text_[pos_++];
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
          uint32_t cp = 0;
          if (!ParseHex4(cp)) return false;
          if (cp >= 0xD800 && cp <= 0xDBFF) {
            uint32_t low = 0;
            if (!Consume("\\u") || !ParseHex4(low) || low < 0xDC00 || low > 0xDFFF) {
              return Fail("coppia surrogata non valida");
            }
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return Fail("coppia surrogata non valida");
          }
          AppendUtf8(out, cp);
          break;
        }
        default:
          return Fail("sequenza di escape non valida");
      }
    }
  }

  bool ParseNumber(JsonValue& out) {
    const size_t start = pos_;
    auto digit = [&](size_t i) { return i < text_.size() && text_[i] >= '0' && text_[i] <= '9'; };
    if (text_[pos_] == '-') ++pos_;
    if (!digit(pos_)) return Fail("numero non valido");
    if (text_[pos_] == '0') {
      ++pos_;
      if (digit(pos_)) return Fail("zeri iniziali non ammessi");
    } else {
      while (digit(pos_)) ++pos_;
    }
    if (pos_ < text_.size() && text_[pos_] == '.') {
      ++pos_;
      if (!digit(pos_)) return Fail("numero non valido");
      while (digit(pos_)) ++pos_;
    }
    if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
      if (!digit(pos_)) return Fail("esponente non valido");
      while (digit(pos_)) ++pos_;
    }
    double value = 0;
    const auto [ptr, ec] = std::from_chars(text_.data() + start, text_.data() + pos_, value);
    if (ec != std::errc() || ptr != text_.data() + pos_ || !std::isfinite(value)) {
      return Fail("numero fuori intervallo");
    }
    out = JsonValue(value);
    return true;
  }

  std::string_view text_;
  size_t pos_ = 0;
  std::string error_;
  size_t error_offset_ = 0;
};

void SerializeString(const std::string& s, std::string& out) {
  static const char* kHex = "0123456789abcdef";
  out += '"';
  for (const char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          out += "\\u00";
          out += kHex[(c >> 4) & 0xF];
          out += kHex[c & 0xF];
        } else {
          out += c;
        }
    }
  }
  out += '"';
}

void SerializeNumber(double n, std::string& out) {
  char buffer[64];
  std::to_chars_result r;
  if (std::floor(n) == n && std::fabs(n) < 1e15) {
    r = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<int64_t>(n));
  } else {
    r = std::to_chars(buffer, buffer + sizeof(buffer), n);
  }
  out.append(buffer, r.ptr);
}

void Serialize(const JsonValue& v, int indent, int level, std::string& out) {
  const auto newline = [&](int lvl) {
    if (indent <= 0) return;
    out += '\n';
    out.append(static_cast<size_t>(indent * lvl), ' ');
  };
  switch (v.type()) {
    case JsonValue::Type::Null: out += "null"; break;
    case JsonValue::Type::Bool: out += v.AsBool() ? "true" : "false"; break;
    case JsonValue::Type::Number: SerializeNumber(v.AsNumber(), out); break;
    case JsonValue::Type::String: SerializeString(v.AsString(), out); break;
    case JsonValue::Type::Array: {
      if (v.AsArray().empty()) { out += "[]"; break; }
      out += '[';
      bool first = true;
      for (const auto& item : v.AsArray()) {
        if (!first) out += ',';
        first = false;
        newline(level + 1);
        Serialize(item, indent, level + 1, out);
      }
      newline(level);
      out += ']';
      break;
    }
    case JsonValue::Type::Object: {
      if (v.AsObject().empty()) { out += "{}"; break; }
      out += '{';
      bool first = true;
      for (const auto& [key, item] : v.AsObject()) {
        if (!first) out += ',';
        first = false;
        newline(level + 1);
        SerializeString(key, out);
        out += indent > 0 ? ": " : ":";
        Serialize(item, indent, level + 1, out);
      }
      newline(level);
      out += '}';
      break;
    }
  }
}

}  // namespace

JsonValue JsonValue::MakeArray() {
  JsonValue v;
  v.type_ = Type::Array;
  return v;
}

JsonValue JsonValue::MakeObject() {
  JsonValue v;
  v.type_ = Type::Object;
  return v;
}

const JsonValue* JsonValue::Find(std::string_view key) const {
  for (const auto& [k, v] : object_) {
    if (k == key) return &v;
  }
  return nullptr;
}

void JsonValue::Set(std::string key, JsonValue value) {
  for (auto& [k, v] : object_) {
    if (k == key) {
      v = std::move(value);
      return;
    }
  }
  object_.emplace_back(std::move(key), std::move(value));
}

void JsonValue::Push(JsonValue value) { array_.push_back(std::move(value)); }

bool operator==(const JsonValue& a, const JsonValue& b) {
  if (a.type_ != b.type_) return false;
  switch (a.type_) {
    case JsonValue::Type::Null: return true;
    case JsonValue::Type::Bool: return a.bool_ == b.bool_;
    case JsonValue::Type::Number: return a.number_ == b.number_;
    case JsonValue::Type::String: return a.string_ == b.string_;
    case JsonValue::Type::Array: return a.array_ == b.array_;
    case JsonValue::Type::Object: return a.object_ == b.object_;
  }
  return false;
}

JsonParseResult ParseJson(std::string_view utf8) { return Parser(utf8).Run(); }

std::string SerializeJson(const JsonValue& value, int indent) {
  std::string out;
  Serialize(value, indent, 0, out);
  return out;
}

}  // namespace oled
