/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "historical_db.h"
#include "log.h"

#include <ctype.h>
#include <errno.h>
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
#include <strings.h>
#include <unistd.h>
#else
#include <direct.h>
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
#define HISTORICAL_DB_SOURCES_CONFIG_ENV "OPEN_LOTTO_SOURCES_CONFIG"
#define HISTORICAL_DB_URL_ENV_EUROJACKPOT "OPEN_LOTTO_GEWINNZAHLEN_URL_EUROJACKPOT"
#define HISTORICAL_DB_URL_ENV_LOTTO "OPEN_LOTTO_GEWINNZAHLEN_URL_LOTTO"
#define HISTORICAL_DB_WORKERS_ENV "OPEN_LOTTO_HIST_DOWNLOAD_WORKERS"
#define HISTORICAL_DB_FETCH_TIMEOUT_ENV "OPEN_LOTTO_HIST_FETCH_TIMEOUT_SEC"
#define HISTORICAL_DB_DRAW_TIMEOUT_ENV "OPEN_LOTTO_HIST_DRAW_TIMEOUT_SEC"
#define HISTORICAL_DB_RETRY_ATTEMPTS_ENV "OPEN_LOTTO_HIST_MAX_RETRY_ATTEMPTS"
#define HISTORICAL_DB_RETRY_BASE_DELAY_ENV "OPEN_LOTTO_HIST_RETRY_BASE_DELAY_MS"
#define HISTORICAL_DB_RETRY_MAX_DELAY_ENV "OPEN_LOTTO_HIST_RETRY_MAX_DELAY_MS"
#define HISTORICAL_DB_MAX_FETCH_DRAWS_ENV "OPEN_LOTTO_HIST_MAX_FETCH_DRAWS"
#define HISTORICAL_DB_DOWNLOAD_CONFIG_ENV "OPEN_LOTTO_DOWNLOAD_CONFIG"

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

typedef struct
{
    int workers;
    int fetch_timeout_sec;
    int draw_timeout_sec;
    int max_retry_attempts;
    int retry_base_delay_ms;
    int retry_max_delay_ms;
    int max_fetch_draws;
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
    0,
};

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
        fprintf(stderr, "\033[%dF", *printed_lines);

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
        else if (strcasecmp(key, "max_fetch_draws") == 0)
            parse_int_bounded(value, &s->max_fetch_draws, 0, HISTORICAL_DB_MAX_DATES);
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
    s.max_fetch_draws = 0;

    load_download_settings_from_config(&s);

    s.workers = read_env_int_bounded(HISTORICAL_DB_WORKERS_ENV, s.workers, 1, 32);
    s.fetch_timeout_sec =
        read_env_int_bounded(HISTORICAL_DB_FETCH_TIMEOUT_ENV, s.fetch_timeout_sec, 5, 300);
    s.draw_timeout_sec =
        read_env_int_bounded(HISTORICAL_DB_DRAW_TIMEOUT_ENV, s.draw_timeout_sec, 5, 300);
    s.max_retry_attempts = read_env_int_bounded(HISTORICAL_DB_RETRY_ATTEMPTS_ENV,
                                                s.max_retry_attempts, 1, 10);
    s.retry_base_delay_ms = read_env_int_bounded(HISTORICAL_DB_RETRY_BASE_DELAY_ENV,
                                                 s.retry_base_delay_ms, 100, 60000);
    s.retry_max_delay_ms = read_env_int_bounded(HISTORICAL_DB_RETRY_MAX_DELAY_ENV,
                                                s.retry_max_delay_ms, 100, 120000);
    s.max_fetch_draws = read_env_int_bounded(HISTORICAL_DB_MAX_FETCH_DRAWS_ENV,
                                             s.max_fetch_draws, 0, HISTORICAL_DB_MAX_DATES);

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
    return delay;
}

static char *fetch_url_text_with_retries(const char *url, int timeout_sec, const char *context)
{
    char cmd[1024];

    if (!url || strchr(url, '"') || strchr(url, '`'))
        return NULL;

    for (int attempt = 1; attempt <= g_download_settings.max_retry_attempts; attempt++)
    {
        snprintf(cmd, sizeof(cmd), "curl -fsSL --max-time %d \"%s\"", timeout_sec, url);

        FILE *pipe = popen(cmd, "r");
        if (!pipe)
        {
            log_error("[historical_db] %s: popen failed for URL: %s", context, url);
            return NULL;
        }

        char *data = read_stream(pipe);
        int rc = pclose(pipe);

        if (rc == 0 && data)
        {
            log_debug("[historical_db] %s: received response (%zu bytes)", context,
                      strlen(data));
            return data;
        }

        free(data);
        log_warn("[historical_db] %s: curl failed (exit=%d, attempt=%d/%d, timeout=%ds)",
                 context, rc, attempt, g_download_settings.max_retry_attempts, timeout_sec);

        if (attempt < g_download_settings.max_retry_attempts)
        {
            unsigned int delay = retry_delay_ms(attempt);
            log_debug("[historical_db] %s: retrying after %u ms", context, delay);
            sleep_ms(delay);
        }
    }

    log_error("[historical_db] %s: giving up after %d attempts for URL: %s", context,
              g_download_settings.max_retry_attempts, url);
    return NULL;
}

static int date_already_collected(const char dates[][16], int count, const char *date)
{
    if (!dates || !date)
        return 0;

    for (int i = 0; i < count; i++)
    {
        if (strcmp(dates[i], date) == 0)
            return 1;
    }

    return 0;
}

static int collect_game_dates_all_years(const char *game_name, char dates[][16], int max_dates)
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

    for (int year = current_year; year >= first_year; year--)
    {
        char url[512];
        snprintf(url, sizeof(url),
                 "https://www.eurojackpot.com/wlinfo/WL_InfoService?client=jsn&gruppe=ZahlenUndQuoten&"
                 "ewGewsum=ja&historie=ja&spielart=%s&adg=ja&lang=de&jahr=%d",
                 spielart, year);

        char *year_json = fetch_url_text(url);
        if (!year_json)
        {
            log_warn("[historical_db] sync_latest: yearly history fetch failed for game='%s' "
                     "year=%d",
                     game_name, year);
            continue;
        }

        char year_dates[200][16];
        int year_count = parse_history_dates(year_json, year_dates, 200);
        free(year_json);

        if (year_count <= 0)
            continue;

        for (int i = 0; i < year_count && total < max_dates; i++)
        {
            if (!date_already_collected((const char (*)[16])dates, total, year_dates[i]))
            {
                snprintf(dates[total], 16, "%.15s", year_dates[i]);
                total++;
            }
        }

        if (total >= max_dates)
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
                 checked_path, HISTORICAL_DB_SOURCES_CONFIG_ENV,
                 HISTORICAL_DB_URL_ENV_EUROJACKPOT);
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

const char *historical_db_default_root(void)
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

    int n = snprintf(out, out_size, "%s/%s.json", checkpoint_dir, date);
    if (n < 0 || (size_t)n >= out_size)
        return -1;

    return 0;
}

static int write_text_file(const char *path, const char *content)
{
    if (!path || !content)
        return -1;

    FILE *fp = fopen(path, "wb");
    if (!fp)
        return -1;

    size_t len = strlen(content);
    size_t wrote = fwrite(content, 1, len, fp);
    int rc = fclose(fp);
    if (wrote != len || rc != 0)
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

static int parse_json_string_value(const char *p, char *out, size_t out_size)
{
    size_t j = 0;
    if (!p || !out || out_size == 0)
        return -1;

    p = skip_ws(p);
    if (*p == 'n')
    {
        out[0] = '\0';
        return 0;
    }

    if (*p != '"')
        return -1;
    p++;

    while (*p && *p != '"')
    {
        if (*p == '\\' && p[1] != '\0')
            p++;

        if (j + 1 < out_size)
            out[j++] = *p;
        p++;
    }

    if (*p != '"')
        return -1;

    out[j] = '\0';
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
            out[count++] = atoi(tmp);
            q = strchr(q + 1, '"');
            if (!q)
                return -1;
            q++;
            continue;
        }

        if (*q == '-' || isdigit((unsigned char)*q))
        {
            char *end = NULL;
            long v = strtol(q, &end, 10);
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

    long v = strtol(colon + 1, &end, 10);
    if (end == colon + 1)
        return -1;

    *out = (int)v;
    return 0;
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

    for (const char *p = arr; *p && *p != ']' && idx < HISTORICAL_DB_MAX_WINNING_CLASSES; p++)
    {
        if (*p == '{')
        {
            int depth = 1;
            const char *start = p;
            const char *end = p + 1;
            while (*end && depth > 0)
            {
                if (*end == '{')
                    depth++;
                else if (*end == '}')
                    depth--;
                end++;
            }

            if (depth != 0)
                return -1;

            size_t span = (size_t)(end - start);
            if (span > 0)
            {
                char obj[1024];
                if (span >= sizeof(obj))
                    span = sizeof(obj) - 1;
                memcpy(obj, start, span);
                obj[span] = '\0';

                HistoricalWinningClass *wc = &snap->winning_classes[idx];
                int winners = 0;
                double payout = 0.0;

                if (parse_string_field(obj, "klasse", wc->class_id, sizeof(wc->class_id)) != 0)
                    snprintf(wc->class_id, sizeof(wc->class_id), "%d", idx + 1);

                if (parse_string_field(obj, "beschreibung", wc->description,
                                       sizeof(wc->description)) != 0 ||
                    wc->description[0] == '\0')
                {
                    if (parse_string_field(obj, "kurzbeschreibung", wc->description,
                                           sizeof(wc->description)) != 0)
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
            }

            p = end - 1;
        }
    }

    snap->winning_class_count = idx;
    return idx > 0 ? 0 : -1;
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

    const char *head = find_key(json, "head");
    const char *follow;

    if (!head)
        return -1;

    if (parse_string_field(head, "datum", snap->draw_date, sizeof(snap->draw_date)) != 0)
        return -1;

    follow = find_key(head, "folgeZiehung");
    if (!follow || parse_string_field(follow, "datum", snap->next_draw_date,
                                      sizeof(snap->next_draw_date)) != 0)
    {
        return -1;
    }

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

    const char *head = find_key(json, "head");
    const char *follow;

    if (!head)
        return -1;

    if (parse_string_field(head, "datum", snap->draw_date, sizeof(snap->draw_date)) != 0)
        return -1;

    follow = find_key(head, "folgeZiehung");
    if (!follow || parse_string_field(follow, "datum", snap->next_draw_date,
                                      sizeof(snap->next_draw_date)) != 0)
    {
        return -1;
    }

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
    if (snap->main_count != expected_main_count)
        return -1;

    if (parse_string_field(numbers_root, "superzahl", superzahl, sizeof(superzahl)) != 0 ||
        superzahl[0] == '\0')
    {
        return -1;
    }

    snap->extra_numbers[0] = atoi(superzahl);
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

    for (const char *p = arr + 1; *p && *p != ']' && count < max_dates; p++)
    {
        if (*p == '"')
        {
            char date_str[16];
            if (parse_json_string_value(p, date_str, sizeof(date_str)) == 0 && date_str[0] != '\0')
            {
                snprintf(dates[count], 16, "%.15s", date_str);
                count++;
                p = strchr(p + 1, '"');
                if (!p)
                    break;
            }
        }
    }

    return count;
}

static char *fetch_draw_for_date(const char *game_name, const char *date)
{
    if (!game_name || !date)
        return NULL;

    const char *spielart = game_spielart_code(game_name);
    if (!spielart)
        return NULL;

    /* Bound the date to 15 chars so the compiler can verify format-truncation safety. */
    char safe_date[16];
    snprintf(safe_date, sizeof(safe_date), "%.15s", date);

    char url[512];
    snprintf(url, sizeof(url),
             "https://www.eurojackpot.com/wlinfo/WL_InfoService?client=jsn&gruppe=ZahlenUndQuoten&"
             "ewGewsum=ja&spielart=%s&datum=%.15s&adg=ja&lang=de",
             spielart, safe_date);

    if (strchr(url, '"') || strchr(url, '`'))
        return NULL;

    log_debug("[historical_db] fetch_draw_for_date: fetching draw for game=%s date=%s",
              game_name, date);

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

static int write_draw_to_history(const char *path, const HistoricalDrawSnapshot *snap)
{
    FILE *fp;
    char ts[32];
    time_t now = time(NULL);
    struct tm tmv;

    if (!path || !snap)
        return -1;

#ifdef _WIN32
    gmtime_s(&tmv, &now);
#else
    gmtime_r(&now, &tmv);
#endif
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tmv);

    fp = fopen(path, "w");
    if (!fp)
        return -1;

    fprintf(fp, "{\n");
    fprintf(fp, "  \"game\": \"%s\",\n", snap->game);
    fprintf(fp, "  \"last_sync_at\": \"%s\",\n", ts);
    fprintf(fp, "  \"draws\": [\n");

    fprintf(fp, "    {\n");
    fprintf(fp, "      \"draw_date\": \"%s\",\n", snap->draw_date);
    fprintf(fp, "      \"next_draw_date\": \"%s\",\n", snap->next_draw_date);
    fprintf(fp, "      \"source_url\": \"%s\",\n", snap->source_url);

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
        fprintf(fp,
                "        {\"class\": \"%s\", \"description\": \"%s\", \"winners\": %d, "
                "\"payout\": %.2f}%s\n",
                wc->class_id, wc->description, wc->winners, wc->payout,
                i + 1 == snap->winning_class_count ? "" : ",");
    }
    fprintf(fp, "      ]\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");

    fclose(fp);
    return 0;
}

static int append_draw_to_history(const char *path, const HistoricalDrawSnapshot *snap)
{
    char *json = NULL;
    FILE *fp;

    if (!path || !snap)
        return -1;

    if (read_file_text(path, &json) != 0)
    {
        return write_draw_to_history(path, snap);
    }

    if (!json)
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
    const char *arr_end = strrchr(json, ']');

    if (!arr_start || !arr_end || arr_end <= arr_start)
    {
        free(json);
        return write_draw_to_history(path, snap);
    }

    fp = fopen(path, "w");
    if (!fp)
    {
        free(json);
        return -1;
    }

    fwrite(json, 1, (size_t)(arr_end - json - 1), fp);

    fprintf(fp, ",\n    {\n");
    fprintf(fp, "      \"draw_date\": \"%s\",\n", snap->draw_date);
    fprintf(fp, "      \"next_draw_date\": \"%s\",\n", snap->next_draw_date);
    fprintf(fp, "      \"source_url\": \"%s\",\n", snap->source_url);

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
        fprintf(fp,
                "        {\"class\": \"%s\", \"description\": \"%s\", \"winners\": %d, "
                "\"payout\": %.2f}%s\n",
                wc->class_id, wc->description, wc->winners, wc->payout,
                i + 1 == snap->winning_class_count ? "" : ",");
    }
    fprintf(fp, "      ]\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");

    fclose(fp);
    free(json);
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

    const char *last_obj_start = NULL;
    int depth = 0;

    for (const char *p = arr_start + 1; *p; p++)
    {
        if (*p == '{')
        {
            if (depth == 0)
                last_obj_start = p;
            depth++;
        }
        else if (*p == '}')
        {
            depth--;
            if (depth == 0 && last_obj_start)
            {
                const char *obj_end = p + 1;
                size_t obj_len = (size_t)(obj_end - last_obj_start);

                if (obj_len >= obj_size)
                    obj_len = obj_size - 1;

                memcpy(draw_obj, last_obj_start, obj_len);
                draw_obj[obj_len] = '\0';
                return 0;
            }
        }
    }

    return -1;
}

static int parse_local_snapshot_json(const char *json, HistoricalDrawSnapshot *snap)
{
    char draw_obj[2048];
    int idx = 0;

    if (!json || !snap)
        return -1;

    memset(snap, 0, sizeof(*snap));

    if (parse_string_field(json, "game", snap->game, sizeof(snap->game)) != 0)
        return -1;

    if (extract_last_draw_object(json, draw_obj, sizeof(draw_obj)) != 0)
        return -1;

    if (parse_string_field(draw_obj, "draw_date", snap->draw_date, sizeof(snap->draw_date)) !=
        0)
        return -1;
    if (parse_string_field(draw_obj, "next_draw_date", snap->next_draw_date,
                           sizeof(snap->next_draw_date)) != 0)
        return -1;
    if (parse_string_field(draw_obj, "source_url", snap->source_url, sizeof(snap->source_url)) !=
        0)
        return -1;

    const char *main_arr = find_key(draw_obj, "main_numbers");
    const char *extra_arr = find_key(draw_obj, "extra_numbers");
    if (!main_arr || !extra_arr)
        return -1;

    snap->main_count = parse_number_array(main_arr, snap->main_numbers,
                                           HISTORICAL_DB_MAX_MAIN_NUMBERS);
    snap->extra_count = parse_number_array(extra_arr, snap->extra_numbers,
                                            HISTORICAL_DB_MAX_EXTRA_NUMBERS);
    if (snap->main_count <= 0 || snap->extra_count <= 0)
        return -1;

    const char *classes = find_key(draw_obj, "winning_classes");
    if (!classes)
        return -1;

    for (const char *p = strchr(classes, '['); p && *p && *p != ']'; p++)
    {
        if (*p == '{' && idx < HISTORICAL_DB_MAX_WINNING_CLASSES)
        {
            int depth = 1;
            const char *start = p;
            const char *end = p + 1;
            while (*end && depth > 0)
            {
                if (*end == '{')
                    depth++;
                else if (*end == '}')
                    depth--;
                end++;
            }

            if (depth != 0)
                return -1;

            size_t span = (size_t)(end - start);
            char obj[512];
            if (span >= sizeof(obj))
                span = sizeof(obj) - 1;
            memcpy(obj, start, span);
            obj[span] = '\0';

            HistoricalWinningClass *wc = &snap->winning_classes[idx];
            if (parse_string_field(obj, "class", wc->class_id, sizeof(wc->class_id)) != 0)
                return -1;
            if (parse_string_field(obj, "description", wc->description, sizeof(wc->description)) !=
                0)
                return -1;
            if (parse_int_field(obj, "winners", &wc->winners) != 0)
                return -1;
            if (parse_double_field(obj, "payout", &wc->payout) != 0)
                return -1;

            idx++;
            p = end - 1;
        }
    }

    snap->winning_class_count = idx;
    return 0;
}

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

int historical_db_sync_latest(const char *game_name, const char *db_root,
                              HistoricalDrawSnapshot *out_snapshot)
{
    char path[768];
    char dir[768];
    char *json = NULL;
    const char *url;
    HistoricalDrawSnapshot snapshot;
    HistoricalDrawSnapshot existing;
    int has_existing = 0;

    if (!game_name || !out_snapshot)
        return HISTORICAL_DB_ERR_INVALID_ARG;

    g_download_settings = load_download_settings();

    log_info("[historical_db] sync_latest: starting sync for game='%s'", game_name);
    log_info("[historical_db] sync config: workers=%d fetch_timeout=%ds draw_timeout=%ds "
             "retries=%d backoff=%d..%dms max_fetch=%d",
             g_download_settings.workers, g_download_settings.fetch_timeout_sec,
             g_download_settings.draw_timeout_sec, g_download_settings.max_retry_attempts,
             g_download_settings.retry_base_delay_ms, g_download_settings.retry_max_delay_ms,
             g_download_settings.max_fetch_draws);
#ifndef _OPENMP
    if (g_download_settings.workers > 1)
    {
        log_warn("[historical_db] OpenMP disabled in this build; falling back to single-thread "
                 "download execution");
        g_download_settings.workers = 1;
    }
#endif

    url = default_game_source_url(game_name);
    if (!url)
    {
        log_warn("[historical_db] sync_latest: no upstream URL configured for game='%s'",
                 game_name);
        return HISTORICAL_DB_ERR_UNSUPPORTED_GAME;
    }

    log_debug("[historical_db] sync_latest: upstream URL resolved to %s", url);

    log_info("[historical_db] sync_latest: downloading latest draw data...");
    json = fetch_url_text(url);
    if (!json)
    {
        log_error("[historical_db] sync_latest: network fetch failed for game='%s'", game_name);
        return HISTORICAL_DB_ERR_NETWORK;
    }
    log_info("[historical_db] sync_latest: download succeeded");

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

    if (historical_db_load_latest(game_name, db_root, &existing) >= 0)
    {
        has_existing = 1;
        log_debug("[historical_db] sync_latest: existing local snapshot found (draw_date=%s)",
                  existing.draw_date);
    }
    else
    {
        log_info("[historical_db] sync_latest: no existing local snapshot — will perform initial "
                 "bulk download");
    }

    char history_dates[HISTORICAL_DB_MAX_DATES][16];
    int history_count = 0;
    if (strcasecmp(game_name, "Eurojackpot") == 0 || strcasecmp(game_name, "Lotto 6aus49") == 0 ||
        strcasecmp(game_name, "Lotto") == 0)
    {
        history_count =
            collect_game_dates_all_years(game_name, history_dates, HISTORICAL_DB_MAX_DATES);
    }
    if (history_count <= 0)
    {
        history_count = parse_history_dates(json, history_dates, HISTORICAL_DB_MAX_DATES);
    }
    log_debug("[historical_db] sync_latest: %d historical draw dates available from upstream",
              history_count);

    if (!has_existing && history_count > 0)
    {
        int fetch_count = 0;
        int max_fetch = history_count;
        if (g_download_settings.max_fetch_draws > 0 &&
            g_download_settings.max_fetch_draws < max_fetch)
        {
            max_fetch = g_download_settings.max_fetch_draws;
            log_info("[historical_db] sync_latest: limiting bulk fetch to %d draws due to %s",
                     max_fetch, HISTORICAL_DB_MAX_FETCH_DRAWS_ENV);
        }
        int first = 1;
        time_t download_start = time(NULL);
        size_t total_bytes = 0;
        int printed_lines = 0;
        char checkpoint_dir[1024];
        if (build_checkpoint_dir_path(path, checkpoint_dir, sizeof(checkpoint_dir)) != 0)
        {
            free(json);
            return HISTORICAL_DB_ERR_IO;
        }
        if (mkdir_p(checkpoint_dir) != 0)
        {
            free(json);
            return HISTORICAL_DB_ERR_IO;
        }

        BulkDrawResult *results = calloc((size_t)max_fetch, sizeof(BulkDrawResult));
        int *worker_done = calloc((size_t)g_download_settings.workers, sizeof(int));
        size_t *worker_bytes = calloc((size_t)g_download_settings.workers, sizeof(size_t));
        if (!results)
        {
            free(json);
            return HISTORICAL_DB_ERR_IO;
        }
        if (!worker_done || !worker_bytes)
        {
            free(worker_done);
            free(worker_bytes);
            free(results);
            free(json);
            return HISTORICAL_DB_ERR_IO;
        }

        log_info(
            "[historical_db] sync_latest: bulk download — fetching up to %d historical draws "
            "with %d worker(s)",
            max_fetch, g_download_settings.workers);
        log_info("[historical_db] sync_latest: resume cache directory = %s", checkpoint_dir);
        fprintf(stderr, "  Fetching %d historical draws for %s\n", max_fetch, game_name);
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
            log_debug("[historical_db] sync_latest: fetching draw %d/%d (date=%s)", i + 1,
                      max_fetch, history_dates[i]);

            char cache_path[1024];
            char *draw_json = NULL;
            if (build_checkpoint_entry_path(checkpoint_dir, history_dates[i], cache_path,
                                            sizeof(cache_path)) == 0)
            {
                if (load_cached_draw_json(cache_path, &draw_json) == 0 && draw_json)
                {
                    results[i].bytes = strlen(draw_json);
                    results[i].fetched_ok = 1;
                    results[i].parsed_ok =
                        (parse_game_json(game_name, draw_json, &results[i].draw) == 0) ? 1 : 0;
                    if (!results[i].parsed_ok)
                    {
                        free(draw_json);
                        draw_json = NULL;
                        remove(cache_path);
                    }
                }
            }

            if (!draw_json)
                draw_json = fetch_draw_for_date(game_name, history_dates[i]);

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

                if (parse_game_json(game_name, draw_json, &results[i].draw) == 0)
                {
                    results[i].parsed_ok = 1;

                    if (build_checkpoint_entry_path(checkpoint_dir, history_dates[i], cache_path,
                                                    sizeof(cache_path)) == 0)
                    {
                        if (write_text_file(cache_path, draw_json) != 0)
                        {
                            log_warn("[historical_db] sync_latest: failed to persist resume cache "
                                     "for date=%s",
                                     history_dates[i]);
                        }
                    }
                }
                else
                {
                    results[i].parsed_ok = 0;
                }

                free(draw_json);
            }

#ifdef _OPENMP
#pragma omp critical(historical_db_progress)
#endif
            {
                total_bytes += results[i].bytes;
                completed++;
                if (worker_idx >= 0 && worker_idx < g_download_settings.workers)
                {
                    worker_done[worker_idx]++;
                    worker_bytes[worker_idx] += results[i].bytes;
                }
                print_worker_progress_bars(completed, max_fetch, download_start, total_bytes,
                                           g_download_settings.workers, worker_done,
                                           worker_bytes, &printed_lines);
            }
        }

        FILE *fp = fopen(path, "w");
        if (!fp)
        {
            free(worker_done);
            free(worker_bytes);
            free(results);
            free(json);
            return HISTORICAL_DB_ERR_IO;
        }

        fprintf(fp, "{\n");
        fprintf(fp, "  \"game\": \"%s\",\n", game_name);
        fprintf(fp, "  \"last_sync_at\": \"2026-06-08T20:00:00Z\",\n");
        fprintf(fp, "  \"draws\": [\n");

        for (int i = 0; i < max_fetch; i++)
        {
            if (!results[i].fetched_ok)
            {
                log_warn("[historical_db] sync_latest: skipping draw %d/%d (fetch failed)",
                         i + 1, max_fetch);
                continue;
            }
            if (!results[i].parsed_ok)
            {
                log_warn("[historical_db] sync_latest: failed to parse draw JSON for date=%s",
                         history_dates[i]);
                continue;
            }

            HistoricalDrawSnapshot *draw = &results[i].draw;
            if (!first)
                fprintf(fp, ",\n");
            first = 0;

            fprintf(fp, "    {\n");
            fprintf(fp, "      \"draw_date\": \"%s\",\n", draw->draw_date);
            fprintf(fp, "      \"next_draw_date\": \"%s\",\n", draw->next_draw_date);
            const char *spielart = game_spielart_code(game_name);
            fprintf(fp,
                    "      \"source_url\": "
                    "\"https://www.eurojackpot.com/wlinfo/"
                    "WL_InfoService?client=jsn&gruppe=ZahlenUndQuoten&ewGewsum=ja&"
                    "spielart=%s&datum=%s&adg=ja&lang=de\",\n",
                    spielart ? spielart : "", draw->draw_date);

            fprintf(fp, "      \"main_numbers\": [");
            for (int j = 0; j < draw->main_count; j++)
                fprintf(fp, "%s%d", j == 0 ? "" : ", ", draw->main_numbers[j]);
            fprintf(fp, "],\n");

            fprintf(fp, "      \"extra_numbers\": [");
            for (int j = 0; j < draw->extra_count; j++)
                fprintf(fp, "%s%d", j == 0 ? "" : ", ", draw->extra_numbers[j]);
            fprintf(fp, "],\n");

            fprintf(fp, "      \"winning_classes\": [\n");
            for (int j = 0; j < draw->winning_class_count; j++)
            {
                const HistoricalWinningClass *wc = &draw->winning_classes[j];
                fprintf(fp,
                        "        {\"class\": \"%s\", \"description\": \"%s\", "
                        "\"winners\": %d, \"payout\": %.2f}%s\n",
                        wc->class_id, wc->description, wc->winners, wc->payout,
                        j + 1 == draw->winning_class_count ? "" : ",");
            }
            fprintf(fp, "      ]\n");
            fprintf(fp, "    }");

            fetch_count++;
            log_debug("[historical_db] sync_latest: stored draw date=%s (%d so far)",
                      draw->draw_date, fetch_count);
        }

        free(worker_done);
        free(worker_bytes);
        free(results);

        cleanup_checkpoint_cache(checkpoint_dir, history_dates, max_fetch);

        fprintf(fp, "\n  ]\n");
        fprintf(fp, "}\n");
        fclose(fp);

        log_info(
            "[historical_db] sync_latest: bulk download complete — %d draws stored to %s",
            fetch_count, path);

        if (fetch_count > 0)
        {
            if (parse_game_json(game_name, json, &snapshot) == 0)
            {
                strncpy(snapshot.source_url, url, sizeof(snapshot.source_url) - 1);
                *out_snapshot = snapshot;
                free(json);
                log_info("[historical_db] sync_latest: initial sync done (latest draw_date=%s)",
                         snapshot.draw_date);
                return HISTORICAL_DB_SYNC_UPDATED;
            }
        }

        free(json);
        return HISTORICAL_DB_ERR_PARSE;
    }

    log_debug("[historical_db] sync_latest: parsing latest draw from upstream response");
    if (parse_game_json(game_name, json, &snapshot) != 0)
    {
        log_error("[historical_db] sync_latest: failed to parse upstream JSON for game='%s'",
                  game_name);
        free(json);
        return HISTORICAL_DB_ERR_PARSE;
    }
    free(json);

    strncpy(snapshot.source_url, url, sizeof(snapshot.source_url) - 1);

    if (has_existing && strcmp(snapshot.draw_date, existing.draw_date) == 0)
    {
        log_info(
            "[historical_db] sync_latest: local snapshot already up-to-date (draw_date=%s)",
            snapshot.draw_date);
        *out_snapshot = snapshot;
        return HISTORICAL_DB_SYNC_UNCHANGED;
    }

    log_info("[historical_db] sync_latest: new draw detected (draw_date=%s) — appending to %s",
             snapshot.draw_date, path);
    if (append_draw_to_history(path, &snapshot) != 0)
    {
        log_error("[historical_db] sync_latest: failed to write snapshot to '%s'", path);
        return HISTORICAL_DB_ERR_IO;
    }

    *out_snapshot = snapshot;
    log_info("[historical_db] sync_latest: sync complete (draw_date=%s)", snapshot.draw_date);
    return HISTORICAL_DB_SYNC_UPDATED;
}
