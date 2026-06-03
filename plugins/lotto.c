#include "combogen.h"
#include "lottery_plugin.h"

/* Lotto 6aus49:
 *  - 6 main numbers from 1..49
 *  - 1 extra (Superzahl) from 0..9
 */

static const LotteryInfo INFO = {
    .main_count  = 6,
    .main_min    = 1,
    .main_max    = 49,
    .extra_count = 1,
    .extra_min   = 0,
    .extra_max   = 9
};

const LotteryInfo* plugin_get_info(void)
{
    return &INFO;
}

const char* plugin_get_name(void)
{
    return "Lotto 6aus49";
}

void plugin_draw(LotteryResult *out, draw_event_callback cb)
{
    generate_draw(
        INFO.main_count,
        INFO.main_min,
        INFO.main_max,
        INFO.extra_count,
        INFO.extra_min,
        INFO.extra_max,
        out,
        cb
    );
}
