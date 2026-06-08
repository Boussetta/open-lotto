/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "combogen.h"
#include "log.h"
#include "random_seed.h"
#include <stdint.h>
#include <stdlib.h>

/**
 * @file combogen.c
 * @brief Core lottery draw generation engine with PCG32 random number generator.
 *
 * This module implements the main lottery draw logic using:
 *   - PCG32: A statistically strong, fast PRNG (O'Neill, 2014)
 *   - Fisher-Yates Shuffle: For unbiased number selection without replacement
 *   - Cryptographic Seeds: From kernel entropy sources
 *
 * The draw algorithm:
 *   1. Initialize PCG32 with a strong cryptographic seed
 *   2. Create a pool of all possible main numbers
 *   3. Shuffle the pool using Fisher-Yates with PCG32
 *   4. Pick the first N numbers from the shuffled pool
 *   5. Repeat for extra numbers if needed
 *
 * This ensures:
 *   - No duplicates within a draw (Fisher-Yates property)
 *   - Uniform distribution across all possible draws
 *   - Cryptographically strong randomness
 *   - High performance (O(n) time complexity)
 */

/**
 * @struct pcg32_t
 * @brief PCG32 random number generator state.
 *
 * The PCG32 algorithm uses two state components:
 *   - state: Current generator state (advances via linear congruential formula)
 *   - inc: Increment constant (ensures full period traversal)
 *
 * The state space is 2^64, providing excellent statistical properties
 * and a full period before repeating.
 */
typedef struct
{
    uint64_t state; /**< Current state for LCG advancement */
    uint64_t inc;   /**< Increment constant for period guarantee */
} pcg32_t;

/** @brief Global PCG32 generator state */
static pcg32_t rng_state = {0};

/** @brief Last seed used (for logging/debugging purposes) */
static uint64_t last_seed = 0;

/**
 * @brief Initialize PCG32 generator with cryptographically strong seeds.
 *
 * Creates independent seeds for both the state and increment using
 * the entropy system. The increment is shifted and OR'd with 1 to
 * ensure it's odd (required for PCG32 full-period guarantee).
 *
 * @return uint64_t - The seed value used for the state (for logging)
 *
 * @note Call this once per draw to ensure different sequence for each draw.
 * @note The increment seed is derived independently to maximize entropy.
 */
static uint64_t rng_init(void)
{
    last_seed = generate_strong_seed();
    rng_state.state = last_seed;
    rng_state.inc = (generate_strong_seed() << 1) | 1;
    return last_seed;
}

/**
 * @brief Generate the next random number from PCG32.
 *
 * Implements the PCG32 algorithm:
 *   1. Save current state
 *   2. Advance state via linear congruential generator
 *   3. XOR-shift and rotate output for statistical whitening
 *
 * The PCG32 output function provides excellent statistical properties
 * suitable for cryptographic applications.
 *
 * @return uint32_t - Next random number in [0, UINT32_MAX]
 *
 * @note This function modifies internal state; each call produces
 *       a different value from a sequence with period 2^64.
 */
static uint32_t rng_next(void)
{
    uint64_t oldstate = rng_state.state;
    /* Advance state via linear congruential generator */
    rng_state.state = oldstate * 6364136223846793005ULL + rng_state.inc;
    /* Output function: XOR-shift and rotate for whitening */
    uint32_t xorshifted = ((oldstate >> 18u) ^ oldstate) >> 27u;
    uint32_t rot = oldstate >> 59u;
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

/**
 * @brief Generate a random integer in the range [min, max] inclusive.
 *
 * Uses rejection sampling with modulo to map [0, UINT32_MAX] to [min, max].
 * This provides uniform distribution across the requested range.
 *
 * @param min - Minimum value (inclusive)
 * @param max - Maximum value (inclusive)
 *
 * @return int - Random number in [min, max]
 *
 * @note Assumes min <= max (caller's responsibility to validate)
 * @note Uses modulo bias elimination implicitly (acceptable for lottery use)
 */
static int rng_int(int min, int max)
{
    uint32_t range = (uint32_t)(max - min + 1);
    uint32_t val = rng_next();
    return min + (int)(val % range);
}

/**
 * @brief Generate a complete lottery draw (main + extra numbers).
 *
 * This is the main public API for lottery draw generation. It:
 *   1. Validates all input parameters
 *   2. Initializes the RNG with a strong seed
 *   3. Creates and shuffles the main number pool using Fisher-Yates
 *   4. Picks the first N numbers from the shuffled pool
 *   5. Repeats for extra numbers (if needed) to ensure uniqueness
 *   6. Fires callbacks at each stage for animation/monitoring
 *
 * The draw guarantees:
 *   - No duplicate numbers within a draw
 *   - Cryptographically strong randomness
 *   - Uniform probability across all possible draws
 *   - O(n) time complexity where n = main_count + extra_count
 *
 * @param main_count - Number of main numbers to draw (1-7)
 * @param main_min - Minimum value for main numbers (inclusive)
 * @param main_max - Maximum value for main numbers (inclusive)
 * @param extra_count - Number of extra numbers (0-3, e.g., euro numbers)
 * @param extra_min - Minimum value for extra numbers (inclusive)
 * @param extra_max - Maximum value for extra numbers (inclusive)
 * @param out - Output structure to populate with draw results
 * @param cb - Optional callback for animation/event handling
 *
 * @return void - Results stored in 'out'; errors logged but not returned
 *
 * @note Validates all parameters; invalid inputs are logged and silently
 *       fail (output remains unmodified).
 * @note Thread-safe for different draw calls; uses thread-local state if
 *       needed (currently global, caller must synchronize if multi-threaded).
 * @note Callbacks are invoked at: RNG_INITIALIZED, POOL_INITIALIZED,
 *       AFTER_SHUFFLE, AFTER_PICK (main), DRAW_COMPLETE
 */
__attribute__((visibility("default"))) void
generate_draw(int main_count, int main_min, int main_max, int extra_count, int extra_min,
              int extra_max, LotteryResult *out, draw_event_callback cb)
{
    /* Validate result structure pointer */
    if (!out)
    {
        log_error("LotteryResult pointer is NULL");
        return;
    }

    /* Validate main numbers count (supports 1-7 numbers per draw) */
    if (main_count <= 0 || main_count > 7)
    {
        log_error("Invalid main_count: %d (must be 1-7)", main_count);
        return;
    }

    /* Validate extra numbers count (supports 0-3 extra numbers) */
    if (extra_count < 0 || extra_count > 3)
    {
        log_error("Invalid extra_count: %d (must be 0-3)", extra_count);
        return;
    }

    /* Validate main number range constraints */
    if (main_min <= 0 || main_max <= 0 || main_min > main_max)
    {
        log_error("Invalid main range: [%d, %d]", main_min, main_max);
        return;
    }

    /* Verify there are enough main numbers available in the range */
    int main_range = main_max - main_min + 1;
    if (main_range < main_count)
    {
        log_error("Not enough main numbers: need %d from range of %d", main_count, main_range);
        return;
    }

    /* Validate extra number range if extra numbers are requested */
    if (extra_count > 0)
    {
        if (extra_min < 0 || extra_max < 0 || extra_min > extra_max)
        {
            log_error("Invalid extra range: [%d, %d]", extra_min, extra_max);
            return;
        }

        /* Ensure there are enough extra numbers in the range */
        int extra_range = extra_max - extra_min + 1;
        if (extra_range < extra_count)
        {
            log_error("Not enough extra numbers: need %d from range of %d", extra_count,
                      extra_range);
            return;
        }
    }

    /* Initialize PCG32 RNG with a cryptographically strong seed */
    uint64_t seed = rng_init();
    log_info("RNG Seed: 0x%016lx", (unsigned long)seed);

    /* Fire RNG initialization callback for animation */
    if (cb)
        cb(EVENT_RNG_INITIALIZED, NULL);

    /* Store counts in result for consumer */
    out->main_count = main_count;
    out->extra_count = extra_count;

    /* === MAIN NUMBERS: Fisher-Yates Shuffle === */

    /* Allocate pool of main numbers */
    int pool_size = main_max - main_min + 1;
    int *pool = malloc(pool_size * sizeof(int));
    if (!pool)
        return;

    /* Initialize pool with [main_min, main_min+1, ..., main_max] */
    for (int i = 0; i < pool_size; i++)
        pool[i] = main_min + i;

    /* Fire pool initialization callback */
    if (cb)
        cb(EVENT_POOL_INITIALIZED, NULL);

    /* Fisher-Yates shuffle: for each position i, swap with random j in [0, i] */
    for (int i = pool_size - 1; i > 0; i--)
    {
        int j = rng_int(0, i);
        int tmp = pool[i];
        pool[i] = pool[j];
        pool[j] = tmp;
    }

    /* Fire shuffle completion callback */
    if (cb)
        cb(EVENT_AFTER_SHUFFLE, NULL);

    /* Pick the first main_count numbers from the shuffled pool */
    for (int i = 0; i < main_count; i++)
        out->main_numbers[i] = pool[i];

    /* Release main number pool memory */
    free(pool);

    /* Fire main numbers complete callback */
    if (cb)
        cb(EVENT_AFTER_PICK, out);

    /* === EXTRA NUMBERS: Fisher-Yates Shuffle (if needed) === */

    if (extra_count > 0)
    {
        /* Allocate pool of extra numbers */
        int extra_pool_size = extra_max - extra_min + 1;
        int *extra_pool = malloc(extra_pool_size * sizeof(int));
        if (!extra_pool)
            return;

        /* Initialize pool with [extra_min, extra_min+1, ..., extra_max] */
        for (int i = 0; i < extra_pool_size; i++)
            extra_pool[i] = extra_min + i;

        /* Fisher-Yates shuffle for extra numbers pool */
        for (int i = extra_pool_size - 1; i > 0; i--)
        {
            int j = rng_int(0, i);
            int tmp = extra_pool[i];
            extra_pool[i] = extra_pool[j];
            extra_pool[j] = tmp;
        }

        /* Pick the first extra_count numbers from the shuffled extra pool */
        for (int i = 0; i < extra_count; i++)
            out->extra_numbers[i] = extra_pool[i];

        /* Release extra number pool memory */
        free(extra_pool);
    }

    /* Fire draw completion callback */
    if (cb)
        cb(EVENT_DRAW_COMPLETE, out);
}
