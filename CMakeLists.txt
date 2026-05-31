cmake_minimum_required(VERSION 3.10)
project(open-lotto C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
)

# ---------------------------------------------------------
# Main executable
# ---------------------------------------------------------
add_executable(open-lotto
    src/main.c
    src/plugin_loader.c
    src/log.c
)

# ---------------------------------------------------------
# Plugins directory
# ---------------------------------------------------------
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)

# ---------------------------------------------------------
# Function to build a plugin
# ---------------------------------------------------------
function(add_lottery_plugin NAME SRCFILE)
    add_library(${NAME} SHARED
        ${SRCFILE}
        src/combogen.c
        src/random.c
    )

    # Output plugin .so into build/plugins/
    set_target_properties(${NAME} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
    )
endfunction()

# ---------------------------------------------------------
# Build plugins
# ---------------------------------------------------------
add_lottery_plugin(lotto plugins/lotto.c)
add_lottery_plugin(eurojackpot plugins/eurojackpot.c)

# ---------------------------------------------------------
# Installation (optional)
# ---------------------------------------------------------
install(TARGETS open-lotto DESTINATION bin)
install(DIRECTORY ${CMAKE_BINARY_DIR}/plugins DESTINATION bin)
