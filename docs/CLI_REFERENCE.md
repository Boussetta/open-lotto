<!--
SPDX-FileCopyrightText: 2025 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# CLI Reference Guide

Complete command-line interface documentation for **Open-Lotto**.

## Table of Contents

1. [Quick Start](#quick-start)
2. [Global Options](#global-options)
3. [Modes](#modes)
4. [Analytics Commands](#analytics-commands)
5. [Configuration Options](#configuration-options)
6. [Output Formats](#output-formats)
7. [Examples](#examples)
8. [Environment Variables](#environment-variables)
9. [Config File](#config-file)
10. [Exit Codes](#exit-codes)

---

## Quick Start

### List available games

```bash
./open-lotto --list-games
```

### Run a single draw

```bash
./open-lotto --game "Lotto 6aus49"
```

### Get help

```bash
./open-lotto --help
./open-lotto -h
```

---

## Global Options

### `--game NAME` (Required for most commands)

Specify the lottery game to use.

**Available games:** Use `--list-games` to see all installed games

**Example:**
```bash
./open-lotto --game "Lotto 6aus49"
./open-lotto --game "EuroJackpot"
./open-lotto --game lotto
```

---

### `--draws N`

Number of independent draws to generate.

- **Default:** 1
- **Range:** Any positive integer >= 1

**Example:**
```bash
./open-lotto --game lotto --draws 10
./open-lotto --game eurojackpot --draws 100
```

---

### `--seed VALUE`

Deterministic seed for reproducible draws.

- **Formats:** Decimal (e.g., `12345`) or hex (e.g., `0x1234abcd`)
- **Use case:** Replicate exact draws across runs

**Example:**
```bash
./open-lotto --game "Lotto 6aus49" --seed 0x1234abcd
./open-lotto --game "Lotto 6aus49" --seed 12345
```

---

### `--verbose LEVEL`

Set logging level.

- **Options:** `ERROR`, `WARN`, `INFO` (default), `DEBUG`
- **Default:** `INFO`

**Example:**
```bash
./open-lotto --game lotto --verbose DEBUG
./open-lotto --game lotto --verbose ERROR
```

---

### `--validate-only`

Validate configuration without running.

- Useful for CI/CD pipelines
- Returns 0 if valid, 1 if invalid

**Example:**
```bash
./open-lotto --game lotto --validate-only
./open-lotto --game lotto --draws 50 --validate-only
```

---

### `--reload-plugin`

Reload the selected plugin from disk before running.

- Useful during plugin development
- Reloads the shared object (.so) dynamically

**Example:**
```bash
./open-lotto --game lotto --reload-plugin
```

---

### `--help`, `-h`

Display usage information and exit with status 0.

**Example:**
```bash
./open-lotto --help
./open-lotto -h
```

---

## Modes

### Interactive Drawing (Default)

Run a single draw and display results to stdout.

```bash
./open-lotto --game lotto
```

---

### Animated CLI Mode (`--animate`)

Display animated number reveal with spinner animation.

- **Incompatible with:** `--gui`, `--export`

**Example:**
```bash
./open-lotto --game lotto --animate
./open-lotto --game eurojackpot --animate --draws 5
```

---

### Graphical Mode (`--gui`)

Launch interactive GUI visualization.

- **Syntax:** `--gui [2D|3D]`
- **Default:** 2D (SDL2)
- **Incompatible with:** `--animate`, `--export`

#### 2D GUI (SDL2)

```bash
./open-lotto --game lotto --gui 2D
./open-lotto --game eurojackpot --gui
```

#### 3D GUI (OpenGL)

```bash
./open-lotto --game lotto --gui 3D
```

#### Theme Control

```bash
./open-lotto --game lotto --gui 3D --dark-mode on
./open-lotto --game lotto --gui 3D --dark-mode off
./open-lotto --game lotto --gui 3D --dark-mode auto
```

#### Debug Overlay (3D only)

Show FPS and physics debug information.

```bash
./open-lotto --game lotto --gui 3D --debug-overlay
```

---

### Export Mode (`--export`)

Export draw results or simulation analytics results to file.

- **Formats:** `csv`, `json`
- **Requires:** `--output FILE`
- **Incompatible with:** `--animate`, `--gui`

**Example:**
```bash
./open-lotto --game lotto --draws 100 --export csv --output results.csv
./open-lotto --game eurojackpot --draws 50 --export json --output draws.json
./open-lotto --game "Lotto 6aus49" --draws 10000 --simulation-analytics --export json --output sim_analytics.json
```

---

## Analytics Commands

### Simulation Analytics

Compute analytics from simulated draws (instead of historical DB snapshots).

**Syntax:**
```bash
./open-lotto --game NAME --draws N --simulation-analytics [--top N] [--format table|json|csv]
./open-lotto --game NAME --draws N --simulation-analytics --export csv|json --output FILE
```

**Behavior:**
- Uses generated draws from the selected plugin/game configuration
- Supports deterministic runs via `--seed`
- Outputs include schema version `simulation-analytics/v1`

**Example:**
```bash
./open-lotto --game "Lotto 6aus49" --draws 5000 --seed 0x1234 --simulation-analytics --format table
./open-lotto --game "Lotto 6aus49" --draws 5000 --seed 0x1234 --simulation-analytics --format json --top 10
./open-lotto --game "Lotto 6aus49" --draws 5000 --seed 0x1234 --simulation-analytics --export csv --output sim_analytics.csv
```

---

### Frequency Distribution

Analyze how often each number appears in historical draws.

**Syntax:**
```bash
./open-lotto --game NAME [--from DATE] [--to DATE] --frequency-distribution [--format FORMAT] [--explain]
```

**Example:**
```bash
./open-lotto --game "Lotto 6aus49" --from 2024-01-01 --to 2024-12-31 --frequency-distribution
./open-lotto --game lotto --frequency-distribution --format json
./open-lotto --game lotto --frequency-distribution --explain
```

**Output fields:**
- Ball number
- Hit count (occurrences)
- Frequency percentage
- Rank (hot to cold)

---

### Barometer Analytics

Calculate overdue factors—how long it has been since a number last appeared relative to expected interval.

**Syntax:**
```bash
./open-lotto --game NAME [--from DATE] [--to DATE] --analytics-barometer [--format FORMAT] [--explain]
```

**Formula:**
```
Barometer Factor = Observed Gap / Expected Interval
Expected Interval = Total Balls / Picks Per Draw
```

**Example:**
```bash
./open-lotto --game "Lotto 6aus49" --from 2024-01-01 --to 2024-12-31 --analytics-barometer
./open-lotto --game lotto --analytics-barometer --format csv
./open-lotto --game lotto --analytics-barometer --explain
```

**Interpretation:**
- **Factor > 1.0:** Number is overdue (hasn't appeared as often as statistically expected)
- **Factor < 1.0:** Number appeared more frequently than expected
- **Factor ≈ 1.0:** Number appears at expected frequency

---

### Hot/Cold Rankings

Rank numbers by frequency (hot = highest, cold = lowest).

**Syntax:**
```bash
./open-lotto --game NAME [--from DATE] [--to DATE] --analytics-hot-cold [--top N] [--format FORMAT] [--explain]
```

**Options:**
- `--top N` — Return top N hot and top N cold numbers (default: 10)

**Example:**
```bash
./open-lotto --game "Lotto 6aus49" --from 2024-01-01 --to 2024-12-31 --analytics-hot-cold
./open-lotto --game lotto --analytics-hot-cold --top 15
./open-lotto --game lotto --analytics-hot-cold --format json --explain
```

---

### Period Filtering

Analyze draws within an inclusive date range.

**Syntax:**
```bash
--from YYYY-MM-DD --to YYYY-MM-DD
```

- **Format:** ISO 8601 (YYYY-MM-DD)
- **Behavior:** Inclusive on both ends
- **Default:** If omitted, uses entire historical dataset

**Example:**
```bash
./open-lotto --game lotto --from 2024-01-01 --to 2024-06-30 --frequency-distribution
./open-lotto --game lotto --from 2024-07-01 --to 2024-12-31 --analytics-hot-cold
```

---

### Custom Historical CSV

Use a custom CSV file as the historical source (for testing/simulation).

**Syntax:**
```bash
--historical-csv FILE
```

**CSV Format:**
```
draw_date,main1,main2,main3,main4,main5,main6[,extra]
2024-01-01,5,12,18,29,31,42
2024-01-08,3,14,21,26,35,40
...
```

**Example:**
```bash
./open-lotto --game lotto --historical-csv data.csv --from 2024-01-01 --to 2024-12-31 --frequency-distribution
```

---

### Explanation Mode (`--explain`)

Display formulas and assumptions used in analytics calculations.

- Works with: `--frequency-distribution`, `--analytics-barometer`, `--analytics-hot-cold`
- **Text output:** Appends formula as human-readable text
- **JSON output:** Adds `explain` field to output

**Example:**
```bash
./open-lotto --game lotto --frequency-distribution --explain
./open-lotto --game lotto --analytics-barometer --format json --explain
```

---

## Configuration Options

### Theme Control

**Option:** `--dark-mode MODE`

- **Values:** `on`, `off`, `auto` (default)
- **Applies to:** GUI and analytics views

**Example:**
```bash
./open-lotto --game lotto --gui 3D --dark-mode on
./open-lotto --game lotto --gui 2D --dark-mode off
```

---

### Database Snapshot

**Command:** `--database-gewinnzahlen update`

Sync local historical real-data snapshot from official sources.

**Syntax:**
```bash
./open-lotto --game NAME --database-gewinnzahlen update
```

**Example:**
```bash
./open-lotto --game "Lotto 6aus49" --database-gewinnzahlen update
./open-lotto --game eurojackpot --database-gewinnzahlen update
```

---

## Output Formats

### `--format FORMAT`

Used with analytics commands to control output format.

- **Options:** `table` (default), `json`, `csv`
- **Applies to:** `--frequency-distribution`, `--analytics-barometer`, `--analytics-hot-cold`

**Example:**
```bash
./open-lotto --game lotto --frequency-distribution --format table
./open-lotto --game lotto --frequency-distribution --format json
./open-lotto --game lotto --frequency-distribution --format csv
```

### Table Format (Default)

Human-readable terminal output with aligned columns.

```
Ball | Count | Percentage | Rank
-----+-------+------------+------
  5  |  42   |   5.2%     |  1
 14  |  41   |   5.1%     |  2
 ...
```

### JSON Format

Machine-readable structured output.

```json
{
  "mode": "frequency",
  "game": "Lotto 6aus49",
  "period": {
    "from": "2024-01-01",
    "to": "2024-12-31"
  },
  "results": [
    {"number": 5, "count": 42, "percentage": 5.2, "rank": 1},
    {"number": 14, "count": 41, "percentage": 5.1, "rank": 2}
  ],
  "explain": {
    "mode": "frequency",
    "formula": "count(number)/draws"
  }
}
```

### CSV Format

Comma-separated values for import into spreadsheets.

```
number,count,percentage,rank
5,42,5.2,1
14,41,5.1,2
...
```

---

## Examples

### Basic Draws

```bash
# Single draw
./open-lotto --game lotto

# 10 draws
./open-lotto --game "Lotto 6aus49" --draws 10

# Reproducible draw
./open-lotto --game lotto --draws 5 --seed 0xdeadbeef
```

### Animation & Display

```bash
# Animated CLI
./open-lotto --game lotto --animate

# Multiple animated draws
./open-lotto --game eurojackpot --animate --draws 3

# 2D GUI
./open-lotto --game lotto --gui 2D

# 3D GUI with dark mode
./open-lotto --game lotto --gui 3D --dark-mode on

# 3D GUI with debug overlay
./open-lotto --game lotto --gui 3D --debug-overlay
```

### Export

```bash
# CSV export
./open-lotto --game lotto --draws 100 --export csv --output draws.csv

# JSON export
./open-lotto --game eurojackpot --draws 50 --export json --output draws.json

# Large export with deterministic seed
./open-lotto --game lotto --draws 1000 --seed 12345 --export csv --output large.csv
```

### Analytics - Frequency

```bash
# Entire history
./open-lotto --game lotto --frequency-distribution

# Date range
./open-lotto --game lotto --from 2024-01-01 --to 2024-12-31 --frequency-distribution

# With explanation
./open-lotto --game lotto --frequency-distribution --explain

# As JSON
./open-lotto --game lotto --frequency-distribution --format json

# Custom CSV source
./open-lotto --game lotto --historical-csv mydata.csv --frequency-distribution
```

### Analytics - Barometer

```bash
# Simple barometer
./open-lotto --game lotto --analytics-barometer

# Date range
./open-lotto --game lotto --from 2024-06-01 --to 2024-12-31 --analytics-barometer

# With explanation (text format)
./open-lotto --game lotto --analytics-barometer --explain

# As CSV (for spreadsheet analysis)
./open-lotto --game lotto --analytics-barometer --format csv --output barometer.csv

# As JSON with explanation
./open-lotto --game lotto --analytics-barometer --format json --explain
```

### Analytics - Hot/Cold

```bash
# Top 10 (default)
./open-lotto --game lotto --analytics-hot-cold

# Top 20 entries
./open-lotto --game lotto --analytics-hot-cold --top 20

# Date range, top 5
./open-lotto --game lotto --from 2024-01-01 --to 2024-03-31 --analytics-hot-cold --top 5

# With formulas (JSON)
./open-lotto --game lotto --analytics-hot-cold --format json --explain

# Custom data, top 15
./open-lotto --game lotto --historical-csv data.csv --analytics-hot-cold --top 15
```

### Validation & Debugging

```bash
# Validate configuration
./open-lotto --game lotto --validate-only

# Validate with specific options
./open-lotto --game lotto --draws 100 --export csv --output out.csv --validate-only

# Debug logging
./open-lotto --game lotto --verbose DEBUG

# Update real-data snapshot
./open-lotto --game "Lotto 6aus49" --database-gewinnzahlen update
```

---

## Environment Variables

### `OPEN_LOTTO_PLUGIN_PATH`

Custom path to plugin directory.

```bash
export OPEN_LOTTO_PLUGIN_PATH=/custom/plugin/path
./open-lotto --list-games
```

- **Default:** `./plugins/` (relative to binary)

---

### `OPEN_LOTTO_LANG`

Set CLI locale/language.

```bash
export OPEN_LOTTO_LANG=en
./open-lotto --game lotto

export OPEN_LOTTO_LANG=fr
./open-lotto --game lotto
```

- **Supported:** `en` (English), `fr` (French)
- **Fallback:** `en` if not found

---

### `OPEN_LOTTO_ANALYTICS_GUI_TIMEOUT_MS`

Auto-close analytics GUI after N milliseconds (testing/automation).

```bash
export OPEN_LOTTO_ANALYTICS_GUI_TIMEOUT_MS=5000
./open-lotto --game lotto --frequency-distribution
```

- **Default:** 0 (no timeout, user must close manually)

---

## Config File

### Location

`~/.lottorc` (user home directory)

### Format

INI-style with `[defaults]` section.

### Example

```ini
[defaults]
game = Lotto 6aus49
draws = 10
gui = 2D
dark-mode = on
verbose = INFO
```

### Supported Keys

All CLI options (without dashes) can be set in the config file. CLI arguments always override config file values.

### Precedence

1. CLI arguments (highest priority)
2. Config file
3. Built-in defaults

---

## Exit Codes

| Code | Meaning |
|------|---------|
| **0** | Success |
| **1** | Configuration error, validation failure, or runtime error |

### Examples

```bash
./open-lotto --help
echo $?  # Outputs: 0

./open-lotto --invalid-option
echo $?  # Outputs: 1

./open-lotto --game nonexistent
echo $?  # Outputs: 1

./open-lotto --game lotto
echo $?  # Outputs: 0 (if successful)
```

---

## Troubleshooting

### "Unknown option" Error

Ensure the option is spelled correctly and uses `--` prefix.

```bash
# ✓ Correct
./open-lotto --help

# ✗ Wrong
./open-lotto -help
./open-lotto --hlep
```

### "game_name not provided"

Use `--game` with a valid game name from `--list-games`.

```bash
./open-lotto --list-games
./open-lotto --game "Lotto 6aus49"
```

### Plugin Not Found

Check `OPEN_LOTTO_PLUGIN_PATH` or ensure plugins are in `./plugins/`.

```bash
ls -la ./plugins/
export OPEN_LOTTO_PLUGIN_PATH=/path/to/plugins
```

### Analytics Show No Data

Ensure historical data is available via `--database-gewinnzahlen update` or provide `--historical-csv`.

```bash
./open-lotto --game lotto --database-gewinnzahlen update
./open-lotto --game lotto --frequency-distribution
```

---

## See Also

- [API_REFERENCE.md](API_REFERENCE.md) — C API documentation
- [ARCHITECTURE.md](ARCHITECTURE.md) — System design
- [DEVELOPER_API.md](DEVELOPER_API.md) — Embeddable C API
- [plugin-guide.md](plugin-guide.md) — Plugin development
