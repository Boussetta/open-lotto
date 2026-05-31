#ifndef PLUGIN_LOADER_H
#define PLUGIN_LOADER_H

#include <stddef.h>
#include "lottery_plugin.h"
#include "combogen.h"

typedef struct {
    char name[64];
    void *handle;
    const LotteryInfo *info;
    void (*generate)(LotteryResult *out, draw_event_callback cb);
} LoadedPlugin;

typedef struct {
    LoadedPlugin *plugins;
    int count;
} PluginRegistry;

PluginRegistry plugin_loader_init(const char *directory);
void plugin_loader_list(const PluginRegistry *reg);
const LoadedPlugin* plugin_loader_find(const PluginRegistry *reg, const char *name);
void plugin_loader_free(PluginRegistry *reg);

#endif
