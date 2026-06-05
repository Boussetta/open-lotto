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

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2TTF REQUIRED SDL2_ttf)

# OpenGL support for 3D GUI
find_package(OpenGL REQUIRED)

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
    src/plugin_loader.c
    src/plugin_registry.c
    src/random_seed.c
    src/gui_sdl.c
    src/gui_opengl.c
    src/log.c
)

target_link_libraries(open-lotto
    PRIVATE
        ${SDL2_LIBRARIES}
        ${SDL2TTF_LIBRARIES}
        ${OPENGL_LIBRARIES}
        dl
        m
)

target_link_options(open-lotto PRIVATE -Wl,-export-dynamic)

add_library(lotto SHARED plugins/lotto.c)
add_library(eurojackpot SHARED plugins/eurojackpot.c)

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
    )

    # Benchmark is not a test but a utility - we don't add it as a test
    # Users can run it manually: ./benchmark [iterations] [verbose]
endif()
