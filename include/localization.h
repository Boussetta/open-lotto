/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef LOCALIZATION_H
#define LOCALIZATION_H

/**
 * @file localization.h
 * @brief Minimal localization key/value API for CLI and overlays.
 */

typedef enum
{
    LOCALIZE_DRAW = 0,
    LOCALIZE_MAIN,
    LOCALIZE_EXTRA,
    LOCALIZE_DRAWING_NUMBERS,
    LOCALIZE_COUNT
} LocalizationKey;

const char *localization_detect_locale(void);
/** @brief Lookup translated string for key under locale fallback rules. */
const char *localization_get(const char *locale, LocalizationKey key);

#endif
