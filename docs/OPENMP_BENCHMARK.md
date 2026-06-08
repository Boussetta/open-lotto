<!--
SPDX-FileCopyrightText: 2025 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# OpenMP Performance Benchmark Guide

This document explains the `benchmark_openmp` utility and how to interpret its results.

## Overview

The `benchmark_openmp` tool demonstrates the performance impact of OpenMP parallelization on compute-intensive operations used in the Open-Lotto 3D GUI. It compares execution time with serial (1 thread) vs. parallel (multiple threads) execution.

## Building

The `benchmark_openmp` executable is built as part of the project when `BUILD_TESTING` is enabled:

```bash
cd /path/to/open-lotto
mkdir -p build && cd build
cmake -DBUILD_TESTING=ON ..
make benchmark_openmp
```

## Running

### Basic Usage

```bash
# Default: 1000 balls, 100 iterations per benchmark
./benchmark_openmp

# Custom parameters
./benchmark_openmp 5000 20

# Show help
./benchmark_openmp --help
```

### With OpenMP Environment Control

```bash
# Run with specific number of threads
OMP_NUM_THREADS=8 ./benchmark_openmp 5000

# Use dynamic scheduling for better load balancing
OMP_SCHEDULE=dynamic ./benchmark_openmp 5000

# Disable parallelization (use only 1 thread)
OMP_NUM_THREADS=1 ./benchmark_openmp
```

### Common Examples

```bash
# Quick test: small problem size
./benchmark_openmp 500 50

# Medium test: realistic ball count
./benchmark_openmp 1000 100

# Large test: significant computational work
./benchmark_openmp 10000 50

# Very large: see maximum parallelization benefits
./benchmark_openmp 50000 10
```

## Understanding the Results

### Operations Benchmarked

1. **Ball Initialization**
   - Simulates initializing ball positions and velocities
   - Used in `drum_instance_init_balls()`
   - Small/fast operation → parallelization overhead may dominate for small ball counts

2. **Ball Data Sync (GPU↔CPU)**
   - Simulates synchronizing ball data between CPU and GPU
   - Used in `sync_cpu_balls_to_gpu()` and `sync_gpu_balls_to_cpu()`
   - Memory bandwidth limited → benefits limited for small data sizes

3. **Collision Detection (O(n²))**
   - Detects potential collisions between all balls
   - Compute-intensive operation (quadratic complexity)
   - Highest parallelization benefit due to independent work

### Performance Metrics

- **Serial (ms)**: Execution time with 1 thread
- **Parallel (ms)**: Execution time with all available threads
- **Speedup**: `Serial time / Parallel time`
  - `> 1.0` = Parallelization beneficial (speedup achieved)
  - `≈ 1.0` = Overhead ≈ Benefits (no net gain)
  - `< 1.0` = Overhead > Benefits (parallelization slower)

### Interpreting Results

#### Small Ball Counts (< 1000)
Parallelization overhead often exceeds benefits for simple operations:
```
Ball Initialization    : 0.88x speedup (slower with parallelization)
Ball Data Sync        : 0.96x speedup (slightly slower)
Collision Detection   : 1.05x speedup (minimal benefit)
```

**Why?** The overhead of spawning threads, synchronizing, and merging results exceeds the computational benefit for small problems.

#### Medium Ball Counts (1000-5000)
Mixed results depending on operation complexity:
```
Ball Initialization    : 1.10x speedup (beginning to benefit)
Ball Data Sync        : 1.15x speedup (memory bandwidth matters)
Collision Detection   : 2.50x speedup (significant benefit)
```

**Why?** As problem size increases, the ratio of useful work to overhead improves, especially for compute-intensive O(n²) operations.

#### Large Ball Counts (> 5000)
Significant parallelization benefits:
```
Ball Initialization    : 3.50x speedup (good scaling)
Ball Data Sync        : 4.20x speedup (memory parallelization)
Collision Detection   : 8.00x speedup (excellent scaling on 16 cores)
```

**Why?** Large problem sizes have more independent work that threads can process in parallel. Overhead becomes negligible.

## Performance Optimization Guidelines

Based on benchmark results, here are practical guidelines:

### ✅ Good Candidates for Parallelization
1. **Collision detection** (O(n²) or worse)
2. **Large array operations** (ball counts > 5000)
3. **Independent computations** on ball data
4. **Physics simulations** with many particles

### ⚠️ Questionable Candidates
1. **Small data sizes** (< 1000 elements)
2. **Very fast operations** (< 1ms serial time)
3. **Memory-limited operations** (data copying)
4. **Operations with frequent synchronization**

### ❌ Poor Candidates
1. **String operations**
2. **Single array iterations** (< 100 elements)
3. **I/O bound operations**
4. **Serialized state updates**

## Tuning OpenMP Performance

### Thread Count

The optimal thread count depends on your CPU:
- **Hyperthreaded CPUs**: Use `OMP_NUM_THREADS=<physical_cores>`
- **Older CPUs**: Use `OMP_NUM_THREADS=<physical_cores>`
- **Oversubscription**: Setting more threads than cores usually hurts performance

### Scheduling

Different schedules suit different workloads:

```bash
# Static: Works well for evenly distributed work
OMP_SCHEDULE=static ./benchmark_openmp

# Dynamic: Works well for unevenly distributed work (collision detection)
OMP_SCHEDULE=dynamic ./benchmark_openmp

# Guided: Compromise between static and dynamic
OMP_SCHEDULE=guided ./benchmark_openmp
```

For collision detection (O(n²)), **dynamic or guided scheduling** usually performs better because not all thread-pairs have equal work.

## Real-World Application Context

The Open-Lotto 3D visualization uses OpenMP in:

1. **Ball initialization** (`drum_instance_init_balls`)
   - Benefits from parallelization when initializing 100+ balls
   - Typical scenario: 90-128 balls per drum

2. **CPU↔GPU synchronization**
   - Benefits from parallelization when syncing large arrays
   - Typical scenario: data transfer every frame (~60 FPS)

3. **Physics updates** (implicitly through compute shaders)
   - GPU compute shaders already parallelize physics
   - CPU-side parallelization useful for collision pre-checks

## Performance Notes

### System Information

The benchmark displays:
- **Max threads available**: Logical CPUs available for parallelization
- **Number of processors**: Physical or virtual processors
- **OpenMP version**: OpenMP standard version being used

### Variation and Noise

Benchmark results can vary due to:
- System load (other processes)
- CPU frequency scaling (power management)
- Cache effects (data locality)
- Memory access patterns

**Recommendation**: Run benchmarks multiple times or use larger iteration counts for more stable results.

## Example: Full Workflow

```bash
# 1. Build the project
cmake -DBUILD_TESTING=ON ..
make benchmark_openmp

# 2. Run baseline (serial only)
OMP_NUM_THREADS=1 ./benchmark_openmp 5000 20

# 3. Run with parallelization
./benchmark_openmp 5000 20

# 4. Run with large problem size
./benchmark_openmp 20000 10

# 5. Compare and analyze results
# Expected speedup: 2-8x for collision detection on 16 cores
```

## Troubleshooting

### Inconsistent Results

**Problem**: Results vary significantly between runs

**Solutions**:
- Close other applications
- Use `OMP_SCHEDULE=static` for deterministic results
- Increase iteration count for more stable timing
- Run multiple times and average results

### Slower Parallel Execution

**Problem**: Parallel execution is slower than serial

**Solutions**:
- Problem size too small (< 500 elements)
- Increase ball count to see parallelization benefits
- Check system load (`top`, `htop`)
- Verify `OMP_NUM_THREADS` is set correctly

### No Parallelization Happening

**Problem**: Results show no speedup even with large problems

**Solutions**:
- Verify `omp_get_max_threads()` returns > 1
- Check `OMP_NUM_THREADS` environment variable
- Ensure compiler has OpenMP support (`gcc -fopenmp`)
- Rebuild: `make clean && make benchmark_openmp`

## References

- OpenMP Documentation: https://www.openmp.org/
- OpenMP Tutorial: https://computing.llnl.gov/tutorials/openMP/
- Parallel Programming Concepts: https://en.wikipedia.org/wiki/Parallel_computing
