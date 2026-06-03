cmake_minimum_required(VERSION 3.10)
project(open-lotto C)

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
