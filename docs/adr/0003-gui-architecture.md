<!--
SPDX-FileCopyrightText: 2025 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# ADR-0003: GUI Rendering Architecture

**Date:** 2024-06-01
**Status:** Accepted

---

## Context

Open-Lotto supports two GUI modes: a 2D terminal/SDL mode and a 3D OpenGL drum animation. The
choice of rendering backend and its integration with the rest of the application required a
deliberate architectural decision to keep the core engine independent of the display layer.

## Decision

Two independent rendering backends are implemented behind a unified CLI flag (`--gui`):

| Flag | Backend | File |
|------|---------|------|
| `--gui 2D` (default) | SDL2 + SDL_ttf | `src/gui_sdl.c` |
| `--gui 3D` | OpenGL (fixed-function) + SDL2 window | `src/gui_opengl.c` |

**GUI separation:** `main.c` selects the backend at startup; neither backend includes the other.
The draw result (`LotteryResult`) is produced by the plugin independently of the GUI and passed
in.

**OpenGL fixed-function pipeline:** The 3D mode deliberately avoids GLSL shaders, GLU, and
external math libraries to minimise build dependencies. All geometry (drum shell, balls) is
rendered with `glBegin`/`glEnd` and matrix stack operations.

**DrumInstance struct:** Each drum (main and extra) is encapsulated in a `DrumInstance` that
holds all physics state, animation phase, ball data, and textures. `GuiState3D` owns pointers
to both instances and all shared SDL/GL/font state.

**State machine phases per drum:**
`FALLING → ROTATING → STOPPING → PICK_PAUSE` (repeated per pick) → `DRAW_COMPLETE`

The extra drum's state machine begins in a `waiting = 1` state and is unblocked only when the
main drum reaches `DRAW_COMPLETE`.

## Consequences

### Positive
- Core engine (plugin, combogen, RNG) has zero GUI dependency.
- Fixed-function OpenGL requires no shader compilation step and works on older drivers.
- `DrumInstance` encapsulation makes the dual-drum layout straightforward to extend (e.g.
  triple drum for a future game).

### Negative
- Fixed-function OpenGL is deprecated in OpenGL 3.2+ core profiles; switching to a core
  profile would require rewriting the renderer with VBOs and shaders.
- `gui_opengl.c` is a large, monolithic file; a future refactor may split physics, state
  machine, and rendering into separate translation units.
- The 2D SDL backend and 3D OpenGL backend duplicate some display logic (font rendering,
  result layout).

### Neutral
- SDL2 is used as the platform layer for both backends (window, events, timing), so it is
  always a required dependency.
