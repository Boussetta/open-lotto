/**
 * @file theme.h
 * @brief Theme management (light/dark mode color schemes)
 */

#ifndef THEME_H
#define THEME_H

#include <stdint.h>

/**
 * @brief Color theme structure with RGBA colors for UI elements
 */
typedef struct
{
    uint32_t background;      /**< Main background color (RGBA) */
    uint32_t text_primary;    /**< Primary text color */
    uint32_t text_secondary;  /**< Secondary/dim text */
    uint32_t border;          /**< UI border color */
    uint32_t overlay_bg;      /**< Debug overlay background */
    uint32_t accent;          /**< Highlight/accent color */
    uint32_t drum;            /**< Drum shell fill color */
    uint32_t drum_grid;       /**< Drum wireframe/grid lines color */
} Theme;

/**
 * @brief Detect system dark mode preference from environment
 *
 * Checks GTK_THEME, QT_STYLE_OVERRIDE, and other environment variables
 * to determine if the system is using dark mode.
 *
 * @return 1 if dark mode detected, 0 if light mode or unknown
 */
int theme_detect_system_dark_mode(void);

/**
 * @brief Get color theme for the specified mode
 *
 * Returns a complete color palette suitable for rendering the UI
 * in either light or dark mode.
 *
 * @param dark_mode 1 for dark theme, 0 for light theme
 * @return Theme struct with RGBA colors (0xRRGGBBAA format)
 */
Theme theme_get(int dark_mode);

#endif /* THEME_H */
