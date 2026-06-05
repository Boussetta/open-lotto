#!/usr/bin/env bash
set -euo pipefail

RESULTS_FILE="${1:-${PWD}/build/benchmark_results.json}"
BUILD_DIR="${2:-${PWD}/build}"

if [ ! -f "${BUILD_DIR}/benchmark" ]; then
  echo "Error: benchmark not found" >&2
  exit 1
fi

echo "Running benchmarks..." 
mkdir -p "$(dirname "${RESULTS_FILE}")"

# Run benchmark and save output
OUTPUT=$("${BUILD_DIR}/benchmark" 100000 2>&1)

# Extract values using awk to get the numeric part
RNG=$(echo "$OUTPUT" | grep "RNG draws/sec" | awk -F: '{print $2}' | awk '{print $1}')
EURO=$(echo "$OUTPUT" | grep "Eurojackpot draws/sec" | awk -F: '{print $2}' | awk '{print $1}')
LOTTO=$(echo "$OUTPUT" | grep "Lotto 6aus49 draws/sec" | awk -F: '{print $2}' | awk '{print $1}')
SEED=$(echo "$OUTPUT" | grep "Seed generation/sec" | awk -F: '{print $2}' | awk '{print $1}')

# Create JSON
python3 << PYTHON
import json
from datetime import datetime

data = {
    "timestamp": datetime.utcnow().isoformat() + "Z",
    "benchmarks": {
        "rng": {"value": float("${RNG}"), "unit": "draws/sec"},
        "eurojackpot": {"value": float("${EURO}"), "unit": "draws/sec"},
        "lotto": {"value": float("${LOTTO}"), "unit": "draws/sec"},
        "seed": {"value": float("${SEED}"), "unit": "seeds/sec"}
    }
}

with open("${RESULTS_FILE}", "w") as f:
    json.dump(data, f, indent=2)

print("✓ Benchmarks saved")
PYTHON
