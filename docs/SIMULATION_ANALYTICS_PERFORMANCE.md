# Simulation Analytics Performance Baseline

This document tracks the baseline budget for simulation analytics in v0.4.0.

## Benchmark Command

```bash
./open-lotto --game "Lotto 6aus49" --draws 10000 --seed 0x1234 --simulation-analytics --format table --top 10 --verbose ERROR
./open-lotto --game "Lotto 6aus49" --draws 10000 --seed 0x1234 --simulation-analytics --format json --top 10 --verbose ERROR
```

## Initial Budgets

- table output: 1200 ms
- json output: 1400 ms

These limits are enforced in CI via:

- `tests/test_simulation_analytics_perf_budget.sh`

## Notes

- Budgets can be tuned per environment with:
  - `BUDGET_SIM_ANALYTICS_TABLE_MS`
  - `BUDGET_SIM_ANALYTICS_JSON_MS`
- Keep thresholds tight enough to detect regressions, but avoid flaky values.
