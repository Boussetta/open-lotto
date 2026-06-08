<!--
SPDX-FileCopyrightText: 2026 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# Localization String Table

Open-Lotto exposes a small translation-ready string table for CLI labels.

## Supported Locales

- `en` (default)
- `fr` (sample community locale)

If the requested locale is not supported, Open-Lotto falls back to English.

## Locale Selection

Locale is selected in this order:

1. `OPEN_LOTTO_LANG` environment variable
2. `LANG` environment variable
3. default `en`

Examples:

```bash
OPEN_LOTTO_LANG=fr ./build/open-lotto --game "Lotto 6aus49" --draws 1
OPEN_LOTTO_LANG=en ./build/open-lotto --game "Lotto 6aus49" --draws 1
```

## Current Translation Keys

- `LOCALIZE_DRAW`
- `LOCALIZE_MAIN`
- `LOCALIZE_EXTRA`
- `LOCALIZE_DRAWING_NUMBERS`

Defined in `include/localization.h` and implemented in `src/localization.c`.

## Adding a New Locale

1. Add a locale table entry in `src/localization.c`.
2. Provide all `LOCALIZE_*` keys.
3. Add or update localization tests in `tests/test_localization.c`.
4. Validate with `ctest --output-on-failure`.

## Contributor Notes

- Keep strings ASCII unless the project standard changes.
- Maintain concise wording for terminal readability.
- Do not remove English entries; they are the fallback baseline.
