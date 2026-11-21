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
  src/core/app_history.cpp
  src/core/ui_protocol.cpp
  src/core/dim_params.cpp
  src/core/dim_model.cpp
  src/core/dim_schedule.cpp
  src/core/dim_stats.cpp
  src/core/dim_zones.cpp
  src/core/shade_model.cpp
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
  tests/test_app_history.cpp
  tests/test_ui_protocol.cpp
  tests/test_dim_model.cpp
  tests/test_dim_schedule.cpp
  tests/test_dim_stats.cpp
  tests/test_dim_zones.cpp
  tests/test_shade_model.cpp
)
target_include_directories(oled-guard-tests PRIVATE tests)
target_link_libraries(oled-guard-tests PRIVATE oled_core)
add_test(NAME oled-guard-tests COMMAND oled-guard-tests)

# Shader compilati in build con fxc: bytecode incluso come header.
find_program(FXC_EXE fxc REQUIRED)
set(SHADER_OUT ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${SHADER_OUT})

# Parte GPU condivisa fra l'app e i test con device WARP (oscuramento e shader).
add_library(oled_gpu STATIC
  src/app/dimmer.cpp
  src/app/shared_dim.cpp
)
target_include_directories(oled_gpu PUBLIC src/app ${SHADER_OUT})
target_link_libraries(oled_gpu PUBLIC oled_core d3d11 dxgi)

# var: nome dell'header e della variabile g_<var>; entry: funzione dello shader.
function(add_shader_entry var file entry profile)
  set(src ${CMAKE_SOURCE_DIR}/src/app/shaders/${file}.hlsl)
  set(out ${SHADER_OUT}/${var}.h)
  add_custom_command(OUTPUT ${out}
    COMMAND ${FXC_EXE} /nologo /T ${profile} /E ${entry} /O3 /WX /Fh ${out} /Vn g_${var} ${src}
    DEPENDS ${src} VERBATIM)
  target_sources(oled_gpu PRIVATE ${out})
endfunction()
add_shader_entry(fullscreen_vs fullscreen_vs main vs_5_0)
add_shader_entry(shift_ps shift_ps main ps_5_0)
add_shader_entry(dim_detect dim_cs CSDetect cs_5_0)
add_shader_entry(dim_activate dim_cs CSActivate cs_5_0)
add_shader_entry(dim_motion_h dim_cs CSMotionH cs_5_0)
add_shader_entry(dim_motion_v dim_cs CSMotionV cs_5_0)
add_shader_entry(dim_level dim_cs CSLevel cs_5_0)

# Test degli shader su device WARP (nessuna GPU necessaria).
add_executable(oled-guard-gpu-tests
  tests/test_main.cpp
  tests_gpu/test_dimmer_gpu.cpp
  tests_gpu/test_shift_ps_gpu.cpp
)
target_include_directories(oled-guard-gpu-tests PRIVATE tests tests_gpu)
target_link_libraries(oled-guard-gpu-tests PRIVATE oled_gpu)
add_test(NAME oled-guard-gpu-tests COMMAND oled-guard-gpu-tests)

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
  src/app/ui_window.cpp
  src/app/windows_settings.cpp
  src/app/stats_file.cpp
  src/app/dim_diagnostics.cpp
  src/app/window_zones.cpp
  src/app/oled-guard.manifest
)


# WebView2 SDK (unica dipendenza esterna, scelta dall'utente): header di sistema
# (niente warning) e loader statico, nessuna DLL da distribuire.
target_include_directories(oled-guard SYSTEM PRIVATE third_party/webview2/include)
target_link_libraries(oled-guard PRIVATE ${CMAKE_SOURCE_DIR}/third_party/webview2/x64/WebView2LoaderStatic.lib shcore)

# Interfaccia HTML/CSS/JS copiata accanto all'eseguibile.
add_custom_command(TARGET oled-guard POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/src/ui $<TARGET_FILE_DIR:oled-guard>/ui
  VERBATIM)
target_link_libraries(oled-guard PRIVATE oled_core oled_gpu
  d3d11 dxgi dcomp dwmapi wtsapi32 shell32 user32 advapi32 shlwapi ole32 powrprof)
