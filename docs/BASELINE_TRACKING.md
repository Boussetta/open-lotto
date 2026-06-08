<!--
SPDX-FileCopyrightText: 2025 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# Performance Baseline Tracking Guide

This guide explains how to track, manage, and interpret historical performance baselines in Open-Lotto.

## Overview

Open-Lotto maintains a historical record of performance baselines to detect:
- Performance regressions (sudden drops)
- Gradual performance degradation (trends)
- Performance improvements
- Architecture-specific differences

## Baseline Management

### Adding Baselines

Baselines are automatically collected and archived by the CI pipeline during every performance job run. To manually add a baseline:

```bash
./scripts/manage_baselines.py add ./build/benchmark_results.json
```

With optional metadata:

```bash
./scripts/manage_baselines.py add ./build/benchmark_results.json \
  --commit $(git rev-parse HEAD) \
  --arch x86_64
```

### Viewing Baseline History

The baseline history is stored in `.github/benchmarks/history.json` with entries for each recorded baseline:

```json
{
  "2026-06-08T12:00:00Z": {
    "timestamp": "2026-06-08T12:00:00Z",
    "commit": "abc123def456...",
    "benchmarks": {
      "rng": { "value": 544781.0, "unit": "draws/sec" },
      "eurojackpot": { "value": 534676.0, "unit": "draws/sec" },
      "lotto": { "value": 570067.0, "unit": "draws/sec" },
      "seed": { "value": 5760369.0, "unit": "seeds/sec" }
    },
    "metadata": { "architecture": "x86_64" }
  },
  ...
}
```

## Viewing Performance Data

### Generate Performance Report

Generate a comprehensive performance report:

```bash
python3 ./scripts/manage_baselines.py report
```

Output includes:
- Current baseline values
- Oldest baseline values
- Trend direction and percentage change
- Any detected performance degradation

### Show Performance Trend

View a specific metric's trend over time:

```bash
python3 ./scripts/manage_baselines.py trend lotto --limit 10
```

Output:
```
Trend for lotto (10 entries):
  2026-05-28T10:00:00Z: 565000.5
  2026-05-29T10:00:00Z: 568000.2
  2026-05-30T10:00:00Z: 570000.1
  ...
```

### Get Statistical Summary

View min, max, mean, and standard deviation:

```bash
python3 ./scripts/manage_baselines.py stats eurojackpot
```

Output:
```
Statistics for eurojackpot:
  Count: 15
  Min: 520000.0
  Max: 535000.0
  Mean: 530000.0
  Median: 531000.0
  StdDev: 4500.0
```

### Detect Performance Degradation

Check for gradual performance degradation:

```bash
python3 ./scripts/manage_baselines.py degrade rng --threshold 5
```

Output (if degradation detected):
```
⚠️  Performance degradation detected: 7.2%
  Historical average: 560000.0
  Recent average: 520000.0
```

## CI Integration

### Automatic Baseline Collection

The CI pipeline automatically:
1. Runs benchmarks on every performance job
2. Stores results in `build/benchmark_results.json`
3. Archives baseline in `.github/benchmarks/history.json`
4. Compares against current baseline for regression detection

### Release Baseline Archival

When releasing a new version, baselines are archived with the release:
1. Create a new baseline file: `.github/benchmarks/baseline_vX.Y.Z.json`
2. Reference this baseline in release notes
3. Use for long-term trend analysis

## Performance Regression Detection

### Immediate Regression (vs. Current Baseline)

The CI detects immediate performance regressions by comparing new results against `.github/benchmark_baseline.json`:

```bash
./scripts/check_performance_regression.sh \
  ./build/benchmark_results.json \
  ./.github/benchmark_baseline.json \
  5  # threshold: 5%
```

**Action if detected:**
- CI job fails
- Review code changes since last baseline update
- Identify optimization opportunity or revert change

### Gradual Degradation (Historical Trend)

Detect gradual performance degradation over multiple commits:

```bash
python3 ./scripts/manage_baselines.py degrade eurojackpot --threshold 5
```

**Action if detected:**
- Investigate recent changes to affected code paths
- Profile with `perf` or `valgrind`
- Identify optimization opportunity
- Consider bisecting commits to pinpoint change

## Baseline Update Procedures

### After Optimization

When performance improves intentionally (after optimization):

1. **Verify improvement is real:**
   ```bash
   ./build/benchmark
   ./build/benchmark
   ./build/benchmark
   ```
   Run 3 times to confirm consistency.

2. **Add baseline:**
   ```bash
   python3 ./scripts/manage_baselines.py add ./build/benchmark_results.json
   ```

3. **Update current baseline:**
   ```bash
   cp ./build/benchmark_results.json ./.github/benchmark_baseline.json
   ```

4. **Commit:**
   ```bash
   git add .github/benchmark_baseline.json .github/benchmarks/history.json
   git commit -m "perf: update baseline after optimization"
   ```

### Periodic Baseline Snapshots (Weekly)

The CI can be configured to create a weekly baseline snapshot:

```yaml
schedule:
  - cron: '0 0 ? * MON'  # Every Monday at midnight UTC
```

This ensures regular tracking of performance trends even without code changes.

### Architecture-Specific Baselines

Different architectures may have different baseline values. Store them separately:

- `.github/benchmark_baseline_x86_64.json`
- `.github/benchmark_baseline_aarch64.json`
- `.github/benchmark_baseline_arm.json`

The `check_performance_regression.sh` script automatically detects and uses the correct baseline.

## Interpreting Results

### What Good Performance Looks Like

- **Flat trend** (±1-2%): Normal variance, consistent performance
- **Upward trend** (>5%): Performance improved (usually intentional optimization)
- **Downward trend** (>5%): Performance degraded (investigate cause)
- **High variance** (>10%): System load interference; take multiple samples

### Common Causes of Variance

| Cause | Magnitude | Solution |
|-------|-----------|----------|
| Compilation flags | 5-20% | Compare with `-O2` vs. `-O3` |
| System load | 2-5% | Run benchmarks in isolation |
| Thermal throttling | 10-30% | Ensure consistent CPU frequency |
| CPU frequency scaling | 5-15% | Disable boost during benchmarks |
| Memory pressure | 5-10% | Close other applications |
| Cache state | 2-5% | Run benchmark multiple times |

### Interpreting Metrics

| Metric | Unit | Higher is Better | Target |
|--------|------|------------------|--------|
| `rng` | draws/sec | Yes | >500k |
| `eurojackpot` | draws/sec | Yes | >500k |
| `lotto` | draws/sec | Yes | >550k |
| `seed` | seeds/sec | Yes | >5M |

## Baseline History Structure

### Directory Layout

```
.github/benchmarks/
├── history.json              # All historical baselines (JSON)
├── baseline_v1.0.0.json      # Release baseline snapshots
├── baseline_v1.1.0.json
└── ...
```

### History Entry Format

```json
{
  "timestamp": "2026-06-08T12:00:00Z",
  "commit": "abc123def456...",
  "benchmarks": {
    "metric_name": {
      "value": 123456.0,
      "unit": "draws/sec"
    }
  },
  "metadata": {
    "architecture": "x86_64",
    "compiler": "gcc-11",
    "flags": "-O3 -march=native"
  }
}
```

## Troubleshooting

### No Baseline History Available

**Issue:** `manage_baselines.py report` returns empty

**Cause:** First run, no baselines collected yet

**Solution:** Run benchmarks and add baseline:
```bash
cmake --build build
./scripts/run_benchmarks.sh ./build/benchmark_results.json ./build
python3 ./scripts/manage_baselines.py add ./build/benchmark_results.json
```

### Unreliable Degradation Detection

**Issue:** False positive degradation warnings

**Cause:** High variance in measurements (system load, thermal issues)

**Solution:** 
1. Increase threshold: `--threshold 10` instead of 5
2. Run benchmarks multiple times and take average
3. Disable CPU boost: `echo 0 | sudo tee /sys/devices/system/cpu/cpufreq/boost`
4. Close background applications

### Baseline Not Updating

**Issue:** Changes don't affect regression detection

**Cause:** Only `.github/benchmark_baseline.json` is used for regression

**Solution:** Update the current baseline after confirming improvement:
```bash
cp ./build/benchmark_results.json ./.github/benchmark_baseline.json
git add .github/benchmark_baseline.json
git commit -m "perf: update baseline"
```

## Best Practices

1. **Take multiple samples** — Run benchmarks 3+ times and use the best result
2. **Run in isolation** — Close other applications to reduce variance
3. **Document changes** — Include performance impact in commit messages
4. **Regular reviews** — Check trends weekly for unexpected degradation
5. **Baseline after optimization** — Create release baselines with version tags
6. **Monitor architecture-specific** — Track performance on different CPU architectures

## References

- [PERFORMANCE_TUNING.md](PERFORMANCE_TUNING.md) — Profiling and optimization techniques
- [PERFORMANCE.md](PERFORMANCE.md) — Current benchmark results and notes
- [Scripts](../scripts/) — Benchmark and baseline management scripts
