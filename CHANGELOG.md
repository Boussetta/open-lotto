<!--
SPDX-FileCopyrightText: 2025 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

#### Simulation Analytics (milestone/v0.4.0-simulation-analytics)
- New CLI mode: `--simulation-analytics` to compute analytics directly from simulated draws
- Simulation analytics core aggregation engine (`simulation_analytics_core`)
- Simulation analytics advanced metrics (`simulation_analytics_advanced`):
  - Gap/streak tracking
  - Hot/cold ranking
  - Normalized entropy
- Simulation analytics metadata model (`simulation_analytics_metadata`) with schema version
- Simulation analytics file export (`--export csv|json --output FILE`) in simulation analytics mode
- Versioned schema contract documentation and schema file:
  - `docs/SIMULATION_ANALYTICS_SCHEMA.md`
  - `docs/schemas/simulation_analytics_v1.schema.json`
- Metric catalog and acceptance formulas:
  - `docs/SIMULATION_ANALYTICS_METRIC_CATALOG.md`

### Tests

- New unit tests:
  - `test_simulation_analytics_metadata`
  - `test_simulation_analytics_core`
  - `test_simulation_analytics_advanced`
  - `test_simulation_analytics_property`
- New integration and contract tests:
  - `tests/test_simulation_analytics_e2e.sh`
  - `tests/test_simulation_analytics_export.sh`
  - `tests/test_simulation_analytics_schema.sh`
- New performance budget guard:
  - `tests/test_simulation_analytics_perf_budget.sh`

### Documentation

- Added `docs/SIMULATION_ANALYTICS_PERFORMANCE.md`
- README docs index updated with simulation analytics references

## [1.1.0] - 2026-06-10

### Added

#### Real Lottery Analytics (milestone/v1.1.0-real-lottery-analytics)
- **Frequency distribution analytics**: `--frequency-distribution --from YYYY-MM-DD --to YYYY-MM-DD`
  - Table, JSON, and CSV output formats (`--format`)
  - 2D SDL and 3D OpenGL visualizations (`--gui 2D|3D`)
  - Explain mode (`--explain`) showing formula: `count(number) / draws_in_period`
- **Barometer analytics**: `--analytics-barometer` — overdue-number scoring per draw
  - factor = `observed_gap / expected_interval` where `expected_interval = population / picks_per_draw`
  - Table, JSON, CSV, 2D GUI, and 3D matrix GUI views
- **Hot/cold analytics**: `--analytics-hot-cold --top N` — ranked by ascending/descending frequency
  - Table, JSON, CSV, 2D GUI, and 3D GUI views
- **Shared analytics period parsing**: `--from` / `--to` validated by `validate_analytics_period()`
- **Analytics data quality evaluation**: automatic gap, duplicate, and range checks before computation
- **Analytics golden baselines**: regression test harness (`tests/test_analytics_golden.sh`)
- **Analytics performance budgets**: CI enforcement of per-command latency caps (`tests/test_analytics_perf_budget.sh`)
- **Historical CSV override**: `--historical-csv FILE` for simulation/dev datasets
- **Explain mode for all three analytics commands** (CLI text and JSON variants)
- New CLI reference guide: `docs/CLI_REFERENCE.md`

#### Bug Fixes
- **Fixed interleaved terminal output during parallel historical sync** (#97): moved all `log_warn`
  and `log_debug` calls inside the existing `omp critical(historical_db_progress)` section so
  no log output can race with ANSI cursor-movement sequences from the progress panel

### Changed
- Analytics commands default to the local real-data historical DB snapshot (no `--historical-csv` needed for live use)
- `validate.h` / `validate.c`: added `validate_iso_date()` and `validate_analytics_period()` public functions

## [0.1.0] - 2025

### Added

#### Core Features
- **Lottery Ball Physics Engine**: Realistic 3D drum simulation with Verlet integration
  - Dual `DrumInstance` physics implementation for accurate ball dynamics
  - Collision detection and sphere packing constraints
  - Configurable ball count, drum geometry, and spin physics

- **Multi-Game Support via Plugin System**:
  - Dynamic plugin loading with `plugin_loader.c`
  - Plugin registry system for game registration
  - Built-in plugins:
    - **Lotto 6aus49** (German national lottery)
    - **Eurojackpot** (European multi-state lottery)
  - Plugin interface documented in `include/lottery_plugin.h`

- **Random Number Generation**:
  - Hybrid entropy sources: `getrandom()` (Linux), RDRAND (CPU), monotonic clock jitter
  - Uniform distribution and Marsaglia's xorshift128 algorithm
  - Seeding and entropy pool management

- **Graphical User Interfaces**:
  - **SDL2 2D GUI** (`gui_sdl.c`): Animated draw display with ball animations
  - **OpenGL 3D GUI** (`gui_opengl.c`): Real-time 3D drum simulation with physics
  - Dark mode support
  - Debug overlay for physics visualization

- **Export Capabilities**:
  - CSV export format for draw results
  - JSON export format for draw results and metadata
  - Batch export functionality

- **Command-Line Interface**:
  - Game selection and configuration
  - Multiple draw generation
  - GUI launching (2D and 3D modes)
  - Export format selection
  - Performance animation mode

#### Developer Experience
- **Comprehensive Testing**:
  - Unit tests for core components (combogen, export, plugin loader)
  - Benchmark suite with OpenMP parallel performance measurement
  - CMake-based test framework with CTest integration

- **Code Quality Tools**:
  - `clang-format` style compliance (LLVM style, 100-char columns)
  - `cppcheck` static analysis integration
  - `clang-tidy` code quality checks
  - `AddressSanitizer` and `UBSanitizer` memory safety checks
  - Pre-commit git hooks for automated checks

- **Documentation**:
  - Comprehensive README with features and usage examples
  - CONTRIBUTING.md with development guidelines and plugin creation tutorial
  - Plugin development guide with step-by-step examples
  - Architecture Decision Records (ADRs) in `docs/adr/`
  - Man page documentation
  - Doxygen configuration for API documentation

- **CI/CD Pipeline**:
  - GitHub Actions workflows for:
    - Continuous integration on push/PR
    - Automated releases with artifact generation
    - Performance regression detection
    - REUSE license compliance checking
  - Dependabot for dependency scanning and auto-updates
  - Label-based PR categorization

#### Project Governance
- **Code Review Process** (CODE_REVIEW.md):
  - Pull request requirements and review criteria
  - Code owner assignments by domain
  - Automated checks and manual review process

- **Security Policy** (SECURITY.md):
  - Responsible vulnerability disclosure process
  - Dependency management with Dependabot
  - REUSE license compliance enforcement

- **Release Process** (RELEASES.md):
  - Semantic versioning implementation
  - Automated release workflow with GitHub Actions
  - Changelog generation from commit messages
  - Artifact generation and integrity verification

- **Governance Framework** (GOVERNANCE.md):
  - Project maintainer roles and responsibilities
  - Decision-making process
  - Contribution acceptance criteria
  - Community code of conduct

### Technical Details

#### Build System
- CMake 3.10+ with modern practices
- C11 standard compilation
- Optional OpenMP support for parallel benchmarking
- Multi-configuration support (Debug, Release, RelWithDebInfo)

#### Dependencies
- **Optional**: SDL2, SDL2_ttf (for GUI)
- **Optional**: OpenGL (for 3D rendering)
- **Development**: CMake, pkg-config, clang-format, cppcheck, clang-tidy

#### Platform Support
- Linux (primary target)
- macOS (via Homebrew dependencies)
- Windows (via WSL2)

#### Performance
- Benchmark suite shows O(n) complexity for draw generation
- OpenMP parallelization support for multi-core systems
- Optimized physics simulation with adaptive delta-time

### Known Limitations
- Only latest main branch version receives security updates
- 3D GUI requires OpenGL 3.0+ support
- Plugin system is C-based (by design, for portability)

---

**Repository**: https://github.com/Boussetta/open-lotto  
**License**: MIT  
**Maintainer**: Wissem Boussetta
