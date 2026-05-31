#include "random.h"

static uint32_t pcg32_random(RandomGenerator *rng) {
    uint64_t oldstate = rng->state;
    rng->state = oldstate * 6364136223846793005ULL + (rng->inc | 1);
    uint32_t xorshifted = ((oldstate >> 18u) ^ oldstate) >> 27u;
    uint32_t rot = oldstate >> 59u;
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

static int pcg32_next_int(RandomGenerator *rng, int max) {
    return pcg32_random(rng) % max;
}

RandomGenerator create_pcg32_rng(uint64_t seed) {
    RandomGenerator rng;
    rng.state = seed;
    rng.inc = (seed << 1u) | 1u;
    rng.next_int = pcg32_next_int;
    return rng;
}
