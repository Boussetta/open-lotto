/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "config.h"
#include "export.h"
#include "gui_opengl.h"
#include "gui_sdl.h"
#include "log.h"
#include "plugin_loader.h"
#include "plugin_registry.h"
#include "random_seed.h"
#include "theme.h"
#include "validate.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

/* ---------------------------------------------------------
   SPINNER ANIMATION
   --------------------------------------------------------- */
static const char *SPINNER_FRAMES[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
static const int SPINNER_COUNT = 10;

/* ---------------------------------------------------------
   ANIMATION HELPERS
   --------------------------------------------------------- */

/* Print main numbers range from index 0 to count-1 */
static void print_main_numbers(const LotteryResult *result, int count)
{
    printf("Lottozahlen: ");
    for (int j = 0; j < count; j++)
        printf("%d ", result->main_numbers[j]);
}


/* Print main numbers + separator + extra numbers range */
static void print_main_and_extra(const LotteryResult *result, int main_count, int extra_count)
{
    printf("Lottozahlen: ");
    for (int j = 0; j < main_count; j++)
        printf("%d ", result->main_numbers[j]);
    printf("+ ");
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
        usleep(30000);
    }

    /* Reveal the actual number */
    printf("\r");
    print_prefix_fn(result, revealed_count);
    printf("%d ", number);
    fflush(stdout);
    usleep(150000);
}

/* ---------------------------------------------------------
   ANIMATE NUMBER REVEAL (same line, spinner-only)
   --------------------------------------------------------- */
static void animate_numbers(const LotteryInfo *info, const LotteryResult *result)
{
    printf("Drawing numbers…\n\n");

    printf("Lottozahlen: ");

    /* Animate main numbers */
    for (int i = 0; i < info->main_count; i++)
    {
        animate_number_reveal(result->main_numbers[i], print_main_numbers, result, i);
    }

    /* Animate extra numbers if present */
    if (info->extra_count > 0)
    {
        printf("+ ");

        for (int i = 0; i < info->extra_count; i++)
        {
            /* Custom print function for extra numbers */
            for (int f = 0; f < SPINNER_COUNT; f++)
            {
                printf("\r");
                print_main_and_extra(result, info->main_count, i);
                printf("%s ", SPINNER_FRAMES[f]);
                fflush(stdout);
                usleep(30000);
            }

            /* Reveal extra number */
            printf("\r");
            print_main_and_extra(result, info->main_count, i);
            printf("%d ", result->extra_numbers[i]);
            fflush(stdout);
            usleep(150000);
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
    printf("%s (Draw %d):\n", game_name, draw_num);
    printf("  Main: ");
    for (int j = 0; j < result->main_count; j++)
        printf("%d ", result->main_numbers[j]);

    if (result->extra_count > 0)
    {
        printf("+ ");
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
            "  %s --game NAME --validate-only\n"
            "  %s --list-games\n"
            "\n"
            "Modes:\n"
            "  --animate              Animated CLI draw display (spinner animation)\n"
            "  --gui [2D|3D]          Graphical mode (default: 2D SDL2, or 3D OpenGL)\n"
            "  --dark-mode <mode>     Dark mode theme: on, off, or auto (default: auto)\n"
            "  --debug-overlay        Show FPS/physics HUD in 3D GUI (requires --gui 3D)\n"
            "  --export FORMAT        Export results to file (csv or json)\n"
            "  --reload-plugin        Reload the selected plugin from disk before running\n"
            "  --validate-only        Validate configuration without running\n"
            "\n"
            "Output Options:\n"
            "  --output FILE     Destination file for --export (required with --export)\n"
            "  --draws N         Number of draws (default: 1)\n"
            "  --verbose LEVEL   Log level: ERROR, WARN, INFO, DEBUG (default: INFO)\n"
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
            "Examples:\n"
            "  %s --list-games\n"
            "  %s --game \"Lotto 6aus49\"\n"
            "  %s --game \"Lotto 6aus49\" --draws 10\n"
            "  %s --game \"Lotto 6aus49\" --animate\n"
            "  %s --game \"Lotto 6aus49\" --gui 3D\n"
            "  %s --game \"Lotto 6aus49\" --draws 100 --export csv --output results.csv\n"
            "  %s --game \"Lotto 6aus49\" --validate-only\n"
            "  %s --game \"Lotto 6aus49\" --verbose DEBUG\n"
            "\n"
            "Environment Variables:\n"
            "  OPEN_LOTTO_PLUGIN_PATH  Custom plugin directory path\n",
            prog, prog, prog, prog, prog, prog, prog, prog, prog, prog, prog, prog);
}

/* ---------------------------------------------------------
   MAIN
   --------------------------------------------------------- */
int main(int argc, char **argv)
{
    /* Load config file early so defaults are available */
    LoCalConfig cfg;
    config_load_lottorc(&cfg);

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
    int dark_mode = -1; /* -1 = auto (detect from system), 0 = off, 1 = on */
    int cli_log_level_set = 0;
    LogLevel log_level = LOG_INFO;
    const char *export_format = NULL;
    const char *export_filename = NULL;
    int validate_only = 0;
    int reload_plugin = 0;

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
            debug_overlay = 1;
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
        else
        {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            config_free(&cfg);
            return 1;
        }
    }

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
            gui_run_opengl(selected->name, &selected->info, debug_overlay, resolved_dark_mode);
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
            selected->draw(&results[i], silent_callback);
        }

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
    }

    log_debug("Cleaning up resources");
    registry_destroy(registry);
    config_free(&cfg);
    return 0;
}