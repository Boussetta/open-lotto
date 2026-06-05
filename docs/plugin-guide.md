# Plugin Development Guide

This guide explains how to write a new lottery game plugin for Open-Lotto.

---

## Overview

Each game is a dynamically loaded shared library (`.so`) that exports three C symbols defined
in [`include/lottery_plugin.h`](../include/lottery_plugin.h). The core engine discovers plugins
at runtime by scanning the `plugins/` directory inside the build output.

---

## Plugin Interface

Every plugin must implement these three functions:

```c
#include "lottery_plugin.h"

/* Return static game configuration (number ranges and pick counts). */
const LotteryInfo *plugin_get_info(void);

/* Return a human-readable display name for the game. */
const char *plugin_get_name(void);

/* Perform one draw and write the result to *out.
 * Call cb(event, out) at each DrawEvent milestone (may be NULL). */
void plugin_draw(LotteryResult *out, draw_event_callback cb);
```

The `LotteryInfo` and `LotteryResult` types are defined in
[`include/combogen.h`](../include/combogen.h):

```c
typedef struct {
    int main_count;   /* how many main numbers to pick */
    int main_min;     /* lowest possible main number (inclusive) */
    int main_max;     /* highest possible main number (inclusive) */
    int extra_count;  /* how many extra numbers to pick (0 = none) */
    int extra_min;    /* lowest possible extra number (inclusive) */
    int extra_max;    /* highest possible extra number (inclusive) */
} LotteryInfo;

typedef struct {
    int main_numbers[MAX_MAIN_NUMBERS];   /* drawn main numbers */
    int main_count;
    int extra_numbers[MAX_EXTRA_NUMBERS]; /* drawn extra numbers */
    int extra_count;
} LotteryResult;
```

---

## Step-by-Step Example: PowerBall

The US Powerball game draws 5 main numbers from 1–69 and 1 Powerball from 1–26.

### 1. Create the source file

Create `plugins/powerball.c`:

```c
/* SPDX-License-Identifier: MIT */
#include "combogen.h"
#include "lottery_plugin.h"

/* US Powerball:
 *  - 5 main numbers from 1..69
 *  - 1 Powerball from 1..26
 */

static const LotteryInfo INFO = {
    .main_count  = 5,
    .main_min    = 1,
    .main_max    = 69,
    .extra_count = 1,
    .extra_min   = 1,
    .extra_max   = 26,
};

const LotteryInfo *plugin_get_info(void)
{
    return &INFO;
}

const char *plugin_get_name(void)
{
    return "PowerBall";
}

void plugin_draw(LotteryResult *out, draw_event_callback cb)
{
    generate_draw(INFO.main_count, INFO.main_min, INFO.main_max,
                  INFO.extra_count, INFO.extra_min, INFO.extra_max,
                  out, cb);
}
```

### 2. Register the plugin in CMakeLists.txt

Add the following block to `CMakeLists.txt` alongside the existing plugin targets:

```cmake
add_library(powerball SHARED plugins/powerball.c)
target_link_libraries(powerball PRIVATE m)
set_target_properties(powerball PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)
```

### 3. Build and run

```bash
mkdir -p build && cd build
cmake ..
make -j
./open-lotto --game powerball
```

Expected output:

```
PowerBall: 7 14 28 41 63 | PB: 18
```

### 4. Multiple draws and export

```bash
./open-lotto --game powerball --draws 5
./open-lotto --game powerball --draws 10 --export json --output powerball.json
```

---

## Games Without Extra Numbers

If your game has no bonus ball, set `extra_count = 0`. The `extra_min` and `extra_max` values
are ignored when `extra_count = 0`.

Example — a hypothetical pick-6 from 1–42 with no bonus:

```c
static const LotteryInfo INFO = {
    .main_count  = 6,
    .main_min    = 1,
    .main_max    = 42,
    .extra_count = 0,
    .extra_min   = 0,
    .extra_max   = 0,
};
```

---

## Draw Event Callbacks

`plugin_draw` receives a `draw_event_callback cb` which the engine uses for animation and
logging. Pass it through to `generate_draw` unchanged. If you implement a custom draw
algorithm (not using `generate_draw`), fire the callbacks at these points:

| Event | When to fire |
|-------|-------------|
| `EVENT_RNG_INITIALIZED` | After the RNG seed is set |
| `EVENT_POOL_INITIALIZED` | After the candidate pool is populated |
| `EVENT_AFTER_SHUFFLE` | After the Fisher-Yates shuffle |
| `EVENT_AFTER_PICK` | After each number is picked |
| `EVENT_DRAW_COMPLETE` | When all picks are done |

`cb` may be `NULL`; always guard before calling:

```c
if (cb != NULL) {
    cb(EVENT_DRAW_COMPLETE, out);
}
```

---

## Naming Convention

- Source file: `plugins/<gamename>.c` (lowercase, no spaces)
- `plugin_get_name()` return value: human-readable display name (e.g. `"Lotto 6aus49"`)
- CMake target name: matches the source file base name (e.g. `powerball`)

The engine identifies games by the string returned from `plugin_get_name()`, not by the
filename of the `.so`. You can rename the file freely as long as the symbol is correct.

---

## Testing Your Plugin

Add a basic smoke test by running:

```bash
cd build
ctest --output-on-failure
./open-lotto --list-games         # your game should appear
./open-lotto --game powerball     # single draw must exit 0
./open-lotto --game powerball --draws 100  # stress test
```

For unit tests, follow the patterns in `tests/test_combogen.c` and register a new test
executable in `CMakeLists.txt` using the `stub_plugin` helper in `tests/stub_plugin.c`.

---

## Reference: Existing Plugins

| File | Game | Main | Extra |
|------|------|------|-------|
| `plugins/lotto.c` | Lotto 6aus49 | 6 from 1–49 | 1 Superzahl from 0–9 |
| `plugins/eurojackpot.c` | EuroJackpot | 5 from 1–50 | 2 Euro numbers from 1–12 |
