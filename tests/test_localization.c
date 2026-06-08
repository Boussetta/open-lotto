/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "../include/localization.h"
#include "test.h"
#include <string.h>

static void test_known_locale_keys(void)
{
    const char *draw = localization_get("fr", LOCALIZE_DRAW);
    assert_true(strcmp(draw, "Tirage") == 0, "French locale provides translated Draw label");
}

static void test_unknown_locale_falls_back_to_english(void)
{
    const char *main_label = localization_get("es", LOCALIZE_MAIN);
    assert_true(strcmp(main_label, "Main") == 0, "Unknown locale falls back to English main label");
}

static void test_missing_locale_falls_back_to_english(void)
{
    const char *extra_label = localization_get(NULL, LOCALIZE_EXTRA);
    assert_true(strcmp(extra_label, "Extra") == 0, "NULL locale falls back to English");
}

int main(void)
{
    test_suite("Localization Tests");

    test_known_locale_keys();
    test_unknown_locale_falls_back_to_english();
    test_missing_locale_falls_back_to_english();

    test_summary();
}
