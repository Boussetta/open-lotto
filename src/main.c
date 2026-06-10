/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "analytics.h"
#include "analytics_data_quality.h"
#include "combogen.h"
#include "config.h"
#include "export.h"
#include "gui_opengl.h"
#include "gui_sdl.h"
#include "historical_db.h"
#include "localization.h"
#include "log.h"
#include "plugin_loader.h"
#include "plugin_registry.h"
#include "random_seed.h"
#include "theme.h"
#include "validate.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define portable_msleep(ms) Sleep(ms)
#else
#include <strings.h>
#include <unistd.h>
#define portable_msleep(ms) usleep((ms) * 1000)
#endif
#include <time.h>

/* ---------------------------------------------------------
   SPINNER ANIMATION
   --------------------------------------------------------- */
static const char *SPINNER_FRAMES[] = {"-", "\\", "|", "/", "-", "\\", "|", "/", "-", "/"};
static const int SPINNER_COUNT = 10;
static const char *g_cli_locale = "en";

/* ---------------------------------------------------------
   ANIMATION HELPERS
   --------------------------------------------------------- */

/* Print main numbers range from index 0 to count-1 */
static void print_main_numbers(const LotteryResult *result, int count)
{
    printf("%s: ", localization_get(g_cli_locale, LOCALIZE_MAIN));
    for (int j = 0; j < count; j++)
        printf("%d ", result->main_numbers[j]);
}

/* Print main numbers + separator + extra numbers range */
static void print_main_and_extra(const LotteryResult *result, int main_count, int extra_count)
{
    printf("%s: ", localization_get(g_cli_locale, LOCALIZE_MAIN));
    for (int j = 0; j < main_count; j++)
        printf("%d ", result->main_numbers[j]);
    printf("| %s: ", localization_get(g_cli_locale, LOCALIZE_EXTRA));
    for (int j = 0; j < extra_count; j++)
        printf("%d ", result->extra_numbers[j]);
}

/* Animate a single number reveal with spinner animation */
static void animate_number_reveal(int number, void (*print_prefix_fn)(const LotteryResult *, int),
                                  const LotteryResult *result, int revealed_count)
{
    /* Spinner animation frames */
    for (int f = 0; f < SPINNER_COUNT; f++)
    {
        printf("\r");
        print_prefix_fn(result, revealed_count);
        printf("%s ", SPINNER_FRAMES[f]);
        fflush(stdout);
        portable_msleep(30);
    }

    /* Reveal the actual number */
    printf("\r");
    print_prefix_fn(result, revealed_count);
    printf("%d ", number);
    fflush(stdout);
    portable_msleep(150);
}

/* ---------------------------------------------------------
   ANIMATE NUMBER REVEAL (same line, spinner-only)
   --------------------------------------------------------- */
static void animate_numbers(const LotteryInfo *info, const LotteryResult *result)
{
    printf("%s\n\n", localization_get(g_cli_locale, LOCALIZE_DRAWING_NUMBERS));

    printf("%s: ", localization_get(g_cli_locale, LOCALIZE_MAIN));

    /* Animate main numbers */
    for (int i = 0; i < info->main_count; i++)
    {
        animate_number_reveal(result->main_numbers[i], print_main_numbers, result, i);
    }

    /* Animate extra numbers if present */
    if (info->extra_count > 0)
    {
        printf("| %s: ", localization_get(g_cli_locale, LOCALIZE_EXTRA));

        for (int i = 0; i < info->extra_count; i++)
        {
            /* Custom print function for extra numbers */
            for (int f = 0; f < SPINNER_COUNT; f++)
            {
                printf("\r");
                print_main_and_extra(result, info->main_count, i);
                printf("%s ", SPINNER_FRAMES[f]);
                fflush(stdout);
                portable_msleep(30);
            }

            /* Reveal extra number */
            printf("\r");
            print_main_and_extra(result, info->main_count, i);
            printf("%d ", result->extra_numbers[i]);
            fflush(stdout);
            portable_msleep(150);
        }
    }

    printf("\n\n");
}

/* ---------------------------------------------------------
   SILENT CALLBACK FOR ANIMATION MODE
   --------------------------------------------------------- */
static void silent_callback(DrawEvent event, const LotteryResult *res)
{
    (void)event;
    (void)res;
}

/* Print draw result in normal (non-animated) CLI mode */
static void print_draw_result(const char *game_name, int draw_num, const LotteryResult *result)
{
    printf("%s (%s %d):\n", game_name, localization_get(g_cli_locale, LOCALIZE_DRAW), draw_num);
    printf("  %s: ", localization_get(g_cli_locale, LOCALIZE_MAIN));
    for (int j = 0; j < result->main_count; j++)
        printf("%d ", result->main_numbers[j]);

    if (result->extra_count > 0)
    {
        printf("| %s: ", localization_get(g_cli_locale, LOCALIZE_EXTRA));
        for (int j = 0; j < result->extra_count; j++)
            printf("%d ", result->extra_numbers[j]);
    }

    printf("\n\n");
}

/* ---------------------------------------------------------
   PARSE LOG LEVEL FROM STRING
   --------------------------------------------------------- */
static LogLevel parse_log_level(const char *level_str)
{
    if (!level_str)
        return LOG_INFO;

    if (strcasecmp(level_str, "ERROR") == 0)
        return LOG_ERROR;
    if (strcasecmp(level_str, "WARN") == 0)
        return LOG_WARN;
    if (strcasecmp(level_str, "INFO") == 0)
        return LOG_INFO;
    if (strcasecmp(level_str, "DEBUG") == 0)
        return LOG_DEBUG;

    fprintf(stderr, "Invalid log level: %s (use ERROR, WARN, INFO, or DEBUG)\n", level_str);
    return LOG_INFO;
}

static uint64_t splitmix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static int parse_seed_value(const char *seed_str, uint64_t *out_seed)
{
    char *end = NULL;
    errno = 0;

    unsigned long long parsed = strtoull(seed_str, &end, 0);
    if (errno != 0 || end == seed_str || (end && *end != '\0'))
        return 0;

    *out_seed = (uint64_t)parsed;
    return 1;
}

static uint64_t derive_draw_seed(uint64_t base_seed, int draw_index)
{
    if (draw_index == 0)
        return base_seed;

    return splitmix64(base_seed ^ (uint64_t)draw_index);
}

/* ---------------------------------------------------------
   Usage
   --------------------------------------------------------- */
static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s --game NAME [--draws N] [--animate] [--gui [2D|3D]] [--reload-plugin] [--verbose "
            "LEVEL]\n"
            "  %s --game NAME [--draws N] [--export csv|json] [--output FILE] [--reload-plugin] "
            "[--verbose LEVEL]\n"
            "  %s --game NAME --database-gewinnzahlen update\n"
            "  %s --game NAME --validate-only\n"
            "  %s --game NAME --download-url set URL\n"
            "  %s --download-config NAME set VALUE\n"
            "  %s --list-games\n"
            "\n"
            "Modes:\n"
            "  --animate              Animated CLI draw display (spinner animation)\n"
            "  --gui [2D|3D]          Graphical mode (default: 2D SDL2, or 3D OpenGL)\n"
            "  --dark-mode <mode>     Dark mode theme: on, off, or auto (default: auto)\n"
            "  --debug-overlay        Show FPS/physics HUD in 3D GUI (requires --gui 3D)\n"
            "  --export FORMAT        Export results to file (csv or json)\n"
            "  --database-gewinnzahlen update\n"
            "                         Sync local DB with latest official draw (per game)\n"
            "  --download-url set URL\n"
            "                         Set game source URL in sources config (requires --game)\n"
            "  --download-config NAME set VALUE\n"
            "                         Persist a download tuning value in download config\n"
            "  --reload-plugin        Reload the selected plugin from disk before running\n"
            "  --validate-only        Validate configuration without running\n"
            "\n"
            "Output Options:\n"
            "  --output FILE     Destination file for --export (required with --export)\n"
            "  --draws N         Number of draws (default: 1)\n"
            "  --seed VALUE      Deterministic seed (decimal or 0x-prefixed hex)\n"
            "  --verbose LEVEL   Log level: ERROR, WARN, INFO, DEBUG (default: INFO)\n"
            "\n"
            "Analytics Period Options:\n"
            "  --from YYYY-MM-DD Inclusive period start date for analytics APIs\n"
            "  --to YYYY-MM-DD   Inclusive period end date for analytics APIs\n"
            "  --frequency-distribution Print frequency distribution over historical data\n"
            "  --analytics-barometer   Print overdue barometer over historical data\n"
            "  --analytics-hot-cold    Print hot/cold number rankings over historical data\n"
            "  --top N                 Number of hot/cold entries (default: 10)\n"
            "  --explain               Show formulas/assumptions for analytics outputs\n"
            "  --format FORMAT         Analytics output format: table, json, csv\n"
            "  --historical-csv FILE   Historical draw CSV override (simulation/dev datasets)\n"
            "                         By default analytics read local real-data DB snapshot\n"
            "\n"
            "Log Levels:\n"
            "  ERROR, WARN, INFO (default), DEBUG\n"
            "\n"
            "Config File (~/.lottorc):\n"
            "  Persistent defaults for any option. CLI arguments always override config.\n"
            "  Example ~/.lottorc:\n"
            "    [defaults]\n"
            "    game = Lotto 6aus49\n"
            "    draws = 10\n"
            "    verbose = INFO\n"
            "\n"
            "Download Config Files:\n"
            "  ~/.config/open-lotto/sources.conf   Upstream source URLs per game\n"
            "  ~/.config/open-lotto/download.conf  Persistent download tuning values\n"
            "\n"
            "Examples:\n"
            "  %s --list-games\n"
            "  %s --game \"Lotto 6aus49\" --database-gewinnzahlen update\n"
            "  %s --game \"Lotto 6aus49\"\n"
            "  %s --game \"Lotto 6aus49\" --draws 10\n"
            "  %s --game \"Lotto 6aus49\" --draws 10 --seed 0x1234abcd\n"
            "  %s --game \"Lotto 6aus49\" --animate\n"
            "  %s --game \"Lotto 6aus49\" --gui 3D\n"
            "  %s --game \"Lotto 6aus49\" --draws 100 --export csv --output results.csv\n"
            "  %s --game \"Lotto 6aus49\" --validate-only\n"
            "  %s --game \"Lotto 6aus49\" --verbose DEBUG\n"
            "  %s --game \"Eurojackpot\" --database-gewinnzahlen update\n"
            "  %s --game \"Lotto 6aus49\" --download-url set https://example/api\n"
            "  %s --download-config \"#sym:HISTORICAL_DB_DOWNLOAD_WORKERS_DEFAULT\" set 10\n"
            "\n"
            "Environment Variables:\n"
            "  OPEN_LOTTO_PLUGIN_PATH  Custom plugin directory path\n"
            "  OPEN_LOTTO_LANG         CLI locale (en, fr; fallback to en)\n"
            "  OPEN_LOTTO_GEWINNZAHLEN_URL_EUROJACKPOT  Override upstream sync endpoint\n"
            "  OPEN_LOTTO_SOURCES_CONFIG               Override sources config path\n"
            "  OPEN_LOTTO_DOWNLOAD_CONFIG              Override download config path\n",
            prog, prog, prog, prog, prog, prog, prog, prog, prog, prog, prog, prog, prog, prog,
            prog, prog, prog, prog, prog, prog);
}

static void trim_whitespace(char *s)
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

static int ensure_directory_recursive(const char *dir_path)
{
    if (!dir_path || dir_path[0] == '\0')
        return -1;

    char tmp[PATH_MAX];
    int n = snprintf(tmp, sizeof(tmp), "%s", dir_path);
    if (n <= 0 || (size_t)n >= sizeof(tmp))
        return -1;

    for (char *p = tmp + 1; *p; ++p)
    {
        if (*p != '/')
            continue;

        *p = '\0';
#ifdef _WIN32
        if (_mkdir(tmp) != 0 && errno != EEXIST)
            return -1;
#else
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
            return -1;
#endif
        *p = '/';
    }

#ifdef _WIN32
    if (_mkdir(tmp) != 0 && errno != EEXIST)
        return -1;
#else
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return -1;
#endif
    return 0;
}

static int ensure_parent_directory(const char *file_path)
{
    if (!file_path)
        return -1;

    char dir[PATH_MAX];
    int n = snprintf(dir, sizeof(dir), "%s", file_path);
    if (n <= 0 || (size_t)n >= sizeof(dir))
        return -1;

    char *slash = strrchr(dir, '/');
    if (!slash)
        return 0;

    *slash = '\0';
    if (dir[0] == '\0')
        return 0;

    return ensure_directory_recursive(dir);
}

static int get_sources_config_path_cli(char *out, size_t out_size)
{
    const char *override = getenv("OPEN_LOTTO_SOURCES_CONFIG");
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

static int get_download_config_path_cli(char *out, size_t out_size)
{
    const char *override = getenv("OPEN_LOTTO_DOWNLOAD_CONFIG");
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

static int parse_section_header(const char *line, char *section, size_t section_size)
{
    if (!line || line[0] != '[')
        return 0;

    const char *end = strchr(line, ']');
    if (!end || end <= line + 1)
        return 0;

    size_t len = (size_t)(end - (line + 1));
    if (len >= section_size)
        return 0;

    memcpy(section, line + 1, len);
    section[len] = '\0';
    trim_whitespace(section);
    return 1;
}

static int parse_key_value(char *line, char **key_out, char **value_out)
{
    char *eq = strchr(line, '=');
    if (!eq)
        return 0;

    *eq = '\0';
    char *key = line;
    char *value = eq + 1;
    trim_whitespace(key);
    trim_whitespace(value);
    if (key[0] == '\0')
        return 0;

    *key_out = key;
    *value_out = value;
    return 1;
}

static int ini_set_value(const char *path, const char *target_section, const char *target_key,
                         const char *new_value)
{
    if (!path || !target_section || !target_key || !new_value)
        return -1;

    if (ensure_parent_directory(path) != 0)
        return -1;

    char temp_path[PATH_MAX];
    int n = snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
    if (n <= 0 || (size_t)n >= sizeof(temp_path))
        return -1;

    FILE *in = fopen(path, "r");
    FILE *out = fopen(temp_path, "w");
    if (!out)
    {
        if (in)
            fclose(in);
        return -1;
    }

    int found_section = 0;
    int wrote_key = 0;

    if (in)
    {
        int in_target_section = 0;
        char line[1024];
        while (fgets(line, sizeof(line), in))
        {
            char line_copy[1024];
            int copied = snprintf(line_copy, sizeof(line_copy), "%s", line);
            if (copied <= 0 || (size_t)copied >= sizeof(line_copy))
            {
                fclose(in);
                fclose(out);
                remove(temp_path);
                return -1;
            }

            trim_whitespace(line_copy);

            char section[256];
            if (parse_section_header(line_copy, section, sizeof(section)))
            {
                if (in_target_section && !wrote_key)
                {
                    fprintf(out, "%s = %s\n", target_key, new_value);
                    wrote_key = 1;
                }

                in_target_section = (strcasecmp(section, target_section) == 0);
                if (in_target_section)
                    found_section = 1;

                fputs(line, out);
                continue;
            }

            if (in_target_section && line_copy[0] != '\0' && line_copy[0] != '#' &&
                line_copy[0] != ';')
            {
                char parsed[1024];
                snprintf(parsed, sizeof(parsed), "%s", line_copy);
                char *key = NULL;
                char *value = NULL;
                if (parse_key_value(parsed, &key, &value) && strcasecmp(key, target_key) == 0)
                {
                    fprintf(out, "%s = %s\n", target_key, new_value);
                    wrote_key = 1;
                    continue;
                }
            }

            fputs(line, out);
        }

        fclose(in);
    }

    if (found_section)
    {
        if (!wrote_key)
            fprintf(out, "%s = %s\n", target_key, new_value);
    }
    else
    {
        fprintf(out, "[%s]\n%s = %s\n", target_section, target_key, new_value);
    }

    if (fclose(out) != 0)
    {
        remove(temp_path);
        return -1;
    }

    if (rename(temp_path, path) != 0)
    {
        remove(temp_path);
        return -1;
    }

    return 0;
}

static const char *game_source_key_cli(const char *game_name)
{
    if (!game_name)
        return NULL;

    if (strcasecmp(game_name, "Eurojackpot") == 0)
        return "eurojackpot";

    if (strcasecmp(game_name, "Lotto 6aus49") == 0 || strcasecmp(game_name, "Lotto") == 0)
        return "lotto";

    return NULL;
}

static int map_download_config_key(const char *raw_name, const char **out_key)
{
    if (!raw_name || !out_key)
        return 0;

    const char *name = raw_name;
    if (strncmp(name, "#sym:", 5) == 0)
        name += 5;

    if (strcasecmp(name, "workers") == 0 ||
        strcasecmp(name, "HISTORICAL_DB_DOWNLOAD_WORKERS_DEFAULT") == 0 ||
        strcasecmp(name, "OPEN_LOTTO_HIST_DOWNLOAD_WORKERS") == 0)
    {
        *out_key = "workers";
        return 1;
    }
    if (strcasecmp(name, "fetch_timeout_sec") == 0 ||
        strcasecmp(name, "HISTORICAL_DB_FETCH_TIMEOUT_SEC_DEFAULT") == 0 ||
        strcasecmp(name, "OPEN_LOTTO_HIST_FETCH_TIMEOUT_SEC") == 0)
    {
        *out_key = "fetch_timeout_sec";
        return 1;
    }
    if (strcasecmp(name, "draw_timeout_sec") == 0 ||
        strcasecmp(name, "HISTORICAL_DB_DRAW_TIMEOUT_SEC_DEFAULT") == 0 ||
        strcasecmp(name, "OPEN_LOTTO_HIST_DRAW_TIMEOUT_SEC") == 0)
    {
        *out_key = "draw_timeout_sec";
        return 1;
    }
    if (strcasecmp(name, "max_retry_attempts") == 0 ||
        strcasecmp(name, "HISTORICAL_DB_MAX_RETRY_ATTEMPTS_DEFAULT") == 0 ||
        strcasecmp(name, "OPEN_LOTTO_HIST_MAX_RETRY_ATTEMPTS") == 0)
    {
        *out_key = "max_retry_attempts";
        return 1;
    }
    if (strcasecmp(name, "retry_base_delay_ms") == 0 ||
        strcasecmp(name, "HISTORICAL_DB_RETRY_BASE_DELAY_MS_DEFAULT") == 0 ||
        strcasecmp(name, "OPEN_LOTTO_HIST_RETRY_BASE_DELAY_MS") == 0)
    {
        *out_key = "retry_base_delay_ms";
        return 1;
    }
    if (strcasecmp(name, "retry_max_delay_ms") == 0 ||
        strcasecmp(name, "HISTORICAL_DB_RETRY_MAX_DELAY_MS_DEFAULT") == 0 ||
        strcasecmp(name, "OPEN_LOTTO_HIST_RETRY_MAX_DELAY_MS") == 0)
    {
        *out_key = "retry_max_delay_ms";
        return 1;
    }
    if (strcasecmp(name, "max_fetch_draws") == 0 ||
        strcasecmp(name, "OPEN_LOTTO_HIST_MAX_FETCH_DRAWS") == 0)
    {
        *out_key = "max_fetch_draws";
        return 1;
    }

    return 0;
}

/* ---------------------------------------------------------
   MAIN
   --------------------------------------------------------- */
int main(int argc, char **argv)
{
    /* Load config file early so defaults are available */
    LoCalConfig cfg;
    config_load_lottorc(&cfg);
    g_cli_locale = localization_detect_locale();

    /* Handle help command */
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0))
    {
        print_usage(argv[0]);
        config_free(&cfg);
        return 0;
    }

    /* Handle --list-games command */
    if (argc >= 2 && strcmp(argv[1], "--list-games") == 0)
    {
        PluginRegistry *registry = registry_create();
        if (!registry)
        {
            log_error("Failed to create plugin registry");
            config_free(&cfg);
            return 1;
        }
        registry_discover_plugins(registry);
        registry_list_games(registry);
        registry_destroy(registry);
        config_free(&cfg);
        return 0;
    }

    /* Variables for parsed CLI options; use sentinels to track what was set */
    const char *game_name = NULL; /* NULL = not set by CLI */
    int cli_draws = -1;           /* -1 = not set by CLI */
    int animate = 0;
    int gui = 0;
    const char *gui_mode = NULL;
    int debug_overlay = 0;
#if !OPEN_LOTTO_ENABLE_OPENGL
    (void)debug_overlay;
#endif
    int dark_mode = -1; /* -1 = auto (detect from system), 0 = off, 1 = on */
    int cli_log_level_set = 0;
    LogLevel log_level = LOG_INFO;
    const char *export_format = NULL;
    const char *export_filename = NULL;
    int validate_only = 0;
    int reload_plugin = 0;
    const char *database_gewinnzahlen_cmd = NULL;
    const char *download_url_cmd = NULL;
    const char *download_url_value = NULL;
    const char *download_config_name = NULL;
    const char *download_config_cmd = NULL;
    const char *download_config_value = NULL;
    int use_seed = 0;
    uint64_t seed_value = 0;
    const char *period_from = NULL;
    const char *period_to = NULL;
    int analytics_frequency = 0;
    int analytics_barometer = 0;
    int analytics_hot_cold = 0;
    int analytics_top = 10;
    int analytics_explain = 0;
    const char *analytics_format = "table";
    const char *historical_csv = NULL;
    int historical_csv_from_cli = 0;

    /* ---------------------------------------------------------
       Parse arguments (all options, --game may appear anywhere)
       --------------------------------------------------------- */
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--game") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--game requires a game name.\n");
                config_free(&cfg);
                return 1;
            }
            game_name = argv[++i];
        }
        else if (strcmp(argv[i], "--animate") == 0)
        {
            animate = 1;
        }
        else if (strcmp(argv[i], "--gui") == 0)
        {
            gui = 1;
            /* Check if next argument is a GUI mode specification */
            if (i + 1 < argc && (strcmp(argv[i + 1], "2D") == 0 || strcmp(argv[i + 1], "3D") == 0))
            {
                gui_mode = argv[++i];
            }
        }
        else if (strcmp(argv[i], "--debug-overlay") == 0)
        {
#if OPEN_LOTTO_ENABLE_OPENGL
            debug_overlay = 1;
#else
            log_warn("Ignoring --debug-overlay: OpenGL backend is disabled in this build.");
#endif
        }
        else if (strcmp(argv[i], "--dark-mode") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--dark-mode requires 'on', 'off', or 'auto'.\n");
                config_free(&cfg);
                return 1;
            }
            i++;
            const char *dm_str = argv[i];
            if (strcasecmp(dm_str, "on") == 0)
                dark_mode = 1;
            else if (strcasecmp(dm_str, "off") == 0)
                dark_mode = 0;
            else if (strcasecmp(dm_str, "auto") == 0)
                dark_mode = -1;
            else
            {
                fprintf(stderr, "Invalid --dark-mode value '%s' (use: on, off, auto)\n", dm_str);
                config_free(&cfg);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--verbose") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--verbose requires a log level (ERROR, WARN, INFO, DEBUG).\n");
                config_free(&cfg);
                return 1;
            }

            const char *level_str = argv[++i];
            if (validate_log_level(level_str) != VALIDATE_OK)
            {
                config_free(&cfg);
                return 1;
            }
            log_level = parse_log_level(level_str);
            cli_log_level_set = 1;
        }
        else if (strcmp(argv[i], "--draws") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--draws requires a number.\n");
                config_free(&cfg);
                return 1;
            }

            if (validate_draw_count(argv[++i], &cli_draws) != VALIDATE_OK)
            {
                config_free(&cfg);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--export") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--export requires a format (csv or json).\n");
                config_free(&cfg);
                return 1;
            }

            export_format = argv[++i];
            if (validate_export_format(export_format) != VALIDATE_OK)
            {
                config_free(&cfg);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--output") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--output requires a filename.\n");
                config_free(&cfg);
                return 1;
            }

            export_filename = argv[++i];
        }
        else if (strcmp(argv[i], "--validate-only") == 0)
        {
            validate_only = 1;
        }
        else if (strcmp(argv[i], "--reload-plugin") == 0)
        {
            reload_plugin = 1;
        }
        else if (strcmp(argv[i], "--database-gewinnzahlen") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--database-gewinnzahlen requires a subcommand (use: update).\n");
                config_free(&cfg);
                return 1;
            }
            database_gewinnzahlen_cmd = argv[++i];
            if (strcmp(database_gewinnzahlen_cmd, "update") != 0)
            {
                fprintf(stderr,
                        "Unsupported --database-gewinnzahlen subcommand '%s' (use: update).\n",
                        database_gewinnzahlen_cmd);
                config_free(&cfg);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--download-url") == 0)
        {
            if (i + 2 >= argc)
            {
                fprintf(stderr, "--download-url requires: set <url>.\n");
                config_free(&cfg);
                return 1;
            }

            download_url_cmd = argv[++i];
            if (strcmp(download_url_cmd, "set") != 0)
            {
                fprintf(stderr, "Unsupported --download-url subcommand '%s' (use: set).\n",
                        download_url_cmd);
                config_free(&cfg);
                return 1;
            }

            download_url_value = argv[++i];
            if (download_url_value[0] == '\0')
            {
                fprintf(stderr, "--download-url set requires a non-empty URL.\n");
                config_free(&cfg);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--download-config") == 0)
        {
            if (i + 3 >= argc)
            {
                fprintf(stderr, "--download-config requires: <name> set <value>.\n");
                config_free(&cfg);
                return 1;
            }

            download_config_name = argv[++i];
            download_config_cmd = argv[++i];
            if (strcmp(download_config_cmd, "set") != 0)
            {
                fprintf(stderr, "Unsupported --download-config subcommand '%s' (use: set).\n",
                        download_config_cmd);
                config_free(&cfg);
                return 1;
            }

            download_config_value = argv[++i];
            if (download_config_value[0] == '\0')
            {
                fprintf(stderr, "--download-config set requires a non-empty value.\n");
                config_free(&cfg);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--seed") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--seed requires a numeric value (decimal or 0x-prefixed hex).\n");
                config_free(&cfg);
                return 1;
            }

            if (!parse_seed_value(argv[++i], &seed_value))
            {
                fprintf(stderr,
                        "Invalid --seed value '%s' (expected decimal or 0x-prefixed hex).\n",
                        argv[i]);
                config_free(&cfg);
                return 1;
            }
            use_seed = 1;
        }
        else if (strcmp(argv[i], "--from") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--from requires a date value in YYYY-MM-DD format.\n");
                config_free(&cfg);
                return 1;
            }
            period_from = argv[++i];
        }
        else if (strcmp(argv[i], "--to") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--to requires a date value in YYYY-MM-DD format.\n");
                config_free(&cfg);
                return 1;
            }
            period_to = argv[++i];
        }
        else if (strcmp(argv[i], "--frequency-distribution") == 0 ||
                 strcmp(argv[i], "--analytics-frequency") == 0)
        {
            analytics_frequency = 1;
        }
        else if (strcmp(argv[i], "--analytics-barometer") == 0)
        {
            analytics_barometer = 1;
        }
        else if (strcmp(argv[i], "--analytics-hot-cold") == 0)
        {
            analytics_hot_cold = 1;
        }
        else if (strcmp(argv[i], "--format") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--format requires one of: table, json, csv.\n");
                config_free(&cfg);
                return 1;
            }

            analytics_format = argv[++i];
            if (strcmp(analytics_format, "table") != 0 && strcmp(analytics_format, "json") != 0 &&
                strcmp(analytics_format, "csv") != 0)
            {
                fprintf(stderr, "Error: Unsupported --format '%s'. Use table, json, or csv.\n",
                        analytics_format);
                config_free(&cfg);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--historical-csv") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--historical-csv requires a file path.\n");
                config_free(&cfg);
                return 1;
            }
            historical_csv = argv[++i];
            historical_csv_from_cli = 1;
        }
        else if (strcmp(argv[i], "--top") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "--top requires a positive integer.\n");
                config_free(&cfg);
                return 1;
            }

            char *end = NULL;
            long parsed = strtol(argv[++i], &end, 10);
            if (!end || *end != '\0' || parsed <= 0 || parsed > 128)
            {
                fprintf(stderr, "Error: --top must be an integer between 1 and 128.\n");
                config_free(&cfg);
                return 1;
            }
            analytics_top = (int)parsed;
        }
        else if (strcmp(argv[i], "--explain") == 0)
        {
            analytics_explain = 1;
        }
        else
        {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            config_free(&cfg);
            return 1;
        }
    }

    int analytics_mode_count = analytics_frequency + analytics_barometer + analytics_hot_cold;

    /* ---------------------------------------------------------
       Apply config file defaults for options not set by CLI
       --------------------------------------------------------- */
    if (!game_name && cfg.game)
    {
        game_name = cfg.game;
        log_debug("Using game from config file: %s", game_name);
    }

    int draws = 1; /* built-in default */
    if (cli_draws >= 1)
    {
        draws = cli_draws;
    }
    else if (cfg.draws >= 1)
    {
        draws = cfg.draws;
        log_debug("Using draws from config file: %d", draws);
    }

    if (!export_format && cfg.export_format)
    {
        export_format = cfg.export_format;
        log_debug("Using export format from config file: %s", export_format);
        if (validate_export_format(export_format) != VALIDATE_OK)
        {
            config_free(&cfg);
            return 1;
        }
    }

    if (!export_filename && cfg.output_file)
    {
        export_filename = cfg.output_file;
        log_debug("Using output file from config file: %s", export_filename);
    }

    if (!cli_log_level_set && cfg.verbose_level)
    {
        if (validate_log_level(cfg.verbose_level) == VALIDATE_OK)
        {
            log_level = parse_log_level(cfg.verbose_level);
            log_debug("Using verbose level from config file: %s", cfg.verbose_level);
        }
    }

    if (!gui_mode && cfg.gui_mode)
    {
        gui_mode = cfg.gui_mode;
        if (strcmp(gui_mode, "2D") == 0 || strcmp(gui_mode, "3D") == 0)
        {
            gui = 1;
            log_debug("Using GUI mode from config file: %s", gui_mode);
        }
    }

    if (download_url_cmd || download_config_cmd)
    {
        if (download_url_cmd)
        {
            const char *source_key = game_source_key_cli(game_name);
            if (!source_key)
            {
                fprintf(stderr, "--download-url set requires a supported --game (Eurojackpot or "
                                "Lotto 6aus49).\n");
                config_free(&cfg);
                return 1;
            }

            char sources_path[PATH_MAX];
            if (get_sources_config_path_cli(sources_path, sizeof(sources_path)) != 0)
            {
                fprintf(stderr, "Failed to resolve sources config path.\n");
                config_free(&cfg);
                return 1;
            }

            if (ini_set_value(sources_path, "sources", source_key, download_url_value) != 0)
            {
                fprintf(stderr, "Failed to update sources config at %s.\n", sources_path);
                config_free(&cfg);
                return 1;
            }

            printf("Updated source URL for '%s' in %s\n", source_key, sources_path);
        }

        if (download_config_cmd)
        {
            const char *config_key = NULL;
            if (!map_download_config_key(download_config_name, &config_key))
            {
                fprintf(
                    stderr,
                    "Unknown --download-config key '%s'. Use one of: workers, fetch_timeout_sec, "
                    "draw_timeout_sec, max_retry_attempts, retry_base_delay_ms, "
                    "retry_max_delay_ms, max_fetch_draws, or #sym:* aliases.\n",
                    download_config_name);
                config_free(&cfg);
                return 1;
            }

            char download_path[PATH_MAX];
            if (get_download_config_path_cli(download_path, sizeof(download_path)) != 0)
            {
                fprintf(stderr, "Failed to resolve download config path.\n");
                config_free(&cfg);
                return 1;
            }

            if (ini_set_value(download_path, "download", config_key, download_config_value) != 0)
            {
                fprintf(stderr, "Failed to update download config at %s.\n", download_path);
                config_free(&cfg);
                return 1;
            }

            printf("Updated download setting '%s' in %s\n", config_key, download_path);
        }

        config_free(&cfg);
        return 0;
    }

    /* Require --game (or game in config) */
    if (!game_name)
    {
        fprintf(stderr, "Error: --game NAME is required.\n"
                        "Hint: Set 'game = <name>' in ~/.lottorc for a persistent default.\n");
        print_usage(argv[0]);
        config_free(&cfg);
        return 1;
    }

    /* Validate option combinations */
    if (validate_option_conflicts(animate, gui, export_format) != VALIDATE_OK)
    {
        config_free(&cfg);
        return 1;
    }

    if (validate_export_pair(export_format, export_filename) != VALIDATE_OK)
    {
        config_free(&cfg);
        return 1;
    }

    if (gui_mode && validate_gui_mode(gui_mode) != VALIDATE_OK)
    {
        config_free(&cfg);
        return 1;
    }

    /* Validate --from / --to period: both must be present if either is given */
    if (period_from && !period_to)
    {
        fprintf(stderr, "Error: --from requires --to.\n");
        fprintf(stderr, "Hint: Example: --from 2025-01-01 --to 2025-12-31\n");
        config_free(&cfg);
        return 1;
    }
    if (period_to && !period_from)
    {
        fprintf(stderr, "Error: --to requires --from.\n");
        fprintf(stderr, "Hint: Example: --from 2025-01-01 --to 2025-12-31\n");
        config_free(&cfg);
        return 1;
    }
    if (period_from && period_to)
    {
        if (validate_analytics_period(period_from, period_to, NULL, NULL) != VALIDATE_OK)
        {
            config_free(&cfg);
            return 1;
        }
    }

    if (gui && use_seed)
    {
        fprintf(stderr, "Error: --seed is currently supported only in CLI mode.\n");
        config_free(&cfg);
        return 1;
    }

    /* Default GUI mode to 2D if not specified */
    if (!gui_mode)
    {
        gui_mode = "2D";
    }

    /* Set the log level early so all subsequent operations are logged */
    log_set_level(log_level);
    log_debug("Starting open-lotto with log level set to DEBUG");

    /* ---------------------------------------------------------
       Discover and load plugins
       --------------------------------------------------------- */
    PluginRegistry *registry = registry_create();
    if (!registry)
    {
        log_error("Failed to create plugin registry");
        config_free(&cfg);
        return 1;
    }

    registry_discover_plugins(registry);

    /* Validate game name against discovered plugins */
    if (validate_game_name(game_name, registry) != VALIDATE_OK)
    {
        registry_destroy(registry);
        config_free(&cfg);
        return 1;
    }

    if (reload_plugin)
    {
        if (registry_reload_plugin(registry, game_name) != 0)
        {
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }
    }

    LoadedPlugin *selected = registry_find_plugin(registry, game_name);
    if (!selected) /* Should not happen if validate_game_name passed, but defensive check */
    {
        log_error("Game '%s' not found", game_name);
        registry_destroy(registry);
        config_free(&cfg);
        return 1;
    }

    if (database_gewinnzahlen_cmd)
    {
        HistoricalDrawSnapshot snapshot;
        int db_rc = historical_db_sync_latest(selected->name, NULL, &snapshot);
        if (db_rc < 0)
        {
            fprintf(stderr,
                    "Error: database sync failed for '%s' (code %d). Check source config and "
                    "network.\n",
                    selected->name, db_rc);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        if (db_rc == HISTORICAL_DB_SYNC_UNCHANGED)
            printf("Local historical database already up to date for '%s' (draw_date=%s).\n",
                   selected->name, snapshot.draw_date);
        else
            printf("Historical database updated for '%s' (latest draw_date=%s).\n", selected->name,
                   snapshot.draw_date);

        registry_destroy(registry);
        config_free(&cfg);
        return 0;
    }

    if (analytics_mode_count > 0)
    {
        HistoricalDraw *historical_draws = calloc(ANALYTICS_MAX_DRAWS, sizeof(HistoricalDraw));
        HistoricalDraw *filtered = calloc(ANALYTICS_MAX_DRAWS, sizeof(HistoricalDraw));
        AnalyticsDrawRecord *dq_records = calloc(ANALYTICS_MAX_DRAWS, sizeof(AnalyticsDrawRecord));
        int draw_count = 0;
        int filtered_count = 0;

        if (!historical_draws || !filtered || !dq_records)
        {
            fprintf(stderr, "Error: Out of memory while preparing analytics buffers.\n");
            free(historical_draws);
            free(filtered);
            free(dq_records);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        int load_rc = VALIDATE_OK;
        if (historical_csv_from_cli)
        {
            load_rc =
                analytics_load_historical_csv(historical_csv, historical_draws, ANALYTICS_MAX_DRAWS,
                                              &draw_count, &selected->info);
        }
        else
        {
            load_rc = analytics_load_historical_db_snapshot(selected->name, NULL, historical_draws,
                                                            ANALYTICS_MAX_DRAWS, &draw_count,
                                                            &selected->info);
        }

        if (load_rc != VALIDATE_OK)
        {
            free(historical_draws);
            free(filtered);
            free(dq_records);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        if (analytics_filter_period(historical_draws, draw_count, period_from, period_to, filtered,
                                    &filtered_count) != VALIDATE_OK)
        {
            free(historical_draws);
            free(filtered);
            free(dq_records);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        for (int i = 0; i < filtered_count; i++)
        {
            dq_records[i].draw_date = filtered[i].draw_date;
            dq_records[i].main_count = filtered[i].result.main_count;
            dq_records[i].extra_count = filtered[i].result.extra_count;
            memset(dq_records[i].main_numbers, 0, sizeof(dq_records[i].main_numbers));
            memset(dq_records[i].extra_numbers, 0, sizeof(dq_records[i].extra_numbers));

            for (int j = 0; j < filtered[i].result.main_count && j < ANALYTICS_MAX_MAIN_NUMBERS;
                 j++)
                dq_records[i].main_numbers[j] = filtered[i].result.main_numbers[j];
            for (int j = 0; j < filtered[i].result.extra_count && j < ANALYTICS_MAX_EXTRA_NUMBERS;
                 j++)
                dq_records[i].extra_numbers[j] = filtered[i].result.extra_numbers[j];
        }

        AnalyticsDataQualityReport dq_report;
        if (analytics_data_quality_evaluate(
                dq_records, filtered_count, period_from, period_to, selected->info.main_count,
                selected->info.main_min, selected->info.main_max, selected->info.extra_count,
                selected->info.extra_min, selected->info.extra_max, &dq_report) != VALIDATE_OK)
        {
            free(historical_draws);
            free(filtered);
            free(dq_records);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        char dq_cli[256];
        analytics_data_quality_format_cli(&dq_report, dq_cli, sizeof(dq_cli));
        printf("%s\n", dq_cli);

        if (analytics_data_quality_has_severe_issues(&dq_report))
        {
            fprintf(stderr, "Error: Severe data integrity issues detected in selected period.\n");
            free(historical_draws);
            free(filtered);
            free(dq_records);
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        if (analytics_frequency)
        {
            FrequencyReport report;
            if (analytics_compute_frequency(filtered, filtered_count, selected->info.main_min,
                                            selected->info.main_max, &report) != VALIDATE_OK)
            {
                free(historical_draws);
                free(filtered);
                free(dq_records);
                registry_destroy(registry);
                config_free(&cfg);
                return 1;
            }

            snprintf(report.from_date, sizeof(report.from_date), "%s", period_from);
            report.from_date[sizeof(report.from_date) - 1] = '\0';
            snprintf(report.to_date, sizeof(report.to_date), "%s", period_to);
            report.to_date[sizeof(report.to_date) - 1] = '\0';

            if (gui && strcmp(gui_mode, "3D") == 0)
            {
                printf("[GUI 3D] OpenGL frequency visualization\n");
                if (gui_render_frequency_3d(selected->name, &report, dark_mode) != 0)
                    analytics_print_frequency_gui_3d_matlab(&report);
            }
            else if (gui)
            {
                printf("[GUI 2D] SDL frequency visualization\n");
                if (gui_render_frequency_2d(selected->name, &report, dark_mode) != 0)
                    analytics_print_frequency_gui_2d(&report);
            }
            else if (strcmp(analytics_format, "json") == 0)
                analytics_print_frequency_json(&report);
            else if (strcmp(analytics_format, "csv") == 0)
                analytics_print_frequency_csv(&report);
            else
                analytics_print_frequency_table(&report);

            if (analytics_explain)
            {
                if (strcmp(analytics_format, "json") == 0)
                {
                    printf("{\"explain\":{\"mode\":\"frequency\",\"formula\":\"count(number)/"
                           "draws\",\"period\":\"inclusive\"}}\n");
                }
                else
                {
                    printf("Explain: frequency percentage = count(number) / draws in inclusive "
                           "period [%s, %s].\n",
                           period_from, period_to);
                }
            }
        }
        else if (analytics_barometer)
        {
            BarometerReport report;
            if (analytics_compute_barometer(filtered, filtered_count, selected->info.main_min,
                                            selected->info.main_max, selected->info.main_count,
                                            &report) != VALIDATE_OK)
            {
                free(historical_draws);
                free(filtered);
                free(dq_records);
                registry_destroy(registry);
                config_free(&cfg);
                return 1;
            }

            snprintf(report.from_date, sizeof(report.from_date), "%s", period_from);
            report.from_date[sizeof(report.from_date) - 1] = '\0';
            snprintf(report.to_date, sizeof(report.to_date), "%s", period_to);
            report.to_date[sizeof(report.to_date) - 1] = '\0';

            if (gui && strcmp(gui_mode, "3D") == 0)
            {
                printf("[GUI 3D] OpenGL barometer visualization\n");
                if (gui_render_barometer_3d(selected->name, &report, dark_mode) != 0)
                    analytics_print_barometer_gui_3d_matlab(&report);
            }
            else if (gui)
            {
                printf("[GUI 2D] SDL barometer visualization\n");
                if (gui_render_barometer_2d(selected->name, &report, dark_mode) != 0)
                    analytics_print_barometer_gui_2d(&report);
            }
            else if (strcmp(analytics_format, "json") == 0)
                analytics_print_barometer_json(&report);
            else if (strcmp(analytics_format, "csv") == 0)
                analytics_print_barometer_csv(&report);
            else
                analytics_print_barometer_table(&report);

            if (analytics_explain)
            {
                if (strcmp(analytics_format, "json") == 0)
                {
                    printf("{\"explain\":{\"mode\":\"barometer\",\"formula\":\"observed_gap/"
                           "expected_interval\",\"expected_interval\":\"population/"
                           "picks_per_draw\"}}\n");
                }
                else
                {
                    printf("Explain: barometer factor = observed_gap / expected_interval, where "
                           "expected_interval = population / picks_per_draw.\n");
                }
            }
        }
        else if (analytics_hot_cold)
        {
            HotColdReport report;
            if (analytics_compute_hot_cold(filtered, filtered_count, selected->info.main_min,
                                           selected->info.main_max, analytics_top,
                                           &report) != VALIDATE_OK)
            {
                free(historical_draws);
                free(filtered);
                free(dq_records);
                registry_destroy(registry);
                config_free(&cfg);
                return 1;
            }

            snprintf(report.from_date, sizeof(report.from_date), "%s", period_from);
            report.from_date[sizeof(report.from_date) - 1] = '\0';
            snprintf(report.to_date, sizeof(report.to_date), "%s", period_to);
            report.to_date[sizeof(report.to_date) - 1] = '\0';

            if (gui && strcmp(gui_mode, "3D") == 0)
            {
                printf("[GUI 3D] OpenGL hot/cold visualization\n");
                if (gui_render_hot_cold_3d(selected->name, &report, dark_mode) != 0)
                    analytics_print_hot_cold_gui_3d_matlab(&report);
            }
            else if (gui)
            {
                printf("[GUI 2D] SDL hot/cold visualization\n");
                if (gui_render_hot_cold_2d(selected->name, &report, dark_mode) != 0)
                    analytics_print_hot_cold_gui_2d(&report);
            }
            else if (strcmp(analytics_format, "json") == 0)
                analytics_print_hot_cold_json(&report);
            else if (strcmp(analytics_format, "csv") == 0)
                analytics_print_hot_cold_csv(&report);
            else
                analytics_print_hot_cold_table(&report);

            if (analytics_explain)
            {
                if (strcmp(analytics_format, "json") == 0)
                {
                    printf("{\"explain\":{\"mode\":\"hot-cold\",\"rule\":\"hot=highest frequency, "
                           "cold=lowest frequency\",\"tie_break\":\"ascending number\"}}\n");
                }
                else
                {
                    printf("Explain: hot numbers are ranked by descending frequency; cold numbers "
                           "by ascending frequency; ties use ascending number.\n");
                }
            }
        }

        free(historical_draws);
        free(filtered);
        free(dq_records);
        registry_destroy(registry);
        config_free(&cfg);
        return 0;
    }

    /* If --validate-only flag is set, exit after successful validation */
    if (validate_only)
    {
        printf("Configuration is valid:\n");
        printf("  Game: %s\n", selected->name);
        printf("  Draws: %d\n", draws);
        if (export_format)
        {
            printf("  Export: %s -> %s\n", export_format, export_filename);
        }
        if (animate)
        {
            printf("  Mode: Animated CLI\n");
        }
        else if (gui)
        {
            printf("  Mode: GUI (%s)\n", gui_mode);
        }
        else
        {
            printf("  Mode: CLI\n");
        }
        if (reload_plugin)
        {
            printf("  Plugin reload: enabled\n");
        }
        if (use_seed)
        {
            printf("  Seed: 0x%016llx\n", (unsigned long long)seed_value);
        }
        registry_destroy(registry);
        config_free(&cfg);
        return 0;
    }

    /* ---------------------------------------------------------
       GUI MODE (always 1 draw)
       --------------------------------------------------------- */
    if (gui)
    {
        log_debug("Launching %s GUI mode", gui_mode);

        /* Resolve dark_mode: CLI override > config file > system auto-detect */
        int resolved_dark_mode = dark_mode;
        if (resolved_dark_mode == -1 && cfg.dark_mode)
        {
            if (strcasecmp(cfg.dark_mode, "on") == 0)
                resolved_dark_mode = 1;
            else if (strcasecmp(cfg.dark_mode, "off") == 0)
                resolved_dark_mode = 0;
            /* else stay at -1 for auto */
        }
        if (resolved_dark_mode == -1)
        {
            resolved_dark_mode = theme_detect_system_dark_mode();
        }

        if (strcmp(gui_mode, "3D") == 0)
        {
#if OPEN_LOTTO_ENABLE_OPENGL
            gui_run_opengl(selected->name, &selected->info, debug_overlay, resolved_dark_mode);
#else
            log_error("3D GUI backend is not available in this build. Use '--gui 2D'.");
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
#endif
        }
        else /* Default to 2D */
        {
            gui_run(selected->name, &selected->info, resolved_dark_mode);
        }

        registry_destroy(registry);
        config_free(&cfg);
        return 0;
    }

    /* ---------------------------------------------------------
       CLI MODE (single or multiple draws)
       --------------------------------------------------------- */
    log_debug("Running CLI mode with %d draw(s)", draws);

    if (export_format)
    {
        /* Export mode: collect all results then write to file */
        LotteryResult *results = (LotteryResult *)malloc(sizeof(LotteryResult) * draws);
        if (!results)
        {
            log_error("Failed to allocate memory for results");
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }

        for (int i = 0; i < draws; i++)
        {
            if (use_seed)
                combogen_set_forced_seed(derive_draw_seed(seed_value, i));
            else
                combogen_clear_forced_seed();

            selected->draw(&results[i], silent_callback);
        }

        combogen_clear_forced_seed();

        /* Export to file based on format */
        int export_result = 0;
        if (strcmp(export_format, "csv") == 0)
        {
            export_result =
                export_results_csv_file(export_filename, selected->name, results, draws);
        }
        else if (strcmp(export_format, "json") == 0)
        {
            export_result =
                export_results_json_file(export_filename, selected->name, results, draws);
        }

        free(results);

        if (export_result != 0)
        {
            log_error("Export failed");
            registry_destroy(registry);
            config_free(&cfg);
            return 1;
        }
    }
    else
    {
        /* Normal CLI mode: print as we go */
        for (int i = 0; i < draws; i++)
        {
            LotteryResult result;

            if (use_seed)
                combogen_set_forced_seed(derive_draw_seed(seed_value, i));
            else
                combogen_clear_forced_seed();

            selected->draw(&result, silent_callback);

            if (animate)
            {
                printf("Draw %d:\n", i + 1);
                animate_numbers(&selected->info, &result);
                printf("\n");
            }
            else
            {
                print_draw_result(selected->name, i + 1, &result);
            }
        }

        combogen_clear_forced_seed();
    }

    log_debug("Cleaning up resources");
    registry_destroy(registry);
    config_free(&cfg);
    return 0;
}
