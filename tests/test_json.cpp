#include <string>

#include "json.h"
#include "test_framework.h"

using oled::JsonValue;
using oled::ParseJson;
using oled::SerializeJson;

TEST(json_parses_scalars) {
  auto n = ParseJson("null");
  CHECK(n.value && n.value->type() == JsonValue::Type::Null);
  auto t = ParseJson(" true ");
  CHECK(t.value && t.value->type() == JsonValue::Type::Bool && t.value->AsBool());
  auto num = ParseJson("-1.5e2");
  CHECK(num.value && num.value->type() == JsonValue::Type::Number);
  if (num.value) CHECK_NEAR(num.value->AsNumber(), -150.0, 1e-12);
  auto s = ParseJson("\"a\"");
  CHECK(s.value && s.value->AsString() == "a");
}

TEST(json_parses_nested) {
  auto r = ParseJson(R"({"a":[1,{"b":false}], "c": {}})");
  CHECK(r.value.has_value());
  if (!r.value) return;
  const JsonValue* a = r.value->Find("a");
  CHECK(a && a->type() == JsonValue::Type::Array && a->AsArray().size() == 2);
  if (!a || a->AsArray().size() != 2) return;
  const JsonValue* b = a->AsArray()[1].Find("b");
  CHECK(b && b->type() == JsonValue::Type::Bool && b->AsBool() == false);
  CHECK(r.value->Find("missing") == nullptr);
}

TEST(json_string_escapes) {
  auto r = ParseJson(R"("\"\\\n\/\u00e8\ud83d\ude00")");
  CHECK(r.value.has_value());
  if (r.value) CHECK_EQ(r.value->AsString(), std::string("\"\\\n/\xC3\xA8\xF0\x9F\x98\x80"));
}

TEST(json_skips_utf8_bom) {
  auto r = ParseJson("\xEF\xBB\xBF{\"x\":1}");
  CHECK(r.value.has_value());
  if (r.value) {
    const JsonValue* x = r.value->Find("x");
    CHECK(x && x->AsNumber() == 1.0);
  }
}

TEST(json_rejects_garbage) {
  for (const char* bad : {"{", "{\"a\":}", "[1,]", "{\"a\":1}x", "\"\x01\"", "tru", "", "01", "{\"a\" 1}",
                          "\"\\ud83d\"", "[1 2]", "\"abc"}) {
    auto r = ParseJson(bad);
    CHECK(!r.value);
    CHECK(!r.error.empty());
  }
}

TEST(json_depth_limit) {
  CHECK(!ParseJson(std::string(65, '[') + std::string(65, ']')).value);
  CHECK(ParseJson(std::string(64, '[') + std::string(64, ']')).value.has_value());
}

TEST(json_roundtrip) {
  JsonValue obj = JsonValue::MakeObject();
  obj.Set("n", JsonValue(nullptr));
  obj.Set("b", JsonValue(true));
  obj.Set("i", JsonValue(2));
  obj.Set("d", JsonValue(2.5));
  obj.Set("s", JsonValue("ciao \"è\"\n\\"));
  JsonValue arr = JsonValue::MakeArray();
  arr.Push(JsonValue(1));
  arr.Push(JsonValue("x"));
  obj.Set("a", arr);
  obj.Set("o", JsonValue::MakeObject());
  const std::string text = SerializeJson(obj);
  auto r = ParseJson(text);
  CHECK(r.value.has_value());
  if (r.value) CHECK(*r.value == obj);
  CHECK(SerializeJson(JsonValue(2)).find("2.0") == std::string::npos);
  CHECK_EQ(SerializeJson(JsonValue(2)), std::string("2"));
}

TEST(json_set_replaces_existing_key) {
  JsonValue obj = JsonValue::MakeObject();
  obj.Set("k", JsonValue(1));
  obj.Set("k", JsonValue(2));
  CHECK_EQ(obj.AsObject().size(), size_t{1});
  CHECK(obj.Find("k")->AsNumber() == 2.0);
}
