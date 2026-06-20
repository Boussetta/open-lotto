/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef LOTTO_H
#define LOTTO_H

#include "random.h" // <-- REQUIRED so RandomGenerator is known

/**
 * @file lotto.h
 * @brief Legacy Lotto draw API (kept for compatibility).
 */

typedef struct
{
    int numbers[6];
    int superzahl;
} LottoResult;

LottoResult lotto_draw(RandomGenerator rng);

#endif