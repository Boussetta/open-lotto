# SPDX-FileCopyrightText: 2025 Wissem Boussetta
# SPDX-License-Identifier: MIT

#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

cppcheck \
  --enable=all \
  --suppress=unmatchedSuppression \
  --suppress=missingIncludeSystem \
  --suppress=missingInclude \
  --suppress=unusedFunction \
  --suppress=constParameterPointer \
  --suppress=constVariablePointer \
  --suppress=constVariable \
  --suppress=unreadVariable \
  --suppress=normalCheckLevelMaxBranches \
  --suppress=checkLevelNormal \
  --suppress=checkersReport \
  --suppress=staticFunction:src/config.c \
  --error-exitcode=1 \
  src/ plugins/ include/ tests/
