#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_root/build-tidy"

if ! command -v clang-tidy >/dev/null 2>&1; then
  echo "clang-tidy is required but was not found in PATH." >&2
  echo "Install it (for example: sudo apt-get install clang-tidy) and try again." >&2
  exit 1
fi

# Generate compile commands if not present
if [ ! -f "$build_dir/compile_commands.json" ]; then
  cmake -S "$repo_root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DBUILD_TESTING=ON > /dev/null 2>&1
fi

find "$repo_root/src" "$repo_root/plugins" "$repo_root/tests" -name "*.c" \
  | xargs clang-tidy -p "$build_dir" --config-file="$repo_root/.clang-tidy"
