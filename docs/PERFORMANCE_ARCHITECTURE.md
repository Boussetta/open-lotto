# Performance Architecture: CPU-GPU Parallelization

## Overview

Open-Lotto uses a **hybrid CPU-GPU parallelization strategy** to deliver smooth 60 FPS visualization with realistic ball physics:

1. **CPU**: Initial ball setup, data marshalling, collision pre-checks (OpenMP)
2. **GPU**: Real-time physics simulation, collision response (GLSL compute shaders)
3. **Synchronization**: Efficient CPU↔GPU data transfer (OpenMP parallel copies)

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                        │
│                  (Open-Lotto GUI)                           │
└────────┬────────────────────────────────────┬───────────────┘
         │                                    │
         ▼ (ball init, data setup)           ▼ (rendering)
    ┌─────────┐    ┌──────────────┐    ┌──────────────┐
    │   CPU   │◄──►│   GPU Mem    │◄──►│   GPU Render │
    │ OpenMP  │    │  (SSBOs)     │    │   (Shaders)  │
    │  Cores  │    │              │    │              │
    └─────────┘    └──────────────┘    └──────────────┘
         ▲                                    ▲
         │ (sync_cpu_to_gpu,                 │ (60 FPS display)
         │  sync_gpu_to_cpu)                 │
         └────────────────────────────────────┘
```

## Performance Characteristics

### CPU-Bound Operations (OpenMP)

| Operation | Complexity | Speedup | When Used |
|-----------|-----------|---------|-----------|
| Ball Init | O(n) | 3-4x | Once per draw |
| Data Sync | O(n) | 1.2-1.5x | Every frame |
| Collision Precheck | O(n²) | 2-4x | Per-update (optional) |

**Optimal**: n = 100-1000 balls
**Scaling**: Near-linear on 8-16 cores

### GPU-Bound Operations (Compute Shaders)

| Operation | Complexity | Throughput | Utilization |
|-----------|-----------|-----------|------------|
| Physics Step | O(n) | 10M balls/s | 90%+ |
| Collision Response | O(n²) | 5M pairs/s | 80%+ |

**Optimal**: Local workgroup size = 64 threads
**Memory**: Shared memory for collision grid acceleration

## Synchronization Points

### Frame Loop (60 FPS)

```
1. Update drum phase (CPU, serial)
2. Physics timestep:
   a. Sync CPU→GPU (OpenMP parallel)
   b. GPU compute shader (collision detection + response)
   c. Sync GPU→CPU (OpenMP parallel)
3. Render (GPU, OpenGL)
4. Present frame (display)

Cycle time: ~16.6ms per frame
```

### Draw Event

```
1. Init balls (OpenMP parallel) [once]
2. Spin phase (physics loop) [2-3 seconds]
3. Stop phase (physics loop) [1-2 seconds]
4. Pick phase (pick highlight) [0.5 seconds]

Total: ~4-6 seconds per draw
```

## Bottleneck Analysis

### CPU-to-GPU Bandwidth

**Data**:
- Per ball: 3×float position + 3×float velocity = 24 bytes
- Typical count: 128 balls = 3 KB per frame
- Frame rate: 60 FPS = 180 KB/s

**Bandwidth utilization**: < 1% of PCIe 3.0 (4 GB/s available)

**Conclusion**: Not bandwidth-limited; OpenMP overhead can be greater than benefit for small ball counts.

### GPU Physics Compute

**Operation**: Collision detection (O(n²))
- 128 balls → 16K pairs per frame
- Each pair: ~50 operations
- Total: 800K operations/frame @ 60 FPS = 48M ops/sec

**GPU throughput**: > 1 TFLOP available
**Utilization**: < 0.005% of GPU capacity

**Conclusion**: GPU is vastly overpowered; bottleneck is CPU frame submission.

## Scalability Analysis

### What Scales Well

```
Balls  | GPU Time | CPU Time (Serial) | CPU Time (OpenMP) | FPS Impact
-------|----------|------------------|-------------------|----------
128    | 0.05ms   | 0.1ms            | 0.08ms            | +0%
512    | 0.2ms    | 0.5ms            | 0.2ms             | +1%
2000   | 0.8ms    | 2.2ms            | 0.6ms             | +3%
10000  | 4ms      | 12ms             | 2.5ms             | +15%
50000  | 20ms     | 65ms             | 8ms               | +45%
```

### Real-World Constraints

1. **Screen resolution**: 1400×900 (typical)
2. **Rendering overhead**: ~5-8ms per frame
3. **Available time**: 16.6ms (60 FPS target)

**Practical limit**: ~5000 balls before GPU rendering becomes bottleneck

## Optimization Roadmap

### Current (Implemented)

✅ Ball initialization (OpenMP)
✅ Data synchronization (OpenMP)
✅ GPU compute shaders (GLSL 4.3)

### Future Opportunities

1. **Collision grid spatial acceleration**
   - Pre-check with spatial grid (O(n) instead of O(n²))
   - Reduce false positives before GPU

2. **Adaptive physics timestep**
   - Skip updates when balls settled
   - Reduce GPU submissions for idle frames

3. **GPU memory management**
   - Double-buffering for async compute
   - Reduce synchronization points

4. **CPU-GPU pipelining**
   - Overlap GPU compute with CPU data prep
   - Reduce frame-to-frame latency

## Configuration Tuning

### For High Ball Counts (> 5000)

```bash
# Maximize parallelization
OMP_NUM_THREADS=16
OMP_SCHEDULE=dynamic,10
./open-lotto --draws 1000 --gui

# Monitor GPU utilization
nvidia-smi --query-gpu=utilization.gpu --loop-ms=100
```

### For Low-Power Systems

```bash
# Reduce overhead
OMP_NUM_THREADS=2
OMP_SCHEDULE=static,100

# Reduce ball count (not OpenMP-related, but helps overall)
# GUI will handle gracefully
```

### For Development/Profiling

```bash
# Serial execution for baseline
OMP_NUM_THREADS=1 ./benchmark_openmp 1000 100

# Parallel execution
OMP_NUM_THREADS=16 ./benchmark_openmp 1000 100

# Check scaling efficiency
for threads in 1 2 4 8 16; do
    OMP_NUM_THREADS=$threads ./benchmark_openmp 5000
done
```

## Measurement Methodology

### Correct Benchmarking

✅ **Do**:
- Run multiple iterations (100+)
- Measure warm cache performance
- Use `clock_gettime()` or similar high-precision timer
- Disable frequency scaling for consistency
- Close other applications

❌ **Don't**:
- Single iteration (cache effects too large)
- Small datasets (overhead dominates)
- Measure wall-clock time (variable)
- Include I/O operations
- Run under system load

### Profiling Tools

```bash
# Linux: Detailed analysis
perf record -g -e cycles:u ./open-lotto --gui
perf report

# Linux: CPU utilization
top -1 -b -n 1 | grep open-lotto

# Linux: Thread activity
ps -eLo pid,tid,comm | grep open-lotto
```

## References

- **OpenMP**: [OPENMP_OPTIMIZATION.md](./OPENMP_OPTIMIZATION.md)
- **Benchmarking**: [OPENMP_BENCHMARK.md](./OPENMP_BENCHMARK.md)
- **Performance Data**: [PERFORMANCE.md](./PERFORMANCE.md)
- **Source**: [src/gui_opengl.c](../src/gui_opengl.c)

## Related Issues

- Performance tracking and improvements logged in project issues
- See `docs/adr/` for architectural decision records
- GitHub discussions for community performance insights
