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

#ifndef _WIN32
#include <strings.h>
#include <unistd.h>
#else
#include <direct.h>
#define strcasecmp _stricmp
#endif

#define READ_CHUNK 4096
#define HISTORICAL_DB_MAX_DATES 4096
#define EUROJACKPOT_FIRST_DRAW_YEAR 2012

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

static int collect_eurojackpot_dates_all_years(char dates[][16], int max_dates)
{
    time_t now = time(NULL);
    struct tm tmv;
#ifdef _WIN32
    gmtime_s(&tmv, &now);
#else
    gmtime_r(&now, &tmv);
#endif

    int current_year = tmv.tm_year + 1900;
    int total = 0;

    for (int year = current_year; year >= EUROJACKPOT_FIRST_DRAW_YEAR; year--)
    {
        char url[512];
        snprintf(url, sizeof(url),
                 "https://www.eurojackpot.com/wlinfo/WL_InfoService?client=jsn&gruppe=ZahlenUndQuoten&"
                 "ewGewsum=ja&historie=ja&spielart=EJ&adg=ja&lang=de&jahr=%d",
                 year);

        char *year_json = fetch_url_text(url);
        if (!year_json)
        {
            log_warn("[historical_db] sync_latest: yearly history fetch failed for year=%d",
                     year);
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

static const char *EUROJACKPOT_DEFAULT_URL =
    "https://www.eurojackpot.com/wlinfo/WL_InfoService?client=jsn&gruppe=ZahlenUndQuoten&"
    "ewGewsum=ja&historie=ja&spielart=EJ&adg=ja&lang=de";

static const char *default_game_source_url(const char *game_name)
{
    const char *override = NULL;

    if (!game_name)
        return NULL;

    if (strcasecmp(game_name, "Eurojackpot") == 0)
    {
        override = getenv("OPEN_LOTTO_GEWINNZAHLEN_URL_EUROJACKPOT");
        return (override && override[0] != '\0') ? override : EUROJACKPOT_DEFAULT_URL;
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
    char cmd[1024];

    if (!url || strchr(url, '"') || strchr(url, '`'))
        return NULL;

    log_debug("[historical_db] fetch_url_text: GET %s", url);

    snprintf(cmd, sizeof(cmd), "curl -fsSL --max-time 20 \"%s\"", url);

    FILE *pipe = popen(cmd, "r");
    if (!pipe)
    {
        log_error("[historical_db] fetch_url_text: popen failed for URL: %s", url);
        return NULL;
    }

    char *data = read_stream(pipe);
    int rc = pclose(pipe);
    if (rc != 0 || !data)
    {
        log_error("[historical_db] fetch_url_text: curl exited with code %d for URL: %s", rc,
                  url);
        free(data);
        return NULL;
    }

    log_debug("[historical_db] fetch_url_text: received response (%zu bytes)",
              data ? strlen(data) : 0);
    return data;
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
    char cmd[1024];

    if (!game_name || !date)
        return NULL;

    /* Bound the date to 15 chars so the compiler can verify format-truncation safety. */
    char safe_date[16];
    snprintf(safe_date, sizeof(safe_date), "%.15s", date);

    char url[512];
    snprintf(url, sizeof(url),
             "https://www.eurojackpot.com/wlinfo/WL_InfoService?client=jsn&gruppe=ZahlenUndQuoten&"
             "ewGewsum=ja&spielart=EJ&datum=%.15s&adg=ja&lang=de",
             safe_date);

    if (strchr(url, '"') || strchr(url, '`'))
        return NULL;

    log_debug("[historical_db] fetch_draw_for_date: fetching draw for game=%s date=%s",
              game_name, date);

    snprintf(cmd, sizeof(cmd), "curl -fsSL --max-time 10 \"%s\"", url);

    FILE *pipe = popen(cmd, "r");
    if (!pipe)
    {
        log_error("[historical_db] fetch_draw_for_date: popen failed (game=%s date=%s)",
                  game_name, date);
        return NULL;
    }

    char *data = read_stream(pipe);
    int rc = pclose(pipe);
    if (rc != 0 || !data)
    {
        log_warn("[historical_db] fetch_draw_for_date: curl failed (exit=%d, game=%s date=%s)",
                 rc, game_name, date);
        free(data);
        return NULL;
    }

    log_debug("[historical_db] fetch_draw_for_date: received response for %s (%zu bytes)", date,
              data ? strlen(data) : 0);
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
    return idx > 0 ? 0 : -1;
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

    log_info("[historical_db] sync_latest: starting sync for game='%s'", game_name);

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
    if (strcasecmp(game_name, "Eurojackpot") == 0)
    {
        history_count = collect_eurojackpot_dates_all_years(history_dates, HISTORICAL_DB_MAX_DATES);
    }
    if (history_count <= 0)
    {
        history_count = parse_history_dates(json, history_dates, HISTORICAL_DB_MAX_DATES);
    }
    log_debug("[historical_db] sync_latest: %d historical draw dates available from upstream",
              history_count);

    if (!has_existing && history_count > 0)
    {
        FILE *fp = fopen(path, "w");
        if (!fp)
        {
            free(json);
            return HISTORICAL_DB_ERR_IO;
        }

        fprintf(fp, "{\n");
        fprintf(fp, "  \"game\": \"%s\",\n", game_name);
        fprintf(fp, "  \"last_sync_at\": \"2026-06-08T20:00:00Z\",\n");
        fprintf(fp, "  \"draws\": [\n");

        int fetch_count = 0;
        int max_fetch = history_count;
        int first = 1;
        time_t download_start = time(NULL);
        size_t total_bytes = 0;

        log_info(
            "[historical_db] sync_latest: bulk download — fetching up to %d historical draws",
            max_fetch);
        fprintf(stderr, "  Fetching %d historical draws for %s\n", max_fetch, game_name);
        print_progress_bar(0, max_fetch, download_start, 0);

        for (int i = 0; i < max_fetch; i++)
        {
            log_debug("[historical_db] sync_latest: fetching draw %d/%d (date=%s)", i + 1,
                      max_fetch, history_dates[i]);
            char *draw_json = fetch_draw_for_date(game_name, history_dates[i]);
            if (!draw_json)
            {
                log_warn("[historical_db] sync_latest: skipping draw %d/%d (fetch failed)",
                         i + 1, max_fetch);
                print_progress_bar(i + 1, max_fetch, download_start, total_bytes);
                continue;
            }

            total_bytes += strlen(draw_json);

            HistoricalDrawSnapshot draw;
            if (parse_eurojackpot_json(draw_json, &draw) == 0)
            {
                if (!first)
                    fprintf(fp, ",\n");
                first = 0;

                fprintf(fp, "    {\n");
                fprintf(fp, "      \"draw_date\": \"%s\",\n", draw.draw_date);
                fprintf(fp, "      \"next_draw_date\": \"%s\",\n", draw.next_draw_date);
                fprintf(fp,
                        "      \"source_url\": "
                        "\"https://www.eurojackpot.com/wlinfo/"
                        "WL_InfoService?client=jsn&gruppe=ZahlenUndQuoten&ewGewsum=ja&"
                        "spielart=EJ&datum=%s&adg=ja&lang=de\",\n",
                        draw.draw_date);

                fprintf(fp, "      \"main_numbers\": [");
                for (int j = 0; j < draw.main_count; j++)
                    fprintf(fp, "%s%d", j == 0 ? "" : ", ", draw.main_numbers[j]);
                fprintf(fp, "],\n");

                fprintf(fp, "      \"extra_numbers\": [");
                for (int j = 0; j < draw.extra_count; j++)
                    fprintf(fp, "%s%d", j == 0 ? "" : ", ", draw.extra_numbers[j]);
                fprintf(fp, "],\n");

                fprintf(fp, "      \"winning_classes\": [\n");
                for (int j = 0; j < draw.winning_class_count; j++)
                {
                    const HistoricalWinningClass *wc = &draw.winning_classes[j];
                    fprintf(fp,
                            "        {\"class\": \"%s\", \"description\": \"%s\", "
                            "\"winners\": %d, \"payout\": %.2f}%s\n",
                            wc->class_id, wc->description, wc->winners, wc->payout,
                            j + 1 == draw.winning_class_count ? "" : ",");
                }
                fprintf(fp, "      ]\n");
                fprintf(fp, "    }");

                fetch_count++;
                log_debug(
                    "[historical_db] sync_latest: stored draw date=%s (%d so far)",
                    draw.draw_date, fetch_count);
            }
            else
            {
                log_warn(
                    "[historical_db] sync_latest: failed to parse draw JSON for date=%s",
                    history_dates[i]);
            }

            print_progress_bar(i + 1, max_fetch, download_start, total_bytes);
            free(draw_json);
        }

        fprintf(fp, "\n  ]\n");
        fprintf(fp, "}\n");
        fclose(fp);

        log_info(
            "[historical_db] sync_latest: bulk download complete — %d draws stored to %s",
            fetch_count, path);

        if (fetch_count > 0)
        {
            if (parse_eurojackpot_json(json, &snapshot) == 0)
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
    if (parse_eurojackpot_json(json, &snapshot) != 0)
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
