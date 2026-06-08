<!--
SPDX-FileCopyrightText: 2026 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# Developer API

This document describes the public C API provided by `open_lotto_api` for deterministic draw generation and integration into external tools.

## Header and Target

- Header: `include/open_lotto_api.h`
- CMake target: `open_lotto_devapi`

Example CMake usage:

```cmake
target_link_libraries(my_tool PRIVATE open_lotto_devapi)
target_include_directories(my_tool PRIVATE ${CMAKE_SOURCE_DIR}/include)
```

## Core Types

```c
typedef struct
{
    int main_count;
    int main_min;
    int main_max;
    int extra_count;
    int extra_min;
    int extra_max;
} OpenLottoDrawSpec;
```

`OpenLottoDrawSpec` defines one game's draw constraints.

## Functions

```c
int open_lotto_validate_spec(const OpenLottoDrawSpec *spec);
int open_lotto_generate(const OpenLottoDrawSpec *spec, LotteryResult *out);
int open_lotto_generate_seeded(const OpenLottoDrawSpec *spec, uint64_t seed, LotteryResult *out);
uint64_t open_lotto_derive_seed(uint64_t base_seed, uint64_t draw_index);
int open_lotto_sync_historical_latest(const char *game_name, const char *db_root,
                                      HistoricalDrawSnapshot *out_snapshot);
int open_lotto_load_historical_latest(const char *game_name, const char *db_root,
                                      HistoricalDrawSnapshot *out_snapshot);
const char *open_lotto_version(void);
```

Return values:

- `OPEN_LOTTO_API_SUCCESS` (0): success
- `OPEN_LOTTO_API_SYNC_UNCHANGED` (10): sync completed and latest draw was already present
- `OPEN_LOTTO_API_ERR_INVALID_ARG` (1): null pointer argument
- `OPEN_LOTTO_API_ERR_INVALID_SPEC` (2): invalid draw constraints
- `OPEN_LOTTO_API_ERR_UNSUPPORTED_GAME` (3): no upstream sync source for this game
- `OPEN_LOTTO_API_ERR_NETWORK` (4): upstream fetch failed
- `OPEN_LOTTO_API_ERR_PARSE` (5): upstream/local payload parse failed
- `OPEN_LOTTO_API_ERR_IO` (6): local snapshot read/write failed

Historical sync currently supports `Eurojackpot` and stores a latest snapshot per game in the local
history database root (`db_root`, or default path when `NULL`).

## Reproducibility Model

For deterministic batches:

1. Choose a `base_seed`.
2. For each draw index `i`, compute `seed_i = open_lotto_derive_seed(base_seed, i)`.
3. Call `open_lotto_generate_seeded(..., seed_i, ...)`.

This guarantees stable replay of the same batch while generating distinct draws per index.

## Minimal Example

```c
#include "open_lotto_api.h"
#include <stdio.h>

int main(void)
{
    OpenLottoDrawSpec spec = {
        .main_count = 6,
        .main_min = 1,
        .main_max = 49,
        .extra_count = 1,
        .extra_min = 0,
        .extra_max = 9,
    };

    LotteryResult out;
    uint64_t seed = open_lotto_derive_seed(0x1234ULL, 0);

    if (open_lotto_generate_seeded(&spec, seed, &out) != OPEN_LOTTO_API_SUCCESS)
        return 1;

    printf("Draw: ");
    for (int i = 0; i < out.main_count; i++)
        printf("%d ", out.main_numbers[i]);
    printf("\n");

    return 0;
}
```
