/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "../include/analytics_data_quality.h"
#include "test.h"
#include <string.h>

static AnalyticsDrawRecord make_record(const char *date, int a, int b, int c, int d, int e, int f)
{
    AnalyticsDrawRecord r;
    memset(&r, 0, sizeof(r));
    r.draw_date = date;
    r.main_count = 6;
    r.main_numbers[0] = a;
    r.main_numbers[1] = b;
    r.main_numbers[2] = c;
    r.main_numbers[3] = d;
    r.main_numbers[4] = e;
    r.main_numbers[5] = f;
    r.extra_count = 0;
    return r;
}

static void test_clean_dataset(void)
{
    test_suite("Analytics DQ - clean dataset");

    AnalyticsDrawRecord records[] = {
        make_record("2025-01-05", 1, 2, 3, 4, 5, 6),
        make_record("2025-01-12", 7, 8, 9, 10, 11, 12),
    };

    AnalyticsDataQualityReport report;
    int rc = analytics_data_quality_evaluate(records, 2, "2025-01-01", "2025-12-31", 6, 1, 49, 0,
                                             0, 0, &report);

    assert_equals(rc, VALIDATE_OK, "evaluation succeeds");
    assert_equals(report.malformed_rows, 0, "no malformed rows");
    assert_equals(report.duplicate_draws, 0, "no duplicate draws");
    assert_equals(report.out_of_range_dates, 0, "no out-of-period rows");
    assert_equals(report.game_rule_inconsistencies, 0, "no rule inconsistencies");
    assert_equals(analytics_data_quality_has_severe_issues(&report), 0,
                  "no severe integrity issues");
}

static void test_problematic_dataset(void)
{
    test_suite("Analytics DQ - problematic dataset");

    AnalyticsDrawRecord records[] = {
        make_record("2025-01-05", 1, 2, 3, 4, 5, 6),
        make_record("2025-01-05", 1, 2, 3, 4, 5, 6), /* duplicate */
        make_record("2025-13-12", 7, 8, 9, 10, 11, 12),
        make_record("2024-12-31", 7, 8, 9, 10, 11, 12),
        make_record("2025-02-01", 0, 2, 3, 4, 5, 6), /* out-of-range number */
    };

    AnalyticsDataQualityReport report;
    int rc = analytics_data_quality_evaluate(records, 5, "2025-01-01", "2025-12-31", 6, 1, 49, 0,
                                             0, 0, &report);

    assert_equals(rc, VALIDATE_OK, "evaluation succeeds with reportable issues");
    assert_equals(report.duplicate_draws, 1, "duplicate draw is detected");
    assert_equals(report.malformed_rows, 1, "malformed date is detected");
    assert_equals(report.out_of_range_dates, 2,
                  "out-of-period rows are detected (invalid date and pre-period row)");
    assert_equals(report.game_rule_inconsistencies, 1, "rule inconsistency is detected");
    assert_equals(analytics_data_quality_has_severe_issues(&report), 1,
                  "severe integrity issues are flagged");
}

static void test_output_formatters(void)
{
    test_suite("Analytics DQ - output formatters");

    AnalyticsDataQualityReport report;
    report.total_rows = 10;
    report.malformed_rows = 1;
    report.duplicate_draws = 2;
    report.out_of_range_dates = 3;
    report.game_rule_inconsistencies = 4;

    char cli[256];
    char gui[128];
    analytics_data_quality_format_cli(&report, cli, sizeof(cli));
    analytics_data_quality_format_gui(&report, gui, sizeof(gui));

    assert_true(strstr(cli, "rows=10") != NULL, "CLI formatter includes row count");
    assert_true(strstr(cli, "duplicates=2") != NULL, "CLI formatter includes duplicate count");
    assert_true(strstr(gui, "DQ M:1") != NULL, "GUI formatter includes malformed count");
    assert_true(strstr(gui, "D:2") != NULL, "GUI formatter includes duplicate count");
}

int main(void)
{
    test_clean_dataset();
    test_problematic_dataset();
    test_output_formatters();

    test_summary();
}
