/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef GUI_SDL_H
#define GUI_SDL_H

#include "analytics.h"
#include "combogen.h"

/**
 * @file gui_sdl.h
 * @brief SDL2-based 2D renderers for draws and analytics.
 */

/*
 * Run the SDL2 GUI for the given game.
 * - window_title: e.g. "Lotto 6aus49"
 * - info: rules for the game
 *
 * The GUI internally calls generate_draw() with a callback that
 * receives the same DrawEvent sequence as --animate.
 */
void gui_run(const char *game_name, const LotteryInfo *info, int dark_mode);

/** @brief Render frequency-distribution analytics as 2D chart. */
int gui_render_frequency_2d(const char *title, const FrequencyReport *report, int dark_mode);
/** @brief Render stacked 2D overlay comparison for multiple frequency reports. */
int gui_render_frequency_overlay_stacked_2d(const char *title, const FrequencyReport *real_report,
                                            const FrequencyReport *sim_report,
                                            const FrequencyReport *rank10_report, int dark_mode);
/** @brief Render barometer analytics as 2D chart. */
int gui_render_barometer_2d(const char *title, const BarometerReport *report, int dark_mode);
/** @brief Render hot/cold analytics as 2D chart. */
int gui_render_hot_cold_2d(const char *title, const HotColdReport *report, int dark_mode);

#endif /* GUI_SDL_H */
