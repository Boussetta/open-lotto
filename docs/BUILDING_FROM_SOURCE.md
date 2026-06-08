# Building Open-Lotto from Source

## Quick Start

For most users, the automated installation script is the easiest path:

```bash
./scripts/install_deps.sh
cmake -S . -B build
cmake --build build
./build/open-lotto --game lotto --draws 10
```

## Prerequisites

### Required Dependencies

| Package | Min Version | Purpose | Linux | macOS | Windows |
|---------|-------------|---------|-------|-------|---------|
| **CMake** | 3.10 | Build system | ✓ | ✓ | ✓ |
| **C Compiler** | C11 | Code compilation | gcc/clang | clang | MSVC/MinGW |
| **make** | any | Build driver | ✓ | ✓ | GNU Make |
| **pkg-config** | any | Library discovery | ✓ | ✓ | (optional) |
| **SDL2** | 2.0 | 2D GUI rendering | ✓ | ✓ | ✓ |
| **SDL2_ttf** | 2.0 | Font rendering | ✓ | ✓ | ✓ |
| **OpenGL** | any | 3D GUI rendering | ✓ | ✓ | ✓ |

### Optional Dependencies

| Package | Purpose | Enable Flag |
|---------|---------|------------|
| **OpenMP** | Parallel physics simulation | `-DENABLE_OPENMP=ON` |
| **Doxygen** | API documentation generation | (automatic) |
| **lcov** | Code coverage reporting | (in CI only) |
| **git** | Version control (cloning repo) | (automatic) |

## Linux Installation

### Automated Installation (Recommended)

```bash
git clone https://github.com/Boussetta/open-lotto.git
cd open-lotto
./scripts/install_deps.sh
```

The script detects your Linux distribution and installs appropriate packages:
- **Ubuntu/Debian**: apt-get
- **Fedora/RHEL**: dnf/yum
- **Arch/Manjaro**: pacman
- **openSUSE**: zypper

### Manual Installation

#### Ubuntu / Debian / Pop!_OS / Linux Mint

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    ccache \
    pkg-config \
    libsdl2-dev \
    libsdl2-ttf-dev \
    libgl1-mesa-dev \
    mesa-common-dev
```

#### Fedora / RHEL / CentOS / Rocky / Alma

```bash
sudo dnf install -y \
    gcc \
    gcc-c++ \
    cmake \
    ccache \
    pkgconfig \
    SDL2-devel \
    SDL2_ttf-devel \
    mesa-libGL-devel \
    mesa-libEGL-devel
```

#### Arch / Manjaro

```bash
sudo pacman -S --noconfirm \
    base-devel \
    cmake \
    ccache \
    sdl2 \
    sdl2_ttf \
    mesa
```

#### openSUSE

```bash
sudo zypper install -y \
    gcc \
    gcc-c++ \
    cmake \
    ccache \
    pkgconfig \
    SDL2-devel \
    libSDL2_ttf-devel \
    Mesa-libGL-devel
```

## macOS Installation

Using Homebrew:

```bash
# Install Homebrew (if not installed)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install cmake ccache pkg-config sdl2 sdl2_ttf

# Alternatively, run the script
./scripts/install_deps.sh
```

For M1/M2 Macs: Ensure you're using Apple Clang (usually the default).

## Windows Installation

### Option 1: MSVC (Visual Studio)

1. Install **Visual Studio 2019+** with C++ build tools
2. Install **CMake** (https://cmake.org/download/)
3. Install **vcpkg** for package management:
   ```powershell
   git clone https://github.com/Microsoft/vcpkg.git
   cd vcpkg
   .\vcpkg integrate install
   ```

4. Install SDL2 and dependencies:
   ```powershell
   .\vcpkg install sdl2:x64-windows sdl2-ttf:x64-windows
   ```

5. Configure and build:
   ```powershell
   cmake -S . -B build `
     -DCMAKE_TOOLCHAIN_FILE="path/to/vcpkg/scripts/buildsystems/vcpkg.cmake" `
     -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release
   ```

### Option 2: MinGW + MSYS2

1. Install **MSYS2**: https://www.msys2.org/
2. Open MSYS2 terminal and install:
   ```bash
   pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake \
            mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_ttf
   ```

3. Build:
   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```

## Building from Source

### Basic Build

```bash
# 1. Clone repository
git clone https://github.com/Boussetta/open-lotto.git
cd open-lotto

# 2. Create build directory
cmake -S . -B build

# 3. Compile (Release mode, optimized)
cmake --build build --config Release -j$(nproc)

# 4. Optional: Install to system
cmake --install build

# 5. Run
./build/open-lotto --game lotto --draws 5
```

### Build Variants

#### Development (Debug Symbols)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

Includes debug symbols, disables optimizations, enables sanitizers-friendly compilation.

#### Release with Optimizations

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS="-O3 -march=native -flto"
cmake --build build -j$(nproc)
```

Enables aggressive optimizations and Link-Time Optimization (LTO).

#### With OpenMP (Parallel Physics)

```bash
cmake -S . -B build -DENABLE_OPENMP=ON
cmake --build build -j$(nproc)
```

Parallelizes physics simulation loops. Expected 2–4× speedup on multi-core.

#### With Code Coverage

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="--coverage -fprofile-arcs -ftest-coverage" \
  -DCMAKE_EXE_LINKER_FLAGS="--coverage"
cmake --build build -j$(nproc)

# Run tests to generate coverage data
ctest --test-dir build --output-on-failure

# Generate report
./scripts/run_coverage.sh
```

### Build Options Summary

| CMake Option | Default | Purpose |
|--------------|---------|---------|
| `-DCMAKE_BUILD_TYPE` | Release | Debug, Release, RelWithDebInfo, MinSizeRel |
| `-DENABLE_OPENMP` | OFF | Enable OpenMP for parallelization |
| `-DBUILD_TESTING` | ON | Build unit tests (disable to skip tests) |
| `-DCMAKE_C_COMPILER` | auto-detect | Specify GCC, Clang, MSVC, etc. |
| `-DCMAKE_C_FLAGS` | empty | Custom compiler flags |

## Running Tests

### Unit Tests

```bash
# Build with testing enabled (default)
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build

# Run all tests
ctest --test-dir build --output-on-failure

# Run specific test
ctest --test-dir build -R test_combogen --output-on-failure
```

### Test Categories

- **Unit Tests**: `test_combogen`, `test_export`, `test_plugin_loader`
- **Integration Tests**: CLI success/failure paths
- **Stress Tests**: `test_stress` with many draws
- **Performance Tests**: `benchmark`, `benchmark_openmp`

### Benchmarking

```bash
# Build benchmark target
cmake --build build --target benchmark

# Run
./build/benchmark

# For OpenMP parallel variant
cmake -S . -B build -DENABLE_OPENMP=ON
./build/benchmark_openmp
```

## Installing

### System Installation

```bash
cmake --install build --prefix /usr/local
```

Installs:
- Executable to `/usr/local/bin/open-lotto`
- Man page to `/usr/local/share/man/man1/open-lotto.1`
- Plugins to `/usr/local/lib/open-lotto/plugins/`

### Uninstall

```bash
xargs rm < build/install_manifest.txt
```

## Troubleshooting

### CMake Not Found

**Error:** `cmake: command not found`

**Solution:**
```bash
# Install CMake
sudo apt-get install cmake  # Debian/Ubuntu
brew install cmake          # macOS
choco install cmake         # Windows

# Or download from https://cmake.org/download/
```

### SDL2 Not Found

**Error:** `Could not find SDL2`

**Solution (Linux):**
```bash
sudo apt-get install libsdl2-dev libsdl2-ttf-dev
# Or equivalent for your distro
```

**Solution (macOS):**
```bash
brew install sdl2 sdl2_ttf
```

**Solution (Manual pkg-config path):**
```bash
export PKG_CONFIG_PATH=/opt/SDL2/lib/pkgconfig:$PKG_CONFIG_PATH
cmake -S . -B build
```

### Compiler Errors

**Error:** `undefined reference to 'dlopen'`

**Cause:** Missing `-ldl` linker flag (rare, CMake usually handles)

**Solution:**
```bash
cmake -S . -B build -DCMAKE_C_FLAGS="-ldl"
cmake --build build
```

**Error:** `conflicting types for 'main'`

**Cause:** Rare compiler incompatibility

**Solution:**
```bash
# Use gcc explicitly
cmake -S . -B build -DCMAKE_C_COMPILER=gcc
cmake --build build
```

### OpenGL / Mesa Issues

**Error:** `undefined reference to 'glClear'`

**Solution (Linux):**
```bash
sudo apt-get install libgl1-mesa-dev mesa-common-dev
```

**Solution (Fedora):**
```bash
sudo dnf install mesa-libGL-devel
```

### Plugin Loading Fails

**Error:** `Failed to load plugin: cannot open shared object file`

**Cause:** Plugins not built or library path not found

**Solution:**
```bash
# Ensure plugins built
cmake --build build --target lotto eurojackpot

# Check plugin file exists
ls -la build/plugins/

# Run from build directory or set LD_LIBRARY_PATH
export LD_LIBRARY_PATH=$PWD/build/plugins:$LD_LIBRARY_PATH
./build/open-lotto --game lotto --draws 5
```

### Sanitizer Errors on Runtime

**Error:** `AddressSanitizer: heap-buffer-overflow`

**Cause:** Possible buffer access outside bounds

**Solution:**
```bash
# Run with debug build and ASan enabled
cmake -S . -B build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
cmake --build build-asan

# Run with more verbose output
ASAN_OPTIONS="verbosity=2:halt_on_error=1" ./build-asan/open-lotto
```

See [DEBUGGING.md](DEBUGGING.md) for more details.

## Clean Build

```bash
rm -rf build
cmake -S . -B build
cmake --build build
```

## Advanced: Custom Flags

### Enable All Optimizations

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS="-O3 -march=native -flto -ffast-math" \
  -DENABLE_OPENMP=ON
cmake --build build -j$(nproc)
```

### Profile-Guided Optimization (PGO)

```bash
# Phase 1: Instrument with profiling
cmake -S . -B build \
  -DCMAKE_C_FLAGS="-O2 -fprofile-generate"
cmake --build build

# Phase 2: Generate profile data
./build/open-lotto --game lotto --draws 100000 --validate-only

# Phase 3: Rebuild with profile feedback
cmake -S . -B build \
  -DCMAKE_C_FLAGS="-O3 -fprofile-use -fprofile-correction"
cmake --build build
```

### Minimal Build (No Unnecessary Deps)

```bash
cmake -S . -B build \
  -DBUILD_TESTING=OFF \
  -DENABLE_OPENMP=OFF
cmake --build build
```

## IDE Setup

### VS Code

See [docs/ide-setup.md](ide-setup.md) for full configuration.

### Visual Studio (Windows)

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
```

Opens Visual Studio and configures CMake extension.

### CLion (JetBrains)

1. Open project in CLion
2. CMake should auto-detect (top status bar)
3. Build via **Build > Build Project**

### Neovim/Vim with vim-cmake

```vim
:CMakeGenerate
:CMakeBuild
:CTest
```

## Performance Notes

- **Incremental builds are fast** due to ccache (auto-used if installed)
- **Release builds take ~5 seconds** on modern hardware
- **Debug builds take ~3 seconds** (less optimization)
- **LTO builds take ~15 seconds** (whole-program analysis)

Use `cmake --build build -v` to see full compilation commands.

## CI/CD Integration

For GitHub Actions, see `.github/workflows/ci.yml`:
- Builds with gcc and clang
- Runs tests with sanitizers
- Performs static analysis
- Generates coverage reports

For local CI testing:

```bash
./scripts/run_format_check.sh
./scripts/run_cppcheck.sh
./scripts/run_clang_tidy.sh
./scripts/run_sanitizers.sh
ctest --test-dir build
```
