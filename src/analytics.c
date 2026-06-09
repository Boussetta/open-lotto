/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "analytics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_numbers(const char *field, int *out, int max_out)
{
    char buf[256];
    size_t len = strlen(field);
    if (len >= sizeof(buf))
        return -1;

    strcpy(buf, field);

    int count = 0;
    char *token = strtok(buf, " ");
    while (token)
    {
        if (count >= max_out)
            return -1;

        char *end = NULL;
        long val = strtol(token, &end, 10);
        if (!end || *end != '\0')
            return -1;

        out[count++] = (int)val;
        token = strtok(NULL, " ");
    }

    return count;
}

int analytics_load_historical_csv(const char *csv_path, HistoricalDraw *out_draws, int max_draws,
                                  int *out_count, const LotteryInfo *rules)
{
    if (!csv_path || !out_draws || !out_count || !rules)
        return VALIDATE_ERR_EMPTY;

    FILE *f = fopen(csv_path, "r");
    if (!f)
    {
        fprintf(stderr, "Error: Could not open historical CSV '%s'.\n", csv_path);
        return VALIDATE_ERR_FILE_INVALID;
    }

    char line[1024];
    if (!fgets(line, sizeof(line), f))
    {
        fclose(f);
        fprintf(stderr, "Error: Historical CSV '%s' is empty.\n", csv_path);
        return VALIDATE_ERR_FILE_INVALID;
    }

    /* Expected header: draw_date,main_numbers,extra_numbers */
    int count = 0;
    while (fgets(line, sizeof(line), f))
    {
        if (count >= max_draws)
        {
            fclose(f);
            fprintf(stderr, "Error: Historical CSV exceeds max supported draws (%d).\n",
                    max_draws);
            return VALIDATE_ERR_OUT_OF_RANGE;
        }

        char *nl = strchr(line, '\n');
        if (nl)
            *nl = '\0';

        char *date = strtok(line, ",");
        char *main_field = strtok(NULL, ",");
        char *extra_field = strtok(NULL, ",");

        if (!date || !main_field)
        {
            fclose(f);
            fprintf(stderr, "Error: Malformed historical CSV row.\n");
            return VALIDATE_ERR_INVALID_FORMAT;
        }

        if (!extra_field)
            extra_field = "";

        if (validate_iso_date(date) != VALIDATE_OK)
        {
            fclose(f);
            return VALIDATE_ERR_INVALID_FORMAT;
        }

        HistoricalDraw *d = &out_draws[count];
        memset(d, 0, sizeof(*d));
        strncpy(d->draw_date, date, sizeof(d->draw_date) - 1);
        d->draw_date[sizeof(d->draw_date) - 1] = '\0';

        d->result.main_count = parse_numbers(main_field, d->result.main_numbers, MAX_MAIN_NUMBERS);
        if (d->result.main_count != rules->main_count)
        {
            fclose(f);
            fprintf(stderr, "Error: Main number count mismatch in historical CSV row '%s'.\n", date);
            return VALIDATE_ERR_INVALID_FORMAT;
        }

        if (rules->extra_count > 0)
        {
            d->result.extra_count =
                parse_numbers(extra_field, d->result.extra_numbers, MAX_EXTRA_NUMBERS);
            if (d->result.extra_count != rules->extra_count)
            {
                fclose(f);
                fprintf(stderr,
                        "Error: Extra number count mismatch in historical CSV row '%s'.\n",
                        date);
                return VALIDATE_ERR_INVALID_FORMAT;
            }
        }
        else
        {
            d->result.extra_count = 0;
        }

        count++;
    }

    fclose(f);
    *out_count = count;
    return VALIDATE_OK;
}

int analytics_filter_period(const HistoricalDraw *draws, int draw_count, const char *from_date,
                            const char *to_date, HistoricalDraw *out_filtered,
                            int *out_filtered_count)
{
    if (!draws || !from_date || !to_date || !out_filtered || !out_filtered_count)
        return VALIDATE_ERR_EMPTY;

    if (validate_analytics_period(from_date, to_date, NULL, NULL) != VALIDATE_OK)
        return VALIDATE_ERR_INVALID_FORMAT;

    int out_count = 0;
    for (int i = 0; i < draw_count; i++)
    {
        if (strcmp(draws[i].draw_date, from_date) < 0)
            continue;
        if (strcmp(draws[i].draw_date, to_date) > 0)
            continue;
        out_filtered[out_count++] = draws[i];
    }

    *out_filtered_count = out_count;
    return VALIDATE_OK;
}

int analytics_compute_frequency(const HistoricalDraw *draws, int draw_count, int number_min,
                                int number_max, FrequencyReport *out_report)
{
    if (!draws || !out_report || draw_count < 0 || number_min < 0 || number_max >= 128 ||
        number_min > number_max)
    {
        return VALIDATE_ERR_INVALID_FORMAT;
    }

    memset(out_report, 0, sizeof(*out_report));
    out_report->total_draws = draw_count;
    out_report->number_min = number_min;
    out_report->number_max = number_max;

    for (int i = 0; i < draw_count; i++)
    {
        const LotteryResult *r = &draws[i].result;
        for (int j = 0; j < r->main_count; j++)
        {
            int n = r->main_numbers[j];
            if (n >= number_min && n <= number_max)
                out_report->counts[n]++;
        }
    }

    return VALIDATE_OK;
}

void analytics_print_frequency_table(const FrequencyReport *report)
{
    if (!report)
        return;

    printf("Frequency distribution (draws: %d)\n", report->total_draws);
    printf("number,count,percentage\n");
    int denominator = report->total_draws > 0 ? report->total_draws : 1;

    for (int n = report->number_min; n <= report->number_max; n++)
    {
        double pct = (100.0 * report->counts[n]) / (double)denominator;
        printf("%d,%d,%.4f\n", n, report->counts[n], pct);
    }
}

void analytics_print_frequency_csv(const FrequencyReport *report)
{
    analytics_print_frequency_table(report);
}

void analytics_print_frequency_json(const FrequencyReport *report)
{
    if (!report)
        return;

    int denominator = report->total_draws > 0 ? report->total_draws : 1;

    printf("{\n");
    printf("  \"draws\": %d,\n", report->total_draws);
    printf("  \"frequency\": [\n");

    for (int n = report->number_min; n <= report->number_max; n++)
    {
        double pct = (100.0 * report->counts[n]) / (double)denominator;
        printf("    {\"number\": %d, \"count\": %d, \"percentage\": %.4f}%s\n", n,
               report->counts[n], pct, (n == report->number_max) ? "" : ",");
    }

    printf("  ]\n");
    printf("}\n");
}

void analytics_print_frequency_gui_2d(const FrequencyReport *report)
{
    if (!report)
        return;

    printf("[GUI 2D] Frequency Distribution\n");
    int max_count = 1;
    for (int n = report->number_min; n <= report->number_max; n++)
    {
        if (report->counts[n] > max_count)
            max_count = report->counts[n];
    }

    for (int n = report->number_min; n <= report->number_max; n++)
    {
        int bar_len = (40 * report->counts[n]) / max_count;
        printf("%2d | ", n);
        for (int i = 0; i < bar_len; i++)
            putchar('#');
        printf(" (%d)\n", report->counts[n]);
    }
}

void analytics_print_frequency_gui_3d_matlab(const FrequencyReport *report)
{
    if (!report)
        return;

    printf("[GUI 3D] MatLab-style figure description\n");
    printf("figure;\n");
    printf("x = [%d:%d];\n", report->number_min, report->number_max);
    printf("y = [");
    for (int n = report->number_min; n <= report->number_max; n++)
    {
        printf("%d%s", report->counts[n], (n == report->number_max) ? "" : " ");
    }
    printf("];\n");
    printf("bar3(y); xlabel('Number Index'); ylabel('Series'); zlabel('Frequency');\n");
}
