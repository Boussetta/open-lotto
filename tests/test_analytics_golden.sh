#!/usr/bin/env bash
set -euo pipefail

BIN="$1"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

run_and_compare() {
  local name="$1"
  local golden="$2"
  shift 2

  local actual
  actual="$(mktemp)"
  "$BIN" "$@" --verbose ERROR > "$actual"

  if ! diff -u "$golden" "$actual"; then
    echo "golden mismatch: $name"
    rm -f "$actual"
    exit 1
  fi

  rm -f "$actual"
  echo "golden ok: $name"
}

run_and_compare \
  "frequency-json" \
  "$ROOT_DIR/tests/golden/analytics_frequency_json.golden" \
  --game "Lotto 6aus49" --frequency-distribution --from 2025-01-01 --to 2025-01-31 \
  --historical-csv "$ROOT_DIR/tests/fixtures/historical_lotto_small.csv" --format json

run_and_compare \
  "barometer-gui2d" \
  "$ROOT_DIR/tests/golden/analytics_barometer_gui2d.golden" \
  --game "Lotto 6aus49" --analytics-barometer --from 2025-03-01 --to 2025-03-31 \
  --historical-csv "$ROOT_DIR/tests/fixtures/historical_lotto_barometer.csv" --gui 2D

run_and_compare \
  "hotcold-gui3d" \
  "$ROOT_DIR/tests/golden/analytics_hotcold_gui3d.golden" \
  --game "Lotto 6aus49" --analytics-hot-cold --from 2025-01-01 --to 2025-02-28 \
  --historical-csv "$ROOT_DIR/tests/fixtures/historical_lotto_small.csv" --top 5 --gui 3D

echo "analytics golden baselines OK"
