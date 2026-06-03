#ifndef PLUGIN_LOADER_H
#define PLUGIN_LOADER_H

#include "lottery_plugin.h"

typedef struct {
    LotteryInfo info;   /* copy of rules */
    char name[64];
    void (*draw)(LotteryResult *, draw_event_callback);
    void *handle;
} LoadedPlugin;

LoadedPlugin *load_plugin(const char *path);
void unload_plugin(LoadedPlugin *plugin);

#endif /* PLUGIN_LOADER_H */
