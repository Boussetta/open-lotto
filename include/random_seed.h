/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef RANDOM_SEED_H
#define RANDOM_SEED_H

#include <stdint.h>

/**
 * @file random_seed.h
 * @brief Cryptographically strong seed derivation helpers.
 */

/** @brief Generate a high-entropy 64-bit seed from system sources. */
uint64_t generate_strong_seed(void);

#endif
