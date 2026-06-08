/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef PLUGIN_LOADER_H
#define PLUGIN_LOADER_H

#include "lottery_plugin.h"

#define LOADED_PLUGIN_PATH_MAX 512

typedef struct
{
    LotteryInfo info; /* copy of rules */
    char name[64];
    char path[LOADED_PLUGIN_PATH_MAX];
    void (*draw)(LotteryResult *, draw_event_callback);
    void *handle;
} LoadedPlugin;

LoadedPlugin *load_plugin(const char *path);
void unload_plugin(LoadedPlugin *plugin);

#endif /* PLUGIN_LOADER_H */
