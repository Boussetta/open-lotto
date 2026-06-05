#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "lottery_plugin.h"
#include "plugin_loader.h"

LoadedPlugin *load_plugin(const char *path)
{
    /* Validate input path */
    if (!path || path[0] == '\0')
    {
        log_error("Invalid plugin path (NULL or empty)");
        return NULL;
    }

    log_debug("Attempting to load plugin from: %s", path);

    void *handle = dlopen(path, RTLD_NOW);
    if (!handle)
    {
        log_error("Failed to load plugin %s: %s", path, dlerror());
        return NULL;
    }

    /* Use intermediate void* to avoid pedantic pointer-to-function conversion warning */
    const LotteryInfo *(*get_info)(void) =
        (const LotteryInfo *(*)(void))(uintptr_t)dlsym(handle, "plugin_get_info");
    const char *(*get_name)(void) =
        (const char *(*)(void))(uintptr_t)dlsym(handle, "plugin_get_name");
    void (*draw_fn)(LotteryResult *, draw_event_callback) =
        (void (*)(LotteryResult *, draw_event_callback))(uintptr_t)dlsym(handle, "plugin_draw");

    /* Validate all function pointers were found */
    if (!get_info || !get_name || !draw_fn)
    {
        log_warn("Plugin %s missing required symbols (get_info, get_name, or draw) — skipping",
                 path);
        dlclose(handle);
        return NULL;
    }

    /* Call get_info() and validate return value */
    const LotteryInfo *info = get_info();
    if (!info)
    {
        log_error("Plugin %s get_info() returned NULL", path);
        dlclose(handle);
        return NULL;
    }

    /* Call get_name() and validate return value */
    const char *name = get_name();
    if (!name)
    {
        log_error("Plugin %s get_name() returned NULL", path);
        dlclose(handle);
        return NULL;
    }

    /* Validate name is not empty and not too long */
    if (name[0] == '\0')
    {
        log_error("Plugin %s returned empty name", path);
        dlclose(handle);
        return NULL;
    }

    size_t name_len = strlen(name);
    if (name_len >= sizeof(((LoadedPlugin *)0)->name))
    {
        log_error("Plugin %s name too long (%zu bytes, max %zu)", path, name_len,
                  sizeof(((LoadedPlugin *)0)->name) - 1);
        dlclose(handle);
        return NULL;
    }

    /* Allocate plugin structure */
    LoadedPlugin *p = malloc(sizeof(LoadedPlugin));
    if (!p)
    {
        log_error("Out of memory loading plugin %s", path);
        dlclose(handle);
        return NULL;
    }

    /* Safe copy of validated name */
    strncpy(p->name, name, sizeof(p->name) - 1);
    p->name[sizeof(p->name) - 1] = '\0';

    /* Store the validated info and function pointers */
    p->info = *info;
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
