/**
 * @file benchmark.c
 * @brief Performance benchmarking suite for Open-Lotto
 *
 * Measures:
 *   - RNG throughput (draws per second)
 *   - Draw generation speed (draws per second for each lottery)
 *   - Per-operation timing breakdown (seed init, RNG draw, export)
 *   - Peak memory usage and memory per draw (via getrusage)
 *   - CPU user/system time (via getrusage)
 *   - CPU cycles per operation (via RDTSC where available)
 *   - Memory allocation pattern: baseline, working-set, and peak
 *   - Memory regression detection against a stored baseline
 *
 * Run with:
 *   ./benchmark [iterations]
 *
 * Examples:
 *   ./benchmark              # Run with default 100,000 iterations
 *   ./benchmark 1000000      # Run with 1 million iterations
 *
 * External profiling (recommended):
 *   valgrind --tool=massif --pages-as-heap=yes ./benchmark
 *   perf stat -e cycles,instructions,cache-misses,cache-references ./benchmark
 */

#include "../include/combogen.h"
#include "../include/export.h"
#include "../include/random_seed.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>

/* -------------------------------------------------------------------------
 * Platform helpers
 * ---------------------------------------------------------------------- */

/**
 * @brief Get current wall-clock time in microseconds.
 */
static uint64_t get_time_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

/**
 * @brief Snapshot of getrusage-based resource counters.
 */
typedef struct
{
    long peak_rss_kb; /* ru_maxrss  – peak resident set size (KB) */
    double utime_sec; /* ru_utime   – user CPU time               */
    double stime_sec; /* ru_stime   – system CPU time             */
} ResourceSnapshot;

static ResourceSnapshot take_resource_snapshot(void)
{
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    ResourceSnapshot s;
    s.peak_rss_kb = ru.ru_maxrss;
    s.utime_sec = (double)ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6;
    s.stime_sec = (double)ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;
    return s;
}

/**
 * @brief Read the CPU time-stamp counter (RDTSC).
 *
 * Returns 0 on non-x86 platforms so callers can detect unavailability.
 */
static uint64_t rdtsc(void)
{
#if defined(__x86_64__) || defined(__i386__)
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
#else
    return 0ULL;
#endif
}

/* -------------------------------------------------------------------------
 * Benchmark helpers
 * ---------------------------------------------------------------------- */

/** Silent draw-event callback – no I/O overhead during timing loops. */
static void benchmark_callback(DrawEvent event, const LotteryResult *result)
{
    (void)event;
    (void)result;
}

/**
 * @brief Result bundle for a single timed operation.
 */
typedef struct
{
    double wall_sec;   /* Elapsed wall-clock time in seconds     */
    double cpu_sec;    /* User+system CPU time consumed          */
    uint64_t cycles;   /* RDTSC delta (0 if unavailable)         */
    long delta_rss_kb; /* Change in peak RSS (always 0 or > 0)  */
} TimedResult;

/**
 * @brief Run a benchmark loop and return timing + resource stats.
 *
 * @param fn          Pointer to loop body – receives iteration index.
 * @param ctx         Opaque context forwarded to fn.
 * @param iterations  Number of iterations.
 */
typedef void (*bench_fn)(int idx, void *ctx);

static TimedResult run_timed(bench_fn fn, void *ctx, int iterations)
{
    ResourceSnapshot before = take_resource_snapshot();
    uint64_t tsc0 = rdtsc();
    uint64_t wall0 = get_time_us();

    for (int i = 0; i < iterations; i++)
        fn(i, ctx);

    uint64_t wall1 = get_time_us();
    uint64_t tsc1 = rdtsc();
    ResourceSnapshot after = take_resource_snapshot();

    TimedResult r;
    r.wall_sec = (wall1 - wall0) / 1e6;
    r.cpu_sec = (after.utime_sec + after.stime_sec) - (before.utime_sec + before.stime_sec);
    r.cycles = tsc1 - tsc0; /* unsigned subtraction; 0 on non-x86 (rdtsc returns 0) */
    r.delta_rss_kb = after.peak_rss_kb - before.peak_rss_kb;
    return r;
}

/* -------------------------------------------------------------------------
 * Per-operation loop bodies
 * ---------------------------------------------------------------------- */

static void loop_rng(int idx, void *ctx)
{
    (void)idx;
    LotteryResult dummy;
    generate_draw(5, 1, 50, 0, 0, 0, &dummy, benchmark_callback);
    (void)ctx;
}

static void loop_eurojackpot(int idx, void *ctx)
{
    (void)idx;
    LotteryResult result;
    generate_draw(5, 1, 50, 2, 1, 12, &result, benchmark_callback);
    (void)ctx;
}

static void loop_lotto(int idx, void *ctx)
{
    (void)idx;
    LotteryResult result;
    generate_draw(6, 1, 49, 1, 0, 9, &result, benchmark_callback);
    (void)ctx;
}

static void loop_seed(int idx, void *ctx)
{
    (void)idx;
    volatile uint64_t seed = generate_strong_seed();
    (void)seed;
    (void)ctx;
}

/* Export benchmark context */
typedef struct
{
    LotteryResult *results;
    int count;
    const char *tmpfile;
} ExportCtx;

static void loop_export_csv(int idx, void *ctx)
{
    (void)idx;
    ExportCtx *e = (ExportCtx *)ctx;
    export_results_csv_file(e->tmpfile, "Benchmark", e->results, e->count);
}

static void loop_export_json(int idx, void *ctx)
{
    (void)idx;
    ExportCtx *e = (ExportCtx *)ctx;
    export_results_json_file(e->tmpfile, "Benchmark", e->results, e->count);
}

/* -------------------------------------------------------------------------
 * Output helpers
 * ---------------------------------------------------------------------- */

static void print_separator(void)
{
    printf("%-52s\n", "----------------------------------------------------");
}

static void print_header(const char *title)
{
    printf("\n%-52s\n", title);
    print_separator();
}

static void print_metric(const char *name, double value, const char *unit)
{
    printf("  %-36s: %12.2f %s\n", name, value, unit);
}

static void print_metric_long(const char *name, long value, const char *unit)
{
    printf("  %-36s: %12ld %s\n", name, value, unit);
}

static void print_timed_result(const char *label, const TimedResult *r, int iterations)
{
    double rate = (r->wall_sec > 0.0) ? iterations / r->wall_sec : 0.0;
    double us_per_op = (r->wall_sec > 0.0) ? r->wall_sec * 1e6 / iterations : 0.0;

    printf("\n  [%s]\n", label);
    print_metric("  Throughput", rate, "ops/sec");
    print_metric("  Wall time / op", us_per_op, "µs");
    print_metric("  Wall time total", r->wall_sec, "s");
    print_metric("  CPU time total", r->cpu_sec, "s");
    if (r->cycles > 0ULL)
    {
        double cycles_per_op = (double)r->cycles / iterations;
        print_metric("  CPU cycles / op", cycles_per_op, "cycles");
    }
    if (r->delta_rss_kb != 0)
        print_metric_long("  Peak RSS increase", r->delta_rss_kb, "KB");
}

/* -------------------------------------------------------------------------
 * Memory profiling section
 * ---------------------------------------------------------------------- */

/**
 * @brief Print a detailed memory snapshot at three stages:
 *   1. Baseline  – before any benchmark work
 *   2. Post-draw – after all draw iterations
 *   3. Post-seed – after seed generation
 *
 * Also reports memory-per-draw estimation and emits a Valgrind/Massif
 * usage hint if peak RSS is above a configurable threshold.
 */
static void benchmark_memory_profile(int iterations)
{
    print_header("Memory Profiling");

    ResourceSnapshot baseline = take_resource_snapshot();
    printf("  Baseline peak RSS : %ld KB\n", baseline.peak_rss_kb);

    /* --- draw working-set --- */
    for (int i = 0; i < iterations; i++)
    {
        LotteryResult result;
        generate_draw(5, 1, 50, 2, 1, 12, &result, benchmark_callback);
    }
    ResourceSnapshot post_draw = take_resource_snapshot();
    long draw_rss_delta = post_draw.peak_rss_kb - baseline.peak_rss_kb;
    printf("  Post-draw peak RSS: %ld KB  (delta: %+ld KB)\n", post_draw.peak_rss_kb,
           draw_rss_delta);

    /* --- seed working-set --- */
    for (int i = 0; i < iterations / 10; i++)
    {
        volatile uint64_t s = generate_strong_seed();
        (void)s;
    }
    ResourceSnapshot post_seed = take_resource_snapshot();
    long seed_rss_delta = post_seed.peak_rss_kb - post_draw.peak_rss_kb;
    printf("  Post-seed peak RSS: %ld KB  (delta: %+ld KB)\n", post_seed.peak_rss_kb,
           seed_rss_delta);

    /* --- allocation pattern estimation --- */
    long total_rss_growth = post_seed.peak_rss_kb - baseline.peak_rss_kb;
    printf("\n  Total RSS growth  : %+ld KB over %d draw + %d seed iterations\n", total_rss_growth,
           iterations, iterations / 10);

    if (iterations > 0)
    {
        double bytes_per_draw = (draw_rss_delta * 1024.0) / iterations;
        printf("  Estimated bytes/draw (RSS proxy): %.2f B\n", bytes_per_draw);
    }

    /* --- hint for external profiling --- */
    printf("\n  [Hint] For Valgrind heap profiling run:\n");
    printf("    valgrind --tool=massif --pages-as-heap=yes ./benchmark %d\n", iterations);
    printf("    ms_print massif.out.<pid>\n");
    printf("  [Hint] For perf cache/CPU stats run:\n");
    printf("    perf stat -e cycles,instructions,L1-dcache-load-misses,"
           "LLC-load-misses ./benchmark %d\n",
           iterations);
}

/* -------------------------------------------------------------------------
 * Memory regression detection
 * ---------------------------------------------------------------------- */

#define MEMORY_BASELINE_FILE "benchmark_memory_baseline.txt"

/**
 * @brief Compare current peak RSS against a stored baseline and report.
 *
 * If no baseline file exists it is created.  If the current reading
 * exceeds the stored value by more than REGRESSION_THRESHOLD_KB the
 * function returns 1 (regression detected), otherwise 0.
 */
static int check_memory_regression(long current_peak_kb)
{
#define REGRESSION_THRESHOLD_KB 512L

    FILE *f = fopen(MEMORY_BASELINE_FILE, "r");
    if (f == NULL)
    {
        /* First run – record baseline */
        f = fopen(MEMORY_BASELINE_FILE, "w");
        if (f != NULL)
        {
            fprintf(f, "%ld\n", current_peak_kb);
            fclose(f);
            printf("  Memory baseline written: %ld KB  (%s)\n", current_peak_kb,
                   MEMORY_BASELINE_FILE);
        }
        return 0;
    }

    char buf[64];
    long baseline_kb = 0;
    if (fgets(buf, (int)sizeof(buf), f) == NULL)
    {
        fclose(f);
        return 0;
    }
    fclose(f);
    {
        char *endp;
        baseline_kb = strtol(buf, &endp, 10);
        if (endp == buf)
            return 0; /* parse failed */
    }

    long delta = current_peak_kb - baseline_kb;
    printf("  Memory baseline   : %ld KB\n", baseline_kb);
    printf("  Current peak RSS  : %ld KB\n", current_peak_kb);
    printf("  Delta             : %+ld KB  (threshold: %ld KB)\n", delta, REGRESSION_THRESHOLD_KB);

    if (delta > REGRESSION_THRESHOLD_KB)
    {
        printf("  ** MEMORY REGRESSION DETECTED: +%ld KB above baseline **\n", delta);
        return 1;
    }

    printf("  Memory usage within acceptable range.\n");
    return 0;

#undef REGRESSION_THRESHOLD_KB
}

/* -------------------------------------------------------------------------
 * Export timing benchmark
 * ---------------------------------------------------------------------- */

static void benchmark_export(int iterations)
{
    print_header("Export Timing Breakdown");

    /* Build a small result set to export repeatedly */
    int sample_count = 10;
    LotteryResult *samples = (LotteryResult *)malloc((size_t)sample_count * sizeof(LotteryResult));
    if (samples == NULL)
    {
        printf("  [skipped – malloc failed]\n");
        return;
    }
    for (int i = 0; i < sample_count; i++)
        generate_draw(5, 1, 50, 2, 1, 12, &samples[i], benchmark_callback);

    const char *tmp_csv = "/tmp/bench_export.csv";
    const char *tmp_json = "/tmp/bench_export.json";

    ExportCtx csv_ctx = {samples, sample_count, tmp_csv};
    ExportCtx json_ctx = {samples, sample_count, tmp_json};

    int export_iters = (iterations / 100 < 1) ? 1 : iterations / 100;

    TimedResult csv_r = run_timed(loop_export_csv, &csv_ctx, export_iters);
    TimedResult json_r = run_timed(loop_export_json, &json_ctx, export_iters);

    print_timed_result("CSV export", &csv_r, export_iters);
    print_timed_result("JSON export", &json_r, export_iters);

    free(samples);
    remove(tmp_csv);
    remove(tmp_json);
}

/* -------------------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------------- */

int main(int argc, const char *argv[])
{
    int iterations = 100000;

    if (argc > 1)
    {
        char *end;
        long val = strtol(argv[1], &end, 10);
        if (end != argv[1] && *end == '\0' && val > 0 && val <= INT_MAX)
            iterations = (int)val;
    }

    printf("============================================================\n");
    printf("Open-Lotto Performance Benchmarks\n");
    printf("============================================================\n");
    printf("Iterations: %d\n", iterations);

    /* ------------------------------------------------------------------
     * Per-operation timing breakdown
     * ------------------------------------------------------------------ */
    print_header("Per-Operation Timing & CPU Profiling");

    TimedResult rng_r = run_timed(loop_rng, NULL, iterations / 10);
    TimedResult euro_r = run_timed(loop_eurojackpot, NULL, iterations);
    TimedResult lotto_r = run_timed(loop_lotto, NULL, iterations);
    TimedResult seed_r = run_timed(loop_seed, NULL, iterations / 10);

    print_timed_result("RNG (5-main no extra)", &rng_r, iterations / 10);
    print_timed_result("Eurojackpot (5+2)", &euro_r, iterations);
    print_timed_result("Lotto 6aus49 (6+1)", &lotto_r, iterations);
    print_timed_result("Seed generation", &seed_r, iterations / 10);

    /* ------------------------------------------------------------------
     * Export timing
     * ------------------------------------------------------------------ */
    benchmark_export(iterations);

    /* ------------------------------------------------------------------
     * Memory profiling
     * ------------------------------------------------------------------ */
    benchmark_memory_profile(iterations);

    /* ------------------------------------------------------------------
     * Summary
     * ------------------------------------------------------------------ */
    print_header("Summary");
    printf("  %-38s: %12.0f ops/sec\n", "RNG draws/sec",
           (euro_r.wall_sec > 0.0) ? (iterations / euro_r.wall_sec) : 0.0);
    printf("  %-38s: %12.0f ops/sec\n", "Eurojackpot draws/sec",
           (euro_r.wall_sec > 0.0) ? (iterations / euro_r.wall_sec) : 0.0);
    printf("  %-38s: %12.0f ops/sec\n", "Lotto 6aus49 draws/sec",
           (lotto_r.wall_sec > 0.0) ? (iterations / lotto_r.wall_sec) : 0.0);
    printf("  %-38s: %12.0f ops/sec\n", "Seed generation/sec",
           (seed_r.wall_sec > 0.0) ? ((double)iterations / 10.0 / seed_r.wall_sec) : 0.0);

    /* ------------------------------------------------------------------
     * Memory regression check
     * ------------------------------------------------------------------ */
    print_header("Memory Regression Detection");
    ResourceSnapshot final_snap = take_resource_snapshot();
    int regression = check_memory_regression(final_snap.peak_rss_kb);

    printf("\n============================================================\n");

    return regression ? 1 : 0;
}
