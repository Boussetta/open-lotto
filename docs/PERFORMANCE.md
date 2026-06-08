<!--
SPDX-FileCopyrightText: 2025 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

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

Baselines are stored in architecture-specific files to ensure meaningful comparisons across different hardware platforms:

- **Throughput baselines**: `.github/benchmark_baseline_<arch>.json` (e.g., `benchmark_baseline_x86_64.json`)
- **Memory baselines**: `benchmark_memory_baseline_<arch>.txt` (e.g., `benchmark_memory_baseline_x86_64.txt`)

Supported architectures: `x86_64`, `aarch64`, `armv7l`, `armv6l`, `riscv64`, `ppc64`, and others.

### Update Baseline

Run on a clean system with no background load:

```bash
# Generate benchmarks for current architecture
./scripts/run_benchmarks.sh build/benchmark_results.json build

# Copy to architecture-specific baseline file
ARCH=$(uname -m)
cp build/benchmark_results.json .github/benchmark_baseline_${ARCH}.json
git add .github/benchmark_baseline_${ARCH}.json
git commit -m "chore: update performance baseline for ${ARCH}"
```

Memory baselines are created automatically on first run with the benchmark executable.

### When to Update
- After significant algorithm optimizations
- After changing compiler flags
- When intentionally accepting performance changes
- When establishing baselines for a new architecture

**Do NOT** update baseline to hide regressions.

## CI Integration

The `performance` job runs on every commit:
1. Builds in Release mode (with optimizations)
2. Detects system architecture automatically
3. Runs benchmarks (100k iterations)
4. Checks for >5% regressions against architecture-specific baselines (fails if exceeded)
5. Uploads `benchmark_results.json` as artifact

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
- [x] Architecture-specific baselines (x86_64, ARM, etc.)
