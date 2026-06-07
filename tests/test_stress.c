/**
 * @file test_stress.c
 * @brief Stress tests for draw generation under high load.
 *
 * Exercises the draw generation engine with pathological and large-volume
 * inputs to surface memory corruption, integer overflow, or algorithmic
 * failures that only appear at scale.
 *
 * Test groups:
 *   S1 – High-volume draw generation (10 000 draws per game)
 *   S2 – Edge-case configurations (smallest/largest valid ranges)
 *   S3 – Rapid sequential calls without rest between them
 *   S4 – Export pipeline under load (1 000-draw export files)
 */

#include "../include/combogen.h"
#include "../include/export.h"
#include "test.h"
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static void silent_cb(DrawEvent event, const LotteryResult *result)
{
    (void)event;
    (void)result;
}

static int is_unique(const int *arr, int count)
{
    for (int i = 0; i < count; i++)
        for (int j = i + 1; j < count; j++)
            if (arr[i] == arr[j])
                return 0;
    return 1;
}

static int in_range(const int *arr, int count, int min, int max)
{
    for (int i = 0; i < count; i++)
        if (arr[i] < min || arr[i] > max)
            return 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* S1 – High-volume draw generation                                     */
/* ------------------------------------------------------------------ */

#define HIGH_VOLUME_DRAWS 10000

static void test_high_volume(void)
{
    test_suite("Stress S1 – High-Volume Draw Generation");

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
        int ok = 1;
        for (int i = 0; i < HIGH_VOLUME_DRAWS; i++)
        {
            LotteryResult r;
            generate_draw(games[g].mc, games[g].mn, games[g].mx, games[g].ec, games[g].en,
                          games[g].ex, &r, silent_cb);

            if (!is_unique(r.main_numbers, r.main_count) ||
                !in_range(r.main_numbers, r.main_count, games[g].mn, games[g].mx) ||
                r.main_count != games[g].mc || r.extra_count != games[g].ec)
            {
                ok = 0;
                break;
            }
        }

        char msg[128];
        snprintf(msg, sizeof(msg), "[%s] %d draws: all valid (unique, in-range, correct counts)",
                 games[g].name, HIGH_VOLUME_DRAWS);
        assert_true(ok, msg);
    }
}

/* ------------------------------------------------------------------ */
/* S2 – Edge-case configurations                                        */
/* ------------------------------------------------------------------ */

static void test_edge_case_configs(void)
{
    test_suite("Stress S2 – Edge-Case Configurations");

    /* Minimum viable draw: pick 1 number from a pool of 1 */
    {
        LotteryResult r;
        generate_draw(1, 42, 42, 0, 0, 0, &r, silent_cb);
        assert_equals(r.main_count, 1, "Edge: 1-of-1 pool → main_count == 1");
        assert_equals(r.main_numbers[0], 42, "Edge: 1-of-1 pool → value == 42");
        assert_equals(r.extra_count, 0, "Edge: no extras configured → extra_count == 0");
    }

    /* Pick maximum supported main numbers (MAX_MAIN_NUMBERS = 7) */
    {
        LotteryResult r;
        generate_draw(MAX_MAIN_NUMBERS, 1, 50, 0, 0, 0, &r, silent_cb);
        assert_equals(r.main_count, MAX_MAIN_NUMBERS,
                      "Edge: MAX_MAIN_NUMBERS draws → count correct");
        assert_true(is_unique(r.main_numbers, r.main_count),
                    "Edge: MAX_MAIN_NUMBERS draws → all unique");
        assert_true(in_range(r.main_numbers, r.main_count, 1, 50),
                    "Edge: MAX_MAIN_NUMBERS draws → all in range");
    }

    /* Pick maximum supported extra numbers (MAX_EXTRA_NUMBERS = 3) */
    {
        LotteryResult r;
        generate_draw(1, 1, 10, MAX_EXTRA_NUMBERS, 1, 20, &r, silent_cb);
        assert_equals(r.extra_count, MAX_EXTRA_NUMBERS,
                      "Edge: MAX_EXTRA_NUMBERS extras → count correct");
        assert_true(is_unique(r.extra_numbers, r.extra_count),
                    "Edge: MAX_EXTRA_NUMBERS extras → all unique");
        assert_true(in_range(r.extra_numbers, r.extra_count, 1, 20),
                    "Edge: MAX_EXTRA_NUMBERS extras → all in range");
    }

    /* Large pool: pick 5 from 1..1000 */
    {
        int ok = 1;
        for (int i = 0; i < 1000; i++)
        {
            LotteryResult r;
            generate_draw(5, 1, 1000, 0, 0, 0, &r, silent_cb);
            if (!is_unique(r.main_numbers, 5) || !in_range(r.main_numbers, 5, 1, 1000))
            {
                ok = 0;
                break;
            }
        }
        assert_true(ok, "Edge: 5-of-1000 pool (1000 draws) → always valid");
    }
}

/* ------------------------------------------------------------------ */
/* S3 – Rapid sequential calls                                          */
/* ------------------------------------------------------------------ */

#define RAPID_CALLS 5000

static void test_rapid_sequential(void)
{
    test_suite("Stress S3 – Rapid Sequential Calls");

    int no_collision = 1;

    /* Run many back-to-back draws and check that consecutive results
     * are not suspiciously identical (tests RNG re-seeding / state). */
    LotteryResult prev, cur;
    generate_draw(5, 1, 50, 2, 1, 12, &prev, silent_cb);

    int identical_count = 0;

    for (int i = 0; i < RAPID_CALLS; i++)
    {
        generate_draw(5, 1, 50, 2, 1, 12, &cur, silent_cb);

        /* Identical consecutive draws are astronomically unlikely.
         * Allow at most 1 coincidence in RAPID_CALLS iterations. */
        int same = 1;
        for (int j = 0; j < cur.main_count; j++)
            if (cur.main_numbers[j] != prev.main_numbers[j])
            {
                same = 0;
                break;
            }
        if (same)
            identical_count++;

        prev = cur;
    }

    if (identical_count > 1)
        no_collision = 0;

    assert_true(no_collision, "S3: consecutive draws are not suspiciously identical");
    assert_true(identical_count <= 1, "S3: at most 1 coincidental identical consecutive draw");
}

/* ------------------------------------------------------------------ */
/* S4 – Export pipeline under load                                      */
/* ------------------------------------------------------------------ */

#define EXPORT_STRESS_DRAWS 1000

static void test_export_stress(void)
{
    test_suite("Stress S4 – Export Pipeline Under Load");

    /* Build a result array */
    LotteryResult results[EXPORT_STRESS_DRAWS];
    for (int i = 0; i < EXPORT_STRESS_DRAWS; i++)
        generate_draw(5, 1, 50, 2, 1, 12, &results[i], silent_cb);

    /* CSV export of 1000 draws (one draw at a time, overwriting each call) */
    const char *csv_path = "/tmp/open_lotto_stress_export.csv";
    int csv_ok = 1;
    for (int i = 0; i < EXPORT_STRESS_DRAWS; i++)
    {
        int ret = export_results_csv_file(csv_path, "Eurojackpot", &results[i], 1);
        if (ret != 0)
        {
            csv_ok = 0;
            break;
        }
    }
    assert_true(csv_ok, "S4: CSV export succeeds for all 1000 draws");

    /* Verify final file is readable and non-empty */
    FILE *f = fopen(csv_path, "r");
    assert_true(f != NULL, "S4: final CSV file is readable");
    if (f)
    {
        char line[256];
        int has_header = 0, has_data = 0;
        if (fgets(line, sizeof(line), f) && strstr(line, "draw_number"))
            has_header = 1;
        if (fgets(line, sizeof(line), f))
            has_data = 1;
        fclose(f);
        remove(csv_path);
        assert_true(has_header, "S4: CSV file has header row");
        assert_true(has_data, "S4: CSV file has at least one data row");
    }

    /* JSON export of 1000 draws (one draw at a time) */
    const char *json_path = "/tmp/open_lotto_stress_export.json";
    int json_ok = 1;
    for (int i = 0; i < EXPORT_STRESS_DRAWS; i++)
    {
        int ret = export_results_json_file(json_path, "Eurojackpot", &results[i], 1);
        if (ret != 0)
        {
            json_ok = 0;
            break;
        }
    }
    assert_true(json_ok, "S4: JSON export succeeds for all 1000 draws");

    f = fopen(json_path, "r");
    assert_true(f != NULL, "S4: final JSON file is readable");
    if (f)
    {
        char buf[256];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        remove(json_path);
        buf[n] = '\0';
        assert_true(buf[0] == '{', "S4: JSON file starts with '{'");
    }
}

/* ------------------------------------------------------------------ */
/* Entry point                                                           */
/* ------------------------------------------------------------------ */

int main(void)
{
    test_high_volume();
    test_edge_case_configs();
    test_rapid_sequential();
    test_export_stress();

    printf("\nTests passed: %d/%d\n", test_passed, test_count);
    return test_failed > 0 ? 1 : 0;
}
