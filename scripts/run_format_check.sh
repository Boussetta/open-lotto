# SPDX-FileCopyrightText: 2025 Wissem Boussetta
# SPDX-License-Identifier: MIT

#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

if ! command -v clang-format >/dev/null 2>&1; then
  echo "clang-format is required but was not found in PATH." >&2
  echo "Install it (for example: sudo apt-get install clang-format) and try again." >&2
  exit 1
fi

clang-format -n -Werror $(find src plugins include tests -name "*.c" -o -name "*.h")
