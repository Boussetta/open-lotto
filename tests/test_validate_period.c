/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "../include/validate.h"
#include "test.h"

static void test_iso_date_validation(void)
{
    test_suite("Validation - ISO date format");

    assert_equals(validate_iso_date("2026-06-09"), VALIDATE_OK,
                  "valid ISO date passes validation");
    assert_equals(validate_iso_date("2024-02-29"), VALIDATE_OK,
                  "valid leap day passes validation");

    assert_not_equals(validate_iso_date("2026-13-01"), VALIDATE_OK,
                      "invalid month is rejected");
    assert_not_equals(validate_iso_date("2026-02-30"), VALIDATE_OK,
                      "invalid day is rejected");
    assert_not_equals(validate_iso_date("2023-02-29"), VALIDATE_OK,
                      "non-leap year february 29 is rejected");
    assert_not_equals(validate_iso_date("20260609"), VALIDATE_OK,
                      "non-ISO compact format is rejected");
}

static void test_period_validation(void)
{
    test_suite("Validation - Analytics period");

    assert_equals(validate_analytics_period("2025-01-01", "2025-12-31", NULL, NULL),
                  VALIDATE_OK, "valid period passes validation");

    assert_not_equals(validate_analytics_period("2025-12-31", "2025-01-01", NULL, NULL),
                      VALIDATE_OK, "inverted period is rejected");

    assert_not_equals(validate_analytics_period(NULL, "2025-01-01", NULL, NULL), VALIDATE_OK,
                      "missing --from value is rejected");
    assert_not_equals(validate_analytics_period("2025-01-01", NULL, NULL, NULL), VALIDATE_OK,
                      "missing --to value is rejected");
}

static void test_available_range_intersection(void)
{
    test_suite("Validation - Available range intersection");

    assert_equals(validate_analytics_period("2025-02-01", "2025-03-01", "2025-01-01",
                                            "2025-12-31"),
                  VALIDATE_OK, "requested period intersecting available range passes");

    assert_not_equals(validate_analytics_period("2024-01-01", "2024-12-31", "2025-01-01",
                                                "2025-12-31"),
                      VALIDATE_OK, "non-overlapping period before available range is rejected");

    assert_not_equals(validate_analytics_period("2026-01-01", "2026-12-31", "2025-01-01",
                                                "2025-12-31"),
                      VALIDATE_OK, "non-overlapping period after available range is rejected");
}

int main(void)
{
    test_iso_date_validation();
    test_period_validation();
    test_available_range_intersection();

    test_summary();
}
