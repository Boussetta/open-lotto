#include <stdlib.h>
#include <stdint.h>
#include "combogen.h"
#include "random_seed.h"
#include "log.h"

/* PCG32 Random Number Generator (statistically strong) */
typedef struct {
    uint64_t state;
    uint64_t inc;
} pcg32_t;

static pcg32_t rng_state = {0};
static uint64_t last_seed = 0;

/* Initialize PCG32 with a strong seed and return the seed value */
static uint64_t rng_init(void)
{
    last_seed = generate_strong_seed();
    rng_state.state = last_seed;
    rng_state.inc = (generate_strong_seed() << 1) | 1;
    return last_seed;
}

/* PCG32 next value */
static uint32_t rng_next(void)
{
    uint64_t oldstate = rng_state.state;
    rng_state.state = oldstate * 6364136223846793005ULL + rng_state.inc;
    uint32_t xorshifted = ((oldstate >> 18u) ^ oldstate) >> 27u;
    uint32_t rot = oldstate >> 59u;
    return (xorshifted >> rot) | (xorshifted << (32u - rot));
}

/* Generate random integer in range [min, max] */
static int rng_int(int min, int max)
{
    uint32_t range = (uint32_t)(max - min + 1);
    uint32_t val = rng_next();
    return min + (int)(val % range);
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
    /* Validate result structure */
    if (!out) {
        log_error("LotteryResult pointer is NULL");
        return;
    }

    /* Validate main numbers count */
    if (main_count <= 0 || main_count > 7) {
        log_error("Invalid main_count: %d (must be 1-7)", main_count);
        return;
    }

    /* Validate extra numbers count */
    if (extra_count < 0 || extra_count > 3) {
        log_error("Invalid extra_count: %d (must be 0-3)", extra_count);
        return;
    }

    /* Validate main number range */
    if (main_min <= 0 || main_max <= 0 || main_min > main_max) {
        log_error("Invalid main range: [%d, %d]", main_min, main_max);
        return;
    }

    int main_range = main_max - main_min + 1;
    if (main_range < main_count) {
        log_error("Not enough main numbers: need %d from range of %d", main_count, main_range);
        return;
    }

    /* Validate extra number range if extra numbers needed */
    if (extra_count > 0) {
        if (extra_min < 0 || extra_max < 0 || extra_min > extra_max) {
            log_error("Invalid extra range: [%d, %d]", extra_min, extra_max);
            return;
        }
    }

    uint64_t seed = rng_init();
    log_info("RNG Seed: 0x%016lx", (unsigned long)seed);

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
