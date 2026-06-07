/**
 * @file test_property.c
 * @brief Property-based tests for RNG and draw generation.
 *
 * Verifies that mathematical invariants hold across thousands of independently
 * seeded draws.  Each property is a universal statement that must be true for
 * every single draw, regardless of the random seed used:
 *
 *   P1 – Main numbers are always unique      (permutation property)
 *   P2 – Extra numbers are always unique     (permutation property)
 *   P3 – Main numbers are within [min, max]  (range property)
 *   P4 – Extra numbers are within [min, max] (range property)
 *   P5 – Main number count matches config    (cardinality property)
 *   P6 – Extra number count matches config   (cardinality property)
 *   P7 – Export CSV output is non-empty and contains expected tokens
 *   P8 – Export JSON output is non-empty and contains expected tokens
 *
 * Failures indicate a broken invariant that would produce invalid lottery
 * tickets; they should be treated as high-severity bugs.
 */

#include "../include/combogen.h"
#include "../include/export.h"
#include "test.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

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

static void silent_cb(DrawEvent event, const LotteryResult *result)
{
    (void)event;
    (void)result;
}

/* ------------------------------------------------------------------ */
/* Game configuration table                                             */
/* ------------------------------------------------------------------ */

typedef struct
{
    const char *name;
    int main_count;
    int main_min;
    int main_max;
    int extra_count;
    int extra_min;
    int extra_max;
} GameConfig;

static const GameConfig GAMES[] = {
    {"Eurojackpot", 5, 1, 50, 2, 1, 12},
    {"Lotto 6aus49", 6, 1, 49, 1, 0, 9},
};
static const int GAME_COUNT = (int)(sizeof(GAMES) / sizeof(GAMES[0]));

/* Number of independent draws executed per property check.
 * Large enough to catch non-deterministic failures, small enough
 * to keep the test suite fast (< 1 s on any modern CPU). */
#define PROPERTY_ITERATIONS 2000

/* ------------------------------------------------------------------ */
/* P1-P6 – structural invariants                                        */
/* ------------------------------------------------------------------ */

static void test_structural_properties(void)
{
    test_suite("Property Tests – Structural Invariants (P1-P6)");

    for (int g = 0; g < GAME_COUNT; g++)
    {
        const GameConfig *cfg = &GAMES[g];
        printf("\n[Game: %s | %d iterations]\n", cfg->name, PROPERTY_ITERATIONS);

        int p1_ok = 1, p2_ok = 1, p3_ok = 1, p4_ok = 1, p5_ok = 1, p6_ok = 1;

        for (int i = 0; i < PROPERTY_ITERATIONS; i++)
        {
            LotteryResult r;
            generate_draw(cfg->main_count, cfg->main_min, cfg->main_max, cfg->extra_count,
                          cfg->extra_min, cfg->extra_max, &r, silent_cb);

            if (!is_unique(r.main_numbers, r.main_count))
                p1_ok = 0;
            if (cfg->extra_count > 1 && !is_unique(r.extra_numbers, r.extra_count))
                p2_ok = 0;
            if (!in_range(r.main_numbers, r.main_count, cfg->main_min, cfg->main_max))
                p3_ok = 0;
            if (cfg->extra_count > 0 &&
                !in_range(r.extra_numbers, r.extra_count, cfg->extra_min, cfg->extra_max))
                p4_ok = 0;
            if (r.main_count != cfg->main_count)
                p5_ok = 0;
            if (r.extra_count != cfg->extra_count)
                p6_ok = 0;
        }

        char msg[128];

        snprintf(msg, sizeof(msg), "[%s] P1: main numbers always unique", cfg->name);
        assert_true(p1_ok, msg);

        if (cfg->extra_count > 1)
        {
            snprintf(msg, sizeof(msg), "[%s] P2: extra numbers always unique", cfg->name);
            assert_true(p2_ok, msg);
        }

        snprintf(msg, sizeof(msg), "[%s] P3: main numbers always in [%d,%d]", cfg->name,
                 cfg->main_min, cfg->main_max);
        assert_true(p3_ok, msg);

        if (cfg->extra_count > 0)
        {
            snprintf(msg, sizeof(msg), "[%s] P4: extra numbers always in [%d,%d]", cfg->name,
                     cfg->extra_min, cfg->extra_max);
            assert_true(p4_ok, msg);
        }

        snprintf(msg, sizeof(msg), "[%s] P5: main_count always == %d", cfg->name, cfg->main_count);
        assert_true(p5_ok, msg);

        snprintf(msg, sizeof(msg), "[%s] P6: extra_count always == %d", cfg->name,
                 cfg->extra_count);
        assert_true(p6_ok, msg);
    }
}

/* ------------------------------------------------------------------ */
/* P7 – CSV export always well-formed                                   */
/* ------------------------------------------------------------------ */

static void test_csv_export_property(void)
{
    test_suite("Property Tests – CSV Export Always Well-Formed (P7)");

    const GameConfig *cfg = &GAMES[0]; /* Eurojackpot */
    const char *path = "/tmp/open_lotto_prop_test.csv";
    const int EXPORT_ITERATIONS = 50;

    int p7_ok = 1;

    for (int i = 0; i < EXPORT_ITERATIONS; i++)
    {
        LotteryResult r;
        generate_draw(cfg->main_count, cfg->main_min, cfg->main_max, cfg->extra_count,
                      cfg->extra_min, cfg->extra_max, &r, silent_cb);

        int ret = export_results_csv_file(path, cfg->name, &r, 1);
        if (ret != 0)
        {
            p7_ok = 0;
            continue;
        }

        FILE *f = fopen(path, "r");
        if (!f)
        {
            p7_ok = 0;
            continue;
        }

        char line[256];
        /* Header must be present */
        if (!fgets(line, sizeof(line), f))
        {
            p7_ok = 0;
            fclose(f);
            continue;
        }
        if (strstr(line, "draw_number") == NULL)
        {
            p7_ok = 0;
            fclose(f);
            continue;
        }
        /* Data row must be present */
        if (!fgets(line, sizeof(line), f))
        {
            p7_ok = 0;
            fclose(f);
            continue;
        }
        /* Must start with "1," */
        if (strncmp(line, "1,", 2) != 0)
        {
            p7_ok = 0;
        }

        fclose(f);
        remove(path);
    }

    assert_true(p7_ok, "P7: CSV export always produces valid header + data row");
}

/* ------------------------------------------------------------------ */
/* P8 – JSON export always well-formed                                  */
/* ------------------------------------------------------------------ */

static void test_json_export_property(void)
{
    test_suite("Property Tests – JSON Export Always Well-Formed (P8)");

    const GameConfig *cfg = &GAMES[0]; /* Eurojackpot */
    const char *path = "/tmp/open_lotto_prop_test.json";
    const int EXPORT_ITERATIONS = 50;

    int p8_ok = 1;

    for (int i = 0; i < EXPORT_ITERATIONS; i++)
    {
        LotteryResult r;
        generate_draw(cfg->main_count, cfg->main_min, cfg->main_max, cfg->extra_count,
                      cfg->extra_min, cfg->extra_max, &r, silent_cb);

        int ret = export_results_json_file(path, cfg->name, &r, 1);
        if (ret != 0)
        {
            p8_ok = 0;
            continue;
        }

        FILE *f = fopen(path, "r");
        if (!f)
        {
            p8_ok = 0;
            continue;
        }

        char content[1024];
        size_t n = fread(content, 1, sizeof(content) - 1, f);
        fclose(f);
        remove(path);

        content[n] = '\0';
        /* Must look like JSON: starts with '{' and contains "draws" key */
        if (content[0] != '{' || strstr(content, "\"draws\"") == NULL)
        {
            p8_ok = 0;
        }
    }

    assert_true(p8_ok, "P8: JSON export always produces valid JSON with 'draws' key");
}

/* ------------------------------------------------------------------ */
/* Entry point                                                           */
/* ------------------------------------------------------------------ */

int main(void)
{
    test_structural_properties();
    test_csv_export_property();
    test_json_export_property();

    printf("\nTests passed: %d/%d\n", test_passed, test_count);
    return test_failed > 0 ? 1 : 0;
}
