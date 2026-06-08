# SPDX-FileCopyrightText: 2025 Wissem Boussetta
# SPDX-License-Identifier: MIT

#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_root/.build-sanitizer-local"

cmake -S "$repo_root" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=undefined -g"

cmake --build "$build_dir" --config Debug -j
ASAN_OPTIONS="halt_on_error=1:exitcode=99" ctest --test-dir "$build_dir" --output-on-failure
