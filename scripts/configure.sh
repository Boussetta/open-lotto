#!/usr/bin/env bash
# configure.sh — CMake configure wrapper for open-lotto
#
# Use this instead of calling cmake directly when a cross-build environment
# (Buildroot, Yocto, ROS, etc.) has overridden PKG_CONFIG_PATH and causes
# system SDL2 / OpenGL libraries not to be found.
#
# Usage:
#   ./scripts/configure.sh [extra cmake args...]
#   ./scripts/configure.sh --profile-configure
#   ./scripts/configure.sh --profile-configure --profile-output /tmp/cmake-trace.json
#
# Examples:
#   ./scripts/configure.sh
#   ./scripts/configure.sh -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
#   ./scripts/configure.sh -DCMAKE_BUILD_TYPE=Release -B build/release

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# ---------------------------------------------------------------------------
# Prepend system pkg-config directories to override any cross-build path.
# ---------------------------------------------------------------------------
SYSTEM_PKG_CONFIG_DIRS=(
    /usr/lib/x86_64-linux-gnu/pkgconfig
    /usr/lib/aarch64-linux-gnu/pkgconfig
    /usr/lib/arm-linux-gnueabihf/pkgconfig
    /usr/lib/pkgconfig
    /usr/share/pkgconfig
    /usr/local/lib/pkgconfig
    /usr/local/share/pkgconfig
)

SYS_PATH=""
for p in "${SYSTEM_PKG_CONFIG_DIRS[@]}"; do
    [[ -d "$p" ]] && SYS_PATH="${SYS_PATH:+$SYS_PATH:}$p"
done

# System paths go FIRST so they shadow any cross-sysroot overrides.
export PKG_CONFIG_PATH="${SYS_PATH}${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

echo "[configure.sh] PKG_CONFIG_PATH=$PKG_CONFIG_PATH"

# ---------------------------------------------------------------------------
# Parse optional -B / --build-dir argument; default to build/
# ---------------------------------------------------------------------------
BUILD_DIR="$REPO_ROOT/build"
EXTRA_ARGS=()
PROFILE_CONFIGURE=0
PROFILE_OUTPUT=""
i=1
while [[ $i -le $# ]]; do
    arg="${!i}"
    if [[ "$arg" == "-B" ]]; then
        i=$(( i + 1 ))
        BUILD_DIR="${!i}"
    elif [[ "$arg" == -B* ]]; then
        BUILD_DIR="${arg#-B}"
    elif [[ "$arg" == "--profile-configure" ]]; then
        PROFILE_CONFIGURE=1
    elif [[ "$arg" == "--profile-output" ]]; then
        i=$(( i + 1 ))
        PROFILE_OUTPUT="${!i}"
    else
        EXTRA_ARGS+=("$arg")
    fi
    i=$(( i + 1 ))
done

mkdir -p "$BUILD_DIR"

PROFILE_FLAGS=()
if [[ $PROFILE_CONFIGURE -eq 1 ]]; then
    if ! cmake --help | grep -q -- "--profiling-format"; then
        echo "[configure.sh] Your CMake version does not support --profiling-format."
        echo "[configure.sh] Upgrade CMake to a version with configure profiling support."
        exit 1
    fi

    if [[ -z "$PROFILE_OUTPUT" ]]; then
        PROFILE_OUTPUT="$BUILD_DIR/cmake-configure-trace.json"
    fi

    PROFILE_FLAGS=(
        --profiling-format=google-trace
        "--profiling-output=$PROFILE_OUTPUT"
    )
    echo "[configure.sh] Configure profiling enabled"
    echo "[configure.sh] Trace output: $PROFILE_OUTPUT"
fi

cmake "${PROFILE_FLAGS[@]}" -S "$REPO_ROOT" -B "$BUILD_DIR" "${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}"
