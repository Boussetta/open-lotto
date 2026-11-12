/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

/**
 * @file historical_db.c
 * @brief Historical snapshot sync, parsing, caching, and local persistence.
 */

#include "historical_db.h"
#include "log.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#ifdef _OPENMP
#include <omp.h>
#endif

#ifndef _WIN32
#include <signal.h>
#include <spawn.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#else
#include <direct.h>
#include <io.h>
#include <windows.h>
#define strcasecmp _stricmp
#endif

#define READ_CHUNK 4096
#define HISTORICAL_DB_MAX_DATES 4096
#define EUROJACKPOT_FIRST_DRAW_YEAR 2012
#define LOTTO_FIRST_DRAW_YEAR 2012
#define HISTORICAL_DB_FETCH_TIMEOUT_SEC_DEFAULT 30
#define HISTORICAL_DB_DRAW_TIMEOUT_SEC_DEFAULT 20
#define HISTORICAL_DB_MAX_RETRY_ATTEMPTS_DEFAULT 3
#define HISTORICAL_DB_RETRY_BASE_DELAY_MS_DEFAULT 1000
#define HISTORICAL_DB_RETRY_MAX_DELAY_MS_DEFAULT 8000
#define HISTORICAL_DB_DOWNLOAD_WORKERS_DEFAULT 4
#define HISTORICAL_DB_SCHEMA_VERSION 1
#define HISTORICAL_DB_SOURCES_CONFIG_ENV "OPEN_LOTTO_SOURCES_CONFIG"
#define HISTORICAL_DB_URL_ENV_EUROJACKPOT "OPEN_LOTTO_GEWINNZAHLEN_URL_EUROJACKPOT"
#define HISTORICAL_DB_URL_ENV_LOTTO "OPEN_LOTTO_GEWINNZAHLEN_URL_LOTTO"
#define HISTORICAL_DB_WORKERS_ENV "OPEN_LOTTO_HIST_DOWNLOAD_WORKERS"
#define HISTORICAL_DB_FETCH_TIMEOUT_ENV "OPEN_LOTTO_HIST_FETCH_TIMEOUT_SEC"
#define HISTORICAL_DB_DRAW_TIMEOUT_ENV "OPEN_LOTTO_HIST_DRAW_TIMEOUT_SEC"
#define HISTORICAL_DB_RETRY_ATTEMPTS_ENV "OPEN_LOTTO_HIST_MAX_RETRY_ATTEMPTS"
#define HISTORICAL_DB_RETRY_BASE_DELAY_ENV "OPEN_LOTTO_HIST_RETRY_BASE_DELAY_MS"
#define HISTORICAL_DB_RETRY_MAX_DELAY_ENV "OPEN_LOTTO_HIST_RETRY_MAX_DELAY_MS"
#define HISTORICAL_DB_RETRY_JITTER_ENV "OPEN_LOTTO_HIST_RETRY_JITTER_MS"
#define HISTORICAL_DB_BULK_PACE_ENV "OPEN_LOTTO_HIST_BULK_FETCH_PACE_MS"
#define HISTORICAL_DB_MAX_FETCH_DRAWS_ENV "OPEN_LOTTO_HIST_MAX_FETCH_DRAWS"
#define HISTORICAL_DB_FORCE_FULL_REBUILD_ENV "OPEN_LOTTO_HIST_FORCE_FULL_REBUILD"
#define HISTORICAL_DB_DOWNLOAD_CONFIG_ENV "OPEN_LOTTO_DOWNLOAD_CONFIG"
/* URL templates for per-draw and per-year history requests. Placeholders:
 * {spielart} (EJ / LOTTO), {date} (YYYY-MM-DD, per-draw only), {year} (history only). */
#define HISTORICAL_DB_DRAW_URL_TEMPLATE_ENV "OPEN_LOTTO_HIST_DRAW_URL_TEMPLATE"
#define HISTORICAL_DB_HISTORY_URL_TEMPLATE_ENV "OPEN_LOTTO_HIST_HISTORY_URL_TEMPLATE"
#define HISTORICAL_DB_DRAW_URL_TEMPLATE_DEFAULT                                                    \
    "https://www.eurojackpot.com/wlinfo/WL_InfoService?client=jsn&gruppe=ZahlenUndQuoten&"         \
    "ewGewsum=ja&spielart={spielart}&datum={date}&adg=ja&lang=de"
#define HISTORICAL_DB_HISTORY_URL_TEMPLATE_DEFAULT                                                 \
    "https://www.eurojackpot.com/wlinfo/WL_InfoService?client=jsn&gruppe=ZahlenUndQuoten&"         \
    "ewGewsum=ja&historie=ja&spielart={spielart}&adg=ja&lang=de&jahr={year}"
/* A sync lock older than this is considered abandoned. */
#define HISTORICAL_DB_LOCK_STALE_SEC (6 * 60 * 60)
/* Marker left in the resume cache directory when a bulk download did not complete. */
#define HISTORICAL_DB_PENDING_MARKER "PENDING"
/* parse_json_string_value() result when the decoded value did not fit the buffer. */
#define JSON_STRING_TRUNCATED -2

/* Width (in filled/empty characters) of the terminal progress bar. */
#define PROGRESS_BAR_WIDTH 40

/**
 * Print a single-line progress bar on stderr with percentage and bit rate.
 * Call with done == total to emit a final newline.
 *
 *   Downloading historical draws [=========>          ] 23/50 (46%) 125 KiB/s
 */
static void print_progress_bar(int done, int total, time_t start_time, size_t total_bytes)
{
    if (total <= 0)
        return;

    int filled = (int)((double)done / (double)total * PROGRESS_BAR_WIDTH);
    if (filled > PROGRESS_BAR_WIDTH)
        filled = PROGRESS_BAR_WIDTH;

    int percentage = (int)((double)done / (double)total * 100.0);

    time_t now = time(NULL);
    double elapsed = difftime(now, start_time);
    double bit_rate = 0.0;
    const char *rate_unit = "B/s";

    if (elapsed > 0)
    {
        bit_rate = (double)total_bytes / elapsed;
        if (bit_rate >= 1024.0 * 1024.0)
        {
            bit_rate /= (1024.0 * 1024.0);
            rate_unit = "MiB/s";
        }
        else if (bit_rate >= 1024.0)
        {
            bit_rate /= 1024.0;
            rate_unit = "KiB/s";
        }
    }

    fprintf(stderr, "\r  Downloading historical draws [");
    for (int i = 0; i < PROGRESS_BAR_WIDTH; i++)
    {
        if (i < filled - 1)
            fputc('=', stderr);
        else if (i == filled - 1)
            fputc('>', stderr);
        else
            fputc(' ', stderr);
    }
    fprintf(stderr, "] %d/%d (%d%%) %.0f %s", done, total, percentage, bit_rate, rate_unit);

    if (done >= total)
        fputc('\n', stderr);

    fflush(stderr);
}

static int parse_history_dates(const char *json, char dates[][16], int max_dates);
static char *fetch_draw_for_date(const char *game_name, const char *date);
static char *fetch_url_text(const char *url);
static char *read_stream(FILE *fp);
static int read_file_text(const char *path, char **out);
static void trim_ascii_whitespace(char *s);
static int get_download_config_path(char *out, size_t out_size);
static int parse_int_bounded(const char *raw, int *out, int min_value, int max_value);
static unsigned long historical_db_process_id(void);
static int parse_source_url_chain(const char *raw, char urls[][512], int max_urls);

typedef struct
{
    int workers;
    int fetch_timeout_sec;
    int draw_timeout_sec;
    int max_retry_attempts;
    int retry_base_delay_ms;
    int retry_max_delay_ms;
    int retry_jitter_ms;
    int bulk_fetch_pace_ms;
    int max_fetch_draws;
    int force_full_rebuild;
} DownloadSettings;

typedef struct
{
    HistoricalDrawSnapshot draw;
    int fetched_ok;
    int parsed_ok;
    size_t bytes;
} BulkDrawResult;

static void load_download_settings_from_config(DownloadSettings *s);

static DownloadSettings g_download_settings = {
    HISTORICAL_DB_DOWNLOAD_WORKERS_DEFAULT,
    HISTORICAL_DB_FETCH_TIMEOUT_SEC_DEFAULT,
    HISTORICAL_DB_DRAW_TIMEOUT_SEC_DEFAULT,
    HISTORICAL_DB_MAX_RETRY_ATTEMPTS_DEFAULT,
    HISTORICAL_DB_RETRY_BASE_DELAY_MS_DEFAULT,
    HISTORICAL_DB_RETRY_MAX_DELAY_MS_DEFAULT,
    250,
    0,
    0,
    0,
};

static int g_progress_log_tracking_enabled = 0;
static int g_progress_log_lines_since_draw = 0;

static void historical_db_progress_log_observer(void)
{
    if (g_progress_log_tracking_enabled)
        g_progress_log_lines_since_draw++;
}

static void progress_log_tracking_begin(void)
{
    g_progress_log_lines_since_draw = 0;
    g_progress_log_tracking_enabled = 1;
    log_set_line_observer(historical_db_progress_log_observer);
}

static void progress_log_tracking_end(void)
{
    g_progress_log_tracking_enabled = 0;
    g_progress_log_lines_since_draw = 0;
    log_set_line_observer(NULL);
}

static double bytes_per_sec_since(time_t start_time, size_t bytes)
{
    time_t now = time(NULL);
    double elapsed = difftime(now, start_time);
    if (elapsed <= 0.0)
        return 0.0;
    return (double)bytes / elapsed;
}

static void format_rate(double bytes_per_sec, double *value, const char **unit)
{
    *value = bytes_per_sec;
    *unit = "B/s";

    if (*value >= 1024.0 * 1024.0)
    {
        *value /= (1024.0 * 1024.0);
        *unit = "MiB/s";
    }
    else if (*value >= 1024.0)
    {
        *value /= 1024.0;
        *unit = "KiB/s";
    }
}

static int terminal_supports_ansi_redraw(void)
{
    if (!isatty(fileno(stderr)))
        return 0;

    const char *term = getenv("TERM");
    if (!term || term[0] == '\0')
        return 0;

    if (strcasecmp(term, "dumb") == 0)
        return 0;

    return 1;
}

static void print_worker_progress_bars(int done, int total, time_t start_time, size_t total_bytes,
                                       int workers, const int *worker_done,
                                       const size_t *worker_bytes, int *printed_lines)
{
    if (total <= 0)
        return;

    if (workers <= 1 || !terminal_supports_ansi_redraw())
    {
        print_progress_bar(done, total, start_time, total_bytes);
        return;
    }

    if (*printed_lines > 0)
    {
        int rewind_lines = *printed_lines;
        if (g_progress_log_tracking_enabled && g_progress_log_lines_since_draw > 0)
        {
            rewind_lines += g_progress_log_lines_since_draw;
            g_progress_log_lines_since_draw = 0;
        }
        fprintf(stderr, "\033[%dF", rewind_lines);
    }

    int filled = (int)((double)done / (double)total * PROGRESS_BAR_WIDTH);
    if (filled > PROGRESS_BAR_WIDTH)
        filled = PROGRESS_BAR_WIDTH;
    int percentage = (int)((double)done / (double)total * 100.0);

    double total_rate_value = 0.0;
    const char *total_rate_unit = "B/s";
    format_rate(bytes_per_sec_since(start_time, total_bytes), &total_rate_value, &total_rate_unit);

    fprintf(stderr, "\033[2K  All workers              [");
    for (int i = 0; i < PROGRESS_BAR_WIDTH; i++)
    {
        if (i < filled - 1)
            fputc('=', stderr);
        else if (i == filled - 1)
            fputc('>', stderr);
        else
            fputc(' ', stderr);
    }
    fprintf(stderr, "] %d/%d (%d%%) %.0f %s\n", done, total, percentage, total_rate_value,
            total_rate_unit);

    for (int w = 0; w < workers; w++)
    {
        int worker_count = worker_done[w];
        int worker_pct = (int)((double)worker_count / (double)total * 100.0);
        int worker_filled = (int)((double)worker_count / (double)total * PROGRESS_BAR_WIDTH);
        if (worker_filled > PROGRESS_BAR_WIDTH)
            worker_filled = PROGRESS_BAR_WIDTH;

        double worker_rate_value = 0.0;
        const char *worker_rate_unit = "B/s";

        format_rate(bytes_per_sec_since(start_time, worker_bytes[w]), &worker_rate_value,
                    &worker_rate_unit);

        fprintf(stderr, "\033[2K  Worker %-2d               [", w + 1);
        for (int i = 0; i < PROGRESS_BAR_WIDTH; i++)
        {
            if (i < worker_filled - 1)
                fputc('=', stderr);
            else if (i == worker_filled - 1)
                fputc('>', stderr);
            else
                fputc(' ', stderr);
        }
        fprintf(stderr, "] %d/%d (%d%%) %.0f %s\n", worker_count, total, worker_pct,
                worker_rate_value, worker_rate_unit);
    }

    *printed_lines = workers + 1;
    if (done >= total)
    {
        *printed_lines = 0;
        fputc('\n', stderr);
    }

    fflush(stderr);
}

static int read_env_int_bounded(const char *name, int fallback, int min_value, int max_value)
{
    const char *raw = getenv(name);
    if (!raw || raw[0] == '\0')
        return fallback;

    char *end = NULL;
    long value = strtol(raw, &end, 10);
    if (end == raw || *end != '\0')
        return fallback;

    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return (int)value;
}

static int read_env_bool(const char *name, int fallback)
{
    const char *raw = getenv(name);
    if (!raw || raw[0] == '\0')
        return fallback;

    if (strcasecmp(raw, "1") == 0 || strcasecmp(raw, "true") == 0 || strcasecmp(raw, "yes") == 0 ||
        strcasecmp(raw, "on") == 0)
        return 1;

    if (strcasecmp(raw, "0") == 0 || strcasecmp(raw, "false") == 0 || strcasecmp(raw, "no") == 0 ||
        strcasecmp(raw, "off") == 0)
        return 0;

    return fallback;
}

static int get_download_config_path(char *out, size_t out_size)
{
    const char *override = getenv(HISTORICAL_DB_DOWNLOAD_CONFIG_ENV);
    if (override && override[0] != '\0')
    {
        int n = snprintf(out, out_size, "%s", override);
        return (n > 0 && (size_t)n < out_size) ? 0 : -1;
    }

    const char *home = getenv("HOME");
    if (!home || home[0] == '\0')
        return -1;

    int n = snprintf(out, out_size, "%s/.config/open-lotto/download.conf", home);
    return (n > 0 && (size_t)n < out_size) ? 0 : -1;
}

static int parse_int_bounded(const char *raw, int *out, int min_value, int max_value)
{
    if (!raw || !out)
        return 0;

    char *end = NULL;
    long value = strtol(raw, &end, 10);
    if (end == raw || *end != '\0')
        return 0;

    if (value < min_value)
        value = min_value;
    if (value > max_value)
        value = max_value;

    *out = (int)value;
    return 1;
}

static int parse_source_url_chain(const char *raw, char urls[][512], int max_urls)
{
    int count = 0;
    const char *p = raw;

    if (!raw || !urls || max_urls <= 0)
        return 0;

    while (*p && count < max_urls)
    {
        while (*p == ',' || *p == ';' || isspace((unsigned char)*p))
            p++;

        if (*p == '\0')
            break;

        const char *start = p;
        while (*p && *p != ',' && *p != ';')
            p++;

        const char *end = p;
        while (end > start && isspace((unsigned char)*(end - 1)))
            end--;

        if (end <= start)
            continue;

        size_t len = (size_t)(end - start);
        if (len >= 512)
            len = 511;

        memcpy(urls[count], start, len);
        urls[count][len] = '\0';
        count++;
    }

    return count;
}

static void load_download_settings_from_config(DownloadSettings *s)
{
    if (!s)
        return;

    char path[512];
    if (get_download_config_path(path, sizeof(path)) != 0)
        return;

    FILE *fp = fopen(path, "r");
    if (!fp)
        return;

    char line[512];
    int in_download_section = 0;
    while (fgets(line, sizeof(line), fp))
    {
        trim_ascii_whitespace(line);
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';')
            continue;

        if (line[0] == '[')
        {
            in_download_section = (strcasecmp(line, "[download]") == 0);
            continue;
        }

        if (!in_download_section)
            continue;

        char *sep = strchr(line, '=');
        if (!sep)
            continue;

        *sep = '\0';
        char *key = line;
        char *value = sep + 1;
        trim_ascii_whitespace(key);
        trim_ascii_whitespace(value);

        if (strcasecmp(key, "workers") == 0)
            parse_int_bounded(value, &s->workers, 1, 32);
        else if (strcasecmp(key, "fetch_timeout_sec") == 0)
            parse_int_bounded(value, &s->fetch_timeout_sec, 5, 300);
        else if (strcasecmp(key, "draw_timeout_sec") == 0)
            parse_int_bounded(value, &s->draw_timeout_sec, 5, 300);
        else if (strcasecmp(key, "max_retry_attempts") == 0)
            parse_int_bounded(value, &s->max_retry_attempts, 1, 10);
        else if (strcasecmp(key, "retry_base_delay_ms") == 0)
            parse_int_bounded(value, &s->retry_base_delay_ms, 100, 60000);
        else if (strcasecmp(key, "retry_max_delay_ms") == 0)
            parse_int_bounded(value, &s->retry_max_delay_ms, 100, 120000);
        else if (strcasecmp(key, "retry_jitter_ms") == 0)
            parse_int_bounded(value, &s->retry_jitter_ms, 0, 10000);
        else if (strcasecmp(key, "bulk_fetch_pace_ms") == 0)
            parse_int_bounded(value, &s->bulk_fetch_pace_ms, 0, 10000);
        else if (strcasecmp(key, "max_fetch_draws") == 0)
            parse_int_bounded(value, &s->max_fetch_draws, 0, HISTORICAL_DB_MAX_DATES);
        else if (strcasecmp(key, "force_full_rebuild") == 0)
            s->force_full_rebuild =
                (strcasecmp(value, "1") == 0 || strcasecmp(value, "true") == 0 ||
                 strcasecmp(value, "yes") == 0 || strcasecmp(value, "on") == 0)
                    ? 1
                    : 0;
    }

    fclose(fp);
}

static DownloadSettings load_download_settings(void)
{
    DownloadSettings s;
    s.workers = HISTORICAL_DB_DOWNLOAD_WORKERS_DEFAULT;
    s.fetch_timeout_sec = HISTORICAL_DB_FETCH_TIMEOUT_SEC_DEFAULT;
    s.draw_timeout_sec = HISTORICAL_DB_DRAW_TIMEOUT_SEC_DEFAULT;
    s.max_retry_attempts = HISTORICAL_DB_MAX_RETRY_ATTEMPTS_DEFAULT;
    s.retry_base_delay_ms = HISTORICAL_DB_RETRY_BASE_DELAY_MS_DEFAULT;
    s.retry_max_delay_ms = HISTORICAL_DB_RETRY_MAX_DELAY_MS_DEFAULT;
    s.retry_jitter_ms = 250;
    s.bulk_fetch_pace_ms = 0;
    s.max_fetch_draws = 0;
    s.force_full_rebuild = 0;

    load_download_settings_from_config(&s);

    s.workers = read_env_int_bounded(HISTORICAL_DB_WORKERS_ENV, s.workers, 1, 32);
    s.fetch_timeout_sec =
        read_env_int_bounded(HISTORICAL_DB_FETCH_TIMEOUT_ENV, s.fetch_timeout_sec, 5, 300);
    s.draw_timeout_sec =
        read_env_int_bounded(HISTORICAL_DB_DRAW_TIMEOUT_ENV, s.draw_timeout_sec, 5, 300);
    s.max_retry_attempts =
        read_env_int_bounded(HISTORICAL_DB_RETRY_ATTEMPTS_ENV, s.max_retry_attempts, 1, 10);
    s.retry_base_delay_ms =
        read_env_int_bounded(HISTORICAL_DB_RETRY_BASE_DELAY_ENV, s.retry_base_delay_ms, 100, 60000);
    s.retry_max_delay_ms =
        read_env_int_bounded(HISTORICAL_DB_RETRY_MAX_DELAY_ENV, s.retry_max_delay_ms, 100, 120000);
    s.retry_jitter_ms =
        read_env_int_bounded(HISTORICAL_DB_RETRY_JITTER_ENV, s.retry_jitter_ms, 0, 10000);
    s.bulk_fetch_pace_ms =
        read_env_int_bounded(HISTORICAL_DB_BULK_PACE_ENV, s.bulk_fetch_pace_ms, 0, 10000);
    s.max_fetch_draws = read_env_int_bounded(HISTORICAL_DB_MAX_FETCH_DRAWS_ENV, s.max_fetch_draws,
                                             0, HISTORICAL_DB_MAX_DATES);
    s.force_full_rebuild =
        read_env_bool(HISTORICAL_DB_FORCE_FULL_REBUILD_ENV, s.force_full_rebuild);

    if (s.retry_max_delay_ms < s.retry_base_delay_ms)
        s.retry_max_delay_ms = s.retry_base_delay_ms;

    return s;
}

static const char *game_source_key(const char *game_name)
{
    if (!game_name)
        return NULL;

    if (strcasecmp(game_name, "Eurojackpot") == 0)
        return "eurojackpot";

    if (strcasecmp(game_name, "Lotto 6aus49") == 0 || strcasecmp(game_name, "Lotto") == 0)
        return "lotto";

    return NULL;
}

static const char *game_spielart_code(const char *game_name)
{
    if (!game_name)
        return NULL;

    if (strcasecmp(game_name, "Eurojackpot") == 0)
        return "EJ";

    if (strcasecmp(game_name, "Lotto 6aus49") == 0 || strcasecmp(game_name, "Lotto") == 0)
        return "LOTTO";

    return NULL;
}

static int game_first_history_year(const char *game_name)
{
    if (!game_name)
        return EUROJACKPOT_FIRST_DRAW_YEAR;

    if (strcasecmp(game_name, "Lotto 6aus49") == 0 || strcasecmp(game_name, "Lotto") == 0)
        return LOTTO_FIRST_DRAW_YEAR;

    return EUROJACKPOT_FIRST_DRAW_YEAR;
}

static void trim_ascii_whitespace(char *s)
{
    if (!s)
        return;

    size_t start = 0;
    while (s[start] && isspace((unsigned char)s[start]))
        start++;

    size_t end = strlen(s);
    while (end > start && isspace((unsigned char)s[end - 1]))
        end--;

    if (start > 0)
        memmove(s, s + start, end - start);
    s[end - start] = '\0';
}

static int get_sources_config_path(char *out, size_t out_size)
{
    const char *override = getenv(HISTORICAL_DB_SOURCES_CONFIG_ENV);
    if (override && override[0] != '\0')
    {
        int n = snprintf(out, out_size, "%s", override);
        return (n > 0 && (size_t)n < out_size) ? 0 : -1;
    }

    const char *home = getenv("HOME");
    if (!home || home[0] == '\0')
        return -1;

    int n = snprintf(out, out_size, "%s/.config/open-lotto/sources.conf", home);
    return (n > 0 && (size_t)n < out_size) ? 0 : -1;
}

static int load_source_url_from_config(const char *game_name, char *out, size_t out_size)
{
    if (!game_name || !out || out_size == 0)
        return -1;

    char path[512];
    if (get_sources_config_path(path, sizeof(path)) != 0)
        return -1;

    FILE *fp = fopen(path, "r");
    if (!fp)
        return -1;

    char line[1024];
    int in_sources_section = 0;
    int found = -1;

    const char *lookup_key = game_source_key(game_name);
    if (!lookup_key)
    {
        fclose(fp);
        return -1;
    }

    while (fgets(line, sizeof(line), fp))
    {
        trim_ascii_whitespace(line);
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';')
            continue;

        if (line[0] == '[')
        {
            in_sources_section = (strcasecmp(line, "[sources]") == 0);
            continue;
        }

        if (!in_sources_section)
            continue;

        char *sep = strchr(line, '=');
        if (!sep)
            continue;

        *sep = '\0';
        char *key = line;
        char *value = sep + 1;
        trim_ascii_whitespace(key);
        trim_ascii_whitespace(value);

        if (strcasecmp(key, lookup_key) == 0 && value[0] != '\0')
        {
            int n = snprintf(out, out_size, "%s", value);
            if (n > 0 && (size_t)n < out_size)
                found = 0;
            break;
        }
    }

    fclose(fp);
    return found;
}

/**
 * Strictly validate a calendar date in YYYY-MM-DD form. Upstream-provided dates are
 * spliced into URLs and file paths, so anything else must be rejected.
 */
static int is_valid_iso_date(const char *s)
{
    static const int days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (!s || strlen(s) != 10 || s[4] != '-' || s[7] != '-')
        return 0;

    for (int i = 0; i < 10; i++)
    {
        if (i == 4 || i == 7)
            continue;
        if (s[i] < '0' || s[i] > '9')
            return 0;
    }

    int year = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
    int month = (s[5] - '0') * 10 + (s[6] - '0');
    int day = (s[8] - '0') * 10 + (s[9] - '0');

    if (year < 1900 || year > 2999 || month < 1 || month > 12 || day < 1)
        return 0;

    int max_day = days_in_month[month - 1];
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
        max_day = 29;

    return day <= max_day;
}

/**
 * Conservative URL character allowlist (RFC 3986 unreserved + reserved + '%').
 * Rejects whitespace, control characters, quotes, backslashes, braces and anything
 * else a shell could interpret. On Windows the URL is still passed through cmd.exe,
 * so characters cmd expands inside double quotes ('%', '!', '^') are rejected too.
 */
static int url_is_safe(const char *url)
{
    if (!url || url[0] == '\0' || url[0] == '-')
        return 0;

    for (const unsigned char *p = (const unsigned char *)url; *p; p++)
    {
        if (isalnum(*p))
            continue;

        if (strchr("-._~:/?#[]@$&'()*+,;=", *p))
            continue;

#ifdef _WIN32
        return 0;
#else
        if (*p == '!' || *p == '%')
            continue;
        return 0;
#endif
    }

    return 1;
}

/**
 * Expand {spielart}, {date} and {year} placeholders of a URL template.
 * Returns 0 on success, -1 on unknown placeholder or overflow.
 */
static int expand_url_template(const char *tmpl, const char *spielart, const char *date, int year,
                               char *out, size_t out_size)
{
    size_t j = 0;

    if (!tmpl || !out || out_size == 0)
        return -1;

    for (const char *p = tmpl; *p;)
    {
        const char *value = NULL;
        char year_buf[16];
        size_t skip = 0;

        if (strncmp(p, "{spielart}", 10) == 0)
        {
            value = spielart;
            skip = 10;
        }
        else if (strncmp(p, "{date}", 6) == 0)
        {
            value = date;
            skip = 6;
        }
        else if (strncmp(p, "{year}", 6) == 0)
        {
            snprintf(year_buf, sizeof(year_buf), "%d", year);
            value = year_buf;
            skip = 6;
        }

        if (skip > 0)
        {
            if (!value)
                return -1;
            size_t len = strlen(value);
            if (j + len >= out_size)
                return -1;
            memcpy(out + j, value, len);
            j += len;
            p += skip;
            continue;
        }

        if (j + 1 >= out_size)
            return -1;
        out[j++] = *p++;
    }

    out[j] = '\0';
    return 0;
}

static const char *url_template_from_env(const char *env_name, const char *fallback)
{
    const char *value = getenv(env_name);
    return (value && value[0] != '\0') ? value : fallback;
}

/** Format the current UTC time as an ISO-8601 timestamp (YYYY-MM-DDTHH:MM:SSZ). */
static void format_utc_now(char *out, size_t out_size)
{
    time_t now = time(NULL);
    struct tm tmv;
#ifdef _WIN32
    gmtime_s(&tmv, &now);
#else
    gmtime_r(&now, &tmv);
#endif
    if (strftime(out, out_size, "%Y-%m-%dT%H:%M:%SZ", &tmv) == 0 && out_size > 0)
        out[0] = '\0';
}

/** Write @p s as a quoted, escaped JSON string. */
static void json_write_string(FILE *fp, const char *s)
{
    fputc('"', fp);
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++)
    {
        switch (*p)
        {
        case '"':
            fputs("\\\"", fp);
            break;
        case '\\':
            fputs("\\\\", fp);
            break;
        case '\b':
            fputs("\\b", fp);
            break;
        case '\f':
            fputs("\\f", fp);
            break;
        case '\n':
            fputs("\\n", fp);
            break;
        case '\r':
            fputs("\\r", fp);
            break;
        case '\t':
            fputs("\\t", fp);
            break;
        default:
            if (*p < 0x20)
                fprintf(fp, "\\u%04x", (unsigned int)*p);
            else
                fputc(*p, fp);
            break;
        }
    }
    fputc('"', fp);
}

static void sleep_ms(unsigned int delay_ms)
{
#ifdef _WIN32
    Sleep(delay_ms);
#else
    usleep((useconds_t)delay_ms * 1000U);
#endif
}

static unsigned int retry_delay_ms(int attempt)
{
    unsigned int delay = (unsigned int)g_download_settings.retry_base_delay_ms;
    for (int i = 1; i < attempt; i++)
    {
        if (delay >= (unsigned int)g_download_settings.retry_max_delay_ms / 2U)
            return (unsigned int)g_download_settings.retry_max_delay_ms;
        delay *= 2;
    }

    if (g_download_settings.retry_jitter_ms > 0)
    {
        unsigned int max_jitter = (unsigned int)g_download_settings.retry_jitter_ms;
        unsigned int entropy = (unsigned int)clock() ^ (unsigned int)historical_db_process_id() ^
                               (unsigned int)(attempt * 2654435761U);
        unsigned int jitter = entropy % (max_jitter + 1U);

        if (delay + jitter > (unsigned int)g_download_settings.retry_max_delay_ms)
            return (unsigned int)g_download_settings.retry_max_delay_ms;

        return delay + jitter;
    }

    return delay;
}

#ifndef _WIN32
/**
 * Run curl without a shell (posix_spawnp + argv array) and capture its stdout.
 * posix_spawnp is used instead of fork/execvp because downloads run on several
 * OpenMP threads, and fork() in a multithreaded process is only safe for
 * async-signal-safe work in the child.
 *
 * @param exit_code receives curl's exit status (or -1 when it did not exit normally)
 * @return heap buffer with the response (possibly empty), or NULL when spawning failed
 */
static char *run_curl_capture(const char *url, int timeout_sec, int *exit_code)
{
    int fds[2];
    char timeout_buf[16];
    posix_spawn_file_actions_t actions;
    pid_t pid;

    *exit_code = -1;
    snprintf(timeout_buf, sizeof(timeout_buf), "%d", timeout_sec);

    if (pipe(fds) != 0)
        return NULL;

    /* Keep pipe ends from leaking into curl processes spawned by other threads. */
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);

    if (posix_spawn_file_actions_init(&actions) != 0)
    {
        close(fds[0]);
        close(fds[1]);
        return NULL;
    }
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, fds[0]);

    char *const argv[] = {"curl", "-fsSL", "--max-time", timeout_buf, "--url", (char *)url, NULL};
    int spawn_rc = posix_spawnp(&pid, "curl", &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);

    if (spawn_rc != 0)
    {
        close(fds[0]);
        errno = spawn_rc;
        return NULL;
    }

    FILE *fp = fdopen(fds[0], "rb");
    char *data = NULL;
    if (fp)
    {
        data = read_stream(fp);
        fclose(fp);
    }
    else
    {
        close(fds[0]);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0)
    {
        if (errno != EINTR)
        {
            free(data);
            return NULL;
        }
    }

    if (WIFEXITED(status))
        *exit_code = WEXITSTATUS(status);

    if (!data)
        data = calloc(1, 1);
    return data;
}
#endif

static char *fetch_url_text_with_retries(const char *url, int timeout_sec, const char *context)
{
    if (!url_is_safe(url))
    {
        log_error("[historical_db] %s: refusing URL with disallowed characters", context);
        return NULL;
    }

    for (int attempt = 1; attempt <= g_download_settings.max_retry_attempts; attempt++)
    {
#ifdef _WIN32
        char cmd[1024];
        int n =
            snprintf(cmd, sizeof(cmd), "curl -fsSL --max-time %d --url \"%s\"", timeout_sec, url);
        if (n < 0 || (size_t)n >= sizeof(cmd))
            return NULL;

        FILE *pipe = popen(cmd, "r");
        if (!pipe)
        {
            log_error("[historical_db] %s: popen failed for URL: %s", context, url);
            return NULL;
        }

        char *data = read_stream(pipe);
        int rc = pclose(pipe);
#else
        int rc = -1;
        char *data = run_curl_capture(url, timeout_sec, &rc);
        if (!data && rc == -1)
        {
            log_error("[historical_db] %s: failed to run curl for URL: %s (%s)", context, url,
                      strerror(errno));
            return NULL;
        }
#endif

        if (rc == 0 && data)
        {
            log_debug("[historical_db] %s: received response (%zu bytes)", context, strlen(data));
            return data;
        }

        free(data);
        log_warn("[historical_db] %s: curl failed (exit=%d, attempt=%d/%d, timeout=%ds)", context,
                 rc, attempt, g_download_settings.max_retry_attempts, timeout_sec);

        if (attempt < g_download_settings.max_retry_attempts)
        {
            unsigned int delay = retry_delay_ms(attempt);
            log_debug("[historical_db] %s: retrying after %u ms (attempt=%d)", context, delay,
                      attempt);
            sleep_ms(delay);
        }
    }

    log_error("[historical_db] %s: giving up after %d attempts for URL: %s", context,
              g_download_settings.max_retry_attempts, url);
    return NULL;
}

static int date_already_collected(const char *dates, int count, const char *date)
{
    if (!dates || !date)
        return 0;

    for (int i = 0; i < count; i++)
    {
        const char *candidate = dates + (size_t)i * 16;
        if (strcmp(candidate, date) == 0)
            return 1;
    }

    return 0;
}

static int collect_game_dates_all_years(const char *game_name, char dates[][16], int max_dates,
                                        int *failed_years)
{
    const char *spielart = game_spielart_code(game_name);
    if (!spielart)
        return 0;

    time_t now = time(NULL);
    struct tm tmv;
#ifdef _WIN32
    gmtime_s(&tmv, &now);
#else
    gmtime_r(&now, &tmv);
#endif

    int current_year = tmv.tm_year + 1900;
    int first_year = game_first_history_year(game_name);
    int total = 0;
    const char *tmpl = url_template_from_env(HISTORICAL_DB_HISTORY_URL_TEMPLATE_ENV,
                                             HISTORICAL_DB_HISTORY_URL_TEMPLATE_DEFAULT);

    for (int year = current_year; year >= first_year; year--)
    {
        char url[1024];
        if (expand_url_template(tmpl, spielart, NULL, year, url, sizeof(url)) != 0)
        {
            log_error("[historical_db] invalid %s template",
                      HISTORICAL_DB_HISTORY_URL_TEMPLATE_ENV);
            break;
        }

        char *year_json = fetch_url_text(url);
        if (!year_json)
        {
            log_warn("[historical_db] sync_latest: yearly history fetch failed for game='%s' "
                     "year=%d",
                     game_name, year);
            if (failed_years)
                (*failed_years)++;
        }
        else
        {
            char year_dates[200][16];
            int year_count = parse_history_dates(year_json, year_dates, 200);
            free(year_json);

            for (int i = 0; i < year_count && total < max_dates; i++)
            {
                if (!date_already_collected(&dates[0][0], total, year_dates[i]))
                {
                    snprintf(dates[total], 16, "%.15s", year_dates[i]);
                    total++;
                }
            }
        }

        /* A template without {year} returns the full history in one request. */
        if (total >= max_dates || !strstr(tmpl, "{year}"))
            break;
    }

    return total;
}

static const char *default_game_source_url(const char *game_name)
{
    const char *override = NULL;
    static char configured_url[512];
    char config_path[512];
    const char *checked_path = "(unresolved: HOME not set)";

    if (!game_name)
        return NULL;

    if (strcasecmp(game_name, "Eurojackpot") == 0)
    {
        override = getenv(HISTORICAL_DB_URL_ENV_EUROJACKPOT);
        if (override && override[0] != '\0')
            return override;

        if (get_sources_config_path(config_path, sizeof(config_path)) == 0)
            checked_path = config_path;

        if (load_source_url_from_config(game_name, configured_url, sizeof(configured_url)) == 0)
            return configured_url;

        log_warn("[historical_db] no source URL configured for Eurojackpot; checked %s; "
                 "set [sources] eurojackpot in sources config, or set %s, or set %s",
                 checked_path, HISTORICAL_DB_SOURCES_CONFIG_ENV, HISTORICAL_DB_URL_ENV_EUROJACKPOT);
        return NULL;
    }

    if (strcasecmp(game_name, "Lotto 6aus49") == 0 || strcasecmp(game_name, "Lotto") == 0)
    {
        override = getenv(HISTORICAL_DB_URL_ENV_LOTTO);
        if (override && override[0] != '\0')
            return override;

        if (get_sources_config_path(config_path, sizeof(config_path)) == 0)
            checked_path = config_path;

        if (load_source_url_from_config(game_name, configured_url, sizeof(configured_url)) == 0)
            return configured_url;

        log_warn("[historical_db] no source URL configured for Lotto 6aus49; checked %s; "
                 "set [sources] lotto in sources config, or set %s, or set %s",
                 checked_path, HISTORICAL_DB_SOURCES_CONFIG_ENV, HISTORICAL_DB_URL_ENV_LOTTO);
        return NULL;
    }

    return NULL;
}

static int mkdir_single(const char *path)
{
    if (!path || path[0] == '\0')
        return -1;

#ifdef _WIN32
    int rc = _mkdir(path);
#else
    int rc = mkdir(path, 0755);
#endif
    if (rc == 0 || errno == EEXIST)
        return 0;

    return -1;
}

static int mkdir_p(const char *path)
{
    if (!path || path[0] == '\0')
        return -1;

    char tmp[512];
    size_t len = strlen(path);
    if (len >= sizeof(tmp))
        return -1;

    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (size_t i = 1; i < len; i++)
    {
        if (tmp[i] == '/')
        {
            tmp[i] = '\0';
            if (mkdir_single(tmp) != 0)
                return -1;
            tmp[i] = '/';
        }
    }

    return mkdir_single(tmp);
}

static const char *historical_db_default_root(void)
{
    static char path[512];
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0')
        home = ".";

    snprintf(path, sizeof(path), "%s/.local/share/open-lotto/history", home);
    return path;
}

static void game_to_slug(const char *game_name, char *out, size_t out_size)
{
    size_t j = 0;
    if (!out || out_size == 0)
        return;

    if (!game_name)
    {
        out[0] = '\0';
        return;
    }

    for (size_t i = 0; game_name[i] != '\0' && j + 1 < out_size; i++)
    {
        unsigned char c = (unsigned char)game_name[i];
        if (isalnum(c))
            out[j++] = (char)tolower(c);
        else if (c == ' ' || c == '-' || c == '_')
            out[j++] = '_';
    }

    out[j] = '\0';
}

static int build_snapshot_path(const char *game_name, const char *db_root, char *out,
                               size_t out_size)
{
    char slug[128];
    const char *root = db_root && db_root[0] != '\0' ? db_root : historical_db_default_root();

    game_to_slug(game_name, slug, sizeof(slug));
    if (slug[0] == '\0')
        return -1;

    int n = snprintf(out, out_size, "%s/%s_gewinnzahlen.json", root, slug);
    if (n < 0 || (size_t)n >= out_size)
        return -1;

    return 0;
}

static int build_checkpoint_dir_path(const char *snapshot_path, char *out, size_t out_size)
{
    if (!snapshot_path || !out || out_size == 0)
        return -1;

    int n = snprintf(out, out_size, "%s.sync_cache", snapshot_path);
    if (n < 0 || (size_t)n >= out_size)
        return -1;

    return 0;
}

static int build_checkpoint_entry_path(const char *checkpoint_dir, const char *date, char *out,
                                       size_t out_size)
{
    if (!checkpoint_dir || !date || !out || out_size == 0)
        return -1;

    /* The date becomes a path component: only accept strict YYYY-MM-DD. */
    if (!is_valid_iso_date(date))
        return -1;

    int n = snprintf(out, out_size, "%s/%s.json", checkpoint_dir, date);
    if (n < 0 || (size_t)n >= out_size)
        return -1;

    return 0;
}

static unsigned long historical_db_process_id(void)
{
#ifdef _WIN32
    return (unsigned long)GetCurrentProcessId();
#else
    return (unsigned long)getpid();
#endif
}

static int build_atomic_tmp_path(const char *path, char *out, size_t out_size)
{
    if (!path || !out || out_size == 0)
        return -1;

    int n = snprintf(out, out_size, "%s.tmp.%lu.%ld", path, historical_db_process_id(),
                     (long)time(NULL));
    if (n < 0 || (size_t)n >= out_size)
        return -1;

    return 0;
}

static FILE *open_atomic_write_tmp(const char *path, char *tmp_path, size_t tmp_path_size)
{
    if (build_atomic_tmp_path(path, tmp_path, tmp_path_size) != 0)
        return NULL;

    return fopen(tmp_path, "wb");
}

static int replace_file_atomically(const char *tmp_path, const char *path)
{
#ifdef _WIN32
    remove(path);
#endif
    if (rename(tmp_path, path) != 0)
    {
        remove(tmp_path);
        return -1;
    }

    return 0;
}

static int commit_atomic_write(FILE *fp, const char *tmp_path, const char *path)
{
    if (!fp || !tmp_path || !path)
        return -1;

    if (fflush(fp) != 0)
    {
        fclose(fp);
        remove(tmp_path);
        return -1;
    }

    if (fclose(fp) != 0)
    {
        remove(tmp_path);
        return -1;
    }

    return replace_file_atomically(tmp_path, path);
}

static int build_sync_lock_path(const char *snapshot_path, char *out, size_t out_size)
{
    if (!snapshot_path || !out || out_size == 0)
        return -1;

    int n = snprintf(out, out_size, "%s.lock", snapshot_path);
    if (n < 0 || (size_t)n >= out_size)
        return -1;

    return 0;
}

static int open_lock_file_exclusive(const char *lock_path)
{
#ifdef _WIN32
    return _open(lock_path, _O_CREAT | _O_EXCL | _O_WRONLY, _S_IREAD | _S_IWRITE);
#else
    return open(lock_path, O_CREAT | O_EXCL | O_WRONLY, 0600);
#endif
}

/**
 * Decide whether an existing lock file was abandoned: its owner PID is gone
 * (POSIX only) or it is older than HISTORICAL_DB_LOCK_STALE_SEC.
 */
static int sync_lock_is_stale(const char *lock_path)
{
    struct stat st;
    if (stat(lock_path, &st) != 0)
        return 0;

    double age = difftime(time(NULL), st.st_mtime);
    if (age > (double)HISTORICAL_DB_LOCK_STALE_SEC)
    {
        log_warn("[historical_db] lock '%s' is %.0f s old; treating as stale", lock_path, age);
        return 1;
    }

#ifndef _WIN32
    FILE *fp = fopen(lock_path, "r");
    if (!fp)
        return 0;

    char buf[64] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[n] = '\0';

    char *end = NULL;
    errno = 0;
    long pid = strtol(buf, &end, 10);
    if (end == buf || errno != 0 || pid <= 0 || pid > INT_MAX)
        return 0; /* Unknown owner (e.g. lock being written right now): rely on age only. */

    if (kill((pid_t)pid, 0) != 0 && errno == ESRCH)
    {
        log_warn("[historical_db] lock '%s' owner pid %ld is not running; treating as stale",
                 lock_path, pid);
        return 1;
    }
#endif

    return 0;
}

static int acquire_sync_lock(const char *snapshot_path, char *lock_path, size_t lock_path_size)
{
    if (build_sync_lock_path(snapshot_path, lock_path, lock_path_size) != 0)
        return -1;

    int fd = open_lock_file_exclusive(lock_path);
    if (fd < 0 && errno == EEXIST && sync_lock_is_stale(lock_path))
    {
        remove(lock_path);
        fd = open_lock_file_exclusive(lock_path);
    }
    if (fd < 0)
        return -1;

    char pid_buf[32];
    int len = snprintf(pid_buf, sizeof(pid_buf), "%lu\n", historical_db_process_id());
#ifdef _WIN32
    int wrote = _write(fd, pid_buf, (unsigned int)len);
    _close(fd);
#else
    ssize_t wrote = write(fd, pid_buf, (size_t)len);
    close(fd);
#endif
    if (wrote != len)
        log_warn("[historical_db] failed to record pid in lock '%s'", lock_path);

    return 0;
}

static void release_sync_lock(const char *lock_path)
{
    if (!lock_path || lock_path[0] == '\0')
        return;

    remove(lock_path);
}

static int write_text_file(const char *path, const char *content)
{
    char tmp_path[1024];
    if (!path || !content)
        return -1;

    FILE *fp = open_atomic_write_tmp(path, tmp_path, sizeof(tmp_path));
    if (!fp)
        return -1;

    size_t len = strlen(content);
    size_t wrote = fwrite(content, 1, len, fp);
    if (wrote != len)
    {
        fclose(fp);
        remove(tmp_path);
        return -1;
    }

    if (commit_atomic_write(fp, tmp_path, path) != 0)
        return -1;

    return 0;
}

static int load_cached_draw_json(const char *cache_path, char **out_json)
{
    if (!cache_path || !out_json)
        return -1;

    return read_file_text(cache_path, out_json);
}

static void cleanup_checkpoint_cache(const char *checkpoint_dir, char dates[][16], int count)
{
    if (!checkpoint_dir || !dates || count <= 0)
        return;

    for (int i = 0; i < count; i++)
    {
        char entry_path[1024];
        if (build_checkpoint_entry_path(checkpoint_dir, dates[i], entry_path, sizeof(entry_path)) ==
            0)
        {
            remove(entry_path);
        }
    }

    char marker_path[1024];
    if (snprintf(marker_path, sizeof(marker_path), "%s/%s", checkpoint_dir,
                 HISTORICAL_DB_PENDING_MARKER) < (int)sizeof(marker_path))
        remove(marker_path);

#ifdef _WIN32
    _rmdir(checkpoint_dir);
#else
    rmdir(checkpoint_dir);
#endif
}

static char *read_stream(FILE *fp)
{
    size_t cap = READ_CHUNK;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf)
        return NULL;

    while (!feof(fp))
    {
        if (len + READ_CHUNK + 1 > cap)
        {
            size_t new_cap = cap * 2;
            char *new_buf = realloc(buf, new_cap);
            if (!new_buf)
            {
                free(buf);
                return NULL;
            }
            buf = new_buf;
            cap = new_cap;
        }

        size_t n = fread(buf + len, 1, READ_CHUNK, fp);
        len += n;
        if (ferror(fp))
        {
            free(buf);
            return NULL;
        }
    }

    buf[len] = '\0';
    return buf;
}

static char *fetch_url_text(const char *url)
{
    log_debug("[historical_db] fetch_url_text: GET %s", url);

    return fetch_url_text_with_retries(url, g_download_settings.fetch_timeout_sec,
                                       "fetch_url_text");
}

static const char *skip_ws(const char *p)
{
    while (p && *p && isspace((unsigned char)*p))
        p++;
    return p;
}

static int hex4_value(const char *p, unsigned int *out)
{
    unsigned int v = 0;
    for (int i = 0; i < 4; i++)
    {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9')
            v |= (unsigned int)(c - '0');
        else if (c >= 'a' && c <= 'f')
            v |= (unsigned int)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v |= (unsigned int)(c - 'A' + 10);
        else
            return -1;
    }
    *out = v;
    return 0;
}

/** Encode a Unicode code point as UTF-8; returns the byte count (0 if invalid). */
static size_t utf8_encode(unsigned int cp, char out[4])
{
    if (cp < 0x80)
    {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800)
    {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000)
    {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF)
    {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

/**
 * Skip a JSON string literal. @p p must point at the opening quote.
 * @return pointer just past the closing quote, or NULL if unterminated.
 */
static const char *json_skip_string(const char *p)
{
    if (!p || *p != '"')
        return NULL;

    for (p++; *p; p++)
    {
        if (*p == '\\')
        {
            if (p[1] == '\0')
                return NULL;
            p++;
        }
        else if (*p == '"')
        {
            return p + 1;
        }
    }

    return NULL;
}

/**
 * Find the end of a JSON object/array starting at @p p ('{' or '['), ignoring
 * brackets inside string literals.
 * @return pointer just past the matching closing bracket, or NULL.
 */
static const char *json_find_container_end(const char *p)
{
    int depth = 0;

    if (!p || (*p != '{' && *p != '['))
        return NULL;

    while (*p)
    {
        if (*p == '"')
        {
            p = json_skip_string(p);
            if (!p)
                return NULL;
            continue;
        }

        if (*p == '{' || *p == '[')
            depth++;
        else if (*p == '}' || *p == ']')
        {
            depth--;
            if (depth == 0)
                return p + 1;
        }
        p++;
    }

    return NULL;
}

/**
 * Decode a JSON string value (or null) into @p out.
 * Handles \" \\ \/ \b \f \n \r \t and \uXXXX (including surrogate pairs) as UTF-8.
 * @return 0 on success, -1 on malformed input, JSON_STRING_TRUNCATED when the
 *         value did not fit (out then holds a NUL-terminated, UTF-8-safe prefix).
 */
static int parse_json_string_value(const char *p, char *out, size_t out_size)
{
    size_t j = 0;
    int truncated = 0;

    if (!p || !out || out_size == 0)
        return -1;

    out[0] = '\0';
    p = skip_ws(p);
    if (strncmp(p, "null", 4) == 0)
        return 0;

    if (*p != '"')
        return -1;
    p++;

    while (*p && *p != '"')
    {
        char buf[4];
        size_t len = 0;

        if ((unsigned char)*p < 0x20)
            return -1;

        if (*p == '\\')
        {
            p++;
            switch (*p)
            {
            case '"':
            case '\\':
            case '/':
                buf[0] = *p;
                len = 1;
                break;
            case 'b':
                buf[0] = '\b';
                len = 1;
                break;
            case 'f':
                buf[0] = '\f';
                len = 1;
                break;
            case 'n':
                buf[0] = '\n';
                len = 1;
                break;
            case 'r':
                buf[0] = '\r';
                len = 1;
                break;
            case 't':
                buf[0] = '\t';
                len = 1;
                break;
            case 'u':
            {
                unsigned int cp = 0;
                if (hex4_value(p + 1, &cp) != 0)
                    return -1;
                p += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF)
                {
                    unsigned int lo = 0;
                    if (p[1] != '\\' || p[2] != 'u' || hex4_value(p + 3, &lo) != 0 || lo < 0xDC00 ||
                        lo > 0xDFFF)
                        return -1;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    p += 6;
                }
                else if (cp >= 0xDC00 && cp <= 0xDFFF)
                {
                    return -1;
                }
                if (cp == 0)
                    return -1; /* Embedded NUL cannot be represented. */
                len = utf8_encode(cp, buf);
                if (len == 0)
                    return -1;
                break;
            }
            default:
                return -1;
            }
            p++;
        }
        else
        {
            buf[0] = *p++;
            len = 1;
        }

        if (truncated)
            continue;

        if (j + len >= out_size)
        {
            truncated = 1;
            continue;
        }

        memcpy(out + j, buf, len);
        j += len;
    }

    if (*p != '"')
        return -1;

    if (truncated)
    {
        /* Drop an incomplete trailing UTF-8 sequence copied from raw bytes. */
        size_t k = j;
        while (k > 0 && ((unsigned char)out[k - 1] & 0xC0) == 0x80)
            k--;
        if (k > 0 && ((unsigned char)out[k - 1] & 0x80))
        {
            unsigned char lead = (unsigned char)out[k - 1];
            size_t need = (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : 4;
            if (j - (k - 1) < need)
                j = k - 1;
        }
    }

    out[j] = '\0';
    return truncated ? JSON_STRING_TRUNCATED : 0;
}

/** Strictly parse a base-10 integer within [min_value, max_value]. */
static int parse_int_strict(const char *s, int *out, long min_value, long max_value)
{
    char *end = NULL;

    if (!s || !out)
        return -1;

    s = skip_ws(s);
    if (*s == '\0')
        return -1;

    errno = 0;
    long v = strtol(s, &end, 10);
    if (end == s || errno == ERANGE || v < min_value || v > max_value)
        return -1;

    end = (char *)skip_ws(end);
    if (*end != '\0')
        return -1;

    *out = (int)v;
    return 0;
}

static const char *find_key(const char *json, const char *key)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    return strstr(json, needle);
}

static int parse_string_field(const char *json, const char *key, char *out, size_t out_size)
{
    const char *k = find_key(json, key);
    const char *colon;

    if (!k)
        return -1;

    colon = strchr(k, ':');
    if (!colon)
        return -1;

    return parse_json_string_value(colon + 1, out, out_size);
}

static int parse_number_array(const char *p, int *out, int max_count)
{
    int count = 0;
    const char *q = strchr(p, '[');
    if (!q)
        return -1;
    q++;

    while (*q && *q != ']' && count < max_count)
    {
        q = skip_ws(q);
        if (*q == ',')
        {
            q++;
            continue;
        }

        if (*q == '"')
        {
            char tmp[16];
            if (parse_json_string_value(q, tmp, sizeof(tmp)) != 0)
                return -1;
            if (parse_int_strict(tmp, &out[count], INT_MIN, INT_MAX) != 0)
                return -1;
            count++;
            q = json_skip_string(q);
            if (!q)
                return -1;
            continue;
        }

        if (*q == '-' || isdigit((unsigned char)*q))
        {
            char *end = NULL;
            errno = 0;
            long v = strtol(q, &end, 10);
            if (end == q || errno == ERANGE || v < INT_MIN || v > INT_MAX)
                return -1;
            out[count++] = (int)v;
            q = end;
            continue;
        }

        q++;
    }

    return count;
}

static int parse_double_field(const char *json, const char *key, double *out)
{
    const char *k = find_key(json, key);
    const char *colon;
    char *end = NULL;

    if (!k || !out)
        return -1;

    colon = strchr(k, ':');
    if (!colon)
        return -1;

    *out = strtod(colon + 1, &end);
    return (end != colon + 1) ? 0 : -1;
}

static int parse_int_field(const char *json, const char *key, int *out)
{
    const char *k = find_key(json, key);
    const char *colon;
    char *end = NULL;

    if (!k || !out)
        return -1;

    colon = strchr(k, ':');
    if (!colon)
        return -1;

    errno = 0;
    long v = strtol(colon + 1, &end, 10);
    if (end == colon + 1 || errno == ERANGE || v < INT_MIN || v > INT_MAX)
        return -1;

    *out = (int)v;
    return 0;
}

static int validate_snapshot_integrity(const HistoricalDrawSnapshot *snap)
{
    if (!snap)
        return -1;

    if (snap->game[0] == '\0' || snap->draw_date[0] == '\0' || snap->next_draw_date[0] == '\0' ||
        snap->source_url[0] == '\0')
    {
        return -1;
    }

    if (snap->main_count <= 0 || snap->main_count > HISTORICAL_DB_MAX_MAIN_NUMBERS)
        return -1;

    if (snap->extra_count <= 0 || snap->extra_count > HISTORICAL_DB_MAX_EXTRA_NUMBERS)
        return -1;

    if (snap->winning_class_count < 0 ||
        snap->winning_class_count > HISTORICAL_DB_MAX_WINNING_CLASSES)
    {
        return -1;
    }

    return 0;
}

/** Parse a string field, accepting a truncated value with a warning. */
static int parse_string_field_lenient(const char *json, const char *key, char *out, size_t out_size)
{
    int rc = parse_string_field(json, key, out, out_size);
    if (rc == JSON_STRING_TRUNCATED)
    {
        log_warn("[historical_db] field '%s' truncated to %zu bytes", key, out_size - 1);
        return 0;
    }
    return rc;
}

static int parse_winning_classes(const char *json, HistoricalDrawSnapshot *snap)
{
    const char *k = find_key(json, "gewinnklassen");
    const char *arr;
    int idx = 0;

    if (!k || !snap)
        return -1;

    arr = strchr(k, '[');
    if (!arr)
        return -1;

    for (const char *p = arr + 1; *p && *p != ']' && idx < HISTORICAL_DB_MAX_WINNING_CLASSES;)
    {
        if (*p == '"')
        {
            p = json_skip_string(p);
            if (!p)
                return -1;
            continue;
        }

        if (*p != '{')
        {
            p++;
            continue;
        }

        const char *end = json_find_container_end(p);
        if (!end)
            return -1;

        size_t span = (size_t)(end - p);
        char obj[1024];
        if (span >= sizeof(obj))
            span = sizeof(obj) - 1;
        memcpy(obj, p, span);
        obj[span] = '\0';

        HistoricalWinningClass *wc = &snap->winning_classes[idx];
        int winners = 0;
        double payout = 0.0;

        if (parse_string_field(obj, "klasse", wc->class_id, sizeof(wc->class_id)) != 0 ||
            wc->class_id[0] == '\0')
            snprintf(wc->class_id, sizeof(wc->class_id), "%d", idx + 1);

        if (parse_string_field_lenient(obj, "beschreibung", wc->description,
                                       sizeof(wc->description)) != 0 ||
            wc->description[0] == '\0')
        {
            if (parse_string_field_lenient(obj, "kurzbeschreibung", wc->description,
                                           sizeof(wc->description)) != 0 ||
                wc->description[0] == '\0')
            {
                snprintf(wc->description, sizeof(wc->description), "Class %d", idx + 1);
            }
        }

        if (parse_int_field(obj, "anzahl", &winners) != 0)
            winners = 0;
        if (parse_double_field(obj, "quote", &payout) != 0)
            payout = 0.0;

        wc->winners = winners;
        wc->payout = payout;
        idx++;
        p = end;
    }

    snap->winning_class_count = idx;
    return idx > 0 ? 0 : -1;
}

/** Check that every number lies in [min_value, max_value]. */
static int numbers_in_range(const int *numbers, int count, int min_value, int max_value)
{
    for (int i = 0; i < count; i++)
    {
        if (numbers[i] < min_value || numbers[i] > max_value)
            return 0;
    }
    return 1;
}

/** Parse head.datum and head.folgeZiehung.datum, requiring strict YYYY-MM-DD dates. */
static int parse_head_dates(const char *json, HistoricalDrawSnapshot *snap)
{
    const char *head = find_key(json, "head");
    const char *follow;

    if (!head)
        return -1;

    if (parse_string_field(head, "datum", snap->draw_date, sizeof(snap->draw_date)) != 0 ||
        !is_valid_iso_date(snap->draw_date))
        return -1;

    follow = find_key(head, "folgeZiehung");
    if (!follow ||
        parse_string_field(follow, "datum", snap->next_draw_date, sizeof(snap->next_draw_date)) !=
            0 ||
        !is_valid_iso_date(snap->next_draw_date))
    {
        return -1;
    }

    return 0;
}

static int parse_eurojackpot_json(const char *json, HistoricalDrawSnapshot *snap)
{
    const int expected_main_count = 5;
    const int expected_extra_count = 2;
    const char *numbers_root;
    const char *first_numbers;
    const char *second_numbers;

    if (!json || !snap)
        return -1;

    memset(snap, 0, sizeof(*snap));
    strncpy(snap->game, "Eurojackpot", sizeof(snap->game) - 1);

    if (parse_head_dates(json, snap) != 0)
        return -1;

    numbers_root = find_key(json, "zahlen");
    if (!numbers_root)
        return -1;

    numbers_root = find_key(numbers_root, "hauptlotterie");
    if (!numbers_root)
        return -1;

    numbers_root = find_key(numbers_root, "ziehungen");
    if (!numbers_root)
        return -1;

    first_numbers = find_key(numbers_root, "zahlenSortiert");
    if (!first_numbers)
        return -1;

    second_numbers = find_key(first_numbers + 1, "zahlenSortiert");
    if (!second_numbers)
        return -1;

    snap->main_count = parse_number_array(first_numbers, snap->main_numbers, expected_main_count);
    snap->extra_count =
        parse_number_array(second_numbers, snap->extra_numbers, expected_extra_count);

    if (snap->main_count != expected_main_count || snap->extra_count != expected_extra_count)
        return -1;

    if (!numbers_in_range(snap->main_numbers, snap->main_count, 1, 50) ||
        !numbers_in_range(snap->extra_numbers, snap->extra_count, 1, 12))
        return -1;

    if (parse_winning_classes(json, snap) != 0)
        return -1;

    return 0;
}

static int parse_lotto_json(const char *json, HistoricalDrawSnapshot *snap)
{
    const int expected_main_count = 6;
    const int expected_extra_count = 1;
    const char *numbers_root;
    const char *first_numbers;
    char superzahl[16];

    if (!json || !snap)
        return -1;

    memset(snap, 0, sizeof(*snap));
    strncpy(snap->game, "Lotto 6aus49", sizeof(snap->game) - 1);

    if (parse_head_dates(json, snap) != 0)
        return -1;

    numbers_root = find_key(json, "zahlen");
    if (!numbers_root)
        return -1;

    numbers_root = find_key(numbers_root, "hauptlotterie");
    if (!numbers_root)
        return -1;

    numbers_root = find_key(numbers_root, "ziehungen");
    if (!numbers_root)
        return -1;

    first_numbers = find_key(numbers_root, "zahlenSortiert");
    if (!first_numbers)
        return -1;

    snap->main_count = parse_number_array(first_numbers, snap->main_numbers, expected_main_count);
    if (snap->main_count != expected_main_count ||
        !numbers_in_range(snap->main_numbers, snap->main_count, 1, 49))
        return -1;

    if (parse_string_field(numbers_root, "superzahl", superzahl, sizeof(superzahl)) != 0 ||
        parse_int_strict(superzahl, &snap->extra_numbers[0], 0, 9) != 0)
    {
        return -1;
    }

    snap->extra_count = expected_extra_count;
    snap->winning_class_count = 0;
    return 0;
}

static int parse_game_json(const char *game_name, const char *json, HistoricalDrawSnapshot *snap)
{
    if (!game_name || !json || !snap)
        return -1;

    if (strcasecmp(game_name, "Eurojackpot") == 0)
        return parse_eurojackpot_json(json, snap);

    if (strcasecmp(game_name, "Lotto 6aus49") == 0 || strcasecmp(game_name, "Lotto") == 0)
        return parse_lotto_json(json, snap);

    return -1;
}

static int parse_history_dates(const char *json, char dates[][16], int max_dates)
{
    int count = 0;
    const char *history_key = find_key(json, "history");
    if (!history_key)
        return 0;

    const char *tage_key = find_key(history_key, "tage");
    if (!tage_key)
        return 0;

    const char *arr = strchr(tage_key, '[');
    if (!arr)
        return 0;

    for (const char *p = arr + 1; *p && *p != ']' && count < max_dates;)
    {
        if (*p != '"')
        {
            p++;
            continue;
        }

        char date_str[16];
        int rc = parse_json_string_value(p, date_str, sizeof(date_str));
        p = json_skip_string(p);
        if (!p)
            break;

        if (rc == 0 && is_valid_iso_date(date_str))
        {
            snprintf(dates[count], 16, "%s", date_str);
            count++;
        }
        else
        {
            log_warn("[historical_db] ignoring invalid draw date in upstream history");
        }
    }

    return count;
}

static int build_draw_url(const char *spielart, const char *date, char *out, size_t out_size)
{
    const char *tmpl = url_template_from_env(HISTORICAL_DB_DRAW_URL_TEMPLATE_ENV,
                                             HISTORICAL_DB_DRAW_URL_TEMPLATE_DEFAULT);
    return expand_url_template(tmpl, spielart, date, 0, out, out_size);
}

static char *fetch_draw_for_date(const char *game_name, const char *date)
{
    if (!game_name || !date)
        return NULL;

    const char *spielart = game_spielart_code(game_name);
    if (!spielart)
        return NULL;

    if (!is_valid_iso_date(date))
    {
        log_warn("[historical_db] fetch_draw_for_date: rejecting invalid date");
        return NULL;
    }

    char url[1024];
    if (build_draw_url(spielart, date, url, sizeof(url)) != 0)
    {
        log_error("[historical_db] invalid %s template", HISTORICAL_DB_DRAW_URL_TEMPLATE_ENV);
        return NULL;
    }

    log_debug("[historical_db] fetch_draw_for_date: fetching draw for game=%s date=%s", game_name,
              date);

    char *data = fetch_url_text_with_retries(url, g_download_settings.draw_timeout_sec,
                                             "fetch_draw_for_date");
    if (!data)
    {
        log_warn("[historical_db] fetch_draw_for_date: failed after retries (game=%s date=%s)",
                 game_name, date);
        return NULL;
    }

    log_debug("[historical_db] fetch_draw_for_date: received response for %s (%zu bytes)", date,
              strlen(data));
    return data;
}

static int read_file_text(const char *path, char **out)
{
    FILE *fp;

    if (!path || !out)
        return -1;

    fp = fopen(path, "rb");
    if (!fp)
        return -1;

    *out = read_stream(fp);
    fclose(fp);

    return *out ? 0 : -1;
}

/** Write one draw object (without trailing separator) with all strings JSON-escaped. */
static void write_draw_object(FILE *fp, const HistoricalDrawSnapshot *snap)
{
    fprintf(fp, "    {\n");
    fprintf(fp, "      \"draw_date\": ");
    json_write_string(fp, snap->draw_date);
    fprintf(fp, ",\n      \"next_draw_date\": ");
    json_write_string(fp, snap->next_draw_date);
    fprintf(fp, ",\n      \"source_url\": ");
    json_write_string(fp, snap->source_url);
    fprintf(fp, ",\n");

    fprintf(fp, "      \"main_numbers\": [");
    for (int i = 0; i < snap->main_count; i++)
        fprintf(fp, "%s%d", i == 0 ? "" : ", ", snap->main_numbers[i]);
    fprintf(fp, "],\n");

    fprintf(fp, "      \"extra_numbers\": [");
    for (int i = 0; i < snap->extra_count; i++)
        fprintf(fp, "%s%d", i == 0 ? "" : ", ", snap->extra_numbers[i]);
    fprintf(fp, "],\n");

    fprintf(fp, "      \"winning_classes\": [\n");
    for (int i = 0; i < snap->winning_class_count; i++)
    {
        const HistoricalWinningClass *wc = &snap->winning_classes[i];
        fprintf(fp, "        {\"class\": ");
        json_write_string(fp, wc->class_id);
        fprintf(fp, ", \"description\": ");
        json_write_string(fp, wc->description);
        fprintf(fp, ", \"winners\": %d, \"payout\": %.2f}%s\n", wc->winners, wc->payout,
                i + 1 == snap->winning_class_count ? "" : ",");
    }
    fprintf(fp, "      ]\n");
    fprintf(fp, "    }");
}

/** Write the snapshot file header up to and including the opening of the draws array. */
static void write_snapshot_header(FILE *fp, const char *game)
{
    char ts[32];
    format_utc_now(ts, sizeof(ts));

    fprintf(fp, "{\n");
    fprintf(fp, "  \"schema_version\": %d,\n", HISTORICAL_DB_SCHEMA_VERSION);
    fprintf(fp, "  \"game\": ");
    json_write_string(fp, game);
    fprintf(fp, ",\n  \"last_sync_at\": ");
    json_write_string(fp, ts);
    fprintf(fp, ",\n  \"draws\": [\n");
}

static int write_draw_to_history(const char *path, const HistoricalDrawSnapshot *snap)
{
    FILE *fp;
    char tmp_path[1024];

    if (!path || !snap)
        return -1;

    fp = open_atomic_write_tmp(path, tmp_path, sizeof(tmp_path));
    if (!fp)
        return -1;

    write_snapshot_header(fp, snap->game);
    write_draw_object(fp, snap);
    fprintf(fp, "\n  ]\n");
    fprintf(fp, "}\n");

    if (commit_atomic_write(fp, tmp_path, path) != 0)
        return -1;

    return 0;
}

static int append_draw_to_history(const char *path, const HistoricalDrawSnapshot *snap)
{
    char *json = NULL;
    char tmp_path[1024];
    FILE *fp;

    if (!path || !snap)
        return -1;

    if (read_file_text(path, &json) != 0 || !json)
    {
        return write_draw_to_history(path, snap);
    }

    const char *draws_start = find_key(json, "draws");
    if (!draws_start)
    {
        free(json);
        return write_draw_to_history(path, snap);
    }

    const char *arr_start = strchr(draws_start, '[');
    const char *arr_end = arr_start ? json_find_container_end(arr_start) : NULL;

    if (!arr_start || !arr_end)
    {
        free(json);
        return write_draw_to_history(path, snap);
    }
    arr_end--; /* Point at the closing ']'. */

    /* Is the existing array empty? */
    const char *q = skip_ws(arr_start + 1);
    int empty = (q == arr_end);

    /* Copy everything up to the last non-whitespace character before ']'. */
    const char *copy_end = arr_end;
    while (copy_end > arr_start + 1 && isspace((unsigned char)*(copy_end - 1)))
        copy_end--;

    fp = open_atomic_write_tmp(path, tmp_path, sizeof(tmp_path));
    if (!fp)
    {
        free(json);
        return -1;
    }

    fwrite(json, 1, (size_t)(copy_end - json), fp);
    fprintf(fp, empty ? "\n" : ",\n");
    write_draw_object(fp, snap);
    fprintf(fp, "\n  ]\n");
    fprintf(fp, "}\n");

    free(json);
    if (commit_atomic_write(fp, tmp_path, path) != 0)
        return -1;

    return 0;
}

static int extract_last_draw_object(const char *json, char *draw_obj, size_t obj_size)
{
    const char *draws_key = find_key(json, "draws");
    if (!draws_key)
        return -1;

    const char *arr_start = strchr(draws_key, '[');
    if (!arr_start)
        return -1;

    const char *last_start = NULL;
    const char *last_end = NULL;

    for (const char *p = arr_start + 1; *p && *p != ']';)
    {
        if (*p == '"')
        {
            p = json_skip_string(p);
            if (!p)
                return -1;
            continue;
        }

        if (*p == '{')
        {
            const char *end = json_find_container_end(p);
            if (!end)
                return -1;
            last_start = p;
            last_end = end;
            p = end;
            continue;
        }

        p++;
    }

    if (!last_start)
        return -1;

    size_t obj_len = (size_t)(last_end - last_start);
    if (obj_len >= obj_size)
        return -1;

    memcpy(draw_obj, last_start, obj_len);
    draw_obj[obj_len] = '\0';
    return 0;
}

/** Count the draw objects stored in a local snapshot JSON document (-1 on error). */
static int count_snapshot_draws(const char *json)
{
    const char *draws_key = find_key(json, "draws");
    if (!draws_key)
        return -1;

    const char *arr_start = strchr(draws_key, '[');
    if (!arr_start)
        return -1;

    int count = 0;
    for (const char *p = arr_start + 1; *p && *p != ']';)
    {
        if (*p == '"')
        {
            p = json_skip_string(p);
            if (!p)
                return -1;
            continue;
        }

        if (*p == '{')
        {
            const char *end = json_find_container_end(p);
            if (!end)
                return -1;
            count++;
            p = end;
            continue;
        }

        p++;
    }

    return count;
}

static int parse_local_snapshot_json(const char *json, HistoricalDrawSnapshot *snap)
{
    char draw_obj[4096];
    int idx = 0;
    int schema_version = 0;

    if (!json || !snap)
        return -1;

    memset(snap, 0, sizeof(*snap));

    if (parse_int_field(json, "schema_version", &schema_version) != 0)
    {
        log_warn("[historical_db] local snapshot has no schema_version; treating as legacy format");
    }
    else if (schema_version <= 0)
    {
        log_error("[historical_db] invalid schema_version=%d in local snapshot", schema_version);
        return -1;
    }

    if (parse_string_field(json, "game", snap->game, sizeof(snap->game)) != 0)
        return -1;

    if (extract_last_draw_object(json, draw_obj, sizeof(draw_obj)) != 0)
        return -1;

    if (parse_string_field(draw_obj, "draw_date", snap->draw_date, sizeof(snap->draw_date)) != 0)
        return -1;
    if (parse_string_field(draw_obj, "next_draw_date", snap->next_draw_date,
                           sizeof(snap->next_draw_date)) != 0)
        return -1;
    if (parse_string_field(draw_obj, "source_url", snap->source_url, sizeof(snap->source_url)) != 0)
        return -1;

    const char *main_arr = find_key(draw_obj, "main_numbers");
    const char *extra_arr = find_key(draw_obj, "extra_numbers");
    if (!main_arr || !extra_arr)
        return -1;

    snap->main_count =
        parse_number_array(main_arr, snap->main_numbers, HISTORICAL_DB_MAX_MAIN_NUMBERS);
    snap->extra_count =
        parse_number_array(extra_arr, snap->extra_numbers, HISTORICAL_DB_MAX_EXTRA_NUMBERS);
    if (snap->main_count <= 0 || snap->extra_count <= 0)
        return -1;

    const char *classes = find_key(draw_obj, "winning_classes");
    if (!classes)
        return -1;

    const char *p = strchr(classes, '[');
    if (!p)
        return -1;
    for (p++; *p && *p != ']' && idx < HISTORICAL_DB_MAX_WINNING_CLASSES;)
    {
        if (*p == '"')
        {
            p = json_skip_string(p);
            if (!p)
                return -1;
            continue;
        }

        if (*p != '{')
        {
            p++;
            continue;
        }

        const char *end = json_find_container_end(p);
        if (!end)
            return -1;

        size_t span = (size_t)(end - p);
        char obj[512];
        if (span >= sizeof(obj))
            return -1;
        memcpy(obj, p, span);
        obj[span] = '\0';

        HistoricalWinningClass *wc = &snap->winning_classes[idx];
        if (parse_string_field(obj, "class", wc->class_id, sizeof(wc->class_id)) != 0)
            return -1;
        if (parse_string_field_lenient(obj, "description", wc->description,
                                       sizeof(wc->description)) != 0)
            return -1;
        if (parse_int_field(obj, "winners", &wc->winners) != 0)
            return -1;
        if (parse_double_field(obj, "payout", &wc->payout) != 0)
            return -1;

        idx++;
        p = end;
    }

    snap->winning_class_count = idx;
    return validate_snapshot_integrity(snap);
}

/**
 * @brief Load the latest locally persisted snapshot for a game.
 */
int historical_db_load_latest(const char *game_name, const char *db_root,
                              HistoricalDrawSnapshot *out_snapshot)
{
    char path[768];
    char *json = NULL;
    int rc;

    if (!game_name || !out_snapshot)
        return HISTORICAL_DB_ERR_INVALID_ARG;

    if (build_snapshot_path(game_name, db_root, path, sizeof(path)) != 0)
        return HISTORICAL_DB_ERR_INVALID_ARG;

    if (read_file_text(path, &json) != 0)
        return HISTORICAL_DB_ERR_IO;

    rc = parse_local_snapshot_json(json, out_snapshot);
    free(json);

    return rc == 0 ? HISTORICAL_DB_SYNC_UPDATED : HISTORICAL_DB_ERR_PARSE;
}

static int compare_dates_asc(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static int path_exists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0;
}

static int build_pending_marker_path(const char *checkpoint_dir, char *out, size_t out_size)
{
    int n = snprintf(out, out_size, "%s/%s", checkpoint_dir, HISTORICAL_DB_PENDING_MARKER);
    return (n < 0 || (size_t)n >= out_size) ? -1 : 0;
}

/**
 * Download every draw in @p history_dates (resuming from the checkpoint cache) and
 * rewrite the snapshot at @p path.
 *
 * On any failure the checkpoint cache and its PENDING marker are kept so the next
 * run resumes, and an existing snapshot is never replaced by one with fewer draws.
 *
 * @return HISTORICAL_DB_SYNC_UPDATED when all draws were stored, otherwise an error.
 */
static int bulk_sync_history(const char *game_name, const char *path, const char *checkpoint_dir,
                             char history_dates[][16], int history_count, int history_failures,
                             int existing_draw_count)
{
    const char *spielart = game_spielart_code(game_name);
    char marker_path[1024];

    /* Store draws oldest-first so the last entry is always the latest draw. */
    qsort(history_dates, (size_t)history_count, sizeof(history_dates[0]), compare_dates_asc);

    int max_fetch = history_count;
    if (g_download_settings.max_fetch_draws > 0 && g_download_settings.max_fetch_draws < max_fetch)
    {
        max_fetch = g_download_settings.max_fetch_draws;
        log_info("[historical_db] sync_latest: limiting bulk fetch to the %d most recent draws "
                 "due to %s",
                 max_fetch, HISTORICAL_DB_MAX_FETCH_DRAWS_ENV);
    }
    char (*dates)[16] = &history_dates[history_count - max_fetch];

    if (mkdir_p(checkpoint_dir) != 0 ||
        build_pending_marker_path(checkpoint_dir, marker_path, sizeof(marker_path)) != 0)
        return HISTORICAL_DB_ERR_IO;

    /* Mark the bulk download as in progress; removed only after a complete commit. */
    if (write_text_file(marker_path, "bulk download in progress\n") != 0)
        log_warn("[historical_db] sync_latest: failed to write resume marker '%s'", marker_path);

    BulkDrawResult *results = calloc((size_t)max_fetch, sizeof(BulkDrawResult));
    int *worker_done = calloc((size_t)g_download_settings.workers, sizeof(int));
    size_t *worker_bytes = calloc((size_t)g_download_settings.workers, sizeof(size_t));
    if (!results || !worker_done || !worker_bytes)
    {
        free(worker_done);
        free(worker_bytes);
        free(results);
        return HISTORICAL_DB_ERR_IO;
    }

    time_t download_start = time(NULL);
    size_t total_bytes = 0;
    int printed_lines = 0;

    log_info("[historical_db] sync_latest: bulk download — fetching up to %d historical draws "
             "with %d worker(s)",
             max_fetch, g_download_settings.workers);
    log_info("[historical_db] sync_latest: resume cache directory = %s", checkpoint_dir);
    fprintf(stderr, "  Fetching %d historical draws for %s\n", max_fetch, game_name);
    progress_log_tracking_begin();
    print_worker_progress_bars(0, max_fetch, download_start, 0, g_download_settings.workers,
                               worker_done, worker_bytes, &printed_lines);

    int completed = 0;

#ifdef _OPENMP
#pragma omp parallel for num_threads(g_download_settings.workers) schedule(dynamic)
#endif
    for (int i = 0; i < max_fetch; i++)
    {
#ifdef _OPENMP
        int worker_idx = omp_get_thread_num();
#else
        int worker_idx = 0;
#endif
        /* Serialize the debug log so it never races with progress bar redraws. */
#ifdef _OPENMP
#pragma omp critical(historical_db_progress)
#endif
        {
            log_debug("[historical_db] sync_latest: fetching draw %d/%d (date=%s)", i + 1,
                      max_fetch, dates[i]);
        }

        char cache_path[1024];
        char *draw_json = NULL;
        int from_cache = 0;
        /* Flag: set to 1 when a resume-cache write fails so we can emit the
         * warning inside the progress critical section and avoid interleaving. */
        int cache_write_failed = 0;
        int have_cache_path = build_checkpoint_entry_path(checkpoint_dir, dates[i], cache_path,
                                                          sizeof(cache_path)) == 0;

        if (have_cache_path && load_cached_draw_json(cache_path, &draw_json) == 0 && draw_json)
        {
            if (parse_game_json(game_name, draw_json, &results[i].draw) == 0)
            {
                from_cache = 1;
            }
            else
            {
                free(draw_json);
                draw_json = NULL;
                remove(cache_path);
            }
        }

        if (!draw_json)
        {
            if (g_download_settings.bulk_fetch_pace_ms > 0)
                sleep_ms((unsigned int)g_download_settings.bulk_fetch_pace_ms);

            draw_json = fetch_draw_for_date(game_name, dates[i]);
        }

        if (!draw_json)
        {
            results[i].fetched_ok = 0;
            results[i].parsed_ok = 0;
            results[i].bytes = 0;
        }
        else
        {
            results[i].bytes = strlen(draw_json);
            results[i].fetched_ok = 1;

            if (from_cache || parse_game_json(game_name, draw_json, &results[i].draw) == 0)
            {
                results[i].parsed_ok = 1;
                if (!from_cache && have_cache_path && write_text_file(cache_path, draw_json) != 0)
                    cache_write_failed = 1;
            }
            else
            {
                results[i].parsed_ok = 0;
            }

            free(draw_json);
        }

        /* All stderr writes (warnings + progress) share one critical section so
         * they are never interleaved with ANSI cursor-movement sequences. */
#ifdef _OPENMP
#pragma omp critical(historical_db_progress)
#endif
        {
            if (cache_write_failed)
                log_warn("[historical_db] sync_latest: failed to persist resume cache "
                         "for date=%s",
                         dates[i]);
            total_bytes += results[i].bytes;
            completed++;
            if (worker_idx < g_download_settings.workers)
            {
                worker_done[worker_idx]++;
                worker_bytes[worker_idx] += results[i].bytes;
            }
            print_worker_progress_bars(completed, max_fetch, download_start, total_bytes,
                                       g_download_settings.workers, worker_done, worker_bytes,
                                       &printed_lines);
        }
    }

    progress_log_tracking_end();
    free(worker_done);
    free(worker_bytes);

    int fetch_count = 0;
    int fetch_failures = 0;
    int parse_failures = 0;
    for (int i = 0; i < max_fetch; i++)
    {
        if (!results[i].fetched_ok)
        {
            log_warn("[historical_db] sync_latest: draw %d/%d (date=%s) could not be fetched",
                     i + 1, max_fetch, dates[i]);
            fetch_failures++;
        }
        else if (!results[i].parsed_ok)
        {
            log_warn("[historical_db] sync_latest: failed to parse draw JSON for date=%s",
                     dates[i]);
            parse_failures++;
        }
        else
        {
            fetch_count++;
        }
    }

    int failures = fetch_failures + parse_failures + history_failures;
    int error_rc = (fetch_failures + history_failures) > 0 ? HISTORICAL_DB_ERR_NETWORK
                                                           : HISTORICAL_DB_ERR_PARSE;

    if (failures > 0 && (fetch_count == 0 || fetch_count < existing_draw_count))
    {
        log_error("[historical_db] sync_latest: bulk download incomplete (%d stored, %d failed, "
                  "%d existing); keeping existing snapshot and resume cache '%s'",
                  fetch_count, failures, existing_draw_count, checkpoint_dir);
        free(results);
        return error_rc;
    }

    char tmp_path[1024];
    FILE *fp = open_atomic_write_tmp(path, tmp_path, sizeof(tmp_path));
    if (!fp)
    {
        free(results);
        return HISTORICAL_DB_ERR_IO;
    }

    write_snapshot_header(fp, game_name);
    int first = 1;
    for (int i = 0; i < max_fetch; i++)
    {
        if (!results[i].parsed_ok)
            continue;

        HistoricalDrawSnapshot *draw = &results[i].draw;
        char draw_url[1024];
        if (spielart && build_draw_url(spielart, draw->draw_date, draw_url, sizeof(draw_url)) == 0)
            snprintf(draw->source_url, sizeof(draw->source_url), "%.255s", draw_url);
        else
            draw->source_url[0] = '\0';

        if (!first)
            fprintf(fp, ",\n");
        first = 0;
        write_draw_object(fp, draw);
    }
    fprintf(fp, "\n  ]\n");
    fprintf(fp, "}\n");
    free(results);

    if (commit_atomic_write(fp, tmp_path, path) != 0)
        return HISTORICAL_DB_ERR_IO;

    if (failures > 0)
    {
        log_error("[historical_db] sync_latest: bulk download incomplete — %d draws stored to %s, "
                  "%d failed; resume cache kept for next run",
                  fetch_count, path, failures);
        return error_rc;
    }

    cleanup_checkpoint_cache(checkpoint_dir, dates, max_fetch);
    log_info("[historical_db] sync_latest: bulk download complete — %d draws stored to %s",
             fetch_count, path);
    return HISTORICAL_DB_SYNC_UPDATED;
}

/**
 * @brief Fetch, validate, and persist the latest snapshot for a game.
 */
int historical_db_sync_latest(const char *game_name, const char *db_root,
                              HistoricalDrawSnapshot *out_snapshot)
{
    char path[768];
    char dir[768];
    char *json = NULL;
    const char *configured_url;
    char selected_url[512] = {0};
    char source_urls[8][512];
    int source_url_count = 0;
    HistoricalDrawSnapshot snapshot;
    HistoricalDrawSnapshot existing;
    int has_existing = 0;
    char lock_path[1024] = {0};

    if (!game_name || !out_snapshot)
        return HISTORICAL_DB_ERR_INVALID_ARG;

    g_download_settings = load_download_settings();

    log_info("[historical_db] sync_latest: starting sync for game='%s'", game_name);
    log_info("[historical_db] sync config: workers=%d fetch_timeout=%ds draw_timeout=%ds "
             "retries=%d backoff=%d..%dms jitter=%dms pace=%dms max_fetch=%d "
             "force_full_rebuild=%d",
             g_download_settings.workers, g_download_settings.fetch_timeout_sec,
             g_download_settings.draw_timeout_sec, g_download_settings.max_retry_attempts,
             g_download_settings.retry_base_delay_ms, g_download_settings.retry_max_delay_ms,
             g_download_settings.retry_jitter_ms, g_download_settings.bulk_fetch_pace_ms,
             g_download_settings.max_fetch_draws, g_download_settings.force_full_rebuild);
#ifndef _OPENMP
    if (g_download_settings.workers > 1)
    {
        log_warn("[historical_db] OpenMP disabled in this build; falling back to single-thread "
                 "download execution");
        g_download_settings.workers = 1;
    }
#endif

    configured_url = default_game_source_url(game_name);
    if (!configured_url)
    {
        log_warn("[historical_db] sync_latest: no upstream URL configured for game='%s'",
                 game_name);
        return HISTORICAL_DB_ERR_UNSUPPORTED_GAME;
    }

    source_url_count = parse_source_url_chain(configured_url, source_urls, 8);
    if (source_url_count <= 0)
    {
        log_error("[historical_db] sync_latest: configured source URL list is empty for game='%s'",
                  game_name);
        return HISTORICAL_DB_ERR_UNSUPPORTED_GAME;
    }

    log_debug("[historical_db] sync_latest: upstream source chain has %d endpoint(s)",
              source_url_count);

    log_info("[historical_db] sync_latest: downloading latest draw data...");
    for (int i = 0; i < source_url_count; i++)
    {
        log_info("[historical_db] sync_latest: trying source endpoint %d/%d", i + 1,
                 source_url_count);
        json = fetch_url_text(source_urls[i]);
        if (json)
        {
            size_t url_len = strnlen(source_urls[i], sizeof(selected_url) - 1);
            memcpy(selected_url, source_urls[i], url_len);
            selected_url[url_len] = '\0';
            if (i > 0)
            {
                log_info("[historical_db] sync_latest: fallback endpoint selected (%d/%d)", i + 1,
                         source_url_count);
            }
            break;
        }

        if (i + 1 < source_url_count)
        {
            log_warn("[historical_db] sync_latest: source endpoint %d failed, trying fallback",
                     i + 1);
        }
    }

    if (!json)
    {
        log_error("[historical_db] sync_latest: network fetch failed for game='%s'", game_name);
        return HISTORICAL_DB_ERR_NETWORK;
    }
    log_info("[historical_db] sync_latest: download succeeded");

    /* Validate the upstream payload before touching any local state. */
    log_debug("[historical_db] sync_latest: parsing latest draw from upstream response");
    if (parse_game_json(game_name, json, &snapshot) != 0)
    {
        log_error("[historical_db] sync_latest: failed to parse upstream JSON for game='%s'",
                  game_name);
        free(json);
        return HISTORICAL_DB_ERR_PARSE;
    }
    strncpy(snapshot.source_url, selected_url, sizeof(snapshot.source_url) - 1);
    snapshot.source_url[sizeof(snapshot.source_url) - 1] = '\0';

    if (build_snapshot_path(game_name, db_root, path, sizeof(path)) != 0)
    {
        free(json);
        return HISTORICAL_DB_ERR_IO;
    }

    log_debug("[historical_db] sync_latest: local snapshot path = %s", path);

    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (!slash)
    {
        free(json);
        return HISTORICAL_DB_ERR_IO;
    }
    *slash = '\0';

    if (mkdir_p(dir) != 0)
    {
        log_error("[historical_db] sync_latest: failed to create directory '%s'", dir);
        free(json);
        return HISTORICAL_DB_ERR_IO;
    }

    if (acquire_sync_lock(path, lock_path, sizeof(lock_path)) != 0)
    {
        log_warn("[historical_db] sync_latest: concurrent sync detected, lock '%s' already in "
                 "use",
                 lock_path);
        free(json);
        return HISTORICAL_DB_ERR_IO;
    }

    int existing_draw_count = 0;
    if (historical_db_load_latest(game_name, db_root, &existing) >= 0)
    {
        char *existing_json = NULL;
        has_existing = 1;
        if (read_file_text(path, &existing_json) == 0)
        {
            existing_draw_count = count_snapshot_draws(existing_json);
            if (existing_draw_count < 0)
                existing_draw_count = 0;
        }
        free(existing_json);
        log_debug("[historical_db] sync_latest: existing local snapshot found (draw_date=%s, "
                  "draws=%d)",
                  existing.draw_date, existing_draw_count);
    }
    else
    {
        log_info("[historical_db] sync_latest: no existing local snapshot — will perform initial "
                 "bulk download");
    }

    char checkpoint_dir[1024];
    char marker_path[1024];
    if (build_checkpoint_dir_path(path, checkpoint_dir, sizeof(checkpoint_dir)) != 0 ||
        build_pending_marker_path(checkpoint_dir, marker_path, sizeof(marker_path)) != 0)
    {
        release_sync_lock(lock_path);
        free(json);
        return HISTORICAL_DB_ERR_IO;
    }
    int resume_pending = path_exists(marker_path);

    if (!has_existing || g_download_settings.force_full_rebuild || resume_pending)
    {
        char history_dates[HISTORICAL_DB_MAX_DATES][16];
        int history_failures = 0;

        if (has_existing && g_download_settings.force_full_rebuild)
            log_info("[historical_db] sync_latest: forcing full rebuild due to %s",
                     HISTORICAL_DB_FORCE_FULL_REBUILD_ENV);
        else if (has_existing && resume_pending)
            log_info("[historical_db] sync_latest: resuming incomplete bulk download from %s",
                     checkpoint_dir);

        int history_count = collect_game_dates_all_years(
            game_name, history_dates, HISTORICAL_DB_MAX_DATES, &history_failures);
        if (history_count <= 0)
        {
            history_count = parse_history_dates(json, history_dates, HISTORICAL_DB_MAX_DATES);
            history_failures = 0;
        }
        log_debug("[historical_db] sync_latest: %d historical draw dates available from upstream",
                  history_count);

        if (history_count > 0)
        {
            free(json);
            int rc = bulk_sync_history(game_name, path, checkpoint_dir, history_dates,
                                       history_count, history_failures, existing_draw_count);
            release_sync_lock(lock_path);
            if (rc == HISTORICAL_DB_SYNC_UPDATED)
            {
                *out_snapshot = snapshot;
                log_info("[historical_db] sync_latest: bulk sync done (latest draw_date=%s)",
                         snapshot.draw_date);
            }
            return rc;
        }

        if (has_existing && g_download_settings.force_full_rebuild)
        {
            log_error("[historical_db] sync_latest: full rebuild requested but no draw history "
                      "is available; keeping existing snapshot");
            free(json);
            release_sync_lock(lock_path);
            return HISTORICAL_DB_ERR_NETWORK;
        }
    }
    free(json);

    if (has_existing && strcmp(snapshot.draw_date, existing.draw_date) == 0)
    {
        log_info("[historical_db] sync_latest: local snapshot already up-to-date (draw_date=%s)",
                 snapshot.draw_date);
        *out_snapshot = snapshot;
        release_sync_lock(lock_path);
        return HISTORICAL_DB_SYNC_UNCHANGED;
    }

    log_info("[historical_db] sync_latest: new draw detected (draw_date=%s) — appending to %s",
             snapshot.draw_date, path);
    if (append_draw_to_history(path, &snapshot) != 0)
    {
        log_error("[historical_db] sync_latest: failed to write snapshot to '%s'", path);
        release_sync_lock(lock_path);
        return HISTORICAL_DB_ERR_IO;
    }

    *out_snapshot = snapshot;
    release_sync_lock(lock_path);
    log_info("[historical_db] sync_latest: sync complete (draw_date=%s)", snapshot.draw_date);
    return HISTORICAL_DB_SYNC_UPDATED;
}
