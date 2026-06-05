/**
 * @file benchmark.c
 * @brief Performance benchmarking suite for Open-Lotto
 *
 * Measures:
 *   - RNG throughput (draws per second)
 *   - Draw generation speed (draws per second for each lottery)
 *   - Memory overhead per draw
 *   - Seed generation performance
 *
 * Run with:
 *   ./benchmark [iterations]
 *
 * Examples:
 *   ./benchmark              # Run with default 100,000 iterations
 *   ./benchmark 1000000      # Run with 1 million iterations
 */

#include "../include/combogen.h"
#include "../include/random_seed.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>

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
 * @brief Get current process memory usage in kilobytes
 * @return Memory usage in KB
 */
static long get_memory_kb(void)
{
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss;
}

/**
 * @brief Silent callback for benchmarking (no overhead)
 */
static void benchmark_callback(DrawEvent event, const LotteryResult *result)
{
    (void)event;
    (void)result;
}

/**
 * @brief Benchmark RNG throughput
 *
 * Measures how many random numbers can be generated per second.
 * This tests the raw performance of the PCG32 algorithm.
 *
 * @param iterations - Number of random numbers to generate
 * @return Random numbers per second
 */
static double benchmark_rng(int iterations)
{
    printf("Benchmarking RNG throughput...\n");
    printf("  Generating %d random numbers\n", iterations);

    uint64_t start = get_time_us();

    /* Simple RNG throughput test - call generate_draw once to init RNG,
     * then measure pure generation */
    LotteryResult dummy;
    for (int i = 0; i < iterations; i++)
    {
        generate_draw(5, 1, 50, 0, 0, 0, &dummy, benchmark_callback);
    }

    uint64_t end = get_time_us();
    double elapsed_sec = (end - start) / 1000000.0;

    double draws_per_sec = iterations / elapsed_sec;
    return draws_per_sec;
}

/**
 * @brief Benchmark Eurojackpot draw generation
 *
 * Measures how many complete draws can be generated per second.
 * Eurojackpot: 5 main + 2 extra numbers.
 *
 * @param iterations - Number of draws to generate
 * @return Draws per second
 */
static double benchmark_eurojackpot(int iterations)
{
    printf("\nBenchmarking Eurojackpot draw generation...\n");
    printf("  Generating %d draws (5 main + 2 extra)\n", iterations);

    uint64_t start = get_time_us();

    for (int i = 0; i < iterations; i++)
    {
        LotteryResult result;
        generate_draw(5, 1, 50, 2, 1, 12, &result, benchmark_callback);
    }

    uint64_t end = get_time_us();
    double elapsed_sec = (end - start) / 1000000.0;

    double draws_per_sec = iterations / elapsed_sec;
    return draws_per_sec;
}

/**
 * @brief Benchmark Lotto 6aus49 draw generation
 *
 * Measures how many complete draws can be generated per second.
 * Lotto 6aus49: 6 main + 1 extra (Superzahl) number.
 *
 * @param iterations - Number of draws to generate
 * @return Draws per second
 */
static double benchmark_lotto(int iterations)
{
    printf("\nBenchmarking Lotto 6aus49 draw generation...\n");
    printf("  Generating %d draws (6 main + 1 extra)\n", iterations);

    uint64_t start = get_time_us();

    for (int i = 0; i < iterations; i++)
    {
        LotteryResult result;
        generate_draw(6, 1, 49, 1, 0, 9, &result, benchmark_callback);
    }

    uint64_t end = get_time_us();
    double elapsed_sec = (end - start) / 1000000.0;

    double draws_per_sec = iterations / elapsed_sec;
    return draws_per_sec;
}

/**
 * @brief Benchmark seed generation performance
 *
 * Measures how many seeds can be generated per second.
 * Seeds use multiple entropy sources and should be relatively fast.
 *
 * @param iterations - Number of seeds to generate
 * @return Seeds per second
 */
static double benchmark_seed_generation(int iterations)
{
    printf("\nBenchmarking seed generation...\n");
    printf("  Generating %d seeds\n", iterations);

    uint64_t start = get_time_us();

    for (int i = 0; i < iterations; i++)
    {
        volatile uint64_t seed = generate_strong_seed();
        (void)seed;
    }

    uint64_t end = get_time_us();
    double elapsed_sec = (end - start) / 1000000.0;

    double seeds_per_sec = iterations / elapsed_sec;
    return seeds_per_sec;
}

/**
 * @brief Benchmark memory overhead
 *
 * Measures peak memory usage and memory per draw.
 *
 * @param iterations - Number of draws to generate
 */
static void benchmark_memory(int iterations)
{
    printf("\nBenchmarking memory usage...\n");

    long mem_before = get_memory_kb();

    for (int i = 0; i < iterations; i++)
    {
        LotteryResult result;
        generate_draw(5, 1, 50, 2, 1, 12, &result, benchmark_callback);
    }

    long mem_after = get_memory_kb();
    long mem_used = mem_after - mem_before;

    printf("  Peak memory (before): %ld KB\n", mem_before);
    printf("  Peak memory (after):  %ld KB\n", mem_after);
    printf("  Memory increase: %ld KB\n", mem_used);

    if (iterations > 0)
    {
        double mem_per_draw = (mem_used * 1024.0) / iterations;
        printf("  Memory per draw: %.2f bytes\n", mem_per_draw);
    }
}

/**
 * @brief Print formatted benchmark result
 *
 * @param name - Benchmark name
 * @param value - Measured value
 * @param unit - Unit (e.g., "draws/sec", "seeds/sec")
 */
static void print_result(const char *name, double value, const char *unit)
{
    printf("  %-30s: %12.0f %s\n", name, value, unit);
}

/**
 * @brief Main benchmark entry point
 *
 * @param argc - Argument count
 * @param argv - Arguments: [iterations] [verbose]
 * @return 0 on success
 */
int main(int argc, const char *argv[])
{
    int iterations = 100000;

    /* Parse command-line arguments */
    if (argc > 1)
    {
        char *end;
        long val = strtol(argv[1], &end, 10);
        if (end != argv[1] && *end == '\0' && val > 0 && val <= INT_MAX)
            iterations = (int)val;
    }

    printf("========================================\n");
    printf("Open-Lotto Performance Benchmarks\n");
    printf("========================================\n");
    printf("Iterations: %d\n\n", iterations);

    /* Run benchmarks */
    double rng_rate = benchmark_rng(iterations / 10);
    double eurojackpot_rate = benchmark_eurojackpot(iterations);
    double lotto_rate = benchmark_lotto(iterations);
    double seed_rate = benchmark_seed_generation(iterations / 10);
    benchmark_memory(iterations);

    /* Print summary */
    printf("\n========================================\n");
    printf("Summary\n");
    printf("========================================\n");
    print_result("RNG draws/sec", rng_rate, "draws/sec");
    print_result("Eurojackpot draws/sec", eurojackpot_rate, "draws/sec");
    print_result("Lotto 6aus49 draws/sec", lotto_rate, "draws/sec");
    print_result("Seed generation/sec", seed_rate, "seeds/sec");
    printf("\n");

    /* Additional analysis */
    printf("Estimated performance:\n");
    printf("  %-30s: %.2f ms\n", "Time per RNG draw", 1000.0 / rng_rate);
    printf("  %-30s: %.2f ms\n", "Time per Eurojackpot draw", 1000.0 / eurojackpot_rate);
    printf("  %-30s: %.2f ms\n", "Time per Lotto draw", 1000.0 / lotto_rate);
    printf("  %-30s: %.2f ms\n", "Time per seed", 1000.0 / seed_rate);

    printf("\n========================================\n");

    return 0;
}
