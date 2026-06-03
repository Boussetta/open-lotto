#ifndef RANDOM_H
#define RANDOM_H

#include <stdint.h>

/*
 * Returns a fresh 64-bit seed using /dev/urandom or time fallback.
 */
uint64_t rng_seed(void);

/*
 * Advances the RNG state and returns a 64-bit random number.
 */
uint64_t rng_next(uint64_t *state);

#endif
