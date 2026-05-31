#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

#include "log.h"
#include "plugin_loader.h"
#include "combogen.h"

static void print_usage(void) {
    printf("Usage:\n");
    printf("  open-lotto --list-games\n");
    printf("  open-lotto --game <name> [--draws N] [--animate]\n");
}

/* Spinner frames */
static const char *SPINNER_FRAMES[] = {
    "⠋", "⠙", "⠹", "⠸",
    "⠼", "⠴", "⠦", "⠧",
    "⠇", "⠏"
};
static const int SPINNER_COUNT = 10;

static void animate_numbers(const LotteryInfo *info, const LotteryResult *result) {
    printf("Drawing numbers…\n\n");

    printf("Main: ");
    fflush(stdout);

    for (int i = 0; i < info->main_count; i++) {
        for (int f = 0; f < SPINNER_COUNT; f++) {
            printf("\rMain: ");
            for (int j = 0; j < i; j++) {
                printf("\033[32m%d\033[0m ", result->main_numbers[j]);
            }
            printf("%s ", SPINNER_FRAMES[f]);
            fflush(stdout);
            usleep(30000);
        }

        printf("\rMain: ");
        for (int j = 0; j < i; j++) {
            printf("\033[32m%d\033[0m ", result->main_numbers[j]);
        }
        printf("\033[32m%d\033[0m ", result->main_numbers[i]);
        fflush(stdout);
        usleep(150000);
    }

    printf("\n");

    if (info->extra_count > 0) {
        printf("Extra: ");
        fflush(stdout);

        for (int i = 0; i < info->extra_count; i++) {
            for (int f = 0; f < SPINNER_COUNT; f++) {
                printf("\rExtra: ");
                for (int j = 0; j < i; j++) {
                    printf("\033[33m%d\033[0m ", result->extra_numbers[j]);
                }
                printf("%s ", SPINNER_FRAMES[f]);
                fflush(stdout);
                usleep(30000);
            }

            printf("\rExtra: ");
            for (int j = 0; j < i; j++) {
                printf("\033[33m%d\033[0m ", result->extra_numbers[j]);
            }
            printf("\033[33m%d\033[0m ", result->extra_numbers[i]);
            fflush(stdout);
            usleep(150000);
        }

        printf("\n");
    }

    printf("\n");
}

static void event_handler(
    draw_event_type event,
    const int *pool, int pool_size,
    const int *out,  int out_size,
    uint64_t seed
) {
    switch (event) {
        case EVENT_RNG_INITIALIZED:
            log_info("RNG initialized with seed %llu",
                     (unsigned long long)seed);
            break;
        case EVENT_POOL_INITIALIZED:
            log_debug("Pool initialized (%d numbers)", pool_size);
            break;
        case EVENT_AFTER_SHUFFLE:
            log_debug("Pool shuffled");
            break;
        case EVENT_AFTER_PICK:
            log_debug("Picked %d numbers", out_size);
            break;
        case EVENT_DRAW_COMPLETE:
            log_info("Draw complete");
            break;
    }
}

int main(int argc, char **argv) {
    log_enable_file_output("open-lotto.log");
    log_set_level(LOG_INFO);

    PluginRegistry reg = plugin_loader_init("plugins");

    int animate = 0;
    int draws = 1;   // default

    /* Parse flags */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--animate") == 0) {
            animate = 1;
        } else if (strcmp(argv[i], "--draws") == 0 && i + 1 < argc) {
            draws = atoi(argv[++i]);
            if (draws < 1) draws = 1;
        }
    }

    if (argc == 2 && strcmp(argv[1], "--list-games") == 0) {
        plugin_loader_list(&reg);
        plugin_loader_free(&reg);
        return 0;
    }

    if (argc >= 3 && strcmp(argv[1], "--game") == 0) {
        const char *name = argv[2];
        const LoadedPlugin *p = plugin_loader_find(&reg, name);

        if (!p) {
            printf("Game '%s' not found.\n", name);
            plugin_loader_list(&reg);
            plugin_loader_free(&reg);
            return 1;
        }

        printf("%s:\n\n", p->info->name);

        for (int d = 1; d <= draws; d++) {
            LotteryResult result;
            p->generate(&result, event_handler);

            if (draws > 1) {
                printf("=== Draw %d/%d ===\n", d, draws);
            }

            if (animate) {
                animate_numbers(p->info, &result);
            } else {
                printf("Numbers: ");
                for (int i = 0; i < p->info->main_count; i++)
                    printf("%d ", result.main_numbers[i]);
                printf("\n");

                if (p->info->extra_count > 0) {
                    printf("Extra:   ");
                    for (int i = 0; i < p->info->extra_count; i++)
                        printf("%d ", result.extra_numbers[i]);
                    printf("\n");
                }

                printf("\n");
            }
        }

        plugin_loader_free(&reg);
        return 0;
    }

    print_usage();
    plugin_loader_free(&reg);
    return 1;
}
