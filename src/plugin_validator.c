/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "combogen.h"
#include "log.h"
#include "plugin_loader.h"

#include <stdio.h>
#include <stdlib.h>

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s PLUGIN.so [PLUGIN.so ...]\n", prog);
}

static int validate_info(const char *path, const LotteryInfo *info)
{
    if (info->main_count <= 0 || info->main_count > MAX_MAIN_NUMBERS)
    {
        fprintf(stderr, "%s: invalid main_count %d\n", path, info->main_count);
        return -1;
    }

    if (info->main_min <= 0 || info->main_max < info->main_min)
    {
        fprintf(stderr, "%s: invalid main range [%d, %d]\n", path, info->main_min, info->main_max);
        return -1;
    }

    if ((info->main_max - info->main_min + 1) < info->main_count)
    {
        fprintf(stderr, "%s: main range is too small for %d picks\n", path, info->main_count);
        return -1;
    }

    if (info->extra_count < 0 || info->extra_count > MAX_EXTRA_NUMBERS)
    {
        fprintf(stderr, "%s: invalid extra_count %d\n", path, info->extra_count);
        return -1;
    }

    if (info->extra_count == 0)
        return 0;

    if (info->extra_min < 0 || info->extra_max < info->extra_min)
    {
        fprintf(stderr, "%s: invalid extra range [%d, %d]\n", path, info->extra_min,
                info->extra_max);
        return -1;
    }

    if ((info->extra_max - info->extra_min + 1) < info->extra_count)
    {
        fprintf(stderr, "%s: extra range is too small for %d picks\n", path, info->extra_count);
        return -1;
    }

    return 0;
}

static int validate_unique_range(const char *path, const int *values, int count, int min, int max,
                                 const char *label)
{
    for (int i = 0; i < count; i++)
    {
        if (values[i] < min || values[i] > max)
        {
            fprintf(stderr, "%s: %s value %d is outside [%d, %d]\n", path, label, values[i], min,
                    max);
            return -1;
        }

        for (int j = i + 1; j < count; j++)
        {
            if (values[i] == values[j])
            {
                fprintf(stderr, "%s: %s contains duplicate value %d\n", path, label, values[i]);
                return -1;
            }
        }
    }

    return 0;
}

static int validate_draw(const char *path, LoadedPlugin *plugin)
{
    LotteryResult result = {0};
    plugin->draw(&result, NULL);

    if (result.main_count != plugin->info.main_count)
    {
        fprintf(stderr, "%s: draw returned main_count %d, expected %d\n", path, result.main_count,
                plugin->info.main_count);
        return -1;
    }

    if (result.extra_count != plugin->info.extra_count)
    {
        fprintf(stderr, "%s: draw returned extra_count %d, expected %d\n", path, result.extra_count,
                plugin->info.extra_count);
        return -1;
    }

    if (validate_unique_range(path, result.main_numbers, result.main_count, plugin->info.main_min,
                              plugin->info.main_max, "main draw") != 0)
    {
        return -1;
    }

    if (validate_unique_range(path, result.extra_numbers, result.extra_count,
                              plugin->info.extra_min, plugin->info.extra_max, "extra draw") != 0)
    {
        return -1;
    }

    return 0;
}

static int validate_plugin_path(const char *path)
{
    LoadedPlugin *plugin = load_plugin(path);
    if (!plugin)
    {
        fprintf(stderr, "%s: failed to load plugin\n", path);
        return -1;
    }

    int status = 0;
    if (validate_info(path, &plugin->info) != 0)
        status = -1;

    if (status == 0 && validate_draw(path, plugin) != 0)
        status = -1;

    if (status == 0)
    {
        printf("VALID %s\n", plugin->name);
        printf("  Path: %s\n", plugin->path);
        printf("  Main: %d from %d-%d\n", plugin->info.main_count, plugin->info.main_min,
               plugin->info.main_max);
        if (plugin->info.extra_count > 0)
        {
            printf("  Extra: %d from %d-%d\n", plugin->info.extra_count, plugin->info.extra_min,
                   plugin->info.extra_max);
        }
        else
        {
            printf("  Extra: none\n");
        }
    }

    unload_plugin(plugin);
    return status;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        print_usage(argv[0]);
        return 1;
    }

    log_set_level(LOG_ERROR);

    int exit_code = 0;
    for (int i = 1; i < argc; i++)
    {
        if (validate_plugin_path(argv[i]) != 0)
            exit_code = 1;
    }

    return exit_code;
}