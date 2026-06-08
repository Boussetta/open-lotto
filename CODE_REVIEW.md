<!--
SPDX-FileCopyrightText: 2025 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# Code Review Guidelines

This document outlines the code review process and expectations for the open-lotto project.

## Overview

All changes to the `main` branch must go through a pull request (PR) and receive at least **1 approval** from a code owner before merging. This ensures code quality, prevents regressions, and maintains project consistency.

## Branch Workflow

### Creating a Feature Branch

1. **Create a feature branch** for each issue:
   ```bash
   git checkout -b feature/N-description
   # where N is the issue number
   ```

2. **Commit regularly** with clear, descriptive messages:
   ```bash
   git commit -m "feat: brief description"
   # or: "fix:", "docs:", "test:", "refactor:", etc.
   ```

3. **Run pre-commit checks** before pushing:
   ```bash
   scripts/run_format_check.sh
   scripts/run_cppcheck.sh
   scripts/run_clang_tidy.sh
   scripts/run_sanitizers.sh
   ```

4. **Push to origin**:
   ```bash
   git push origin feature/N-description
   ```

### Opening a Pull Request

1. **Create a PR** on GitHub with a clear title and description
2. **Link the issue** in the PR body: `Closes #N`
3. **Fill out the PR template** checklist completely
4. **Ensure all CI checks pass** before requesting review

## Review Process

### For PR Authors

- **Use the PR template** (auto-populated when creating a PR)
- **Keep PRs focused** — one feature/fix per PR
- **Keep PRs reasonably sized** — easier to review, catch issues faster
- **Respond to feedback promptly** and mark resolved conversations
- **Don't approve your own PR** — wait for code owner review

### For Reviewers

1. **Check the PR template completeness**:
   - Is the issue properly linked?
   - Are all checklist items marked?
   - Do code quality checks pass in CI?

2. **Review code changes**:
   - Does the change match the issue requirements?
   - Is the implementation correct and efficient?
   - Are edge cases handled?
   - Is the code maintainable and well-documented?

3. **Verify testing**:
   - Are new features tested?
   - Do all tests pass locally and in CI?
   - Is coverage maintained/improved?

4. **Check documentation**:
   - Are changes documented (code comments, man page, README)?
   - Are API changes reflected in headers?
   - Is any user-facing behavior described?

5. **Request changes or approve**:
   - **Request Changes**: Require author to address issues
   - **Approve**: PR is ready to merge

## Automated Checks

All PRs must pass the following automated quality gates:

- **build-and-test**: Compilation and unit tests
- **format-check**: Code formatting (clang-format)
- **sanitizers**: Memory safety (AddressSanitizer, UBSanitizer)
- **performance**: Benchmarking and regression detection

**Branch protection enforces:**
- ✓ All status checks must pass
- ✓ Branches must be up-to-date before merging (strict mode)
- ✓ At least 1 approval required
- ✓ Stale reviews are dismissed if new commits added

## Merging to Main

Once approved and all checks pass:

1. **Squash or merge** (per project preference):
   - **Squash**: Combines all commits into one (cleaner history)
   - **Regular merge**: Preserves commit history

2. **Automatic deletion** of feature branch after merge (recommended)

3. **Verify** the merge completed on GitHub

## Code Review Expectations

### Code Quality

- Code follows [Linux kernel coding style](https://www.kernel.org/doc/html/latest/process/coding-style.html) (C99)
- Clear variable and function names
- Comments on complex logic
- No code duplication
- Functions have single responsibility

### Testing

- Unit tests for core functionality
- CLI validation tests for user-facing features
- No regressions in existing tests
- New features include tests

### Documentation

- API changes documented in header files
- User-facing changes documented in man page
- Complex algorithms explained in code comments
- Architecture decisions recorded in docs/adr/

### Performance

- No performance regressions (5% threshold)
- Memory usage is reasonable
- Benchmarks run cleanly

## Common Review Feedback

### Code Quality Issues

```
❌ "This function is too complex"
✓ "Please break this into smaller functions with single responsibility"

❌ "Bad naming"
✓ "The variable 'x' should be 'drum_radius' for clarity"
```

### Testing Issues

```
❌ "Missing tests"
✓ "Please add a test case for the edge case when count == 0"
```

### Performance Issues

```
❌ "This is slow"
✓ "Benchmark shows 15% regression in X metric. Can we optimize by using Y approach?"
```

## Questions?

Refer to:
- [CONTRIBUTING.md](CONTRIBUTING.md) — General contribution guidelines
- [docs/adr/](docs/adr/) — Architecture decision records
- [GitHub discussions](https://github.com/Boussetta/open-lotto/discussions) — Community Q&A
