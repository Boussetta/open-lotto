<!--
SPDX-FileCopyrightText: 2026 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# Entropy Audit (NIST SP 800-90B Aligned)

This document defines a repeatable entropy audit workflow for Open-Lotto random seeding.

## Scope

The audit covers the seed pipeline implemented in `src/random_seed.c`:

- Linux kernel entropy (`getrandom`)
- CPU entropy (`RDRAND`) when available
- Monotonic clock jitter
- XOR mixing and expansion into generator state

## Threat Model and Assumptions

- Open-Lotto is a lottery simulator, not a cryptographic key generator.
- Entropy source quality can vary across hardware and virtualized environments.
- If one source degrades, the other sources should still provide non-trivial uncertainty.

## Test Strategy

NIST SP 800-90B recommends estimating min-entropy on raw noise sources. For Open-Lotto,
we test the collected seed material before final mixing.

### Datasets

Collect a minimum of:

- 1,000,000 samples for startup entropy snapshots
- 1,000,000 samples for per-draw derived seeds

Each sample should be stored as hexadecimal bytes, one sample per line.

### Statistical Checks

Run these checks on each dataset:

- Most Common Value estimate
- Collision estimate
- Markov estimate
- Compression estimate

Use the minimum resulting bound as the conservative min-entropy estimate.

## Acceptance Criteria

A run is considered passing when all conditions hold:

- No source is constantly zero across the sample set
- Estimated min-entropy is above 0.5 bits per output bit for each source stream
- Mixed output stream min-entropy is above 0.75 bits per output bit
- No sanitizer or UB violations occur during capture build

If any condition fails, treat this as a release blocker for `v1.0.0`.

## Reproducible Audit Procedure

1. Build with sanitizers enabled.
2. Capture raw source outputs and mixed seeds into dataset files.
3. Run NIST 800-90B estimators on captured files.
4. Save summary report under `docs/entropy-audit/` with commit hash and platform.

Suggested metadata for each report:

- Commit SHA
- CPU model and OS
- Compiler and flags
- Dataset size
- Estimator outputs
- Final pass/fail decision

## CI Integration Guidance

A lightweight CI gate should:

- Verify dataset capture utility builds and runs
- Publish captured sample artifacts
- Fail on malformed or empty datasets

Full 1,000,000-sample statistical runs may execute in scheduled jobs due to runtime cost.

## Audit Cadence

Run the full audit:

- Before each minor and major release
- After changes to `src/random_seed.c`
- After portability updates affecting entropy APIs

## References

- NIST SP 800-90B: Recommendation for the Entropy Sources Used for Random Bit Generation
- Open-Lotto Security Policy: `SECURITY.md`
