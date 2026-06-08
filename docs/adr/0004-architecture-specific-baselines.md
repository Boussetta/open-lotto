# ADR-0004: Architecture-Specific Performance Baselines

**Date:** 2026-06-08
**Status:** Accepted

---

## Context

Open-Lotto is designed for portability across multiple CPU architectures (x86_64, ARM, RISC-V, PowerPC, etc.).
Performance characteristics vary significantly across architectures due to:

- Cache line sizes and memory hierarchy differences
- Instruction set capabilities and throughput
- Memory bandwidth and latency profiles
- SIMD/vectorization availability and efficiency

A single unified baseline file cannot meaningfully compare performance across architectures. For example,
an x86_64 system achieving 1M draws/sec is fundamentally different from an ARM system achieving 800K draws/sec —
both might be optimal for their respective architectures.

Without architecture-specific baselines, the performance regression detection system would:
- False-positively flag ARM results as regressions compared to x86_64 baselines
- Obscure real regressions on a particular architecture with noise from cross-architecture differences
- Prevent meaningful CI/CD validation on multi-architecture CI runners

## Decision

Performance baselines are now stored in architecture-specific files:

### Throughput Baselines
- **Pattern**: `.github/benchmark_baseline_<arch>.json`
- **Examples**: `benchmark_baseline_x86_64.json`, `benchmark_baseline_aarch64.json`
- **Populated by**: CI/CD pipelines and local benchmark runs via `./scripts/run_benchmarks.sh`

### Memory Baselines
- **Pattern**: `benchmark_memory_baseline_<arch>.txt`
- **Examples**: `benchmark_memory_baseline_x86_64.txt`, `benchmark_memory_baseline_aarch64.txt`
- **Populated by**: Benchmark executable (`./build/benchmark`) on first run

### Architecture Detection
Both the benchmark binary (`tests/benchmark.c`) and regression check script (`scripts/check_performance_regression.sh`)
implement architecture detection via:

1. **Compile-time detection** (benchmark.c):
   - Uses preprocessor macros (`__x86_64__`, `__aarch64__`, etc.)
   - Detects at binary runtime, not at compile time

2. **Runtime detection** (regression check):
   - Uses `uname -m` to detect current system architecture
   - Maps to canonical architecture names

### Supported Architectures
- `x86_64` — 64-bit x86
- `i386` — 32-bit x86
- `aarch64` — 64-bit ARM
- `armv7l` — 32-bit ARM (ARMv7)
- `armv6l` — 32-bit ARM (ARMv6, e.g., Raspberry Pi)
- `riscv64` — 64-bit RISC-V
- `ppc64` — 64-bit PowerPC
- Unknown — Falls back to `uname -m` output

## Consequences

### Positive
- ✅ Meaningful performance regression detection across architectures
- ✅ Portable baseline management (each architecture maintains its own history)
- ✅ CI/CD systems can validate performance on multiple architectures independently
- ✅ Reduced false positives from architecture-specific performance differences
- ✅ Clear visibility of which architectures have established baselines

### Negative
- ⚠️ Requires maintaining multiple baseline files per supported architecture
- ⚠️ New architecture support requires establishing baselines (first run creates them)
- ⚠️ More baseline files in the repository (expected: ~5–10 files)

### Neutral
- Backward-incompatible with old single baseline scheme
- Developers must explicitly update the correct architecture baseline

## Rollout

### For Existing Deployments
1. After merge, the first benchmark run on each architecture creates its baseline
2. Subsequent runs on that architecture compare against its baseline
3. CI/CD systems automatically detect architecture and use appropriate baselines

### For CI/CD Integration
GitHub Actions workflows should:
```bash
# Automatically uses architecture-specific baseline
./scripts/check_performance_regression.sh
```

The script automatically detects the runner's architecture and selects the correct baseline.

### For Local Development
```bash
# Detects local architecture automatically
./build/benchmark 100000
# Creates or updates benchmark_memory_baseline_<arch>.txt

# For throughput baselines
./scripts/run_benchmarks.sh build/benchmark_results.json build
ARCH=$(uname -m)
cp build/benchmark_results.json .github/benchmark_baseline_${ARCH}.json
git add .github/benchmark_baseline_${ARCH}.json
```

## References

- **Issue #30**: "Architecture-specific baseline management"
- **Issue #35**: "Improve developer experience tooling"
- [PERFORMANCE.md](../PERFORMANCE.md) — Updated documentation
- [tests/benchmark.c](../../tests/benchmark.c) — Architecture detection implementation
- [scripts/check_performance_regression.sh](../../scripts/check_performance_regression.sh) — Regression check

## Migration Notes

Old single baselines (`.github/benchmark_baseline.json`, `benchmark_memory_baseline.txt`) can be:
1. Migrated to the current system baseline: `mv benchmark_baseline.json benchmark_baseline_$(uname -m).json`
2. Deleted if using a clean baseline approach (first-run generation)
