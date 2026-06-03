#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include "plugin_loader.h"
#include "lottery_plugin.h"

LoadedPlugin *load_plugin(const char *path)
{
    void *handle = dlopen(path, RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "Failed to load plugin %s: %s\n", path, dlerror());
        return NULL;
    }

    const LotteryInfo* (*get_info)(void) =
        (const LotteryInfo* (*)(void)) dlsym(handle, "plugin_get_info");
    const char* (*get_name)(void) =
        (const char* (*)(void)) dlsym(handle, "plugin_get_name");
    void (*draw_fn)(LotteryResult *, draw_event_callback) =
        (void (*)(LotteryResult *, draw_event_callback)) dlsym(handle, "plugin_draw");

    if (!get_info || !get_name || !draw_fn) {
        fprintf(stderr, "Plugin %s missing required symbols\n", path);
        dlclose(handle);
        return NULL;
    }

    LoadedPlugin *p = malloc(sizeof(LoadedPlugin));
    if (!p) {
        fprintf(stderr, "Out of memory loading plugin %s\n", path);
        dlclose(handle);
        return NULL;
    }

    p->info = *get_info();
    strncpy(p->name, get_name(), sizeof(p->name) - 1);
    p->name[sizeof(p->name) - 1] = '\0';
    p->draw = draw_fn;
    p->handle = handle;

    printf("Loaded plugin: %s\n", p->name);
    return p;
}

void unload_plugin(LoadedPlugin *plugin)
{
    if (!plugin)
        return;

    if (plugin->handle)
        dlclose(plugin->handle);

    free(plugin);
}
