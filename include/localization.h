/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef LOCALIZATION_H
#define LOCALIZATION_H

typedef enum
{
    LOCALIZE_DRAW = 0,
    LOCALIZE_MAIN,
    LOCALIZE_EXTRA,
    LOCALIZE_DRAWING_NUMBERS,
    LOCALIZE_COUNT
} LocalizationKey;

const char *localization_detect_locale(void);
const char *localization_get(const char *locale, LocalizationKey key);

#endif
