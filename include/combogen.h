/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef COMBOGEN_H
#define COMBOGEN_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    EVENT_RNG_INITIALIZED = 0,
    EVENT_POOL_INITIALIZED,
    EVENT_AFTER_SHUFFLE,
    EVENT_AFTER_PICK,
    EVENT_DRAW_COMPLETE
} DrawEvent;

#define MAX_MAIN_NUMBERS 7  /* supports 6aus49 and EuroJackpot */
#define MAX_EXTRA_NUMBERS 3 /* supports EuroJackpot (2 extras) */

typedef struct
{
    int main_numbers[MAX_MAIN_NUMBERS];
    int main_count;

    int extra_numbers[MAX_EXTRA_NUMBERS];
    int extra_count;
} LotteryResult;

typedef struct
{
    int main_count;
    int main_min;
    int main_max;
    int extra_count;
    int extra_min;
    int extra_max;
} LotteryInfo;

typedef void (*draw_event_callback)(DrawEvent event, const LotteryResult *result);

void generate_draw(int main_count, int main_min, int main_max, int extra_count, int extra_min,
                   int extra_max, LotteryResult *out, draw_event_callback cb);

void generate_draw_seeded(int main_count, int main_min, int main_max, int extra_count,
                          int extra_min, int extra_max, uint64_t seed, LotteryResult *out,
                          draw_event_callback cb);

void combogen_set_forced_seed(uint64_t seed);
void combogen_clear_forced_seed(void);

#endif /* COMBOGEN_H */
