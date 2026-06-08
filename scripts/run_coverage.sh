# SPDX-FileCopyrightText: 2025 Wissem Boussetta
# SPDX-License-Identifier: MIT

#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_root/.build-coverage"
THRESHOLD=70  # minimum line coverage percentage

for cmd in lcov genhtml gcov cmake ctest; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "Required tool '$cmd' not found. Install with: sudo apt-get install lcov" >&2
    exit 1
  fi
done

# Configure with coverage flags (gcc required for gcov)
cmake -S "$repo_root" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DCMAKE_C_COMPILER=gcc \
  -DCMAKE_C_FLAGS="--coverage -fprofile-arcs -ftest-coverage" \
  -DCMAKE_EXE_LINKER_FLAGS="--coverage" \
  -DCMAKE_SHARED_LINKER_FLAGS="--coverage"

cmake --build "$build_dir" -j

# Run all tests to generate .gcda files
ctest --test-dir "$build_dir" --output-on-failure

# Capture coverage data
lcov --capture \
  --directory "$build_dir" \
  --output-file "$build_dir/coverage.info" \
  --gcov-tool gcov \
  --ignore-errors mismatch

# Remove third-party, system, and test files from the report
lcov --remove "$build_dir/coverage.info" \
  '/usr/*' \
  '*/tests/*' \
  --ignore-errors unused \
  --output-file "$build_dir/coverage_filtered.info"

# Print summary to stdout (used by CI to show coverage %)
lcov --summary "$build_dir/coverage_filtered.info"

# Extract line coverage percentage and enforce threshold
pct=$(lcov --summary "$build_dir/coverage_filtered.info" 2>&1 \
  | grep -oP 'lines\.*: \K[0-9]+\.[0-9]+')

if [ -z "$pct" ]; then
  echo "Could not parse coverage percentage." >&2
  exit 1
fi

echo ""
echo "Line coverage: ${pct}%  (threshold: ${THRESHOLD}%)"

# Use awk for floating-point comparison
if awk "BEGIN { exit (${pct} >= ${THRESHOLD}) }"; then
  echo "FAILED: coverage ${pct}% is below the ${THRESHOLD}% threshold." >&2
  exit 1
fi

echo "PASSED: coverage meets the ${THRESHOLD}% threshold."
