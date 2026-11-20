cmake_minimum_required(VERSION 3.25)
project(oled_guard LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /WX /permissive- /utf-8 /EHsc)
  add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

enable_testing()

# Logica pura: nessun header di Windows qui dentro.
add_library(oled_core STATIC
  src/core/rng.cpp
  src/core/offset_generator.cpp
  src/core/offset_animator.cpp
  src/core/shift_scheduler.cpp
  src/core/pause_rules.cpp
  src/core/json.cpp
)
target_include_directories(oled_core PUBLIC src/core)

# Eseguibile dei test unitari (senza framework esterni).
add_executable(oled-guard-tests
  tests/test_main.cpp
  tests/test_rng.cpp
  tests/test_offset_generator.cpp
  tests/test_offset_animator.cpp
  tests/test_shift_scheduler.cpp
  tests/test_pause_rules.cpp
  tests/test_json.cpp
)
target_include_directories(oled-guard-tests PRIVATE tests)
target_link_libraries(oled-guard-tests PRIVATE oled_core)
add_test(NAME oled-guard-tests COMMAND oled-guard-tests)
