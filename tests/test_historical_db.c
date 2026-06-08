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

static void set_fixture_env(void)
{
    char url[1024];
#ifdef _WIN32
    _snprintf(url, sizeof(url), "OPEN_LOTTO_GEWINNZAHLEN_URL_EUROJACKPOT=file:///%s",
              HISTORICAL_DB_FIXTURE_PATH);
    _putenv(url);
#else
    snprintf(url, sizeof(url), "file://%s", HISTORICAL_DB_FIXTURE_PATH);
    setenv("OPEN_LOTTO_GEWINNZAHLEN_URL_EUROJACKPOT", url, 1);
#endif
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

    rc = historical_db_sync_latest("Unsupported Game", db_root, &snapshot);
    assert_equals(rc, HISTORICAL_DB_ERR_UNSUPPORTED_GAME, "Unsupported game is rejected");

    test_summary();
}
