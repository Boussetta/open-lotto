<!--
SPDX-FileCopyrightText: 2025 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# IDE Setup Guide

This document describes recommended setup for VS Code, CLion, and IntelliJ IDEA.

## VS Code

### Debugging with GDB

Use the ready-made launch configurations from `.vscode/launch.json`:

- `Debug open-lotto (GDB)`
- `Debug plugin validator (GDB)`
- `Attach to running open-lotto process`

Example runtime arguments for `open-lotto`:

```bash
--game "Lotto 6aus49" --draws 1 --reload-plugin
```

### Attach to a running process

1. Start the app from a shell:

```bash
./build/open-lotto --game "Lotto 6aus49" --animate
```

2. In VS Code, run `Attach to running open-lotto process` and pick the process from the list.

## CLion

1. Open the project root containing `CMakeLists.txt`.
2. Create a CMake profile:

```text
Name: Debug
Build type: Debug
Build directory: cmake-build-debug
CMake options: -DBUILD_TESTING=ON -DOPEN_LOTTO_USE_CCACHE=ON
```

3. Create an Application run/debug configuration:

```text
Executable: <project>/cmake-build-debug/open-lotto
Working directory: <project>
Program arguments: --game "Lotto 6aus49" --draws 1
```

4. Optional plugin validation config:

```text
Executable: <project>/cmake-build-debug/open-lotto-plugin-validator
Program arguments: <project>/cmake-build-debug/plugins/liblotto.so
```

### Attach in CLion

- Run the target normally.
- Use `Run | Attach to Process...` and select `open-lotto`.

## IntelliJ IDEA

IntelliJ IDEA can be used with the JetBrains C/C++ plugin.

1. Install the C/C++ plugin in IntelliJ IDEA.
2. Open the project root as a CMake project.
3. Use the same CMake profile and run/debug settings as the CLion steps above.

If your workflow is mostly C/C++ debug and profiling, CLion is usually the smoother option.

## ccache Tips

Check cache stats:

```bash
ccache -s
```

Reset cache stats:

```bash
ccache -z
```

Clear cache:

```bash
ccache -C
```

## Build Configure Profiling (google-trace)

Generate a CMake configure trace:

```bash
./scripts/configure.sh --profile-configure
```

This requires a CMake version that supports `--profiling-format`.

Custom output path:

```bash
./scripts/configure.sh --profile-configure --profile-output /tmp/open-lotto-cmake-trace.json
```

Open the generated JSON trace with Chrome tracing tools (`chrome://tracing`) or Perfetto.
