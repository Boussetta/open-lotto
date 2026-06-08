/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "open_lotto_api.h"
#include "test.h"
#include <stdio.h>

static int results_equal(const LotteryResult *a, const LotteryResult *b)
{
    if (a->main_count != b->main_count || a->extra_count != b->extra_count)
        return 0;

    for (int i = 0; i < a->main_count; i++)
    {
        if (a->main_numbers[i] != b->main_numbers[i])
            return 0;
    }

    for (int i = 0; i < a->extra_count; i++)
    {
        if (a->extra_numbers[i] != b->extra_numbers[i])
            return 0;
    }

    return 1;
}

int main(void)
{
    test_suite("Developer API Tests");

    OpenLottoDrawSpec valid_spec = {
        .main_count = 6,
        .main_min = 1,
        .main_max = 49,
        .extra_count = 1,
        .extra_min = 0,
        .extra_max = 9,
    };

    OpenLottoDrawSpec invalid_spec = {
        .main_count = 8,
        .main_min = 1,
        .main_max = 49,
        .extra_count = 1,
        .extra_min = 0,
        .extra_max = 9,
    };

    LotteryResult a;
    LotteryResult b;
    LotteryResult c;

    assert_equals(open_lotto_validate_spec(&valid_spec), OPEN_LOTTO_API_SUCCESS,
                  "Valid draw spec is accepted");
    assert_equals(open_lotto_validate_spec(&invalid_spec), OPEN_LOTTO_API_ERR_INVALID_SPEC,
                  "Invalid draw spec is rejected");

    assert_equals(open_lotto_generate(NULL, &a), OPEN_LOTTO_API_ERR_INVALID_ARG,
                  "NULL spec is rejected");
    assert_equals(open_lotto_generate(&valid_spec, NULL), OPEN_LOTTO_API_ERR_INVALID_ARG,
                  "NULL output is rejected");

    assert_equals(open_lotto_generate(&valid_spec, &a), OPEN_LOTTO_API_SUCCESS,
                  "Unseeded draw succeeds");

    assert_equals(open_lotto_generate_seeded(&valid_spec, 0xabc123ULL, &a), OPEN_LOTTO_API_SUCCESS,
                  "Seeded draw succeeds");
    assert_equals(open_lotto_generate_seeded(&valid_spec, 0xabc123ULL, &b), OPEN_LOTTO_API_SUCCESS,
                  "Repeat seeded draw succeeds");
    assert_equals(open_lotto_generate_seeded(&valid_spec, 0xabc124ULL, &c), OPEN_LOTTO_API_SUCCESS,
                  "Different seed draw succeeds");

    assert_true(results_equal(&a, &b), "Same seed produces identical API draws");
    assert_true(!results_equal(&a, &c), "Different seed produces different API draws");

    assert_true(open_lotto_derive_seed(0x42ULL, 0) == 0x42ULL,
                "Derived seed preserves base seed for first draw");
    assert_true(open_lotto_derive_seed(0x42ULL, 1) != 0x42ULL,
                "Derived seed changes for later draws");

    const char *version = open_lotto_version();
    assert_true(version != NULL, "Version string is available");

    test_summary();
}
