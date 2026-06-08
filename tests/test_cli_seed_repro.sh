#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <open-lotto-binary>" >&2
    exit 2
fi

bin="$1"
out1="$(mktemp)"
out2="$(mktemp)"
out3="$(mktemp)"
trap 'rm -f "$out1" "$out2" "$out3"' EXIT

"$bin" --game "Lotto 6aus49" --draws 2 --seed 0x4242 >"$out1" 2>/dev/null
"$bin" --game "Lotto 6aus49" --draws 2 --seed 0x4242 >"$out2" 2>/dev/null
"$bin" --game "Lotto 6aus49" --draws 2 --seed 0x4243 >"$out3" 2>/dev/null

if ! diff -u "$out1" "$out2" >/dev/null; then
    echo "seed reproducibility failed" >&2
    exit 1
fi

if cmp -s "$out1" "$out3"; then
    echo "different seeds unexpectedly matched" >&2
    exit 1
fi

echo "seed reproducibility OK"
