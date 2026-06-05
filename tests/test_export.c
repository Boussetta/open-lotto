/**
 * @file test_export.c
 * @brief Golden-file tests for CSV and JSON export.
 *
 * A fixed LotteryResult is exported to a temporary file whose contents are
 * then verified byte-for-byte against expected strings.  This catches any
 * silent regression in the export format.
 */

#include "../include/combogen.h"
#include "../include/export.h"
#include "test.h"
#include <stdio.h>
#include <string.h>

/* Fixed test data – deterministic, no RNG involved. */
static LotteryResult make_result(void)
{
    LotteryResult r;
    r.main_numbers[0] = 5;
    r.main_numbers[1] = 12;
    r.main_numbers[2] = 23;
    r.main_numbers[3] = 34;
    r.main_numbers[4] = 41;
    r.main_numbers[5] = 48;
    r.main_count = 6;
    r.extra_numbers[0] = 7;
    r.extra_numbers[1] = 11;
    r.extra_count = 2;
    return r;
}

/* ------------------------------------------------------------------ */
static void test_csv_export(void)
{
    test_suite("Export – CSV format");

    LotteryResult result = make_result();
    const char *path = "/tmp/open_lotto_test_export.csv";

    int ret = export_results_csv_file(path, "TestGame", &result, 1);
    assert_equals(ret, 0, "export_results_csv_file returns 0");

    FILE *f = fopen(path, "r");
    assert_true(f != NULL, "CSV file was created");
    if (!f)
        return;

    char line[256];

    /* Header row */
    char *hdr = fgets(line, sizeof(line), f);
    if (!hdr)
    {
        fclose(f);
        remove(path);
        assert_true(0, "CSV: header line readable");
        return;
    }
    assert_true(strcmp(line, "draw_number,main_numbers,extra_numbers\n") == 0,
                "CSV: header is 'draw_number,main_numbers,extra_numbers'");

    /* Data row */
    char *row = fgets(line, sizeof(line), f);
    if (!row)
    {
        fclose(f);
        remove(path);
        assert_true(0, "CSV: data line readable");
        return;
    }
    assert_true(strcmp(line, "1,5 12 23 34 41 48,7 11\n") == 0,
                "CSV: data row matches expected format");

    /* No more rows */
    char *eof = fgets(line, sizeof(line), f);
    assert_true(eof == NULL, "CSV: only one data row written");

    fclose(f);
    remove(path);
}

static void test_csv_no_extras(void)
{
    test_suite("Export – CSV with no extra numbers");

    LotteryResult result = make_result();
    result.extra_count = 0;
    const char *path = "/tmp/open_lotto_test_export_noextra.csv";

    int ret = export_results_csv_file(path, "TestGame", &result, 1);
    assert_equals(ret, 0, "export_results_csv_file (no extras) returns 0");

    FILE *f = fopen(path, "r");
    assert_true(f != NULL, "CSV file created without extras");
    if (!f)
        return;

    char line[256];
    char *hdr2 = fgets(line, sizeof(line), f); /* header */
    if (!hdr2)
    {
        fclose(f);
        remove(path);
        assert_true(0, "CSV (no extras): header readable");
        return;
    }
    char *row2 = fgets(line, sizeof(line), f);
    assert_true(row2 != NULL, "CSV (no extras): data line readable");

    /* Row must not contain a trailing comma for extras */
    assert_true(strcmp(line, "1,5 12 23 34 41 48\n") == 0,
                "CSV (no extras): row has no extra column");

    fclose(f);
    remove(path);
}

static void test_json_export(void)
{
    test_suite("Export – JSON format");

    LotteryResult result = make_result();
    const char *path = "/tmp/open_lotto_test_export.json";

    int ret = export_results_json_file(path, "TestGame", &result, 1);
    assert_equals(ret, 0, "export_results_json_file returns 0");

    FILE *f = fopen(path, "r");
    assert_true(f != NULL, "JSON file was created");
    if (!f)
        return;

    /* Read entire file into a buffer */
    char buf[2048] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    assert_true(n > 0, "JSON file is non-empty");

    assert_true(strstr(buf, "\"game\": \"TestGame\"") != NULL, "JSON: contains game name field");
    assert_true(strstr(buf, "\"draws\"") != NULL, "JSON: contains draws array");
    assert_true(strstr(buf, "\"draw_number\": 1") != NULL, "JSON: draw_number is 1");
    assert_true(strstr(buf, "\"main_numbers\": [5, 12, 23, 34, 41, 48]") != NULL,
                "JSON: main_numbers array matches");
    assert_true(strstr(buf, "\"extra_numbers\": [7, 11]") != NULL,
                "JSON: extra_numbers array matches");

    remove(path);
}

static void test_invalid_args(void)
{
    test_suite("Export – invalid argument rejection");

    LotteryResult result = make_result();

    assert_equals(export_results_csv_file(NULL, "G", &result, 1), -1,
                  "csv: NULL filename returns -1");
    assert_equals(export_results_csv_file("/tmp/x.csv", NULL, &result, 1), -1,
                  "csv: NULL game_name returns -1");
    assert_equals(export_results_csv_file("/tmp/x.csv", "G", NULL, 1), -1,
                  "csv: NULL results returns -1");
    assert_equals(export_results_csv_file("/tmp/x.csv", "G", &result, 0), -1,
                  "csv: zero num_results returns -1");

    assert_equals(export_results_json_file(NULL, "G", &result, 1), -1,
                  "json: NULL filename returns -1");
    assert_equals(export_results_json_file("/tmp/x.json", NULL, &result, 1), -1,
                  "json: NULL game_name returns -1");
    assert_equals(export_results_json_file("/tmp/x.json", "G", NULL, 1), -1,
                  "json: NULL results returns -1");
    assert_equals(export_results_json_file("/tmp/x.json", "G", &result, 0), -1,
                  "json: zero num_results returns -1");
}

/* ------------------------------------------------------------------ */
int main(void)
{
    test_csv_export();
    test_csv_no_extras();
    test_json_export();
    test_invalid_args();

    test_summary();
}
