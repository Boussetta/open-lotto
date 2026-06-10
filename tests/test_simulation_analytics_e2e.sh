#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Wissem Boussetta
# SPDX-License-Identifier: MIT
set -euo pipefail

BIN="$1"

out1="$(mktemp)"
out2="$(mktemp)"

"$BIN" --game "Lotto 6aus49" --draws 500 --seed 0xfeed --simulation-analytics --format json --top 5 --verbose ERROR > "$out1"
"$BIN" --game "Lotto 6aus49" --draws 500 --seed 0xfeed --simulation-analytics --format json --top 5 --verbose ERROR > "$out2"

if ! cmp -s "$out1" "$out2"; then
  echo "simulation analytics e2e output is not deterministic"
  rm -f "$out1" "$out2"
  exit 1
fi

if ! grep -q '"schema_version": "simulation-analytics/v1"' "$out1"; then
  echo "missing schema_version in e2e output"
  rm -f "$out1" "$out2"
  exit 1
fi

rm -f "$out1" "$out2"
echo "simulation analytics e2e deterministic OK"
