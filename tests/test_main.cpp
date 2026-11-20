// Esegue tutti i test registrati. Argomento opzionale: sottostringa del nome.
#include <cstdio>
#include <string>
#include <string_view>

#include "test_framework.h"

namespace {
int g_failures = 0;
}

void oled::test::ReportFailure(const char* file, int line, const std::string& message) {
  ++g_failures;
  std::printf("    %s(%d): %s\n", file, line, message.c_str());
}

int main(int argc, char** argv) {
  const std::string_view filter = argc > 1 ? argv[1] : "";
  int run = 0;
  int failed = 0;
  for (const auto& test : oled::test::Registry()) {
    if (!filter.empty() && std::string_view(test.name).find(filter) == std::string_view::npos) {
      continue;
    }
    const int before = g_failures;
    test.fn();
    ++run;
    const bool ok = g_failures == before;
    if (!ok) ++failed;
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", test.name);
  }
  std::printf("\n%d test eseguiti, %d falliti\n", run, failed);
  return failed == 0 && run > 0 ? 0 : 1;
}
