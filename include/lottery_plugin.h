/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef LOTTERY_PLUGIN_H
#define LOTTERY_PLUGIN_H

#include "combogen.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Every plugin must export these three symbols */
    const LotteryInfo *plugin_get_info(void);
    const char *plugin_get_name(void);
    void plugin_draw(LotteryResult *out, draw_event_callback cb);

#ifdef __cplusplus
}
#endif

#endif /* LOTTERY_PLUGIN_H */
