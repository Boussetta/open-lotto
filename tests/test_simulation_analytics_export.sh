#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Wissem Boussetta
# SPDX-License-Identifier: MIT
set -euo pipefail

BIN="$1"

json_out="$(mktemp)"
csv_out="$(mktemp)"

"$BIN" --game "Lotto 6aus49" --draws 200 --seed 0x42 --simulation-analytics --export json --output "$json_out" --top 3 --verbose ERROR
"$BIN" --game "Lotto 6aus49" --draws 200 --seed 0x42 --simulation-analytics --export csv --output "$csv_out" --top 3 --verbose ERROR

if ! grep -q '"schema_version": "simulation-analytics/v1"' "$json_out"; then
  echo "json export missing schema_version"
  exit 1
fi

if ! grep -q '"advanced"' "$json_out"; then
  echo "json export missing advanced block"
  exit 1
fi

if ! grep -q '^section,key,value,extra$' "$csv_out"; then
  echo "csv export missing header"
  exit 1
fi

if ! grep -q '^metadata,schema_version,simulation-analytics/v1,$' "$csv_out"; then
  echo "csv export missing schema_version"
  exit 1
fi

rm -f "$json_out" "$csv_out"
echo "simulation analytics export OK"
