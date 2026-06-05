#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

cppcheck \
  --enable=all \
  --suppress=missingIncludeSystem \
  --suppress=missingInclude \
  --suppress=unusedFunction \
  --suppress=constParameterPointer \
  --suppress=constVariablePointer \
  --suppress=normalCheckLevelMaxBranches \
  --suppress=checkersReport \
  --error-exitcode=1 \
  src/ plugins/ include/ tests/
