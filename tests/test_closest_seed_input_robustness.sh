#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Wissem Boussetta
# SPDX-License-Identifier: MIT
#
# Robustness checks for --closest-seed-input / --next-draw:
#  - synthesis with very few seeds and many draws never aborts (uniqueness fallback)
#  - pretty-printed (whitespace-reformatted) JSON yields identical predictions
#  - "game" mismatch is rejected (case-insensitive match accepted)
#  - out-of-range historical_draw_count is rejected
#  - --next-draw ignores a config-file "draws" default and rejects --format / --trace-draw

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <open-lotto-binary>" >&2
  exit 2
fi

BIN="$1"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
FIXTURE="$ROOT_DIR/tests/fixtures/historical_lotto_small.csv"
GAME="Lotto 6aus49"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
export HOME="$work/home"
mkdir -p "$HOME"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# expect_fail <pattern> <args...>: command must exit non-zero and print <pattern> on stderr.
expect_fail() {
  local pattern="$1"
  shift
  if "$BIN" "$@" >"$work/out" 2>"$work/err"; then
    fail "expected failure for: $*"
  fi
  grep -q -- "$pattern" "$work/err" || {
    cat "$work/err" >&2
    fail "missing '$pattern' for: $*"
  }
}

"$BIN" --game "$GAME" --closest-seed --historical-csv "$FIXTURE" \
  --from 2025-01-01 --to 2025-02-28 --seed-start 0 --seed-end 200 --max-evals 100 \
  --top 2 --format json --verbose ERROR >"$work/f2.json"
grep -q '"top_candidates"' "$work/f2.json" || fail "closest-seed JSON not produced"

# 1) Few seeds + many draws: positions where every candidate is already used must fall back.
"$BIN" --game "$GAME" --closest-seed-input "$work/f2.json" --draws 3000 --verbose ERROR \
  >"$work/many.txt" || fail "few-seed synthesis with --draws 3000 failed"
grep -q "Synthesized 3000 draws" "$work/many.txt" || fail "3000 draws not synthesized"
# Every draw must have 6 distinct main numbers.
awk '/Main:/ { n = 0; delete seen; for (i = 2; i <= NF && $i != "|"; i++) { if ($i in seen) exit 1; seen[$i] = 1; n++ } if (n != 6) exit 1 }' \
  "$work/many.txt" || fail "duplicate or missing main numbers in synthesized draws"

# 2) Pretty-printed JSON (newlines + spaces around ':' and ',') must parse identically.
sed -e 's/{/{\n    /g' -e 's/\[/[\n  /g' -e 's/,/ ,\n  /g' -e 's/": /"  :  /g' \
  "$work/f2.json" >"$work/pretty.json"
"$BIN" --game "$GAME" --closest-seed-input "$work/f2.json" --draws 5 --verbose ERROR >"$work/a.txt"
"$BIN" --game "$GAME" --closest-seed-input "$work/pretty.json" --draws 5 --verbose ERROR \
  >"$work/b.txt" || fail "pretty-printed JSON rejected"
cmp -s "$work/a.txt" "$work/b.txt" || fail "pretty-printed JSON produced different draws"

# Missing frequency array must fail instead of silently using zeros.
sed -e 's/"main_frequency"/"main_freq_renamed"/' "$work/f2.json" >"$work/nofreq.json"
expect_fail 'main_frequency' --game "$GAME" --closest-seed-input "$work/nofreq.json"

# 3) Game must match --game (case-insensitive).
sed -e 's/"game": "[^"]*"/"game": "Eurojackpot"/' "$work/f2.json" >"$work/other_game.json"
expect_fail "generated for game 'Eurojackpot'" --game "$GAME" --closest-seed-input "$work/other_game.json"
expect_fail "generated for game 'Eurojackpot'" --game "$GAME" --next-draw "$work/other_game.json"
sed -e 's/"game": "[^"]*"/"game": "LOTTO 6AUS49"/' "$work/f2.json" >"$work/upper_game.json"
"$BIN" --game "$GAME" --closest-seed-input "$work/upper_game.json" --verbose ERROR >/dev/null \
  || fail "case-insensitive game match rejected"

# 4) historical_draw_count must be within 1..ANALYTICS_MAX_DRAWS.
sed -e 's/"historical_draw_count": [0-9]*/"historical_draw_count": 2000000000/' \
  "$work/f2.json" >"$work/big.json"
expect_fail 'historical_draw_count' --game "$GAME" --next-draw "$work/big.json"
sed -e 's/"historical_draw_count": [0-9]*/"historical_draw_count": -5/' \
  "$work/f2.json" >"$work/neg.json"
expect_fail 'historical_draw_count' --game "$GAME" --closest-seed-input "$work/neg.json"

# Negative seeds must not wrap to UINT64_MAX.
sed -e '0,/"seed": [0-9]*/s//"seed": -1/' "$work/f2.json" >"$work/negseed.json"
expect_fail 'seed' --game "$GAME" --closest-seed-input "$work/negseed.json"

# 5) --next-draw: config-file draws default is ignored; --format/--trace-draw are rejected.
printf '[defaults]\ndraws = 5\n' >"$HOME/.lottorc"
"$BIN" --game "$GAME" --next-draw "$work/f2.json" --verbose ERROR >"$work/next.txt" 2>"$work/next.err" \
  || fail "--next-draw failed with config-file draws default"
grep -q "Synthesized next draw" "$work/next.txt" || fail "--next-draw output missing"
rm -f "$HOME/.lottorc"
expect_fail '--format is not supported' --game "$GAME" --next-draw "$work/f2.json" --format json
expect_fail '--format is not supported' --game "$GAME" --closest-seed-input "$work/f2.json" --format csv
expect_fail '--trace-draw cannot be used with --next-draw' --game "$GAME" --next-draw "$work/f2.json" --trace-draw 1

echo "closest-seed input robustness OK"
