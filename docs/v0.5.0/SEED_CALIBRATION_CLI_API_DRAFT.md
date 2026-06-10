<!--
SPDX-FileCopyrightText: 2026 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# Seed Calibration CLI/API Draft (v0.5.0)

## Commands
- --seed-trajectory
- --closest-seed

## Shared Options
- --from YYYY-MM-DD
- --to YYYY-MM-DD
- --format table|json|csv
- --top N

## Seed-Trajectory Options
- --window-months N
- --step-months N

## Closest-Seed Options
- --seed-start VALUE
- --seed-end VALUE
- --max-evals N
- --threads N
- --timeout-ms N

## Output Fields (Draft)
- summary
- diagnostics
- top_candidates

## Error Handling
- invalid dates
- invalid ranges
- unsupported option combinations
