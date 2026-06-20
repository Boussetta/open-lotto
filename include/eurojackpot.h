/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef EUROJACKPOT_H
#define EUROJACKPOT_H

#include "random.h"

/**
 * @file eurojackpot.h
 * @brief Legacy EuroJackpot draw API (kept for compatibility).
 */

typedef struct
{
    int numbers[5];
    int eurozahlen[2];
} EurojackpotResult;

EurojackpotResult eurojackpot_draw(RandomGenerator rng);

#endif