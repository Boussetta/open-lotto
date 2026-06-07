# Plugin Marketplace Roadmap

This document captures the future-facing shape of plugin packaging and distribution for Open-Lotto.
It is intentionally lightweight for now and exists so plugin authors can target a stable direction
before a real registry is implemented.

## Status

- No online registry exists yet.
- Plugins are currently distributed as source or shared objects.
- Validation is local via `open-lotto-plugin-validator`.

## Proposed Package Shape

Each plugin package should eventually include:

- Shared object: `lib<plugin>.so`
- Source entry point: `<plugin>.c`
- Human-readable metadata: name, version, author, homepage, license
- Validation output captured from `open-lotto-plugin-validator`
- Basic compatibility note for the Open-Lotto version it targets

## Suggested Metadata Fields

Until the registry exists, keep these values in the repository README or release notes:

- Display name returned by `plugin_get_name()`
- Internal slug used for file names and release assets
- Main number rules
- Extra number rules
- Minimum tested Open-Lotto version
- License and maintainer contact

## Submission Checklist

- Build the plugin in a clean checkout.
- Run `open-lotto-plugin-validator` on the final shared object.
- Run `open-lotto --list-games` with the plugin on the search path.
- Include one smoke-test command in the release notes.
- Document whether the plugin depends on the host's exported `generate_draw` helper or ships its own draw logic.

## Future Work

- Signed plugin manifests
- Versioned compatibility metadata
- Searchable plugin index
- Install and update commands in the CLI