<!--
SPDX-FileCopyrightText: 2026 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# Seed Search Architecture and Performance Model (v0.5.0)

## Goals
- Bound runtime for practical ranges
- Keep deterministic ordering and tie-breaking
- Support parallel execution with stable final ranking

## Search Loop (Draft)
1. Precompute real-period metrics
2. Enumerate candidate seeds in bounded range
3. Simulate N draws per candidate
4. Compute fit score
5. Track top-k

## Stop Conditions (Draft)
- max evaluations
- timeout
- explicit interruption

## Workload Profiles (Draft)
- small
- medium
- large
