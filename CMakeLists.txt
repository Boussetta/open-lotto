# SPDX-FileCopyrightText: 2025 Wissem Boussetta
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.10)

# Read version from VERSION file
file(READ "${CMAKE_SOURCE_DIR}/VERSION" PROJECT_VERSION)
string(STRIP "${PROJECT_VERSION}" PROJECT_VERSION)

project(open-lotto VERSION ${PROJECT_VERSION} LANGUAGES C)

include(CTest)

set(CMAKE_C_VISIBILITY_PRESET default)
set(CMAKE_VISIBILITY_INLINES_HIDDEN OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(OPEN_LOTTO_USE_CCACHE "Use ccache to speed up rebuilds" ON)
if(OPEN_LOTTO_USE_CCACHE AND NOT CMAKE_C_COMPILER_LAUNCHER)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        message(STATUS "ccache enabled: ${CCACHE_PROGRAM}")
    else()
        message(STATUS "ccache requested but not found; continuing without compiler launcher")
    endif()
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# ---------------------------------------------------------------------------
# Fuzzing support (requires clang + compiler-rt)
# Enable with:  cmake -DOPEN_LOTTO_FUZZ=ON -DCMAKE_C_COMPILER=clang ...
# ---------------------------------------------------------------------------
option(OPEN_LOTTO_FUZZ "Build libFuzzer targets (requires clang + compiler-rt)" OFF)

# ---------------------------------------------------------------------------
# Dependency resolution
# ---------------------------------------------------------------------------
# Run  ./scripts/install_deps.sh  to install all required packages.
# See README.md § Dependencies for the full list.
# ---------------------------------------------------------------------------

# ---- Guard against cross-build environment pollution (Buildroot / Yocto) --
#
# When a cross-toolchain (e.g. Buildroot) is on PATH it ships its own
# pkg-config binary whose compiled-in default search path points at the
# cross-sysroot, not the host system.  Using that binary causes every
# pkg_check_modules() call to return cross-sysroot headers and libraries,
# which then fail to compile with the host GCC.
#
# Fix: locate the SYSTEM pkg-config (in /usr/bin or /usr/local/bin) before
# letting find_package(PkgConfig) search PATH, and set PKG_CONFIG_LIBDIR
# and PKG_CONFIG_PATH exclusively to host system directories so that no
# cross-sysroot .pc file can leak in.

# 1. Find the system pkg-config, explicitly skipping cross-toolchain dirs.
find_program(PKG_CONFIG_EXECUTABLE
    NAMES pkg-config
    HINTS /usr/bin /usr/local/bin /opt/homebrew/bin
    NO_DEFAULT_PATH   # do NOT search PATH (buildroot binary would win)
)
if(NOT PKG_CONFIG_EXECUTABLE)
    # Fallback for unusual layouts (e.g. pure Homebrew on macOS)
    find_program(PKG_CONFIG_EXECUTABLE NAMES pkg-config)
endif()
if(NOT PKG_CONFIG_EXECUTABLE)
    message(FATAL_ERROR
        "pkg-config not found in standard system locations.\n"
        "Install it with:\n"
        "  Ubuntu/Debian : sudo apt-get install pkg-config\n"
        "  macOS         : brew install pkg-config\n"
    )
endif()
message(STATUS "Using pkg-config: ${PKG_CONFIG_EXECUTABLE}")

# 2. Build a path string from known system pkgconfig directories.
#    Setting PKG_CONFIG_LIBDIR (not just PKG_CONFIG_PATH) overrides the
#    compiled-in default inside the buildroot pkg-config binary, preventing
#    its cross-sysroot from being searched.
set(_SYSTEM_PC_DIRS
    /usr/lib/x86_64-linux-gnu/pkgconfig
    /usr/lib/aarch64-linux-gnu/pkgconfig
    /usr/lib/arm-linux-gnueabihf/pkgconfig
    /usr/lib/pkgconfig
    /usr/share/pkgconfig
    /usr/local/lib/pkgconfig
    /usr/local/share/pkgconfig
)
set(_SYS_PC_PATH "")
foreach(_d IN LISTS _SYSTEM_PC_DIRS)
    if(IS_DIRECTORY "${_d}")
        set(_SYS_PC_PATH "${_SYS_PC_PATH}${_d}:")
    endif()
endforeach()
string(REGEX REPLACE ":$" "" _SYS_PC_PATH "${_SYS_PC_PATH}")

# Override both variables so no cross-sysroot path can slip through.
set(ENV{PKG_CONFIG_LIBDIR} "${_SYS_PC_PATH}")
set(ENV{PKG_CONFIG_PATH}   "${_SYS_PC_PATH}")

find_package(PkgConfig REQUIRED)

pkg_check_modules(SDL2 sdl2)
if(NOT SDL2_FOUND)
    message(FATAL_ERROR
        "SDL2 not found.\n"
        "Install build dependencies by running:\n"
        "  ./scripts/install_deps.sh\n"
        "Or install SDL2 manually:\n"
        "  Ubuntu/Debian : sudo apt-get install libsdl2-dev\n"
        "  Fedora        : sudo dnf install SDL2-devel\n"
        "  Arch          : sudo pacman -S sdl2\n"
        "  macOS         : brew install sdl2\n"
    )
endif()

pkg_check_modules(SDL2TTF SDL2_ttf)
if(NOT SDL2TTF_FOUND)
    message(FATAL_ERROR
        "SDL2_ttf not found.\n"
        "Install build dependencies by running:\n"
        "  ./scripts/install_deps.sh\n"
        "Or install SDL2_ttf manually:\n"
        "  Ubuntu/Debian : sudo apt-get install libsdl2-ttf-dev\n"
        "  Fedora        : sudo dnf install SDL2_ttf-devel\n"
        "  Arch          : sudo pacman -S sdl2_ttf\n"
        "  macOS         : brew install sdl2_ttf\n"
    )
endif()

# OpenGL support for 3D GUI
find_package(OpenGL)
if(NOT OPENGL_FOUND)
    message(FATAL_ERROR
        "OpenGL not found.\n"
        "Install build dependencies by running:\n"
        "  ./scripts/install_deps.sh\n"
        "Or install OpenGL/Mesa manually:\n"
        "  Ubuntu/Debian : sudo apt-get install libgl-dev mesa-common-dev\n"
        "  Fedora        : sudo dnf install mesa-libGL-devel\n"
        "  Arch          : sudo pacman -S mesa\n"
        "  macOS         : included with Xcode Command Line Tools\n"
    )
endif()
# OpenMP support for parallel ball synchronization and physics
find_package(OpenMP REQUIRED)

include_directories(
    ${SDL2_INCLUDE_DIRS}
    ${SDL2TTF_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(open-lotto
    src/main.c
    src/export.c
    src/combogen.c
    src/config.c
    src/plugin_loader.c
    src/plugin_registry.c
    src/random_seed.c
    src/gui_sdl.c
    src/gui_opengl.c
    src/log.c
    src/theme.c
    src/validate.c
)

target_link_libraries(open-lotto
    PRIVATE
        ${SDL2_LIBRARIES}
        ${SDL2TTF_LIBRARIES}
        ${OPENGL_LIBRARIES}
    OpenMP::OpenMP_C
        dl
        m
)

target_link_options(open-lotto PRIVATE -Wl,-export-dynamic)

add_executable(open-lotto-plugin-validator
    src/plugin_validator.c
    src/plugin_loader.c
    src/combogen.c
    src/random_seed.c
    src/log.c
)

target_link_libraries(open-lotto-plugin-validator
    PRIVATE
        dl
        m
)

target_link_options(open-lotto-plugin-validator PRIVATE -Wl,-export-dynamic)

add_library(lotto SHARED plugins/lotto.c src/combogen.c src/random_seed.c src/log.c)
add_library(eurojackpot SHARED plugins/eurojackpot.c src/combogen.c src/random_seed.c src/log.c)

target_link_libraries(lotto PRIVATE m)
target_link_libraries(eurojackpot PRIVATE m)

set_target_properties(lotto PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)

set_target_properties(eurojackpot PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)

if(EXISTS "${CMAKE_SOURCE_DIR}/fonts")
    file(COPY "${CMAKE_SOURCE_DIR}/fonts"
         DESTINATION "${CMAKE_BINARY_DIR}")
endif()

if(BUILD_TESTING)
    # Unit tests for draw generation logic
    add_executable(test_combogen
        tests/test_combogen.c
        src/combogen.c
        src/random_seed.c
        src/log.c
    )

    add_test(NAME unit_combogen
        COMMAND $<TARGET_FILE:test_combogen>
    )
    set_tests_properties(unit_combogen PROPERTIES
        PASS_REGULAR_EXPRESSION "Tests passed:"
    )

    # ----------------------------------------------------------------
    # Plugin loader integration tests
    # ----------------------------------------------------------------
    add_library(stub_plugin SHARED tests/stub_plugin.c)
    set_target_properties(stub_plugin PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    )

    add_executable(test_plugin_loader
        tests/test_plugin_loader.c
        src/plugin_loader.c
        src/log.c
    )
    target_link_libraries(test_plugin_loader PRIVATE dl)
    target_compile_definitions(test_plugin_loader PRIVATE
        LOTTO_PLUGIN_PATH="${CMAKE_BINARY_DIR}/plugins/liblotto.so"
        STUB_PLUGIN_PATH="${CMAKE_BINARY_DIR}/plugins/libstub_plugin.so"
    )
    add_dependencies(test_plugin_loader lotto stub_plugin)

    add_test(NAME unit_plugin_loader
        COMMAND $<TARGET_FILE:test_plugin_loader>
    )
    set_tests_properties(unit_plugin_loader PROPERTIES
        PASS_REGULAR_EXPRESSION "Tests passed:"
    )

    # ----------------------------------------------------------------
    # Export golden-file tests
    # ----------------------------------------------------------------
    add_executable(test_export
        tests/test_export.c
        src/export.c
        src/log.c
    )

    add_test(NAME unit_export
        COMMAND $<TARGET_FILE:test_export>
    )
    set_tests_properties(unit_export PROPERTIES
        PASS_REGULAR_EXPRESSION "Tests passed:"
    )

    # ----------------------------------------------------------------
    # CLI integration tests – success paths
    # ----------------------------------------------------------------
    add_test(NAME cli_lists_games
        COMMAND $<TARGET_FILE:open-lotto> --list-games
    )
    set_tests_properties(cli_lists_games PROPERTIES
        PASS_REGULAR_EXPRESSION "Eurojackpot|EuroJackpot"
    )

    add_test(NAME cli_runs_lotto_once
        COMMAND $<TARGET_FILE:open-lotto> --game "Lotto 6aus49" --draws 1
    )
    set_tests_properties(cli_runs_lotto_once PROPERTIES
        PASS_REGULAR_EXPRESSION "Main:"
    )

    add_test(NAME cli_runs_eurojackpot_once
        COMMAND $<TARGET_FILE:open-lotto> --game "Eurojackpot" --draws 1
    )
    set_tests_properties(cli_runs_eurojackpot_once PROPERTIES
        PASS_REGULAR_EXPRESSION "Main:"
    )

    add_test(NAME cli_reloads_selected_plugin
        COMMAND $<TARGET_FILE:open-lotto> --game "Lotto 6aus49" --reload-plugin --validate-only
    )
    set_tests_properties(cli_reloads_selected_plugin PROPERTIES
        PASS_REGULAR_EXPRESSION "Plugin reload: enabled"
    )

    add_test(NAME plugin_validator_accepts_valid_plugin
        COMMAND $<TARGET_FILE:open-lotto-plugin-validator> "${CMAKE_BINARY_DIR}/plugins/liblotto.so"
    )
    set_tests_properties(plugin_validator_accepts_valid_plugin PROPERTIES
        PASS_REGULAR_EXPRESSION "VALID Lotto 6aus49"
    )

    add_test(NAME plugin_validator_rejects_invalid_plugin
        COMMAND $<TARGET_FILE:open-lotto-plugin-validator> "${CMAKE_BINARY_DIR}/plugins/libstub_plugin.so"
    )
    set_tests_properties(plugin_validator_rejects_invalid_plugin PROPERTIES WILL_FAIL TRUE)

    # ----------------------------------------------------------------
    # CLI argument validation tests – all must exit non-zero
    # ----------------------------------------------------------------
    add_test(NAME cli_no_args
        COMMAND $<TARGET_FILE:open-lotto>
    )
    set_tests_properties(cli_no_args PROPERTIES WILL_FAIL TRUE)

    add_test(NAME cli_missing_game_flag
        COMMAND $<TARGET_FILE:open-lotto> "Lotto 6aus49"
    )
    set_tests_properties(cli_missing_game_flag PROPERTIES WILL_FAIL TRUE)

    add_test(NAME cli_draws_zero
        COMMAND $<TARGET_FILE:open-lotto> --game "Lotto 6aus49" --draws 0
    )
    set_tests_properties(cli_draws_zero PROPERTIES WILL_FAIL TRUE)

    add_test(NAME cli_draws_negative
        COMMAND $<TARGET_FILE:open-lotto> --game "Lotto 6aus49" --draws -1
    )
    set_tests_properties(cli_draws_negative PROPERTIES WILL_FAIL TRUE)

    add_test(NAME cli_draws_not_a_number
        COMMAND $<TARGET_FILE:open-lotto> --game "Lotto 6aus49" --draws abc
    )
    set_tests_properties(cli_draws_not_a_number PROPERTIES WILL_FAIL TRUE)

    add_test(NAME cli_draws_missing_value
        COMMAND $<TARGET_FILE:open-lotto> --game "Lotto 6aus49" --draws
    )
    set_tests_properties(cli_draws_missing_value PROPERTIES WILL_FAIL TRUE)

    add_test(NAME cli_invalid_export_format
        COMMAND $<TARGET_FILE:open-lotto> --game "Lotto 6aus49" --draws 1 --export xml
    )
    set_tests_properties(cli_invalid_export_format PROPERTIES WILL_FAIL TRUE)

    add_test(NAME cli_export_missing_value
        COMMAND $<TARGET_FILE:open-lotto> --game "Lotto 6aus49" --draws 1 --export
    )
    set_tests_properties(cli_export_missing_value PROPERTIES WILL_FAIL TRUE)

    add_test(NAME cli_unknown_flag
        COMMAND $<TARGET_FILE:open-lotto> --game "Lotto 6aus49" --invalid-flag
    )
    set_tests_properties(cli_unknown_flag PROPERTIES WILL_FAIL TRUE)

    add_test(NAME cli_verbose_missing_value
        COMMAND $<TARGET_FILE:open-lotto> --game "Lotto 6aus49" --verbose
    )
    set_tests_properties(cli_verbose_missing_value PROPERTIES WILL_FAIL TRUE)

    # ----------------------------------------------------------------
    # Performance benchmarking tool (utility, not a test)
    # ----------------------------------------------------------------
    add_executable(benchmark
        tests/benchmark.c
        src/combogen.c
        src/random_seed.c
        src/log.c
        src/export.c
    )

    # Benchmark is not a test but a utility - we don't add it as a test
    # Users can run it manually: ./benchmark [iterations] [verbose]

    # ----------------------------------------------------------------
    # OpenMP parallelization performance benchmarks (utility, not a test)
    # ----------------------------------------------------------------
    add_executable(benchmark_openmp
        tests/benchmark_openmp.c
    )

    target_link_libraries(benchmark_openmp
        PRIVATE OpenMP::OpenMP_C m
    )

    # OpenMP benchmark is not a test but a utility - we don't add it as a test
    # Users can run it manually to see parallelization improvements:
    # ./benchmark_openmp [ball_count] [iterations]
    # ./benchmark_openmp --help
    # OMP_NUM_THREADS=8 ./benchmark_openmp 10000

    # ----------------------------------------------------------------
    # Property-based tests – RNG invariants and export well-formedness
    # ----------------------------------------------------------------
    add_executable(test_property
        tests/test_property.c
        src/combogen.c
        src/export.c
        src/random_seed.c
        src/log.c
    )

    add_test(NAME unit_property
        COMMAND $<TARGET_FILE:test_property>
    )
    set_tests_properties(unit_property PROPERTIES
        PASS_REGULAR_EXPRESSION "Tests passed:"
    )

    # ----------------------------------------------------------------
    # Stress tests – high-volume draws and export pipeline under load
    # ----------------------------------------------------------------
    add_executable(test_stress
        tests/test_stress.c
        src/combogen.c
        src/export.c
        src/random_seed.c
        src/log.c
    )

    add_test(NAME unit_stress
        COMMAND $<TARGET_FILE:test_stress>
    )
    set_tests_properties(unit_stress PROPERTIES
        PASS_REGULAR_EXPRESSION "Tests passed:"
        TIMEOUT 120
    )

    # ----------------------------------------------------------------
    # Performance regression tests – throughput lower-bound guards
    # ----------------------------------------------------------------
    add_executable(test_perf_regression
        tests/test_perf_regression.c
        src/combogen.c
        src/export.c
        src/random_seed.c
        src/log.c
    )
    target_link_libraries(test_perf_regression PRIVATE m)

    add_test(NAME unit_perf_regression
        COMMAND $<TARGET_FILE:test_perf_regression>
    )
    set_tests_properties(unit_perf_regression PROPERTIES
        PASS_REGULAR_EXPRESSION "Tests passed:"
        TIMEOUT 120
    )

endif()

# ---------------------------------------------------------------------------
# libFuzzer targets (opt-in; compiled separately from normal test suite)
# ---------------------------------------------------------------------------
if(OPEN_LOTTO_FUZZ)
    if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR
            "OPEN_LOTTO_FUZZ requires clang.  "
            "Re-configure with -DCMAKE_C_COMPILER=clang.")
    endif()

    add_executable(fuzz_validate
        tests/fuzz_validate.c
        src/validate.c
        src/plugin_loader.c
        src/plugin_registry.c
        src/log.c
    )
    target_compile_options(fuzz_validate PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(fuzz_validate   PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_libraries(fuzz_validate PRIVATE dl m)
    message(STATUS "libFuzzer target enabled: fuzz_validate")
endif()
