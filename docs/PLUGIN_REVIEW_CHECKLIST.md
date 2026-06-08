<!--
SPDX-FileCopyrightText: 2026 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# Plugin Submission Review Checklist

Use this checklist when reviewing a community plugin before merging or publishing.

## 1) Security and Safety

- [ ] No unsafe dynamic loading behavior outside Open-Lotto plugin ABI.
- [ ] No network download/execute behavior inside plugin code.
- [ ] No shell command execution (`system`, `popen`, equivalent).
- [ ] Input and range validation is explicit and bounded.
- [ ] Plugin does not write outside expected output paths.

## 2) ABI and Runtime Compatibility

- [ ] Exports required symbols: `plugin_get_info`, `plugin_get_name`, `plugin_draw`.
- [ ] Loads cleanly with `open-lotto-plugin-validator`.
- [ ] Is discoverable in `open-lotto --list-games`.
- [ ] Runs at least one successful draw command with exit code `0`.
- [ ] Documents minimum Open-Lotto version tested.

## 3) Draw Rules Correctness

- [ ] `main_count/main_min/main_max` match official game rules.
- [ ] `extra_count/extra_min/extra_max` match official game rules.
- [ ] No duplicate numbers inside one draw domain.
- [ ] Callback milestones are respected when custom draw logic is used.

## 4) Code Quality and Tests

- [ ] Code is formatted with project `clang-format` style.
- [ ] No new warnings in clean build.
- [ ] Includes at least one smoke test command in PR description.
- [ ] Optional: add targeted test case if non-trivial custom logic is introduced.

## 5) Licensing and Metadata

- [ ] SPDX header exists in plugin source file.
- [ ] License is compatible with repository policy.
- [ ] Maintainer contact and plugin metadata are documented.
- [ ] Public data source (if any) is declared and attributed.

## 6) Submission Package Contents

- [ ] Plugin source file (or release artifact + source reference).
- [ ] Build instructions.
- [ ] Validation output snippet.
- [ ] Example command for end users.

## Reviewer Sign-Off

- [ ] Security review complete
- [ ] Functional validation complete
- [ ] Documentation complete
- [ ] Approved for merge/distribution
