<!--
SPDX-FileCopyrightText: 2026 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# Simulation Analytics Metric Catalog (v0.4.0)

This document defines the simulation analytics metrics included in milestone `v0.4.0`.

## Scope

Included metrics:

- Frequency distribution
- Gap and streak statistics
- Hot/cold ranking
- Shannon entropy (normalized)
- Confidence intervals for per-number hit rate (normal approximation)

Out of scope for v0.4.0:

- Real-vs-simulated divergence analysis (planned for v0.5.0)
- Source-level anomaly attribution
- Per-game adaptive weighting models

## Definitions

Let:

- `D` = number of simulated draws
- `K` = picks per draw (main numbers)
- `N` = population size in game range (`max - min + 1`)
- `c(n)` = number of draws containing number `n`

For all formulas below, if `D = 0`, metrics must return neutral values and set `insufficient_data=true`.

## Metric: Frequency Distribution

For each number `n` in `[min, max]`:

- Count: `c(n)`
- Hit rate: `p_hat(n) = c(n) / D`
- Percent: `100 * p_hat(n)`

Acceptance behavior:

- Output must include every number in the configured range.
- Sorting defaults to ascending number.

## Metric: Gap Statistics

For each number `n`, define ordered hit positions `h_1 < h_2 < ... < h_m` where a hit means draw contains `n`.

- Internal gaps: `g_i = h_{i+1} - h_i - 1`
- Current gap: `D - h_m` when `m > 0`, else `D`
- Max gap: `max(g_i, leading_gap, trailing_gap)`

Reported fields:

- `current_gap`
- `max_gap`
- `avg_gap` (0 if no internal gaps)

## Metric: Streak Statistics

A streak is a maximal consecutive run of draws where number `n` appears.

Reported fields:

- `longest_streak`
- `current_streak`
- `streak_count`

## Metric: Hot/Cold Ranking

Ranking key:

- Primary: descending `c(n)` for hot, ascending `c(n)` for cold
- Tie-breaker: ascending number `n`

Reported fields:

- `top_hot[]`
- `top_cold[]`
- Each entry includes `number`, `count`, `percentage`

## Metric: Shannon Entropy (Normalized)

For probability mass function over numbers:

- `p_n = c(n) / sum_k c(k)`
- `H = -sum_n p_n * log2(p_n)` for `p_n > 0`
- `H_norm = H / log2(N)` when `N > 1`, else `0`

Interpretation:

- `H_norm` near `1.0`: more even spread
- `H_norm` near `0.0`: concentrated outcomes

## Metric: Confidence Interval (95%)

For each `n`, using normal approximation:

- `SE = sqrt(p_hat(n) * (1 - p_hat(n)) / D)`
- `CI95 = p_hat(n) ± 1.96 * SE`

Clamp interval to `[0, 1]`.

## Validation Rules

- Deterministic for fixed seed and draw count
- No negative counts or percentages
- Percentages and probabilities must be finite values
- `sum(c(n))` must equal `D * K`

## Example (Lotto 6aus49)

Given `D=10000`, `K=6`, `N=49`:

- Expected mean count per number: `D * K / N = 1224.49`
- Expected hit rate per number: `K / N = 0.122449`

## Versioning

The analytics output schema must embed `schema_version`.

Initial schema for this catalog: `simulation-analytics/v1`.
