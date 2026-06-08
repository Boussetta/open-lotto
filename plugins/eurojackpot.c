/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "combogen.h"
#include "lottery_plugin.h"

/* EuroJackpot:
 *  - 5 main numbers from 1..50
 *  - 2 Euro numbers from 1..12
 */

static const LotteryInfo INFO = {.main_count = 5,
                                 .main_min = 1,
                                 .main_max = 50,
                                 .extra_count = 2,
                                 .extra_min = 1,
                                 .extra_max = 12};

const LotteryInfo *plugin_get_info(void)
{
    return &INFO;
}

const char *plugin_get_name(void)
{
    return "EuroJackpot";
}

void plugin_draw(LotteryResult *out, draw_event_callback cb)
{
    generate_draw(INFO.main_count, INFO.main_min, INFO.main_max, INFO.extra_count, INFO.extra_min,
                  INFO.extra_max, out, cb);
}
