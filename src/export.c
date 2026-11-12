/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

/**
 * @file export.c
 * @brief Implement lottery result export to CSV and JSON formats
 */

#include "export.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Write s to f as the body of a JSON string (escaping quotes,
 *        backslashes and control characters).
 */
static int json_write_escaped(FILE *f, const char *s)
{
    if (!s)
        return 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
    {
        int rc;
        switch (*p)
        {
        case '"':
            rc = fputs("\\\"", f);
            break;
        case '\\':
            rc = fputs("\\\\", f);
            break;
        case '\n':
            rc = fputs("\\n", f);
            break;
        case '\r':
            rc = fputs("\\r", f);
            break;
        case '\t':
            rc = fputs("\\t", f);
            break;
        default:
            if (*p < 0x20)
                rc = fprintf(f, "\\u%04x", *p);
            else
                rc = fputc(*p, f);
            break;
        }
        if (rc < 0)
            return -1;
    }
    return 0;
}

/**
 * @brief Export a single lottery result to CSV format
 *
 * CSV format: main_numbers + extra_numbers (space-separated)
 *
 * Example:
 *   5 23 41 2 19 + 11 5
 */
static int export_result_csv(FILE *file, const LotteryResult *result)
{
    if (!file || !result)
    {
        log_error("export_result_csv: Invalid arguments");
        return -1;
    }

    /* Write main numbers (space-separated) */
    for (int i = 0; i < result->main_count; i++)
    {
        if (fprintf(file, "%d", result->main_numbers[i]) < 0)
        {
            log_error("Failed to write to CSV file");
            return -1;
        }
        if (i < result->main_count - 1)
        {
            if (fprintf(file, " ") < 0)
            {
                log_error("Failed to write to CSV file");
                return -1;
            }
        }
    }

    /* Write separator and extra numbers if present */
    if (result->extra_count > 0)
    {
        if (fprintf(file, " + ") < 0)
        {
            log_error("Failed to write to CSV file");
            return -1;
        }

        /* Write extra numbers (space-separated) */
        for (int i = 0; i < result->extra_count && i < MAX_EXTRA_NUMBERS; i++)
        {
            if (fprintf(file, "%d", result->extra_numbers[i]) < 0)
            {
                log_error("Failed to write to CSV file");
                return -1;
            }
            if (i < result->extra_count - 1)
            {
                if (fprintf(file, " ") < 0)
                {
                    log_error("Failed to write to CSV file");
                    return -1;
                }
            }
        }
    }

    /* Write newline */
    if (fprintf(file, "\n") < 0)
    {
        log_error("Failed to write to CSV file");
        return -1;
    }

    return 0;
}

/**
 * @brief Export multiple lottery results to CSV file
 *
 * Creates a plain text CSV-style file with one draw per line:
 * main_numbers + extra_numbers
 */
int export_results_csv_file(const char *filename, const char *game_name,
                            const LotteryResult *results, int num_results)
{
    if (!filename || !game_name || !results || num_results <= 0)
    {
        log_error("export_results_csv_file: Invalid arguments");
        return -1;
    }

    FILE *file = fopen(filename, "w");
    if (!file)
    {
        log_error("Failed to open file for writing: %s", filename);
        return -1;
    }

    /* Write each result */
    for (int i = 0; i < num_results; i++)
    {
        if (export_result_csv(file, &results[i]) != 0)
        {
            log_error("Failed to export result %d", i + 1);
            fclose(file);
            return -1;
        }
    }

    if (fclose(file) != 0)
    {
        log_error("Failed to close file: %s", filename);
        return -1;
    }

    log_info("CSV export completed: %s (%d draws)", filename, num_results);
    return 0;
}

/**
 * @brief Export multiple lottery results to JSON file
 *
 * Creates a JSON file with an array of results.
 * Format:
 *   {
 *     "game": "Eurojackpot",
 *     "draws": [
 *       {
 *         "draw_number": 1,
 *         "main_numbers": [5, 23, 41, 2, 19],
 *         "extra_numbers": [11, 5]
 *       },
 *       ...
 *     ]
 *   }
 */
int export_results_json_file(const char *filename, const char *game_name,
                             const LotteryResult *results, int num_results)
{
    if (!filename || !game_name || !results || num_results <= 0)
    {
        log_error("export_results_json_file: Invalid arguments");
        return -1;
    }

    FILE *file = fopen(filename, "w");
    if (!file)
    {
        log_error("Failed to open file for writing: %s", filename);
        return -1;
    }

    /* Write JSON header */
    if (fputs("{\n  \"game\": \"", file) < 0 || json_write_escaped(file, game_name) < 0 ||
        fputs("\",\n  \"draws\": [\n", file) < 0)
    {
        log_error("Failed to write JSON header");
        fclose(file);
        return -1;
    }

    /* Write each result */
    for (int i = 0; i < num_results; i++)
    {
        const LotteryResult *result = &results[i];

        /* Opening brace and draw number */
        if (fprintf(file, "    {\n      \"draw_number\": %d,\n", i + 1) < 0)
        {
            log_error("Failed to write JSON object");
            fclose(file);
            return -1;
        }

        /* Main numbers array */
        if (fprintf(file, "      \"main_numbers\": [") < 0)
        {
            log_error("Failed to write JSON array");
            fclose(file);
            return -1;
        }

        for (int j = 0; j < result->main_count; j++)
        {
            if (fprintf(file, "%d", result->main_numbers[j]) < 0)
            {
                log_error("Failed to write JSON value");
                fclose(file);
                return -1;
            }
            if (j < result->main_count - 1)
            {
                if (fprintf(file, ", ") < 0)
                {
                    log_error("Failed to write JSON separator");
                    fclose(file);
                    return -1;
                }
            }
        }

        if (fprintf(file, "],\n") < 0)
        {
            log_error("Failed to write JSON array end");
            fclose(file);
            return -1;
        }

        /* Extra numbers array */
        if (fprintf(file, "      \"extra_numbers\": [") < 0)
        {
            log_error("Failed to write JSON array");
            fclose(file);
            return -1;
        }

        for (int j = 0; j < result->extra_count && j < MAX_EXTRA_NUMBERS; j++)
        {
            if (fprintf(file, "%d", result->extra_numbers[j]) < 0)
            {
                log_error("Failed to write JSON value");
                fclose(file);
                return -1;
            }
            if (j < result->extra_count - 1)
            {
                if (fprintf(file, ", ") < 0)
                {
                    log_error("Failed to write JSON separator");
                    fclose(file);
                    return -1;
                }
            }
        }

        if (fprintf(file, "]\n") < 0)
        {
            log_error("Failed to write JSON array end");
            fclose(file);
            return -1;
        }

        /* Closing brace */
        if (i < num_results - 1)
        {
            if (fprintf(file, "    },\n") < 0)
            {
                log_error("Failed to write JSON object end");
                fclose(file);
                return -1;
            }
        }
        else
        {
            if (fprintf(file, "    }\n") < 0)
            {
                log_error("Failed to write JSON object end");
                fclose(file);
                return -1;
            }
        }
    }

    /* Write JSON footer */
    if (fprintf(file, "  ]\n}\n") < 0)
    {
        log_error("Failed to write JSON footer");
        fclose(file);
        return -1;
    }

    if (fclose(file) != 0)
    {
        log_error("Failed to close file: %s", filename);
        return -1;
    }

    log_info("JSON export completed: %s (%d draws)", filename, num_results);
    return 0;
}
