/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

/**
 * @file main.c
 * @brief CLI entrypoint, option parsing, orchestration, and command execution.
 */

#include "analytics.h"
#include "analytics_data_quality.h"
#include "combogen.h"
#include "config.h"
#include "export.h"
#include "gui_opengl.h"
#include "gui_sdl.h"
#include "historical_db.h"
#include "localization.h"
#include "log.h"
#include "plugin_loader.h"
#include "plugin_registry.h"
#include "random_seed.h"
#include "seed_calibration.h"
#include "simulation_analytics_advanced.h"
#include "simulation_analytics_core.h"
#include "simulation_analytics_export.h"
#include "simulation_analytics_metadata.h"
#include "theme.h"
#include "validate.h"
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define portable_msleep(ms) Sleep(ms)
#else
#include <strings.h>
#include <unistd.h>
#define portable_msleep(ms) usleep((ms) * 1000)
#endif
#include <time.h>

/* ---------------------------------------------------------
   SPINNER ANIMATION
   --------------------------------------------------------- */
static const char *SPINNER_FRAMES[] = {"-", "\\", "|", "/", "-", "\\", "|", "/", "-", "/"};
static const int SPINNER_COUNT = 10;
static const char *g_cli_locale = "en";

static int compare_int_asc(const void *a, const void *b)
{
    const int lhs = *(const int *)a;
    const int rhs = *(const int *)b;
    return (lhs > rhs) - (lhs < rhs);
}

static void sort_lottery_result_numbers(LotteryResult *result)
{
    if (!result)
        return;

    if (result->main_count > 1)
    {
        qsort(result->main_numbers, (size_t)result->main_count, sizeof(result->main_numbers[0]),
              compare_int_asc);
    }

    if (result->extra_count > 1)
    {
        qsort(result->extra_numbers, (size_t)result->extra_count, sizeof(result->extra_numbers[0]),
              compare_int_asc);
    }
}

/* ---------------------------------------------------------
   ANIMATION HELPERS
   --------------------------------------------------------- */

/* Print main numbers range from index 0 to count-1 */
static void print_main_numbers(const LotteryResult *result, int count)
{
    printf("%s: ", localization_get(g_cli_locale, LOCALIZE_MAIN));
    for (int j = 0; j < count; j++)
        printf("%d ", result->main_numbers[j]);
}

/* Print main numbers + separator + extra numbers range */
static void print_main_and_extra(const LotteryResult *result, int main_count, int extra_count)
{
    printf("%s: ", localization_get(g_cli_locale, LOCALIZE_MAIN));
    for (int j = 0; j < main_count; j++)
        printf("%d ", result->main_numbers[j]);
    printf("| %s: ", localization_get(g_cli_locale, LOCALIZE_EXTRA));
    for (int j = 0; j < extra_count; j++)
        printf("%d ", result->extra_numbers[j]);
}

/* Animate a single number reveal with spinner animation */
static void animate_number_reveal(int number, void (*print_prefix_fn)(const LotteryResult *, int),
                                  const LotteryResult *result, int revealed_count)
{
    /* Spinner animation frames */
    for (int f = 0; f < SPINNER_COUNT; f++)
    {
        printf("\r");
        print_prefix_fn(result, revealed_count);
        printf("%s ", SPINNER_FRAMES[f]);
        fflush(stdout);
        portable_msleep(30);
    }

    /* Reveal the actual number */
    printf("\r");
    print_prefix_fn(result, revealed_count);
    printf("%d ", number);
    fflush(stdout);
    portable_msleep(150);
}

/* ---------------------------------------------------------
   ANIMATE NUMBER REVEAL (same line, spinner-only)
   --------------------------------------------------------- */
static void animate_numbers(const LotteryInfo *info, const LotteryResult *result)
{
    printf("%s\n\n", localization_get(g_cli_locale, LOCALIZE_DRAWING_NUMBERS));

    printf("%s: ", localization_get(g_cli_locale, LOCALIZE_MAIN));

    /* Animate main numbers */
    for (int i = 0; i < info->main_count; i++)
    {
        animate_number_reveal(result->main_numbers[i], print_main_numbers, result, i);
    }

    /* Animate extra numbers if present */
    if (info->extra_count > 0)
    {
        printf("| %s: ", localization_get(g_cli_locale, LOCALIZE_EXTRA));

        for (int i = 0; i < info->extra_count; i++)
        {
            /* Custom print function for extra numbers */
            for (int f = 0; f < SPINNER_COUNT; f++)
            {
                printf("\r");
                print_main_and_extra(result, info->main_count, i);
                printf("%s ", SPINNER_FRAMES[f]);
                fflush(stdout);
                portable_msleep(30);
            }

            /* Reveal extra number */
            printf("\r");
            print_main_and_extra(result, info->main_count, i);
            printf("%d ", result->extra_numbers[i]);
            fflush(stdout);
            portable_msleep(150);
        }
    }

    printf("\n\n");
}

/* ---------------------------------------------------------
   SILENT CALLBACK FOR ANIMATION MODE
   --------------------------------------------------------- */
/**
 * @brief No-op draw callback used when deterministic generation needs no animation hooks.
 */
static void silent_callback(DrawEvent event, const LotteryResult *res)
{
    (void)event;
    (void)res;
}

/* Print draw result in normal (non-animated) CLI mode */
static void print_draw_result(const char *game_name, int draw_num, const LotteryResult *result)
{
    printf("%s (%s %d):\n", game_name, localization_get(g_cli_locale, LOCALIZE_DRAW), draw_num);
    printf("  %s: ", localization_get(g_cli_locale, LOCALIZE_MAIN));
    for (int j = 0; j < result->main_count; j++)
        printf("%d ", result->main_numbers[j]);

    if (result->extra_count > 0)
    {
        printf("| %s: ", localization_get(g_cli_locale, LOCALIZE_EXTRA));
        for (int j = 0; j < result->extra_count; j++)
            printf("%d ", result->extra_numbers[j]);
    }

    printf("\n\n");
}

/* ---------------------------------------------------------
   PARSE LOG LEVEL FROM STRING
   --------------------------------------------------------- */
/**
 * @brief Parse CLI log level strings into enum values.
 */
static LogLevel parse_log_level(const char *level_str)
{
    if (!level_str)
        return LOG_INFO;

    if (strcasecmp(level_str, "ERROR") == 0)
        return LOG_ERROR;
    if (strcasecmp(level_str, "WARN") == 0)
        return LOG_WARN;
    if (strcasecmp(level_str, "INFO") == 0)
        return LOG_INFO;
    if (strcasecmp(level_str, "DEBUG") == 0)
        return LOG_DEBUG;

    fprintf(stderr, "Invalid log level: %s (use ERROR, WARN, INFO, or DEBUG)\n", level_str);
    return LOG_INFO;
}

/** @brief SplitMix64 mixing step used for deterministic seed derivation. */
static uint64_t splitmix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

/**
 * @brief Parse decimal or 0x-prefixed seed strings.
 *
 * Rejects empty strings, leading whitespace, signs (strtoull would silently turn "-1" into
 * UINT64_MAX), trailing garbage and out-of-range values.
 */
static int parse_seed_value(const char *seed_str, uint64_t *out_seed)
{
    if (!seed_str || !isdigit((unsigned char)seed_str[0]))
        return 0;

    char *end = NULL;
    errno = 0;

    unsigned long long parsed = strtoull(seed_str, &end, 0);
    if (errno != 0 || end == seed_str || (end && *end != '\0'))
        return 0;

    *out_seed = (uint64_t)parsed;
    return 1;
}

/** @brief Write @p s to @p out as a JSON string literal (including the surrounding quotes). */
static void fprint_json_string(FILE *out, const char *s)
{
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++)
    {
        switch (*p)
        {
        case '"':
            fputs("\\\"", out);
            break;
        case '\\':
            fputs("\\\\", out);
            break;
        case '\n':
            fputs("\\n", out);
            break;
        case '\r':
            fputs("\\r", out);
            break;
        case '\t':
            fputs("\\t", out);
            break;
        default:
            if (*p < 0x20)
                fprintf(out, "\\u%04x", (unsigned)*p);
            else
                fputc(*p, out);
            break;
        }
    }
    fputc('"', out);
}

/** @brief Derive a stable per-draw seed from a base seed and draw index. */
static uint64_t derive_draw_seed(uint64_t base_seed, int draw_index)
{
    if (draw_index == 0)
        return base_seed;

    /* Hash the base first so nearby base seeds do not share per-draw seeds.
     * Must stay identical to open_lotto_derive_seed(). */
    return splitmix64(splitmix64(base_seed) + (uint64_t)draw_index);
}

/** @brief Sample one seed uniformly from an inclusive seed domain. */
static uint64_t sample_seed_in_domain(uint64_t *state, uint64_t seed_start, uint64_t seed_end)
{
    *state = splitmix64(*state);
    if (seed_start == 0 && seed_end == UINT64_MAX)
        return *state;

    uint64_t domain_size = seed_end - seed_start + 1ULL;
    uint64_t limit = UINT64_MAX - (UINT64_MAX % domain_size);
    while (*state > limit)
        *state = splitmix64(*state);
    return seed_start + (*state % domain_size);
}

/**
 * @brief Build a unique random sample of candidate seeds without duplicates.
 */
static int build_unique_seed_sample(uint64_t seed_start, uint64_t seed_end, int seed_count,
                                    uint64_t sampling_seed, uint64_t *out_seeds)
{
    if (!out_seeds || seed_count <= 0)
        return 0;

    size_t table_capacity = 1;
    while (table_capacity < (size_t)(seed_count * 2))
        table_capacity <<= 1;

    uint64_t *table = calloc(table_capacity, sizeof(uint64_t));
    unsigned char *used = calloc(table_capacity, sizeof(unsigned char));
    if (!table || !used)
    {
        free(table);
        free(used);
        return 0;
    }

    uint64_t state = sampling_seed;
    int written = 0;
    while (written < seed_count)
    {
        uint64_t candidate = sample_seed_in_domain(&state, seed_start, seed_end);
        size_t idx = (size_t)(candidate & (table_capacity - 1));

        while (used[idx] && table[idx] != candidate)
            idx = (idx + 1) & (table_capacity - 1);

        if (used[idx])
            continue;

        used[idx] = 1;
        table[idx] = candidate;
        out_seeds[written++] = candidate;
    }

    free(table);
    free(used);
    return 1;
}

typedef struct
{
    LotteryInfo info;
} SeedCalibrationDrawContext;

/** Size of per-number frequency tables (indexed directly by number value). */
#define CLOSEST_SEED_FREQ_TABLE_SIZE (COMBOGEN_MAX_NUMBER + 1)

/** Upper bound accepted for reference.historical_draw_count in closest-seed input files. */
#define CLOSEST_SEED_MAX_HISTORICAL_DRAWS ANALYTICS_MAX_DRAWS

typedef struct
{
    int historical_draw_count;
    uint64_t seeds[SEED_CALIBRATION_MAX_TOP_K];
    int seed_count;
    double main_frequency[CLOSEST_SEED_FREQ_TABLE_SIZE];
    double extra_frequency[CLOSEST_SEED_FREQ_TABLE_SIZE];
    int has_main_frequency;
    int has_extra_frequency;
} ClosestSeedReference;

/** @brief Per-seed normalized number frequencies over the calibrated period (draws 0..N-1). */
typedef struct
{
    double main_frequency[SEED_CALIBRATION_MAX_TOP_K][CLOSEST_SEED_FREQ_TABLE_SIZE];
    double extra_frequency[SEED_CALIBRATION_MAX_TOP_K][CLOSEST_SEED_FREQ_TABLE_SIZE];
} ClosestSeedPeriodFrequencies;

/**
 * @brief Check that a game's number ranges fit the closest-seed frequency tables.
 * @return 1 when supported, 0 (after printing an error) otherwise.
 */
static int closest_seed_game_supported(const LotteryInfo *info)
{
    if (!info || info->main_min < 0 || info->main_max > COMBOGEN_MAX_NUMBER ||
        info->main_min > info->main_max ||
        (info->extra_count > 0 && (info->extra_min < 0 || info->extra_max > COMBOGEN_MAX_NUMBER ||
                                   info->extra_min > info->extra_max)))
    {
        fprintf(stderr, "Error: game number ranges must lie within 0..%d for closest-seed modes.\n",
                COMBOGEN_MAX_NUMBER);
        return 0;
    }
    return 1;
}

/**
 * @brief Adapter from seed calibration callbacks to core seeded draw generation.
 */
static int seed_calibration_draw_callback(void *ctx, uint64_t seed, int draw_index,
                                          LotteryResult *out_result)
{
    if (!ctx || !out_result)
        return -1;

    SeedCalibrationDrawContext *draw_ctx = (SeedCalibrationDrawContext *)ctx;
    generate_draw_seeded(draw_ctx->info.main_count, draw_ctx->info.main_min,
                         draw_ctx->info.main_max, draw_ctx->info.extra_count,
                         draw_ctx->info.extra_min, draw_ctx->info.extra_max,
                         derive_draw_seed(seed, draw_index), out_result, silent_callback);
    return 0;
}

/** @brief Print closest-seed results as a human-readable table. */
static void print_closest_seed_table(FILE *out, const SeedCalibrationResult *result)
{
    fprintf(out, "Closest seed calibration\n");
    fprintf(out, "best_seed,%llu\n", (unsigned long long)result->best.seed);
    fprintf(out, "best_score,%.9f\n", result->best.total_score);
    fprintf(out, "evaluated_seeds,%d\n", result->evaluated_seeds);
    fprintf(out, "score_gap,%.9f\n", result->score_gap);
    fprintf(out, "\nTop Candidates\n");
    fprintf(out, "rank,seed,total,frequency,gap,rank_score\n");
    for (int i = 0; i < result->top_candidate_count; i++)
    {
        const SeedCalibrationCandidate *c = &result->top_candidates[i];
        fprintf(out, "%d,%llu,%.9f,%.9f,%.9f,%.9f\n", i + 1, (unsigned long long)c->seed,
                c->total_score, c->frequency_score, c->gap_score, c->rank_score);
    }
}

/** @brief Print closest-seed results as CSV. */
static void print_closest_seed_csv(FILE *out, const SeedCalibrationResult *result)
{
    fprintf(out, "section,key,value,extra\n");
    fprintf(out, "summary,best_seed,%llu,\n", (unsigned long long)result->best.seed);
    fprintf(out, "summary,best_score,%.9f,\n", result->best.total_score);
    fprintf(out, "summary,evaluated_seeds,%d,\n", result->evaluated_seeds);
    fprintf(out, "diagnostics,score_gap,%.9f,\n", result->score_gap);
    for (int i = 0; i < result->top_candidate_count; i++)
    {
        const SeedCalibrationCandidate *c = &result->top_candidates[i];
        fprintf(out, "top_candidates,%d,%llu,%.9f|%.9f|%.9f|%.9f\n", i + 1,
                (unsigned long long)c->seed, c->total_score, c->frequency_score, c->gap_score,
                c->rank_score);
    }
}

/** @brief Print closest-seed results as JSON. */
static void print_closest_seed_json(FILE *out, const SeedCalibrationResult *result,
                                    const char *game_name, const char *from_date,
                                    const char *to_date, const LotteryInfo *info,
                                    const ClosestSeedReference *reference)
{
    fprintf(out, "{\n");
    fprintf(out, "  \"mode\": \"closest-seed\",\n");
    fprintf(out, "  \"schema_version\": \"seed-calibration/v1\",\n");
    fprintf(out, "  \"game\": ");
    fprint_json_string(out, game_name ? game_name : "unknown");
    fprintf(out, ",\n  \"period\": {\"from\": ");
    fprint_json_string(out, from_date);
    fprintf(out, ", \"to\": ");
    fprint_json_string(out, to_date);
    fprintf(out, "},\n");
    fprintf(
        out,
        "  \"summary\": {\"best_seed\": %llu, \"best_score\": %.9f, \"evaluated_seeds\": %d},\n",
        (unsigned long long)result->best.seed, result->best.total_score, result->evaluated_seeds);
    fprintf(out,
            "  \"diagnostics\": {\"score_gap\": %.9f, \"score_components\": {\"frequency\": %.9f, "
            "\"gap\": %.9f, \"rank\": %.9f}},\n",
            result->score_gap, result->best.frequency_score, result->best.gap_score,
            result->best.rank_score);
    fprintf(out, "  \"top_candidates\": [\n");
    for (int i = 0; i < result->top_candidate_count; i++)
    {
        const SeedCalibrationCandidate *c = &result->top_candidates[i];
        fprintf(out,
                "    {\"rank\": %d, \"seed\": %llu, \"score\": %.9f, \"frequency\": %.9f, "
                "\"gap\": %.9f, \"rank_score\": %.9f}%s\n",
                i + 1, (unsigned long long)c->seed, c->total_score, c->frequency_score,
                c->gap_score, c->rank_score, (i + 1 == result->top_candidate_count) ? "" : ",");
    }
    fprintf(out, "  ],\n");

    fprintf(out, "  \"reference\": {\"historical_draw_count\": %d, \"main_frequency\": [",
            reference ? reference->historical_draw_count : 0);
    int emitted = 0;
    if (reference && info)
    {
        for (int n = info->main_min; n <= info->main_max; n++)
        {
            if (emitted++)
                fprintf(out, ", ");
            fprintf(out, "{\"number\": %d, \"frequency\": %.12f}", n, reference->main_frequency[n]);
        }
    }
    fprintf(out, "]");

    if (info && info->extra_count > 0)
    {
        fprintf(out, ", \"extra_frequency\": [");
        emitted = 0;
        if (reference)
        {
            for (int n = info->extra_min; n <= info->extra_max; n++)
            {
                if (emitted++)
                    fprintf(out, ", ");
                fprintf(out, "{\"number\": %d, \"frequency\": %.12f}", n,
                        reference->extra_frequency[n]);
            }
        }
        fprintf(out, "]");
    }

    fprintf(out, "}\n");
    fprintf(out, "}\n");
}

/**
 * @brief Build normalized historical frequencies for main/extra numbers in one period.
 * @return 1 on success, 0 when a historical number lies outside the game's range.
 */
static int build_closest_seed_reference(const HistoricalDraw *draws, int draw_count,
                                        const LotteryInfo *info, ClosestSeedReference *out)
{
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!draws || draw_count <= 0 || !info)
        return 1;

    out->historical_draw_count = draw_count;

    for (int i = 0; i < draw_count; i++)
    {
        for (int j = 0; j < draws[i].result.main_count; j++)
        {
            int n = draws[i].result.main_numbers[j];
            if (n < info->main_min || n > info->main_max)
            {
                fprintf(stderr,
                        "Error: historical draw %d has main number %d outside the game range "
                        "%d..%d.\n",
                        i + 1, n, info->main_min, info->main_max);
                return 0;
            }
            out->main_frequency[n] += 1.0;
        }
        for (int j = 0; j < draws[i].result.extra_count && info->extra_count > 0; j++)
        {
            int n = draws[i].result.extra_numbers[j];
            if (n < info->extra_min || n > info->extra_max)
            {
                fprintf(stderr,
                        "Error: historical draw %d has extra number %d outside the game range "
                        "%d..%d.\n",
                        i + 1, n, info->extra_min, info->extra_max);
                return 0;
            }
            out->extra_frequency[n] += 1.0;
        }
    }

    double main_norm = (double)draw_count * (double)info->main_count;
    if (main_norm <= 0.0)
        main_norm = 1.0;
    for (int n = info->main_min; n <= info->main_max; n++)
        out->main_frequency[n] /= main_norm;
    out->has_main_frequency = 1;

    if (info->extra_count > 0)
    {
        double extra_norm = (double)draw_count * (double)info->extra_count;
        if (extra_norm <= 0.0)
            extra_norm = 1.0;
        for (int n = info->extra_min; n <= info->extra_max; n++)
            out->extra_frequency[n] /= extra_norm;
        out->has_extra_frequency = 1;
    }
    return 1;
}

/* ---------------------------------------------------------
   CLOSEST-SEED JSON INPUT PARSING

   A deliberately small, whitespace-tolerant reader for the JSON written by
   print_closest_seed_json(). It understands string literals (so brackets inside
   strings never confuse nesting) but is not a general JSON parser.
   --------------------------------------------------------- */

/** @brief Skip JSON whitespace inside [p, end). */
static const char *json_skip_ws(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        p++;
    return p;
}

/** @brief Return pointer just past the string literal starting at @p p ('"'), or NULL. */
static const char *json_skip_string(const char *p, const char *end)
{
    if (p >= end || *p != '"')
        return NULL;
    for (p++; p < end; p++)
    {
        if (*p == '\\')
        {
            p++;
            continue;
        }
        if (*p == '"')
            return p + 1;
    }
    return NULL;
}

/**
 * @brief Return pointer to the bracket closing the array/object that opens at @p open.
 */
static const char *json_find_container_end(const char *open, const char *end)
{
    if (!open || open >= end || (*open != '[' && *open != '{'))
        return NULL;

    int depth = 0;
    for (const char *p = open; p < end; p++)
    {
        if (*p == '"')
        {
            p = json_skip_string(p, end);
            if (!p)
                return NULL;
            p--; /* loop increment moves past the closing quote */
        }
        else if (*p == '[' || *p == '{')
            depth++;
        else if (*p == ']' || *p == '}')
        {
            if (--depth == 0)
                return p;
        }
    }
    return NULL;
}

/**
 * @brief Find `"key"` followed by optional whitespace and ':' inside [start, end).
 * @return Pointer to the first non-whitespace character of the value, or NULL.
 */
static const char *json_find_value(const char *start, const char *end, const char *key)
{
    size_t key_len = strlen(key);
    for (const char *p = start; p && p < end; p++)
    {
        if (*p != '"')
            continue;
        const char *after = json_skip_string(p, end);
        if (!after)
            return NULL;
        if ((size_t)(after - p) == key_len + 2 && memcmp(p + 1, key, key_len) == 0)
        {
            const char *q = json_skip_ws(after, end);
            if (q < end && *q == ':')
                return json_skip_ws(q + 1, end);
        }
        p = after - 1;
    }
    return NULL;
}

/** @brief Parse a JSON integer (optional '-') value at @p p into @p out. */
static int json_parse_ll(const char *p, const char *end, long long *out, const char **out_next)
{
    if (!p || p >= end || !(isdigit((unsigned char)*p) || *p == '-'))
        return 0;
    char *stop = NULL;
    errno = 0;
    long long v = strtoll(p, &stop, 10);
    if (errno != 0 || stop == p || stop > end || *stop == '.' || *stop == 'e' || *stop == 'E')
        return 0;
    *out = v;
    if (out_next)
        *out_next = stop;
    return 1;
}

/**
 * @brief Parse one frequency array (objects with "number" and "frequency") from closest-seed JSON.
 *
 * Every number in [number_min, number_max] must appear exactly once with a finite frequency in
 * [0, 1]; anything else is an error.
 */
static int parse_frequency_array(const char *json, const char *json_end, const char *array_key,
                                 double *out_frequency, int number_min, int number_max)
{
    const char *array_start = json_find_value(json, json_end, array_key);
    if (!array_start || *array_start != '[')
    {
        fprintf(stderr, "Error: closest-seed input is missing the \"%s\" array.\n", array_key);
        return 0;
    }
    const char *array_end = json_find_container_end(array_start, json_end);
    if (!array_end)
    {
        fprintf(stderr, "Error: closest-seed input has an unterminated \"%s\" array.\n", array_key);
        return 0;
    }

    unsigned char seen[CLOSEST_SEED_FREQ_TABLE_SIZE] = {0};
    int entries = 0;
    const char *p = json_skip_ws(array_start + 1, array_end);
    while (p < array_end)
    {
        if (*p != '{')
        {
            fprintf(stderr, "Error: closest-seed input \"%s\" must contain only objects.\n",
                    array_key);
            return 0;
        }
        const char *obj_end = json_find_container_end(p, array_end);
        if (!obj_end)
        {
            fprintf(stderr, "Error: closest-seed input has a malformed \"%s\" entry.\n", array_key);
            return 0;
        }

        long long number = 0;
        const char *num_p = json_find_value(p, obj_end, "number");
        const char *freq_p = json_find_value(p, obj_end, "frequency");
        char *freq_stop = NULL;
        double freq = freq_p ? strtod(freq_p, &freq_stop) : 0.0;
        if (!json_parse_ll(num_p, obj_end, &number, NULL) || !freq_p || freq_stop == freq_p ||
            freq_stop > obj_end)
        {
            fprintf(stderr,
                    "Error: closest-seed input \"%s\" entry %d needs numeric \"number\" and "
                    "\"frequency\" fields.\n",
                    array_key, entries + 1);
            return 0;
        }
        if (number < number_min || number > number_max)
        {
            fprintf(stderr,
                    "Error: closest-seed input \"%s\" number %lld is outside the game range "
                    "%d..%d.\n",
                    array_key, number, number_min, number_max);
            return 0;
        }
        if (!isfinite(freq) || freq < 0.0 || freq > 1.0)
        {
            fprintf(stderr,
                    "Error: closest-seed input \"%s\" frequency for number %lld must be in "
                    "[0, 1].\n",
                    array_key, number);
            return 0;
        }
        if (seen[number])
        {
            fprintf(stderr, "Error: closest-seed input \"%s\" lists number %lld twice.\n",
                    array_key, number);
            return 0;
        }
        seen[number] = 1;
        out_frequency[number] = freq;
        entries++;

        p = json_skip_ws(obj_end + 1, array_end);
        if (p < array_end)
        {
            if (*p != ',')
            {
                fprintf(stderr, "Error: closest-seed input \"%s\" is not a valid JSON array.\n",
                        array_key);
                return 0;
            }
            p = json_skip_ws(p + 1, array_end);
        }
    }

    int expected = number_max - number_min + 1;
    if (entries != expected)
    {
        fprintf(stderr,
                "Error: closest-seed input \"%s\" has %d entries; expected %d (numbers %d..%d).\n",
                array_key, entries, expected, number_min, number_max);
        return 0;
    }
    return 1;
}

/** @brief Parse the top-level "game" string; returns 1 and fills @p out on success. */
static int parse_json_game_name(const char *json, const char *json_end, char *out, size_t out_size)
{
    const char *p = json_find_value(json, json_end, "game");
    const char *str_end = p ? json_skip_string(p, json_end) : NULL;
    if (!str_end)
        return 0;

    size_t n = 0;
    for (const char *c = p + 1; c < str_end - 1; c++)
    {
        char ch = *c;
        if (ch == '\\')
        {
            c++;
            switch (*c)
            {
            case 'n':
                ch = '\n';
                break;
            case 't':
                ch = '\t';
                break;
            case 'r':
                ch = '\r';
                break;
            case 'u': /* Non-ASCII escapes cannot match a plugin name we would print. */
                return 0;
            default:
                ch = *c; /* \" \\ \/ */
                break;
            }
        }
        if (n + 1 >= out_size)
            return 0;
        out[n++] = ch;
    }
    out[n] = '\0';
    return 1;
}

/** @brief Read a whole (<= 10 MiB) file into a NUL-terminated heap buffer. */
static char *read_small_text_file(const char *file_path, size_t *out_len)
{
    FILE *f = fopen(file_path, "rb");
    if (!f)
        return NULL;

    char *buf = NULL;
    long size = -1;
    if (fseek(f, 0, SEEK_END) == 0)
        size = ftell(f);
    if (size > 0 && size <= 10 * 1024 * 1024 && fseek(f, 0, SEEK_SET) == 0)
    {
        buf = calloc((size_t)size + 1, 1);
        if (buf && fread(buf, 1, (size_t)size, f) != (size_t)size)
        {
            free(buf);
            buf = NULL;
        }
    }
    fclose(f);
    if (buf && out_len)
        *out_len = (size_t)size;
    return buf;
}

/**
 * @brief Parse closest-seed JSON output for seeds and historical reference frequencies.
 *
 * Prints a specific error message on failure.
 */
static int parse_closest_seed_json_file(const char *file_path, const char *expected_game,
                                        const LotteryInfo *info, ClosestSeedReference *out)
{
    if (!file_path || !info || !out)
        return 0;

    memset(out, 0, sizeof(*out));

    size_t len = 0;
    char *json = read_small_text_file(file_path, &len);
    if (!json)
    {
        fprintf(stderr,
                "Error: Cannot read closest-seed input '%s' (missing, empty or > 10 MiB).\n",
                file_path);
        return 0;
    }
    const char *json_end = json + len;
    int ok = 0;

    char file_game[256];
    if (!parse_json_game_name(json, json_end, file_game, sizeof(file_game)))
    {
        fprintf(stderr, "Error: closest-seed input is missing a valid \"game\" field.\n");
        goto done;
    }
    if (expected_game && strcasecmp(file_game, expected_game) != 0)
    {
        fprintf(stderr,
                "Error: closest-seed input was generated for game '%s' but --game is '%s'.\n",
                file_game, expected_game);
        goto done;
    }

    long long historical_draw_count = 0;
    if (!json_parse_ll(json_find_value(json, json_end, "historical_draw_count"), json_end,
                       &historical_draw_count, NULL))
    {
        fprintf(stderr, "Error: closest-seed input is missing integer "
                        "\"reference.historical_draw_count\".\n");
        goto done;
    }
    if (historical_draw_count < 1 || historical_draw_count > CLOSEST_SEED_MAX_HISTORICAL_DRAWS)
    {
        fprintf(stderr,
                "Error: closest-seed input historical_draw_count %lld must be between 1 and %d.\n",
                historical_draw_count, CLOSEST_SEED_MAX_HISTORICAL_DRAWS);
        goto done;
    }
    out->historical_draw_count = (int)historical_draw_count;

    const char *top_start = json_find_value(json, json_end, "top_candidates");
    const char *top_end =
        (top_start && *top_start == '[') ? json_find_container_end(top_start, json_end) : NULL;
    if (!top_end)
    {
        fprintf(stderr, "Error: closest-seed input is missing the \"top_candidates\" array.\n");
        goto done;
    }

    int listed = 0;
    const char *p = json_skip_ws(top_start + 1, top_end);
    while (p < top_end)
    {
        const char *obj_end = (*p == '{') ? json_find_container_end(p, top_end) : NULL;
        const char *seed_p = obj_end ? json_find_value(p, obj_end, "seed") : NULL;
        char *stop = NULL;
        unsigned long long seed = 0;
        if (seed_p && isdigit((unsigned char)*seed_p))
        {
            errno = 0;
            seed = strtoull(seed_p, &stop, 10);
        }
        if (!seed_p || !stop || stop == seed_p || stop > obj_end || errno != 0 || *stop == '.' ||
            *stop == 'e' || *stop == 'E')
        {
            fprintf(stderr,
                    "Error: closest-seed input top_candidates entry %d needs a non-negative "
                    "integer \"seed\" (0..18446744073709551615).\n",
                    listed + 1);
            goto done;
        }
        if (out->seed_count < SEED_CALIBRATION_MAX_TOP_K)
            out->seeds[out->seed_count++] = (uint64_t)seed;
        listed++;

        p = json_skip_ws(obj_end + 1, top_end);
        if (p < top_end && *p == ',')
            p = json_skip_ws(p + 1, top_end);
        else if (p < top_end)
        {
            fprintf(stderr, "Error: closest-seed input \"top_candidates\" is not a valid array.\n");
            goto done;
        }
    }

    if (out->seed_count <= 0)
    {
        fprintf(stderr, "Error: closest-seed input \"top_candidates\" is empty.\n");
        goto done;
    }
    if (listed > out->seed_count)
    {
        fprintf(stderr, "Warning: closest-seed input lists %d seeds; only the first %d are used.\n",
                listed, out->seed_count);
    }

    if (!parse_frequency_array(json, json_end, "main_frequency", out->main_frequency,
                               info->main_min, info->main_max))
        goto done;
    out->has_main_frequency = 1;

    if (info->extra_count > 0)
    {
        if (!parse_frequency_array(json, json_end, "extra_frequency", out->extra_frequency,
                                   info->extra_min, info->extra_max))
            goto done;
        out->has_extra_frequency = 1;
    }

    ok = 1;

done:
    free(json);
    return ok;
}

/* ---------------------------------------------------------
   CLOSEST-SEED DRAW SYNTHESIS
   --------------------------------------------------------- */

/** @brief One candidate value for a draw position; lower score is better. */
typedef struct
{
    int value;
    int rank; /* 0-based rank of the first seed that produced the value */
    double score;
} PositionCandidate;

/**
 * @brief Candidates for --closest-seed-input: group the per-seed values at one position and score
 * each distinct value by |sample frequency across seeds - reference frequency|.
 */
static int build_sample_frequency_candidates(const int *values, int value_count,
                                             const double *reference_frequency,
                                             const unsigned char *used, PositionCandidate *out,
                                             FILE *trace)
{
    int counts[SEED_CALIBRATION_MAX_TOP_K];
    int n = 0;

    for (int i = 0; i < value_count; i++)
    {
        int idx = 0;
        while (idx < n && out[idx].value != values[i])
            idx++;
        if (idx == n)
        {
            out[n].value = values[i];
            out[n].rank = i;
            counts[n++] = 0;
        }
        counts[idx]++;
    }

    if (trace)
    {
        fprintf(trace, "  candidate-by-seed-rank:");
        for (int i = 0; i < value_count; i++)
            fprintf(trace, " #%d=%d", i + 1, values[i]);
        fputc('\n', trace);
    }

    for (int i = 0; i < n; i++)
    {
        double sample = (double)counts[i] / (double)value_count;
        double ref = reference_frequency[out[i].value];
        out[i].score = fabs(sample - ref);
        if (trace)
        {
            fprintf(trace,
                    "  value=%d count=%d sample=%.9f reference=%.9f delta=%.9f first_rank=%d%s\n",
                    out[i].value, counts[i], sample, ref, out[i].score, out[i].rank + 1,
                    used[out[i].value] ? " [already-used]" : "");
        }
    }
    return n;
}

/**
 * @brief Candidates for --next-draw: one per seed, scored by |seed period frequency - reference
 * frequency| of the number that seed produced.
 */
static int
build_period_score_candidates(const int *values, int value_count,
                              const double (*seed_frequency)[CLOSEST_SEED_FREQ_TABLE_SIZE],
                              const double *reference_frequency, double norm,
                              const unsigned char *used, PositionCandidate *out, FILE *trace)
{
    for (int s = 0; s < value_count; s++)
    {
        int v = values[s];
        double delta = seed_frequency[s][v] - reference_frequency[v];
        out[s].value = v;
        out[s].rank = s;
        out[s].score = fabs(delta);
        if (trace)
        {
            fprintf(trace,
                    "  rank=%d candidate=%d ref=%.9f seed=%.9f error=%.9f delta_count=%+lld%s\n",
                    s + 1, v, reference_frequency[v], seed_frequency[s][v], out[s].score,
                    (long long)llround(delta * norm), used[v] ? " [already-used]" : "");
        }
    }
    return value_count;
}

/**
 * @brief Choose the value for one position.
 *
 * Picks the unused candidate with the lowest score (tie-break: best seed rank). When every
 * candidate is already used in this draw, falls back deterministically to the unused number in
 * [number_min, number_max] with the highest reference frequency (tie-break: smallest number).
 *
 * @return The chosen value, or -1 if the range has no unused number left.
 */
static int select_position_value(const PositionCandidate *cands, int cand_count, int number_min,
                                 int number_max, const unsigned char *used,
                                 const double *reference_frequency, FILE *trace)
{
    int best = -1;
    for (int i = 0; i < cand_count; i++)
    {
        int v = cands[i].value;
        if (v < number_min || v > number_max || used[v])
            continue;
        if (best < 0 || cands[i].score < cands[best].score - 1e-15 ||
            (fabs(cands[i].score - cands[best].score) <= 1e-15 && cands[i].rank < cands[best].rank))
            best = i;
    }

    if (best >= 0)
    {
        if (trace)
            fprintf(trace,
                    "  selected=%d from seed-rank=%d (lowest frequency error=%.9f; "
                    "tie-break=seed rank)\n",
                    cands[best].value, cands[best].rank + 1, cands[best].score);
        return cands[best].value;
    }

    int fallback = -1;
    for (int v = number_min; v <= number_max; v++)
    {
        if (used[v])
            continue;
        if (fallback < 0 || reference_frequency[v] > reference_frequency[fallback] + 1e-15)
            fallback = v;
    }

    if (trace)
    {
        if (fallback >= 0)
            fprintf(trace,
                    "  selected=%d (fallback: all candidates already used; unused number with "
                    "highest reference frequency=%.9f, tie-break=smallest number)\n",
                    fallback, reference_frequency[fallback]);
        else
            fprintf(trace, "  selected=<none> (no unused number left in range)\n");
    }
    return fallback;
}

/**
 * @brief Synthesize one draw at @p draw_index from all closest-seed candidates.
 *
 * With @p period == NULL the --closest-seed-input strategy (sample frequency across seeds) is
 * used; otherwise the --next-draw strategy (per-seed period frequency error). Numbers are unique
 * within the main group and within the extra group.
 */
static int synthesize_closest_seed_draw(const ClosestSeedReference *closest_ref,
                                        const LotteryInfo *info, int draw_index,
                                        const ClosestSeedPeriodFrequencies *period,
                                        LotteryResult *out_result, FILE *trace)
{
    if (!closest_ref || !info || !out_result || closest_ref->seed_count <= 0)
        return 0;

    LotteryResult per_seed[SEED_CALIBRATION_MAX_TOP_K];
    memset(out_result, 0, sizeof(*out_result));

    for (int s = 0; s < closest_ref->seed_count; s++)
    {
        generate_draw_seeded(info->main_count, info->main_min, info->main_max, info->extra_count,
                             info->extra_min, info->extra_max,
                             derive_draw_seed(closest_ref->seeds[s], draw_index), &per_seed[s],
                             silent_callback);
    }

    if (trace)
    {
        for (int s = 0; s < closest_ref->seed_count; s++)
        {
            fprintf(trace, "  Seed rank #%d seed=%llu -> Main:", s + 1,
                    (unsigned long long)closest_ref->seeds[s]);
            for (int j = 0; j < per_seed[s].main_count; j++)
                fprintf(trace, " %d", per_seed[s].main_numbers[j]);
            if (per_seed[s].extra_count > 0)
            {
                fprintf(trace, " | Extra:");
                for (int j = 0; j < per_seed[s].extra_count; j++)
                    fprintf(trace, " %d", per_seed[s].extra_numbers[j]);
            }
            fputc('\n', trace);
        }
    }

    out_result->main_count = info->main_count;
    out_result->extra_count = info->extra_count;

    for (int group = 0; group < 2; group++)
    {
        const int is_extra = (group == 1);
        const int count = is_extra ? info->extra_count : info->main_count;
        const int number_min = is_extra ? info->extra_min : info->main_min;
        const int number_max = is_extra ? info->extra_max : info->main_max;
        const double *reference =
            is_extra ? closest_ref->extra_frequency : closest_ref->main_frequency;
        const double norm = (double)closest_ref->historical_draw_count * (double)count;
        int *out_numbers = is_extra ? out_result->extra_numbers : out_result->main_numbers;
        unsigned char used[CLOSEST_SEED_FREQ_TABLE_SIZE] = {0};

        for (int p = 0; p < count; p++)
        {
            int values[SEED_CALIBRATION_MAX_TOP_K];
            PositionCandidate cands[SEED_CALIBRATION_MAX_TOP_K];
            int cand_count = 0;

            for (int s = 0; s < closest_ref->seed_count; s++)
            {
                values[s] = is_extra ? per_seed[s].extra_numbers[p] : per_seed[s].main_numbers[p];
                if (values[s] < number_min || values[s] > number_max)
                    return 0; /* generator contract violated */
            }

            if (trace)
                fprintf(trace, "Trace %s position %d\n", is_extra ? "extra" : "main", p + 1);

            if (period)
                cand_count = build_period_score_candidates(
                    values, closest_ref->seed_count,
                    (const double (*)[CLOSEST_SEED_FREQ_TABLE_SIZE])(
                        is_extra ? period->extra_frequency : period->main_frequency),
                    reference, norm, used, cands, trace);
            else
                cand_count = build_sample_frequency_candidates(values, closest_ref->seed_count,
                                                               reference, used, cands, trace);

            int chosen = select_position_value(cands, cand_count, number_min, number_max, used,
                                               reference, trace);
            if (chosen < 0)
                return 0;
            out_numbers[p] = chosen;
            used[chosen] = 1;
        }
    }

    return 1;
}

/**
 * @brief Compute one seed's normalized number frequencies over period draws [0..N-1].
 */
static void compute_seed_period_frequency(const ClosestSeedReference *closest_ref,
                                          const LotteryInfo *info, uint64_t seed,
                                          double *main_frequency, double *extra_frequency)
{
    memset(main_frequency, 0, sizeof(double) * CLOSEST_SEED_FREQ_TABLE_SIZE);
    memset(extra_frequency, 0, sizeof(double) * CLOSEST_SEED_FREQ_TABLE_SIZE);

    for (int i = 0; i < closest_ref->historical_draw_count; i++)
    {
        LotteryResult draw;
        generate_draw_seeded(info->main_count, info->main_min, info->main_max, info->extra_count,
                             info->extra_min, info->extra_max, derive_draw_seed(seed, i), &draw,
                             silent_callback);

        for (int j = 0; j < draw.main_count; j++)
            if (draw.main_numbers[j] >= info->main_min && draw.main_numbers[j] <= info->main_max)
                main_frequency[draw.main_numbers[j]] += 1.0;
        for (int j = 0; j < draw.extra_count; j++)
            if (draw.extra_numbers[j] >= info->extra_min &&
                draw.extra_numbers[j] <= info->extra_max)
                extra_frequency[draw.extra_numbers[j]] += 1.0;
    }

    double main_norm = (double)closest_ref->historical_draw_count * (double)info->main_count;
    if (main_norm > 0.0)
        for (int n = info->main_min; n <= info->main_max; n++)
            main_frequency[n] /= main_norm;

    if (info->extra_count > 0)
    {
        double extra_norm = (double)closest_ref->historical_draw_count * (double)info->extra_count;
        if (extra_norm > 0.0)
            for (int n = info->extra_min; n <= info->extra_max; n++)
                extra_frequency[n] /= extra_norm;
    }
}

/** @brief Build per-seed normalized frequencies over period draws [0..N-1] for every seed. */
static int build_seed_period_frequencies(const ClosestSeedReference *closest_ref,
                                         const LotteryInfo *info, ClosestSeedPeriodFrequencies *out)
{
    if (!closest_ref || !info || !out || closest_ref->historical_draw_count <= 0)
        return 0;

    for (int s = 0; s < closest_ref->seed_count; s++)
        compute_seed_period_frequency(closest_ref, info, closest_ref->seeds[s],
                                      out->main_frequency[s], out->extra_frequency[s]);
    return 1;
}

/** @brief Compute normalized L1 distance between simulated and reference frequencies. */
static double compute_frequency_l1_distance(const double *sim_frequency,
                                            const double *ref_frequency, int number_min,
                                            int number_max)
{
    if (!sim_frequency || number_min > number_max)
        return 0.0;

    double sum = 0.0;
    int count = 0;
    for (int n = number_min; n <= number_max; n++)
    {
        double ref = ref_frequency ? ref_frequency[n] : 0.0;
        sum += fabs(sim_frequency[n] - ref);
        count++;
    }

    return (count > 0) ? (sum / (double)count) : 0.0;
}

/** @brief Compute per-seed period-frequency diagnostics against reference frequencies. */
static int compute_seed_period_frequency_diagnostics(const ClosestSeedReference *closest_ref,
                                                     const LotteryInfo *info, uint64_t seed,
                                                     double *main_l1, double *extra_l1,
                                                     double *combined_l1)
{
    if (!closest_ref || !info || closest_ref->historical_draw_count <= 0)
        return 0;

    double main_frequency[CLOSEST_SEED_FREQ_TABLE_SIZE];
    double extra_frequency[CLOSEST_SEED_FREQ_TABLE_SIZE];
    compute_seed_period_frequency(closest_ref, info, seed, main_frequency, extra_frequency);

    double local_main_l1 = compute_frequency_l1_distance(
        main_frequency, closest_ref->main_frequency, info->main_min, info->main_max);
    double local_extra_l1 = 0.0;
    if (info->extra_count > 0)
        local_extra_l1 = compute_frequency_l1_distance(
            extra_frequency, closest_ref->extra_frequency, info->extra_min, info->extra_max);

    if (main_l1)
        *main_l1 = local_main_l1;
    if (extra_l1)
        *extra_l1 = local_extra_l1;
    if (combined_l1)
        *combined_l1 =
            (info->extra_count > 0) ? 0.5 * (local_main_l1 + local_extra_l1) : local_main_l1;

    return 1;
}

#define CLOSEST_SEED_PROGRESS_BAR_WIDTH 32

typedef struct
{
    int enabled;
    int printed_lines;
} ClosestSeedProgressState;

/** @brief Check whether stderr likely supports ANSI redraw sequences. */
static int closest_seed_terminal_supports_ansi_redraw(void)
{
    if (!isatty(fileno(stderr)))
        return 0;

    const char *term = getenv("TERM");
    if (!term || term[0] == '\0')
        return 0;
    if (strcasecmp(term, "dumb") == 0)
        return 0;
    return 1;
}

/** @brief Draw fixed-width progress bar cells to stderr. */
static void draw_progress_bar_cells(int done, int total)
{
    int filled = 0;
    if (total > 0)
    {
        filled = (int)((double)done / (double)total * CLOSEST_SEED_PROGRESS_BAR_WIDTH);
        if (filled > CLOSEST_SEED_PROGRESS_BAR_WIDTH)
            filled = CLOSEST_SEED_PROGRESS_BAR_WIDTH;
    }

    for (int i = 0; i < CLOSEST_SEED_PROGRESS_BAR_WIDTH; i++)
    {
        if (i < filled - 1)
            fputc('=', stderr);
        else if (i == filled - 1)
            fputc('>', stderr);
        else
            fputc(' ', stderr);
    }
}

/**
 * @brief Multi-worker progress callback for closest-seed calibration mode.
 */
static void closest_seed_progress_callback(void *ctx, int done, int total, int workers,
                                           const int *worker_done,
                                           const int *worker_utilization_pct)
{
    ClosestSeedProgressState *state = (ClosestSeedProgressState *)ctx;
    if (!state || !state->enabled || total <= 0)
        return;

    if (workers < 1)
        workers = 1;

    if (state->printed_lines > 0)
        fprintf(stderr, "\033[%dF", state->printed_lines);

    int pct = (int)((double)done / (double)total * 100.0);
    if (pct > 100)
        pct = 100;

    fprintf(stderr, "\033[2K  Closest-seed total   [");
    draw_progress_bar_cells(done, total);
    fprintf(stderr, "] %d/%d (%d%%)\n", done, total, pct);

    for (int w = 0; w < workers; w++)
    {
        int worker_count = worker_done ? worker_done[w] : 0;
        if (worker_count < 0)
            worker_count = 0;
        if (worker_count > total)
            worker_count = total;

        int cpu_load_pct = worker_utilization_pct ? worker_utilization_pct[w] : 0;
        if (cpu_load_pct < 0)
            cpu_load_pct = 0;
        if (cpu_load_pct > 100)
            cpu_load_pct = 100;

        int worker_pct = (int)((double)worker_count / (double)total * 100.0);
        if (worker_pct > 100)
            worker_pct = 100;

        fprintf(stderr, "\033[2K  CPU %-2d  load:%3d%%   [", w + 1, cpu_load_pct);
        draw_progress_bar_cells(worker_count, total);
        fprintf(stderr, "] %d/%d (%d%%)\n", worker_count, total, worker_pct);
    }

    state->printed_lines = workers + 1;
    if (done >= total)
    {
        state->printed_lines = 0;
        fputc('\n', stderr);
    }

    fflush(stderr);
}

/** @brief Print simulation analytics summary in table form. */
static void print_simulation_analytics_table(const SimulationAnalyticsCoreReport *core,
                                             const SimulationAnalyticsAdvancedReport *advanced)
{
    printf("Simulation analytics summary\n");
    printf("draws,%d\n", core->draw_count);
    printf("range,%d-%d\n", core->number_min, core->number_max);
    printf("total_hits,%d\n", core->total_hits);
    printf("mean_hits_per_number,%.6f\n", core->mean_hits_per_number);
    printf("variance_hits_per_number,%.6f\n", core->variance_hits_per_number);
    printf("entropy_normalized,%.6f\n", advanced->entropy_normalized);
    printf("\nTop Hot Numbers\n");
    printf("rank,number,count,percentage\n");
    for (int i = 0; i < advanced->top_n; i++)
    {
        printf("%d,%d,%d,%.6f\n", i + 1, advanced->hot[i].number, advanced->hot[i].count,
               advanced->hot[i].percentage);
    }
    printf("\nTop Cold Numbers\n");
    printf("rank,number,count,percentage\n");
    for (int i = 0; i < advanced->top_n; i++)
    {
        printf("%d,%d,%d,%.6f\n", i + 1, advanced->cold[i].number, advanced->cold[i].count,
               advanced->cold[i].percentage);
    }
}

/** @brief Print simulation analytics summary as JSON. */
static void print_simulation_analytics_json(const SimulationAnalyticsMetadata *metadata,
                                            const SimulationAnalyticsCoreReport *core,
                                            const SimulationAnalyticsAdvancedReport *advanced)
{
    char metadata_json[768];
    if (simulation_analytics_metadata_to_json(metadata, metadata_json, sizeof(metadata_json)) != 0)
    {
        snprintf(metadata_json, sizeof(metadata_json),
                 "{\"schema_version\":\"%s\",\"generated_at\":\"1970-01-01T00:00:00Z\","
                 "\"game\":\"unknown\",\"source\":\"simulation\",\"run_count\":0}",
                 SIM_ANALYTICS_SCHEMA_VERSION);
    }

    printf("{\n");
    printf("  \"schema_version\": \"%s\",\n", SIM_ANALYTICS_SCHEMA_VERSION);
    printf("  \"metadata\": %s,\n", metadata_json);
    printf("  \"core\": {\"draw_count\": %d, \"number_min\": %d, \"number_max\": %d, "
           "\"total_hits\": %d, \"mean_hits_per_number\": %.6f, "
           "\"variance_hits_per_number\": %.6f},\n",
           core->draw_count, core->number_min, core->number_max, core->total_hits,
           core->mean_hits_per_number, core->variance_hits_per_number);
    printf("  \"advanced\": {\"entropy_normalized\": %.6f, \"hot\": [",
           advanced->entropy_normalized);
    for (int i = 0; i < advanced->top_n; i++)
    {
        printf("{\"number\":%d,\"count\":%d,\"percentage\":%.6f}%s", advanced->hot[i].number,
               advanced->hot[i].count, advanced->hot[i].percentage,
               (i + 1 == advanced->top_n) ? "" : ",");
    }
    printf("], \"cold\": [");
    for (int i = 0; i < advanced->top_n; i++)
    {
        printf("{\"number\":%d,\"count\":%d,\"percentage\":%.6f}%s", advanced->cold[i].number,
               advanced->cold[i].count, advanced->cold[i].percentage,
               (i + 1 == advanced->top_n) ? "" : ",");
    }
    printf("]}\n");
    printf("}\n");
}

/** @brief Print simulation analytics summary as CSV. */
static void print_simulation_analytics_csv(const SimulationAnalyticsCoreReport *core,
                                           const SimulationAnalyticsAdvancedReport *advanced)
{
    printf("section,key,value,extra\n");
    printf("core,draw_count,%d,\n", core->draw_count);
    printf("core,number_min,%d,\n", core->number_min);
    printf("core,number_max,%d,\n", core->number_max);
    printf("core,total_hits,%d,\n", core->total_hits);
    printf("core,mean_hits_per_number,%.6f,\n", core->mean_hits_per_number);
    printf("core,variance_hits_per_number,%.6f,\n", core->variance_hits_per_number);
    printf("advanced,entropy_normalized,%.6f,\n", advanced->entropy_normalized);
    for (int i = 0; i < advanced->top_n; i++)
    {
        printf("hot,number,%d,%d|%.6f\n", advanced->hot[i].number, advanced->hot[i].count,
               advanced->hot[i].percentage);
        printf("cold,number,%d,%d|%.6f\n", advanced->cold[i].number, advanced->cold[i].count,
               advanced->cold[i].percentage);
    }
}

/* ---------------------------------------------------------
   Usage
   --------------------------------------------------------- */
/**
 * @brief Print full CLI help/usage text.
 */
static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s --game NAME [--draws N] [--animate] [--gui [2D|3D]] [--reload-plugin] [--verbose "
            "LEVEL]\n"
            "  %s --game NAME [--draws N] [--export csv|json] [--output FILE] [--reload-plugin] "
            "[--verbose LEVEL]\n"
            "  %s --game NAME --database-gewinnzahlen update\n"
            "  %s --game NAME --validate-only\n"
            "  %s --game NAME --download-url set URL\n"
            "  %s --download-config NAME set VALUE\n"
            "  %s --list-games\n"
            "\n"
            "Modes:\n"
            "  --animate              Animated CLI draw display (spinner animation)\n"
            "  --gui [2D|3D]          Graphical mode (default: 2D SDL2, or 3D OpenGL)\n"
            "  --dark-mode <mode>     Dark mode theme: on, off, or auto (default: auto)\n"
            "  --debug-overlay        Show FPS/physics HUD in 3D GUI (requires --gui 3D)\n"
            "  --export FORMAT        Export results to file (csv or json)\n"
            "  --database-gewinnzahlen update\n"
            "                         Sync local DB with latest official draw (per game)\n"
            "  --download-url set URL\n"
            "                         Set game source URL in sources config (requires --game)\n"
            "  --download-config NAME set VALUE\n"
            "                         Persist a download tuning value in download config\n"
            "  --reload-plugin        Reload the selected plugin from disk before running\n"
            "  --validate-only        Validate configuration without running\n"
            "  --simulation-analytics  Compute analytics over simulated draws\n"
            "\n"
            "Output Options:\n"
            "  --output FILE     Destination file for --export (required with --export)\n"
            "  --draws N         Number of draws (default: 1)\n"
            "  --sorted          Sort drawn numbers from lowest to highest (non-GUI only)\n"
            "  --seed VALUE      Deterministic seed (decimal or 0x-prefixed hex)\n"
            "  --verbose LEVEL   Log level: ERROR, WARN, INFO, DEBUG (default: INFO)\n"
            "\n"
            "Analytics Period Options:\n"
            "  --from YYYY-MM-DD Inclusive period start date for analytics APIs\n"
            "  --to YYYY-MM-DD   Inclusive period end date for analytics APIs\n"
            "  --frequency-distribution Print frequency distribution over historical data\n"
            "  --analytics-barometer   Print overdue barometer over historical data\n"
            "  --analytics-hot-cold    Print hot/cold number rankings over historical data\n"
            "  --draw-count-period     Return number of historical draws in [--from, --to]\n"
            "  --top N                 Number of hot/cold entries (default: 10)\n"
            "  --explain               Show formulas/assumptions for analytics outputs\n"
            "  --format FORMAT         Analytics output format: table, json, csv\n"
            "  --historical-csv FILE   Historical draw CSV override (simulation/dev datasets)\n"
            "                         By default analytics read local real-data DB snapshot\n"
            "  --sim-historical-csv FILE\n"
            "                         Export simulated draws as historical-format CSV (requires\n"
            "                         --from DATE as the synthetic start date). The output can\n"
            "                         then be passed to --historical-csv for 2D/3D GUI analytics\n"
            "\n"
            "Log Levels:\n"
            "  ERROR, WARN, INFO (default), DEBUG\n"
            "\n"
            "Config File (~/.lottorc):\n"
            "  Persistent defaults for any option. CLI arguments always override config.\n"
            "  Example ~/.lottorc:\n"
            "    [defaults]\n"
            "    game = Lotto 6aus49\n"
            "    draws = 10\n"
            "    verbose = INFO\n"
            "\n"
            "Download Config Files:\n"
            "  ~/.config/open-lotto/sources.conf   Upstream source URLs per game\n"
            "  ~/.config/open-lotto/download.conf  Persistent download tuning values\n"
            "\n",
            prog, prog, prog, prog, prog, prog, prog);

    fprintf(stderr,
            "Examples:\n"
            "  %s --list-games\n"
            "  %s --game \"Lotto 6aus49\" --database-gewinnzahlen update\n"
            "  %s --game \"Lotto 6aus49\"\n"
            "  %s --game \"Lotto 6aus49\" --draws 10\n"
            "  %s --game \"Lotto 6aus49\" --draws 10 --seed 0x1234abcd\n"
            "  %s --game \"Lotto 6aus49\" --animate\n"
            "  %s --game \"Lotto 6aus49\" --gui 3D\n"
            "  %s --game \"Lotto 6aus49\" --draws 100 --export csv --output results.csv\n"
            "  %s --game \"Lotto 6aus49\" --validate-only\n"
            "  %s --game \"Lotto 6aus49\" --verbose DEBUG\n"
            "  %s --game \"Eurojackpot\" --database-gewinnzahlen update\n"
            "  %s --game \"Lotto 6aus49\" --download-url set https://example/api\n"
            "  %s --download-config \"#sym:HISTORICAL_DB_DOWNLOAD_WORKERS_DEFAULT\" set 10\n"
            "  %s --game \"Lotto 6aus49\" --draws 10000 --simulation-analytics --format json\n"
            "  %s --game \"Lotto 6aus49\" --draw-count-period --from 2026-01-01 --to 2026-07-01\n"
            "  %s --game \"Lotto 6aus49\" --next-draw closest-10.json --sorted\n"
            "\n"
            "Environment Variables:\n"
            "  OPEN_LOTTO_PLUGIN_PATH  Custom plugin directory path\n"
            "  OPEN_LOTTO_LANG         CLI locale (en, fr; fallback to en)\n"
            "  OPEN_LOTTO_GEWINNZAHLEN_URL_EUROJACKPOT  Override upstream sync endpoint\n"
            "  OPEN_LOTTO_SOURCES_CONFIG               Override sources config path\n"
            "  OPEN_LOTTO_DOWNLOAD_CONFIG              Override download config path\n",
            prog, prog, prog, prog, prog, prog, prog, prog, prog, prog, prog, prog, prog, prog,
            prog, prog);

    fprintf(stderr,
            "\nClosest-Seed Mode:\n"
            "  --closest-seed          Find best-fit simulator seed for one fixed period\n"
            "                         Uses local real-data DB snapshot by default\n"
            "  --seed-start VALUE      Start of seed search range (required)\n"
            "  --seed-end VALUE        End of seed search range (required)\n"
            "  --seed-count N          Evaluate N unique sampled seeds (alternative mode)\n"
            "  --sample-seed VALUE     RNG seed for reproducible unique sampling\n"
            "  --max-evals N           Max seed evaluations (default: 100000)\n"
            "  --threads N             Worker threads for closest-seed search (default: max CPUs)\n"
            "  --output-file FILE      Write closest-seed output directly to FILE\n"
            "  --next-draw FILE        Read closest-seed JSON and synthesize exactly draw N+1\n"
            "                         (always traces to stderr; no --draws/--format/--trace-draw)\n"
            "  --closest-seed-input FILE\n"
            "                         Read closest-seed JSON and synthesize next draw(s)\n"
            "  --trace-draw N          With --closest-seed-input: trace only synthesized draw N\n"
            "  --timeout-ms N          Soft time budget hint in milliseconds (reserved)\n"
            "\n"
            "Closest-Seed Example:\n"
            "  %s --game \"Lotto 6aus49\" --closest-seed --from 2026-01-01 --to 2026-06-30 "
            "--seed-start 0 --seed-end 100000 --format json\n",
            prog);

    fprintf(stderr,
            "  %s --game \"Lotto 6aus49\" --closest-seed --from 2026-01-01 --to 2026-06-30 "
            "--seed-count 100000 --sample-seed 0x1234 --format json\n",
            prog);
}

/** @brief Trim leading and trailing ASCII whitespace in-place. */
static void trim_whitespace(char *s)
{
    if (!s)
        return;

    size_t start = 0;
    while (s[start] && isspace((unsigned char)s[start]))
        start++;

    size_t end = strlen(s);
    while (end > start && isspace((unsigned char)s[end - 1]))
        end--;

    if (start > 0)
        memmove(s, s + start, end - start);
    s[end - start] = '\0';
}

/** @brief Create a directory tree if it does not already exist. */
static int ensure_directory_recursive(const char *dir_path)
{
    if (!dir_path || dir_path[0] == '\0')
        return -1;

    char tmp[PATH_MAX];
    int n = snprintf(tmp, sizeof(tmp), "%s", dir_path);
    if (n <= 0 || (size_t)n >= sizeof(tmp))
        return -1;

    for (char *p = tmp + 1; *p; ++p)
    {
        if (*p != '/')
            continue;

        *p = '\0';
#ifdef _WIN32
        if (_mkdir(tmp) != 0 && errno != EEXIST)
            return -1;
#else
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
            return -1;
#endif
        *p = '/';
    }

#ifdef _WIN32
    if (_mkdir(tmp) != 0 && errno != EEXIST)
        return -1;
#else
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return -1;
#endif
    return 0;
}

/** @brief Ensure the parent directory of a file path exists. */
static int ensure_parent_directory(const char *file_path)
{
    if (!file_path)
        return -1;

    char dir[PATH_MAX];
    int n = snprintf(dir, sizeof(dir), "%s", file_path);
    if (n <= 0 || (size_t)n >= sizeof(dir))
        return -1;

    char *slash = strrchr(dir, '/');
    if (!slash)
        return 0;

    *slash = '\0';
    if (dir[0] == '\0')
        return 0;

    return ensure_directory_recursive(dir);
}

/** @brief Resolve effective sources config path for CLI operations. */
static int get_sources_config_path_cli(char *out, size_t out_size)
{
    const char *override = getenv("OPEN_LOTTO_SOURCES_CONFIG");
    if (override && override[0] != '\0')
    {
        int n = snprintf(out, out_size, "%s", override);
        return (n > 0 && (size_t)n < out_size) ? 0 : -1;
    }

    const char *home = getenv("HOME");
    if (!home || home[0] == '\0')
        return -1;

    int n = snprintf(out, out_size, "%s/.config/open-lotto/sources.conf", home);
    return (n > 0 && (size_t)n < out_size) ? 0 : -1;
}

/** @brief Resolve effective download config path for CLI operations. */
static int get_download_config_path_cli(char *out, size_t out_size)
{
    const char *override = getenv("OPEN_LOTTO_DOWNLOAD_CONFIG");
    if (override && override[0] != '\0')
    {
        int n = snprintf(out, out_size, "%s", override);
        return (n > 0 && (size_t)n < out_size) ? 0 : -1;
    }

    const char *home = getenv("HOME");
    if (!home || home[0] == '\0')
        return -1;

    int n = snprintf(out, out_size, "%s/.config/open-lotto/download.conf", home);
    return (n > 0 && (size_t)n < out_size) ? 0 : -1;
}

/** @brief Parse an INI section header line into section name. */
static int parse_section_header(const char *line, char *section, size_t section_size)
{
    if (!line || line[0] != '[')
        return 0;

    const char *end = strchr(line, ']');
    if (!end || end <= line + 1)
        return 0;

    size_t len = (size_t)(end - (line + 1));
    if (len >= section_size)
        return 0;

    memcpy(section, line + 1, len);
    section[len] = '\0';
    trim_whitespace(section);
    return 1;
}

/** @brief Parse an INI-style key=value line in-place. */
static int parse_key_value(char *line, char **key_out, char **value_out)
{
    char *eq = strchr(line, '=');
    if (!eq)
        return 0;

    *eq = '\0';
    char *key = line;
    char *value = eq + 1;
    trim_whitespace(key);
    trim_whitespace(value);
    if (key[0] == '\0')
        return 0;

    *key_out = key;
    *value_out = value;
    return 1;
}

/**
 * @brief Set or create one INI value in a config file atomically enough for CLI use.
 */
static int ini_set_value(const char *path, const char *target_section, const char *target_key,
                         const char *new_value)
{
    if (!path || !target_section || !target_key || !new_value)
        return -1;

    if (ensure_parent_directory(path) != 0)
        return -1;

    char temp_path[PATH_MAX];
    int n = snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
    if (n <= 0 || (size_t)n >= sizeof(temp_path))
        return -1;

    FILE *in = fopen(path, "r");
    FILE *out = fopen(temp_path, "w");
    if (!out)
    {
        if (in)
            fclose(in);
        return -1;
    }

    int found_section = 0;
    int wrote_key = 0;

    if (in)
    {
        int in_target_section = 0;
        char line[1024];
        while (fgets(line, sizeof(line), in))
        {
            char line_copy[1024];
            int copied = snprintf(line_copy, sizeof(line_copy), "%s", line);
            if (copied <= 0 || (size_t)copied >= sizeof(line_copy))
            {
                fclose(in);
                fclose(out);
                remove(temp_path);
                return -1;
            }

            trim_whitespace(line_copy);

            char section[256];
            if (parse_section_header(line_copy, section, sizeof(section)))
            {
                if (in_target_section && !wrote_key)
                {
                    fprintf(out, "%s = %s\n", target_key, new_value);
                    wrote_key = 1;
                }

                in_target_section = (strcasecmp(section, target_section) == 0);
                if (in_target_section)
                    found_section = 1;

                fputs(line, out);
                continue;
            }

            if (in_target_section && line_copy[0] != '\0' && line_copy[0] != '#' &&
                line_copy[0] != ';')
            {
                char parsed[1024];
                snprintf(parsed, sizeof(parsed), "%s", line_copy);
                char *key = NULL;
                char *value = NULL;
                if (parse_key_value(parsed, &key, &value) && strcasecmp(key, target_key) == 0)
                {
                    fprintf(out, "%s = %s\n", target_key, new_value);
                    wrote_key = 1;
                    continue;
                }
            }

            fputs(line, out);
        }

        fclose(in);
    }

    if (found_section)
    {
        if (!wrote_key)
            fprintf(out, "%s = %s\n", target_key, new_value);
    }
    else
    {
        fprintf(out, "[%s]\n%s = %s\n", target_section, target_key, new_value);
    }

    if (fclose(out) != 0)
    {
        remove(temp_path);
        return -1;
    }

    if (rename(temp_path, path) != 0)
    {
        remove(temp_path);
        return -1;
    }

    return 0;
}

/** @brief Map display game name to sources.conf key. */
static const char *game_source_key_cli(const char *game_name)
{
    if (!game_name)
        return NULL;

    if (strcasecmp(game_name, "Eurojackpot") == 0)
        return "eurojackpot";

    if (strcasecmp(game_name, "Lotto 6aus49") == 0 || strcasecmp(game_name, "Lotto") == 0)
        return "lotto";

    return NULL;
}

/** @brief Normalize symbolic download-config names to canonical config keys. */
static int map_download_config_key(const char *raw_name, const char **out_key)
{
    if (!raw_name || !out_key)
        return 0;

    const char *name = raw_name;
    if (strncmp(name, "#sym:", 5) == 0)
        name += 5;

    if (strcasecmp(name, "workers") == 0 ||
        strcasecmp(name, "HISTORICAL_DB_DOWNLOAD_WORKERS_DEFAULT") == 0 ||
        strcasecmp(name, "OPEN_LOTTO_HIST_DOWNLOAD_WORKERS") == 0)
    {
        *out_key = "workers";
        return 1;
    }
    if (strcasecmp(name, "fetch_timeout_sec") == 0 ||
        strcasecmp(name, "HISTORICAL_DB_FETCH_TIMEOUT_SEC_DEFAULT") == 0 ||
        strcasecmp(name, "OPEN_LOTTO_HIST_FETCH_TIMEOUT_SEC") == 0)
    {
        *out_key = "fetch_timeout_sec";
        return 1;
    }
    if (strcasecmp(name, "draw_timeout_sec") == 0 ||
        strcasecmp(name, "HISTORICAL_DB_DRAW_TIMEOUT_SEC_DEFAULT") == 0 ||
        strcasecmp(name, "OPEN_LOTTO_HIST_DRAW_TIMEOUT_SEC") == 0)
    {
        *out_key = "draw_timeout_sec";
        return 1;
    }
    if (strcasecmp(name, "max_retry_attempts") == 0 ||
        strcasecmp(name, "HISTORICAL_DB_MAX_RETRY_ATTEMPTS_DEFAULT") == 0 ||
        strcasecmp(name, "OPEN_LOTTO_HIST_MAX_RETRY_ATTEMPTS") == 0)
    {
        *out_key = "max_retry_attempts";
        return 1;
    }
    if (strcasecmp(name, "retry_base_delay_ms") == 0 ||
        strcasecmp(name, "HISTORICAL_DB_RETRY_BASE_DELAY_MS_DEFAULT") == 0 ||
        strcasecmp(name, "OPEN_LOTTO_HIST_RETRY_BASE_DELAY_MS") == 0)
    {
        *out_key = "retry_base_delay_ms";
        return 1;
    }
    if (strcasecmp(name, "retry_max_delay_ms") == 0 ||
        strcasecmp(name, "HISTORICAL_DB_RETRY_MAX_DELAY_MS_DEFAULT") == 0 ||
        strcasecmp(name, "OPEN_LOTTO_HIST_RETRY_MAX_DELAY_MS") == 0)
    {
        *out_key = "retry_max_delay_ms";
        return 1;
    }
    if (strcasecmp(name, "max_fetch_draws") == 0 ||
        strcasecmp(name, "OPEN_LOTTO_HIST_MAX_FETCH_DRAWS") == 0)
    {
        *out_key = "max_fetch_draws";
        return 1;
    }

    return 0;
}

/* ---------------------------------------------------------
   MAIN
   --------------------------------------------------------- */
/**
 * @brief Main CLI entry point.
 */
int main(int argc, char **argv)
{
    /* Load config file early so defaults are available */
    LoCalConfig cfg;
    config_load_lottorc(&cfg);
    g_cli_locale = localization_detect_locale();

    /* Handle help command */
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0))
    {
        print_usage(argv[0]);
        config_free(&cfg);
        return 0;
    }

    /* Handle --list-games command */
    if (argc >= 2 && strcmp(argv[1], "--list-games") == 0)
    {
        PluginRegistry *registry = registry_create();
        if (!registry)
        {
            log_error("Failed to create plugin registry");
            config_free(&cfg);
            return 1;
        }
        registry_discover_plugins(registry);
        registry_list_games(registry);
        registry_destroy(registry);
        config_free(&cfg);
        return 0;
    }

    /* Variables for parsed CLI options; use sentinels to track what was set */
    const char *game_name = NULL; /* NULL = not set by CLI */
    int cli_draws = -1;           /* -1 = not set by CLI */
    int animate = 0;
    int gui = 0;
    const char *gui_mode = NULL;
    int sorted_numbers = 0;
    int debug_overlay = 0;
#if !OPEN_LOTTO_ENABLE_OPENGL
    (void)debug_overlay;
#endif
    int dark_mode = -1; /* -1 = auto (detect from system), 0 = off, 1 = on */
    int cli_log_level_set = 0;
    LogLevel log_level = LOG_INFO;
    const char *export_format = NULL;
    const char *export_filename = NULL;
    int validate_only = 0;
    int reload_plugin = 0;
    const char *database_gewinnzahlen_cmd = NULL;
    const char *download_url_cmd = NULL;
    const char *download_url_value = NULL;
    const char *download_config_name = NULL;
    const char *download_config_cmd = NULL;
    const char *download_config_value = NULL;
    int use_seed = 0;
    uint64_t seed_value = 0;
    const char *period_from = NULL;
    const char *period_to = NULL;
    int analytics_frequency = 0;
    int analytics_barometer = 0;
    int analytics_hot_cold = 0;
    int simulation_analytics = 0;
    int period_draw_count_mode = 0;
    int closest_seed_mode = 0;
    uint64_t closest_seed_start = 0;
    uint64_t closest_seed_end = 0;
    int closest_seed_start_set = 0;
    int closest_seed_end_set = 0;
    int closest_seed_count = 0;
    int closest_seed_count_set = 0;
    uint64_t closest_seed_sampling_seed = 0;
    int closest_seed_sampling_seed_set = 0;
    int closest_seed_max_evals = 100000;
    int closest_seed_max_evals_set = 0;
    int closest_seed_threads = 0;
    int closest_seed_threads_set = 0;
    int closest_seed_timeout_ms = 0;
    int closest_seed_timeout_set = 0;
    int closest_seed_trace_draw = 0;
    int closest_seed_trace_draw_set = 0;
    const char *closest_seed_output_file = NULL;
    const char *next_draw_input_file = NULL;
    const char *closest_seed_input_file = NULL;
    const char *sim_historical_csv_output = NULL;
    int analytics_top = 10;
    int analytics_explain = 0;
    const char *analytics_format = "table";
    int analytics_format_set = 0;
    const char *historical_csv = NULL;
    int historical_csv_from_cli = 0;

    /* ---------------------------------------------------------
       Parse arguments (all options, --game may appear anywhere)
       --------------------------------------------------------- */
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--game") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--game requires a game name.\n");
                config_free(&cfg);
                return 1;
            }
            game_name = argv[++i];
        }
        else if (strcmp(argv[i], "--animate") == 0)
        {
            animate = 1;
        }
        else if (strcmp(argv[i], "--gui") == 0)
        {
            gui = 1;
            /* Check if next argument is a GUI mode specification */
            if (i + 1 < argc && (strcmp(argv[i + 1], "2D") == 0 || strcmp(argv[i + 1], "3D") == 0))
            {
                gui_mode = argv[++i];
            }
        }
        else if (strcmp(argv[i], "--sorted") == 0)
        {
            sorted_numbers = 1;
        }
        else if (strcmp(argv[i], "--debug-overlay") == 0)
        {
#if OPEN_LOTTO_ENABLE_OPENGL
            debug_overlay = 1;
#else
            log_warn("Ignoring --debug-overlay: OpenGL backend is disabled in this build.");
#endif
        }
        else if (strcmp(argv[i], "--dark-mode") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--dark-mode requires 'on', 'off', or 'auto'.\n");
                config_free(&cfg);
                return 1;
            }
            i++;
            const char *dm_str = argv[i];
            if (strcasecmp(dm_str, "on") == 0)
                dark_mode = 1;
            else if (strcasecmp(dm_str, "off") == 0)
                dark_mode = 0;
            else if (strcasecmp(dm_str, "auto") == 0)
                dark_mode = -1;
            else
            {
                fprintf(stderr, "Invalid --dark-mode value '%s' (use: on, off, auto)\n", dm_str);
                config_free(&cfg);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--verbose") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--verbose requires a log level (ERROR, WARN, INFO, DEBUG).\n");
                config_free(&cfg);
                return 1;
            }

            const char *level_str = argv[++i];
            if (validate_log_level(level_str) != VALIDATE_OK)
            {
                config_free(&cfg);
                return 1;
            }
            log_level = parse_log_level(level_str);
            cli_log_level_set = 1;
        }
        else if (strcmp(argv[i], "--draws") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--draws requires a number.\n");
                config_free(&cfg);
                return 1;
            }

            if (validate_draw_count(argv[++i], &cli_draws) != VALIDATE_OK)
            {
                config_free(&cfg);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--export") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--export requires a format (csv or json).\n");
                config_free(&cfg);
                return 1;
            }

            export_format = argv[++i];
            if (validate_export_format(export_format) != VALIDATE_OK)
            {
                config_free(&cfg);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--output") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--output requires a filename.\n");
                config_free(&cfg);
                return 1;
            }

            export_filename = argv[++i];
        }
        else if (strcmp(argv[i], "--validate-only") == 0)
        {
            validate_only = 1;
        }
        else if (strcmp(argv[i], "--reload-plugin") == 0)
        {
            reload_plugin = 1;
        }
        else if (strcmp(argv[i], "--database-gewinnzahlen") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--database-gewinnzahlen requires a subcommand (use: update).\n");
                config_free(&cfg);
                return 1;
            }
            database_gewinnzahlen_cmd = argv[++i];
            if (strcmp(database_gewinnzahlen_cmd, "update") != 0)
            {
                fprintf(stderr,
                        "Unsupported --database-gewinnzahlen subcommand '%s' (use: update).\n",
                        database_gewinnzahlen_cmd);
                config_free(&cfg);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--download-url") == 0)
        {
            if (i + 2 >= argc)
            {
                fprintf(stderr, "--download-url requires: set <url>.\n");
                config_free(&cfg);
                return 1;
            }

            download_url_cmd = argv[++i];
            if (strcmp(download_url_cmd, "set") != 0)
            {
                fprintf(stderr, "Unsupported --download-url subcommand '%s' (use: set).\n",
                        download_url_cmd);
                config_free(&cfg);
                return 1;
            }

            download_url_value = argv[++i];
            if (download_url_value[0] == '\0')
            {
                fprintf(stderr, "--download-url set requires a non-empty URL.\n");
                config_free(&cfg);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--download-config") == 0)
        {
            if (i + 3 >= argc)
            {
                fprintf(stderr, "--download-config requires: <name> set <value>.\n");
                config_free(&cfg);
                return 1;
            }

            download_config_name = argv[++i];
            download_config_cmd = argv[++i];
            if (strcmp(download_config_cmd, "set") != 0)
            {
                fprintf(stderr, "Unsupported --download-config subcommand '%s' (use: set).\n",
                        download_config_cmd);
                config_free(&cfg);
                return 1;
            }

            download_config_value = argv[++i];
            if (download_config_value[0] == '\0')
            {
                fprintf(stderr, "--download-config set requires a non-empty value.\n");
                config_free(&cfg);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--seed") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--seed requires a numeric value (decimal or 0x-prefixed hex).\n");
                config_free(&cfg);
                return 1;
            }

            if (!parse_seed_value(argv[++i], &seed_value))
            {
                fprintf(stderr,
                        "Invalid --seed value '%s' (expected decimal or 0x-prefixed hex).\n",
                        argv[i]);
                config_free(&cfg);
                return 1;
            }
            use_seed = 1;
        }
        else if (strcmp(argv[i], "--from") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--from requires a date value in YYYY-MM-DD format.\n");
                config_free(&cfg);
                return 1;
            }
            period_from = argv[++i];
        }
        else if (strcmp(argv[i], "--to") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--to requires a date value in YYYY-MM-DD format.\n");
                config_free(&cfg);
                return 1;
            }
            period_to = argv[++i];
        }
        else if (strcmp(argv[i], "--frequency-distribution") == 0 ||
                 strcmp(argv[i], "--analytics-frequency") == 0)
        {
            analytics_frequency = 1;
        }
        else if (strcmp(argv[i], "--analytics-barometer") == 0)
        {
            analytics_barometer = 1;
        }
        else if (strcmp(argv[i], "--analytics-hot-cold") == 0)
        {
            analytics_hot_cold = 1;
        }
        else if (strcmp(argv[i], "--simulation-analytics") == 0)
        {
            simulation_analytics = 1;
        }
        else if (strcmp(argv[i], "--draw-count-period") == 0)
        {
            period_draw_count_mode = 1;
        }
        else if (strcmp(argv[i], "--closest-seed") == 0)
        {
            closest_seed_mode = 1;
        }
        else if (strcmp(argv[i], "--seed-start") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr,
                        "--seed-start requires a numeric value (decimal or 0x-prefixed hex).\n");
                config_free(&cfg);
                return 1;
            }

            if (!parse_seed_value(argv[++i], &closest_seed_start))
            {
                fprintf(stderr,
                        "Invalid --seed-start value '%s' (expected decimal or 0x-prefixed hex).\n",
                        argv[i]);
                config_free(&cfg);
                return 1;
            }
            closest_seed_start_set = 1;
        }
        else if (strcmp(argv[i], "--seed-end") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr,
                        "--seed-end requires a numeric value (decimal or 0x-prefixed hex).\n");
                config_free(&cfg);
                return 1;
            }

            if (!parse_seed_value(argv[++i], &closest_seed_end))
            {
                fprintf(stderr,
                        "Invalid --seed-end value '%s' (expected decimal or 0x-prefixed hex).\n",
                        argv[i]);
                config_free(&cfg);
                return 1;
            }
            closest_seed_end_set = 1;
        }
        else if (strcmp(argv[i], "--seed-count") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--seed-count requires a positive integer.\n");
                config_free(&cfg);
                return 1;
            }

            char *end = NULL;
            long parsed = strtol(argv[++i], &end, 10);
            if (!end || *end != '\0' || parsed <= 0 || parsed > INT_MAX)
            {
                fprintf(stderr, "Error: --seed-count must be a positive integer.\n");
                config_free(&cfg);
                return 1;
            }
            closest_seed_count = (int)parsed;
            closest_seed_count_set = 1;
        }
        else if (strcmp(argv[i], "--sample-seed") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr,
                        "--sample-seed requires a numeric value (decimal or 0x-prefixed hex).\n");
                config_free(&cfg);
                return 1;
            }
            if (!parse_seed_value(argv[++i], &closest_seed_sampling_seed))
            {
                fprintf(stderr,
                        "Invalid --sample-seed value '%s' (expected decimal or 0x-prefixed hex).\n",
                        argv[i]);
                config_free(&cfg);
                return 1;
            }
            closest_seed_sampling_seed_set = 1;
        }
        else if (strcmp(argv[i], "--max-evals") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--max-evals requires a positive integer.\n");
                config_free(&cfg);
                return 1;
            }

            char *end = NULL;
            long parsed = strtol(argv[++i], &end, 10);
            if (!end || *end != '\0' || parsed <= 0 || parsed > INT_MAX)
            {
                fprintf(stderr, "Error: --max-evals must be a positive integer.\n");
                config_free(&cfg);
                return 1;
            }
            closest_seed_max_evals = (int)parsed;
            closest_seed_max_evals_set = 1;
        }
        else if (strcmp(argv[i], "--threads") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--threads requires a positive integer.\n");
                config_free(&cfg);
                return 1;
            }

            char *end = NULL;
            long parsed = strtol(argv[++i], &end, 10);
            if (!end || *end != '\0' || parsed <= 0 || parsed > INT_MAX)
            {
                fprintf(stderr, "Error: --threads must be a positive integer.\n");
                config_free(&cfg);
                return 1;
            }
            closest_seed_threads = (int)parsed;
            closest_seed_threads_set = 1;
        }
        else if (strcmp(argv[i], "--timeout-ms") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--timeout-ms requires a positive integer.\n");
                config_free(&cfg);
                return 1;
            }

            char *end = NULL;
            long parsed = strtol(argv[++i], &end, 10);
            if (!end || *end != '\0' || parsed <= 0 || parsed > INT_MAX)
            {
                fprintf(stderr, "Error: --timeout-ms must be a positive integer.\n");
                config_free(&cfg);
                return 1;
            }
            closest_seed_timeout_ms = (int)parsed;
            closest_seed_timeout_set = 1;
        }
        else if (strcmp(argv[i], "--output-file") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--output-file requires a file path.\n");
                config_free(&cfg);
                return 1;
            }

            closest_seed_output_file = argv[++i];
            if (closest_seed_output_file[0] == '\0')
            {
                fprintf(stderr, "--output-file requires a non-empty path.\n");
                config_free(&cfg);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--closest-seed-input") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--closest-seed-input requires a file path.\n");
                config_free(&cfg);
                return 1;
            }

            closest_seed_input_file = argv[++i];
            if (closest_seed_input_file[0] == '\0')
            {
                fprintf(stderr, "--closest-seed-input requires a non-empty path.\n");
                config_free(&cfg);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--next-draw") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--next-draw requires a file path.\n");
                config_free(&cfg);
                return 1;
            }

            next_draw_input_file = argv[++i];
            if (next_draw_input_file[0] == '\0')
            {
                fprintf(stderr, "--next-draw requires a non-empty path.\n");
                config_free(&cfg);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--trace-draw") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--trace-draw requires a positive integer.\n");
                config_free(&cfg);
                return 1;
            }

            char *end = NULL;
            long parsed = strtol(argv[++i], &end, 10);
            if (!end || *end != '\0' || parsed <= 0 || parsed > INT_MAX)
            {
                fprintf(stderr, "Error: --trace-draw must be a positive integer.\n");
                config_free(&cfg);
                return 1;
            }

            closest_seed_trace_draw = (int)parsed;
            closest_seed_trace_draw_set = 1;
        }
        else if (strcmp(argv[i], "--format") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--format requires one of: table, json, csv.\n");
                config_free(&cfg);
                return 1;
            }

            analytics_format = argv[++i];
            analytics_format_set = 1;
            if (strcmp(analytics_format, "table") != 0 && strcmp(analytics_format, "json") != 0 &&
                strcmp(analytics_format, "csv") != 0)
            {
                fprintf(stderr, "Error: Unsupported --format '%s'. Use table, json, or csv.\n",
                        analytics_format);
                config_free(&cfg);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--historical-csv") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--historical-csv requires a file path.\n");
                config_free(&cfg);
                return 1;
            }
            historical_csv = argv[++i];
            historical_csv_from_cli = 1;
        }
        else if (strcmp(argv[i], "--top") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--top requires a positive integer.\n");
                config_free(&cfg);
                return 1;
            }

            char *end = NULL;
            long parsed = strtol(argv[++i], &end, 10);
            if (!end || *end != '\0' || parsed <= 0 || parsed > 128)
            {
                fprintf(stderr, "Error: --top must be an integer between 1 and 128.\n");
                config_free(&cfg);
                return 1;
            }
            analytics_top = (int)parsed;
        }
        else if (strcmp(argv[i], "--explain") == 0)
        {
            analytics_explain = 1;
        }
        else if (strcmp(argv[i], "--sim-historical-csv") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--sim-historical-csv requires a file path.\n");
                config_free(&cfg);
                return 1;
            }
            sim_historical_csv_output = argv[++i];
        }
        else
        {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            config_free(&cfg);
            return 1;
        }
    }

    int analytics_mode_count = analytics_frequency + analytics_barometer + analytics_hot_cold;

    /* ---------------------------------------------------------
       Apply config file defaults for options not set by CLI
       --------------------------------------------------------- */
    if (!game_name && cfg.game)
    {
        game_name = cfg.game;
        log_debug("Using game from config file: %s", game_name);
    }

    int draws = 1; /* built-in default */
    if (cli_draws >= 1)
    {
        draws = cli_draws;
    }
    else if (cfg.draws >= 1)
    {
        draws = cfg.draws;
        log_debug("Using draws from config file: %d", draws);
    }

    if (!export_format && cfg.export_format)
    {
        export_format = cfg.export_format;
        log_debug("Using export format from config file: %s", export_format);
        if (validate_export_format(export_format) != VALIDATE_OK)
        {
            config_free(&cfg);
            return 1;
        }
    }

    if (!export_filename && cfg.output_file)
    {
        export_filename = cfg.output_file;
        log_debug("Using output file from config file: %s", export_filename);
    }

    if (!cli_log_level_set && cfg.verbose_level)
    {
        if (validate_log_level(cfg.verbose_level) == VALIDATE_OK)
        {
            log_level = parse_log_level(cfg.verbose_level);
            log_debug("Using verbose level from config file: %s", cfg.verbose_level);
        }
    }

    if (!gui_mode && cfg.gui_mode)
    {
        gui_mode = cfg.gui_mode;
        if (strcmp(gui_mode, "2D") == 0 || strcmp(gui_mode, "3D") == 0)
        {
            gui = 1;
            log_debug("Using GUI mode from config file: %s", gui_mode);
        }
    }

    if (download_url_cmd || download_config_cmd)
    {
        if (download_url_cmd)
        {
            const char *source_key = game_source_key_cli(game_name);
            if (!source_key)
            {
                fprintf(stderr, "--download-url set requires a supported --game (Eurojackpot or "
                                "Lotto 6aus49).\n");
                config_free(&cfg);
                return 1;
            }

            char sources_path[PATH_MAX];
            if (get_sources_config_path_cli(sources_path, sizeof(sources_path)) != 0)
            {
                fprintf(stderr, "Failed to resolve sources config path.\n");
                config_free(&cfg);
                return 1;
            }

            if (ini_set_value(sources_path, "sources", source_key, download_url_value) != 0)
            {
                fprintf(stderr, "Failed to update sources config at %s.\n", sources_path);
                config_free(&cfg);
                return 1;
            }

            printf("Updated source URL for '%s' in %s\n", source_key, sources_path);
        }

        if (download_config_cmd)
        {
            const char *config_key = NULL;
            if (!map_download_config_key(download_config_name, &config_key))
            {
                fprintf(
                    stderr,
                    "Unknown --download-config key '%s'. Use one of: workers, fetch_timeout_sec, "
                    "draw_timeout_sec, max_retry_attempts, retry_base_delay_ms, "
                    "retry_max_delay_ms, max_fetch_draws, or #sym:* aliases.\n",
                    download_config_name);
                config_free(&cfg);
                return 1;
            }

            char download_path[PATH_MAX];
            if (get_download_config_path_cli(download_path, sizeof(download_path)) != 0)
            {
                fprintf(stderr, "Failed to resolve download config path.\n");
                config_free(&cfg);
                return 1;
            }

            if (ini_set_value(download_path, "download", config_key, download_config_value) != 0)
            {
                fprintf(stderr, "Failed to update download config at %s.\n", download_path);
                config_free(&cfg);
                return 1;
            }

            printf("Updated download setting '%s' in %s\n", config_key, download_path);
        }

        config_free(&cfg);
        return 0;
    }

    /* Require --game (or game in config) */
    if (!game_name)
    {
        fprintf(stderr, "Error: --game NAME is required.\n"
                        "Hint: Set 'game = <name>' in ~/.lottorc for a persistent default.\n");
        print_usage(argv[0]);
        config_free(&cfg);
        return 1;
    }

    /* Validate option combinations */
    if (validate_option_conflicts(animate, gui, export_format) != VALIDATE_OK)
    {
        config_free(&cfg);
        return 1;
    }

    if (validate_export_pair(export_format, export_filename) != VALIDATE_OK)
    {
        config_free(&cfg);
        return 1;
    }

    if (gui_mode && validate_gui_mode(gui_mode) != VALIDATE_OK)
    {
        config_free(&cfg);
        return 1;
    }

    /* Validate --from / --to period: both must be present if either is given.
       Exceptions:
       - --historical-csv: dates are optional; defaults come from the file.
       - --sim-historical-csv: only --from is needed as start date; optional (defaults to today).
    */
    if (period_from && !period_to && !sim_historical_csv_output && !historical_csv_from_cli)
    {
        fprintf(stderr, "Error: --from requires --to.\n");
        fprintf(stderr, "Hint: Example: --from 2025-01-01 --to 2025-12-31\n");
        config_free(&cfg);
        return 1;
    }
    if (period_to && !period_from)
    {
        fprintf(stderr, "Error: --to requires --from.\n");
        fprintf(stderr, "Hint: Example: --from 2025-01-01 --to 2025-12-31\n");
        config_free(&cfg);
        return 1;
    }
    if (period_from && period_to)
    {
        if (validate_analytics_period(period_from, period_to, NULL, NULL) != VALIDATE_OK)
        {
            config_free(&cfg);
            return 1;
        }
    }

    if (gui && use_seed)
    {
        fprintf(stderr, "Error: --seed is currently supported only in CLI mode.\n");
        config_free(&cfg);
        return 1;
    }

    if (sim_historical_csv_output)
    {
        if (gui || animate || export_format)
        {
            fprintf(
                stderr,
                "Error: --sim-historical-csv cannot be combined with --gui/--animate/--export.\n");
            config_free(&cfg);
            return 1;
        }
    }

    if (simulation_analytics)
    {
        if (gui || animate)
        {
            fprintf(stderr, "Error: --simulation-analytics is CLI-only (no --gui/--animate).\n");
            config_free(&cfg);
            return 1;
        }
        if (analytics_mode_count > 0)
        {
            fprintf(stderr,
                    "Error: --simulation-analytics cannot be combined with historical analytics "
                    "modes.\n");
            config_free(&cfg);
            return 1;
        }
    }

    if (period_draw_count_mode)
    {
        if (animate || gui || export_format || simulation_analytics || analytics_mode_count > 0 ||
            closest_seed_mode || closest_seed_input_file || sim_historical_csv_output ||
            database_gewinnzahlen_cmd)
        {
            fprintf(stderr, "Error: --draw-count-period is a standalone analytics mode and "
                            "cannot be combined with draw generation, closest-seed, export, or "
                            "other analytics modes.\n");
            config_free(&cfg);
            return 1;
        }

        if (!period_from || !period_to)
        {
            fprintf(stderr, "Error: --draw-count-period requires --from and --to.\n");
            fprintf(stderr, "Hint: Example: --draw-count-period --from 2026-01-01 --to "
                            "2026-07-04\n");
            config_free(&cfg);
            return 1;
        }
    }

    if (closest_seed_mode)
    {
        if (next_draw_input_file)
        {
            fprintf(stderr, "Error: --closest-seed and --next-draw cannot be used together.\n");
            config_free(&cfg);
            return 1;
        }

        if (closest_seed_input_file)
        {
            fprintf(stderr,
                    "Error: --closest-seed and --closest-seed-input cannot be used together.\n");
            config_free(&cfg);
            return 1;
        }

        if (animate || export_format || simulation_analytics || analytics_mode_count > 0 ||
            sim_historical_csv_output)
        {
            fprintf(stderr, "Error: --closest-seed cannot be combined with animate/export or "
                            "other analytics modes.\n");
            config_free(&cfg);
            return 1;
        }
        if (gui && strcmp(gui_mode, "2D") != 0)
        {
            fprintf(stderr,
                    "Error: --closest-seed GUI visualization currently supports only --gui 2D.\n");
            config_free(&cfg);
            return 1;
        }
        if (!period_from || !period_to)
        {
            fprintf(stderr,
                    "Error: --closest-seed requires --from and --to for one fixed period.\n");
            config_free(&cfg);
            return 1;
        }

        if (closest_seed_sampling_seed_set && !closest_seed_count_set)
        {
            fprintf(stderr, "Error: --sample-seed requires --seed-count.\n");
            config_free(&cfg);
            return 1;
        }

        if (closest_seed_count_set)
        {
            if (closest_seed_max_evals_set)
            {
                fprintf(stderr, "Error: --max-evals cannot be combined with --seed-count mode.\n");
                config_free(&cfg);
                return 1;
            }
            if (closest_seed_start_set != closest_seed_end_set)
            {
                fprintf(
                    stderr,
                    "Error: sampled mode with bounds requires both --seed-start and --seed-end.\n");
                config_free(&cfg);
                return 1;
            }

            if (closest_seed_start_set)
            {
                if (closest_seed_start > closest_seed_end)
                {
                    fprintf(stderr, "Error: --seed-start must be <= --seed-end.\n");
                    config_free(&cfg);
                    return 1;
                }

                uint64_t domain_size = closest_seed_end - closest_seed_start + 1ULL;
                if ((uint64_t)closest_seed_count > domain_size)
                {
                    fprintf(stderr, "Error: --seed-count exceeds bounded domain size (%llu).\n",
                            (unsigned long long)domain_size);
                    config_free(&cfg);
                    return 1;
                }
            }
        }
        else if (!closest_seed_start_set || !closest_seed_end_set)
        {
            fprintf(stderr, "Error: --closest-seed requires --seed-count OR both --seed-start and "
                            "--seed-end.\n");
            config_free(&cfg);
            return 1;
        }

        if (!closest_seed_count_set && closest_seed_start > closest_seed_end)
        {
            fprintf(stderr, "Error: --seed-start must be <= --seed-end.\n");
            config_free(&cfg);
            return 1;
        }
        if (analytics_top > SEED_CALIBRATION_MAX_TOP_K)
        {
            fprintf(stderr, "Error: --top must be <= %d for --closest-seed.\n",
                    SEED_CALIBRATION_MAX_TOP_K);
            config_free(&cfg);
            return 1;
        }
    }
    else if (closest_seed_threads_set || closest_seed_timeout_set || closest_seed_count_set ||
             closest_seed_sampling_seed_set || closest_seed_output_file)
    {
        fprintf(stderr,
                "Error: --threads/--timeout-ms/--seed-count/--sample-seed/--output-file are "
                "supported only with --closest-seed.\n");
        config_free(&cfg);
        return 1;
    }

    if (closest_seed_trace_draw_set && !closest_seed_input_file && !next_draw_input_file)
    {
        fprintf(stderr, "Error: --trace-draw is supported only with --closest-seed-input.\n");
        config_free(&cfg);
        return 1;
    }

    if (next_draw_input_file && closest_seed_input_file)
    {
        fprintf(stderr, "Error: --next-draw and --closest-seed-input cannot be used together.\n");
        config_free(&cfg);
        return 1;
    }

    if (next_draw_input_file)
    {
        /* Check the CLI value only: a config-file "draws" default must not break --next-draw. */
        if (cli_draws != -1 && cli_draws != 1)
        {
            fprintf(stderr,
                    "Error: --next-draw computes exactly one draw (N+1); do not use --draws.\n");
            config_free(&cfg);
            return 1;
        }
        draws = 1;

        if (closest_seed_trace_draw_set)
        {
            fprintf(stderr, "Error: --trace-draw cannot be used with --next-draw; the N+1 trace "
                            "is always printed to stderr.\n");
            config_free(&cfg);
            return 1;
        }
    }

    if ((closest_seed_input_file || next_draw_input_file) && analytics_format_set)
    {
        fprintf(stderr, "Error: --format is not supported with --closest-seed-input or "
                        "--next-draw (output is always a draw listing).\n");
        config_free(&cfg);
        return 1;
    }

    if (closest_seed_input_file &&
        (animate || gui || closest_seed_mode || simulation_analytics || analytics_mode_count > 0 ||
         period_draw_count_mode || historical_csv_from_cli || sim_historical_csv_output ||
         database_gewinnzahlen_cmd || reload_plugin || period_from || period_to ||
         closest_seed_output_file || export_format || export_filename))
    {
        fprintf(stderr, "Error: --closest-seed-input is a standalone prediction mode and cannot be "
                        "combined with closest-seed/analytics/gui/export options.\n");
        config_free(&cfg);
        return 1;
    }

    if (next_draw_input_file &&
        (animate || gui || closest_seed_mode || closest_seed_input_file || simulation_analytics ||
         analytics_mode_count > 0 || period_draw_count_mode || historical_csv_from_cli ||
         sim_historical_csv_output || database_gewinnzahlen_cmd || reload_plugin || period_from ||
         period_to || closest_seed_output_file || export_format || export_filename))
    {
        fprintf(stderr,
                "Error: --next-draw is a standalone prediction mode and cannot be combined with "
                "closest-seed/analytics/gui/export options.\n");
        config_free(&cfg);
        return 1;
    }

    /* Default GUI mode to 2D if not specified */
    if (!gui_mode)
    {
        gui_mode = "2D";
    }

    /* Set the log level early so all subsequent operations are logged */
    log_set_level(log_level);
    log_debug("Starting open-lotto with log level set to DEBUG");

    /* ---------------------------------------------------------
       Discover and load plugins
       --------------------------------------------------------- */
    PluginRegistry *registry = registry_create();
    if (!registry)
    {
        log_error("Failed to create plugin registry");
        config_free(&cfg);
        return 1;
    }

    registry_discover_plugins(registry);

    /* Validate game name against discovered plugins */
    if (validate_game_name(game_name, registry) != VALIDATE_OK)
    {
        registry_destroy(registry);
        config_free(&cfg);
        return 1;
    }

    if (reload_plugin)
    {
        if (registry_reload_plugin(registry, game_name) != 0)
        {
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }
    }

    LoadedPlugin *selected = registry_find_plugin(registry, game_name);
    if (!selected) /* Should not happen if validate_game_name passed, but defensive check */
    {
        log_error("Game '%s' not found", game_name);
        registry_destroy(registry);
        config_free(&cfg);
        return 1;
    }

    if (closest_seed_input_file || next_draw_input_file)
    {
        int next_draw_mode = (next_draw_input_file != NULL);
        const char *prediction_input_file =
            next_draw_mode ? next_draw_input_file : closest_seed_input_file;

        ClosestSeedReference closest_ref;
        if (!closest_seed_game_supported(&selected->info) ||
            !parse_closest_seed_json_file(prediction_input_file, selected->name, &selected->info,
                                          &closest_ref))
        {
            fprintf(stderr,
                    "Error: Invalid closest-seed input file '%s'. Expected JSON output "
                    "from --closest-seed --format json for the same --game.\n",
                    prediction_input_file);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        if (next_draw_mode)
            log_info("next-draw loaded: file=%s seeds=%d historical_draw_count=%d",
                     prediction_input_file, closest_ref.seed_count,
                     closest_ref.historical_draw_count);
        else
            log_info("closest-seed-input loaded: file=%s seeds=%d historical_draw_count=%d",
                     prediction_input_file, closest_ref.seed_count,
                     closest_ref.historical_draw_count);

        if (next_draw_mode)
        {
            int draw_index = closest_ref.historical_draw_count;

            fprintf(stderr,
                    "Next draw mode: period has N=%d real draws, computing draw N+1 (index=%d)\n",
                    closest_ref.historical_draw_count, draw_index);

            ClosestSeedPeriodFrequencies *period_freq = calloc(1, sizeof(*period_freq));
            if (!period_freq ||
                !build_seed_period_frequencies(&closest_ref, &selected->info, period_freq))
            {
                fprintf(stderr,
                        "Error: Failed to recompute period frequency distributions for next-draw "
                        "mode.\n");
                free(period_freq);
                registry_destroy(registry);
                config_free(&cfg);
                return 1;
            }

            LotteryResult result;
            fprintf(stderr, "\nNext draw trace (N+1): draw_index=%d\n", draw_index);
            int synth_ok = synthesize_closest_seed_draw(&closest_ref, &selected->info, draw_index,
                                                        period_freq, &result, stderr);
            free(period_freq);
            if (!synth_ok)
            {
                fprintf(stderr, "Error: Failed to synthesize next draw (N+1).\n");
                registry_destroy(registry);
                config_free(&cfg);
                return 1;
            }

            if (sorted_numbers)
                sort_lottery_result_numbers(&result);

            printf("Synthesized next draw (N+1) from %d closest-seed candidate(s)\n\n",
                   closest_ref.seed_count);
            print_draw_result(selected->name, 1, &result);

            registry_destroy(registry);
            config_free(&cfg);
            return 0;
        }

        if (log_level == LOG_DEBUG && !closest_seed_trace_draw_set)
        {
            double diag_start = omp_get_wtime();
            for (int s = 0; s < closest_ref.seed_count; s++)
            {
                double main_l1 = 0.0;
                double extra_l1 = 0.0;
                double combined_l1 = 0.0;
                if (compute_seed_period_frequency_diagnostics(&closest_ref, &selected->info,
                                                              closest_ref.seeds[s], &main_l1,
                                                              &extra_l1, &combined_l1))
                {
                    if (selected->info.extra_count > 0)
                    {
                        log_debug(
                            "closest-seed-input seed diagnostics: rank=%d seed=%llu "
                            "period_l1_main=%.9f period_l1_extra=%.9f period_l1_combined=%.9f",
                            s + 1, (unsigned long long)closest_ref.seeds[s], main_l1, extra_l1,
                            combined_l1);
                    }
                    else
                    {
                        log_debug("closest-seed-input seed diagnostics: rank=%d seed=%llu "
                                  "period_l1_main=%.9f",
                                  s + 1, (unsigned long long)closest_ref.seeds[s], main_l1);
                    }
                }
            }
            log_debug("closest-seed-input period diagnostics completed in %.3f sec",
                      omp_get_wtime() - diag_start);
        }
        else if (closest_seed_trace_draw_set)
        {
            log_info("closest-seed-input trace mode enabled: draw=%d (period diagnostics skipped)",
                     closest_seed_trace_draw);
        }

        log_info("closest-seed-input synthesis plan: requested_draws=%d start_draw_index=%d "
                 "end_draw_index=%d",
                 draws, closest_ref.historical_draw_count,
                 closest_ref.historical_draw_count + draws - 1);

        if (draws > INT_MAX - closest_ref.historical_draw_count)
        {
            fprintf(stderr, "Error: --draws %d is too large for historical_draw_count %d.\n", draws,
                    closest_ref.historical_draw_count);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        if (closest_seed_trace_draw_set && closest_seed_trace_draw > draws)
        {
            fprintf(stderr, "Error: --trace-draw %d exceeds --draws %d.\n", closest_seed_trace_draw,
                    draws);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        LotteryResult *results = calloc((size_t)draws, sizeof(LotteryResult));
        if (!results)
        {
            fprintf(stderr, "Error: Out of memory while preparing synthesized draws.\n");
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        if (closest_seed_trace_draw_set)
            log_set_level(LOG_WARN);

        for (int i = 0; i < draws; i++)
        {
            int draw_index = closest_ref.historical_draw_count + i;
            int trace_this_draw =
                closest_seed_trace_draw_set && ((i + 1) == closest_seed_trace_draw);
            if (!closest_seed_trace_draw_set || trace_this_draw)
                log_debug("closest-seed-input synthesize draw=%d draw_index=%d", i + 1, draw_index);

            if (trace_this_draw)
                fprintf(stderr, "\nTrace draw #%d (draw_index=%d) across %d closest seeds\n", i + 1,
                        draw_index, closest_ref.seed_count);

            if (!synthesize_closest_seed_draw(&closest_ref, &selected->info, draw_index, NULL,
                                              &results[i], trace_this_draw ? stderr : NULL))
            {
                fprintf(stderr, "Error: Failed to synthesize draw %d from closest-seed input.\n",
                        i + 1);
                free(results);
                registry_destroy(registry);
                config_free(&cfg);
                return 1;
            }

            if (sorted_numbers)
                sort_lottery_result_numbers(&results[i]);
        }

        if (closest_seed_trace_draw_set)
            log_set_level(log_level);

        if (draws == 1)
            printf("Synthesized draw from %d closest-seed candidate(s)\n\n",
                   closest_ref.seed_count);
        else
            printf("Synthesized %d draws from %d closest-seed candidate(s)\n\n", draws,
                   closest_ref.seed_count);

        for (int i = 0; i < draws; i++)
            print_draw_result(selected->name, i + 1, &results[i]);

        free(results);
        registry_destroy(registry);
        config_free(&cfg);
        return 0;
    }

    if (database_gewinnzahlen_cmd)
    {
        HistoricalDrawSnapshot snapshot;
        int db_rc = historical_db_sync_latest(selected->name, NULL, &snapshot);
        if (db_rc < 0)
        {
            fprintf(stderr,
                    "Error: database sync failed for '%s' (code %d). Check source config and "
                    "network.\n",
                    selected->name, db_rc);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        if (db_rc == HISTORICAL_DB_SYNC_UNCHANGED)
            printf("Local historical database already up to date for '%s' (draw_date=%s).\n",
                   selected->name, snapshot.draw_date);
        else
            printf("Historical database updated for '%s' (latest draw_date=%s).\n", selected->name,
                   snapshot.draw_date);

        registry_destroy(registry);
        config_free(&cfg);
        return 0;
    }

    if (closest_seed_mode)
    {
        ClosestSeedProgressState progress_state;
        progress_state.enabled = closest_seed_terminal_supports_ansi_redraw();
        progress_state.printed_lines = 0;

        if (!closest_seed_threads_set)
            closest_seed_threads = omp_get_max_threads();
        if (closest_seed_count_set && !closest_seed_sampling_seed_set)
            closest_seed_sampling_seed = generate_strong_seed();

        fprintf(stderr, "closest-seed run: threads=%d\n", closest_seed_threads);
        if (closest_seed_count_set)
            fprintf(stderr, "closest-seed sampled mode: sample-seed=0x%016llx\n",
                    (unsigned long long)closest_seed_sampling_seed);

        HistoricalDraw *historical_draws = calloc(ANALYTICS_MAX_DRAWS, sizeof(HistoricalDraw));
        HistoricalDraw *filtered = calloc(ANALYTICS_MAX_DRAWS, sizeof(HistoricalDraw));
        int draw_count = 0;
        int filtered_count = 0;

        if (!historical_draws || !filtered)
        {
            fprintf(stderr, "Error: Out of memory while preparing closest-seed buffers.\n");
            free(historical_draws);
            free(filtered);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        int load_rc = VALIDATE_OK;
        if (historical_csv_from_cli)
        {
            load_rc =
                analytics_load_historical_csv(historical_csv, historical_draws, ANALYTICS_MAX_DRAWS,
                                              &draw_count, &selected->info);
        }
        else
        {
            load_rc = analytics_load_historical_db_snapshot(selected->name, NULL, historical_draws,
                                                            ANALYTICS_MAX_DRAWS, &draw_count,
                                                            &selected->info);
        }

        if (load_rc != VALIDATE_OK)
        {
            free(historical_draws);
            free(filtered);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        if (analytics_filter_period(historical_draws, draw_count, period_from, period_to, filtered,
                                    &filtered_count) != VALIDATE_OK)
        {
            free(historical_draws);
            free(filtered);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        SeedCalibrationDrawContext draw_ctx;
        draw_ctx.info = selected->info;

        ClosestSeedReference closest_reference;
        if (!closest_seed_game_supported(&selected->info) ||
            !build_closest_seed_reference(filtered, filtered_count, &selected->info,
                                          &closest_reference))
        {
            free(historical_draws);
            free(filtered);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        uint64_t *sampled_seeds = NULL;
        uint64_t sampled_seed_start = closest_seed_start_set ? closest_seed_start : 0ULL;
        uint64_t sampled_seed_end = closest_seed_end_set ? closest_seed_end : UINT64_MAX;
        if (closest_seed_count_set)
        {
            sampled_seeds = calloc((size_t)closest_seed_count, sizeof(uint64_t));
            if (!sampled_seeds)
            {
                fprintf(stderr, "Error: Out of memory while preparing sampled seed set.\n");
                free(historical_draws);
                free(filtered);
                registry_destroy(registry);
                config_free(&cfg);
                return 1;
            }

            if (!build_unique_seed_sample(sampled_seed_start, sampled_seed_end, closest_seed_count,
                                          closest_seed_sampling_seed, sampled_seeds))
            {
                fprintf(stderr, "Error: Failed to build unique sampled seed set.\n");
                free(sampled_seeds);
                free(historical_draws);
                free(filtered);
                registry_destroy(registry);
                config_free(&cfg);
                return 1;
            }
        }

        SeedCalibrationRequest request;
        memset(&request, 0, sizeof(request));
        request.historical_draws = filtered;
        request.historical_draw_count = filtered_count;
        request.number_min = selected->info.main_min;
        request.number_max = selected->info.main_max;
        request.expected_main_count = selected->info.main_count;
        request.seed_list = sampled_seeds;
        request.seed_list_count = closest_seed_count_set ? closest_seed_count : 0;
        request.seed_start = closest_seed_count_set ? sampled_seed_start : closest_seed_start;
        request.seed_end = closest_seed_count_set ? sampled_seed_end : closest_seed_end;
        request.max_evals = closest_seed_max_evals;
        request.threads = closest_seed_threads;
        request.top_k = analytics_top;
        request.weight_frequency = 1.0;
        request.weight_gap = 1.0;
        request.weight_rank = 1.0;
        request.draw_for_seed = seed_calibration_draw_callback;
        request.draw_ctx = &draw_ctx;
        request.progress_fn = closest_seed_progress_callback;
        request.progress_ctx = &progress_state;

        if (closest_seed_timeout_ms > 0)
        {
            log_warn("--timeout-ms=%d is currently reserved; using --max-evals to bound work.",
                     closest_seed_timeout_ms);
        }

        SeedCalibrationResult result;
        int rc = seed_calibration_find_closest(&request, &result);
        if (rc != SEED_CALIBRATION_OK)
        {
            fprintf(stderr, "Error: closest-seed search failed (code %d).\n", rc);
            free(sampled_seeds);
            free(historical_draws);
            free(filtered);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        if (strcmp(analytics_format, "json") == 0)
        {
            FILE *closest_seed_out = stdout;
            if (closest_seed_output_file)
            {
                closest_seed_out = fopen(closest_seed_output_file, "w");
                if (!closest_seed_out)
                {
                    fprintf(stderr, "Error: Cannot open '%s' for writing.\n",
                            closest_seed_output_file);
                    free(sampled_seeds);
                    free(historical_draws);
                    free(filtered);
                    registry_destroy(registry);
                    config_free(&cfg);
                    return 1;
                }
            }
            print_closest_seed_json(closest_seed_out, &result, selected->name, period_from,
                                    period_to, &selected->info, &closest_reference);
            if (closest_seed_out != stdout)
            {
                fclose(closest_seed_out);
                printf("Closest-seed output written to %s\n", closest_seed_output_file);
            }
        }
        else if (strcmp(analytics_format, "csv") == 0)
        {
            FILE *closest_seed_out = stdout;
            if (closest_seed_output_file)
            {
                closest_seed_out = fopen(closest_seed_output_file, "w");
                if (!closest_seed_out)
                {
                    fprintf(stderr, "Error: Cannot open '%s' for writing.\n",
                            closest_seed_output_file);
                    free(sampled_seeds);
                    free(historical_draws);
                    free(filtered);
                    registry_destroy(registry);
                    config_free(&cfg);
                    return 1;
                }
            }
            print_closest_seed_csv(closest_seed_out, &result);
            if (closest_seed_out != stdout)
            {
                fclose(closest_seed_out);
                printf("Closest-seed output written to %s\n", closest_seed_output_file);
            }
        }
        else
        {
            FILE *closest_seed_out = stdout;
            if (closest_seed_output_file)
            {
                closest_seed_out = fopen(closest_seed_output_file, "w");
                if (!closest_seed_out)
                {
                    fprintf(stderr, "Error: Cannot open '%s' for writing.\n",
                            closest_seed_output_file);
                    free(sampled_seeds);
                    free(historical_draws);
                    free(filtered);
                    registry_destroy(registry);
                    config_free(&cfg);
                    return 1;
                }
            }
            print_closest_seed_table(closest_seed_out, &result);
            if (closest_seed_out != stdout)
            {
                fclose(closest_seed_out);
                printf("Closest-seed output written to %s\n", closest_seed_output_file);
            }
        }

        if (gui)
        {
            HistoricalDraw *simulated = calloc((size_t)filtered_count, sizeof(HistoricalDraw));
            if (simulated)
            {
                for (int i = 0; i < filtered_count; i++)
                {
                    simulated[i] = filtered[i];
                    generate_draw_seeded(selected->info.main_count, selected->info.main_min,
                                         selected->info.main_max, selected->info.extra_count,
                                         selected->info.extra_min, selected->info.extra_max,
                                         derive_draw_seed(result.best.seed, i),
                                         &simulated[i].result, silent_callback);
                }

                FrequencyReport real_report;
                FrequencyReport sim_report;
                FrequencyReport rank10_report;
                int has_rank10 = 0;
                if (analytics_compute_frequency(filtered, filtered_count, selected->info.main_min,
                                                selected->info.main_max,
                                                &real_report) == VALIDATE_OK &&
                    analytics_compute_frequency(simulated, filtered_count, selected->info.main_min,
                                                selected->info.main_max,
                                                &sim_report) == VALIDATE_OK)
                {
                    if (result.top_candidate_count >= 10)
                    {
                        for (int i = 0; i < filtered_count; i++)
                        {
                            simulated[i] = filtered[i];
                            generate_draw_seeded(selected->info.main_count, selected->info.main_min,
                                                 selected->info.main_max,
                                                 selected->info.extra_count,
                                                 selected->info.extra_min, selected->info.extra_max,
                                                 derive_draw_seed(result.top_candidates[9].seed, i),
                                                 &simulated[i].result, silent_callback);
                        }

                        if (analytics_compute_frequency(
                                simulated, filtered_count, selected->info.main_min,
                                selected->info.main_max, &rank10_report) == VALIDATE_OK)
                        {
                            has_rank10 = 1;
                        }
                    }

                    snprintf(real_report.from_date, sizeof(real_report.from_date), "%s",
                             period_from);
                    snprintf(real_report.to_date, sizeof(real_report.to_date), "%s", period_to);
                    snprintf(sim_report.from_date, sizeof(sim_report.from_date), "%s", period_from);
                    snprintf(sim_report.to_date, sizeof(sim_report.to_date), "%s", period_to);
                    if (has_rank10)
                    {
                        snprintf(rank10_report.from_date, sizeof(rank10_report.from_date), "%s",
                                 period_from);
                        snprintf(rank10_report.to_date, sizeof(rank10_report.to_date), "%s",
                                 period_to);
                    }

                    if (gui_render_frequency_overlay_stacked_2d(
                            selected->name, &real_report, &sim_report,
                            has_rank10 ? &rank10_report : NULL, dark_mode) != 0)
                    {
                        fprintf(stderr,
                                "Warning: Failed to render stacked 2D overlay visualization.\n");
                    }
                }
                free(simulated);
            }
            else
            {
                fprintf(
                    stderr,
                    "Warning: Out of memory; skipping closest-seed GUI overlay visualization.\n");
            }
        }

        free(sampled_seeds);
        free(historical_draws);
        free(filtered);
        registry_destroy(registry);
        config_free(&cfg);
        return 0;
    }

    if (period_draw_count_mode)
    {
        HistoricalDraw *historical_draws = calloc(ANALYTICS_MAX_DRAWS, sizeof(HistoricalDraw));
        HistoricalDraw *filtered = calloc(ANALYTICS_MAX_DRAWS, sizeof(HistoricalDraw));
        int draw_count = 0;
        int filtered_count = 0;

        if (!historical_draws || !filtered)
        {
            fprintf(stderr, "Error: Out of memory while preparing draw-count buffers.\n");
            free(historical_draws);
            free(filtered);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        int load_rc = VALIDATE_OK;
        if (historical_csv_from_cli)
        {
            load_rc =
                analytics_load_historical_csv(historical_csv, historical_draws, ANALYTICS_MAX_DRAWS,
                                              &draw_count, &selected->info);
        }
        else
        {
            load_rc = analytics_load_historical_db_snapshot(selected->name, NULL, historical_draws,
                                                            ANALYTICS_MAX_DRAWS, &draw_count,
                                                            &selected->info);
        }

        if (load_rc != VALIDATE_OK)
        {
            free(historical_draws);
            free(filtered);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        if (analytics_filter_period(historical_draws, draw_count, period_from, period_to, filtered,
                                    &filtered_count) != VALIDATE_OK)
        {
            free(historical_draws);
            free(filtered);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        if (strcmp(analytics_format, "json") == 0)
        {
            printf("{\n");
            printf("  \"mode\": \"draw-count-period\",\n");
            printf("  \"game\": ");
            fprint_json_string(stdout, selected->name);
            printf(",\n  \"period\": {\"from\": ");
            fprint_json_string(stdout, period_from);
            printf(", \"to\": ");
            fprint_json_string(stdout, period_to);
            printf("},\n");
            printf("  \"draw_count\": %d\n", filtered_count);
            printf("}\n");
        }
        else if (strcmp(analytics_format, "csv") == 0)
        {
            printf("section,key,value,extra\n");
            printf("summary,mode,draw-count-period,\n");
            printf("summary,game,%s,\n", selected->name);
            printf("summary,from,%s,\n", period_from);
            printf("summary,to,%s,\n", period_to);
            printf("summary,draw_count,%d,\n", filtered_count);
        }
        else
        {
            printf("Draw count for period\n");
            printf("game,%s\n", selected->name);
            printf("from,%s\n", period_from);
            printf("to,%s\n", period_to);
            printf("draw_count,%d\n", filtered_count);
        }

        free(historical_draws);
        free(filtered);
        registry_destroy(registry);
        config_free(&cfg);
        return 0;
    }

    if (analytics_mode_count > 0)
    {
        HistoricalDraw *historical_draws = calloc(ANALYTICS_MAX_DRAWS, sizeof(HistoricalDraw));
        HistoricalDraw *filtered = calloc(ANALYTICS_MAX_DRAWS, sizeof(HistoricalDraw));
        AnalyticsDrawRecord *dq_records = calloc(ANALYTICS_MAX_DRAWS, sizeof(AnalyticsDrawRecord));
        int draw_count = 0;
        int filtered_count = 0;

        if (!historical_draws || !filtered || !dq_records)
        {
            fprintf(stderr, "Error: Out of memory while preparing analytics buffers.\n");
            free(historical_draws);
            free(filtered);
            free(dq_records);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        int load_rc = VALIDATE_OK;
        if (historical_csv_from_cli)
        {
            load_rc =
                analytics_load_historical_csv(historical_csv, historical_draws, ANALYTICS_MAX_DRAWS,
                                              &draw_count, &selected->info);
        }
        else
        {
            load_rc = analytics_load_historical_db_snapshot(selected->name, NULL, historical_draws,
                                                            ANALYTICS_MAX_DRAWS, &draw_count,
                                                            &selected->info);
        }

        if (load_rc != VALIDATE_OK)
        {
            free(historical_draws);
            free(filtered);
            free(dq_records);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        /* Default period to full file range when --historical-csv is used without --from/--to */
        static char auto_from[11], auto_to[11];
        if (!period_from && historical_csv_from_cli && draw_count > 0)
        {
            snprintf(auto_from, sizeof(auto_from), "%s", historical_draws[0].draw_date);
            snprintf(auto_to, sizeof(auto_to), "%s", historical_draws[draw_count - 1].draw_date);
            period_from = auto_from;
            period_to = auto_to;
        }

        if (analytics_filter_period(historical_draws, draw_count, period_from, period_to, filtered,
                                    &filtered_count) != VALIDATE_OK)
        {
            free(historical_draws);
            free(filtered);
            free(dq_records);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        for (int i = 0; i < filtered_count; i++)
        {
            dq_records[i].draw_date = filtered[i].draw_date;
            dq_records[i].main_count = filtered[i].result.main_count;
            dq_records[i].extra_count = filtered[i].result.extra_count;
            memset(dq_records[i].main_numbers, 0, sizeof(dq_records[i].main_numbers));
            memset(dq_records[i].extra_numbers, 0, sizeof(dq_records[i].extra_numbers));

            for (int j = 0; j < filtered[i].result.main_count && j < ANALYTICS_MAX_MAIN_NUMBERS;
                 j++)
                dq_records[i].main_numbers[j] = filtered[i].result.main_numbers[j];
            for (int j = 0; j < filtered[i].result.extra_count && j < ANALYTICS_MAX_EXTRA_NUMBERS;
                 j++)
                dq_records[i].extra_numbers[j] = filtered[i].result.extra_numbers[j];
        }

        AnalyticsDataQualityReport dq_report;
        if (analytics_data_quality_evaluate(
                dq_records, filtered_count, period_from, period_to, selected->info.main_count,
                selected->info.main_min, selected->info.main_max, selected->info.extra_count,
                selected->info.extra_min, selected->info.extra_max, &dq_report) != VALIDATE_OK)
        {
            free(historical_draws);
            free(filtered);
            free(dq_records);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        char dq_cli[256];
        analytics_data_quality_format_cli(&dq_report, dq_cli, sizeof(dq_cli));
        printf("%s\n", dq_cli);

        if (analytics_data_quality_has_severe_issues(&dq_report))
        {
            fprintf(stderr, "Error: Severe data integrity issues detected in selected period.\n");
            free(historical_draws);
            free(filtered);
            free(dq_records);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        if (analytics_frequency)
        {
            FrequencyReport report;
            if (analytics_compute_frequency(filtered, filtered_count, selected->info.main_min,
                                            selected->info.main_max, &report) != VALIDATE_OK)
            {
                free(historical_draws);
                free(filtered);
                free(dq_records);
                registry_destroy(registry);
                config_free(&cfg);
                return 1;
            }

            snprintf(report.from_date, sizeof(report.from_date), "%s", period_from);
            report.from_date[sizeof(report.from_date) - 1] = '\0';
            snprintf(report.to_date, sizeof(report.to_date), "%s", period_to);
            report.to_date[sizeof(report.to_date) - 1] = '\0';

            if (gui && strcmp(gui_mode, "3D") == 0)
            {
                printf("[GUI 3D] OpenGL frequency visualization\n");
                if (gui_render_frequency_3d(selected->name, &report, dark_mode) != 0)
                    analytics_print_frequency_gui_3d_matlab(&report);
            }
            else if (gui)
            {
                printf("[GUI 2D] SDL frequency visualization\n");
                if (gui_render_frequency_2d(selected->name, &report, dark_mode) != 0)
                    analytics_print_frequency_gui_2d(&report);
            }
            else if (strcmp(analytics_format, "json") == 0)
                analytics_print_frequency_json(&report);
            else if (strcmp(analytics_format, "csv") == 0)
                analytics_print_frequency_csv(&report);
            else
                analytics_print_frequency_table(&report);

            if (analytics_explain)
            {
                if (strcmp(analytics_format, "json") == 0)
                {
                    printf("{\"explain\":{\"mode\":\"frequency\",\"formula\":\"count(number)/"
                           "draws\",\"period\":\"inclusive\"}}\n");
                }
                else
                {
                    printf("Explain: frequency percentage = count(number) / draws in inclusive "
                           "period [%s, %s].\n",
                           period_from, period_to);
                }
            }
        }
        else if (analytics_barometer)
        {
            BarometerReport report;
            if (analytics_compute_barometer(filtered, filtered_count, selected->info.main_min,
                                            selected->info.main_max, selected->info.main_count,
                                            &report) != VALIDATE_OK)
            {
                free(historical_draws);
                free(filtered);
                free(dq_records);
                registry_destroy(registry);
                config_free(&cfg);
                return 1;
            }

            snprintf(report.from_date, sizeof(report.from_date), "%s", period_from);
            report.from_date[sizeof(report.from_date) - 1] = '\0';
            snprintf(report.to_date, sizeof(report.to_date), "%s", period_to);
            report.to_date[sizeof(report.to_date) - 1] = '\0';

            if (gui && strcmp(gui_mode, "3D") == 0)
            {
                printf("[GUI 3D] OpenGL barometer visualization\n");
                if (gui_render_barometer_3d(selected->name, &report, dark_mode) != 0)
                    analytics_print_barometer_gui_3d_matlab(&report);
            }
            else if (gui)
            {
                printf("[GUI 2D] SDL barometer visualization\n");
                if (gui_render_barometer_2d(selected->name, &report, dark_mode) != 0)
                    analytics_print_barometer_gui_2d(&report);
            }
            else if (strcmp(analytics_format, "json") == 0)
                analytics_print_barometer_json(&report);
            else if (strcmp(analytics_format, "csv") == 0)
                analytics_print_barometer_csv(&report);
            else
                analytics_print_barometer_table(&report);

            if (analytics_explain)
            {
                if (strcmp(analytics_format, "json") == 0)
                {
                    printf("{\"explain\":{\"mode\":\"barometer\",\"formula\":\"observed_gap/"
                           "expected_interval\",\"expected_interval\":\"population/"
                           "picks_per_draw\"}}\n");
                }
                else
                {
                    printf("Explain: barometer factor = observed_gap / expected_interval, where "
                           "expected_interval = population / picks_per_draw.\n");
                }
            }
        }
        else if (analytics_hot_cold)
        {
            HotColdReport report;
            if (analytics_compute_hot_cold(filtered, filtered_count, selected->info.main_min,
                                           selected->info.main_max, analytics_top,
                                           &report) != VALIDATE_OK)
            {
                free(historical_draws);
                free(filtered);
                free(dq_records);
                registry_destroy(registry);
                config_free(&cfg);
                return 1;
            }

            snprintf(report.from_date, sizeof(report.from_date), "%s", period_from);
            report.from_date[sizeof(report.from_date) - 1] = '\0';
            snprintf(report.to_date, sizeof(report.to_date), "%s", period_to);
            report.to_date[sizeof(report.to_date) - 1] = '\0';

            if (gui && strcmp(gui_mode, "3D") == 0)
            {
                printf("[GUI 3D] OpenGL hot/cold visualization\n");
                if (gui_render_hot_cold_3d(selected->name, &report, dark_mode) != 0)
                    analytics_print_hot_cold_gui_3d_matlab(&report);
            }
            else if (gui)
            {
                printf("[GUI 2D] SDL hot/cold visualization\n");
                if (gui_render_hot_cold_2d(selected->name, &report, dark_mode) != 0)
                    analytics_print_hot_cold_gui_2d(&report);
            }
            else if (strcmp(analytics_format, "json") == 0)
                analytics_print_hot_cold_json(&report);
            else if (strcmp(analytics_format, "csv") == 0)
                analytics_print_hot_cold_csv(&report);
            else
                analytics_print_hot_cold_table(&report);

            if (analytics_explain)
            {
                if (strcmp(analytics_format, "json") == 0)
                {
                    printf("{\"explain\":{\"mode\":\"hot-cold\",\"rule\":\"hot=highest frequency, "
                           "cold=lowest frequency\",\"tie_break\":\"ascending number\"}}\n");
                }
                else
                {
                    printf("Explain: hot numbers are ranked by descending frequency; cold numbers "
                           "by ascending frequency; ties use ascending number.\n");
                }
            }
        }

        free(historical_draws);
        free(filtered);
        free(dq_records);
        registry_destroy(registry);
        config_free(&cfg);
        return 0;
    }

    if (sim_historical_csv_output)
    {
        /* Default start date to today if --from not given */
        static char sim_from_buf[16];
        if (!period_from)
        {
            time_t now = time(NULL);
            struct tm *tm_now = localtime(&now);
            snprintf(sim_from_buf, sizeof(sim_from_buf), "%04d-%02d-%02d",
                     (tm_now->tm_year + 1900) % 10000, (tm_now->tm_mon + 1) % 100,
                     tm_now->tm_mday % 100);
            period_from = sim_from_buf;
        }

        FILE *hcsv = fopen(sim_historical_csv_output, "w");
        if (!hcsv)
        {
            fprintf(stderr, "Error: Cannot open '%s' for writing.\n", sim_historical_csv_output);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        /* Parse start date from --from */
        int sy = 2025, sm = 1, sd = 1;
        sscanf(period_from, "%d-%d-%d", &sy, &sm, &sd);

        fprintf(hcsv, "draw_date,main_numbers,extra_numbers\n");
        for (int i = 0; i < draws; i++)
        {
            LotteryResult result;

            if (use_seed)
                combogen_set_forced_seed(derive_draw_seed(seed_value, i));
            else
                combogen_clear_forced_seed();

            selected->draw(&result, silent_callback);
            if (sorted_numbers)
                sort_lottery_result_numbers(&result);

            /* Advance date by i days from start */
            struct tm t = {0};
            t.tm_year = sy - 1900;
            t.tm_mon = sm - 1;
            t.tm_mday = sd + i;
            mktime(&t);

            char date_buf[16];
            snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d", (t.tm_year + 1900) % 10000,
                     (t.tm_mon + 1) % 100, t.tm_mday % 100);

            fprintf(hcsv, "%s,", date_buf);
            for (int j = 0; j < result.main_count; j++)
                fprintf(hcsv, "%s%d", j ? " " : "", result.main_numbers[j]);
            fprintf(hcsv, ",");
            for (int j = 0; j < result.extra_count; j++)
                fprintf(hcsv, "%s%d", j ? " " : "", result.extra_numbers[j]);
            fprintf(hcsv, "\n");
        }
        combogen_clear_forced_seed();
        fclose(hcsv);
        printf("Simulated historical CSV written to %s (%d draws from %s).\n",
               sim_historical_csv_output, draws, period_from);
        registry_destroy(registry);
        config_free(&cfg);
        return 0;
    }

    if (simulation_analytics)
    {
        LotteryResult *results = (LotteryResult *)calloc((size_t)draws, sizeof(LotteryResult));
        SimulationAnalyticsCoreReport core_report;
        SimulationAnalyticsAdvancedReport advanced_report;
        SimulationAnalyticsMetadata metadata;

        if (!results)
        {
            log_error("Failed to allocate memory for simulation draws");
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        for (int i = 0; i < draws; i++)
        {
            if (use_seed)
                combogen_set_forced_seed(derive_draw_seed(seed_value, i));
            else
                combogen_clear_forced_seed();

            selected->draw(&results[i], silent_callback);
        }
        combogen_clear_forced_seed();

        if (simulation_analytics_core_aggregate(results, draws, selected->info.main_min,
                                                selected->info.main_max, selected->info.main_count,
                                                &core_report) != 0)
        {
            free(results);
            log_error("Failed to compute core simulation analytics");
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        if (simulation_analytics_advanced_compute(results, draws, selected->info.main_min,
                                                  selected->info.main_max, analytics_top,
                                                  &advanced_report) != 0)
        {
            free(results);
            log_error("Failed to compute advanced simulation analytics");
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        if (simulation_analytics_metadata_init(&metadata, selected->name, draws, use_seed,
                                               seed_value, "simulation") != 0)
        {
            free(results);
            log_error("Failed to initialize simulation metadata");
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        if (export_format)
        {
            int export_rc = -1;
            if (strcmp(export_format, "json") == 0)
                export_rc = simulation_analytics_export_json_file(export_filename, &metadata,
                                                                  &core_report, &advanced_report);
            else if (strcmp(export_format, "csv") == 0)
                export_rc = simulation_analytics_export_csv_file(export_filename, &metadata,
                                                                 &core_report, &advanced_report);

            if (export_rc != 0)
            {
                free(results);
                log_error("Simulation analytics export failed");
                registry_destroy(registry);
                config_free(&cfg);
                return 1;
            }
            printf("Simulation analytics exported to %s (%s).\n", export_filename, export_format);
        }
        else
        {
            if (strcmp(analytics_format, "json") == 0)
                print_simulation_analytics_json(&metadata, &core_report, &advanced_report);
            else if (strcmp(analytics_format, "csv") == 0)
                print_simulation_analytics_csv(&core_report, &advanced_report);
            else
                print_simulation_analytics_table(&core_report, &advanced_report);
        }

        free(results);
        registry_destroy(registry);
        config_free(&cfg);
        return 0;
    }

    /* If --validate-only flag is set, exit after successful validation */
    if (validate_only)
    {
        printf("Configuration is valid:\n");
        printf("  Game: %s\n", selected->name);
        printf("  Draws: %d\n", draws);
        if (export_format)
        {
            printf("  Export: %s -> %s\n", export_format, export_filename);
        }
        if (animate)
        {
            printf("  Mode: Animated CLI\n");
        }
        else if (gui)
        {
            printf("  Mode: GUI (%s)\n", gui_mode);
        }
        else
        {
            printf("  Mode: CLI\n");
        }
        if (reload_plugin)
        {
            printf("  Plugin reload: enabled\n");
        }
        if (sorted_numbers)
        {
            printf("  Output ordering: sorted ascending\n");
        }
        if (use_seed)
        {
            printf("  Seed: 0x%016llx\n", (unsigned long long)seed_value);
        }
        registry_destroy(registry);
        config_free(&cfg);
        return 0;
    }

    /* ---------------------------------------------------------
       GUI MODE (always 1 draw)
       --------------------------------------------------------- */
    if (gui)
    {
        log_debug("Launching %s GUI mode", gui_mode);
        if (sorted_numbers)
            log_warn("Ignoring --sorted in GUI mode.");

        /* Resolve dark_mode: CLI override > config file > system auto-detect */
        int resolved_dark_mode = dark_mode;
        if (resolved_dark_mode == -1 && cfg.dark_mode)
        {
            if (strcasecmp(cfg.dark_mode, "on") == 0)
                resolved_dark_mode = 1;
            else if (strcasecmp(cfg.dark_mode, "off") == 0)
                resolved_dark_mode = 0;
            /* else stay at -1 for auto */
        }
        if (resolved_dark_mode == -1)
        {
            resolved_dark_mode = theme_detect_system_dark_mode();
        }

        if (strcmp(gui_mode, "3D") == 0)
        {
#if OPEN_LOTTO_ENABLE_OPENGL
            gui_run_opengl(selected->name, &selected->info, debug_overlay, resolved_dark_mode);
#else
            log_error("3D GUI backend is not available in this build. Use '--gui 2D'.");
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
#endif
        }
        else /* Default to 2D */
        {
            gui_run(selected->name, &selected->info, resolved_dark_mode);
        }

        registry_destroy(registry);
        config_free(&cfg);
        return 0;
    }

    /* ---------------------------------------------------------
       CLI MODE (single or multiple draws)
       --------------------------------------------------------- */
    log_debug("Running CLI mode with %d draw(s)", draws);

    if (export_format)
    {
        /* Export mode: collect all results then write to file */
        LotteryResult *results = (LotteryResult *)malloc(sizeof(LotteryResult) * draws);
        if (!results)
        {
            log_error("Failed to allocate memory for results");
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        for (int i = 0; i < draws; i++)
        {
            if (use_seed)
                combogen_set_forced_seed(derive_draw_seed(seed_value, i));
            else
                combogen_clear_forced_seed();

            selected->draw(&results[i], silent_callback);
            if (sorted_numbers)
                sort_lottery_result_numbers(&results[i]);
        }

        combogen_clear_forced_seed();

        /* Export to file based on format */
        int export_result = 0;
        if (strcmp(export_format, "csv") == 0)
        {
            export_result =
                export_results_csv_file(export_filename, selected->name, results, draws);
        }
        else if (strcmp(export_format, "json") == 0)
        {
            export_result =
                export_results_json_file(export_filename, selected->name, results, draws);
        }

        free(results);

        if (export_result != 0)
        {
            log_error("Export failed");
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }
    }
    else
    {
        /* Normal CLI mode: print as we go */
        for (int i = 0; i < draws; i++)
        {
            LotteryResult result;

            if (use_seed)
                combogen_set_forced_seed(derive_draw_seed(seed_value, i));
            else
                combogen_clear_forced_seed();

            selected->draw(&result, silent_callback);
            if (sorted_numbers)
                sort_lottery_result_numbers(&result);

            if (animate)
            {
                printf("Draw %d:\n", i + 1);
                animate_numbers(&selected->info, &result);
                printf("\n");
            }
            else
            {
                print_draw_result(selected->name, i + 1, &result);
            }
        }

        combogen_clear_forced_seed();
    }

    log_debug("Cleaning up resources");
    registry_destroy(registry);
    config_free(&cfg);
    return 0;
}
