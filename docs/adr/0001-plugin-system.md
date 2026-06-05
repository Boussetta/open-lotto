# ADR-0001: Plugin-Based Game Architecture

**Date:** 2024-01-01
**Status:** Accepted

---

## Context

The project needed to support multiple lottery games (e.g. Lotto 6aus49, EuroJackpot) without
hardcoding game rules into the core engine. New games should be addable without recompiling the
main binary, and the engine should remain agnostic of any specific lottery's configuration.

## Decision

Each lottery game is implemented as a dynamically loaded shared library (`.so`) that exports
three C symbols defined by `lottery_plugin.h`:

```c
const LotteryInfo *plugin_get_info(void);
const char        *plugin_get_name(void);
void               plugin_draw(LotteryResult *out, draw_event_callback cb);
```

The core `plugin_loader` scans a `plugins/` directory at startup, opens each `.so` with
`dlopen`, resolves these symbols, and registers the game in a central `plugin_registry`.
The main binary only depends on this interface; game-specific logic lives entirely in the
plugin.

## Consequences

### Positive
- New games can be added by writing a single `.c` file — no core changes required.
- The engine binary is decoupled from game rules, improving maintainability.
- Games can be distributed as standalone `.so` files.
- Unit-testing plugins is straightforward (stub implementations in `tests/stub_plugin.c`).

### Negative
- Requires `dlopen` / POSIX dynamic linking, limiting portability to systems without it.
- Symbol resolution errors only surface at runtime, not at compile time.
- All plugins must be compiled and placed in the correct directory before the binary can list games.

### Neutral
- The plugin interface (`lottery_plugin.h`) is the public ABI; any change to it is a breaking
  change for all existing plugins.
