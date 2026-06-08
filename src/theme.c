/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "theme.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

/**
 * Detect dark mode from system environment variables
 */
int theme_detect_system_dark_mode(void)
{
    /* Check GTK_THEME for dark indicator */
    const char *gtk_theme = getenv("GTK_THEME");
    if (gtk_theme && strstr(gtk_theme, "dark"))
    {
        log_info("Dark mode detected from GTK_THEME");
        return 1;
    }

    /* Check QT_STYLE_OVERRIDE for dark indicator */
    const char *qt_style = getenv("QT_STYLE_OVERRIDE");
    if (qt_style && strstr(qt_style, "dark"))
    {
        log_info("Dark mode detected from QT_STYLE_OVERRIDE");
        return 1;
    }

    /* Check QT_PLATFORM_THEME */
    const char *qt_platform = getenv("QT_PLATFORM_THEME");
    if (qt_platform && strstr(qt_platform, "dark"))
    {
        log_info("Dark mode detected from QT_PLATFORM_THEME");
        return 1;
    }

    /* Future: could check dconf/gsettings on GNOME:
     * gsettings get org.gnome.desktop.interface gtk-application-prefer-dark-theme
     */

    log_info("No dark mode detected; using light theme");
    return 0;
}

/**
 * Get color palette for light or dark theme
 */
Theme theme_get(int dark_mode)
{
    Theme theme;

    if (dark_mode)
    {
        /* ========== DARK MODE ========== */
        theme.background = 0x1a1a1aff;     /* Dark charcoal background */
        theme.text_primary = 0xffffffff;   /* Pure white text */
        theme.text_secondary = 0xccccccff; /* Light gray for secondary text */
        theme.border = 0x444444ff;         /* Medium gray borders */
        theme.overlay_bg = 0x00000070;     /* Black overlay with ~44% opacity */
        theme.accent = 0x00d4ffff;         /* Cyan accent */
        theme.drum = 0x3a6ea8ff;           /* Steel blue drum — pops on dark bg */
        theme.drum_grid = 0x80b4e0ff;      /* Light blue wireframe */

        log_debug("Theme: DARK mode active");
    }
    else
    {
        /* ========== LIGHT MODE ========== */
        theme.background = 0xf5f5f5ff;     /* Very light gray background */
        theme.text_primary = 0x000000ff;   /* Pure black text */
        theme.text_secondary = 0x666666ff; /* Dark gray for secondary text */
        theme.border = 0xccccccff;         /* Light gray borders */
        theme.overlay_bg = 0xffffff99;     /* White overlay with ~60% opacity */
        theme.accent = 0x0066ffff;         /* Blue accent */
        theme.drum = 0x0d0d0dff;           /* Near-black drum — pops on light bg */
        theme.drum_grid = 0x4a4a4aff;      /* Dark gray wireframe */

        log_debug("Theme: LIGHT mode active");
    }

    return theme;
}
