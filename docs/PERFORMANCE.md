# Performance Testing & Regression Detection

## Overview

The open-lotto project includes automated performance benchmarking and regression detection to ensure code changes don't degrade performance.

## Benchmark Metrics

The benchmark measures:
- **RNG draws/sec** — PCG32 random number generation throughput
- **Eurojackpot draws/sec** — Full draw simulation (50 main + 12 extra numbers)
- **Lotto 6aus49 draws/sec** — Full draw simulation (6 main + 0 extra numbers)
- **Seed generation/sec** — Entropy seed generation speed

## Local Testing

### Run Benchmarks
```bash
./scripts/run_benchmarks.sh [output_json] [build_dir]
```

Runs the benchmark executable (100,000 iterations) and saves results to JSON.

**Example:**
```bash
./scripts/run_benchmarks.sh build/benchmark_results.json build
```

**Output:** JSON file with timestamp and benchmark values
```json
{
  "timestamp": "2026-06-05T08:08:32.086761Z",
  "benchmarks": {
    "rng": {"value": 544781.0, "unit": "draws/sec"},
    "eurojackpot": {"value": 534676.0, "unit": "draws/sec"},
    "lotto": {"value": 570067.0, "unit": "draws/sec"},
    "seed": {"value": 5760369.0, "unit": "seeds/sec"}
  }
}
```

### Check for Regressions
```bash
./scripts/check_performance_regression.sh [current_json] [baseline_json] [threshold_percent]
```

Compares current results against baseline. Fails (exit 1) if any metric degrades by more than the threshold.

**Example:**
```bash
./scripts/check_performance_regression.sh \
  build/benchmark_results.json \
  .github/benchmark_baseline.json \
  5
```

**Output:**
```
Checking performance regressions (threshold: 5%)...
✗ REGRESSION rng: 18.5%
↑ eurojackpot: 1.8%
✓ lotto: 2.1%
✓ seed: 0.2%
```

## Baseline Management

The baseline is stored in `.github/benchmark_baseline.json` and represents expected throughput.

### Update Baseline
Run on a clean system with no background load:
```bash
./scripts/run_benchmarks.sh build/benchmark_results.json build
cp build/benchmark_results.json .github/benchmark_baseline.json
git add .github/benchmark_baseline.json
git commit -m "chore: update performance baseline"
```

### When to Update
- After significant algorithm optimizations
- After changing compiler flags
- When intentionally accepting performance changes

**Do NOT** update baseline to hide regressions.

## CI Integration

The `performance` job runs on every commit:
1. Builds in Release mode (with optimizations)
2. Runs benchmarks (100k iterations)
3. Checks for >5% regressions (fails if exceeded)
4. Uploads `benchmark_results.json` as artifact

Results are available in GitHub Actions artifacts for historical tracking.

## Interpreting Results

### Normal Variance
±2-3% variation between runs is normal due to:
- System scheduler variation
- CPU frequency scaling
- Cache behavior

### Regressions (>5%)
Indicates a real performance impact. Investigate:
1. Algorithm changes
2. New allocations/memory usage
3. Additional conditionals in hot paths
4. Disabled optimizations

### Improvements (>5% faster)
Usually from:
- Algorithm optimization
- Reduced allocations
- Better cache locality
- Compiler improvements

## Benchmark Code

The benchmark is in `tests/benchmark.c`. It measures:
- Pure RNG throughput (no draw logic)
- Per-game draw cycles with full state machine
- Entropy seed generation

Run locally:
```bash
./build/benchmark 100000
```

## Future Enhancements

- [ ] Per-commit historical tracking
- [ ] Comparative analysis (main vs branch)
- [ ] Memory profiling (peak usage, allocations)
- [ ] CPU profiling (hot paths)
- [ ] Architecture-specific baselines (x86_64, ARM, etc.)
