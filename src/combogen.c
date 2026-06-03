#include <stdlib.h>
#include <time.h>
#include "combogen.h"

static int rng_int(int min, int max)
{
    return min + rand() % (max - min + 1);
}

__attribute__((visibility("default")))
void generate_draw(
    int main_count,
    int main_min,
    int main_max,
    int extra_count,
    int extra_min,
    int extra_max,
    LotteryResult *out,
    draw_event_callback cb
){
    srand((unsigned)time(NULL));

    if (cb)
        cb(EVENT_RNG_INITIALIZED, NULL);

    out->main_count = main_count;
    out->extra_count = extra_count;

    int pool_size = main_max - main_min + 1;
    int *pool = malloc(pool_size * sizeof(int));
    if (!pool)
        return;

    for (int i = 0; i < pool_size; i++)
        pool[i] = main_min + i;

    if (cb)
        cb(EVENT_POOL_INITIALIZED, NULL);

    for (int i = pool_size - 1; i > 0; i--) {
        int j = rng_int(0, i);
        int tmp = pool[i];
        pool[i] = pool[j];
        pool[j] = tmp;
    }

    if (cb)
        cb(EVENT_AFTER_SHUFFLE, NULL);

    for (int i = 0; i < main_count; i++)
        out->main_numbers[i] = pool[i];

    free(pool);

    if (cb)
        cb(EVENT_AFTER_PICK, out);

    for (int i = 0; i < extra_count; i++)
        out->extra_numbers[i] = rng_int(extra_min, extra_max);

    if (cb)
        cb(EVENT_DRAW_COMPLETE, out);
}
