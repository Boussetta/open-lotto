/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "analytics_data_quality.h"
#include <stdio.h>
#include <string.h>

static int numbers_equal(const int *a, const int *b, int count)
{
    for (int i = 0; i < count; i++)
    {
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}

static int record_numbers_in_range(const AnalyticsDrawRecord *r, int expected_main_count,
                                   int main_min, int main_max, int expected_extra_count,
                                   int extra_min, int extra_max)
{
    if (r->main_count != expected_main_count)
        return 0;
    if (r->extra_count != expected_extra_count)
        return 0;

    for (int i = 0; i < r->main_count; i++)
    {
        if (r->main_numbers[i] < main_min || r->main_numbers[i] > main_max)
            return 0;
    }

    for (int i = 0; i < r->extra_count; i++)
    {
        if (r->extra_numbers[i] < extra_min || r->extra_numbers[i] > extra_max)
            return 0;
    }

    return 1;
}

static int record_in_period(const AnalyticsDrawRecord *r, const char *period_from,
                            const char *period_to)
{
    if (!period_from || !period_to)
        return 1;

    if (strcmp(r->draw_date, period_from) < 0)
        return 0;
    if (strcmp(r->draw_date, period_to) > 0)
        return 0;

    return 1;
}

int analytics_data_quality_evaluate(const AnalyticsDrawRecord *records, int record_count,
                                    const char *period_from, const char *period_to,
                                    int expected_main_count, int main_min, int main_max,
                                    int expected_extra_count, int extra_min, int extra_max,
                                    AnalyticsDataQualityReport *out_report)
{
    if (!records || !out_report || record_count < 0)
        return VALIDATE_ERR_EMPTY;

    if (period_from || period_to)
    {
        if (!period_from || !period_to)
            return VALIDATE_ERR_EMPTY;
        if (validate_analytics_period(period_from, period_to, NULL, NULL) != VALIDATE_OK)
            return VALIDATE_ERR_INVALID_FORMAT;
    }

    memset(out_report, 0, sizeof(*out_report));
    out_report->total_rows = record_count;

    for (int i = 0; i < record_count; i++)
    {
        const AnalyticsDrawRecord *r = &records[i];

        if (validate_iso_date(r->draw_date) != VALIDATE_OK)
            out_report->malformed_rows++;

        if (!record_in_period(r, period_from, period_to))
            out_report->out_of_range_dates++;

        if (!record_numbers_in_range(r, expected_main_count, main_min, main_max,
                                     expected_extra_count, extra_min, extra_max))
            out_report->game_rule_inconsistencies++;

        for (int j = i + 1; j < record_count; j++)
        {
            const AnalyticsDrawRecord *other = &records[j];
            if (strcmp(r->draw_date, other->draw_date) != 0)
                continue;
            if (r->main_count != other->main_count || r->extra_count != other->extra_count)
                continue;
            if (!numbers_equal(r->main_numbers, other->main_numbers, r->main_count))
                continue;
            if (!numbers_equal(r->extra_numbers, other->extra_numbers, r->extra_count))
                continue;
            out_report->duplicate_draws++;
        }
    }

    return VALIDATE_OK;
}

int analytics_data_quality_has_severe_issues(const AnalyticsDataQualityReport *report)
{
    if (!report)
        return 1;

    return (report->malformed_rows > 0 || report->duplicate_draws > 0 ||
            report->game_rule_inconsistencies > 0);
}

void analytics_data_quality_format_cli(const AnalyticsDataQualityReport *report, char *out,
                                       size_t out_size)
{
    if (!out || out_size == 0)
        return;

    if (!report)
    {
        snprintf(out, out_size, "analytics diagnostics: unavailable");
        return;
    }

    snprintf(out, out_size,
             "analytics diagnostics | rows=%d malformed=%d duplicates=%d out_of_period=%d "
             "rule_mismatch=%d",
             report->total_rows, report->malformed_rows, report->duplicate_draws,
             report->out_of_range_dates, report->game_rule_inconsistencies);
}

void analytics_data_quality_format_gui(const AnalyticsDataQualityReport *report, char *out,
                                       size_t out_size)
{
    if (!out || out_size == 0)
        return;

    if (!report)
    {
        snprintf(out, out_size, "Diagnostics unavailable");
        return;
    }

    snprintf(out, out_size, "DQ M:%d D:%d O:%d R:%d", report->malformed_rows,
             report->duplicate_draws, report->out_of_range_dates,
             report->game_rule_inconsistencies);
}
