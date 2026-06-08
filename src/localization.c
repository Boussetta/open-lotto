/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "localization.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const char *locale;
    const char *entries[LOCALIZE_COUNT];
} LocaleTable;

static const LocaleTable TABLES[] = {
    {
        "en",
        {
            "Draw",
            "Main",
            "Extra",
            "Drawing numbers...",
        },
    },
    {
        "fr",
        {
            "Tirage",
            "Principaux",
            "Supplementaires",
            "Tirage en cours...",
        },
    },
};

static const LocaleTable *find_table(const char *locale)
{
    size_t table_count = sizeof(TABLES) / sizeof(TABLES[0]);
    for (size_t i = 0; i < table_count; i++)
    {
        if (strcmp(TABLES[i].locale, locale) == 0)
            return &TABLES[i];
    }
    return NULL;
}

static int locale_starts_with(const char *locale, const char *prefix)
{
    size_t i = 0;
    while (prefix[i] != '\0')
    {
        if (locale[i] == '\0')
            return 0;
        if ((char)tolower((unsigned char)locale[i]) != prefix[i])
            return 0;
        i++;
    }
    return 1;
}

const char *localization_detect_locale(void)
{
    const char *raw = getenv("OPEN_LOTTO_LANG");
    if (!raw || raw[0] == '\0')
        raw = getenv("LANG");

    if (!raw || raw[0] == '\0')
        return "en";

    if (locale_starts_with(raw, "fr"))
        return "fr";

    return "en";
}

const char *localization_get(const char *locale, LocalizationKey key)
{
    const LocaleTable *fallback = find_table("en");
    if (!fallback || key < 0 || key >= LOCALIZE_COUNT)
        return "";

    const LocaleTable *table = locale ? find_table(locale) : NULL;
    if (!table)
        table = fallback;

    const char *candidate = table->entries[key];
    if (candidate && candidate[0] != '\0')
        return candidate;

    return fallback->entries[key];
}
