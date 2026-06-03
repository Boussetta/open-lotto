#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
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
   ANIMATE NUMBER REVEAL (same line, spinner-only)
   --------------------------------------------------------- */
static void animate_numbers(const LotteryInfo *info, const LotteryResult *result)
{
    printf("Drawing numbers…\n\n");

    printf("Lottozahlen: ");

    /* MAIN NUMBERS */
    for (int i = 0; i < info->main_count; i++) {

        /* spinner at the correct position */
        for (int f = 0; f < SPINNER_COUNT; f++) {
            printf("\rLottozahlen: ");

            /* already revealed numbers */
            for (int j = 0; j < i; j++)
                printf("%d ", result->main_numbers[j]);

            /* spinner in place of next number */
            printf("%s ", SPINNER_FRAMES[f]);

            fflush(stdout);
            usleep(30000);
        }

        /* reveal number */
        printf("\rLottozahlen: ");
        for (int j = 0; j < i; j++)
            printf("%d ", result->main_numbers[j]);

        printf("%d ", result->main_numbers[i]);
        fflush(stdout);
        usleep(150000);
    }

    /* EXTRA NUMBERS (Superzahl) */
    if (info->extra_count > 0) {

        printf("+ ");

        for (int i = 0; i < info->extra_count; i++) {

            for (int f = 0; f < SPINNER_COUNT; f++) {
                printf("\rLottozahlen: ");

                /* main numbers */
                for (int j = 0; j < info->main_count; j++)
                    printf("%d ", result->main_numbers[j]);

                printf("+ ");

                /* already revealed extras */
                for (int j = 0; j < i; j++)
                    printf("%d ", result->extra_numbers[j]);

                /* spinner */
                printf("%s ", SPINNER_FRAMES[f]);

                fflush(stdout);
                usleep(30000);
            }

            /* reveal extra number */
            printf("\rLottozahlen: ");
            for (int j = 0; j < info->main_count; j++)
                printf("%d ", result->main_numbers[j]);

            printf("+ ");

            for (int j = 0; j < i; j++)
                printf("%d ", result->extra_numbers[j]);

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
   Usage
   --------------------------------------------------------- */
static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s --game NAME [--draws N] [--animate] [--gui]\n"
        "  %s --list-games\n"
        "\n"
        "Examples:\n"
        "  %s --game \"Lotto 6aus49\"\n"
        "  %s --game \"Lotto 6aus49\" --draws 10\n"
        "  %s --game \"Lotto 6aus49\" --animate\n"
        "  %s --game \"EuroJackpot\" --gui\n"
        "\n"
        "Environment Variables:\n"
        "  OPEN_LOTTO_PLUGIN_PATH  Custom plugin directory path\n",
        prog, prog, prog, prog, prog, prog
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
        else if (strcmp(argv[i], "--draws") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--draws requires a number.\n");
                return 1;
            }
            draws = atoi(argv[++i]);
            if (draws < 1) draws = 1;
        }
    }

    if (animate && gui) {
        fprintf(stderr, "Cannot use --animate and --gui together.\n");
        return 1;
    }

    /* ---------------------------------------------------------
       Discover and load plugins
       --------------------------------------------------------- */
    PluginRegistry *registry = registry_create();
    if (!registry) {
        fprintf(stderr, "Failed to create plugin registry\n");
        return 1;
    }

    registry_discover_plugins(registry);

    LoadedPlugin *selected = registry_find_plugin(registry, game_name);
    if (!selected) {
        fprintf(stderr, "Game '%s' not found.\n", game_name);
        fprintf(stderr, "Use --list-games to see available games.\n");
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