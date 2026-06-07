/**
 * @file benchmark_openmp.c
 * @brief OpenMP parallelization performance benchmarks
 *
 * Demonstrates performance improvements from OpenMP parallelization
 * in operations like ball initialization and data synchronization.
 *
 * Run with:
 *   ./benchmark_openmp                    # Default: all ball counts
 *   ./benchmark_openmp --help             # Show options
 *   ./benchmark_openmp 1000 4             # 1000 balls, 4 threads
 *
 * Example with OpenMP environment:
 *   OMP_NUM_THREADS=8 ./benchmark_openmp 10000
 */

#include <limits.h>
#include <math.h>
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

/**
 * @brief Get current wall-clock time in microseconds
 * @return Time in microseconds since epoch
 */
static uint64_t get_time_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
}

/**
 * @brief Simulate ball position initialization (serial version)
 *
 * Mimics the drum_instance_init_balls function logic but in isolation
 * for benchmarking. Each ball gets random position and velocity.
 *
 * @param balls - Array of ball data (3 floats per ball: x, y, z)
 * @param ball_count - Number of balls to initialize
 */
static void init_balls_serial(float *balls, int ball_count)
{
    for (int i = 0; i < ball_count; i++)
    {
        int idx = i * 3;
        balls[idx] = (float)rand() / RAND_MAX * 200.0f - 100.0f;     /* x */
        balls[idx + 1] = (float)rand() / RAND_MAX * 200.0f - 100.0f; /* y */
        balls[idx + 2] = (float)rand() / RAND_MAX * 200.0f - 100.0f; /* z */
    }
}

/**
 * @brief Simulate ball position initialization (OpenMP parallel version)
 *
 * Same as serial version but parallelized with OpenMP.
 * Each thread initializes a chunk of balls independently.
 *
 * @param balls - Array of ball data (3 floats per ball: x, y, z)
 * @param ball_count - Number of balls to initialize
 */
static void init_balls_parallel(float *balls, int ball_count)
{
#pragma omp parallel for default(none) shared(balls, ball_count)
    for (int i = 0; i < ball_count; i++)
    {
        int idx = i * 3;
        balls[idx] = (float)rand() / RAND_MAX * 200.0f - 100.0f;     /* x */
        balls[idx + 1] = (float)rand() / RAND_MAX * 200.0f - 100.0f; /* y */
        balls[idx + 2] = (float)rand() / RAND_MAX * 200.0f - 100.0f; /* z */
    }
}

/**
 * @brief Simulate ball data synchronization (serial version)
 *
 * Mimics sync_cpu_balls_to_gpu / sync_gpu_balls_to_cpu operations.
 * Copies velocity data from source to destination array.
 *
 * @param source - Source array (velocity data, 3 floats per ball)
 * @param dest - Destination array (velocity data, 3 floats per ball)
 * @param ball_count - Number of balls
 */
static void sync_balls_serial(const float *source, float *dest, int ball_count)
{
    for (int i = 0; i < ball_count; i++)
    {
        int idx = i * 3;
        dest[idx] = source[idx];
        dest[idx + 1] = source[idx + 1];
        dest[idx + 2] = source[idx + 2];
    }
}

/**
 * @brief Simulate ball data synchronization (OpenMP parallel version)
 *
 * Same as serial version but parallelized with OpenMP.
 *
 * @param source - Source array (velocity data, 3 floats per ball)
 * @param dest - Destination array (velocity data, 3 floats per ball)
 * @param ball_count - Number of balls
 */
static void sync_balls_parallel(const float *source, float *dest, int ball_count)
{
#pragma omp parallel for default(none) shared(source, dest, ball_count)
    for (int i = 0; i < ball_count; i++)
    {
        int idx = i * 3;
        dest[idx] = source[idx];
        dest[idx + 1] = source[idx + 1];
        dest[idx + 2] = source[idx + 2];
    }
}

/**
 * @brief Simulate collision detection (serial version)
 *
 * Simple O(n²) collision check - demonstrates workload that scales
 * well with parallelization.
 *
 * @param balls - Array of ball positions (3 floats per ball)
 * @param ball_count - Number of balls
 * @return Number of potential collisions detected
 */
static int detect_collisions_serial(const float *balls, int ball_count)
{
    int collisions = 0;
    float radius = 1.0f;

    for (int i = 0; i < ball_count; i++)
    {
        for (int j = i + 1; j < ball_count; j++)
        {
            int i_idx = i * 3;
            int j_idx = j * 3;

            float dx = balls[j_idx] - balls[i_idx];
            float dy = balls[j_idx + 1] - balls[i_idx + 1];
            float dz = balls[j_idx + 2] - balls[i_idx + 2];

            float dist_sq = dx * dx + dy * dy + dz * dz;
            if (dist_sq < radius * 2 * radius * 2)
                collisions++;
        }
    }

    return collisions;
}

/**
 * @brief Simulate collision detection (OpenMP parallel version)
 *
 * Same as serial version but parallelized with OpenMP.
 *
 * @param balls - Array of ball positions (3 floats per ball)
 * @param ball_count - Number of balls
 * @return Number of potential collisions detected
 */
static int detect_collisions_parallel(const float *balls, int ball_count)
{
    int collisions = 0;
    float radius = 1.0f;

#pragma omp parallel for default(none) shared(balls, ball_count, radius) reduction(+ : collisions) \
    schedule(dynamic)
    for (int i = 0; i < ball_count; i++)
    {
        for (int j = i + 1; j < ball_count; j++)
        {
            int i_idx = i * 3;
            int j_idx = j * 3;

            float dx = balls[j_idx] - balls[i_idx];
            float dy = balls[j_idx + 1] - balls[i_idx + 1];
            float dz = balls[j_idx + 2] - balls[i_idx + 2];

            float dist_sq = dx * dx + dy * dy + dz * dz;
            if (dist_sq < radius * 2 * radius * 2)
                collisions++;
        }
    }

    return collisions;
}

/**
 * @brief Benchmark a specific operation
 *
 * @param name - Operation name
 * @param serial_fn - Serial function pointer
 * @param parallel_fn - Parallel function pointer
 * @param ball_count - Number of balls
 * @param iterations - Iterations per test
 * @param data_setup - Pointer to setup data (optional)
 * @return Struct with timing results
 */
typedef struct
{
    const char *name;
    double serial_time_ms;
    double parallel_time_ms;
    double speedup;
} BenchmarkResult;

/**
 * @brief Run init_balls benchmark
 */
static BenchmarkResult benchmark_init_balls(int ball_count, int iterations)
{
    BenchmarkResult result = {.name = "Ball Initialization"};

    float *balls = (float *)malloc(ball_count * 3 * sizeof(float));
    if (!balls)
    {
        printf("Error: Failed to allocate ball data\n");
        return result;
    }

    int max_threads = omp_get_max_threads();

    /* Serial benchmark */
    omp_set_num_threads(1);
    uint64_t start = get_time_us();
    for (int iter = 0; iter < iterations; iter++)
    {
        init_balls_serial(balls, ball_count);
    }
    uint64_t end = get_time_us();
    result.serial_time_ms = (end - start) / 1000.0;

    /* Parallel benchmark */
    omp_set_num_threads(max_threads);
    start = get_time_us();
    for (int iter = 0; iter < iterations; iter++)
    {
        init_balls_parallel(balls, ball_count);
    }
    end = get_time_us();
    result.parallel_time_ms = (end - start) / 1000.0;

    result.speedup =
        (result.serial_time_ms > 0.001) ? result.serial_time_ms / result.parallel_time_ms : 1.0;

    free(balls);
    return result;
}

/**
 * @brief Run sync_balls benchmark
 */
static BenchmarkResult benchmark_sync_balls(int ball_count, int iterations)
{
    BenchmarkResult result = {.name = "Ball Data Sync (GPU↔CPU)"};

    float *source = (float *)malloc(ball_count * 3 * sizeof(float));
    float *dest = (float *)malloc(ball_count * 3 * sizeof(float));
    if (!source || !dest)
    {
        printf("Error: Failed to allocate sync data\n");
        free(source);
        free(dest);
        return result;
    }

    /* Initialize source data */
    for (int i = 0; i < ball_count * 3; i++)
        source[i] = (float)rand() / RAND_MAX;

    int max_threads = omp_get_max_threads();

    /* Serial benchmark */
    omp_set_num_threads(1);
    uint64_t start = get_time_us();
    for (int iter = 0; iter < iterations; iter++)
    {
        sync_balls_serial(source, dest, ball_count);
    }
    uint64_t end = get_time_us();
    result.serial_time_ms = (end - start) / 1000.0;

    /* Parallel benchmark */
    omp_set_num_threads(max_threads);
    start = get_time_us();
    for (int iter = 0; iter < iterations; iter++)
    {
        sync_balls_parallel(source, dest, ball_count);
    }
    end = get_time_us();
    result.parallel_time_ms = (end - start) / 1000.0;

    result.speedup =
        (result.serial_time_ms > 0.001) ? result.serial_time_ms / result.parallel_time_ms : 1.0;

    free(source);
    free(dest);
    return result;
}

/**
 * @brief Run collision detection benchmark
 */
static BenchmarkResult benchmark_collision_detection(int ball_count, int iterations)
{
    BenchmarkResult result = {.name = "Collision Detection (O(n²))"};

    float *balls = (float *)malloc(ball_count * 3 * sizeof(float));
    if (!balls)
    {
        printf("Error: Failed to allocate ball data\n");
        return result;
    }

    /* Initialize ball positions */
    for (int i = 0; i < ball_count * 3; i++)
        balls[i] = (float)rand() / RAND_MAX * 100.0f - 50.0f;

    int max_threads = omp_get_max_threads();

    /* Serial benchmark */
    omp_set_num_threads(1);
    uint64_t start = get_time_us();
    volatile int dummy = 0;
    for (int iter = 0; iter < iterations; iter++)
    {
        dummy += detect_collisions_serial(balls, ball_count);
    }
    uint64_t end = get_time_us();
    (void)dummy;
    result.serial_time_ms = (end - start) / 1000.0;

    /* Parallel benchmark */
    omp_set_num_threads(max_threads);
    start = get_time_us();
    dummy = 0;
    for (int iter = 0; iter < iterations; iter++)
    {
        dummy += detect_collisions_parallel(balls, ball_count);
    }
    end = get_time_us();
    (void)dummy;
    result.parallel_time_ms = (end - start) / 1000.0;

    result.speedup =
        (result.serial_time_ms > 0.001) ? result.serial_time_ms / result.parallel_time_ms : 1.0;

    free(balls);
    return result;
}

/**
 * @brief Print benchmark results in formatted table
 */
static void print_results(const BenchmarkResult *results, int count, int ball_count)
{
    printf("\n");
    printf("========================================\n");
    printf("OpenMP Performance Analysis\n");
    printf("========================================\n");
    printf("Configuration:\n");
    printf("  - Balls: %d\n", ball_count);
    printf("  - Max threads: %d\n", omp_get_max_threads());
    printf("  - Processors: %d\n", omp_get_num_procs());
    printf("\n");

    printf("%-35s | Serial (ms) | Parallel (ms) | Speedup\n", "Operation");
    printf("%s-+-----------+---------------+--------\n", "-----------------------------------");

    double total_serial = 0.0;
    double total_parallel = 0.0;

    for (int i = 0; i < count; i++)
    {
        printf("%-35s | %11.3f | %13.3f | %6.2fx\n", results[i].name, results[i].serial_time_ms,
               results[i].parallel_time_ms, results[i].speedup);
        total_serial += results[i].serial_time_ms;
        total_parallel += results[i].parallel_time_ms;
    }

    printf("%s-+-----------+---------------+--------\n", "-----------------------------------");
    double overall_speedup = (total_serial > 0.001) ? total_serial / total_parallel : 1.0;
    printf("%-35s | %11.3f | %13.3f | %6.2fx\n", "TOTAL", total_serial, total_parallel,
           overall_speedup);
    printf("\n");
}

/**
 * @brief Print help message
 */
static void print_help(const char *prog_name)
{
    printf("Usage: %s [OPTIONS] [ball_count] [iterations]\n", prog_name);
    printf("\n");
    printf("Options:\n");
    printf("  --help              Show this help message\n");
    printf("  --parallel          Enable parallel benchmarking (default)\n");
    printf("  --serial            Run serial benchmarks only\n");
    printf("\n");
    printf("Arguments:\n");
    printf("  ball_count          Number of balls to simulate (default: 1000)\n");
    printf("  iterations          Number of iterations per test (default: 100)\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s                       # Default: 1000 balls, 100 iterations\n", prog_name);
    printf("  %s 5000 500              # 5000 balls, 500 iterations\n", prog_name);
    printf("  %s --serial 1000         # Serial only, 1000 balls\n", prog_name);
    printf("\n");
    printf("Environment Variables:\n");
    printf("  OMP_NUM_THREADS     Set number of OpenMP threads\n");
    printf("  OMP_SCHEDULE        Set OpenMP schedule type\n");
    printf("\n");
}

/**
 * @brief Main benchmark entry point
 */
int main(int argc, char *argv[])
{
    int ball_count = 1000;
    int iterations = 100;
    int serial_only = 0;

    /* Parse command-line arguments */
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
            print_help(argv[0]);
            return 0;
        }
        else if (strcmp(argv[i], "--serial") == 0)
        {
            serial_only = 1;
        }
        else if (strcmp(argv[i], "--parallel") == 0)
        {
            serial_only = 0;
        }
        else if (argv[i][0] != '-')
        {
            /* First positional: ball_count, second: iterations */
            if (i == 1 || argv[i - 1][0] == '-')
            {
                char *end;
                long val = strtol(argv[i], &end, 10);
                if (end != argv[i] && *end == '\0' && val > 0 && val <= INT_MAX)
                    ball_count = (int)val;
            }
            else
            {
                char *end;
                long val = strtol(argv[i], &end, 10);
                if (end != argv[i] && *end == '\0' && val > 0 && val <= INT_MAX)
                    iterations = (int)val;
            }
        }
    }

    printf("\n");
    printf("========================================\n");
    printf("Open-Lotto OpenMP Benchmarks\n");
    printf("========================================\n");

    /* Show OpenMP info */
    printf("OpenMP Configuration:\n");
    printf("  Max threads available: %d\n", omp_get_max_threads());
    printf("  Number of processors: %d\n", omp_get_num_procs());
#ifdef _OPENMP
    printf("  OpenMP version: %d\n", _OPENMP);
#endif
    printf("\n");

    if (serial_only)
    {
        printf("Mode: SERIAL ONLY (no parallel comparison)\n");
        printf("To enable parallel benchmarking, run without --serial flag\n");
        return 0;
    }

    printf("Mode: PARALLEL PERFORMANCE ANALYSIS\n");
    printf("Comparing serial execution (1 thread) vs parallel (%d threads)\n\n",
           omp_get_max_threads());

    /* Run benchmarks */
    BenchmarkResult results[3];
    int result_count = 0;

    printf("Running benchmarks... (ball_count=%d, iterations=%d)\n\n", ball_count, iterations);

    printf("  - Ball initialization...\n");
    results[result_count++] = benchmark_init_balls(ball_count, iterations);

    printf("  - Data synchronization (GPU↔CPU)...\n");
    results[result_count++] = benchmark_sync_balls(ball_count, iterations);

    printf("  - Collision detection...\n");
    results[result_count++] = benchmark_collision_detection(ball_count, iterations);

    /* Print results */
    print_results(results, result_count, ball_count);

    /* Print interpretation */
    printf("Interpretation:\n");
    printf("  - Speedup > 1.0 = Parallelization provided performance benefit\n");
    printf("  - Speedup ≈ 1.0 = Parallelization overhead ≈ benefits (overhead dominates)\n");
    printf("  - More balls → greater speedup (reduced overhead impact)\n");
    printf("  - Collision detection benefits most (O(n²) workload)\n");
    printf("  - Data sync has overhead for small sizes (single copy fast)\n");
    printf("\n");

    return 0;
}
