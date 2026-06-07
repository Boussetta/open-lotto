#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "log.h"
#include "plugin_registry.h"

#define INITIAL_CAPACITY 10
#define MAX_PATH 512

static int is_plugin_file(const char *filename)
{
    return strstr(filename, ".so") != NULL;
}

static int registry_find_plugin_index(PluginRegistry *registry, const char *game_name)
{
    if (!registry || !game_name)
        return -1;

    for (int i = 0; i < registry->count; i++)
    {
        if (!registry->plugins[i])
            continue;

        if (strcasecmp(registry->plugins[i]->name, game_name) == 0)
            return i;
    }

    return -1;
}

static void registry_add_plugin(PluginRegistry *registry, LoadedPlugin *plugin)
{
    if (!registry || !plugin)
        return;

    /* Validate plugin name is not empty */
    if (plugin->name[0] == '\0')
    {
        log_error("Cannot add plugin with empty name");
        unload_plugin(plugin);
        return;
    }

    /* Resize if needed */
    if (registry->count >= registry->capacity)
    {
        registry->capacity *= 2;
        LoadedPlugin **new_plugins = (LoadedPlugin **)realloc(
            registry->plugins, registry->capacity * sizeof(LoadedPlugin *));
        if (!new_plugins)
        {
            log_error("Failed to resize plugin registry");
            return;
        }
        registry->plugins = new_plugins;
    }

    registry->plugins[registry->count++] = plugin;
}

static void scan_plugin_directory(PluginRegistry *registry, const char *dirpath)
{
    if (!dirpath || !registry)
    {
        log_warn("Invalid directory path or registry");
        return;
    }

    DIR *dir = opendir(dirpath);
    if (!dir)
    {
        log_debug("Plugin directory not found: %s", dirpath);
        return;
    }

    log_info("Scanning plugin directory: %s", dirpath);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (!is_plugin_file(entry->d_name))
            continue;

        char plugin_path[MAX_PATH];
        int len = snprintf(plugin_path, sizeof(plugin_path), "%s/%s", dirpath, entry->d_name);
        if (len < 0 || len >= (int)sizeof(plugin_path))
        {
            log_warn("Plugin path too long, skipping: %s/%s", dirpath, entry->d_name);
            continue;
        }

        log_debug("Loading plugin: %s", plugin_path);
        LoadedPlugin *plugin = load_plugin(plugin_path);
        if (plugin)
        {
            registry_add_plugin(registry, plugin);
        }
    }

    closedir(dir);
}

PluginRegistry *registry_create(void)
{
    PluginRegistry *registry = malloc(sizeof(PluginRegistry));
    if (!registry)
    {
        log_error("Failed to allocate plugin registry");
        return NULL;
    }

    registry->plugins = malloc(INITIAL_CAPACITY * sizeof(LoadedPlugin *));
    if (!registry->plugins)
    {
        log_error("Failed to allocate plugin array");
        free(registry);
        return NULL;
    }

    registry->count = 0;
    registry->capacity = INITIAL_CAPACITY;

    return registry;
}

void registry_discover_plugins(PluginRegistry *registry)
{
    if (!registry)
    {
        log_error("Invalid plugin registry");
        return;
    }

    log_info("Discovering plugins...");

    /* Check environment variable first */
    const char *env_path = getenv("OPEN_LOTTO_PLUGIN_PATH");
    if (env_path)
    {
        log_info("Using OPEN_LOTTO_PLUGIN_PATH: %s", env_path);
        scan_plugin_directory(registry, env_path);
    }

    /* Try default locations */
    const char *default_paths[] = {"./plugins", "./build/plugins", "/usr/lib/open-lotto/plugins",
                                   "/usr/local/lib/open-lotto/plugins", NULL};

    for (int i = 0; default_paths[i] != NULL; i++)
    {
        /* Skip if same as env_path (already scanned) */
        if (env_path && strcmp(env_path, default_paths[i]) == 0)
            continue;

        scan_plugin_directory(registry, default_paths[i]);
    }

    if (registry->count == 0)
    {
        log_warn("No plugins found in any search path");
    }
    else
    {
        log_info("Successfully discovered %d plugin(s)", registry->count);
    }
}

LoadedPlugin *registry_find_plugin(PluginRegistry *registry, const char *game_name)
{
    if (!registry)
    {
        log_error("Invalid registry");
        return NULL;
    }

    if (!game_name || game_name[0] == '\0')
    {
        log_error("Invalid game name (NULL or empty)");
        return NULL;
    }

    /* Validate game name length */
    size_t game_name_len = strlen(game_name);
    if (game_name_len > 256)
    {
        log_error("Game name too long: %zu bytes (max 256)", game_name_len);
        return NULL;
    }

    int index = registry_find_plugin_index(registry, game_name);
    if (index >= 0)
    {
        log_info("Found plugin: %s", game_name);
        return registry->plugins[index];
    }

    log_error("Plugin not found: %s", game_name);
    return NULL;
}

int registry_reload_plugin(PluginRegistry *registry, const char *game_name)
{
    if (!registry)
    {
        log_error("Invalid registry");
        return -1;
    }

    if (!game_name || game_name[0] == '\0')
    {
        log_error("Invalid game name for reload");
        return -1;
    }

    int index = registry_find_plugin_index(registry, game_name);
    if (index < 0)
    {
        log_error("Cannot reload unknown plugin: %s", game_name);
        return -1;
    }

    LoadedPlugin *current = registry->plugins[index];
    if (!current || current->path[0] == '\0')
    {
        log_error("Plugin %s has no reloadable source path", game_name);
        return -1;
    }

    LoadedPlugin *replacement = load_plugin(current->path);
    if (!replacement)
    {
        log_error("Failed to reload plugin from %s", current->path);
        return -1;
    }

    if (strcasecmp(replacement->name, current->name) != 0)
    {
        log_error("Reloaded plugin name mismatch: expected %s, got %s", current->name,
                  replacement->name);
        unload_plugin(replacement);
        return -1;
    }

    registry->plugins[index] = replacement;
    unload_plugin(current);
    log_info("Reloaded plugin: %s", replacement->name);
    return 0;
}

void registry_list_games(PluginRegistry *registry)
{
    if (!registry)
    {
        log_error("Invalid plugin registry");
        return;
    }

    if (registry->count == 0)
    {
        fprintf(stdout, "No plugins available\n");
        return;
    }

    fprintf(stdout, "Available games:\n");
    for (int i = 0; i < registry->count; i++)
    {
        fprintf(stdout, "  - %s\n", registry->plugins[i]->name);
    }
}

void registry_destroy(PluginRegistry *registry)
{
    if (!registry)
        return;

    for (int i = 0; i < registry->count; i++)
    {
        if (registry->plugins[i])
        {
            unload_plugin(registry->plugins[i]);
        }
    }

    free(registry->plugins);
    free(registry);
    log_info("Plugin registry destroyed");
}
