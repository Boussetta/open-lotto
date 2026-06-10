<!--
SPDX-FileCopyrightText: 2026 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# v0.5.0 Announcement Copy (Draft)

## Release Note Short Copy
open-lotto v0.5.0 introduces seed calibration: a new way to tune simulator seeds so synthetic draws best match real historical draw behavior over configurable periods.

## Release Note Long Copy
v0.5.0 adds two calibration workflows:
- Seed trajectory: compute best-fit simulator seeds across windows (for example every 3 months).
- Closest seed: search a bounded seed range for the best fit on one selected period.

This release emphasizes reproducibility and transparency. Results are deterministic for the same inputs and include diagnostics such as top-k candidates and score gaps.

Important: these are best-fit simulator seeds, not recovered real-world lottery machine seeds.

## README Snippet
Try closest seed on a selected period:

`./open-lotto --game "Lotto 6aus49" --closest-seed --from 2026-01-01 --to 2026-06-10 --seed-start 0 --seed-end 500000 --format json`

## Social Post Draft
New in open-lotto v0.5.0: seed calibration.
Now you can fit simulator seeds to historical draw statistics, inspect top candidates, and track best-fit seed trajectories over time windows.
Deterministic outputs, clear diagnostics, and transparent limitations.

## FAQ Draft
1. Does this recover the real lottery seed?
No. It finds best-fit simulator seeds under our scoring model.

2. Does this predict future winning numbers?
No. It is a retrospective calibration and analysis tool.

3. Why provide top-k seeds instead of one value?
Multiple seeds can score similarly. Top-k improves transparency.

4. Why can the best seed change between windows?
Window statistics differ, so the optimization target changes.

5. Why does the score gap matter?
A larger gap usually indicates a more clearly separated best-fit candidate.
