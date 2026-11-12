/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "historical_db.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef HISTORICAL_DB_FIXTURE_PATH
#define HISTORICAL_DB_FIXTURE_PATH "tests/data/eurojackpot_gewinnzahlen.json"
#endif

#ifndef HISTORICAL_DB_FIXTURE_MALFORMED_PATH
#define HISTORICAL_DB_FIXTURE_MALFORMED_PATH "tests/data/eurojackpot_malformed.json"
#endif

#ifndef HISTORICAL_DB_FIXTURE_PARTIAL_PATH
#define HISTORICAL_DB_FIXTURE_PARTIAL_PATH "tests/data/eurojackpot_partial.json"
#endif

static void set_env(const char *name, const char *value)
{
#ifdef _WIN32
    char env_line[1536];
    _snprintf(env_line, sizeof(env_line), "%s=%s", name, value ? value : "");
    _putenv(env_line);
#else
    if (value)
        setenv(name, value, 1);
    else
        unsetenv(name);
#endif
}

static void set_euro_url_env(const char *value)
{
    set_env("OPEN_LOTTO_GEWINNZAHLEN_URL_EUROJACKPOT", value);
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

/* Directory holding the fixtures, derived from the main fixture path. */
static void fixture_dir(char *out, size_t out_size)
{
    snprintf(out, out_size, "%s", HISTORICAL_DB_FIXTURE_PATH);
    char *slash = strrchr(out, '/');
    if (slash)
        *slash = '\0';
    else
        snprintf(out, out_size, "%s", ".");
}

/* Point per-draw and history requests at local file:// fixtures (no network). */
static void set_offline_templates(const char *draw_pattern)
{
    char dir[768];
    char path[1024];
    char url[1100];

    fixture_dir(dir, sizeof(dir));

    snprintf(path, sizeof(path), "%s/eurojackpot_history.json", dir);
    build_file_url(path, url, sizeof(url));
    set_env("OPEN_LOTTO_HIST_HISTORY_URL_TEMPLATE", url);

    snprintf(path, sizeof(path), "%s/%s", dir, draw_pattern);
    build_file_url(path, url, sizeof(url));
    set_env("OPEN_LOTTO_HIST_DRAW_URL_TEMPLATE", url);
}

static int file_exists(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return 0;
    fclose(fp);
    return 1;
}

static int count_draws_in_snapshot(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return -1;

    int count = 0;
    char line[1024];
    while (fgets(line, sizeof(line), fp))
    {
        if (strstr(line, "\"draw_date\""))
            count++;
    }
    fclose(fp);
    return count;
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

    /* Keep every failure path fast and fully offline. */
    set_env("OPEN_LOTTO_HIST_MAX_RETRY_ATTEMPTS", "1");
    set_env("OPEN_LOTTO_HIST_RETRY_BASE_DELAY_MS", "100");
    set_env("OPEN_LOTTO_HIST_RETRY_MAX_DELAY_MS", "100");
    set_env("OPEN_LOTTO_HIST_RETRY_JITTER_MS", "0");
    set_env("OPEN_LOTTO_HIST_DOWNLOAD_WORKERS", "1");
    set_env("OPEN_LOTTO_HIST_FORCE_FULL_REBUILD", NULL);
    set_env("OPEN_LOTTO_HIST_MAX_FETCH_DRAWS", NULL);
    set_env("OPEN_LOTTO_DOWNLOAD_CONFIG", "/nonexistent/open-lotto-download.conf");
    set_offline_templates("eurojackpot_draw_{date}.json");

    set_fixture_env();
    temp_db_root(db_root, sizeof(db_root));

    char snapshot_path[512];
    char lock_path[600];
    char marker_path[700];
    snprintf(snapshot_path, sizeof(snapshot_path), "%s/eurojackpot_gewinnzahlen.json", db_root);
    snprintf(lock_path, sizeof(lock_path), "%s.lock", snapshot_path);
    snprintf(marker_path, sizeof(marker_path), "%s.sync_cache/PENDING", snapshot_path);

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

    assert_equals(count_draws_in_snapshot(snapshot_path), 3,
                  "Bulk sync stores every valid history date (invalid dates dropped)");
    assert_true(!file_exists("/tmp/open_lotto_pwned"), "Injected history date is never executed");
    assert_true(!file_exists(marker_path), "Successful bulk sync removes resume marker");
    assert_true(!file_exists(lock_path), "Sync lock released after bulk sync");

    rc = historical_db_sync_latest("Eurojackpot", db_root, &snapshot);
    assert_equals(rc, HISTORICAL_DB_SYNC_UNCHANGED,
                  "Second sync reports unchanged when draw date is identical");

    rc = historical_db_load_latest("Eurojackpot", db_root, &loaded);
    assert_equals(rc, HISTORICAL_DB_SYNC_UPDATED, "Load returns stored snapshot");
    assert_true(strcmp(loaded.draw_date, "2026-06-05") == 0, "Stored draw date can be reloaded");
    assert_true(loaded.winning_classes[1].winners == 2, "Stored winning class winners persist");
    assert_true(
        strcmp(loaded.winning_classes[0].description, "5 + 2 \"Jackpot\" \\ \xc3\xa4 {x}") == 0,
        "Escaped strings round-trip through the snapshot file");
    assert_true(strstr(loaded.source_url, "eurojackpot_draw_2026-06-05.json") != NULL,
                "Per-draw source URL comes from the configured template");

    /* Forced rebuild while every per-draw request fails must keep the old snapshot. */
    set_offline_templates("missing_draw_{date}.json");
    set_env("OPEN_LOTTO_HIST_FORCE_FULL_REBUILD", "1");
    rc = historical_db_sync_latest("Eurojackpot", db_root, &snapshot);
    assert_equals(rc, HISTORICAL_DB_ERR_NETWORK, "Failed forced rebuild reports network error");
    assert_equals(count_draws_in_snapshot(snapshot_path), 3,
                  "Failed forced rebuild keeps existing snapshot intact");
    assert_true(file_exists(marker_path), "Failed forced rebuild keeps resume cache");
    set_env("OPEN_LOTTO_HIST_FORCE_FULL_REBUILD", NULL);

    /* Next run resumes the interrupted bulk download. */
    set_offline_templates("eurojackpot_draw_{date}.json");
    rc = historical_db_sync_latest("Eurojackpot", db_root, &snapshot);
    assert_equals(rc, HISTORICAL_DB_SYNC_UPDATED, "Pending bulk download resumes on next sync");
    assert_true(!file_exists(marker_path), "Resumed bulk sync clears resume marker");
    assert_equals(count_draws_in_snapshot(snapshot_path), 3, "Resumed bulk sync stores all draws");

#ifndef _WIN32
    /* A lock owned by a dead process is stale and must be reclaimed. */
    pid_t child = fork();
    if (child == 0)
        _exit(0);
    waitpid(child, NULL, 0);
    FILE *lock_fp = fopen(lock_path, "w");
    if (lock_fp)
    {
        fprintf(lock_fp, "%ld\n", (long)child);
        fclose(lock_fp);
    }
    rc = historical_db_sync_latest("Eurojackpot", db_root, &snapshot);
    assert_equals(rc, HISTORICAL_DB_SYNC_UNCHANGED, "Stale lock from dead process is reclaimed");

    /* A lock held by a live process blocks concurrent syncs. */
    lock_fp = fopen(lock_path, "w");
    if (lock_fp)
    {
        fprintf(lock_fp, "%ld\n", (long)getpid());
        fclose(lock_fp);
    }
    rc = historical_db_sync_latest("Eurojackpot", db_root, &snapshot);
    assert_equals(rc, HISTORICAL_DB_ERR_IO, "Live lock blocks concurrent sync");
    remove(lock_path);
#endif

    /* A newer upstream draw is appended incrementally to the existing snapshot. */
    {
        char dir[768];
        char path[1024];
        char url[1100];
        fixture_dir(dir, sizeof(dir));
        snprintf(path, sizeof(path), "%s/eurojackpot_draw_2026-06-09.json", dir);
        build_file_url(path, url, sizeof(url));
        set_euro_url_env(url);
    }
    rc = historical_db_sync_latest("Eurojackpot", db_root, &snapshot);
    assert_equals(rc, HISTORICAL_DB_SYNC_UPDATED, "Newer upstream draw is appended");
    assert_equals(count_draws_in_snapshot(snapshot_path), 4, "Append adds exactly one draw");
    rc = historical_db_load_latest("Eurojackpot", db_root, &loaded);
    assert_true(rc == HISTORICAL_DB_SYNC_UPDATED && strcmp(loaded.draw_date, "2026-06-09") == 0,
                "Appended draw is the latest stored draw");

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

    /* Shell metacharacters in a configured URL are refused, never executed. */
    set_euro_url_env("file:///tmp/x$(touch${IFS}/tmp/open_lotto_pwned).json");
    rc = historical_db_sync_latest("Eurojackpot", db_root, &snapshot);
    assert_equals(rc, HISTORICAL_DB_ERR_NETWORK, "Unsafe source URL is rejected");
    assert_true(!file_exists("/tmp/open_lotto_pwned"), "Unsafe source URL is never executed");

    rc = historical_db_sync_latest("Unsupported Game", db_root, &snapshot);
    assert_equals(rc, HISTORICAL_DB_ERR_UNSUPPORTED_GAME, "Unsupported game is rejected");

    test_summary();
}
