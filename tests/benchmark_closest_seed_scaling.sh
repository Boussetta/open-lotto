#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Wissem Boussetta
# SPDX-License-Identifier: MIT

set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <open-lotto-binary> [seed_count] [threads_csv]" >&2
  echo "example: $0 ./build/open-lotto 200000 1,2,4,8" >&2
  exit 2
fi

BIN="$1"
SEED_COUNT="${2:-200000}"
THREADS_CSV="${3:-1,2,4,8}"

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
FIXTURE="$ROOT_DIR/tests/fixtures/historical_lotto_small.csv"

echo "closest-seed scaling benchmark"
echo "seed_count=$SEED_COUNT"
echo "threads=$THREADS_CSV"
echo

t1=""
printf "threads,elapsed_sec,seeds_per_sec,speedup\n"

for t in ${THREADS_CSV//,/ }; do
  out="$(mktemp)"
  elapsed="$(/usr/bin/time -f '%e' "$BIN" \
    --game "Lotto 6aus49" \
    --closest-seed \
    --historical-csv "$FIXTURE" \
    --from 2025-01-01 \
    --to 2025-02-28 \
    --seed-count "$SEED_COUNT" \
    --sample-seed 0x1234 \
    --threads "$t" \
    --format json \
    --verbose ERROR > "$out" 2>&1 >/dev/null; cat "$out")"

  seconds="$(echo "$elapsed" | tail -n 1)"
  if [[ -z "$t1" && "$t" == "1" ]]; then
    t1="$seconds"
  fi

  seeds_per_sec="$(awk -v n="$SEED_COUNT" -v s="$seconds" 'BEGIN { if (s > 0) printf "%.3f", n/s; else print "0.000" }')"
  speedup="$(awk -v base="${t1:-$seconds}" -v cur="$seconds" 'BEGIN { if (cur > 0) printf "%.3f", base/cur; else print "0.000" }')"
  printf "%s,%s,%s,%s\n" "$t" "$seconds" "$seeds_per_sec" "$speedup"
  rm -f "$out"
done
