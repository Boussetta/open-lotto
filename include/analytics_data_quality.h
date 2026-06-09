/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef ANALYTICS_DATA_QUALITY_H
#define ANALYTICS_DATA_QUALITY_H

#include "validate.h"
#include <stddef.h>

#define ANALYTICS_MAX_MAIN_NUMBERS 10
#define ANALYTICS_MAX_EXTRA_NUMBERS 5

typedef struct
{
    const char *draw_date; /* YYYY-MM-DD */
    int main_numbers[ANALYTICS_MAX_MAIN_NUMBERS];
    int main_count;
    int extra_numbers[ANALYTICS_MAX_EXTRA_NUMBERS];
    int extra_count;
} AnalyticsDrawRecord;

typedef struct
{
    int malformed_rows;
    int duplicate_draws;
    int out_of_range_dates;
    int game_rule_inconsistencies;
    int total_rows;
} AnalyticsDataQualityReport;

/**
 * Evaluate historical draw quality for analytics calculations.
 *
 * Rules checked:
 * - Date format validity and optional period bounds
 * - Duplicate draws (same date + same numbers)
 * - Game-rule consistency (counts and number ranges)
 */
int analytics_data_quality_evaluate(const AnalyticsDrawRecord *records, int record_count,
                                    const char *period_from, const char *period_to,
                                    int expected_main_count, int main_min, int main_max,
                                    int expected_extra_count, int extra_min, int extra_max,
                                    AnalyticsDataQualityReport *out_report);

/**
 * Returns non-zero when severe integrity issues exist.
 */
int analytics_data_quality_has_severe_issues(const AnalyticsDataQualityReport *report);

/**
 * Render concise diagnostics for regular CLI output.
 */
void analytics_data_quality_format_cli(const AnalyticsDataQualityReport *report, char *out,
                                       size_t out_size);

/**
 * Render concise diagnostics text for GUI overlays (2D/3D).
 */
void analytics_data_quality_format_gui(const AnalyticsDataQualityReport *report, char *out,
                                       size_t out_size);

#endif /* ANALYTICS_DATA_QUALITY_H */
