<!--
SPDX-FileCopyrightText: 2025 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# Performance Optimization with OpenMP

## Overview

Open-Lotto uses OpenMP (Open Multi-Processing) to parallelize CPU-bound operations in the 3D visualization and physics simulation. This guide explains the OpenMP implementations, their performance impact, and how to measure improvements.

## Key Benefits

OpenMP provides **2-4x speedup** on modern multi-core systems for compute-intensive operations:
- **Ball initialization**: 3+ cores benefit
- **Data synchronization**: Memory bandwidth utilization
- **Collision detection**: Significant scaling (near-linear on 16+ cores)

## Current OpenMP Usage

### 1. Ball Initialization (`drum_instance_init_balls`)

**Location**: [src/gui_opengl.c](../src/gui_opengl.c#L1015)

**What it does**: Initializes positions and velocities for all balls in a drum

```c
#pragma omp parallel for default(none) shared(drum, spawn_r)
for (int i = 0; i < drum->ball_count; i++) {
    // Random position initialization
    drum->balls[i].x = frand_range(-spawn_r, spawn_r);
    drum->balls[i].y = frand_range(...);
    // ... velocity and rotation setup
}
```

**Performance**: 
- 100 balls: negligible improvement
- 1,000 balls: 1.5x speedup
- 10,000 balls: 4-8x speedup on 16 cores

**When used**: Every time a drum starts a new draw cycle

### 2. Ball Data Synchronization (`sync_cpu_balls_to_gpu` / `sync_gpu_balls_to_cpu`)

**Location**: [src/gui_opengl.c](../src/gui_opengl.c#L455)

**What it does**: Transfers ball position/velocity data between CPU and GPU

```c
#pragma omp parallel for default(none) shared(state, drum)
for (int i = 0; i < drum->ball_count; i++) {
    state->gpu_ball_cache[i].px = drum->balls[i].x;
    state->gpu_ball_cache[i].py = drum->balls[i].y;
    // ... copy velocity data
}
```

**Performance**:
- Bandwidth-limited operation
- Benefits most with large ball counts (> 5,000)
- 1.2-1.5x speedup typical

**When used**: Every frame (60 FPS) during animation

### 3. Physics Simulation (GPU Compute)

**Location**: [src/gui_opengl.c - BALL_COMPUTE_SHADER_SRC](../src/gui_opengl.c#L314)

**What it does**: GPU-based physics with collision detection

Already highly optimized with GPU compute shaders (local_size_x = 64), no additional CPU parallelization needed.

## Performance Measurement

### Benchmark Tool

Run the dedicated OpenMP benchmark to measure parallelization benefits:

```bash
# Build
make benchmark_openmp

# Run with default settings (1000 balls, 100 iterations)
./benchmark_openmp

# Custom size
./benchmark_openmp 5000 50

# View help
./benchmark_openmp --help
```

### Interpreting Results

```
Operation                      | Serial (ms) | Parallel (ms) | Speedup
Ball Initialization            |       1.113 |         0.275 |   4.05x
Ball Data Sync (GPU↔CPU)      |       0.194 |         0.153 |   1.27x
Collision Detection (O(n²))   |     657.256 |       198.442 |   3.31x
```

**Speedup factors**:
- `> 1.0` = Parallelization beneficial
- `≈ 1.0` = Overhead ≈ benefits
- `< 1.0` = Overhead > benefits (use serial for small problems)

## Optimization Guidelines

### ✅ Use OpenMP When:
1. **Loop count > 100** (overhead becomes negligible)
2. **Independent iterations** (no data dependencies)
3. **Compute-bound** (not memory-bound)
4. **Significant workload** per iteration (> 100 operations)

### ❌ Avoid OpenMP When:
1. **Loop count < 50** (overhead dominates)
2. **Serialized dependencies** (each iteration depends on previous)
3. **Memory-limited** (bandwidth already saturated)
4. **Very fast operations** (< 1ms total time)

## Environment Configuration

### Control Thread Count

```bash
# Use specific number of threads
OMP_NUM_THREADS=8 ./open-lotto

# Use all available cores (default)
OMP_NUM_THREADS=0 ./open-lotto
```

### Schedule Tuning

```bash
# Static: Good for uniform workload
OMP_SCHEDULE=static,1 ./benchmark_openmp

# Dynamic: Good for uneven workload
OMP_SCHEDULE=dynamic,10 ./benchmark_openmp

# Guided: Compromise between static and dynamic
OMP_SCHEDULE=guided ./benchmark_openmp
```

## Technical Details

### Compilation Flags

Project uses:
- `-fopenmp` (GCC/Clang)
- `OpenMP::OpenMP_C` (CMake target linking)

### Thread Safety

All OpenMP parallelization uses `default(none)` clause to ensure:
- **Explicit data sharing** (no implicit globals)
- **Type safety** (compiler catches missing declarations)
- **Race condition prevention** (shared vs. private variables clearly marked)

Example:
```c
#pragma omp parallel for default(none) shared(drum, delta_time, drum_radius) \
    reduction(+:settled_count)
```

### Memory Considerations

OpenMP regions use **stack memory** for thread-local data:
- No global state modified
- Thread-safe by design
- No data races possible with proper annotations

## Performance Monitoring

### Check Available Threads

```c
int max_threads = omp_get_max_threads();
int num_procs = omp_get_num_procs();
printf("Threads: %d, Cores: %d\n", max_threads, num_procs);
```

### Profiling

Use system tools to measure parallelization benefits:

```bash
# Linux: perf
perf record -g ./open-lotto --gui
perf report

# Linux: htop (real-time monitoring)
htop
# Press '1' to see per-core usage
```

## Troubleshooting

### Slow Parallel Execution

**Problem**: Parallelization is slower than serial

**Solutions**:
1. Problem size too small → increase ball count
2. System load too high → close other applications
3. Wrong schedule → try `OMP_SCHEDULE=dynamic`
4. Hyperthreading issues → set threads to physical core count

### Inconsistent Results

**Solutions**:
1. Disable frequency scaling (if testing)
2. Run benchmarks multiple times
3. Use `OMP_SCHEDULE=static` for deterministic results
4. Close other applications

### No Parallelization

**Check**:
```bash
echo $OMP_NUM_THREADS
OMP_NUM_THREADS=0 ./benchmark_openmp
```

## References

- [OpenMP Official Documentation](https://www.openmp.org/)
- [OpenMP Best Practices](https://computing.llnl.gov/tutorials/openMP/)
- [Open-Lotto Benchmark Guide](./OPENMP_BENCHMARK.md)
- [Performance Analysis Architecture](./PERFORMANCE.md)

## Related Files

- Implementation: [src/gui_opengl.c](../src/gui_opengl.c)
- Benchmarking: [tests/benchmark_openmp.c](../tests/benchmark_openmp.c)
- Documentation: [docs/OPENMP_BENCHMARK.md](./OPENMP_BENCHMARK.md)
