#include "lottery_plugin.h"
#include "combogen.h"

static LotteryInfo INFO = {
    .name = "Eurojackpot",
    .main_count = 5,
    .main_min = 1,
    .main_max = 50,
    .extra_count = 2,
    .extra_min = 1,
    .extra_max = 12
};

const LotteryInfo* lottery_get_info(void) {
    return &INFO;
}

void lottery_generate(LotteryResult *out, draw_event_callback cb) {
    generate_draw(&INFO, out, cb);
}
