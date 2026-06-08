/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

/**
 * @file test_perf_regression.c
 * @brief Performance regression tests for draw generation.
 *
 * Guards against performance regressions by measuring draw throughput and
 * failing if it drops below a conservative lower bound.  The lower bound is
 * deliberately generous (10× below typical hardware) so the test is portable
 * and never flakes on heavily-loaded CI machines, while still catching
 * catastrophic regressions such as accidental O(n²) loops or unbounded
 * blocking calls.
 *
 * Test groups:
 *   PR1 – Single-draw latency (Eurojackpot, Lotto 6aus49)
 *   PR2 – Batch throughput (draws per second ≥ MIN_DRAWS_PER_SEC)
 *   PR3 – Export throughput (exports per second ≥ MIN_EXPORTS_PER_SEC)
 *   PR4 – Per-draw time does not regress across batches (CV check)
 */

#include "../include/combogen.h"
#include "../include/export.h"
#include "test.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

/* ------------------------------------------------------------------ */
/* Tunable limits                                                        */
/* ------------------------------------------------------------------ */

/** Minimum acceptable draw throughput (draws/second).
 *  Set conservatively to pass even on slow CI runners. */
#define MIN_DRAWS_PER_SEC 5000

/** Minimum acceptable CSV export throughput (exports/second). */
#define MIN_EXPORTS_PER_SEC 500

/** Maximum acceptable time for a single draw (microseconds). */
#define MAX_SINGLE_DRAW_US 5000 /* 5 ms – only fails on pathological hang */

/** Number of draws per throughput batch. */
#define THROUGHPUT_BATCH 50000

/** Number of CSV exports for the export throughput test. */
#define EXPORT_BATCH 2000

/* ------------------------------------------------------------------ */
/* Timing                                                               */
/* ------------------------------------------------------------------ */

static uint64_t now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

static void silent_cb(DrawEvent event, const LotteryResult *result)
{
    (void)event;
    (void)result;
}

/* ------------------------------------------------------------------ */
/* PR1 – Single-draw latency                                            */
/* ------------------------------------------------------------------ */

static void test_single_draw_latency(void)
{
    test_suite("Perf Regression PR1 – Single-Draw Latency");

    struct
    {
        const char *name;
        int mc;
        int mn;
        int mx;
        int ec;
        int en;
        int ex;
    } games[] = {
        {"Eurojackpot", 5, 1, 50, 2, 1, 12},
        {"Lotto 6aus49", 6, 1, 49, 1, 0, 9},
    };
    int ngames = (int)(sizeof(games) / sizeof(games[0]));

    for (int g = 0; g < ngames; g++)
    {
        LotteryResult r;
        uint64_t t0 = now_us();
        generate_draw(games[g].mc, games[g].mn, games[g].mx, games[g].ec, games[g].en, games[g].ex,
                      &r, silent_cb);
        uint64_t elapsed = now_us() - t0;

        char msg[128];
        snprintf(msg, sizeof(msg), "[%s] single draw < %d µs (got %llu µs)", games[g].name,
                 MAX_SINGLE_DRAW_US, (unsigned long long)elapsed);
        assert_true(elapsed < MAX_SINGLE_DRAW_US, msg);
    }
}

/* ------------------------------------------------------------------ */
/* PR2 – Batch throughput                                               */
/* ------------------------------------------------------------------ */

static void test_batch_throughput(void)
{
    test_suite("Perf Regression PR2 – Batch Throughput");

    struct
    {
        const char *name;
        int mc;
        int mn;
        int mx;
        int ec;
        int en;
        int ex;
    } games[] = {
        {"Eurojackpot", 5, 1, 50, 2, 1, 12},
        {"Lotto 6aus49", 6, 1, 49, 1, 0, 9},
    };
    int ngames = (int)(sizeof(games) / sizeof(games[0]));

    for (int g = 0; g < ngames; g++)
    {
        uint64_t t0 = now_us();
        for (int i = 0; i < THROUGHPUT_BATCH; i++)
        {
            LotteryResult r;
            generate_draw(games[g].mc, games[g].mn, games[g].mx, games[g].ec, games[g].en,
                          games[g].ex, &r, silent_cb);
        }
        uint64_t elapsed_us = now_us() - t0;
        double elapsed_s = elapsed_us / 1e6;
        double dps = THROUGHPUT_BATCH / elapsed_s;

        printf("  [%s] %d draws in %.3f s  →  %.0f draws/s  (min: %d)\n", games[g].name,
               THROUGHPUT_BATCH, elapsed_s, dps, MIN_DRAWS_PER_SEC);

        char msg[128];
        snprintf(msg, sizeof(msg), "[%s] throughput ≥ %d draws/s (got %.0f)", games[g].name,
                 MIN_DRAWS_PER_SEC, dps);
        assert_true(dps >= MIN_DRAWS_PER_SEC, msg);
    }
}

/* ------------------------------------------------------------------ */
/* PR3 – Export throughput                                              */
/* ------------------------------------------------------------------ */

static void test_export_throughput(void)
{
    test_suite("Perf Regression PR3 – Export Throughput");

    LotteryResult r;
    generate_draw(5, 1, 50, 2, 1, 12, &r, silent_cb);

    /* CSV */
    {
        const char *csv_path = "/tmp/open_lotto_perf_export.csv";
        uint64_t t0 = now_us();
        for (int i = 0; i < EXPORT_BATCH; i++)
            export_results_csv_file(csv_path, "Eurojackpot", &r, 1);
        uint64_t elapsed_us = now_us() - t0;
        double elapsed_s = elapsed_us / 1e6;
        double eps = EXPORT_BATCH / elapsed_s;
        remove(csv_path);

        printf("  [CSV]  %d exports in %.3f s  →  %.0f exports/s  (min: %d)\n", EXPORT_BATCH,
               elapsed_s, eps, MIN_EXPORTS_PER_SEC);

        char msg[64];
        snprintf(msg, sizeof(msg), "CSV export throughput ≥ %d/s (got %.0f)", MIN_EXPORTS_PER_SEC,
                 eps);
        assert_true(eps >= MIN_EXPORTS_PER_SEC, msg);
    }

    /* JSON */
    {
        const char *json_path = "/tmp/open_lotto_perf_export.json";
        uint64_t t0 = now_us();
        for (int i = 0; i < EXPORT_BATCH; i++)
            export_results_json_file(json_path, "Eurojackpot", &r, 1);
        uint64_t elapsed_us = now_us() - t0;
        double elapsed_s = elapsed_us / 1e6;
        double eps = EXPORT_BATCH / elapsed_s;
        remove(json_path);

        printf("  [JSON] %d exports in %.3f s  →  %.0f exports/s  (min: %d)\n", EXPORT_BATCH,
               elapsed_s, eps, MIN_EXPORTS_PER_SEC);

        char msg[64];
        snprintf(msg, sizeof(msg), "JSON export throughput ≥ %d/s (got %.0f)", MIN_EXPORTS_PER_SEC,
                 eps);
        assert_true(eps >= MIN_EXPORTS_PER_SEC, msg);
    }
}

/* ------------------------------------------------------------------ */
/* PR4 – Consistency: per-draw time does not regress across batches     */
/*                                                                       */
/* Splits THROUGHPUT_BATCH draws into SEGMENTS segments and checks that  */
/* the slowest segment is at most SLOWDOWN_FACTOR × the fastest segment. */
/* A large ratio indicates a non-linear performance cliff (e.g. memory  */
/* fragmentation, seed re-init stall) that would not be caught by the   */
/* average-throughput test.                                              */
/* ------------------------------------------------------------------ */

#define SEGMENTS 5
#define SEGMENT_SIZE (THROUGHPUT_BATCH / SEGMENTS)
#define SLOWDOWN_FACTOR 5.0 /* slowest segment may be at most 5× fastest */

static void test_throughput_consistency(void)
{
    test_suite("Perf Regression PR4 – Throughput Consistency Across Batches");

    double seg_dps[SEGMENTS];

    for (int s = 0; s < SEGMENTS; s++)
    {
        uint64_t t0 = now_us();
        for (int i = 0; i < SEGMENT_SIZE; i++)
        {
            LotteryResult r;
            generate_draw(5, 1, 50, 2, 1, 12, &r, silent_cb);
        }
        uint64_t elapsed_us = now_us() - t0;
        double elapsed_s = elapsed_us > 0 ? elapsed_us / 1e6 : 1e-6;
        seg_dps[s] = SEGMENT_SIZE / elapsed_s;
        printf("  Segment %d/%d: %.0f draws/s\n", s + 1, SEGMENTS, seg_dps[s]);
    }

    double fastest = seg_dps[0], slowest = seg_dps[0];
    for (int s = 1; s < SEGMENTS; s++)
    {
        if (seg_dps[s] > fastest)
            fastest = seg_dps[s];
        if (seg_dps[s] < slowest)
            slowest = seg_dps[s];
    }

    double ratio = (slowest > 0) ? fastest / slowest : SLOWDOWN_FACTOR + 1;
    printf("  Fastest/slowest ratio: %.2f×  (max allowed: %.1f×)\n", ratio, SLOWDOWN_FACTOR);

    char msg[128];
    snprintf(msg, sizeof(msg), "PR4: throughput consistent across batches (ratio %.2f× ≤ %.1f×)",
             ratio, SLOWDOWN_FACTOR);
    assert_true(ratio <= SLOWDOWN_FACTOR, msg);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                           */
/* ------------------------------------------------------------------ */

int main(void)
{
    test_single_draw_latency();
    test_batch_throughput();
    test_export_throughput();
    test_throughput_consistency();

    printf("\nTests passed: %d/%d\n", test_passed, test_count);
    return test_failed > 0 ? 1 : 0;
}
