// Mini-framework di test senza dipendenze esterne.
#pragma once

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace oled::test {

struct TestCase {
  const char* name;
  void (*fn)();
};

inline std::vector<TestCase>& Registry() {
  static std::vector<TestCase> registry;
  return registry;
}

struct Registrar {
  Registrar(const char* name, void (*fn)()) { Registry().push_back({name, fn}); }
};

// Registra un fallimento: il test continua, il conteggio viene letto dal main.
void ReportFailure(const char* file, int line, const std::string& message);

template <class T>
std::string ToText(const T& value) {
  if constexpr (requires(std::ostream& os, const T& v) { os << v; }) {
    std::ostringstream os;
    os << value;
    return os.str();
  } else {
    return "<?>";
  }
}

}  // namespace oled::test

#define TEST(name)                                                   \
  static void name();                                                \
  static ::oled::test::Registrar name##_registrar(#name, &name);     \
  static void name()

#define CHECK(expr)                                                              \
  do {                                                                           \
    if (!(expr)) ::oled::test::ReportFailure(__FILE__, __LINE__, "CHECK(" #expr ")"); \
  } while (0)

#define CHECK_EQ(a, b)                                                                  \
  do {                                                                                  \
    const auto& check_a_ = (a);                                                         \
    const auto& check_b_ = (b);                                                         \
    if (!(check_a_ == check_b_)) {                                                      \
      ::oled::test::ReportFailure(__FILE__, __LINE__,                                   \
                                  "CHECK_EQ(" #a ", " #b "): " +                        \
                                      ::oled::test::ToText(check_a_) + " != " +         \
                                      ::oled::test::ToText(check_b_));                  \
    }                                                                                   \
  } while (0)

#define CHECK_NEAR(a, b, eps)                                                           \
  do {                                                                                  \
    const double check_a_ = (a);                                                        \
    const double check_b_ = (b);                                                        \
    if (!(std::fabs(check_a_ - check_b_) <= (eps))) {                                   \
      ::oled::test::ReportFailure(__FILE__, __LINE__,                                   \
                                  "CHECK_NEAR(" #a ", " #b "): " +                      \
                                      ::oled::test::ToText(check_a_) + " vs " +         \
                                      ::oled::test::ToText(check_b_));                  \
    }                                                                                   \
  } while (0)
