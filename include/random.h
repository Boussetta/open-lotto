#ifndef RANDOM_H
#define RANDOM_H

#include <stdint.h>

typedef struct RandomGenerator {
    uint64_t state;
    uint64_t inc;
    int (*next_int)(struct RandomGenerator *rng, int max);
} RandomGenerator;

RandomGenerator create_pcg32_rng(uint64_t seed);

#endif
