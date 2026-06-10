<!--
SPDX-FileCopyrightText: 2026 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# Seed Search Architecture and Performance Model (v0.5.0)

## Goals
- Bound runtime for practical ranges
- Keep deterministic ordering and tie-breaking
- Support parallel execution with stable final ranking

## Complexity Model
Let:
- $D$ = draws in selected period
- $S$ = number of evaluated seeds
- $C_{sim}$ = average cost of one simulated draw
- $C_{score}$ = average cost of one score evaluation

Estimated time:

$$
T \approx S \cdot (D \cdot C_{sim} + C_{score})
$$

Estimated memory in streaming mode:

$$
M \approx O(R + K)
$$

Where $R$ is the number range size and $K$ is top-k candidate storage.

## Search Loop (v1)
1. Precompute real-period metrics once.
2. Enumerate candidate seeds in ascending order.
3. For each seed, simulate $D$ draws and compute score components.
4. Apply early reject if partial score already exceeds current best.
5. Keep top-k candidates and best seed under deterministic tie-break.

Pseudocode:

```text
best <- INF
for seed in [seed_start .. seed_end]:
  if eval_count >= max_evals: stop
  if now_ms >= deadline_ms: stop
  score <- evaluate_seed(seed, D, profile)
  if score < best: best <- score
  update_top_k(seed, score)
```

## Stop Conditions
- `max-evals` reached.
- `timeout-ms` reached.
- convergence stagnation for optional heuristic mode.
- explicit interruption from caller.

## Parallel Execution Model
- Partition seed range into deterministic chunks.
- Each worker computes local top-k.
- Final reduction merges by deterministic comparator:
  1. total score
  2. frequency component score
  3. lower seed

## Workload Profiles
- small:
  - period draws <= 64
  - seeds <= 50k
  - target runtime <= 2 s (4 threads)
- medium:
  - period draws <= 256
  - seeds <= 500k
  - target runtime <= 20 s (4 threads)
- large:
  - period draws <= 1024
  - seeds <= 2M
  - target runtime <= 180 s (8 threads)

## Recommended Safe Defaults
- `max-evals`: 100000
- `threads`: 1
- `top`: 10
- `timeout-ms`: unset (explicit opt-in)
- `score-profile`: balanced
