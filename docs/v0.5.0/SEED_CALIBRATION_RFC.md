<!--
SPDX-FileCopyrightText: 2026 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# Seed Calibration RFC (v0.5.0)

## Purpose
This RFC defines how open-lotto reports best-fit simulator seeds against real historical draws.

## Terminology
- Best-fit simulator seed
- Seed trajectory
- Closest seed

## Non-Goals
- Recovering the real lottery machine seed/state
- Predicting guaranteed future winning numbers

## Windowing Modes
- Fixed buckets
- Rolling windows

## Scoring Model (Draft)
- Frequency distance
- Gap/streak distance
- Rank overlap distance

## Output Diagnostics
- best_seed
- top_k_candidates
- score_gap
- stability_hint

## Worked Examples
- 3-month fixed window
- 3-month rolling window

## Open Questions
- Default metric weights
- Default top-k count
- Optional penalties for overfitting
