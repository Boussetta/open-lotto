/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef LOTTERY_PLUGIN_H
#define LOTTERY_PLUGIN_H

#include "combogen.h"

/**
 * @file lottery_plugin.h
 * @brief ABI contract required by dynamically loaded game plugins.
 */

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Return static game rules for this plugin. */
    const LotteryInfo *plugin_get_info(void);
    /** @brief Return display name of the plugin/game. */
    const char *plugin_get_name(void);
    /** @brief Generate one draw using plugin-specific rules. */
    void plugin_draw(LotteryResult *out, draw_event_callback cb);

#ifdef __cplusplus
}
#endif

#endif /* LOTTERY_PLUGIN_H */
