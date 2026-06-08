/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef GUI_OPENGL_H
#define GUI_OPENGL_H

#include "combogen.h"

/*
 * Run the OpenGL 3D GUI for the given game.
 * - window_title: e.g. "Lotto 6aus49"
 * - info: rules for the game
 *
 * The GUI internally calls generate_draw() with a callback that
 * receives the same DrawEvent sequence as --animate.
 *
 * Uses SDL2 for windowing and OpenGL for rendering.
 */
void gui_run_opengl(const char *game_name, const LotteryInfo *info, int debug_overlay,
                    int dark_mode);

#endif /* GUI_OPENGL_H */
