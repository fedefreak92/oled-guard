# Scrive OUT (header con OLED_GUARD_COMMIT) col commit corrente di SRC; "unknown" fuori
# da git. Gira a ogni build (target oled-guard-commit) e riscrive il file solo se il
# commit e' cambiato, cosi' app.cpp si ricompila solo allora.
# Uso: cmake -DSRC=<sorgenti> -DOUT=<header> [-DGIT_EXECUTABLE=<git>] -P build_commit.cmake
set(commit "unknown")
if(GIT_EXECUTABLE)
  execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD WORKING_DIRECTORY "${SRC}"
                  OUTPUT_VARIABLE out OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE rc ERROR_QUIET)
  if(rc EQUAL 0 AND out)
    set(commit "${out}")
  endif()
endif()

set(text "// Generato da cmake/build_commit.cmake a ogni build: non modificare.\n#pragma once\n#define OLED_GUARD_COMMIT \"${commit}\"\n")
set(old "")
if(EXISTS "${OUT}")
  file(READ "${OUT}" old)
endif()
if(NOT old STREQUAL text)
  file(WRITE "${OUT}" "${text}")
endif()
