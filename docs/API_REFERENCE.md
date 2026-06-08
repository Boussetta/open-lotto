<!--
SPDX-FileCopyrightText: 2025 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# API Reference

This document provides a comprehensive reference for the Open-Lotto public API. For implementation details, see [ARCHITECTURE.md](ARCHITECTURE.md).

## Table of Contents

1. [Random Number Generation](#random-number-generation)
2. [Combination Generation](#combination-generation)
3. [Plugin System](#plugin-system)
4. [Export System](#export-system)
5. [Validation](#validation)
6. [Configuration](#configuration)
7. [Logging](#logging)

---

## Random Number Generation

### PCG32 Generator

**Header:** `include/random.h`

#### struct PCG32

```c
struct PCG32 {
    uint64_t state;   // Internal state
    uint64_t inc;     // Increment (stream identifier)
};
```

#### pcg32_seed()

```c
void pcg32_seed(struct PCG32* rng, uint64_t seed, uint64_t inc)
```

**Description:** Initialize a PCG32 generator with a 64-bit seed and stream ID.

**Parameters:**
- `rng` — Pointer to PCG32 struct
- `seed` — Initial state value
- `inc` — Stream increment (use 1 for single stream)

**Example:**
```c
struct PCG32 rng;
pcg32_seed(&rng, 12345ULL, 1ULL);
```

#### pcg32_random()

```c
uint32_t pcg32_random(struct PCG32* rng)
```

**Description:** Generate a 32-bit random number.

**Returns:** Random value in [0, UINT32_MAX]

**Example:**
```c
uint32_t random_num = pcg32_random(&rng);
```

#### pcg32_random_bounded()

```c
uint32_t pcg32_random_bounded(struct PCG32* rng, uint32_t bound)
```

**Description:** Generate a random number in [0, bound).

**Parameters:**
- `rng` — Pointer to PCG32 struct
- `bound` — Upper exclusive bound

**Returns:** Random value in [0, bound)

**Guarantees:** Uniform distribution, no modulo bias

**Example:**
```c
int dice = pcg32_random_bounded(&rng, 6) + 1;  // 1–6
```

### Entropy Seeding

**Header:** `include/random_seed.h`

#### generate_hybrid_seed()

```c
uint64_t generate_hybrid_seed(void)
```

**Description:** Generate a high-entropy 64-bit seed from hybrid sources.

**Sources (in priority order):**
1. Linux kernel entropy (`getrandom()`)
2. Hardware RDRAND (if available on x86-64)
3. Monotonic clock jitter
4. Fallback: time()/getpid()

**Returns:** 64-bit seed suitable for RNG initialization

**Thread-safety:** Safe; uses atomic CPU instructions for entropy

**Example:**
```c
uint64_t seed = generate_hybrid_seed();
pcg32_seed(&rng, seed, 1ULL);
```

---

## Combination Generation

**Header:** `include/combogen.h`

### struct Combination

```c
struct Combination {
    int main[7];       // Main numbers (size = main_count)
    int extra[3];      // Extra numbers (size = extra_count)
};
```

### combogen_validate_params()

```c
int combogen_validate_params(uint32_t main_range, uint32_t main_count,
                             uint32_t extra_range, uint32_t extra_count)
```

**Description:** Validate lottery game parameters.

**Parameters:**
- `main_range` — Total numbers in main pool (e.g., 49 for Lotto)
- `main_count` — Numbers to draw (e.g., 6)
- `extra_range` — Total numbers in extra pool (0 if no extra)
- `extra_count` — Extra numbers to draw (0 if no extra)

**Returns:**
- `0` — Valid parameters
- `1` — Invalid parameters

**Validation Rules:**
- `main_count <= main_range`
- `extra_count <= extra_range` (if extra_count > 0)
- `main_count > 0`
- `main_range <= 1000`

**Example:**
```c
if (combogen_validate_params(50, 5, 12, 2)) {
    fprintf(stderr, "Invalid game parameters\n");
}
```

### combogen_draw()

```c
int combogen_draw(struct Combination* out,
                  uint32_t main_range, uint32_t main_count,
                  uint32_t extra_range, uint32_t extra_count,
                  struct PCG32* rng)
```

**Description:** Generate a lottery draw with unique numbers.

**Parameters:**
- `out` — Output combination (must be non-NULL)
- `main_range`, `main_count`, `extra_range`, `extra_count` — Game parameters
- `rng` — Initialized PCG32 generator

**Returns:**
- `0` — Success
- `1` — Invalid parameters (see `combogen_validate_params()`)

**Guarantees:**
- All main numbers are unique in [1, main_range]
- All extra numbers are unique in [1, extra_range]
- No overlap between main and extra

**Example:**
```c
struct Combination draw;
struct PCG32 rng;
uint64_t seed = generate_hybrid_seed();
pcg32_seed(&rng, seed, 1ULL);

if (combogen_draw(&draw, 50, 5, 12, 2, &rng) == 0) {
    printf("Main: %d %d %d %d %d\n", draw.main[0], draw.main[1], ...);
    printf("Extra: %d %d\n", draw.extra[0], draw.extra[1]);
}
```

---

## Plugin System

**Headers:** `include/lottery_plugin.h`, `include/plugin_registry.h`, `include/plugin_loader.h`

### struct LotteryGame

```c
struct LotteryGame {
    const char* name;           // Game name (e.g., "lotto")
    const char* description;    // Human-readable description
    uint32_t main_range;        // Numbers in main pool
    uint32_t main_count;        // Numbers to draw
    uint32_t extra_range;       // Numbers in extra pool (0 if none)
    uint32_t extra_count;       // Extra numbers to draw
};
```

**Example Game Definition:**
```c
// In plugins/lotto.c
const struct LotteryGame game = {
    .name = "lotto",
    .description = "Classic Lotto (6/49)",
    .main_range = 49,
    .main_count = 6,
    .extra_range = 0,
    .extra_count = 0
};
```

### plugin_registry_init()

```c
int plugin_registry_init(const char* plugin_dir)
```

**Description:** Initialize plugin registry by scanning a directory.

**Parameters:**
- `plugin_dir` — Path to plugins directory (e.g., "./build/plugins")

**Returns:**
- `0` — Success (at least one plugin loaded)
- `1` — Failure (no plugins found or load errors)

**Side Effects:**
- Loads all `.so` files in directory
- Populates internal registry

**Example:**
```c
if (plugin_registry_init("./build/plugins")) {
    fprintf(stderr, "Failed to initialize plugins\n");
    return 1;
}
```

### plugin_registry_lookup()

```c
struct LotteryGame* plugin_registry_lookup(const char* game_name)
```

**Description:** Find a game by name in the registry.

**Parameters:**
- `game_name` — Name to search (e.g., "lotto", "eurojackpot")

**Returns:**
- Non-NULL — Pointer to LotteryGame struct
- NULL — Game not found

**Example:**
```c
struct LotteryGame* game = plugin_registry_lookup("lotto");
if (!game) {
    fprintf(stderr, "Game 'lotto' not found\n");
    return 1;
}
printf("Game: %s (%d/%d)\n", game->name, game->main_count, game->main_range);
```

### plugin_registry_list()

```c
struct LotteryGame** plugin_registry_list(int* count)
```

**Description:** Get list of all registered games.

**Parameters:**
- `count` — Output parameter; set to number of games

**Returns:**
- Pointer to array of LotteryGame pointers
- NULL if no games registered

**Memory:** Caller must free the array with `free()`

**Example:**
```c
int count = 0;
struct LotteryGame** games = plugin_registry_list(&count);
for (int i = 0; i < count; i++) {
    printf("- %s\n", games[i]->name);
}
free(games);
```

### plugin_registry_free()

```c
void plugin_registry_free(void)
```

**Description:** Unload all plugins and free registry memory.

**Side Effects:**
- Calls `dlclose()` on all loaded plugins
- Clears internal registry

**Call Pattern:**
```c
plugin_registry_init("./plugins");
// ... use plugins ...
plugin_registry_free();  // Cleanup at exit
```

---

## Export System

**Header:** `include/export.h`

### export_draws_to_csv()

```c
int export_draws_to_csv(const char* filename,
                        const struct Combination* draws, int count,
                        const struct LotteryGame* game)
```

**Description:** Export draws to CSV file.

**Parameters:**
- `filename` — Output file path
- `draws` — Array of Combination structs
- `count` — Number of draws
- `game` — Game definition (for metadata)

**Returns:**
- `0` — Success
- `1` — File I/O error

**Output Format:**
```
draw_number,main_numbers,extra_numbers
1,1 2 3 4 5 6,
2,7 8 9 10 11 12,
```

**Example:**
```c
struct Combination draws[10];
// ... populate draws ...
if (export_draws_to_csv("results.csv", draws, 10, game)) {
    perror("CSV export failed");
}
```

### export_draws_to_json()

```c
int export_draws_to_json(const char* filename,
                         const struct Combination* draws, int count,
                         const struct LotteryGame* game)
```

**Description:** Export draws to JSON file.

**Parameters:**
- `filename` — Output file path
- `draws` — Array of Combination structs
- `count` — Number of draws
- `game` — Game definition

**Returns:**
- `0` — Success
- `1` — File I/O error

**Output Format:**
```json
{
  "game": "lotto",
  "timestamp": "2026-06-08T12:34:56Z",
  "draws": [
    {
      "draw_number": 1,
      "main": [1, 2, 3, 4, 5, 6],
      "extra": []
    }
  ]
}
```

**Example:**
```c
if (export_draws_to_json("results.json", draws, 10, game)) {
    perror("JSON export failed");
}
```

---

## Validation

**Header:** `include/validate.h`

### validate_game_name()

```c
int validate_game_name(const char* name, const char** error_msg)
```

**Description:** Validate that a game name is registered.

**Parameters:**
- `name` — Game name to validate
- `error_msg` — Output parameter for error message (if invalid)

**Returns:**
- `0` — Valid
- `1` — Invalid; `*error_msg` is set

**Example:**
```c
const char* error = NULL;
if (validate_game_name("lotto", &error)) {
    fprintf(stderr, "Error: %s\n", error);
}
```

### validate_draw_count()

```c
int validate_draw_count(int count, const char** error_msg)
```

**Description:** Validate draw count.

**Parameters:**
- `count` — Number of draws
- `error_msg` — Output error message

**Returns:**
- `0` — Valid (1 <= count <= 1,000,000)
- `1` — Invalid

### validate_export_config()

```c
int validate_export_config(const char* format, const char* filename,
                           const char** error_msg)
```

**Description:** Validate export format and filename.

**Parameters:**
- `format` — "csv", "json", or NULL
- `filename` — Output file path (or NULL)
- `error_msg` — Output error message

**Returns:**
- `0` — Valid
- `1` — Invalid

**Validation:**
- Format is "csv", "json", or NULL
- If format specified, filename must be provided
- Filename is writable and doesn't escape directory

### validate_all()

```c
int validate_all(const struct Config* config, const char** error_msg)
```

**Description:** Validate entire configuration at once.

**Parameters:**
- `config` — Configuration struct
- `error_msg` — Output error message

**Returns:**
- `0` — Valid
- `1` — Invalid

---

## Configuration

**Header:** `include/config.h`

### struct Config

```c
struct Config {
    const char* game;
    int draws;
    const char* export_format;  // "csv", "json", or NULL
    const char* export_filename;
    int log_level;
    const char* gui_mode;       // "2d", "3d"
};
```

### config_load()

```c
int config_load(struct Config* out, const char* config_file)
```

**Description:** Load configuration from file (`.lottorc` format).

**Parameters:**
- `out` — Output Config struct
- `config_file` — Path to config file

**Returns:**
- `0` — Success
- `1` — File not found or parse error

**File Format:**
```ini
game=lotto
draws=10
log_level=info
gui=2d
export_format=csv
export_filename=results.csv
```

**Example:**
```c
struct Config cfg;
if (config_load(&cfg, "$HOME/.lottorc")) {
    fprintf(stderr, "Config load failed\n");
    return 1;
}
```

### config_merge_args()

```c
void config_merge_args(struct Config* cfg, int argc, char* argv[])
```

**Description:** Override config values with CLI arguments.

**CLI Arguments:**
- `--game <name>`
- `--draws <count>`
- `--export <format>` (csv, json)
- `--output <filename>`
- `--log <level>` (quiet, error, warn, info, debug, verbose)
- `--gui <mode>` (2d, 3d)
- `--validate-only` (check config, don't run)

**Example:**
```c
config_load(&cfg, "$HOME/.lottorc");
config_merge_args(&cfg, argc, argv);  // CLI overrides file
```

---

## Logging

**Header:** `include/log.h`

### enum LogLevel

```c
enum LogLevel {
    LOG_QUIET = 0,
    LOG_ERROR = 1,
    LOG_WARN = 2,
    LOG_INFO = 3,
    LOG_DEBUG = 4,
    LOG_VERBOSE = 5
};
```

### log_set_level()

```c
void log_set_level(enum LogLevel level)
```

**Description:** Set global log verbosity level.

**Example:**
```c
log_set_level(LOG_INFO);
```

### log_error(), log_warn(), log_info(), log_debug()

```c
void log_error(const char* format, ...);
void log_warn(const char* format, ...);
void log_info(const char* format, ...);
void log_debug(const char* format, ...);
void log_verbose(const char* format, ...);
```

**Description:** Log messages at specific levels.

**Format:** Printf-style format string

**Example:**
```c
log_info("Generating %d draws for game '%s'", count, game->name);
log_debug("Using seed: 0x%016llx", seed);
```

---

## Command-Line Usage

### Basic Usage

```bash
./open-lotto --game lotto --draws 5
```

### With Export

```bash
./open-lotto --game eurojackpot --draws 100 --export csv --output results.csv
```

### Validation Only

```bash
./open-lotto --game lotto --validate-only
```

### With GUI

```bash
./open-lotto --game lotto --gui 3d
./open-lotto --game lotto --gui 2d  # Default
```

### Debug Logging

```bash
./open-lotto --game lotto --log debug --draws 10
```

---

## Error Codes

| Code | Meaning | Common Causes |
|------|---------|--------------|
| `0` | Success | Normal completion |
| `1` | Invalid arguments | Unknown game, bad draw count, invalid flags |
| `2` | Plugin load failure | Missing `.so` file, ABI mismatch |
| `3` | Export error | File not writable, disk full |
| `4` | Configuration error | Invalid config file, merge conflict |

---

## Memory Management

All Open-Lotto APIs follow these principles:

- **Caller allocates:** Structures passed to functions are caller-allocated (e.g., `struct Combination* out`)
- **Library allocates:** Arrays returned by functions (e.g., `plugin_registry_list()`) must be freed with `free()`
- **No hidden state:** Structs are self-contained; no hidden pointers or allocations

**Example:**
```c
struct Combination draw;  // Caller allocates
struct PCG32 rng;        // Caller allocates

// combogen_draw() fills draw, no allocation
combogen_draw(&draw, 50, 5, 12, 2, &rng);

// plugin_registry_list() returns heap-allocated array
int count = 0;
struct LotteryGame** games = plugin_registry_list(&count);
free(games);  // Caller frees
```

---

## Thread Safety

| Component | Thread-Safe? | Notes |
|-----------|-------------|-------|
| PCG32 | No | Each thread needs its own instance |
| combogen | Yes | Stateless function |
| plugin_registry | Yes | Read-only after init |
| generate_hybrid_seed | Yes | Uses atomic CPU instructions |
| Logging | Yes | Thread-local buffering |

---

## See Also

- [ARCHITECTURE.md](ARCHITECTURE.md) — System design and internals
- [DEBUGGING.md](DEBUGGING.md) — How to debug with GDB/Valgrind
- [PERFORMANCE_TUNING.md](PERFORMANCE_TUNING.md) — Profiling and optimization
