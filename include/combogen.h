#ifndef COMBOGEN_H
#define COMBOGEN_H

#include <stdint.h>

struct LotteryInfo;
struct LotteryResult;

typedef enum {
    EVENT_RNG_INITIALIZED = 1,
    EVENT_POOL_INITIALIZED,
    EVENT_AFTER_SHUFFLE,
    EVENT_AFTER_PICK,
    EVENT_DRAW_COMPLETE
} draw_event_type;

typedef void (*draw_event_callback)(
    draw_event_type event,
    const int *pool, int pool_size,
    const int *out,  int out_size,
    uint64_t seed
);

void generate_draw(
    const struct LotteryInfo *info,
    struct LotteryResult *result,
    draw_event_callback cb
);

#endif
