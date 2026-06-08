# SPDX-FileCopyrightText: 2025 Wissem Boussetta
# SPDX-License-Identifier: MIT

#!/usr/bin/env bash
set -euo pipefail

# Detect architecture for baseline file naming
detect_architecture() {
    if [[ $(uname -m) == "x86_64" ]]; then
        echo "x86_64"
    elif [[ $(uname -m) == "aarch64" ]]; then
        echo "aarch64"
    elif [[ $(uname -m) == "armv7l" ]]; then
        echo "armv7l"
    elif [[ $(uname -m) == "armv6l" ]]; then
        echo "armv6l"
    elif [[ $(uname -m) == "riscv64" ]]; then
        echo "riscv64"
    elif [[ $(uname -m) == "ppc64" ]]; then
        echo "ppc64"
    else
        uname -m
    fi
}

ARCH=$(detect_architecture)
CURRENT="${1:-./build/benchmark_results.json}"
BASELINE="${2:-./.github/benchmark_baseline_${ARCH}.json}"
THRESHOLD="${3:-5}"

if [ ! -f "$CURRENT" ]; then
  echo "Error: Current results not found" >&2
  exit 1
fi

if [ ! -f "$BASELINE" ]; then
  echo "Creating baseline for architecture: ${ARCH}..."
  mkdir -p "$(dirname "$BASELINE")"
  cp "$CURRENT" "$BASELINE"
  exit 0
fi

echo "Checking performance regressions for ${ARCH} (threshold: ${THRESHOLD}%)..."
python3 << PYTHON
import json, sys

with open("${CURRENT}") as f:
  current = json.load(f)
with open("${BASELINE}") as f:
  baseline = json.load(f)

failed = False
for name in baseline["benchmarks"]:
  if name not in current["benchmarks"]:
    print(f"✗ Missing: {name}")
    continue
  
  base_val = baseline["benchmarks"][name]["value"]
  curr_val = current["benchmarks"][name]["value"]
  
  if base_val == 0:
    continue
  
  # For throughput metrics, higher is better
  change = ((base_val - curr_val) / base_val) * 100
  
  if change > ${THRESHOLD}:
    print(f"✗ REGRESSION {name}: {change:.1f}%")
    failed = True
  else:
    status = "✓" if change > 0 else "↑"
    print(f"{status} {name}: {abs(change):.1f}%")

sys.exit(1 if failed else 0)
PYTHON
