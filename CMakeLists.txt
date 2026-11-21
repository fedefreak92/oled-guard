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
  src/core/config.cpp
  src/core/hotkey_parse.cpp
  src/core/monitor_select.cpp
  src/core/watchdog.cpp
  src/core/wear_stats.cpp
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
  tests/test_config.cpp
  tests/test_hotkey.cpp
  tests/test_monitor_select.cpp
  tests/test_watchdog.cpp
  tests/test_wear_stats.cpp
)
target_include_directories(oled-guard-tests PRIVATE tests)
target_link_libraries(oled-guard-tests PRIVATE oled_core)
add_test(NAME oled-guard-tests COMMAND oled-guard-tests)

# Applicazione Win32 / D3D11 / DXGI / DirectComposition.
add_executable(oled-guard WIN32
  src/app/main.cpp
  src/app/app.cpp
  src/app/logger.cpp
  src/app/paths.cpp
  src/app/emergency.cpp
  src/app/monitors.cpp
  src/app/capture.cpp
  src/app/d3d_device.cpp
  src/app/diagnostics.cpp
  src/app/overlay_window.cpp
  src/app/presenter.cpp
  src/app/render_thread.cpp
  src/app/tray.cpp
  src/app/pause_monitor.cpp
  src/app/config_file.cpp
  src/app/autostart.cpp
  src/app/single_instance.cpp
  src/app/oled-guard.manifest
)

# Shader compilati in build con fxc: bytecode incluso come header.
find_program(FXC_EXE fxc REQUIRED)
set(SHADER_OUT ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${SHADER_OUT})
function(add_shader name profile)
  set(src ${CMAKE_SOURCE_DIR}/src/app/shaders/${name}.hlsl)
  set(out ${SHADER_OUT}/${name}.h)
  add_custom_command(OUTPUT ${out}
    COMMAND ${FXC_EXE} /nologo /T ${profile} /E main /O3 /WX /Fh ${out} /Vn g_${name} ${src}
    DEPENDS ${src} VERBATIM)
  target_sources(oled-guard PRIVATE ${out})
endfunction()
add_shader(fullscreen_vs vs_5_0)
add_shader(shift_ps ps_5_0)

target_include_directories(oled-guard PRIVATE src/app ${SHADER_OUT})
target_link_libraries(oled-guard PRIVATE oled_core
  d3d11 dxgi dcomp dwmapi wtsapi32 shell32 user32 advapi32 shlwapi ole32)
