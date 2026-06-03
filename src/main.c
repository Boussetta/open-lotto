#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include "plugin_loader.h"
#include "plugin_registry.h"
#include "gui_sdl.h"
#include "random_seed.h"
#include "log.h"

/* ---------------------------------------------------------
   SPINNER ANIMATION
   --------------------------------------------------------- */
static const char *SPINNER_FRAMES[] = {
    "⠋", "⠙", "⠹", "⠸",
    "⠼", "⠴", "⠦", "⠧",
    "⠇", "⠏"
};
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
static void animate_number_reveal(
    int number,
    void (*print_prefix_fn)(const LotteryResult*, int),
    const LotteryResult *result,
    int revealed_count
)
{
    /* Spinner animation frames */
    for (int f = 0; f < SPINNER_COUNT; f++) {
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
    for (int i = 0; i < info->main_count; i++) {
        animate_number_reveal(
            result->main_numbers[i],
            print_main_numbers,
            result,
            i
        );
    }

    /* Animate extra numbers if present */
    if (info->extra_count > 0) {
        printf("+ ");

        for (int i = 0; i < info->extra_count; i++) {
            /* Custom print function for extra numbers */
            for (int f = 0; f < SPINNER_COUNT; f++) {
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
        "  %s --game NAME [--draws N] [--animate] [--gui] [--verbose LEVEL]\n"
        "  %s --list-games\n"
        "\n"
        "Log Levels (for --verbose):\n"
        "  ERROR, WARN, INFO (default), DEBUG\n"
        "\n"
        "Examples:\n"
        "  %s --game \"Lotto 6aus49\"\n"
        "  %s --game \"Lotto 6aus49\" --draws 10\n"
        "  %s --game \"Lotto 6aus49\" --animate\n"
        "  %s --game \"EuroJackpot\" --gui\n"
        "  %s --game \"Lotto 6aus49\" --verbose DEBUG\n"
        "\n"
        "Environment Variables:\n"
        "  OPEN_LOTTO_PLUGIN_PATH  Custom plugin directory path\n",
        prog, prog, prog, prog, prog, prog, prog
    );
}

/* ---------------------------------------------------------
   MAIN
   --------------------------------------------------------- */
int main(int argc, char **argv)
{
    /* Handle --list-games command */
    if (argc >= 2 && strcmp(argv[1], "--list-games") == 0) {
        PluginRegistry *registry = registry_create();
        if (!registry) {
            fprintf(stderr, "Failed to create plugin registry\n");
            return 1;
        }
        registry_discover_plugins(registry);
        registry_list_games(registry);
        registry_destroy(registry);
        return 0;
    }

    if (argc < 3 || strcmp(argv[1], "--game") != 0) {
        print_usage(argv[0]);
        return 1;
    }

    const char *game_name = argv[2];
    int animate = 0;
    int gui = 0;
    int draws = 1;
    LogLevel log_level = LOG_INFO;

    /* ---------------------------------------------------------
       Parse arguments
       --------------------------------------------------------- */
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--animate") == 0) {
            animate = 1;
        }
        else if (strcmp(argv[i], "--gui") == 0) {
            gui = 1;
        }
        else if (strcmp(argv[i], "--verbose") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--verbose requires a log level (ERROR, WARN, INFO, DEBUG).\n");
                return 1;
            }
            log_level = parse_log_level(argv[++i]);
        }
        else if (strcmp(argv[i], "--draws") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--draws requires a number.\n");
                return 1;
            }
            
            char *endptr;
            errno = 0;
            long val = strtol(argv[++i], &endptr, 10);
            
            /* Validate: check for errors, non-numeric characters, and range */
            if (errno == ERANGE || *endptr != '\0' || val < 1 || val > INT_MAX) {
                fprintf(stderr, "Error: Invalid number of draws '%s'\n", argv[i]);
                fprintf(stderr, "Must be a positive integer between 1 and %d\n", INT_MAX);
                return 1;
            }
            
            draws = (int)val;
        }
    }

    if (animate && gui) {
        fprintf(stderr, "Cannot use --animate and --gui together.\n");
        return 1;
    }

    /* Set the log level early so all subsequent operations are logged */
    log_set_level(log_level);
    log_debug("Starting open-lotto with log level set to DEBUG");

    /* ---------------------------------------------------------
       Discover and load plugins
       --------------------------------------------------------- */
    PluginRegistry *registry = registry_create();
    if (!registry) {
        log_error("Failed to create plugin registry");
        return 1;
    }

    registry_discover_plugins(registry);

    LoadedPlugin *selected = registry_find_plugin(registry, game_name);
    if (!selected) {
        log_error("Game '%s' not found", game_name);
        log_info("Use --list-games to see available games");
        registry_destroy(registry);
        return 1;
    }

    /* ---------------------------------------------------------
       GUI MODE (always 1 draw)
       --------------------------------------------------------- */
    if (gui) {
        gui_run(selected->name, &selected->info);
        registry_destroy(registry);
        return 0;
    }

    /* ---------------------------------------------------------
       CLI MODE (single or multiple draws)
       --------------------------------------------------------- */
    for (int i = 0; i < draws; i++) {

        LotteryResult result;

        /* generate silently */
        selected->draw(&result, silent_callback);

        /* animated reveal */
        if (animate) {
            printf("Draw %d:\n", i + 1);
            animate_numbers(&selected->info, &result);
            printf("\n");
            continue;
        }

        /* normal CLI output */
        printf("%s (Draw %d):\n", selected->name, i + 1);
        printf("  Main: ");
        for (int j = 0; j < result.main_count; j++)
            printf("%d ", result.main_numbers[j]);

        if (result.extra_count > 0) {
            printf("+ ");
            for (int j = 0; j < result.extra_count; j++)
                printf("%d ", result.extra_numbers[j]);
        }

        printf("\n\n");
    }

    registry_destroy(registry);
    return 0;
}