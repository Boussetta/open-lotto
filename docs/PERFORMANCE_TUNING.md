# Performance Tuning Guide

## Baseline Performance

Current benchmark results on a typical x86_64 system:

- **Lotto (6/49)**: ~241k draws/sec (headless)
- **Eurojackpot (5/50 + 2/12)**: ~195k draws/sec (headless)
- **Memory footprint**: < 10 MB resident
- **Startup time**: < 100 ms

Benchmarks are measured in `tests/benchmark.c` and `tests/benchmark_openmp.c`.

## Profiling Workflow

### 1. Quick Profiling with `perf`

Identify which functions consume CPU time:

```bash
# Build with frame pointers enabled
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS="-fno-omit-frame-pointer"
cmake --build build

# Run perf on headless draws
perf record -F 99 ./build/open-lotto --game lotto --draws 100000 --validate-only

# Report
perf report
```

**Interpreting Output:**
- Top rows = hot spots (CPU-bound functions)
- Scroll through or search for function names
- Use arrow keys to expand call trees

### 2. Memory Profiling with Valgrind

Profile heap allocation and detect leaks:

```bash
# Build with debug symbols
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run with Valgrind's massif tool (heap profiler)
valgrind --tool=massif \
  ./build/open-lotto --game eurojackpot --draws 10000 --validate-only

# Generate graph
ms_print massif.out.12345 > heap_profile.txt
```

**Interpreting Output:**
- Shows peak memory usage and allocation timeline
- Useful for detecting runaway allocations
- Check for steady-state vs. growth patterns

### 3. Cache Efficiency with `perf stat`

Measure CPU cache misses and branch mispredictions:

```bash
perf stat -e cache-references,cache-misses,instructions,cycles \
  ./build/open-lotto --game lotto --draws 100000 --validate-only
```

**Key Metrics:**
- `cache-misses / cache-references` — Cache miss rate (lower is better)
- `cycles / instructions` — IPC (Instruction Per Cycle; higher is better)
- Memory bound if miss rate > 10%

### 4. Full Valgrind Callgrind Analysis

Detailed call graph with instruction counts:

```bash
valgrind --tool=callgrind \
  ./build/open-lotto --game lotto --draws 10000 --validate-only

kcachegrind callgrind.out.12345  # GUI (if available)
```

## Bottleneck Identification

### PCG32 Random Number Generation

Current implementation in `src/random.c`:

```c
uint32_t pcg32_random(struct PCG32* state) {
    uint64_t oldstate = state->state;
    state->state = oldstate * 6364136223846793005ULL + state->inc;
    uint32_t xorshifted = ((oldstate >> 18u) ^ oldstate) >> 27u;
    unsigned rot = oldstate >> 59u;
    return (xorshifted >> rot) | (xorshifted << (32u - rot));
}
```

**Performance Characteristics:**
- ~2-3 CPU cycles per sample (latency-bound)
- Excellent cache locality (single 64-bit state)
- Negligible memory allocation

**If this is the bottleneck:**
- Consider parallelizing draws with OpenMP (see `benchmark_openmp.c`)
- Each thread gets its own PCG32 state
- Use `pcg32_seed_unique()` to create non-overlapping streams

### Entropy Seeding

Function: `generate_hybrid_seed()` in `src/random_seed.c`

**Cost Breakdown:**
- `getrandom()` call: ~10–100 µs (system dependent, can block)
- `__rdrand64_step()` (if available): ~20–100 cycles
- Clock jitter read: ~5 cycles

**If this bottlenecks:**
- It only runs once per draw (not per random number)
- Cache entropy sources in headless mode (not practical for interactive)
- Use `--validate-only` to skip seed generation during configuration validation

### Combogen (Number Sampling)

Function: `combogen_draw()` in `src/combogen.c`

**Algorithm:**
- Calls `pcg32_random()` × (main_count + extra_count) times
- Fisher-Yates partial shuffle: O(main_count)
- No malloc/free (fixed-size arrays)

**Performance Characteristics:**
- Linear in draw size (6–7 random numbers for typical games)
- Cache-friendly (small arrays)
- Negligible compared to graphics

**If this bottlenecks:**
- Unlikely in practice; profiling confirms this is sub-1% of runtime
- Graphics rendering (GUI) dominates on interactive mode

### GUI Rendering

#### SDL2 2D Mode

Hot spots:
- Ball physics update loop (per-frame)
- Texture blitting for each ball (dependent on screen size)
- Event handling and frame synchronization

**Optimization Tips:**
- Disable v-sync if latency-sensitive: `SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0")`
- Use hardware acceleration (default in SDL2)
- Profile with `SDL_GetPerformanceCounter()` to measure frame time

#### OpenGL 3D Mode

Hot spots:
- Physics simulation loop (`update_drum_instance()`)
- Collision detection (O(balls²) in worst case)
- Matrix transformations and vertex submission

**Optimization Tips:**
- Already parallelized with OpenMP (see `OPENMP_QUICKSTART.md`)
- Use higher FPS if CPU allows (60+ Hz) for smoother animation
- Reduce ball count if CPU-bound (edit `MAX_BALLS` in header)

**Current OpenMP Parallelization:**
- 8 parallelized loops in `update_drum_instance()` (physics step)
- Reduction clauses for aggregations (settled_count, max_speed)
- All tests pass with sanitizers; no data races

## Memory Optimization

### Fixed-Size Arrays

All critical data structures use fixed-size arrays to avoid malloc overhead:

```c
#define MAX_MAIN_NUMBERS 7
#define MAX_EXTRA_NUMBERS 3

struct Draw {
    int main[MAX_MAIN_NUMBERS];
    int extra[MAX_EXTRA_NUMBERS];
};
```

**Benefits:**
- Stack allocation (fast)
- Predictable memory layout
- Cache-friendly

### Plugin Isolation

Each game plugin is loaded once into memory. No reallocation per draw.

**Memory Layout:**
- Main executable: ~500 KB
- Each plugin (.so): ~50–100 KB
- Runtime state: < 10 MB

## Cache-Friendly Code Patterns

### 1. Spatial Locality

Keep frequently accessed data close:

```c
// Good: struct packed for cache line
struct DrumBall {
    float x, y, z;      // Position (12 bytes)
    float vx, vy, vz;   // Velocity (12 bytes)
    int number;         // Number (4 bytes)
};
// Total: 28 bytes, fits in one cache line with room to spare

// Bad: scattered pointers
struct Ball { Ball* next; /* scattered allocations */ };
```

### 2. Sequential Access Patterns

Iterate in predictable order to enable prefetching:

```c
// Good: sequential, prefetch-friendly
for (int i = 0; i < num_balls; i++) {
    apply_gravity(&balls[i]);
}

// Avoid: random access patterns
for (int i = 0; i < num_balls; i++) {
    update_ball(random_ball());  // Cache miss per iteration
}
```

### 3. Loop Tiling for Large Arrays

If processing huge arrays, process in blocks to fit L1/L2 cache:

```c
#define TILE_SIZE 64
for (int i = 0; i < N; i += TILE_SIZE) {
    for (int j = i; j < min(i + TILE_SIZE, N); j++) {
        process(array[j]);
    }
}
```

## Compiler Optimization Flags

Current build uses `-O3 -march=native`:

```cmake
set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -fno-omit-frame-pointer")
```

### Tuning Options

**For speed:**
```bash
cmake -DCMAKE_C_FLAGS="-O3 -march=native -flto -ffast-math"
```

**For profiling (preserve frame pointers):**
```bash
cmake -DCMAKE_C_FLAGS="-O2 -fno-omit-frame-pointer -fno-optimize-simd-relocatable"
```

**For debugging (disable optimizations):**
```bash
cmake -DCMAKE_BUILD_TYPE=Debug
```

## Parallel Execution

### OpenMP Usage

See `OPENMP_QUICKSTART.md` for enabling:

```bash
cmake -DENABLE_OPENMP=ON
```

Parallelized loops in `src/gui_opengl.c`:
1. Gravity application (FALLING phase)
2. Boundary checking
3. Velocity normalization
4. Damping
5. Rotation reset (STOPPING phase)
6. Animation (PICK_PAUSE)

**Expected Speedup:**
- 2–3× on quad-core, 4–8× on 8-core (Amdahl's law with serial portions)

### Benchmark Interpretation

Run `./build/benchmark_openmp` for side-by-side comparison:

```
lotto_seq: 241,012 draws/sec
lotto_omp: 504,156 draws/sec
```

**Gain: 209%** (2× speedup typical for small core count)

## Build-Time vs. Runtime

### Link-Time Optimization (LTO)

Enable LTO for smaller binaries and better optimization:

```bash
cmake -DCMAKE_C_FLAGS="-O3 -march=native -flto"
```

**Tradeoff:** Longer compile time, slightly faster runtime and smaller binary.

### Profile-Guided Optimization (PGO)

For maximum performance (advanced):

```bash
# Collect profiling data
cmake -DCMAKE_C_FLAGS="-O3 -fprofile-generate"
cmake --build build
./build/open-lotto --game lotto --draws 100000 --validate-only

# Recompile with profile feedback
cmake -DCMAKE_C_FLAGS="-O3 -fprofile-use -fprofile-correction"
cmake --build build
```

## Monitoring in Production

### Runtime Logging

Enable INFO/DEBUG logging to monitor behavior:

```bash
./open-lotto --game lotto --draws 100 --log debug
```

**Watch for:**
- Seed generation times in logs
- Plugin loading delays
- GUI frame rate drops

### System Monitoring

During interactive use:

```bash
# Terminal 1: Run app
./open-lotto --game lotto --gui 3d

# Terminal 2: Monitor
top -p $PID            # CPU, memory
iotop -p $PID          # Disk I/O (usually minimal)
```

## Common Issues and Solutions

| Issue | Symptom | Solution |
|-------|---------|----------|
| High CPU, few draws | Single-core bottleneck | Enable OpenMP, use `--gui 2d` |
| High memory usage | > 100 MB | Check for plugin leaks, reduce ball count |
| Frame rate drops | Stuttering animation | Reduce max_speed or collision checks |
| Slow plugin loading | Delay on startup | Profile with `perf record`, check dlopen() |
| Cache misses | `perf stat` shows >10% miss rate | Check data layout, enable LTO |

## Benchmarking Checklist

Before and after optimization:

- [ ] Run `./build/benchmark` and record throughput
- [ ] Run `perf stat` for cache metrics
- [ ] Run `valgrind --tool=massif` for memory profile
- [ ] Run interactive GUI and subjectively assess frame rate
- [ ] Commit changes with performance impact documented

For CI: See `.github/workflows/ci.yml` performance job, which runs regression detection against historical baseline.
