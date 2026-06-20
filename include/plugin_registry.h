/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef PLUGIN_REGISTRY_H
#define PLUGIN_REGISTRY_H

#include "plugin_loader.h"

/**
 * @file plugin_registry.h
 * @brief Discovery and lifecycle management for loaded game plugins.
 */

typedef struct
{
    LoadedPlugin **plugins;
    int count;
    int capacity;
} PluginRegistry;

/** @brief Create an empty plugin registry container. */
PluginRegistry *registry_create(void);

/** @brief Discover plugins from standard search locations. */
void registry_discover_plugins(PluginRegistry *registry);

/** @brief Find plugin by game name (case-insensitive match). */
LoadedPlugin *registry_find_plugin(PluginRegistry *registry, const char *game_name);

/** @brief Reload an already discovered plugin in-place. */
int registry_reload_plugin(PluginRegistry *registry, const char *game_name);

/** @brief Print all discovered games/plugins. */
void registry_list_games(PluginRegistry *registry);

/** @brief Destroy registry and unload all loaded plugins. */
void registry_destroy(PluginRegistry *registry);

#endif /* PLUGIN_REGISTRY_H */
