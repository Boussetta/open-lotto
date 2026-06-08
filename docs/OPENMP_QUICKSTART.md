<!--
SPDX-FileCopyrightText: 2025 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# Quick Start: OpenMP Performance Testing

## The 5-Minute Summary

Open-Lotto now includes **OpenMP parallelization** for CPU-intensive operations. Here's how to see the performance benefits:

### Build & Run

```bash
# Build with tests enabled
cmake -DBUILD_TESTING=ON -B build && cd build && make -j8

# Run the benchmark
./benchmark_openmp 5000 50
```

### Expected Output

```
========================================
OpenMP Performance Analysis
========================================
Configuration:
  - Balls: 5000
  - Max threads: 16
  - Processors: 16

Operation                      | Serial (ms) | Parallel (ms) | Speedup
Ball Initialization            |       0.569 |         0.175 |   3.25x
Ball Data Sync (GPU↔CPU)      |       0.135 |         0.117 |   1.15x
Collision Detection (O(n²))   |     327.786 |        99.589 |   3.29x
```

### What This Means

- **Speedup > 1.0** = Parallelization helped ✅
- **3.25x for initialization** = 16 cores running in parallel
- **3.29x for collision detection** = Good scaling on 16 cores
- **1.15x for data sync** = Memory-limited (less parallelization benefit)

## Control Thread Count

```bash
# Run with 8 threads instead of 16
OMP_NUM_THREADS=8 ./benchmark_openmp 5000

# Run serially (for comparison)
OMP_NUM_THREADS=1 ./benchmark_openmp 5000
```

## Run Actual Application

The GUI automatically uses parallelization:

```bash
# Uses all available cores by default
./open-lotto --gui

# Or explicitly set threads
OMP_NUM_THREADS=16 ./open-lotto --gui
```

## Documentation

**Learn more**:
- 📊 **[OPENMP_BENCHMARK.md](./OPENMP_BENCHMARK.md)** - Detailed benchmark guide
- ⚙️ **[OPENMP_OPTIMIZATION.md](./OPENMP_OPTIMIZATION.md)** - How OpenMP is used
- 🏗️ **[PERFORMANCE_ARCHITECTURE.md](./PERFORMANCE_ARCHITECTURE.md)** - System design

## Key Takeaways

1. ✅ OpenMP provides **2-4x speedup** for compute-intensive operations
2. ✅ **Ball initialization** benefits most (O(n) parallelizable)
3. ✅ **Collision detection** scales well (O(n²) workload)
4. ⚠️ **Small problem sizes** have overhead (< 500 elements)
5. ⚠️ **Data sync** is memory-limited (modest benefits)

## When Does It Help?

| Scenario | Benefit | Why |
|----------|---------|-----|
| Large ball counts (> 5000) | 3-4x | Low overhead ratio |
| Collision detection | 2-4x | Compute-bound O(n²) |
| Ball initialization | 2-4x | Independent iterations |
| Data synchronization | 1.2x | Memory bandwidth limited |
| Small ball counts (< 500) | None | Overhead dominates |

## Troubleshooting

**Problem**: "Parallelization slower than serial?"
- **Solution**: Increase ball count to see real benefits
- **Reason**: Overhead of threading is significant for small problems

**Problem**: No speedup even with large counts?
- **Check**: `OMP_NUM_THREADS` environment variable set?
- **Check**: System load too high? (`top` command)
- **Check**: Using correct benchmark? (`./benchmark_openmp`, not `./benchmark`)

## Technical Details

- **Location**: [src/gui_opengl.c](../src/gui_opengl.c)
- **Build flag**: `-fopenmp` (GCC/Clang)
- **CMake**: `OpenMP::OpenMP_C` target
- **Compiler**: GCC 11+, Clang 13+

---

**Need more details?** See the full documentation in the links above!
