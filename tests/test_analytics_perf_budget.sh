#!/usr/bin/env bash
set -euo pipefail

BIN="$1"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

# Budgets in milliseconds on small deterministic fixtures.
BUDGET_FREQUENCY_MS="${BUDGET_FREQUENCY_MS:-800}"
BUDGET_BAROMETER_MS="${BUDGET_BAROMETER_MS:-800}"
BUDGET_HOTCOLD_MS="${BUDGET_HOTCOLD_MS:-800}"

measure_ms() {
  local start end elapsed
  start="$(date +%s%N)"
  "$@" >/dev/null
  end="$(date +%s%N)"
  elapsed=$(( (end - start) / 1000000 ))
  printf '%d' "$elapsed"
}

check_budget() {
  local name="$1"
  local budget_ms="$2"
  shift 2

  local elapsed
  elapsed="$(measure_ms "$@")"
  echo "$name: ${elapsed}ms (budget: ${budget_ms}ms)"

  if [[ "$elapsed" -gt "$budget_ms" ]]; then
    echo "Performance budget exceeded for $name: ${elapsed}ms > ${budget_ms}ms"
    return 1
  fi

  return 0
}

check_budget "frequency" "$BUDGET_FREQUENCY_MS" \
  "$BIN" --game "Lotto 6aus49" --frequency-distribution --from 2025-01-01 --to 2025-01-31 \
  --historical-csv "$ROOT_DIR/tests/fixtures/historical_lotto_small.csv" --format json --verbose ERROR

check_budget "barometer" "$BUDGET_BAROMETER_MS" \
  "$BIN" --game "Lotto 6aus49" --analytics-barometer --from 2025-03-01 --to 2025-03-31 \
  --historical-csv "$ROOT_DIR/tests/fixtures/historical_lotto_barometer.csv" --format json --verbose ERROR

check_budget "hot-cold" "$BUDGET_HOTCOLD_MS" \
  "$BIN" --game "Lotto 6aus49" --analytics-hot-cold --from 2025-01-01 --to 2025-02-28 \
  --historical-csv "$ROOT_DIR/tests/fixtures/historical_lotto_small.csv" --top 5 --format json --verbose ERROR

echo "analytics performance budgets OK"
