#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Wissem Boussetta
# SPDX-License-Identifier: MIT
set -euo pipefail

BIN="$1"

BUDGET_TABLE_MS="${BUDGET_SIM_ANALYTICS_TABLE_MS:-1200}"
BUDGET_JSON_MS="${BUDGET_SIM_ANALYTICS_JSON_MS:-1400}"

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

check_budget "sim-analytics-table" "$BUDGET_TABLE_MS" \
  "$BIN" --game "Lotto 6aus49" --draws 10000 --seed 0x1234 --simulation-analytics --format table --top 10 --verbose ERROR

check_budget "sim-analytics-json" "$BUDGET_JSON_MS" \
  "$BIN" --game "Lotto 6aus49" --draws 10000 --seed 0x1234 --simulation-analytics --format json --top 10 --verbose ERROR

echo "simulation analytics performance budgets OK"
