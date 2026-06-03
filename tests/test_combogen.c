#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/combogen.h"
#include "test.h"

/* Helper: Check if all elements in array are unique */
static int is_unique(const int *arr, int count)
{
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (arr[i] == arr[j])
                return 0;
        }
    }
    return 1;
}

/* Helper: Check if all elements are within [min, max] */
static int in_range(const int *arr, int count, int min, int max)
{
    for (int i = 0; i < count; i++) {
        if (arr[i] < min || arr[i] > max)
            return 0;
    }
    return 1;
}

/* Dummy callback for tests */
static void dummy_callback(DrawEvent event, const LotteryResult *result)
{
    (void)event;
    (void)result;
}

int main(void)
{
    test_suite("Draw Generation Tests");

    /* Test 1: Eurojackpot - Single draw */
    {
        printf("\n[Test Group: Eurojackpot Basic]\n");
        LotteryResult result;
        generate_draw(5, 1, 50, 2, 1, 12, &result, dummy_callback);

        assert_equals(result.main_count, 5, "Main count is 5");
        assert_equals(result.extra_count, 2, "Extra count is 2");
        assert_true(is_unique(result.main_numbers, 5), "Main numbers are unique");
        assert_true(is_unique(result.extra_numbers, 2), "Extra numbers are unique");
        assert_true(in_range(result.main_numbers, 5, 1, 50), "Main numbers in range [1, 50]");
        assert_true(in_range(result.extra_numbers, 2, 1, 12), "Extra numbers in range [1, 12]");
    }

    /* Test 2: Lotto 6aus49 - Single draw */
    {
        printf("\n[Test Group: Lotto 6aus49 Basic]\n");
        LotteryResult result;
        generate_draw(6, 1, 49, 1, 0, 9, &result, dummy_callback);

        assert_equals(result.main_count, 6, "Main count is 6");
        assert_equals(result.extra_count, 1, "Extra count is 1");
        assert_true(is_unique(result.main_numbers, 6), "Main numbers are unique");
        assert_true(in_range(result.main_numbers, 6, 1, 49), "Main numbers in range [1, 49]");
        assert_true(in_range(result.extra_numbers, 1, 0, 9), "Extra number in range [0, 9]");
    }

    /* Test 3: Multiple draws - Eurojackpot */
    {
        printf("\n[Test Group: Eurojackpot Multiple Draws]\n");
        int all_unique = 1;

        for (int draw = 0; draw < 10; draw++) {
            LotteryResult result;
            generate_draw(5, 1, 50, 2, 1, 12, &result, dummy_callback);

            if (!is_unique(result.main_numbers, 5) || !is_unique(result.extra_numbers, 2)) {
                all_unique = 0;
                break;
            }
        }

        assert_true(all_unique, "10 consecutive Eurojackpot draws all have unique numbers");
    }

    /* Test 4: Multiple draws - Lotto */
    {
        printf("\n[Test Group: Lotto 6aus49 Multiple Draws]\n");
        int all_unique = 1;

        for (int draw = 0; draw < 10; draw++) {
            LotteryResult result;
            generate_draw(6, 1, 49, 1, 0, 9, &result, dummy_callback);

            if (!is_unique(result.main_numbers, 6)) {
                all_unique = 0;
                break;
            }
        }

        assert_true(all_unique, "10 consecutive Lotto draws all have unique numbers");
    }

    /* Test 5: Boundary case - minimum range */
    {
        printf("\n[Test Group: Boundary Cases]\n");
        LotteryResult result;
        
        /* 3 numbers from range [1, 3] */
        generate_draw(3, 1, 3, 0, 0, 0, &result, dummy_callback);
        assert_true(is_unique(result.main_numbers, 3), "3 numbers from [1, 3] are unique");
        assert_true(in_range(result.main_numbers, 3, 1, 3), "Numbers in exact range [1, 3]");
    }

    /* Test 6: Extra numbers from small range */
    {
        printf("\n[Test Group: Extra Numbers Edge Cases]\n");
        
        /* 2 extras from range [1, 2] should always be {1, 2} in some order */
        LotteryResult result;
        generate_draw(1, 1, 1, 2, 1, 2, &result, dummy_callback);
        
        assert_equals(result.extra_count, 2, "Extra count is 2");
        assert_true(is_unique(result.extra_numbers, 2), "2 extras from [1, 2] are unique");
        
        int has_1 = (result.extra_numbers[0] == 1 || result.extra_numbers[1] == 1);
        int has_2 = (result.extra_numbers[0] == 2 || result.extra_numbers[1] == 2);
        assert_true(has_1 && has_2, "Extras from [1, 2] contain both 1 and 2");
    }

    /* Test 7: Invalid inputs - should handle gracefully */
    {
        printf("\n[Test Group: Error Handling]\n");
        LotteryResult result;
        
        /* Invalid main_count (too large) - should not modify result */
        result.main_count = -999;
        result.extra_count = -999;
        generate_draw(8, 1, 50, 0, 0, 0, &result, dummy_callback);
        assert_equals(result.main_count, -999, "Invalid main_count does not modify result");

        /* Invalid extra_count (too large) - should not modify result */
        result.main_count = -888;
        result.extra_count = -888;
        generate_draw(5, 1, 50, 4, 1, 12, &result, dummy_callback);
        assert_equals(result.main_count, -888, "Invalid extra_count does not modify result");

        /* Insufficient range for main numbers - should not modify result */
        result.main_count = -777;
        generate_draw(10, 1, 5, 0, 0, 0, &result, dummy_callback);
        assert_equals(result.main_count, -777, "Insufficient main range does not modify result");

        /* Insufficient range for extra numbers - should not modify result */
        result.main_count = -666;
        generate_draw(5, 1, 50, 3, 1, 2, &result, dummy_callback);
        assert_equals(result.main_count, -666, "Insufficient extra range does not modify result");
    }

    /* Test 8: NULL pointer handling */
    {
        printf("\n[Test Group: Null Pointer Handling]\n");
        
        /* Passing NULL should not crash (already tested implicitly above) */
        generate_draw(5, 1, 50, 2, 1, 12, NULL, dummy_callback);
        
        assert_true(1, "NULL result pointer handled safely");
    }

    test_summary();
}
