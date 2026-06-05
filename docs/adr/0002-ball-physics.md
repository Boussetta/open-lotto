# ADR-0002: Ball Physics Model

**Date:** 2024-06-01
**Status:** Accepted

---

## Context

The 3D animated draw mode (`--gui 3D`) requires simulating lottery balls tumbling inside a
rotating drum. The simulation needs to feel physically plausible for a good user experience,
while remaining real-time on modest hardware with dozens of balls in play.

## Decision

A simplified rigid-body physics model is used, running per-frame with a fixed timestep:

**Collision detection:**
- Ball–ball collisions use bounding-sphere intersection (radius `BALL_RADIUS`).
- Ball–drum-shell collisions use a signed-distance check against the drum cylinder, transformed
  into the drum's local frame.

**Response:**
- Elastic collision impulses along the contact normal, scaled by a restitution coefficient.
- Rotational impulses computed via cross-product of tangent velocity and normal, stored as
  per-ball angular velocity (`rot_x/y/z`).

**Friction & rolling:**
- `apply_rolling_friction()` applies angular damping (`BALL_ANGULAR_DAMPING = 0.985`) and
  couples spin to linear velocity each frame.
- Shell contact uses `BALL_SHELL_FRICTION = 0.22` to bleed tangential velocity.

**Rendering:**
- Each ball is rendered with `glRotatef()` applied cumulatively from its angular velocity,
  producing visible tumbling.

An optional GPU compute path exists (env `LOTTO_GPU_COMPUTE`) for the main drum's broad-phase,
but is disabled by default; the extra drum (≤ 12 balls) always uses CPU.

## Consequences

### Positive
- Visually convincing tumbling and rolling with minimal code complexity.
- No external physics library dependency (no Bullet, ODE, etc.).
- Extra drum is cheap enough (12 balls) to avoid GPU overhead entirely.

### Negative
- Not physically accurate: no true rigid-body inertia tensors or constraint solving.
- Tunnelling can occur at very high ball speeds or very low framerates.
- Physics constants (`DRUM_RADIUS`, friction coefficients) were tuned empirically and are
  tightly coupled to the drum geometry.

### Neutral
- The dual-drum layout (main drum at `X = -390`, extra drum at `X = +360`) was chosen to
  keep both drums visible from a fixed camera at `Z = -808`.
