/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "combogen.h"
#include "gui_sdl.h"

#ifndef PROJECT_ROOT_DIR
#define PROJECT_ROOT_DIR "."
#endif

#define WINDOW_WIDTH 900
#define WINDOW_HEIGHT 600

/* Smaller balls, more space */
#define BALL_RADIUS 16.0f
#define BALL_DIAMETER (BALL_RADIUS * 2.0f)

/* Larger drums */
#define MAIN_DRUM_X (WINDOW_WIDTH * 0.30f)
#define MAIN_DRUM_Y (WINDOW_HEIGHT * 0.45f)
#define MAIN_DRUM_R 220.0f

#define EXTRA_DRUM_X (WINDOW_WIDTH * 0.70f)
#define EXTRA_DRUM_Y (WINDOW_HEIGHT * 0.45f)
#define EXTRA_DRUM_R 150.0f

#define MAX_BALLS 64

/* Physics constants for realistic ball motion */
#define GRAVITY 400.0f /* pixels/second² - downward acceleration */

typedef enum
{
    BALL_IN_DRUM = 0,
    BALL_MOVING_TO_RESULT,
    BALL_RESULT
} BallState;

typedef struct
{
    float x, y;
    float vx, vy;
    float target_x, target_y; /* where to glide to in result row */
    int number;
    int is_main; /* 1 = main drum, 0 = extra drum */
    BallState state;
    int result_index; /* index in result row */
} Ball;

typedef struct
{
    Ball balls[MAX_BALLS];
    int count;        /* total balls in this drum */
    int active_count; /* balls still in drum (IN_DRUM or MOVING) */
    int initial_count;
    float cx, cy, radius;
    float rotation_angle; /* degrees, for visual rotation */
} Drum;

typedef enum
{
    ANIMATION_STATE_REVEALING_MAIN,  /* Main drum spinning, picking main numbers */
    ANIMATION_STATE_REVEALING_EXTRA, /* Extra drum spinning, picking extra numbers */
    ANIMATION_STATE_COMPLETE         /* All numbers revealed, animation done */
} AnimationState;

typedef struct
{
    Drum main_drum;
    Drum extra_drum;
    LotteryInfo info;
    LotteryResult result;

    /* Explicit state machine */
    AnimationState animation_state;

    /* Progress tracking */
    int next_pick_index; /* 0 .. main_count + extra_count - 1 */
    int revealed_main;   /* how many main balls fully in row */
    int revealed_extra;  /* how many extra balls fully in row */

    /* Timing */
    float time_since_last_pick;
    float pick_interval; /* seconds between starting new glides */
} GuiState;

/* --------------------------------------------------------- */
/* Utility                                                   */
/* --------------------------------------------------------- */

/* Generate random float in range [a, b) using portable rand()/RAND_MAX */
static float frand_range(float a, float b)
{
    return a + (b - a) * ((float)rand() / (float)RAND_MAX);
}

/* --------------------------------------------------------- */
/* Drum / ball setup                                         */
/* --------------------------------------------------------- */

static void init_drum(Drum *d, int count, float cx, float cy, float radius, int is_main)
{
    if (count > MAX_BALLS)
        count = MAX_BALLS;

    d->count = count;
    d->active_count = count;
    d->initial_count = count;
    d->cx = cx;
    d->cy = cy;
    d->radius = radius;
    d->rotation_angle = 0.0f;

    for (int i = 0; i < count; i++)
    {
        Ball *b = &d->balls[i];
        /* Distribute balls throughout the drum volume (not just edges) */
        float angle = frand_range(0.0f, 2.0f * (float)M_PI);
        float r = frand_range(0.0f, (radius - BALL_RADIUS - 5.0f) *
                                        0.8f); /* use 80% of available radius */
        b->x = cx + cosf(angle) * r;
        b->y = cy + sinf(angle) * r -
               radius * 0.3f; /* start balls higher up so gravity pulls them down */
        /* Initial velocity - horizontal spread prevents pure vertical free-fall */
        b->vx = frand_range(-80.0f, 80.0f);
        b->vy = frand_range(-20.0f, 20.0f);
        b->target_x = b->x;
        b->target_y = b->y;
        b->number = i + 1;
        b->is_main = is_main;
        b->state = BALL_IN_DRUM;
        b->result_index = -1;
    }
}

static Ball *find_ball_by_number(Drum *d, int number)
{
    for (int i = 0; i < d->count; i++)
    {
        if (d->balls[i].number == number)
            return &d->balls[i];
    }
    return NULL;
}

/* --------------------------------------------------------- */
/* Physics                                                   */
/* --------------------------------------------------------- */

static void update_ball_physics(Drum *d, float dt)
{
    /* Rotate drum */
    if (d->active_count > 0)
    {
        d->rotation_angle += 90.0f * dt; /* degrees per second */
        if (d->rotation_angle >= 360.0f)
            d->rotation_angle -= 360.0f;
    }

    /* Drum surface velocity (at outer edge, in rad/s) */
    float drum_omega = 90.0f * (float)M_PI / 180.0f;

    for (int i = 0; i < d->count; i++)
    {
        Ball *b = &d->balls[i];
        if (b->state != BALL_IN_DRUM)
            continue;

        /* Apply gravity */
        b->vy += GRAVITY * dt;

        /* Update position */
        b->x += b->vx * dt;
        b->y += b->vy * dt;

        /* Drum surface friction: drag balls along with rotating drum */
        float bx = b->x - d->cx;
        float by = b->y - d->cy;
        float ball_dist = sqrtf(bx * bx + by * by);
        float wall_r = d->radius - BALL_RADIUS;

        if (ball_dist > 0.0001f)
        {
            /* Surface velocity tangent to drum at ball's position */
            float norm = 1.0f / ball_dist;
            float nx = bx * norm; /* radial normal (outward) */
            float ny = by * norm;
            float tx = -ny; /* tangent (perpendicular, CCW) */
            float ty = nx;

            /* If ball is near drum wall, friction drags it along */
            if (ball_dist > wall_r * 0.9f && ball_dist < d->radius)
            {
                float surface_vt = drum_omega * d->radius;
                float ball_vt = b->vx * tx + b->vy * ty;
                float slip = surface_vt - ball_vt;

                /* Weak friction - gravity must dominate */
                float friction = slip * 0.08f;
                b->vx += friction * tx;
                b->vy += friction * ty;
            }
        }

        /* Wall collision: inelastic bounce (restitution 0.5) */
        if (ball_dist > wall_r && ball_dist > 0.0001f)
        {
            float nx = bx / ball_dist;
            float ny = by / ball_dist;

            /* Push ball back inside */
            b->x -= nx * (ball_dist - wall_r);
            b->y -= ny * (ball_dist - wall_r);

            /* Inelastic bounce */
            float vn = b->vx * nx + b->vy * ny;
            if (vn > 0.0f)
            {
                b->vx -= 1.4f * vn * nx; /* lower bounce coefficient */
                b->vy -= 1.4f * vn * ny;
            }
        }

        /* Air resistance: light enough that horizontal momentum persists */
        b->vx *= 0.99f;
        b->vy *= 0.99f;
    }

    /* Ball-to-ball collisions: energetic spreading through pile */
    for (int i = 0; i < d->count; i++)
    {
        Ball *a = &d->balls[i];
        if (a->state != BALL_IN_DRUM)
            continue;

        for (int j = i + 1; j < d->count; j++)
        {
            Ball *b = &d->balls[j];
            if (b->state != BALL_IN_DRUM)
                continue;

            float dx = b->x - a->x;
            float dy = b->y - a->y;
            float dist2 = dx * dx + dy * dy;
            float minDist = BALL_DIAMETER;
            if (dist2 < minDist * minDist && dist2 > 0.0001f)
            {
                float dist = sqrtf(dist2);
                float overlap = (minDist - dist) * 0.5f;
                float nx = dx / dist;
                float ny = dy / dist;

                /* Separate balls along normal */
                a->x -= nx * overlap;
                a->y -= ny * overlap;
                b->x += nx * overlap;
                b->y += ny * overlap;

                /* Normal impulse: energetic momentum transfer (restitution 0.7) */
                float rvx = b->vx - a->vx;
                float rvy = b->vy - a->vy;
                float rel = rvx * nx + rvy * ny;
                if (rel < 0.0f)
                {
                    /* Momentum transfer through collisions - reduced to let gravity settle balls */
                    float impulse = -rel * 0.4f;
                    a->vx -= impulse * nx;
                    a->vy -= impulse * ny;
                    b->vx += impulse * nx;
                    b->vy += impulse * ny;
                }

                /* Tangential friction: prevent balls from sliding cleanly past each other */
                float tx = -ny;
                float ty = nx;
                float rel_tangent = rvx * tx + rvy * ty;
                float friction = rel_tangent * 0.15f;
                a->vx -= friction * tx;
                a->vy -= friction * ty;
                b->vx += friction * tx;
                b->vy += friction * ty;
            }
        }
    }
}

/* smooth glide from drum to result slot */
static void update_glide_to_result(Drum *d, float dt)
{
    for (int i = 0; i < d->count; i++)
    {
        Ball *b = &d->balls[i];
        if (b->state != BALL_MOVING_TO_RESULT)
            continue;

        float dx = b->target_x - b->x;
        float dy = b->target_y - b->y;

        float dist = sqrtf(dx * dx + dy * dy);
        if (dist < 1.5f)
        {
            b->x = b->target_x;
            b->y = b->target_y;
            b->state = BALL_RESULT;
            d->active_count--;
            continue;
        }

        /* smooth glide: interpolate towards target */
        float speed_factor = 5.0f; /* tune for feel */
        b->x += dx * speed_factor * dt;
        b->y += dy * speed_factor * dt;
    }
}

/* --------------------------------------------------------- */
/* Rendering helpers                                         */
/* --------------------------------------------------------- */

static void draw_filled_circle(SDL_Renderer *r, int cx, int cy, int radius)
{
    for (int dy = -radius; dy <= radius; dy++)
    {
        int dx_max = (int)sqrtf((float)(radius * radius - dy * dy));
        for (int dx = -dx_max; dx <= dx_max; dx++)
        {
            SDL_RenderDrawPoint(r, cx + dx, cy + dy);
        }
    }
}

static void render_ball_at(SDL_Renderer *r, TTF_Font *font, float x, float y, int number,
                           int is_main)
{
    SDL_Color col = is_main ? (SDL_Color){255, 215, 0, 255} : (SDL_Color){50, 150, 255, 255};

    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    draw_filled_circle(r, (int)x, (int)y, (int)BALL_RADIUS);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", number);

    SDL_Surface *surf = TTF_RenderText_Blended(font, buf, (SDL_Color){0, 0, 0, 255});
    if (!surf)
        return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);
    if (!tex)
        return;

    int tw, th;
    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
    SDL_Rect dst = {(int)x - tw / 2, (int)y - th / 2, tw, th};
    SDL_RenderCopy(r, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}

/* Render a rotating drum with visual markers to show rotation */
static void render_drum_outline(SDL_Renderer *r, float cx, float cy, float radius,
                                float rotation_angle)
{
    /* Draw drum circle */
    SDL_SetRenderDrawColor(r, 200, 200, 200, 255);
    int steps = 128;
    for (int i = 0; i < steps; i++)
    {
        float a0 = (float)i / steps * 2.0f * (float)M_PI;
        float a1 = (float)(i + 1) / steps * 2.0f * (float)M_PI;
        int x0 = (int)(cx + cosf(a0) * radius);
        int y0 = (int)(cy + sinf(a0) * radius);
        int x1 = (int)(cx + cosf(a1) * radius);
        int y1 = (int)(cy + sinf(a1) * radius);
        SDL_RenderDrawLine(r, x0, y0, x1, y1);
    }

    /* Draw rotating paddles (fixed to drum interior) */
    float angle_rad = rotation_angle * (float)M_PI / 180.0f;
    SDL_SetRenderDrawColor(r, 255, 150, 100, 255); /* orange rotation markers */
    int num_markers = 12;
    for (int i = 0; i < num_markers; i++)
    {
        float marker_angle = angle_rad + (float)i / num_markers * 2.0f * (float)M_PI;
        float inner_r = radius * 0.80f;
        float outer_r = radius * 0.95f;
        int x0 = (int)(cx + cosf(marker_angle) * inner_r);
        int y0 = (int)(cy + sinf(marker_angle) * inner_r);
        int x1 = (int)(cx + cosf(marker_angle) * outer_r);
        int y1 = (int)(cy + sinf(marker_angle) * outer_r);
        SDL_RenderDrawLine(r, x0, y0, x1, y1);
    }
}

static void render_number_in_result_row(SDL_Renderer *r, TTF_Font *font, int number, float x,
                                        float y, int is_main)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", number);

    SDL_Color col = is_main ? (SDL_Color){255, 215, 0, 255} : (SDL_Color){50, 150, 255, 255};

    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    draw_filled_circle(r, (int)x, (int)y, (int)BALL_RADIUS);

    SDL_Surface *surf = TTF_RenderText_Blended(font, buf, (SDL_Color){0, 0, 0, 255});
    if (!surf)
        return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);
    if (!tex)
        return;
    int tw, th;
    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
    SDL_Rect dst = {(int)x - tw / 2, (int)y - th / 2, tw, th};
    SDL_RenderCopy(r, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}

static void render_result_row(SDL_Renderer *r, TTF_Font *font, GuiState *gs)
{
    float start_x = WINDOW_WIDTH * 0.10f;
    float y = WINDOW_HEIGHT * 0.85f;
    float spacing = BALL_DIAMETER + 10.0f;

    int idx = 0;

    /* only draw revealed main numbers */
    for (int i = 0; i < gs->revealed_main; i++)
    {
        float x = start_x + spacing * idx++;
        render_number_in_result_row(r, font, gs->result.main_numbers[i], x, y, 1);
    }

    /* only draw revealed extra numbers */
    for (int i = 0; i < gs->revealed_extra; i++)
    {
        float x = start_x + spacing * idx++;
        render_number_in_result_row(r, font, gs->result.extra_numbers[i], x, y, 0);
    }
}

/* --------------------------------------------------------- */
/* Draw callback from generate_draw                          */
/* --------------------------------------------------------- */

static GuiState *g_state = NULL;

static void gui_draw_callback(DrawEvent event, const LotteryResult *res)
{
    if (!g_state || !res)
        return;

    if (event == EVENT_DRAW_COMPLETE)
    {
        g_state->result = *res;
    }
}

/* --------------------------------------------------------- */
/* Progressive reveal logic with explicit state transitions */
/* --------------------------------------------------------- */

static void maybe_start_next_pick(GuiState *st)
{
    int total = st->info.main_count + st->info.extra_count;

    /* Stop if all picks have been started */
    if (st->next_pick_index >= total)
        return;

    float start_x = WINDOW_WIDTH * 0.10f;
    float y = WINDOW_HEIGHT * 0.85f;
    float spacing = BALL_DIAMETER + 10.0f;

    int idx = st->next_pick_index;
    st->next_pick_index++;

    if (idx < st->info.main_count)
    {
        /* MAIN DRUM PICK */
        int num = st->result.main_numbers[idx];
        Ball *b = find_ball_by_number(&st->main_drum, num);
        if (b && b->state == BALL_IN_DRUM)
        {
            float target_x = start_x + spacing * idx;
            float target_y = y;
            b->target_x = target_x;
            b->target_y = target_y;
            b->result_index = idx;
            b->state = BALL_MOVING_TO_RESULT;
        }

        /* Transition to extra drum reveal after all main picks have started */
        if (st->next_pick_index == st->info.main_count)
        {
            st->animation_state = ANIMATION_STATE_REVEALING_EXTRA;
        }
    }
    else
    {
        /* EXTRA (SUPERZAHL) PICK */
        int extra_idx = idx - st->info.main_count;
        int num = st->result.extra_numbers[extra_idx];
        Ball *b = find_ball_by_number(&st->extra_drum, num);
        if (b && b->state == BALL_IN_DRUM)
        {
            float target_x = start_x + spacing * (st->info.main_count + extra_idx);
            float target_y = y;
            b->target_x = target_x;
            b->target_y = target_y;
            b->result_index = extra_idx;
            b->state = BALL_MOVING_TO_RESULT;
        }

        /* Transition to animation complete after all picks have started */
        if (st->next_pick_index == total)
        {
            st->animation_state = ANIMATION_STATE_COMPLETE;
        }
    }
}

/* recompute how many balls are fully in the result row */
static void update_revealed_counts(GuiState *st)
{
    int max_main = -1;
    int max_extra = -1;

    for (int i = 0; i < st->main_drum.count; i++)
    {
        Ball *b = &st->main_drum.balls[i];
        if (b->state == BALL_RESULT && b->result_index >= 0)
        {
            if (b->result_index > max_main)
                max_main = b->result_index;
        }
    }
    for (int i = 0; i < st->extra_drum.count; i++)
    {
        Ball *b = &st->extra_drum.balls[i];
        if (b->state == BALL_RESULT && b->result_index >= 0)
        {
            if (b->result_index > max_extra)
                max_extra = b->result_index;
        }
    }

    st->revealed_main = (max_main >= 0) ? (max_main + 1) : 0;
    st->revealed_extra = (max_extra >= 0) ? (max_extra + 1) : 0;
}

/* --------------------------------------------------------- */
/* Public entry                                              */
/* --------------------------------------------------------- */

void gui_run(const char *game_name, const LotteryInfo *info, int dark_mode)
{
    (void)dark_mode; /* Parameter for future theming; currently unused */
    /* Validate input parameters */
    if (!game_name || game_name[0] == '\0')
    {
        fprintf(stderr, "Error: Game name is NULL or empty\n");
        return;
    }

    if (!info)
    {
        fprintf(stderr, "Error: LotteryInfo pointer is NULL\n");
        return;
    }

    /* Validate lottery info structure */
    if (info->main_count <= 0 || info->main_count > 7)
    {
        fprintf(stderr, "Error: Invalid main_count in LotteryInfo: %d\n", info->main_count);
        return;
    }

    if (info->extra_count < 0 || info->extra_count > 3)
    {
        fprintf(stderr, "Error: Invalid extra_count in LotteryInfo: %d\n", info->extra_count);
        return;
    }

    if (info->main_min <= 0 || info->main_max <= 0 || info->main_min > info->main_max)
    {
        fprintf(stderr, "Error: Invalid main range in LotteryInfo: [%d, %d]\n", info->main_min,
                info->main_max);
        return;
    }

    if (info->extra_count > 0 &&
        (info->extra_min < 0 || info->extra_max < 0 || info->extra_min > info->extra_max))
    {
        fprintf(stderr, "Error: Invalid extra range in LotteryInfo: [%d, %d]\n", info->extra_min,
                info->extra_max);
        return;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return;
    }

    if (TTF_Init() != 0)
    {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        SDL_Quit();
        return;
    }

    SDL_Window *win =
        SDL_CreateWindow(game_name ? game_name : "Lotto", SDL_WINDOWPOS_CENTERED,
                         SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    if (!win)
    {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        return;
    }

    SDL_Renderer *ren =
        SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren)
    {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        TTF_Quit();
        SDL_Quit();
        return;
    }

    char font_path[512];
    snprintf(font_path, sizeof(font_path), "%s/fonts/Roboto-Bold.ttf", PROJECT_ROOT_DIR);
    TTF_Font *font = TTF_OpenFont(font_path, 22);
    if (!font)
    {
        fprintf(stderr, "TTF_OpenFont failed: %s\n", TTF_GetError());
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        TTF_Quit();
        SDL_Quit();
        return;
    }

    GuiState state;
    g_state = &state;
    state.info = *info;
    state.animation_state = ANIMATION_STATE_REVEALING_MAIN;
    state.next_pick_index = 0;
    state.revealed_main = 0;
    state.revealed_extra = 0;
    state.time_since_last_pick = 0.0f;
    state.pick_interval = 1.5f; /* seconds between starting new glides */

    /* Seed PRNG once for random initial drum state */
    srand((unsigned)time(NULL));

    init_drum(&state.main_drum, info->main_max, MAIN_DRUM_X, MAIN_DRUM_Y, MAIN_DRUM_R, 1);
    init_drum(&state.extra_drum, info->extra_max, EXTRA_DRUM_X, EXTRA_DRUM_Y, EXTRA_DRUM_R, 0);

    LotteryResult res;
    generate_draw(info->main_count, info->main_min, info->main_max, info->extra_count,
                  info->extra_min, info->extra_max, &res, gui_draw_callback);

    Uint32 last = SDL_GetTicks();
    int running = 1;

    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_QUIT)
                running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
                running = 0;
        }

        Uint32 now = SDL_GetTicks();
        float dt = (now - last) / 1000.0f;
        if (dt > 0.05f)
            dt = 0.05f;
        last = now;

        /* update timers and maybe start a new pick */
        state.time_since_last_pick += dt;
        if (state.time_since_last_pick >= state.pick_interval)
        {
            state.time_since_last_pick = 0.0f;
            maybe_start_next_pick(&state);
        }

        /* physics: main drum always active */
        update_ball_physics(&state.main_drum, dt);
        update_glide_to_result(&state.main_drum, dt);

        /* extra drum only active during REVEALING_EXTRA and COMPLETE states */
        if (state.animation_state >= ANIMATION_STATE_REVEALING_EXTRA)
        {
            update_ball_physics(&state.extra_drum, dt);
            update_glide_to_result(&state.extra_drum, dt);
        }

        /* recompute how many balls are fully in the result row */
        update_revealed_counts(&state);

        SDL_SetRenderDrawColor(ren, 10, 10, 20, 255);
        SDL_RenderClear(ren);

        /* draw drums */
        render_drum_outline(ren, state.main_drum.cx, state.main_drum.cy, state.main_drum.radius,
                            state.main_drum.rotation_angle);
        if (state.animation_state >= ANIMATION_STATE_REVEALING_EXTRA)
        {
            render_drum_outline(ren, state.extra_drum.cx, state.extra_drum.cy,
                                state.extra_drum.radius, state.extra_drum.rotation_angle);
        }

        /* draw balls in main drum - render at their actual physics positions */
        for (int i = 0; i < state.main_drum.count; i++)
        {
            Ball *b = &state.main_drum.balls[i];
            if (b->state == BALL_IN_DRUM || b->state == BALL_MOVING_TO_RESULT)
                render_ball_at(ren, font, b->x, b->y, b->number, b->is_main);
        }

        /* draw balls in extra drum (only after it starts) */
        if (state.animation_state >= ANIMATION_STATE_REVEALING_EXTRA)
        {
            for (int i = 0; i < state.extra_drum.count; i++)
            {
                Ball *b = &state.extra_drum.balls[i];
                if (b->state == BALL_IN_DRUM || b->state == BALL_MOVING_TO_RESULT)
                    render_ball_at(ren, font, b->x, b->y, b->number, b->is_main);
            }
        }

        /* draw result row (only revealed balls) */
        render_result_row(ren, font, &state);

        SDL_RenderPresent(ren);

        /* Animation state is tracked explicitly - no new picks once COMPLETE */
    }

    TTF_CloseFont(font);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    g_state = NULL;
}

static void draw_text(SDL_Renderer *ren, TTF_Font *font, const char *text, int x, int y,
                      SDL_Color color)
{
    SDL_Surface *surf = TTF_RenderText_Blended(font, text, color);
    if (!surf)
        return;

    SDL_Texture *tex = SDL_CreateTextureFromSurface(ren, surf);
    SDL_Rect dst = {x, y, surf->w, surf->h};
    SDL_FreeSurface(surf);
    if (!tex)
        return;

    SDL_RenderCopy(ren, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}

/* -----------------------------------------------------------------------
 * Shared SDL analytics helpers
 * ----------------------------------------------------------------------- */

/* Read optional timeout from OPEN_LOTTO_ANALYTICS_GUI_TIMEOUT_MS env var.
 * 0 means no auto-close (interactive mode).
 * Tests set a small value (e.g. 200) so they don't hang. */
static Uint32 analytics_gui_timeout_ms(void)
{
    const char *env = getenv("OPEN_LOTTO_ANALYTICS_GUI_TIMEOUT_MS");
    if (!env || env[0] == '\0')
        return 0; /* stay open until user closes */
    long v = atol(env);
    return (v > 0) ? (Uint32)v : 0;
}

/* Draw horizontal grid lines at 25 / 50 / 75 / 100 % of chart height */
static void draw_grid(SDL_Renderer *ren, int left, int right, int top, int chart_h,
                      SDL_Color dim)
{
    SDL_SetRenderDrawColor(ren, dim.r, dim.g, dim.b, dim.a);
    for (int q = 1; q <= 4; q++)
    {
        int y = top + chart_h - (chart_h * q / 4);
        SDL_RenderDrawLine(ren, left, y, right, y);
    }
}

/* Lerp float */
static float flerpf(float a, float b, float t)
{
    return a + (b - a) * (t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t);
}

/* Draw a tooltip box near (tx, ty) with two lines of text.
 * Clamps to window boundaries so it never goes off-screen. */
static void draw_tooltip(SDL_Renderer *ren, TTF_Font *font, const char *line1, const char *line2,
                         int tx, int ty, int win_w, int win_h)
{
    if (!font) return;

    /* Measure both lines */
    int w1 = 0, h1 = 0, w2 = 0, h2 = 0;
    TTF_SizeText(font, line1, &w1, &h1);
    if (line2 && line2[0]) TTF_SizeText(font, line2, &w2, &h2);

    int pad    = 8;
    int box_w  = (w1 > w2 ? w1 : w2) + pad * 2;
    int box_h  = h1 + (line2 && line2[0] ? h2 + 4 : 0) + pad * 2;

    /* Offset from cursor */
    int bx = tx + 14;
    int by = ty - box_h - 6;

    /* Clamp to window */
    if (bx + box_w > win_w - 4) bx = tx - box_w - 6;
    if (bx < 4)                  bx = 4;
    if (by < 4)                  by = ty + 16;
    if (by + box_h > win_h - 4)  by = win_h - box_h - 4;

    /* Shadow */
    SDL_Rect shadow = {bx + 3, by + 3, box_w, box_h};
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 60);
    SDL_RenderFillRect(ren, &shadow);

    /* Background */
    SDL_Rect bg = {bx, by, box_w, box_h};
    SDL_SetRenderDrawColor(ren, 28, 32, 44, 230);
    SDL_RenderFillRect(ren, &bg);

    /* Border */
    SDL_SetRenderDrawColor(ren, 255, 215, 0, 200);
    SDL_RenderDrawRect(ren, &bg);

    SDL_Color white = {240, 244, 255, 255};
    SDL_Color gold  = {255, 215, 0,   255};
    draw_text(ren, font, line1, bx + pad, by + pad,        gold);
    if (line2 && line2[0])
        draw_text(ren, font, line2, bx + pad, by + pad + h1 + 4, white);
}

int gui_render_frequency_2d(const char *title, const FrequencyReport *report, int dark_mode)
{
    if (!report)
        return -1;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
        return -1;
    if (TTF_Init() != 0) { SDL_Quit(); return -1; }

    SDL_Window *win = SDL_CreateWindow(
        title ? title : "Frequency Distribution", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    if (!win) { TTF_Quit(); SDL_Quit(); return -1; }

    SDL_Renderer *ren = SDL_CreateRenderer(
        win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) { SDL_DestroyWindow(win); TTF_Quit(); SDL_Quit(); return -1; }

    char font_path[512];
    snprintf(font_path, sizeof(font_path), "%s/fonts/Roboto-Bold.ttf", PROJECT_ROOT_DIR);
    TTF_Font *font    = TTF_OpenFont(font_path, 16);
    TTF_Font *font_sm = TTF_OpenFont(font_path, 13);
    if (!font)
    {
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); TTF_Quit(); SDL_Quit();
        return -1;
    }

    int max_count = 1;
    for (int n = report->number_min; n <= report->number_max; n++)
        if (report->counts[n] > max_count)
            max_count = report->counts[n];

    const int left = 70, right = WINDOW_WIDTH - 30, top = 60, bottom = WINDOW_HEIGHT - 80;
    const int chart_w = right - left, chart_h = bottom - top;
    const int points  = report->number_max - report->number_min + 1;
    const int bar_w   = points > 0 ? (chart_w / points) : 1;

    Uint32 start   = SDL_GetTicks();
    Uint32 timeout = analytics_gui_timeout_ms();
    float  anim    = 0.0f; /* 0→1 bar grow progress */
    Uint32 last    = start;
    int    running = 1;
    int    mouse_x = -1, mouse_y = -1; /* current cursor position */

    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
            if (ev.type == SDL_MOUSEMOTION) { mouse_x = ev.motion.x; mouse_y = ev.motion.y; }
        }
        if (timeout > 0 && SDL_GetTicks() - start >= timeout) running = 0;

        Uint32 now = SDL_GetTicks();
        float dt = (now - last) / 1000.0f;
        last = now;
        anim += dt / 0.45f;
        if (anim > 1.0f) anim = 1.0f;

        /* Background */
        if (dark_mode == 1) SDL_SetRenderDrawColor(ren, 12, 14, 20, 255);
        else                SDL_SetRenderDrawColor(ren, 242, 244, 250, 255);
        SDL_RenderClear(ren);

        SDL_Color fg    = dark_mode==1 ? (SDL_Color){220,228,245,255} : (SDL_Color){15,20,35,255};
        SDL_Color dim   = dark_mode==1 ? (SDL_Color){40,50,65,255}    : (SDL_Color){200,210,225,255};
        SDL_Color bar_c = dark_mode==1 ? (SDL_Color){255,215,0,230}   : (SDL_Color){40,120,230,255};
        SDL_Color hi_c  = (SDL_Color){255, 255, 120, 255}; /* highlighted bar colour */

        draw_grid(ren, left, right, top, chart_h, dim);

        /* Axes */
        SDL_SetRenderDrawColor(ren, fg.r, fg.g, fg.b, fg.a);
        SDL_RenderDrawLine(ren, left, bottom, right, bottom);
        SDL_RenderDrawLine(ren, left, top,    left,  bottom);

        /* Title */
        char header[160];
        snprintf(header, sizeof(header),
                 "Frequency Distribution  |  game: %s  |  draws: %d",
                 title ? title : "", report->total_draws);
        draw_text(ren, font, header, left, 18, fg);

        /* Date range */
        if (report->from_date[0] && report->to_date[0])
        {
            char date_str[128];
            snprintf(date_str, sizeof(date_str), "Period: %s to %s",
                     report->from_date, report->to_date);
            draw_text(ren, font_sm ? font_sm : font, date_str, left, 38, 
                      (SDL_Color){fg.r, fg.g, fg.b, 200});
        }

        if (font_sm)
            draw_text(ren, font_sm, "ESC: close", WINDOW_WIDTH - 120, 18,
                      (SDL_Color){fg.r, fg.g, fg.b, 160});

        /* Determine which bar (if any) the cursor is over */
        int hovered_bar = -1;
        if (mouse_x >= left && mouse_x < right && mouse_y >= top && mouse_y <= bottom)
        {
            int idx = (mouse_x - left) / (bar_w > 0 ? bar_w : 1);
            if (idx >= 0 && idx < points)
                hovered_bar = idx;
        }

        /* Bars */
        int tooltip_x = -1, tooltip_y = -1;
        int tooltip_number = -1;
        for (int i = 0; i < points; i++)
        {
            int  number = report->number_min + i;
            int  full_h = (report->counts[number] * chart_h) / max_count;
            int  height = (int)flerpf(0.0f, (float)full_h, anim);
            int  x = left + i * bar_w + 1;
            int  bw = bar_w > 3 ? bar_w - 2 : 1;

            int is_hovered = (i == hovered_bar);

            /* Hover effect: scale bar slightly and add glow */
            int scaled_w = bw;
            int scaled_h = height;
            int scaled_x = x;
            int scaled_y = bottom - height;
            
            if (is_hovered && height > 0)
            {
                /* Scale bar 20% wider and 10% taller, keeping it centered */
                scaled_w = (int)(bw * 1.20f);
                scaled_h = (int)(height * 1.10f);
                scaled_x = x - (scaled_w - bw) / 2;
                scaled_y = bottom - scaled_h;
                
                /* Glow effect: semi-transparent yellow rings */
                SDL_SetRenderDrawColor(ren, 255, 255, 120, 80);
                SDL_Rect glow = {scaled_x - 2, scaled_y - 2, scaled_w + 4, scaled_h + 4};
                SDL_RenderDrawRect(ren, &glow);
                SDL_Rect glow2 = {scaled_x - 1, scaled_y - 1, scaled_w + 2, scaled_h + 2};
                SDL_RenderDrawRect(ren, &glow2);
            }

            SDL_Color c = is_hovered ? hi_c : bar_c;
            SDL_Rect bar = {scaled_x, scaled_y, scaled_w, scaled_h};
            SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
            SDL_RenderFillRect(ren, &bar);

            /* Top cap */
            SDL_Rect cap = {scaled_x, scaled_y, scaled_w, 3};
            SDL_SetRenderDrawColor(ren, 255, 255, 255, is_hovered ? 180 : 80);
            SDL_RenderFillRect(ren, &cap);

            if (is_hovered)
            {
                tooltip_x      = scaled_x + scaled_w / 2;
                tooltip_y      = scaled_y;
                tooltip_number = number;
            }

            if (i % 5 == 0)
            {
                char label[8];
                snprintf(label, sizeof(label), "%d", number);
                draw_text(ren, font_sm ? font_sm : font, label, x - 2, bottom + 5, fg);
            }
        }

        /* Tooltip */
        if (tooltip_number >= 0 && font_sm && anim >= 1.0f)
        {
            int   count = report->counts[tooltip_number];
            double pct  = report->total_draws > 0
                          ? (100.0 * count) / (double)report->total_draws : 0.0;
            char  line1[32], line2[48];
            snprintf(line1, sizeof(line1), "Ball %d", tooltip_number);
            snprintf(line2, sizeof(line2), "%d draws  (%.1f%%)", count, pct);
            draw_tooltip(ren, font_sm, line1, line2,
                         tooltip_x, tooltip_y, WINDOW_WIDTH, WINDOW_HEIGHT);
        }

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    if (font_sm) TTF_CloseFont(font_sm);
    TTF_CloseFont(font);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    return 0;
}

int gui_render_barometer_2d(const char *title, const BarometerReport *report, int dark_mode)
{
    if (!report) return -1;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return -1;
    if (TTF_Init() != 0) { SDL_Quit(); return -1; }

    SDL_Window *win = SDL_CreateWindow(
        title ? title : "Barometer — Overdue Factor", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    if (!win) { TTF_Quit(); SDL_Quit(); return -1; }

    SDL_Renderer *ren = SDL_CreateRenderer(
        win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) { SDL_DestroyWindow(win); TTF_Quit(); SDL_Quit(); return -1; }

    char font_path[512];
    snprintf(font_path, sizeof(font_path), "%s/fonts/Roboto-Bold.ttf", PROJECT_ROOT_DIR);
    TTF_Font *font    = TTF_OpenFont(font_path, 16);
    TTF_Font *font_sm = TTF_OpenFont(font_path, 13);
    if (!font) { SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); TTF_Quit(); SDL_Quit(); return -1; }

    double max_factor = 0.01;
    for (int n = report->number_min; n <= report->number_max; n++)
        if (report->factors[n] > max_factor)
            max_factor = report->factors[n];

    const int left = 70, right = WINDOW_WIDTH - 30, top = 60, bottom = WINDOW_HEIGHT - 80;
    const int chart_w = right - left, chart_h = bottom - top;
    const int points  = report->number_max - report->number_min + 1;
    const int bar_w   = points > 0 ? (chart_w / points) : 1;

    /* reference line = expected interval (factor = 1.0) */
    int ref_y = bottom - (int)(1.0 / max_factor * chart_h);

    Uint32 start  = SDL_GetTicks();
    Uint32 timeout = analytics_gui_timeout_ms();
    float  anim    = 0.0f;
    Uint32 last     = start;
    int    running  = 1;
    int    mouse_x  = -1, mouse_y = -1;

    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
            if (ev.type == SDL_MOUSEMOTION) { mouse_x = ev.motion.x; mouse_y = ev.motion.y; }
        }
        if (timeout > 0 && SDL_GetTicks() - start >= timeout) running = 0;

        Uint32 now = SDL_GetTicks();
        anim += (now - last) / 1000.0f / 0.45f;
        last = now;
        if (anim > 1.0f) anim = 1.0f;

        if (dark_mode == 1) SDL_SetRenderDrawColor(ren, 12, 14, 20, 255);
        else                SDL_SetRenderDrawColor(ren, 242, 244, 250, 255);
        SDL_RenderClear(ren);

        SDL_Color fg    = dark_mode==1 ? (SDL_Color){220,228,245,255} : (SDL_Color){15,20,35,255};
        SDL_Color dim   = dark_mode==1 ? (SDL_Color){40,50,65,255}    : (SDL_Color){200,210,225,255};
        SDL_Color bar_c = dark_mode==1 ? (SDL_Color){255,145,50,230}  : (SDL_Color){210,80,30,255};
        SDL_Color hi_c  = (SDL_Color){255, 255, 120, 255};

        draw_grid(ren, left, right, top, chart_h, dim);

        SDL_SetRenderDrawColor(ren, fg.r, fg.g, fg.b, fg.a);
        SDL_RenderDrawLine(ren, left, bottom, right, bottom);
        SDL_RenderDrawLine(ren, left, top,    left,  bottom);

        if (ref_y >= top && ref_y <= bottom)
        {
            SDL_SetRenderDrawColor(ren, 100, 220, 100, 180);
            SDL_RenderDrawLine(ren, left, ref_y, right, ref_y);
            if (font_sm)
                draw_text(ren, font_sm, "expected", right - 68, ref_y - 14,
                          (SDL_Color){100, 220, 100, 200});
        }

        char header[160];
        snprintf(header, sizeof(header),
                 "Barometer — Overdue Factor  |  game: %s  |  draws: %d",
                 title ? title : "", report->total_draws);
        draw_text(ren, font, header, left, 18, fg);
        
        /* Date range */
        if (report->from_date[0] && report->to_date[0])
        {
            char date_str[128];
            snprintf(date_str, sizeof(date_str), "Period: %s to %s",
                     report->from_date, report->to_date);
            draw_text(ren, font_sm ? font_sm : font, date_str, left, 38, 
                      (SDL_Color){fg.r, fg.g, fg.b, 200});
        }

        if (font_sm)
            draw_text(ren, font_sm, "ESC: close", WINDOW_WIDTH - 120, 18,
                      (SDL_Color){fg.r, fg.g, fg.b, 160});

        /* Detect hovered bar */
        int hovered_bar = -1;
        if (mouse_x >= left && mouse_x < right && mouse_y >= top && mouse_y <= bottom)
        {
            int idx = (mouse_x - left) / (bar_w > 0 ? bar_w : 1);
            if (idx >= 0 && idx < points) hovered_bar = idx;
        }

        int tooltip_x = -1, tooltip_y = -1, tooltip_number = -1;
        for (int i = 0; i < points; i++)
        {
            int number  = report->number_min + i;
            int full_h  = (int)((report->factors[number] / max_factor) * chart_h);
            int height  = (int)flerpf(0.0f, (float)full_h, anim);
            int x  = left + i * bar_w + 1;
            int bw = bar_w > 3 ? bar_w - 2 : 1;
            int is_hovered = (i == hovered_bar);

            /* Hover effect: scale bar with glow */
            int scaled_w = bw;
            int scaled_h = height;
            int scaled_x = x;
            int scaled_y = bottom - height;
            
            if (is_hovered && height > 0)
            {
                scaled_w = (int)(bw * 1.20f);
                scaled_h = (int)(height * 1.10f);
                scaled_x = x - (scaled_w - bw) / 2;
                scaled_y = bottom - scaled_h;
                
                SDL_SetRenderDrawColor(ren, 255, 200, 80, 100);
                SDL_Rect glow = {scaled_x - 2, scaled_y - 2, scaled_w + 4, scaled_h + 4};
                SDL_RenderDrawRect(ren, &glow);
                SDL_Rect glow2 = {scaled_x - 1, scaled_y - 1, scaled_w + 2, scaled_h + 2};
                SDL_RenderDrawRect(ren, &glow2);
            }

            SDL_Color c = is_hovered ? hi_c : bar_c;
            SDL_Rect bar = {scaled_x, scaled_y, scaled_w, scaled_h};
            SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
            SDL_RenderFillRect(ren, &bar);
            SDL_Rect cap = {scaled_x, scaled_y, scaled_w, 3};
            SDL_SetRenderDrawColor(ren, 255, 255, 255, is_hovered ? 180 : 80);
            SDL_RenderFillRect(ren, &cap);

            if (is_hovered)
            {
                tooltip_x = scaled_x + scaled_w / 2; tooltip_y = scaled_y;
                tooltip_number = number;
            }

            if (i % 5 == 0)
            {
                char label[8];
                snprintf(label, sizeof(label), "%d", number);
                draw_text(ren, font_sm ? font_sm : font, label, x - 2, bottom + 5, fg);
            }
        }

        if (tooltip_number >= 0 && font_sm && anim >= 1.0f)
        {
            char line1[32], line2[64];
            snprintf(line1, sizeof(line1), "Ball %d", tooltip_number);
            snprintf(line2, sizeof(line2), "factor %.3f  |  hits %d",
                     report->factors[tooltip_number], report->hit_counts[tooltip_number]);
            draw_tooltip(ren, font_sm, line1, line2,
                         tooltip_x, tooltip_y, WINDOW_WIDTH, WINDOW_HEIGHT);
        }

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    if (font_sm) TTF_CloseFont(font_sm);
    TTF_CloseFont(font);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    return 0;
}

int gui_render_hot_cold_2d(const char *title, const HotColdReport *report, int dark_mode)
{
    if (!report) return -1;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return -1;
    if (TTF_Init() != 0) { SDL_Quit(); return -1; }

    SDL_Window *win = SDL_CreateWindow(
        title ? title : "Hot / Cold Numbers", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    if (!win) { TTF_Quit(); SDL_Quit(); return -1; }

    SDL_Renderer *ren = SDL_CreateRenderer(
        win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) { SDL_DestroyWindow(win); TTF_Quit(); SDL_Quit(); return -1; }

    char font_path[512];
    snprintf(font_path, sizeof(font_path), "%s/fonts/Roboto-Bold.ttf", PROJECT_ROOT_DIR);
    TTF_Font *font    = TTF_OpenFont(font_path, 16);
    TTF_Font *font_sm = TTF_OpenFont(font_path, 13);
    if (!font) { SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); TTF_Quit(); SDL_Quit(); return -1; }

    int max_count = 1;
    for (int i = 0; i < report->top_n; i++)
    {
        if (report->hot[i].count  > max_count) max_count = report->hot[i].count;
        if (report->cold[i].count > max_count) max_count = report->cold[i].count;
    }

    const int left = 80, right = WINDOW_WIDTH - 40, top = 70, bottom = WINDOW_HEIGHT - 70;
    const int chart_w = right - left, chart_h = bottom - top;
    const int groups  = report->top_n;
    const int group_w = groups > 0 ? chart_w / groups : 1;

    Uint32 start   = SDL_GetTicks();
    Uint32 timeout  = analytics_gui_timeout_ms();
    float  anim     = 0.0f;
    Uint32 last     = start;
    int    running  = 1;
    int    mouse_x  = -1, mouse_y = -1;

    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
            if (ev.type == SDL_MOUSEMOTION) { mouse_x = ev.motion.x; mouse_y = ev.motion.y; }
        }
        if (timeout > 0 && SDL_GetTicks() - start >= timeout) running = 0;

        Uint32 now = SDL_GetTicks();
        anim += (now - last) / 1000.0f / 0.45f;
        last = now;
        if (anim > 1.0f) anim = 1.0f;

        if (dark_mode == 1) SDL_SetRenderDrawColor(ren, 12, 14, 20, 255);
        else                SDL_SetRenderDrawColor(ren, 242, 244, 250, 255);
        SDL_RenderClear(ren);

        SDL_Color fg     = dark_mode==1 ? (SDL_Color){220,228,245,255} : (SDL_Color){15,20,35,255};
        SDL_Color dim    = dark_mode==1 ? (SDL_Color){40,50,65,255}    : (SDL_Color){200,210,225,255};
        SDL_Color hot_c  = (SDL_Color){230, 75,  60, 230};
        SDL_Color cold_c = (SDL_Color){ 60,120, 235, 230};

        draw_grid(ren, left, right, top, chart_h, dim);

        SDL_SetRenderDrawColor(ren, fg.r, fg.g, fg.b, fg.a);
        SDL_RenderDrawLine(ren, left, bottom, right, bottom);
        SDL_RenderDrawLine(ren, left, top,    left,  bottom);

        char header[160];
        snprintf(header, sizeof(header),
                 "Hot / Cold  |  game: %s  |  draws: %d  |  top %d",
                 title ? title : "", report->total_draws, report->top_n);
        draw_text(ren, font, header, left, 18, fg);
        
        /* Date range */
        if (report->from_date[0] && report->to_date[0])
        {
            char date_str[128];
            snprintf(date_str, sizeof(date_str), "Period: %s to %s",
                     report->from_date, report->to_date);
            draw_text(ren, font_sm ? font_sm : font, date_str, left, 38, 
                      (SDL_Color){fg.r, fg.g, fg.b, 200});
        }

        if (font_sm)
            draw_text(ren, font_sm, "ESC: close", WINDOW_WIDTH - 120, 18,
                      (SDL_Color){fg.r, fg.g, fg.b, 160});

        /* Detect hovered group */
        int hovered_group = -1;
        if (mouse_x >= left && mouse_x < right && mouse_y >= top && mouse_y <= bottom)
        {
            int idx = (mouse_x - left) / (group_w > 0 ? group_w : 1);
            if (idx >= 0 && idx < groups) hovered_group = idx;
        }

        int tip_x = -1, tip_y = -1, tip_group = -1, tip_is_hot = -1;
        for (int i = 0; i < groups; i++)
        {
            int x  = left + i * group_w;
            int hw = (group_w / 2) - 4;
            if (hw < 2) hw = 2;

            int h_hot  = (int)flerpf(0.0f, (float)(report->hot[i].count  * chart_h / max_count), anim);
            int h_cold = (int)flerpf(0.0f, (float)(report->cold[i].count * chart_h / max_count), anim);
            int is_h   = (i == hovered_group);

            /* Detect whether cursor is on hot or cold half */
            int on_hot  = is_h && mouse_x >= x + 2      && mouse_x < x + 2      + hw;
            int on_cold = is_h && mouse_x >= x + hw + 6  && mouse_x < x + hw + 6 + hw;

            /* Hot bar with hover scaling */
            int hx = x + 2, hw_s = hw, hh_s = h_hot;
            if (on_hot && h_hot > 0)
            {
                hw_s = (int)(hw * 1.15f);
                hh_s = (int)(h_hot * 1.08f);
                hx = x + 2 - (hw_s - hw) / 2;
                SDL_SetRenderDrawColor(ren, 255, 200, 100, 100);
                SDL_Rect ghlow = {hx - 2, bottom - hh_s - 2, hw_s + 4, hh_s + 4};
                SDL_RenderDrawRect(ren, &ghlow);
                SDL_Rect ghlow2 = {hx - 1, bottom - hh_s - 1, hw_s + 2, hh_s + 2};
                SDL_RenderDrawRect(ren, &ghlow2);
            }
            SDL_Color hc = on_hot ? (SDL_Color){255,200,80,255} : hot_c;
            SDL_Rect hbar = {hx, bottom - hh_s, hw_s, hh_s};
            SDL_SetRenderDrawColor(ren, hc.r, hc.g, hc.b, hc.a);
            SDL_RenderFillRect(ren, &hbar);
            SDL_SetRenderDrawColor(ren, 255, 255, 255, on_hot ? 180 : 80);
            SDL_Rect hcap = {hx, bottom - hh_s, hw_s, 3};
            SDL_RenderFillRect(ren, &hcap);

            /* Cold bar with hover scaling */
            int cx = x + hw + 6, cw_s = hw, ch_s = h_cold;
            if (on_cold && h_cold > 0)
            {
                cw_s = (int)(hw * 1.15f);
                ch_s = (int)(h_cold * 1.08f);
                cx = x + hw + 6 - (cw_s - hw) / 2;
                SDL_SetRenderDrawColor(ren, 140, 200, 255, 100);
                SDL_Rect cglow = {cx - 2, bottom - ch_s - 2, cw_s + 4, ch_s + 4};
                SDL_RenderDrawRect(ren, &cglow);
                SDL_Rect cglow2 = {cx - 1, bottom - ch_s - 1, cw_s + 2, ch_s + 2};
                SDL_RenderDrawRect(ren, &cglow2);
            }
            SDL_Color cc = on_cold ? (SDL_Color){140,200,255,255} : cold_c;
            SDL_Rect cbar = {cx, bottom - ch_s, cw_s, ch_s};
            SDL_SetRenderDrawColor(ren, cc.r, cc.g, cc.b, cc.a);
            SDL_RenderFillRect(ren, &cbar);
            SDL_SetRenderDrawColor(ren, 255, 255, 255, on_cold ? 180 : 80);
            SDL_Rect ccap = {cx, bottom - ch_s, cw_s, 3};
            SDL_RenderFillRect(ren, &ccap);

            if (on_hot)  { tip_x = hx + hw_s/2;  tip_y = bottom - hh_s;  tip_group = i; tip_is_hot = 1; }
            if (on_cold) { tip_x = cx + cw_s/2;  tip_y = bottom - ch_s;  tip_group = i; tip_is_hot = 0; }

            /* Number labels */
            char hot_lbl[8], cold_lbl[8];
            snprintf(hot_lbl,  sizeof(hot_lbl),  "%d", report->hot[i].number);
            snprintf(cold_lbl, sizeof(cold_lbl), "%d", report->cold[i].number);
            draw_text(ren, font_sm ? font_sm : font, hot_lbl,  x + 2,      bottom + 5, hc);
            draw_text(ren, font_sm ? font_sm : font, cold_lbl, x + hw + 6, bottom + 5, cc);
        }

        if (tip_group >= 0 && font_sm && anim >= 1.0f)
        {
            const HotColdEntry *e = tip_is_hot ? &report->hot[tip_group]
                                               : &report->cold[tip_group];
            char line1[40], line2[48];
            snprintf(line1, sizeof(line1), "Ball %d  (%s rank %d)",
                     e->number, tip_is_hot ? "HOT" : "COLD", tip_group + 1);
            snprintf(line2, sizeof(line2), "%d draws  (%.1f%%)",
                     e->count, e->percentage);
            draw_tooltip(ren, font_sm, line1, line2,
                         tip_x, tip_y, WINDOW_WIDTH, WINDOW_HEIGHT);
        }

        /* Legend box */
        int lx = right - 90, ly = top + 6;
        SDL_Rect box = {lx - 8, ly - 4, 88, 46};
        SDL_SetRenderDrawColor(ren,
            dark_mode==1 ? 30 : 210, dark_mode==1 ? 34 : 215, dark_mode==1 ? 46 : 228, 200);
        SDL_RenderFillRect(ren, &box);
        SDL_SetRenderDrawColor(ren, fg.r, fg.g, fg.b, 255);
        SDL_RenderDrawRect(ren, &box);
        draw_text(ren, font_sm ? font_sm : font, "HOT",  lx, ly,      hot_c);
        draw_text(ren, font_sm ? font_sm : font, "COLD", lx, ly + 22, cold_c);

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    if (font_sm) TTF_CloseFont(font_sm);
    TTF_CloseFont(font);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
