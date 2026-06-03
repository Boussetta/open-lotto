cmake_minimum_required(VERSION 3.10)
project(open-lotto C)

include(CTest)

set(CMAKE_C_VISIBILITY_PRESET default)
set(CMAKE_VISIBILITY_INLINES_HIDDEN OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2TTF REQUIRED SDL2_ttf)

include_directories(
    ${SDL2_INCLUDE_DIRS}
    ${SDL2TTF_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(open-lotto
    src/main.c
    src/combogen.c
    src/plugin_loader.c
    src/plugin_registry.c
    src/random_seed.c
    src/gui_sdl.c
    src/log.c
)

target_link_libraries(open-lotto
    PRIVATE
        ${SDL2_LIBRARIES}
        ${SDL2TTF_LIBRARIES}
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

    # CLI integration tests
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
endif()
