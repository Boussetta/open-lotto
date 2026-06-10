/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "analytics.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int slugify_game_name(const char *game_name, char *out, size_t out_size)
{
    if (!game_name || !out || out_size == 0)
        return -1;

    size_t j = 0;
    for (size_t i = 0; game_name[i] != '\0'; i++)
    {
        unsigned char c = (unsigned char)game_name[i];
        if (isalnum(c))
        {
            if (j + 1 >= out_size)
                return -1;
            out[j++] = (char)tolower(c);
        }
        else if (c == ' ' || c == '-' || c == '_')
        {
            if (j > 0 && out[j - 1] != '_')
            {
                if (j + 1 >= out_size)
                    return -1;
                out[j++] = '_';
            }
        }
    }

    if (j == 0)
        return -1;

    out[j] = '\0';
    return 0;
}

static int build_snapshot_path(const char *game_name, const char *db_root, char *out,
                               size_t out_size)
{
    if (!game_name || !out || out_size == 0)
        return -1;

    char slug[64];
    if (slugify_game_name(game_name, slug, sizeof(slug)) != 0)
        return -1;

    const char *root = db_root;
    char default_root[512];
    if (!root || root[0] == '\0')
    {
        const char *home = getenv("HOME");
        if (!home || home[0] == '\0')
            return -1;
        int n = snprintf(default_root, sizeof(default_root), "%s/.local/share/open-lotto/history",
                         home);
        if (n < 0 || (size_t)n >= sizeof(default_root))
            return -1;
        root = default_root;
    }

    int n = snprintf(out, out_size, "%s/%s_gewinnzahlen.json", root, slug);
    return (n < 0 || (size_t)n >= out_size) ? -1 : 0;
}

static int read_file_text(const char *path, char **out)
{
    if (!path || !out)
        return -1;

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return -1;

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return -1;
    }

    long sz = ftell(fp);
    if (sz < 0)
    {
        fclose(fp);
        return -1;
    }
    rewind(fp);

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf)
    {
        fclose(fp);
        return -1;
    }

    size_t rd = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    if (rd != (size_t)sz)
    {
        free(buf);
        return -1;
    }

    buf[rd] = '\0';
    *out = buf;
    return 0;
}

static int parse_json_int_array(const char *arr_start, int *out, int max_out, int *out_count)
{
    if (!arr_start || !out || !out_count || max_out <= 0)
        return -1;

    const char *p = strchr(arr_start, '[');
    if (!p)
        return -1;
    p++;

    int count = 0;
    while (*p)
    {
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
            p++;

        if (*p == ']')
        {
            *out_count = count;
            return 0;
        }

        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end == p)
            return -1;

        if (count >= max_out)
            return -1;
        out[count++] = (int)v;

        p = end;
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
            p++;

        if (*p == ',')
            p++;
        else if (*p == ']')
        {
            *out_count = count;
            return 0;
        }
        else
            return -1;
    }

    return -1;
}

static int parse_draw_object(const char *obj, HistoricalDraw *out_draw, const LotteryInfo *rules)
{
    const char *date_k = strstr(obj, "\"draw_date\"");
    const char *main_k = strstr(obj, "\"main_numbers\"");
    const char *extra_k = strstr(obj, "\"extra_numbers\"");

    if (!date_k || !main_k)
        return -1;

    const char *colon = strchr(date_k, ':');
    if (!colon)
        return -1;

    const char *q1 = strchr(colon, '"');
    if (!q1)
        return -1;
    const char *q2 = strchr(q1 + 1, '"');
    if (!q2)
        return -1;

    size_t date_len = (size_t)(q2 - (q1 + 1));
    if (date_len >= sizeof(out_draw->draw_date))
        return -1;

    memset(out_draw, 0, sizeof(*out_draw));
    memcpy(out_draw->draw_date, q1 + 1, date_len);
    out_draw->draw_date[date_len] = '\0';

    if (validate_iso_date(out_draw->draw_date) != VALIDATE_OK)
        return -1;

    int main_count = 0;
    if (parse_json_int_array(main_k, out_draw->result.main_numbers, MAX_MAIN_NUMBERS,
                             &main_count) != 0)
    {
        return -1;
    }
    out_draw->result.main_count = main_count;

    if (rules && out_draw->result.main_count != rules->main_count)
        return -1;

    if (rules && rules->extra_count > 0)
    {
        int extra_count = 0;
        if (!extra_k || parse_json_int_array(extra_k, out_draw->result.extra_numbers,
                                             MAX_EXTRA_NUMBERS, &extra_count) != 0)
        {
            return -1;
        }
        out_draw->result.extra_count = extra_count;
        if (out_draw->result.extra_count != rules->extra_count)
            return -1;
    }
    else
    {
        out_draw->result.extra_count = 0;
    }

    return 0;
}

static int parse_numbers(const char *field, int *out, int max_out)
{
    char buf[256];
    size_t len = strlen(field);
    if (len >= sizeof(buf))
        return -1;

    strcpy(buf, field);

    int count = 0;
    char *token = strtok(buf, " ");
    while (token)
    {
        if (count >= max_out)
            return -1;

        char *end = NULL;
        long val = strtol(token, &end, 10);
        if (!end || *end != '\0')
            return -1;

        out[count++] = (int)val;
        token = strtok(NULL, " ");
    }

    return count;
}

int analytics_load_historical_csv(const char *csv_path, HistoricalDraw *out_draws, int max_draws,
                                  int *out_count, const LotteryInfo *rules)
{
    if (!csv_path || !out_draws || !out_count || !rules)
        return VALIDATE_ERR_EMPTY;

    FILE *f = fopen(csv_path, "r");
    if (!f)
    {
        fprintf(stderr, "Error: Could not open historical CSV '%s'.\n", csv_path);
        return VALIDATE_ERR_FILE_INVALID;
    }

    char line[1024];
    if (!fgets(line, sizeof(line), f))
    {
        fclose(f);
        fprintf(stderr, "Error: Historical CSV '%s' is empty.\n", csv_path);
        return VALIDATE_ERR_FILE_INVALID;
    }

    /* Expected header: draw_date,main_numbers,extra_numbers */
    int count = 0;
    while (fgets(line, sizeof(line), f))
    {
        if (count >= max_draws)
        {
            fclose(f);
            fprintf(stderr, "Error: Historical CSV exceeds max supported draws (%d).\n", max_draws);
            return VALIDATE_ERR_OUT_OF_RANGE;
        }

        char *nl = strchr(line, '\n');
        if (nl)
            *nl = '\0';

        char *date = strtok(line, ",");
        char *main_field = strtok(NULL, ",");
        char *extra_field = strtok(NULL, ",");

        if (!date || !main_field)
        {
            fclose(f);
            fprintf(stderr, "Error: Malformed historical CSV row.\n");
            return VALIDATE_ERR_INVALID_FORMAT;
        }

        if (!extra_field)
            extra_field = "";

        if (validate_iso_date(date) != VALIDATE_OK)
        {
            fclose(f);
            return VALIDATE_ERR_INVALID_FORMAT;
        }

        HistoricalDraw *d = &out_draws[count];
        memset(d, 0, sizeof(*d));
        strncpy(d->draw_date, date, sizeof(d->draw_date) - 1);
        d->draw_date[sizeof(d->draw_date) - 1] = '\0';

        d->result.main_count = parse_numbers(main_field, d->result.main_numbers, MAX_MAIN_NUMBERS);
        if (d->result.main_count != rules->main_count)
        {
            fclose(f);
            fprintf(stderr, "Error: Main number count mismatch in historical CSV row '%s'.\n",
                    date);
            return VALIDATE_ERR_INVALID_FORMAT;
        }

        if (rules->extra_count > 0)
        {
            d->result.extra_count =
                parse_numbers(extra_field, d->result.extra_numbers, MAX_EXTRA_NUMBERS);
            if (d->result.extra_count != rules->extra_count)
            {
                fclose(f);
                fprintf(stderr, "Error: Extra number count mismatch in historical CSV row '%s'.\n",
                        date);
                return VALIDATE_ERR_INVALID_FORMAT;
            }
        }
        else
        {
            d->result.extra_count = 0;
        }

        count++;
    }

    fclose(f);
    *out_count = count;
    return VALIDATE_OK;
}

int analytics_load_historical_db_snapshot(const char *game_name, const char *db_root,
                                          HistoricalDraw *out_draws, int max_draws, int *out_count,
                                          const LotteryInfo *rules)
{
    if (!game_name || !out_draws || !out_count || !rules || max_draws <= 0)
        return VALIDATE_ERR_EMPTY;

    char path[640];
    if (build_snapshot_path(game_name, db_root, path, sizeof(path)) != 0)
        return VALIDATE_ERR_INVALID_FORMAT;

    char *json = NULL;
    if (read_file_text(path, &json) != 0)
    {
        fprintf(stderr,
                "Error: Could not load historical DB snapshot '%s'. Run --database-gewinnzahlen "
                "update first.\n",
                path);
        return VALIDATE_ERR_FILE_INVALID;
    }

    const char *draws_key = strstr(json, "\"draws\"");
    if (!draws_key)
    {
        free(json);
        fprintf(stderr, "Error: Malformed historical DB snapshot (missing draws array).\n");
        return VALIDATE_ERR_INVALID_FORMAT;
    }

    const char *arr = strchr(draws_key, '[');
    if (!arr)
    {
        free(json);
        fprintf(stderr, "Error: Malformed historical DB snapshot (invalid draws array).\n");
        return VALIDATE_ERR_INVALID_FORMAT;
    }

    int count = 0;
    const char *p = arr + 1;
    while (*p)
    {
        while (*p && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ','))
            p++;

        if (*p == ']')
            break;

        if (*p != '{')
        {
            free(json);
            fprintf(stderr, "Error: Malformed historical DB snapshot (invalid draw object).\n");
            return VALIDATE_ERR_INVALID_FORMAT;
        }

        const char *obj_start = p;
        int depth = 0;
        int in_string = 0;
        int escape = 0;
        while (*p)
        {
            char c = *p;
            if (in_string)
            {
                if (escape)
                    escape = 0;
                else if (c == '\\')
                    escape = 1;
                else if (c == '"')
                    in_string = 0;
            }
            else
            {
                if (c == '"')
                    in_string = 1;
                else if (c == '{')
                    depth++;
                else if (c == '}')
                {
                    depth--;
                    if (depth == 0)
                    {
                        p++;
                        break;
                    }
                }
            }
            p++;
        }

        if (depth != 0)
        {
            free(json);
            fprintf(stderr, "Error: Malformed historical DB snapshot (unclosed draw object).\n");
            return VALIDATE_ERR_INVALID_FORMAT;
        }

        if (count >= max_draws)
        {
            free(json);
            fprintf(stderr,
                    "Error: Historical DB snapshot exceeds max supported draws (%d). Increase "
                    "ANALYTICS_MAX_DRAWS.\n",
                    max_draws);
            return VALIDATE_ERR_OUT_OF_RANGE;
        }

        size_t obj_len = (size_t)(p - obj_start);
        char *obj = (char *)malloc(obj_len + 1);
        if (!obj)
        {
            free(json);
            return VALIDATE_ERR_OUT_OF_RANGE;
        }
        memcpy(obj, obj_start, obj_len);
        obj[obj_len] = '\0';

        if (parse_draw_object(obj, &out_draws[count], rules) != 0)
        {
            free(obj);
            free(json);
            fprintf(stderr,
                    "Error: Historical DB snapshot contains malformed draw data for game '%s'.\n",
                    game_name);
            return VALIDATE_ERR_INVALID_FORMAT;
        }

        free(obj);
        count++;
    }

    free(json);
    *out_count = count;
    return VALIDATE_OK;
}

int analytics_filter_period(const HistoricalDraw *draws, int draw_count, const char *from_date,
                            const char *to_date, HistoricalDraw *out_filtered,
                            int *out_filtered_count)
{
    if (!draws || !from_date || !to_date || !out_filtered || !out_filtered_count)
        return VALIDATE_ERR_EMPTY;

    if (validate_analytics_period(from_date, to_date, NULL, NULL) != VALIDATE_OK)
        return VALIDATE_ERR_INVALID_FORMAT;

    int out_count = 0;
    for (int i = 0; i < draw_count; i++)
    {
        if (strcmp(draws[i].draw_date, from_date) < 0)
            continue;
        if (strcmp(draws[i].draw_date, to_date) > 0)
            continue;
        out_filtered[out_count++] = draws[i];
    }

    *out_filtered_count = out_count;
    return VALIDATE_OK;
}

int analytics_compute_frequency(const HistoricalDraw *draws, int draw_count, int number_min,
                                int number_max, FrequencyReport *out_report)
{
    if (!draws || !out_report || draw_count < 0 || number_min < 0 || number_max >= 128 ||
        number_min > number_max)
    {
        return VALIDATE_ERR_INVALID_FORMAT;
    }

    memset(out_report, 0, sizeof(*out_report));
    out_report->total_draws = draw_count;
    out_report->number_min = number_min;
    out_report->number_max = number_max;

    /* Set date range from first and last draw */
    if (draw_count > 0)
    {
        snprintf(out_report->from_date, sizeof(out_report->from_date), "%s", draws[0].draw_date);
        out_report->from_date[sizeof(out_report->from_date) - 1] = '\0';
        snprintf(out_report->to_date, sizeof(out_report->to_date), "%s",
                 draws[draw_count - 1].draw_date);
        out_report->to_date[sizeof(out_report->to_date) - 1] = '\0';
    }

    for (int i = 0; i < draw_count; i++)
    {
        const LotteryResult *r = &draws[i].result;
        for (int j = 0; j < r->main_count; j++)
        {
            int n = r->main_numbers[j];
            if (n >= number_min && n <= number_max)
                out_report->counts[n]++;
        }
    }

    return VALIDATE_OK;
}

int analytics_compute_barometer(const HistoricalDraw *draws, int draw_count, int number_min,
                                int number_max, int picks_per_draw, BarometerReport *out_report)
{
    if (!draws || !out_report || draw_count < 0 || picks_per_draw <= 0 || number_min < 0 ||
        number_max >= 128 || number_min > number_max)
    {
        return VALIDATE_ERR_INVALID_FORMAT;
    }

    memset(out_report, 0, sizeof(*out_report));
    out_report->total_draws = draw_count;
    out_report->number_min = number_min;
    out_report->number_max = number_max;

    /* Set date range from first and last draw */
    if (draw_count > 0)
    {
        snprintf(out_report->from_date, sizeof(out_report->from_date), "%s", draws[0].draw_date);
        out_report->from_date[sizeof(out_report->from_date) - 1] = '\0';
        snprintf(out_report->to_date, sizeof(out_report->to_date), "%s",
                 draws[draw_count - 1].draw_date);
        out_report->to_date[sizeof(out_report->to_date) - 1] = '\0';
    }

    int population = number_max - number_min + 1;
    out_report->expected_interval = (double)population / (double)picks_per_draw;

    for (int n = number_min; n <= number_max; n++)
    {
        int last_seen = -1;
        for (int i = 0; i < draw_count; i++)
        {
            const LotteryResult *r = &draws[i].result;
            int found = 0;
            for (int j = 0; j < r->main_count; j++)
            {
                if (r->main_numbers[j] == n)
                {
                    out_report->hit_counts[n]++;
                    last_seen = i;
                    found = 1;
                    break;
                }
            }
            if (found)
                continue;
        }

        if (last_seen < 0)
            out_report->observed_gaps[n] = draw_count + 1;
        else
            out_report->observed_gaps[n] = (draw_count - 1) - last_seen;

        out_report->factors[n] = out_report->observed_gaps[n] / out_report->expected_interval;
    }

    return VALIDATE_OK;
}

static int hot_cmp(const void *a, const void *b)
{
    const HotColdEntry *x = (const HotColdEntry *)a;
    const HotColdEntry *y = (const HotColdEntry *)b;
    if (x->count != y->count)
        return (y->count - x->count);
    return x->number - y->number;
}

static int cold_cmp(const void *a, const void *b)
{
    const HotColdEntry *x = (const HotColdEntry *)a;
    const HotColdEntry *y = (const HotColdEntry *)b;
    if (x->count != y->count)
        return (x->count - y->count);
    return x->number - y->number;
}

int analytics_compute_hot_cold(const HistoricalDraw *draws, int draw_count, int number_min,
                               int number_max, int top_n, HotColdReport *out_report)
{
    if (!draws || !out_report || draw_count < 0 || number_min < 0 || number_max >= 128 ||
        number_min > number_max || top_n <= 0)
    {
        return VALIDATE_ERR_INVALID_FORMAT;
    }

    FrequencyReport freq;
    if (analytics_compute_frequency(draws, draw_count, number_min, number_max, &freq) !=
        VALIDATE_OK)
        return VALIDATE_ERR_INVALID_FORMAT;

    HotColdEntry all[128] = {{0}};
    int all_count = 0;
    int denominator = draw_count > 0 ? draw_count : 1;

    for (int n = number_min; n <= number_max; n++)
    {
        all[all_count].number = n;
        all[all_count].count = freq.counts[n];
        all[all_count].percentage = (100.0 * freq.counts[n]) / (double)denominator;
        all_count++;
    }

    memset(out_report, 0, sizeof(*out_report));
    out_report->total_draws = draw_count;
    out_report->top_n = (top_n < all_count) ? top_n : all_count;

    /* Set date range from first and last draw */
    if (draw_count > 0)
    {
        snprintf(out_report->from_date, sizeof(out_report->from_date), "%s", draws[0].draw_date);
        out_report->from_date[sizeof(out_report->from_date) - 1] = '\0';
        snprintf(out_report->to_date, sizeof(out_report->to_date), "%s",
                 draws[draw_count - 1].draw_date);
        out_report->to_date[sizeof(out_report->to_date) - 1] = '\0';
    }

    qsort(all, (size_t)all_count, sizeof(HotColdEntry), hot_cmp);
    for (int i = 0; i < out_report->top_n; i++)
        out_report->hot[i] = all[i];

    qsort(all, (size_t)all_count, sizeof(HotColdEntry), cold_cmp);
    for (int i = 0; i < out_report->top_n; i++)
        out_report->cold[i] = all[i];

    return VALIDATE_OK;
}

void analytics_print_frequency_table(const FrequencyReport *report)
{
    if (!report)
        return;

    printf("Frequency distribution (draws: %d)\n", report->total_draws);
    printf("number,count,percentage\n");
    int denominator = report->total_draws > 0 ? report->total_draws : 1;

    for (int n = report->number_min; n <= report->number_max; n++)
    {
        double pct = (100.0 * report->counts[n]) / (double)denominator;
        printf("%d,%d,%.4f\n", n, report->counts[n], pct);
    }
}

void analytics_print_frequency_csv(const FrequencyReport *report)
{
    analytics_print_frequency_table(report);
}

void analytics_print_frequency_json(const FrequencyReport *report)
{
    if (!report)
        return;

    int denominator = report->total_draws > 0 ? report->total_draws : 1;

    printf("{\n");
    printf("  \"draws\": %d,\n", report->total_draws);
    printf("  \"frequency\": [\n");

    for (int n = report->number_min; n <= report->number_max; n++)
    {
        double pct = (100.0 * report->counts[n]) / (double)denominator;
        printf("    {\"number\": %d, \"count\": %d, \"percentage\": %.4f}%s\n", n,
               report->counts[n], pct, (n == report->number_max) ? "" : ",");
    }

    printf("  ]\n");
    printf("}\n");
}

void analytics_print_frequency_gui_2d(const FrequencyReport *report)
{
    if (!report)
        return;

    printf("[GUI 2D] Frequency Distribution\n");
    int max_count = 1;
    for (int n = report->number_min; n <= report->number_max; n++)
    {
        if (report->counts[n] > max_count)
            max_count = report->counts[n];
    }

    for (int n = report->number_min; n <= report->number_max; n++)
    {
        int bar_len = (40 * report->counts[n]) / max_count;
        printf("%2d | ", n);
        for (int i = 0; i < bar_len; i++)
            putchar('#');
        printf(" (%d)\n", report->counts[n]);
    }
}

void analytics_print_frequency_gui_3d_matlab(const FrequencyReport *report)
{
    if (!report)
        return;

    printf("[GUI 3D] MatLab-style figure description\n");
    printf("figure;\n");
    printf("x = [%d:%d];\n", report->number_min, report->number_max);
    printf("y = [");
    for (int n = report->number_min; n <= report->number_max; n++)
    {
        printf("%d%s", report->counts[n], (n == report->number_max) ? "" : " ");
    }
    printf("];\n");
    printf("bar3(y); xlabel('Number Index'); ylabel('Series'); zlabel('Frequency');\n");
}

void analytics_print_barometer_table(const BarometerReport *report)
{
    if (!report)
        return;

    printf("Barometer (draws: %d, expected interval: %.4f)\n", report->total_draws,
           report->expected_interval);
    printf("number,hits,observed_gap,factor\n");
    for (int n = report->number_min; n <= report->number_max; n++)
    {
        printf("%d,%d,%d,%.6f\n", n, report->hit_counts[n], report->observed_gaps[n],
               report->factors[n]);
    }
}

void analytics_print_barometer_csv(const BarometerReport *report)
{
    analytics_print_barometer_table(report);
}

void analytics_print_barometer_json(const BarometerReport *report)
{
    if (!report)
        return;

    printf("{\n");
    printf("  \"draws\": %d,\n", report->total_draws);
    printf("  \"expected_interval\": %.6f,\n", report->expected_interval);
    printf("  \"barometer\": [\n");

    for (int n = report->number_min; n <= report->number_max; n++)
    {
        printf("    {\"number\": %d, \"hits\": %d, \"observed_gap\": %d, \"factor\": %.6f}%s\n", n,
               report->hit_counts[n], report->observed_gaps[n], report->factors[n],
               (n == report->number_max) ? "" : ",");
    }

    printf("  ]\n");
    printf("}\n");
}

void analytics_print_barometer_gui_2d(const BarometerReport *report)
{
    if (!report)
        return;

    printf("[GUI 2D] Barometer\n");
    for (int n = report->number_min; n <= report->number_max; n++)
    {
        int bar_len = (int)(report->factors[n] * 10.0);
        if (bar_len < 0)
            bar_len = 0;
        if (bar_len > 60)
            bar_len = 60;

        printf("%2d | ", n);
        for (int i = 0; i < bar_len; i++)
            putchar('*');
        printf(" (%.3f)\n", report->factors[n]);
    }
}

void analytics_print_barometer_gui_3d_matlab(const BarometerReport *report)
{
    if (!report)
        return;

    printf("[GUI 3D] MatLab-style figure description\n");
    printf("figure;\n");
    printf("y = [");
    for (int n = report->number_min; n <= report->number_max; n++)
    {
        printf("%.6f%s", report->factors[n], (n == report->number_max) ? "" : " ");
    }
    printf("];\n");
    printf("bar3(y); xlabel('Number Index'); ylabel('Series'); zlabel('Overdue Factor');\n");
}

void analytics_print_hot_cold_table(const HotColdReport *report)
{
    if (!report)
        return;

    printf("Hot and Cold numbers (draws: %d, top: %d)\n", report->total_draws, report->top_n);
    printf("HOT\n");
    printf("rank,number,count,percentage\n");
    for (int i = 0; i < report->top_n; i++)
    {
        printf("%d,%d,%d,%.4f\n", i + 1, report->hot[i].number, report->hot[i].count,
               report->hot[i].percentage);
    }

    printf("COLD\n");
    printf("rank,number,count,percentage\n");
    for (int i = 0; i < report->top_n; i++)
    {
        printf("%d,%d,%d,%.4f\n", i + 1, report->cold[i].number, report->cold[i].count,
               report->cold[i].percentage);
    }
}

void analytics_print_hot_cold_csv(const HotColdReport *report)
{
    analytics_print_hot_cold_table(report);
}

void analytics_print_hot_cold_json(const HotColdReport *report)
{
    if (!report)
        return;

    printf("{\n");
    printf("  \"draws\": %d,\n", report->total_draws);
    printf("  \"top\": %d,\n", report->top_n);

    printf("  \"hot\": [\n");
    for (int i = 0; i < report->top_n; i++)
    {
        printf("    {\"rank\": %d, \"number\": %d, \"count\": %d, \"percentage\": %.4f}%s\n", i + 1,
               report->hot[i].number, report->hot[i].count, report->hot[i].percentage,
               (i + 1 == report->top_n) ? "" : ",");
    }
    printf("  ],\n");

    printf("  \"cold\": [\n");
    for (int i = 0; i < report->top_n; i++)
    {
        printf("    {\"rank\": %d, \"number\": %d, \"count\": %d, \"percentage\": %.4f}%s\n", i + 1,
               report->cold[i].number, report->cold[i].count, report->cold[i].percentage,
               (i + 1 == report->top_n) ? "" : ",");
    }
    printf("  ]\n");
    printf("}\n");
}

void analytics_print_hot_cold_gui_2d(const HotColdReport *report)
{
    if (!report)
        return;

    printf("[GUI 2D] Hot/Cold overview\n");
    printf("HOT: ");
    for (int i = 0; i < report->top_n; i++)
    {
        printf("%d%s", report->hot[i].number, (i + 1 == report->top_n) ? "" : " ");
    }
    printf("\n");

    printf("COLD: ");
    for (int i = 0; i < report->top_n; i++)
    {
        printf("%d%s", report->cold[i].number, (i + 1 == report->top_n) ? "" : " ");
    }
    printf("\n");
}

void analytics_print_hot_cold_gui_3d_matlab(const HotColdReport *report)
{
    if (!report)
        return;

    printf("[GUI 3D] MatLab-style figure description\n");
    printf("figure;\n");
    printf("hot = [");
    for (int i = 0; i < report->top_n; i++)
        printf("%d%s", report->hot[i].count, (i + 1 == report->top_n) ? "" : " ");
    printf("];\n");
    printf("cold = [");
    for (int i = 0; i < report->top_n; i++)
        printf("%d%s", report->cold[i].count, (i + 1 == report->top_n) ? "" : " ");
    printf("];\n");
    printf("bar3([hot; cold]); legend('Hot','Cold');\n");
}
