# Contributing to Open-Lotto

Thank you for your interest in contributing to Open-Lotto! This guide will help you get started.

## Table of Contents

- [Getting Started](#getting-started)
- [Building Locally](#building-locally)
- [Running Tests](#running-tests)
- [Code Style](#code-style)
- [Writing Lottery Plugins](#writing-lottery-plugins)
- [Submitting Pull Requests](#submitting-pull-requests)
- [Reporting Issues](#reporting-issues)

## Getting Started

### Prerequisites

- **Linux/macOS/Windows (WSL2)**
- **CMake** 3.10+
- **GCC** or **Clang**
- **pkg-config**
- **SDL2** and **SDL2_ttf** development libraries

### Installing Dependencies

**Ubuntu/Debian:**
```bash
sudo apt-get update
sudo apt-get install -y cmake gcc pkg-config libsdl2-dev libsdl2-ttf-dev
```

**macOS (with Homebrew):**
```bash
brew install cmake sdl2 sdl2_ttf
```

## Building Locally

### Basic Build

```bash
# Clone the repository
git clone https://github.com/Boussetta/open-lotto.git
cd open-lotto

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON ..

# Build the project
cmake --build . -j

# Run the application
./open-lotto --game "Lotto 6aus49"
```

### Build Options

- `-DCMAKE_BUILD_TYPE=Release` — Optimized release build
- `-DCMAKE_BUILD_TYPE=Debug` — Debug build with symbols
- `-DBUILD_TESTING=ON` — Enable unit tests (default: ON)

### Debug Build with Sanitizers

For memory safety checks during development:

```bash
cmake -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -g" ..

cmake --build . -j
ctest --output-on-failure
```

## Running Tests

### All Tests

```bash
cd build
ctest --output-on-failure
```

### Specific Test

```bash
# Run only unit tests
ctest -R unit_combogen --output-on-failure

# Run only CLI tests
ctest -R cli --output-on-failure
```

### List Available Tests

```bash
ctest -N
```

## Code Style

### Code Formatting

The project uses **clang-format** with LLVM style (100-character columns).

**Format all source files:**
```bash
clang-format -i $(find src plugins include tests -name "*.c" -o -name "*.h")
```

**Check formatting without modifying:**
```bash
clang-format -n -Werror $(find src plugins include tests -name "*.c" -o -name "*.h")
```

### Documentation

- **All public functions** must have Doxygen-style docstrings
- **Complex algorithms** should include inline comments
- **File headers** should describe the module's purpose

Example docstring:
```c
/**
 * @brief Generate a lottery draw with main and extra numbers.
 *
 * Implements Fisher-Yates shuffle for unbiased selection without replacement.
 * Ensures no duplicate numbers within a single draw and uses cryptographically
 * strong RNG seeding.
 *
 * @param main_count - Number of main numbers to draw (1-7)
 * @param main_min - Minimum value for main numbers (inclusive)
 * @param main_max - Maximum value for main numbers (inclusive)
 * @param extra_count - Number of extra numbers (0-3)
 * @param extra_min - Minimum value for extra numbers
 * @param extra_max - Maximum value for extra numbers
 * @param out - Output structure to populate
 * @param cb - Optional callback for animation/events
 *
 * @return void - Results stored in 'out'; errors logged
 *
 * @note Validates all parameters; invalid inputs fail silently
 */
```

### Naming Conventions

- **Functions:** `snake_case` (e.g., `generate_draw()`)
- **Typedefs:** `PascalCase` (e.g., `LotteryResult`)
- **Constants:** `UPPER_SNAKE_CASE` (e.g., `MAX_BALLS`)
- **Variables:** `snake_case` (e.g., `pool_size`)

### Compilation Requirements

- **Must compile with `-Wall -Wextra -Wpedantic -Werror`**
- **All warnings must be fixed** (no ignored warnings)
- **Static analysis must pass** (cppcheck with no errors)

## Writing Lottery Plugins

### Plugin Architecture

A lottery plugin is a shared library (.so/.dll) that implements:
1. Game metadata (name, number ranges, counts)
2. Draw generation function

### Minimal Plugin Example

**File: `plugins/my_lottery.c`**

```c
#include "combogen.h"
#include "lottery_plugin.h"

/**
 * @file my_lottery.c
 * @brief My Custom Lottery plugin
 *
 * My Lottery draws:
 *  - 7 main numbers from 1..49
 *  - 1 bonus number from 0..9
 */

static const LotteryInfo INFO = {
    .main_count = 7,
    .main_min = 1,
    .main_max = 49,
    .extra_count = 1,
    .extra_min = 0,
    .extra_max = 9
};

/**
 * @brief Get lottery game metadata
 *
 * @return Pointer to static LotteryInfo describing game rules
 */
const LotteryInfo* plugin_get_info(void)
{
    return &INFO;
}

/**
 * @brief Get human-readable game name
 *
 * @return Game name (e.g., "My Lottery")
 */
const char* plugin_get_name(void)
{
    return "My Lottery";
}

/**
 * @brief Generate a lottery draw
 *
 * @param out - Output result structure
 * @param cb - Callback for animation/event handling
 *
 * @note Called by the plugin loader for each draw
 */
void plugin_draw(LotteryResult *out, draw_event_callback cb)
{
    generate_draw(
        INFO.main_count,
        INFO.main_min,
        INFO.main_max,
        INFO.extra_count,
        INFO.extra_min,
        INFO.extra_max,
        out,
        cb
    );
}
```

### Adding the Plugin to Build

**Edit `CMakeLists.txt`:**

```cmake
# Add your plugin shared library
add_library(my_lottery SHARED plugins/my_lottery.c)

# Link required libraries
target_link_libraries(my_lottery PRIVATE m)

# Set output directory
set_target_properties(my_lottery PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)
```

### Plugin Validation

After creating your plugin, verify it works:

```bash
# Build
cd build && cmake --build .

# List all discovered games
./open-lotto --list-games

# Test the new game
./open-lotto --game "My Lottery" --draws 5

# Test with animation
./open-lotto --game "My Lottery" --animate --draws 3
```

### Popular Lottery Examples

**Powerball (US):**
- 5 main numbers from 1..69
- 1 powerball from 1..26

**Mega Millions (US):**
- 5 main numbers from 1..70
- 1 mega ball from 1..25

**EuroMillions (EU):**
- 5 main numbers from 1..50
- 2 lucky stars from 1..12

## Submitting Pull Requests

### Before You Start

1. **Fork the repository** on GitHub
2. **Create a feature branch** from `main`:
   ```bash
   git checkout -b feature/my-contribution
   ```
3. **Make your changes** following the code style guidelines

### Testing Requirements

Before submitting a PR, ensure:

1. **All tests pass:**
   ```bash
   cd build && ctest --output-on-failure
   ```

2. **Code is properly formatted:**
   ```bash
   clang-format -n -Werror $(find src plugins include tests -name "*.c" -o -name "*.h")
   ```

3. **No compiler warnings:**
   ```bash
   cmake --build . 2>&1 | grep -i warning
   ```

4. **Static analysis passes:**
   ```bash
    ./scripts/run_cppcheck.sh
   ```

### Optional: Run Cppcheck On Every Commit

Install the repository-managed git hooks once:

```bash
./scripts/install-git-hooks.sh
```

After this, `cppcheck` runs automatically on each `git commit` via `.githooks/pre-commit`.
The pre-commit hook also runs `clang-format` validation and ASan/UBSan test checks, mirroring CI gates locally.
Sanitizer checks use a local build folder (`.build-sanitizer-local/`) to avoid modifying tracked build artifacts.

5. **Sanitizers detect no errors:**
   ```bash
   cmake -DCMAKE_C_FLAGS="-fsanitize=address,undefined" ..
   cmake --build . && ctest --output-on-failure
   ```

### Commit Message Format

Use clear, descriptive commit messages:

```
Add new Powerball lottery plugin

- Implements 5 main numbers from 1-69 + 1 powerball from 1-26
- Includes comprehensive unit tests for draw generation
- Validates Powerball-specific constraints
```

### Pull Request Checklist

- [ ] Tests pass (`ctest --output-on-failure`)
- [ ] Code is formatted (`clang-format`)
- [ ] No compiler warnings (`-Wall -Wextra -Wpedantic -Werror`)
- [ ] Static analysis passes (`cppcheck`)
- [ ] Sanitizers pass (`ASan/UBSan`)
- [ ] Docstrings added for new functions
- [ ] Commit messages are clear and descriptive

### PR Description Template

```markdown
## Description
Brief explanation of the change

## Type of Change
- [ ] Bug fix
- [ ] New feature (plugin, export format, etc.)
- [ ] Documentation
- [ ] Test improvement
- [ ] Performance improvement

## Related Issues
Fixes #(issue number)

## How to Test
Steps to verify the change works

## Checklist
- [ ] Tests pass
- [ ] Code formatted with clang-format
- [ ] No new warnings
```

## Reporting Issues

### Bug Reports

When reporting bugs, include:

1. **System information** (OS, compiler, version)
2. **Steps to reproduce**
3. **Expected behavior**
4. **Actual behavior**
5. **Error logs** (if applicable)

Example:

```markdown
### System
- OS: Ubuntu 24.04 LTS
- Compiler: GCC 15.2.0
- CMake: 3.29

### Steps to Reproduce
1. Run `./open-lotto --game "Eurojackpot" --draws 100`
2. Check output for duplicate numbers

### Expected
All 100 draws have unique euro numbers

### Actual
Draw #42 has euro numbers [3, 3]

### Logs
[Paste output here]
```

### Feature Requests

Describe the desired behavior and use case:

```markdown
### Feature: Export to CSV

Allow users to save draw results to CSV file for record-keeping.

**Use Case:** Lottery organizers want to archive historical draws
with timestamps and player info.

**Proposed API:**
```bash
./open-lotto --game "Lotto" --draws 10 --export csv results.csv
```
```

## Getting Help

- **Questions?** Open a discussion on GitHub
- **Found a bug?** Open an issue with reproduction steps
- **Want to contribute?** Start with a feature request or discussion

## License

By contributing, you agree that your contributions are licensed under the [MIT License](LICENSE).

Thank you for contributing to Open-Lotto! 🎲
