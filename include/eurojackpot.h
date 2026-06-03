#ifndef EUROJACKPOT_H
#define EUROJACKPOT_H

#include "random.h" 

typedef struct {
    int numbers[5];
    int eurozahlen[2];
} EurojackpotResult;

EurojackpotResult eurojackpot_draw(RandomGenerator rng);

#endif