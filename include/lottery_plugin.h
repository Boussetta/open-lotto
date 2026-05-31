#ifndef LOTTERY_PLUGIN_H
#define LOTTERY_PLUGIN_H

#include <stdint.h>
#include "combogen.h"

typedef struct LotteryInfo {
    const char *name;
    int main_count;
    int main_min;
    int main_max;
    int extra_count;
    int extra_min;
    int extra_max;
} LotteryInfo;

typedef struct LotteryResult {
    int main_numbers[16];
    int extra_numbers[16];
} LotteryResult;

const LotteryInfo* lottery_get_info(void);

void lottery_generate(
    LotteryResult *out,
    draw_event_callback cb
);

#endif
