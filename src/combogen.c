#include "combogen.h"
#include "random.h"
#include "log.h"
#include "lottery_plugin.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/random.h>
#include <cpuid.h>
#include <stdint.h>

/* ------------------------------------------------------------
   CPU feature detection for RDRAND
   ------------------------------------------------------------ */
static int cpu_supports_rdrand() {
    unsigned int eax, ebx, ecx, edx;

    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx))
        return 0;

    // GCC defines this as bit_RDRND (not bit_RDRAND)
    return (ecx & bit_RDRND) != 0;
}

/* ------------------------------------------------------------
   Inline assembly RDRAND (no compiler flags needed)
   ------------------------------------------------------------ */
static int rdrand64(uint64_t *value) {
    unsigned char ok;

    __asm__ __volatile__(
        "rdrand %0; setc %1"
        : "=r" (*value), "=qm" (ok)
    );

    return ok;
}

/* ------------------------------------------------------------
   Hybrid seed generator:
   - getrandom() kernel entropy
   - CLOCK_MONOTONIC timestamp
   - RDRAND (if CPU supports it)
   ------------------------------------------------------------ */
static uint64_t generate_seed() {
    uint64_t a = 0, b = 0, c = 0;

    // 1. Kernel entropy
    getrandom(&a, sizeof(a), 0);

    // 2. Monotonic clock
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    b = ((uint64_t)ts.tv_sec << 32) ^ ts.tv_nsec;

    // 3. Hardware entropy (Intel/AMD only)
    if (cpu_supports_rdrand()) {
        uint64_t hw;
        if (rdrand64(&hw))
            c = hw;
    }

    return a ^ b ^ c;
}

/* ------------------------------------------------------------
   Fisher–Yates shuffle using PCG32 RNG
   ------------------------------------------------------------ */
static void shuffle(RandomGenerator rng, int *arr, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = rng.next_int(&rng, i + 1);
        int tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

/* ------------------------------------------------------------
   Main draw generator
   ------------------------------------------------------------ */
void generate_draw(
    const LotteryInfo *info,
    LotteryResult *result,
    draw_event_callback cb
) {
    /* -----------------------------
       RNG initialization
       ----------------------------- */
    uint64_t seed = generate_seed();
    RandomGenerator rng = create_pcg32_rng(seed);

    if (cb)
        cb(EVENT_RNG_INITIALIZED, NULL, 0, NULL, 0, seed);

    /* -----------------------------
       Build main pool
       ----------------------------- */
    int pool[128];
    int pool_size = 0;

    for (int i = info->main_min; i <= info->main_max; i++)
        pool[pool_size++] = i;

    if (cb)
        cb(EVENT_POOL_INITIALIZED, pool, pool_size, NULL, 0, 0);

    /* -----------------------------
       Shuffle pool
       ----------------------------- */
    shuffle(rng, pool, pool_size);

    if (cb)
        cb(EVENT_AFTER_SHUFFLE, pool, pool_size, NULL, 0, 0);

    /* -----------------------------
       Pick main numbers
       ----------------------------- */
    for (int i = 0; i < info->main_count; i++)
        result->main_numbers[i] = pool[i];

    if (cb)
        cb(EVENT_AFTER_PICK,
           pool, pool_size,
           result->main_numbers, info->main_count,
           0);

    /* -----------------------------
       Pick extra numbers
       ----------------------------- */
    for (int i = 0; i < info->extra_count; i++) {
        result->extra_numbers[i] =
            rng.next_int(&rng, info->extra_max - info->extra_min + 1)
            + info->extra_min;
    }

    if (cb)
        cb(EVENT_DRAW_COMPLETE,
           result->main_numbers, info->main_count,
           result->extra_numbers, info->extra_count,
           0);
}
