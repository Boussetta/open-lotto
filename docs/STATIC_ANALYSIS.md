<!--
SPDX-FileCopyrightText: 2025 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# Static Analysis Guide

This guide explains how to use and suppress findings from the static analysis tools integrated into Open-Lotto's CI/CD pipeline.

## Tools Overview

Open-Lotto uses two complementary static analysis tools:

1. **Cppcheck** — Detects common C/C++ coding errors and style issues
2. **Clang-Tidy** — Provides LLVM-based analysis with refactoring suggestions

## Running Locally

### Cppcheck

Run cppcheck analysis on all source files:

```bash
./scripts/run_cppcheck.sh
```

**Output:**
- `error` — Code problems that are very likely bugs
- `warning` — Code problems that are suspicious
- `style` — Code style issues

**Exit Code:**
- `0` — No issues found
- `1` — Issues found (treated as errors by CI)

### Clang-Tidy

Run clang-tidy on all source files:

```bash
./scripts/run_clang_tidy.sh
```

**Output:**
- One line per issue with file, line, column, message
- Organized by check name (e.g., `bugprone-*`, `cert-*`)

**Note:** Clang-tidy requires a compile commands database, which is generated automatically by the script.

### Running Both Tools

Run both tools in sequence:

```bash
./scripts/run_format_check.sh      # Check formatting first
./scripts/run_cppcheck.sh          # Static analysis
./scripts/run_clang_tidy.sh        # LLVM analysis
```

This is the same order as the CI pipeline and local pre-commit hook.

## CI Integration

The static-analysis job runs on every push and pull request:

```yaml
static-analysis:
  runs-on: ubuntu-latest
  steps:
    - name: Run cppcheck
      run: ./scripts/run_cppcheck.sh
    - name: Run clang-tidy
      run: ./scripts/run_clang_tidy.sh
```

**On Failure:**
- Job fails if either tool reports errors
- GitHub displays error message in PR checks
- Developers must fix issues or suppress false positives before merge

## Suppressing False Positives

### Cppcheck Suppressions

Suppress specific cppcheck issues using the `--suppress` flag in `run_cppcheck.sh`:

#### Global Suppressions

Edit `scripts/run_cppcheck.sh`:

```bash
cppcheck \
  --enable=all \
  --suppress=unmatchedSuppression \
  --suppress=missingIncludeSystem \
  --suppress=unusedFunction \           # Example: suppress globally
  ...
```

**Common Suppressions:**

| Check | Reason |
|-------|--------|
| `unmatchedSuppression` | Suppress unused suppression rules |
| `missingIncludeSystem` | Don't warn about system includes |
| `missingInclude` | Don't warn about missing includes |
| `unusedFunction` | Suppress unused function warnings (static functions in headers) |
| `constParameterPointer` | Suppress const parameter suggestions |
| `normalCheckLevelMaxBranches` | Don't warn about complex functions |

#### Inline Suppressions

Suppress specific cppcheck findings in source code:

```c
// cppcheck-suppress unusedVariable
int unused_for_now = 42;

// Suppress multiple checks
// cppcheck-suppress unusedFunction,unusedVariable
static int helper_function() {
    int x = 5;
    return 0;
}
```

**Syntax:**
- Comment: `// cppcheck-suppress <check1>,<check2>`
- Multi-line: `/* cppcheck-suppress <check1> */ code`

**Example from `src/config.c`:**

```c
// cppcheck-suppress staticFunction
static void load_config_from_file(const char* path) {
    // ...
}
```

This tells cppcheck to not warn that `load_config_from_file` is unused (it's called via function pointer).

### Clang-Tidy Suppressions

Suppress specific clang-tidy findings using inline comments or `.clang-tidy` config.

#### Global Suppressions via `.clang-tidy`

Edit `.clang-tidy` to disable specific checks:

```yaml
Checks: '-*,
         clang-analyzer-*,
         bugprone-*,
         cert-*,
         misc-*,
         performance-*,
         portability-*,
         -cert-msc50-cpp,
         -cert-msc51-cpp,
         -performance-avoid-endl,
         -portability-simd-intrinsics'
```

**To suppress a check:**
- Prepend `-` to the check name
- Example: `-bugprone-branch-clone` suppresses the branch clone check

**Common Suppressions:**

| Check | Reason |
|-------|--------|
| `clang-analyzer-deadcode.DeadStores` | False positives in physics code |
| `bugprone-easily-swappable-parameters` | Too noisy for style |
| `cert-msc50-cpp`, `cert-msc51-cpp` | Rand/srand (using PCG32 instead) |
| `performance-avoid-endl` | Code style preference |
| `portability-simd-intrinsics` | RDRAND CPU instruction (x86-specific) |

Current suppressions in `.clang-tidy`:

```yaml
AnalyzeTemporaryDtors: false
Checks: '-*,clang-analyzer-*,bugprone-*,cert-*,misc-*,performance-*,portability-*,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling,-clang-analyzer-optin.core.ExpressionEvalOrder,-clang-analyzer-deadcode.DeadStores,-bugprone-easily-swappable-parameters,-cert-msc50-cpp,-cert-msc51-cpp,-performance-avoid-endl,-portability-simd-intrinsics'
```

#### Inline Suppressions

Suppress specific findings in source code:

```c
// NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
int x = expensive_calculation();

// NOLINT(bugprone-branch-clone)
if (x) {
    result = 1;
} else {
    result = 1;  // Intentional duplicate
}
```

**Syntax:**
- Single line: `// NOLINT(check-name)`
- Next line: `// NOLINTNEXTLINE(check-name)`
- Multiple checks: `// NOLINT(check1,check2)`

**Example from `src/gui_opengl.c`:**

```c
// NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
static void update_physics_step(struct DrumInstance* drum) {
    // Physics code that clang-analyzer flags incorrectly
}
```

## CI Failure Scenarios

### Scenario 1: True Positive Found

**CI Output:**
```
error: something is wrong here (src/main.c:42)
```

**Action:** Fix the issue in source code.

```bash
# Run locally to verify
./scripts/run_cppcheck.sh
# Fix the issue
git add src/main.c
git commit -m "fix: resolve cppcheck error at line 42"
git push
```

### Scenario 2: False Positive from Cppcheck

**CI Output:**
```
error: unused variable 'x' (src/file.c:10)
```

**Verification:** The variable is actually used.

**Action:** Add inline suppression:

```c
// cppcheck-suppress unusedVariable
int x = 5;
// x is used later in calculation
int result = x * 2;
```

**Or** add global suppression if it's a widespread issue:

```bash
# Edit scripts/run_cppcheck.sh
--suppress=unusedVariable:src/file.c
```

### Scenario 3: False Positive from Clang-Tidy

**CI Output:**
```
src/file.c:15:5: warning: ... [bugprone-branch-clone]
```

**Verification:** The code is intentionally duplicated.

**Action:** Add inline suppression:

```c
// NOLINTNEXTLINE(bugprone-branch-clone)
if (condition) {
    action_1();
} else {
    action_1();  // Intentional — both branches do the same
}
```

**Or** disable the check globally in `.clang-tidy` if it's too noisy:

```yaml
Checks: '-*,...,-bugprone-branch-clone'
```

## Best Practices

### 1. Suppress Only False Positives

**Good:** Suppress a specific instance with inline comment explaining why.

```c
// NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
int x = calculate();  // Used in conditional below
if (x > 0) { ... }
```

**Bad:** Suppress entire check globally without understanding the issue.

```yaml
# Don't do this!
Checks: '-*,bugprone-*'  # Disabled everything
```

### 2. Use Specific Suppression Syntax

**Good:**
```c
// cppcheck-suppress unusedVariable
// NOLINTNEXTLINE(check-name)
```

**Bad:**
```c
// ignore this
// nocheck
```

### 3. Document Why Suppression Exists

Add a comment explaining the false positive:

```c
// NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
// The assignment is dead in this path but required by the API contract
int result = 0;
```

### 4. Keep Suppressions Local

Prefer inline suppressions to global ones:
- Inline: Affects only the problematic code
- Global: Silences all instances of the check

### 5. Review Suppressions Regularly

Periodically review suppressions to ensure they're still necessary:

```bash
# Find all suppressions
grep -r "cppcheck-suppress\|NOLINT" src/ plugins/ tests/
```

## Configuring IDE to Match CI

### VS Code with Clang-Tidy Extension

Install the "Clang-Tidy" extension and ensure it uses the project's `.clang-tidy`:

```json
// .vscode/settings.json
{
  "clangTidy.executable": "clang-tidy",
  "clangTidy.checks": "-*,clang-analyzer-*,bugprone-*,...",
  "clangTidy.buildPath": "${workspaceFolder}/build-tidy",
  "clangTidy.compileCommandsDir": "${workspaceFolder}/build-tidy"
}
```

### Command-Line Verification

Before pushing, verify both tools pass locally:

```bash
./scripts/run_format_check.sh && \
./scripts/run_cppcheck.sh && \
./scripts/run_clang_tidy.sh && \
echo "All checks passed!"
```

## Troubleshooting

### "cppcheck: command not found"

**Fix:**
```bash
sudo apt-get install cppcheck
```

### "clang-tidy: command not found"

**Fix:**
```bash
sudo apt-get install clang-tidy
```

### Cppcheck Fails with Unmatched Suppressions

**Error:**
```
error: Unmatched suppression: unusedFunction [unmatchedSuppression]
```

**Cause:** Suppression name doesn't match any actual finding.

**Fix:**
1. Verify the check name (run without suppression to see actual name)
2. Remove the unused suppression from `run_cppcheck.sh`
3. Or set `--suppress=unmatchedSuppression` to ignore this warning

### Clang-Tidy Hangs or Times Out

**Cause:** First run generates compile database (slow).

**Fix:**
```bash
# Pre-generate compile commands
cmake -S . -B build-tidy \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Then run clang-tidy (cached)
./scripts/run_clang_tidy.sh
```

## CI Logs

View static analysis results in GitHub:

1. Go to **Pull Request → Checks**
2. Click **static-analysis** job
3. Expand **Run cppcheck** or **Run clang-tidy** steps
4. View full output with line numbers

## References

- **Cppcheck Manual:** https://cppcheck.sourceforge.io/
- **Clang-Tidy Checks:** https://clang.llvm.org/extra/clang-tidy/checks/list.html
- **Project Configuration:** `.clang-tidy`, `scripts/run_cppcheck.sh`, `scripts/run_clang_tidy.sh`
