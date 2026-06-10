<!--
SPDX-FileCopyrightText: 2026 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# Seed Calibration Test and Performance Strategy (v0.5.0)

## Test Matrix
- Unit:
  - score component functions (frequency/gap/rank)
  - comparator and tie-break behavior
  - window partition logic
- Integration:
  - closest-seed command with fixed fixture
  - seed-trajectory command with fixed fixture
- End-to-end:
  - deterministic replay of JSON outputs on same inputs
  - top-k ordering consistency
- Property:
  - monotonic constraints (top-1 score <= top-2 score)
  - bounds and finite-value invariants
- Performance budgets:
  - small profile budget
  - medium profile budget

## Risk-to-Test Mapping
- scoring correctness:
  - unit assertions with hand-computed expected values
- determinism:
  - repeated runs byte-compare JSON output
- CLI validation:
  - invalid range and invalid period WILL_FAIL tests
- runtime regressions:
  - per-command budget checks with deterministic fixtures
- flaky performance signals:
  - moving average trend checks and conservative threshold margins

## Determinism Cases
- fixed period + fixed seed range + fixed max-evals must produce stable best seed.
- fixed top-k output must preserve rank and score order.
- parallel runs with same thread count must be stable.

## Proposed Budget Env Vars
- `BUDGET_SEED_CLOSEST_SMALL_MS`
- `BUDGET_SEED_CLOSEST_MEDIUM_MS`
- `BUDGET_SEED_TRAJECTORY_SMALL_MS`
- `BUDGET_SEED_TRAJECTORY_MEDIUM_MS`

## Anti-Flake Policy
- Use small deterministic fixtures in CI.
- Keep default budgets realistic for shared runners.
- Avoid GUI dependencies in calibration tests.
- Retries are allowed only for perf jobs and must report both attempts.

## CI Gates (Planned)
1. `seed-calibration-contract`
2. `seed-calibration-determinism`
3. `seed-calibration-perf-budget`

A gate fails if any test in its suite fails or exceeds budget.
