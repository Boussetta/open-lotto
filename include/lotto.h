#ifndef LOTTO_H
#define LOTTO_H

#include "random.h"   // <-- REQUIRED so RandomGenerator is known

typedef struct {
    int numbers[6];
    int superzahl;
} LottoResult;

LottoResult lotto_draw(RandomGenerator rng);

#endif
