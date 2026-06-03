#ifndef PLUGIN_REGISTRY_H
#define PLUGIN_REGISTRY_H

#include "plugin_loader.h"

/* Plugin registry for dynamic discovery and management */

typedef struct {
    LoadedPlugin **plugins;
    int count;
    int capacity;
} PluginRegistry;

/* Initialize and scan plugin directories */
PluginRegistry* registry_create(void);

/* Discover all plugins in standard locations */
void registry_discover_plugins(PluginRegistry *registry);

/* Find a plugin by game name (case-insensitive) */
LoadedPlugin* registry_find_plugin(PluginRegistry *registry, const char *game_name);

/* List all available games */
void registry_list_games(PluginRegistry *registry);

/* Free registry and all plugins */
void registry_destroy(PluginRegistry *registry);

#endif /* PLUGIN_REGISTRY_H */
