#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Wissem Boussetta
# SPDX-License-Identifier: MIT

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <open-lotto-binary>" >&2
  exit 2
fi

BIN="$1"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
FIXTURE="$ROOT_DIR/tests/fixtures/historical_lotto_small.csv"

out1="$(mktemp)"
out2="$(mktemp)"
trap 'rm -f "$out1" "$out2"' EXIT

"$BIN" --game "Lotto 6aus49" \
  --closest-seed \
  --historical-csv "$FIXTURE" \
  --from 2025-01-01 \
  --to 2025-02-28 \
  --seed-start 0 \
  --seed-end 200 \
  --max-evals 100 \
  --top 10 \
  --format json \
  --verbose ERROR > "$out1"

"$BIN" --game "Lotto 6aus49" \
  --closest-seed \
  --historical-csv "$FIXTURE" \
  --from 2025-01-01 \
  --to 2025-02-28 \
  --seed-start 0 \
  --seed-end 200 \
  --max-evals 100 \
  --top 10 \
  --format json \
  --verbose ERROR > "$out2"

if ! cmp -s "$out1" "$out2"; then
  echo "closest-seed output is not deterministic" >&2
  exit 1
fi

if ! grep -q '"mode": "closest-seed"' "$out1"; then
  echo "closest-seed mode field missing in JSON" >&2
  exit 1
fi

if ! grep -q '"schema_version": "seed-calibration/v1"' "$out1"; then
  echo "closest-seed schema_version missing in JSON" >&2
  exit 1
fi

echo "closest-seed deterministic output OK"
