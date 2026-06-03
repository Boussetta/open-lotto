#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>

#include "plugin_loader.h"
#include "lottery_plugin.h"
#include "log.h"

LoadedPlugin *load_plugin(const char *path)
{
    log_debug("Attempting to load plugin from: %s", path);
    
    void *handle = dlopen(path, RTLD_NOW);
    if (!handle) {
        log_error("Failed to load plugin %s: %s", path, dlerror());
        return NULL;
    }

    /* Use intermediate void* to avoid pedantic pointer-to-function conversion warning */
    const LotteryInfo* (*get_info)(void) =
        (const LotteryInfo* (*)(void)) (uintptr_t) dlsym(handle, "plugin_get_info");
    const char* (*get_name)(void) =
        (const char* (*)(void)) (uintptr_t) dlsym(handle, "plugin_get_name");
    void (*draw_fn)(LotteryResult *, draw_event_callback) =
        (void (*)(LotteryResult *, draw_event_callback)) (uintptr_t) dlsym(handle, "plugin_draw");

    if (!get_info || !get_name || !draw_fn) {
        log_error("Plugin %s missing required symbols (get_info, get_name, or draw)", path);
        dlclose(handle);
        return NULL;
    }

    LoadedPlugin *p = malloc(sizeof(LoadedPlugin));
    if (!p) {
        log_error("Out of memory loading plugin %s", path);
        dlclose(handle);
        return NULL;
    }

    p->info = *get_info();
    strncpy(p->name, get_name(), sizeof(p->name) - 1);
    p->name[sizeof(p->name) - 1] = '\0';
    p->draw = draw_fn;
    p->handle = handle;

    log_info("Successfully loaded plugin: %s", p->name);
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
