#include "plugin_loader.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <dlfcn.h>

/* ------------------------------------------------------------
   Normalize plugin name for CLI usage:
   - lowercase
   - remove spaces
   - remove special chars
   - keep letters and digits only
   ------------------------------------------------------------ */
static void normalize_name(const char *raw, char *out, size_t out_size) {
    size_t idx = 0;

    for (size_t i = 0; raw[i] != '\0' && idx < out_size - 1; i++) {
        char c = raw[i];

        if (c >= 'A' && c <= 'Z') {
            out[idx++] = c + 32;  // lowercase
        }
        else if ((c >= 'a' && c <= 'z') ||
                 (c >= '0' && c <= '9')) {
            out[idx++] = c;
        }
        else if (c == ' ') {
            out[idx++] = '_';
        }
        // ignore everything else
    }

    out[idx] = '\0';
}

/* ------------------------------------------------------------
   Load plugins from directory
   ------------------------------------------------------------ */
PluginRegistry plugin_loader_init(const char *directory) {
    PluginRegistry reg = {0};

    DIR *dir = opendir(directory);
    if (!dir) {
        log_error("Cannot open plugin directory: %s", directory);
        return reg;
    }

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {

        // Only load .so files
        if (!strstr(entry->d_name, ".so"))
            continue;

        char path[256];
        snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);

        log_info("Loading plugin: %s", path);

        void *handle = dlopen(path, RTLD_NOW);
        if (!handle) {
            log_error("dlopen failed: %s", dlerror());
            continue;
        }

        // Load required symbols
        const LotteryInfo* (*get_info)(void) =
            (const LotteryInfo* (*)(void)) dlsym(handle, "lottery_get_info");

        void (*generate)(LotteryResult*, draw_event_callback) =
            (void (*)(LotteryResult*, draw_event_callback))
            dlsym(handle, "lottery_generate");

        if (!get_info || !generate) {
            log_error("Plugin %s missing required symbols", entry->d_name);
            dlclose(handle);
            continue;
        }

        // Allocate plugin entry
        reg.plugins = realloc(reg.plugins, sizeof(LoadedPlugin) * (reg.count + 1));
        LoadedPlugin *p = &reg.plugins[reg.count];
        memset(p, 0, sizeof(*p));

        // Fill plugin data
        p->handle = handle;
        p->info = get_info();
        p->generate = generate;

        // Normalize plugin name for CLI usage
        normalize_name(p->info->name, p->name, sizeof(p->name));

        reg.count++;
    }

    closedir(dir);
    return reg;
}

/* ------------------------------------------------------------
   List available games
   ------------------------------------------------------------ */
void plugin_loader_list(const PluginRegistry *reg) {
    printf("Available games:\n");
    for (int i = 0; i < reg->count; i++) {
        printf("  %s (%s)\n", reg->plugins[i].name, reg->plugins[i].info->name);
    }
}

/* ------------------------------------------------------------
   Find plugin by normalized name
   ------------------------------------------------------------ */
const LoadedPlugin* plugin_loader_find(const PluginRegistry *reg, const char *name) {
    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->plugins[i].name, name) == 0)
            return &reg->plugins[i];
    }
    return NULL;
}

/* ------------------------------------------------------------
   Free all plugins
   ------------------------------------------------------------ */
void plugin_loader_free(PluginRegistry *reg) {
    for (int i = 0; i < reg->count; i++) {
        dlclose(reg->plugins[i].handle);
    }
    free(reg->plugins);
    reg->plugins = NULL;
    reg->count = 0;
}
