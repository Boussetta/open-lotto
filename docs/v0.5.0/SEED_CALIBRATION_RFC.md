<!--
SPDX-FileCopyrightText: 2026 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# Seed Calibration RFC (v0.5.0)

## Purpose
This RFC defines how open-lotto reports best-fit simulator seeds against real historical draws.

## Terminology
- Best-fit simulator seed: a seed value that minimizes a defined fit score for a selected period.
- Seed trajectory: the sequence of best-fit simulator seeds across windows in a date range.
- Closest seed: the minimum-score seed found inside the configured search bounds.

## Non-Goals
- Recovering the real lottery machine seed/state.
- Claiming a causal explanation of real drum mechanics.
- Predicting guaranteed future winning numbers.

## Scientific Positioning
The feature calibrates the simulator against observed historical data. Results represent a
"best synthetic fit" under this simulator and objective function, not the true origin of
historical draws.

## Windowing Modes
- Fixed buckets: non-overlapping windows such as Jan-Mar, Apr-Jun, Jul-Sep.
- Rolling windows: sliding windows of a fixed size and configurable step.

## Scoring Model (v1)
Let $w_f + w_g + w_r = 1$, where each weight is in $[0,1]$.

$$
S(\text{seed}) = w_f D_f + w_g D_g + w_r D_r
$$

Where:
- $D_f$: frequency distribution distance (normalized chi-square).
- $D_g$: gap/streak profile distance (normalized absolute error).
- $D_r$: hot/cold rank mismatch distance.

Tie-break order:
1. lower total score $S$
2. lower frequency distance $D_f$
3. lower seed value

## Output Diagnostics
- `best_seed`: single winning candidate under tie-break rules.
- `top_k_candidates`: list of best $k$ seeds with per-component scores.
- `score_gap`: difference between rank 1 and rank 2 score.
- `stability_hint`: qualitative bucket derived from window-to-window volatility.

## Worked Example A: 3-Month Fixed Window
- Input range: 2026-01-01 to 2026-12-31
- Window size: 3 months
- Step: 3 months
- Output windows: 4 windows and 4 best-fit seeds

## Worked Example B: 3-Month Rolling Window
- Input range: 2026-01-01 to 2026-12-31
- Window size: 3 months
- Step: 1 month
- Output windows: 10 rolling windows and a denser trajectory

## Open Questions
- Default weights for $w_f$, $w_g$, and $w_r$
- Default top-k count for diagnostics
- Optional penalties to reduce overfitting on short windows
