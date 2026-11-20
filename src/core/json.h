// JSON minimale (RFC 8259) scritto a mano: parser a discesa ricorsiva e serializer.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oled {

class JsonValue {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  JsonValue() = default;
  JsonValue(std::nullptr_t) {}
  JsonValue(bool b) : type_(Type::Bool), bool_(b) {}
  JsonValue(double n) : type_(Type::Number), number_(n) {}
  JsonValue(int n) : type_(Type::Number), number_(n) {}
  JsonValue(std::string s) : type_(Type::String), string_(std::move(s)) {}
  JsonValue(const char* s) : type_(Type::String), string_(s) {}

  static JsonValue MakeArray();
  static JsonValue MakeObject();

  Type type() const { return type_; }
  bool AsBool() const { return bool_; }
  double AsNumber() const { return number_; }
  const std::string& AsString() const { return string_; }
  const std::vector<JsonValue>& AsArray() const { return array_; }
  // Ordine delle chiavi conservato.
  const std::vector<std::pair<std::string, JsonValue>>& AsObject() const { return object_; }

  const JsonValue* Find(std::string_view key) const;
  void Set(std::string key, JsonValue value);
  void Push(JsonValue value);

  friend bool operator==(const JsonValue& a, const JsonValue& b);

 private:
  Type type_ = Type::Null;
  bool bool_ = false;
  double number_ = 0;
  std::string string_;
  std::vector<JsonValue> array_;
  std::vector<std::pair<std::string, JsonValue>> object_;
};

struct JsonParseResult {
  std::optional<JsonValue> value;
  std::string error;  // italiano
  size_t offset = 0;
};

// Accetta e salta il BOM UTF-8 (Blocco note lo aggiunge).
JsonParseResult ParseJson(std::string_view utf8);
std::string SerializeJson(const JsonValue& value, int indent = 2);

}  // namespace oled
