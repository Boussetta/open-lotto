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

# Run benchmark and capture full output (exit code 1 means memory regression – still capture)
OUTPUT=$("${BUILD_DIR}/benchmark" 100000 2>&1) || true

# ---------------------------------------------------------------------------
# Throughput metrics (Summary section)
# ---------------------------------------------------------------------------
RNG=$(echo "$OUTPUT"  | grep "RNG draws/sec"          | awk -F: '{print $2}' | awk '{print $1}')
EURO=$(echo "$OUTPUT" | grep "Eurojackpot draws/sec"  | awk -F: '{print $2}' | awk '{print $1}')
LOTTO=$(echo "$OUTPUT"| grep "Lotto 6aus49 draws/sec" | awk -F: '{print $2}' | awk '{print $1}')
SEED=$(echo "$OUTPUT" | grep "Seed generation/sec"    | awk -F: '{print $2}' | awk '{print $1}')

# ---------------------------------------------------------------------------
# Per-operation CPU timing (µs/op lines emitted inside timing sections)
# Each "Wall time / op" line is prefixed by two spaces and the section label
# appears just before it, so we extract them in order.
# ---------------------------------------------------------------------------
RNG_US=$(echo "$OUTPUT"  | grep -A4 "\[RNG"              | grep "Wall time / op" | awk -F: '{print $2}' | awk '{print $1}')
EURO_US=$(echo "$OUTPUT" | grep -A4 "\[Eurojackpot"      | grep "Wall time / op" | awk -F: '{print $2}' | awk '{print $1}')
LOTTO_US=$(echo "$OUTPUT"| grep -A4 "\[Lotto"            | grep "Wall time / op" | awk -F: '{print $2}' | awk '{print $1}')
SEED_US=$(echo "$OUTPUT" | grep -A4 "\[Seed"             | grep "Wall time / op" | awk -F: '{print $2}' | awk '{print $1}')
CSV_US=$(echo "$OUTPUT"  | grep -A4 "\[CSV export"       | grep "Wall time / op" | awk -F: '{print $2}' | awk '{print $1}')
JSON_US=$(echo "$OUTPUT" | grep -A4 "\[JSON export"      | grep "Wall time / op" | awk -F: '{print $2}' | awk '{print $1}')

# ---------------------------------------------------------------------------
# Memory metrics
# ---------------------------------------------------------------------------
BASELINE_RSS=$(echo "$OUTPUT" | grep "Baseline peak RSS"  | awk '{print $NF}' | tr -d 'KB')
POSTDRAW_RSS=$(echo "$OUTPUT" | grep "Post-draw peak RSS" | awk '{print $3}')
MEM_REGRESSION=$(echo "$OUTPUT" | grep -c "MEMORY REGRESSION DETECTED" || true)

# ---------------------------------------------------------------------------
# Produce JSON
# ---------------------------------------------------------------------------
python3 << PYTHON
import json
from datetime import datetime

def safe_float(s, default=0.0):
    try:
        return float(s.strip()) if s and s.strip() else default
    except (ValueError, AttributeError):
        return default

def safe_int(s, default=0):
    try:
        return int(s.strip()) if s and s.strip() else default
    except (ValueError, AttributeError):
        return default

data = {
    "timestamp": datetime.utcnow().isoformat() + "Z",
    "benchmarks": {
        "rng":         {"value": safe_float("${RNG}"),   "unit": "draws/sec"},
        "eurojackpot": {"value": safe_float("${EURO}"),  "unit": "draws/sec"},
        "lotto":       {"value": safe_float("${LOTTO}"), "unit": "draws/sec"},
        "seed":        {"value": safe_float("${SEED}"),  "unit": "seeds/sec"},
    },
    "per_op_timing_us": {
        "rng":         safe_float("${RNG_US}"),
        "eurojackpot": safe_float("${EURO_US}"),
        "lotto":       safe_float("${LOTTO_US}"),
        "seed":        safe_float("${SEED_US}"),
        "csv_export":  safe_float("${CSV_US}"),
        "json_export": safe_float("${JSON_US}"),
    },
    "memory": {
        "baseline_peak_rss_kb": safe_int("${BASELINE_RSS}"),
        "post_draw_peak_rss_kb": safe_int("${POSTDRAW_RSS}"),
        "regression_detected": bool(safe_int("${MEM_REGRESSION}")),
    },
}

with open("${RESULTS_FILE}", "w") as f:
    json.dump(data, f, indent=2)

print("✓ Benchmarks saved to ${RESULTS_FILE}")
PYTHON

# ---------------------------------------------------------------------------
# Memory regression gate (non-zero exit propagated to CI)
# ---------------------------------------------------------------------------
if [ "${MEM_REGRESSION}" -gt 0 ]; then
  echo "⚠  Memory regression detected – see benchmark output above." >&2
  exit 1
fi
