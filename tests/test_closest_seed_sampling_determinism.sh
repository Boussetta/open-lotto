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
  --seed-count 1000 \
  --sample-seed 0x1234 \
  --threads 4 \
  --format json \
  --verbose ERROR > "$out1"

"$BIN" --game "Lotto 6aus49" \
  --closest-seed \
  --historical-csv "$FIXTURE" \
  --from 2025-01-01 \
  --to 2025-02-28 \
  --seed-count 1000 \
  --sample-seed 0x1234 \
  --threads 4 \
  --format json \
  --verbose ERROR > "$out2"

if ! cmp -s "$out1" "$out2"; then
  echo "closest-seed sampled mode output is not deterministic" >&2
  exit 1
fi

if ! grep -q '"mode": "closest-seed"' "$out1"; then
  echo "closest-seed mode field missing in sampled JSON" >&2
  exit 1
fi

if ! grep -q '"evaluated_seeds": 1000' "$out1"; then
  echo "sampled evaluated_seeds mismatch" >&2
  exit 1
fi

echo "closest-seed sampled determinism OK"
