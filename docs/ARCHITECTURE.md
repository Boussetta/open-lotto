<!--
SPDX-FileCopyrightText: 2025 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# Open-Lotto Architecture

## Overview

Open-Lotto is a modular, plugin-based lottery number generator written in C. The architecture emphasizes clean separation of concerns, extensibility through plugins, and high-quality random number generation with hybrid entropy seeding.

## Core Components

### 1. Main Application (`src/main.c`)

The entry point that:
- Parses command-line arguments
- Loads the configuration from `.lottorc` or CLI flags
- Initializes the GUI (SDL2 2D or OpenGL 3D)
- Orchestrates the game loop
- Handles graceful shutdown and cleanup

**Key Functions:**
- `main()` — Application entry point
- `run_headless_mode()` — Non-interactive draw generation with export
- `run_gui_mode()` — Interactive GUI mode with animation

### 2. Random Number Generation

#### PCG32 Generator (`src/random.c`, `include/random.h`)

PCG32 is a modern, fast, and statistically strong random number generator ideal for simulations. Each draw gets a unique, high-quality seed.

**Key Features:**
- 64-bit internal state with 32-bit output
- Fast, cache-friendly implementation
- Statistical properties suitable for Monte Carlo workloads
- Seed advance functionality for reproducible streams

#### Hybrid Entropy Seeding (`src/random_seed.c`, `include/random_seed.h`)

Every draw uses a composite seed built from multiple independent entropy sources:

1. **Linux Kernel Entropy** (`getrandom()`) — Cryptographic-grade entropy from `/dev/urandom`
2. **Hardware Entropy** (`RDRAND`) — x86-64 CPU instruction when available
3. **Monotonic Clock Jitter** — High-resolution timer with jitter from system load
4. **Fallback** — Pseudo-random seed from time()/getpid() if entropy unavailable

All sources are XOR-combined into a 64-bit seed that feeds the PCG32 generator.

**Key Functions:**
- `generate_hybrid_seed()` — Composite seed generation
- `get_cpu_entropy()` — RDRAND support detection and execution

### 3. Combination Generation (`src/combogen.c`, `include/combogen.h`)

The core algorithm for drawing unique lottery numbers.

**Algorithm:**
- Validates game parameters: main range, main count, extra range, extra count
- Draws main numbers without replacement using sample-and-shuffle
- Draws extra numbers (if applicable) without replacement
- Enforces validity constraints (e.g., extra_count ≤ extra_range)

**Key Functions:**
- `combogen_draw()` — Generate a single lottery draw
- `combogen_validate_params()` — Parameter validation

**Design:**
- Minimal memory allocation (fixed-size array for MAX_MAIN_NUMBERS + MAX_EXTRA_NUMBERS)
- Fast O(n) sampling with no repeated values
- Suitable for high-throughput scenarios

### 4. Plugin System

#### Plugin Registry (`src/plugin_registry.c`, `include/plugin_registry.h`)

Central registry for dynamically loaded game definitions.

**Design:**
- Plugins are discovered at runtime from a known directory
- Each plugin is a shared library (.so) implementing the lottery game interface
- Registry maintains an in-memory list of available games

**Key Functions:**
- `plugin_registry_init()` — Scan plugin directory and load definitions
- `plugin_registry_lookup()` — Find game by name
- `plugin_registry_list()` — Get all registered games
- `plugin_registry_free()` — Cleanup and unload plugins

#### Plugin Loader (`src/plugin_loader.c`, `include/plugin_loader.h`)

Handles dynamic library loading using `dlopen()` and `dlsym()`.

**Design:**
- Uses `dlopen()` for late binding of game libraries
- `dlsym()` resolves the game definition struct from the shared library
- Error handling with meaningful messages from `dlerror()`

**Key Functions:**
- `plugin_loader_load()` — Load a .so and retrieve game definition
- `plugin_loader_unload()` — Unload and cleanup

#### Game Plugin Interface (`include/lottery_plugin.h`)

Each game plugin must implement:

```c
const struct LotteryGame game = {
    .name = "game_name",
    .description = "Game description",
    .main_range = 50,      // Numbers in main pool
    .main_count = 6,       // Numbers to draw
    .extra_range = 10,     // Numbers in extra pool (0 if none)
    .extra_count = 1       // Numbers to draw from extra pool
};
```

**Standard Plugins:**
- `plugins/lotto.c` — Classic Lotto (6/49)
- `plugins/eurojackpot.c` — Eurojackpot (5/50 + 2/12)

### 5. GUI Architecture

#### SDL2 2D GUI (`src/gui_sdl.c`, `include/gui_sdl.h`)

Default graphical interface using SDL2.

**Features:**
- Real-time ball animation with falling and bouncing physics
- Responsive event handling (keyboard, window management)
- TTF font rendering for clear number display
- Theme support (colors, fonts, layout)

**Key Components:**
- Ball physics simulation
- Collision detection with boundaries
- Texture/sprite rendering
- Event loop and frame rate control

#### OpenGL 3D GUI (`src/gui_opengl.c`, `include/gui_opengl.h`)

Advanced 3D visualization using fixed-function OpenGL.

**Features:**
- 3D rotating drum with perspective camera
- Realistic ball physics with angular momentum
- Lighting and material properties
- No external math library (hand-rolled 4x4 matrix operations)

**Physics Simulation:**
- Gravity-based falling phase
- Velocity damping and settling
- Rotation with inertial drag
- Ball-to-boundary collision with response
- OpenMP parallelization of physics loops for performance

**Key Functions:**
- `update_drum_instance()` — Physics step function (parallelized)
- `render_drum()` — OpenGL rendering
- `check_collisions()` — Collision detection

### 6. Export System (`src/export.c`, `include/export.h`)

Serialize lottery draws to file formats.

**Formats:**
- **CSV**: `draw_number,main_numbers,extra_numbers`
- **JSON**: Structured with game metadata and array of draws

**Key Functions:**
- `export_draws_to_csv()` — Write CSV file
- `export_draws_to_json()` — Write JSON file

### 7. Validation Module (`src/validate.c`, `include/validate.h`)

Comprehensive input validation and sanitization.

**Validates:**
- Game name (exists in registry)
- Draw count (positive, reasonable upper bound)
- Export format (csv, json, or none)
- Export filename (writable, no path traversal)
- Log level (valid enum)
- GUI mode (2d or 3d)
- Option conflicts (e.g., export without --validate-only)

**Features:**
- Helpful error messages with hints
- `--validate-only` flag for CI/automation use cases

### 8. Logging (`src/log.c`, `include/log.h`)

Flexible logging system with configurable verbosity.

**Log Levels:**
- QUIET (0) — Silent operation
- ERROR (1) — Errors only
- WARN (2) — Warnings and above
- INFO (3) — Informational (default)
- DEBUG (4) — Debug tracing
- VERBOSE (5) — Maximum verbosity

**Features:**
- Per-module logger activation
- Colored output on terminal
- Timestamp and severity prefixes

### 9. Configuration (`src/config.c`, `include/config.h`)

Supports `.lottorc` configuration files.

**Format:**
```ini
game=eurojackpot
draws=5
log_level=info
gui=3d
```

**Behavior:**
- CLI arguments override config file values
- Game can be specified in config for convenience
- Follows XDG Base Directory Specification

## Data Flow

```
main()
  │
  ├─→ parse_args() / config_load()
  │    ├─ Resolve game name
  │    └─ Merge CLI + config
  │
  ├─→ plugin_registry_init()
  │    └─ Scan plugins/, dlopen() .so files
  │
  ├─→ Start GUI (SDL2 or OpenGL)
  │    │
  │    └─→ Game Loop (per frame or per draw):
  │         │
  │         ├─ random_seed_generate() → high-entropy 64-bit seed
  │         │
  │         ├─ pcg32_seed() → initialize RNG state
  │         │
  │         ├─ combogen_draw()
  │         │   ├─ Validate parameters
  │         │   ├─ pcg32_random() × (main_count + extra_count)
  │         │   └─ Return unique numbers
  │         │
  │         ├─ update_physics() [GUI]
  │         │   ├─ gravity, damping, collision
  │         │   └─ ball animation
  │         │
  │         ├─ render() [GUI]
  │         │   └─ Display balls, scores
  │         │
  │         └─ export_draws() [if --export]
  │             └─ Write CSV/JSON
```

## Plugin System Internals

### Plugin Discovery

1. Scan `./plugins` directory for `.c` files
2. Build as shared libraries (`.so`) via CMake
3. On startup, `plugin_registry_init()` calls `plugin_loader_load()` for each `.so`
4. Each `.so` exports a global `struct LotteryGame game`
5. Registry stores pointers to `LotteryGame` for lookup

### Plugin Loading

```c
// In plugin_loader.c
void* handle = dlopen("./build/plugins/lotto.so", RTLD_LAZY);
struct LotteryGame* game_ptr = (struct LotteryGame*)dlsym(handle, "game");
registry[index] = game_ptr;
```

### Plugin Isolation

Plugins run in the same process but can:
- Be unloaded at shutdown
- Each define their own game parameters
- Not interfere with each other (separate compilation units)

## Physics Simulation (3D GUI)

### Drum Instance

```c
struct DrumInstance {
    struct DrumBall balls[MAX_BALLS];
    float velocity_x, velocity_y, velocity_z;  // Drum rotation velocity
    float position_x, position_y, position_z;  // Drum center in world
    enum DrumPhase phase;
    float elapsed_time;
};
```

### Ball Struct

```c
struct DrumBall {
    float x, y, z;           // Position
    float vx, vy, vz;        // Velocity
    float rot_x, rot_y, rot_z;  // Angular velocity
    int number;              // Lottery number 1–main_range
    int is_selected;         // Pick state
};
```

### Collision Response

When a ball hits the drum boundary:
1. Detect overlap (position outside radius)
2. Calculate normal (radial from center)
3. Compute response: reverse perpendicular velocity, apply damping
4. If rolling, add friction torque to angular velocity

### Phases

- **FALLING**: Gravity and collision, wait for settling
- **STOPPING**: Decelerate rotation, exit when omega < threshold
- **PICK**: Select N random balls as winners
- **PICK_PAUSE**: Animate selected balls, flash results
- **DONE**: Wait for user input to restart

## Build System

CMake-based build system (`CMakeLists.txt`):
- Detects SDL2, OpenGL, pkg-config
- Compiles main executable, plugins, tests
- Supports `BUILD_TESTING` for unit tests
- Coverage reporting with lcov (enabled by CI)
- Sanitizer builds (ASan/UBSan) for debugging

### Key Targets

- `open-lotto` — Main executable
- `lotto`, `eurojackpot` — Game plugins
- `test_combogen`, `test_plugin_loader`, etc. — Unit tests
- `benchmark`, `benchmark_openmp` — Performance tests

## Code Organization

```
open-lotto/
├── src/                    # Implementation (.c files)
│   ├── main.c
│   ├── random.c, random_seed.c
│   ├── combogen.c
│   ├── plugin_*.c
│   ├── gui_sdl.c, gui_opengl.c
│   ├── export.c
│   ├── validate.c
│   ├── config.c
│   └── log.c
├── include/                # Headers (.h files)
├── plugins/                # Game plugins (.c → .so)
├── tests/                  # Unit & integration tests
├── CMakeLists.txt          # Build configuration
└── docs/                   # Documentation
```

## Design Principles

1. **Modularity**: Plugin system, separate concerns, minimal coupling
2. **Performance**: PCG32 for speed, OpenMP parallelization, fixed-size arrays
3. **Correctness**: Validation at every boundary, test coverage, sanitizers
4. **Extensibility**: New games via plugins without recompilation
5. **Clarity**: Meaningful variable names, comprehensive comments, Doxygen docstrings
6. **Robustness**: Error handling, graceful degradation, meaningful diagnostics
