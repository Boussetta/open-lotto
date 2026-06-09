/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "historical_db.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef HISTORICAL_DB_FIXTURE_PATH
#define HISTORICAL_DB_FIXTURE_PATH "tests/data/eurojackpot_gewinnzahlen.json"
#endif

#ifndef HISTORICAL_DB_FIXTURE_MALFORMED_PATH
#define HISTORICAL_DB_FIXTURE_MALFORMED_PATH "tests/data/eurojackpot_malformed.json"
#endif

#ifndef HISTORICAL_DB_FIXTURE_PARTIAL_PATH
#define HISTORICAL_DB_FIXTURE_PARTIAL_PATH "tests/data/eurojackpot_partial.json"
#endif

static void set_euro_url_env(const char *value)
{
#ifdef _WIN32
    char env_line[1536];
    _snprintf(env_line, sizeof(env_line), "OPEN_LOTTO_GEWINNZAHLEN_URL_EUROJACKPOT=%s", value);
    _putenv(env_line);
#else
    setenv("OPEN_LOTTO_GEWINNZAHLEN_URL_EUROJACKPOT", value, 1);
#endif
}

static void build_file_url(const char *path, char *out, size_t out_size)
{
#ifdef _WIN32
    _snprintf(out, out_size, "file:///%s", path);
#else
    snprintf(out, out_size, "file://%s", path);
#endif
}

static void set_fixture_env(void)
{
    char url[1024];
    build_file_url(HISTORICAL_DB_FIXTURE_PATH, url, sizeof(url));
    set_euro_url_env(url);
}

static void temp_db_root(char *out, size_t out_size)
{
#ifdef _WIN32
    snprintf(out, out_size, "%s", "./.tmp_hist_db_test");
#else
    snprintf(out, out_size, "%s", "/tmp/open_lotto_hist_db_test");
#endif
}

int main(void)
{
    test_suite("Historical DB Sync Tests");

    char db_root[256];
    HistoricalDrawSnapshot snapshot;
    HistoricalDrawSnapshot loaded;

    set_fixture_env();
    temp_db_root(db_root, sizeof(db_root));

#ifdef _WIN32
    _rmdir(db_root);
#else
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", db_root);
    int cleanup_rc = system(cmd);
    (void)cleanup_rc;
#endif

    int rc = historical_db_sync_latest("Eurojackpot", db_root, &snapshot);
    assert_equals(rc, HISTORICAL_DB_SYNC_UPDATED, "First sync creates local snapshot");
    assert_true(strcmp(snapshot.draw_date, "2026-06-05") == 0,
                "Draw date parsed from upstream payload");
    assert_true(snapshot.main_count == 5, "Main numbers count parsed correctly");
    assert_true(snapshot.extra_count == 2, "Extra numbers count parsed correctly");
    assert_true(snapshot.winning_class_count == 4, "Winning classes parsed correctly");

    rc = historical_db_sync_latest("Eurojackpot", db_root, &snapshot);
    assert_equals(rc, HISTORICAL_DB_SYNC_UNCHANGED,
                  "Second sync reports unchanged when draw date is identical");

    rc = historical_db_load_latest("Eurojackpot", db_root, &loaded);
    assert_equals(rc, HISTORICAL_DB_SYNC_UPDATED, "Load returns stored snapshot");
    assert_true(strcmp(loaded.draw_date, "2026-06-05") == 0, "Stored draw date can be reloaded");
    assert_true(loaded.winning_classes[1].winners == 2, "Stored winning class winners persist");

    char missing_url[1024];
    char malformed_url[1024];
    char partial_url[1024];
    char fallback_url[3072];

#ifdef _WIN32
    _snprintf(missing_url, sizeof(missing_url), "file:///./open_lotto_missing_fixture.json");
#else
    snprintf(missing_url, sizeof(missing_url), "file:///tmp/open_lotto_missing_fixture.json");
#endif
    build_file_url(HISTORICAL_DB_FIXTURE_MALFORMED_PATH, malformed_url, sizeof(malformed_url));
    build_file_url(HISTORICAL_DB_FIXTURE_PARTIAL_PATH, partial_url, sizeof(partial_url));

#ifdef _WIN32
    _putenv("OPEN_LOTTO_HIST_MAX_RETRY_ATTEMPTS=1");
    _putenv("OPEN_LOTTO_HIST_RETRY_BASE_DELAY_MS=100");
    _putenv("OPEN_LOTTO_HIST_RETRY_MAX_DELAY_MS=100");
    _putenv("OPEN_LOTTO_HIST_RETRY_JITTER_MS=0");
#else
    setenv("OPEN_LOTTO_HIST_MAX_RETRY_ATTEMPTS", "1", 1);
    setenv("OPEN_LOTTO_HIST_RETRY_BASE_DELAY_MS", "100", 1);
    setenv("OPEN_LOTTO_HIST_RETRY_MAX_DELAY_MS", "100", 1);
    setenv("OPEN_LOTTO_HIST_RETRY_JITTER_MS", "0", 1);
#endif

    set_euro_url_env(missing_url);
    rc = historical_db_sync_latest("Eurojackpot", db_root, &snapshot);
    assert_equals(rc, HISTORICAL_DB_ERR_NETWORK,
                  "Missing upstream payload returns network error after retry path");

    set_euro_url_env(malformed_url);
    rc = historical_db_sync_latest("Eurojackpot", db_root, &snapshot);
    assert_equals(rc, HISTORICAL_DB_ERR_PARSE, "Malformed upstream payload is rejected");

    set_euro_url_env(partial_url);
    rc = historical_db_sync_latest("Eurojackpot", db_root, &snapshot);
    assert_equals(rc, HISTORICAL_DB_ERR_PARSE, "Partial upstream payload is rejected");

    snprintf(fallback_url, sizeof(fallback_url), "%s, %s", missing_url, partial_url);
    set_euro_url_env(fallback_url);
    rc = historical_db_sync_latest("Eurojackpot", db_root, &snapshot);
    assert_equals(rc, HISTORICAL_DB_ERR_PARSE,
                  "Fallback chain uses next endpoint when primary source fails");

    rc = historical_db_sync_latest("Unsupported Game", db_root, &snapshot);
    assert_equals(rc, HISTORICAL_DB_ERR_UNSUPPORTED_GAME, "Unsupported game is rejected");

    test_summary();
}
