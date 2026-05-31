#include "lottery_plugin.h"
#include "combogen.h"

static LotteryInfo INFO = {
    .name = "Lotto 6aus49",
    .main_count = 6,
    .main_min = 1,
    .main_max = 49,
    .extra_count = 1,
    .extra_min = 0,
    .extra_max = 9
};

const LotteryInfo* lottery_get_info(void) {
    return &INFO;
}

void lottery_generate(LotteryResult *out, draw_event_callback cb) {
    generate_draw(&INFO, out, cb);
}
