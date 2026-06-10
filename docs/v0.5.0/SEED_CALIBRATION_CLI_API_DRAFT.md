<!--
SPDX-FileCopyrightText: 2026 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# Seed Calibration CLI/API Draft (v0.5.0)

## Commands
- `--seed-trajectory`: compute best-fit seed per window over a date range.
- `--closest-seed`: compute the minimum-score seed for one selected period.

## Shared Options
- `--from YYYY-MM-DD` (required)
- `--to YYYY-MM-DD` (required)
- `--historical-csv FILE` (optional override)
- `--format table|json|csv` (default: `table`)
- `--top N` (default: `10`, min: `1`)
- `--score-profile balanced|frequency-heavy|streak-heavy` (default: `balanced`)

## Seed-Trajectory Options
- `--window-months N` (required, min: `1`)
- `--step-months N` (optional, default: `window-months`)
- `--seed-start VALUE` (required)
- `--seed-end VALUE` (required)
- `--max-evals-per-window N` (default: `50000`)
- `--threads N` (default: `1`)
- `--timeout-ms N` (optional)

## Closest-Seed Options
- `--seed-start VALUE` (required)
- `--seed-end VALUE` (required)
- `--max-evals N` (default: `100000`)
- `--threads N` (default: `1`)
- `--timeout-ms N` (optional)

## Determinism Rules
- Search order is ascending seed unless a future strategy explicitly documents otherwise.
- Tie-break order: total score, frequency score, lower seed.
- With equal inputs and equal build, outputs must be byte-stable in JSON mode.

## JSON Output Contract (Draft)
Top-level fields:
- `mode`
- `schema_version`
- `period`
- `summary`
- `diagnostics`
- `top_candidates`

`summary` includes:
- `best_seed`
- `best_score`
- `evaluated_seeds`
- `elapsed_ms`

`diagnostics` includes:
- `score_gap`
- `score_components`
- `stability_hint` (trajectory mode)

## C API Draft (Planning)
```c
int seed_calibration_find_closest(const SeedCalibrationRequest *req,
                                  SeedCalibrationResult *out);

int seed_calibration_compute_trajectory(const SeedTrajectoryRequest *req,
                                        SeedTrajectoryResult *out);
```

## CLI Examples
1. Closest seed on historical DB snapshot:
`./open-lotto --game "Lotto 6aus49" --closest-seed --from 2026-01-01 --to 2026-06-10 --seed-start 0 --seed-end 500000 --max-evals 100000 --format json`

2. Closest seed using CSV override:
`./open-lotto --game "Lotto 6aus49" --closest-seed --historical-csv tests/fixtures/historical_lotto_small.csv --from 2025-01-01 --to 2025-01-31 --seed-start 0 --seed-end 100000 --format table`

3. Trajectory with fixed 3-month windows:
`./open-lotto --game "Lotto 6aus49" --seed-trajectory --from 2026-01-01 --to 2026-12-31 --window-months 3 --step-months 3 --seed-start 0 --seed-end 1000000 --max-evals-per-window 50000 --format csv`

4. Rolling trajectory (3-month window, 1-month step):
`./open-lotto --game "Lotto 6aus49" --seed-trajectory --from 2026-01-01 --to 2026-12-31 --window-months 3 --step-months 1 --seed-start 0 --seed-end 250000 --format json`

5. Closest seed with thread cap:
`./open-lotto --game "Eurojackpot" --closest-seed --from 2026-01-01 --to 2026-06-10 --seed-start 0 --seed-end 500000 --threads 4 --format json`

6. Closest seed with timeout:
`./open-lotto --game "Lotto 6aus49" --closest-seed --from 2026-01-01 --to 2026-06-10 --seed-start 0 --seed-end 1000000 --timeout-ms 120000 --format json`

## Error Handling
- invalid date format or `from > to`.
- non-positive `window-months`, `step-months`, `max-evals`, `threads`.
- seed range where `seed-start > seed-end`.
- unsupported combination with GUI/animate-only modes.
