/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef GUI_SDL_H
#define GUI_SDL_H

#include "combogen.h"

/*
 * Run the SDL2 GUI for the given game.
 * - window_title: e.g. "Lotto 6aus49"
 * - info: rules for the game
 *
 * The GUI internally calls generate_draw() with a callback that
 * receives the same DrawEvent sequence as --animate.
 */
void gui_run(const char *game_name, const LotteryInfo *info, int dark_mode);

#endif /* GUI_SDL_H */
