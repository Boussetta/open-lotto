/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "historical_db.h"

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

    int n = snprintf(out, out_size, "%s/%s_latest.json", root, slug);
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

    snprintf(cmd, sizeof(cmd), "curl -fsSL --max-time 20 \"%s\"", url);

    FILE *pipe = popen(cmd, "r");
    if (!pipe)
        return NULL;

    char *data = read_stream(pipe);
    int rc = pclose(pipe);
    if (rc != 0 || !data)
    {
        free(data);
        return NULL;
    }

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

static int write_snapshot_json(const char *path, const HistoricalDrawSnapshot *snap)
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
    fprintf(fp, "  \"synced_at\": \"%s\",\n", ts);
    fprintf(fp, "  \"draw_date\": \"%s\",\n", snap->draw_date);
    fprintf(fp, "  \"next_draw_date\": \"%s\",\n", snap->next_draw_date);
    fprintf(fp, "  \"source_url\": \"%s\",\n", snap->source_url);

    fprintf(fp, "  \"main_numbers\": [");
    for (int i = 0; i < snap->main_count; i++)
        fprintf(fp, "%s%d", i == 0 ? "" : ", ", snap->main_numbers[i]);
    fprintf(fp, "],\n");

    fprintf(fp, "  \"extra_numbers\": [");
    for (int i = 0; i < snap->extra_count; i++)
        fprintf(fp, "%s%d", i == 0 ? "" : ", ", snap->extra_numbers[i]);
    fprintf(fp, "],\n");

    fprintf(fp, "  \"winning_classes\": [\n");
    for (int i = 0; i < snap->winning_class_count; i++)
    {
        const HistoricalWinningClass *wc = &snap->winning_classes[i];
        fprintf(fp,
                "    {\"class\": \"%s\", \"description\": \"%s\", \"winners\": %d, "
                "\"payout\": %.2f}%s\n",
                wc->class_id, wc->description, wc->winners, wc->payout,
                i + 1 == snap->winning_class_count ? "" : ",");
    }
    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");

    fclose(fp);
    return 0;
}

static int parse_local_snapshot_json(const char *json, HistoricalDrawSnapshot *snap)
{
    const char *main_arr;
    const char *extra_arr;
    const char *classes;
    int idx = 0;

    if (!json || !snap)
        return -1;

    memset(snap, 0, sizeof(*snap));

    if (parse_string_field(json, "game", snap->game, sizeof(snap->game)) != 0)
        return -1;
    if (parse_string_field(json, "draw_date", snap->draw_date, sizeof(snap->draw_date)) != 0)
        return -1;
    if (parse_string_field(json, "next_draw_date", snap->next_draw_date,
                           sizeof(snap->next_draw_date)) != 0)
        return -1;
    if (parse_string_field(json, "source_url", snap->source_url, sizeof(snap->source_url)) != 0)
        return -1;

    main_arr = find_key(json, "main_numbers");
    extra_arr = find_key(json, "extra_numbers");
    if (!main_arr || !extra_arr)
        return -1;

    snap->main_count =
        parse_number_array(main_arr, snap->main_numbers, HISTORICAL_DB_MAX_MAIN_NUMBERS);
    snap->extra_count =
        parse_number_array(extra_arr, snap->extra_numbers, HISTORICAL_DB_MAX_EXTRA_NUMBERS);
    if (snap->main_count <= 0 || snap->extra_count <= 0)
        return -1;

    classes = find_key(json, "winning_classes");
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
    HistoricalDrawSnapshot latest;
    HistoricalDrawSnapshot existing;
    int has_existing = 0;

    if (!game_name || !out_snapshot)
        return HISTORICAL_DB_ERR_INVALID_ARG;

    url = default_game_source_url(game_name);
    if (!url)
        return HISTORICAL_DB_ERR_UNSUPPORTED_GAME;

    json = fetch_url_text(url);
    if (!json)
        return HISTORICAL_DB_ERR_NETWORK;

    if (parse_eurojackpot_json(json, &latest) != 0)
    {
        free(json);
        return HISTORICAL_DB_ERR_PARSE;
    }
    free(json);

    strncpy(latest.source_url, url, sizeof(latest.source_url) - 1);

    if (build_snapshot_path(game_name, db_root, path, sizeof(path)) != 0)
        return HISTORICAL_DB_ERR_IO;

    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (!slash)
        return HISTORICAL_DB_ERR_IO;
    *slash = '\0';

    if (mkdir_p(dir) != 0)
        return HISTORICAL_DB_ERR_IO;

    if (historical_db_load_latest(game_name, db_root, &existing) >= 0)
        has_existing = 1;

    if (write_snapshot_json(path, &latest) != 0)
        return HISTORICAL_DB_ERR_IO;

    *out_snapshot = latest;

    if (has_existing && strcmp(existing.draw_date, latest.draw_date) == 0)
        return HISTORICAL_DB_SYNC_UNCHANGED;

    return HISTORICAL_DB_SYNC_UPDATED;
}
