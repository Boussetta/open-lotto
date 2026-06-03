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

#define WINDOW_WIDTH   900
#define WINDOW_HEIGHT  600

/* Smaller balls, more space */
#define BALL_RADIUS    16.0f
#define BALL_DIAMETER  (BALL_RADIUS * 2.0f)

/* Larger drums */
#define MAIN_DRUM_X    (WINDOW_WIDTH * 0.30f)
#define MAIN_DRUM_Y    (WINDOW_HEIGHT * 0.45f)
#define MAIN_DRUM_R    220.0f

#define EXTRA_DRUM_X   (WINDOW_WIDTH * 0.70f)
#define EXTRA_DRUM_Y   (WINDOW_HEIGHT * 0.45f)
#define EXTRA_DRUM_R   150.0f

#define MAX_BALLS      64

typedef enum {
    BALL_IN_DRUM = 0,
    BALL_MOVING_TO_RESULT,
    BALL_RESULT
} BallState;

typedef struct {
    float x, y;
    float vx, vy;
    float target_x, target_y; /* where to glide to in result row */
    int   number;
    int   is_main;            /* 1 = main drum, 0 = extra drum */
    BallState state;
    int   result_index;       /* index in result row */
} Ball;

typedef struct {
    Ball  balls[MAX_BALLS];
    int   count;          /* total balls in this drum */
    int   active_count;   /* balls still in drum (IN_DRUM or MOVING) */
    int   initial_count;
    float cx, cy, radius;
    float rotation_angle; /* degrees, for visual rotation */
} Drum;

typedef enum {
    ANIMATION_STATE_REVEALING_MAIN,   /* Main drum spinning, picking main numbers */
    ANIMATION_STATE_REVEALING_EXTRA,  /* Extra drum spinning, picking extra numbers */
    ANIMATION_STATE_COMPLETE          /* All numbers revealed, animation done */
} AnimationState;

typedef struct {
    Drum          main_drum;
    Drum          extra_drum;
    LotteryInfo   info;
    LotteryResult result;

    /* Explicit state machine */
    AnimationState animation_state;

    /* Progress tracking */
    int           next_pick_index;     /* 0 .. main_count + extra_count - 1 */
    int           revealed_main;       /* how many main balls fully in row */
    int           revealed_extra;      /* how many extra balls fully in row */

    /* Timing */
    float         time_since_last_pick;
    float         pick_interval;       /* seconds between starting new glides */
} GuiState;

/* --------------------------------------------------------- */
/* Utility                                                   */
/* --------------------------------------------------------- */

static float frand_range(float a, float b)
{
    return a + (b - a) * (rand() / (float)RAND_MAX);
}

static void clamp_to_circle(float cx, float cy, float r, float *x, float *y)
{
    float dx = *x - cx;
    float dy = *y - cy;
    float dist = sqrtf(dx*dx + dy*dy);
    float max_r = r - BALL_RADIUS;

    if (dist > max_r && dist > 0.0001f) {
        float s = max_r / dist;
        *x = cx + dx * s;
        *y = cy + dy * s;
    }
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

    for (int i = 0; i < count; i++) {
        Ball *b = &d->balls[i];
        float angle = frand_range(0.0f, 2.0f * (float)M_PI);
        float r = frand_range(0.0f, radius - BALL_RADIUS - 5.0f);
        b->x = cx + cosf(angle) * r;
        b->y = cy + sinf(angle) * r;
        b->vx = frand_range(-80.0f, 80.0f);
        b->vy = frand_range(-80.0f, 80.0f);
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
    for (int i = 0; i < d->count; i++) {
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
    float energy = d->active_count > 0 ? (d->active_count / (float)d->initial_count) : 0.0f;
    if (energy < 0.2f) energy = 0.2f;

    /* medium TV-style rotation */
    if (d->active_count > 0) {
        d->rotation_angle += 90.0f * dt; /* degrees per second */
        if (d->rotation_angle >= 360.0f)
            d->rotation_angle -= 360.0f;
    }

    for (int i = 0; i < d->count; i++) {
        Ball *b = &d->balls[i];
        if (b->state != BALL_IN_DRUM)
            continue;

        b->x += b->vx * dt;
        b->y += b->vy * dt;

        b->vx += frand_range(-20.0f, 20.0f) * dt;
        b->vy += frand_range(-20.0f, 20.0f) * dt;

        float damp = 0.96f + 0.04f * energy;
        b->vx *= damp;
        b->vy *= damp;

        clamp_to_circle(d->cx, d->cy, d->radius, &b->x, &b->y);
    }

    for (int i = 0; i < d->count; i++) {
        Ball *a = &d->balls[i];
        if (a->state != BALL_IN_DRUM)
            continue;

        for (int j = i + 1; j < d->count; j++) {
            Ball *b = &d->balls[j];
            if (b->state != BALL_IN_DRUM)
                continue;

            float dx = b->x - a->x;
            float dy = b->y - a->y;
            float dist2 = dx*dx + dy*dy;
            float minDist = BALL_DIAMETER;
            if (dist2 < minDist * minDist && dist2 > 0.0001f) {
                float dist = sqrtf(dist2);
                float overlap = (minDist - dist) * 0.5f;
                float nx = dx / dist;
                float ny = dy / dist;

                a->x -= nx * overlap;
                a->y -= ny * overlap;
                b->x += nx * overlap;
                b->y += ny * overlap;

                float rvx = b->vx - a->vx;
                float rvy = b->vy - a->vy;
                float rel = rvx * nx + rvy * ny;
                if (rel < 0.0f) {
                    float impulse = -rel * 0.5f;
                    a->vx -= impulse * nx;
                    a->vy -= impulse * ny;
                    b->vx += impulse * nx;
                    b->vy += impulse * ny;
                }
            }
        }
    }
}

/* smooth glide from drum to result slot */
static void update_glide_to_result(Drum *d, float dt)
{
    for (int i = 0; i < d->count; i++) {
        Ball *b = &d->balls[i];
        if (b->state != BALL_MOVING_TO_RESULT)
            continue;

        float dx = b->target_x - b->x;
        float dy = b->target_y - b->y;

        float dist = sqrtf(dx*dx + dy*dy);
        if (dist < 1.5f) {
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
    for (int dy = -radius; dy <= radius; dy++) {
        int dx_max = (int)sqrtf((float)(radius*radius - dy*dy));
        for (int dx = -dx_max; dx <= dx_max; dx++) {
            SDL_RenderDrawPoint(r, cx + dx, cy + dy);
        }
    }
}

static void render_ball_at(SDL_Renderer *r, TTF_Font *font,
                           float x, float y, int number, int is_main)
{
    SDL_Color col = is_main
        ? (SDL_Color){ 255, 215, 0, 255 }
        : (SDL_Color){  50, 150, 255, 255 };

    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    draw_filled_circle(r, (int)x, (int)y, (int)BALL_RADIUS);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", number);

    SDL_Surface *surf = TTF_RenderText_Blended(font, buf, (SDL_Color){0,0,0,255});
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);
    if (!tex) return;

    int tw, th;
    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
    SDL_Rect dst = {
        (int)x - tw / 2,
        (int)y - th / 2,
        tw, th
    };
    SDL_RenderCopy(r, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}

static void render_drum_outline(SDL_Renderer *r, float cx, float cy, float radius)
{
    SDL_SetRenderDrawColor(r, 200, 200, 200, 255);
    int steps = 128;
    for (int i = 0; i < steps; i++) {
        float a0 = (float)i / steps * 2.0f * (float)M_PI;
        float a1 = (float)(i+1) / steps * 2.0f * (float)M_PI;
        int x0 = (int)(cx + cosf(a0) * radius);
        int y0 = (int)(cy + sinf(a0) * radius);
        int x1 = (int)(cx + cosf(a1) * radius);
        int y1 = (int)(cy + sinf(a1) * radius);
        SDL_RenderDrawLine(r, x0, y0, x1, y1);
    }
}

static void render_number_in_result_row(
    SDL_Renderer *r,
    TTF_Font *font,
    int number,
    float x,
    float y,
    int is_main)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", number);

    SDL_Color col = is_main
        ? (SDL_Color){ 255, 215, 0, 255 }
        : (SDL_Color){  50, 150, 255, 255 };

    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    draw_filled_circle(r, (int)x, (int)y, (int)BALL_RADIUS);

    SDL_Surface *surf = TTF_RenderText_Blended(font, buf, (SDL_Color){0,0,0,255});
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);
    if (!tex) return;
    int tw, th;
    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
    SDL_Rect dst = { (int)x - tw/2, (int)y - th/2, tw, th };
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
    for (int i = 0; i < gs->revealed_main; i++) {
        float x = start_x + spacing * idx++;
        render_number_in_result_row(r, font, gs->result.main_numbers[i], x, y, 1);
    }

    /* only draw revealed extra numbers */
    for (int i = 0; i < gs->revealed_extra; i++) {
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

    if (event == EVENT_DRAW_COMPLETE) {
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

    if (idx < st->info.main_count) {
        /* MAIN DRUM PICK */
        int num = st->result.main_numbers[idx];
        Ball *b = find_ball_by_number(&st->main_drum, num);
        if (b && b->state == BALL_IN_DRUM) {
            float target_x = start_x + spacing * idx;
            float target_y = y;
            b->target_x = target_x;
            b->target_y = target_y;
            b->result_index = idx;
            b->state = BALL_MOVING_TO_RESULT;
        }

        /* Transition to extra drum reveal after all main picks have started */
        if (st->next_pick_index == st->info.main_count) {
            st->animation_state = ANIMATION_STATE_REVEALING_EXTRA;
        }

    } else {
        /* EXTRA (SUPERZAHL) PICK */
        int extra_idx = idx - st->info.main_count;
        int num = st->result.extra_numbers[extra_idx];
        Ball *b = find_ball_by_number(&st->extra_drum, num);
        if (b && b->state == BALL_IN_DRUM) {
            float target_x = start_x + spacing * (st->info.main_count + extra_idx);
            float target_y = y;
            b->target_x = target_x;
            b->target_y = target_y;
            b->result_index = extra_idx;
            b->state = BALL_MOVING_TO_RESULT;
        }

        /* Transition to animation complete after all picks have started */
        if (st->next_pick_index == total) {
            st->animation_state = ANIMATION_STATE_COMPLETE;
        }
    }
}

/* recompute how many balls are fully in the result row */
static void update_revealed_counts(GuiState *st)
{
    int max_main = -1;
    int max_extra = -1;

    for (int i = 0; i < st->main_drum.count; i++) {
        Ball *b = &st->main_drum.balls[i];
        if (b->state == BALL_RESULT && b->result_index >= 0) {
            if (b->result_index > max_main)
                max_main = b->result_index;
        }
    }
    for (int i = 0; i < st->extra_drum.count; i++) {
        Ball *b = &st->extra_drum.balls[i];
        if (b->state == BALL_RESULT && b->result_index >= 0) {
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

void gui_run(const char *game_name, const LotteryInfo *info)
{
    /* Validate input parameters */
    if (!game_name || game_name[0] == '\0') {
        fprintf(stderr, "Error: Game name is NULL or empty\n");
        return;
    }
    
    if (!info) {
        fprintf(stderr, "Error: LotteryInfo pointer is NULL\n");
        return;
    }
    
    /* Validate lottery info structure */
    if (info->main_count <= 0 || info->main_count > 7) {
        fprintf(stderr, "Error: Invalid main_count in LotteryInfo: %d\n", info->main_count);
        return;
    }
    
    if (info->extra_count < 0 || info->extra_count > 3) {
        fprintf(stderr, "Error: Invalid extra_count in LotteryInfo: %d\n", info->extra_count);
        return;
    }
    
    if (info->main_min <= 0 || info->main_max <= 0 || info->main_min > info->main_max) {
        fprintf(stderr, "Error: Invalid main range in LotteryInfo: [%d, %d]\n", 
                info->main_min, info->main_max);
        return;
    }
    
    if (info->extra_count > 0 && 
        (info->extra_min < 0 || info->extra_max < 0 || info->extra_min > info->extra_max)) {
        fprintf(stderr, "Error: Invalid extra range in LotteryInfo: [%d, %d]\n", 
                info->extra_min, info->extra_max);
        return;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return;
    }

    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        SDL_Quit();
        return;
    }

    SDL_Window *win = SDL_CreateWindow(
        game_name ? game_name : "Lotto",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN
    );
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        return;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        TTF_Quit();
        SDL_Quit();
        return;
    }

    char font_path[512];
    snprintf(font_path, sizeof(font_path), "%s/fonts/Roboto-Bold.ttf", PROJECT_ROOT_DIR);
    TTF_Font *font = TTF_OpenFont(font_path, 22);
    if (!font) {
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
    state.pick_interval = 1.5f;   /* seconds between starting new glides */

    srand((unsigned)time(NULL));

    init_drum(&state.main_drum, info->main_max, MAIN_DRUM_X, MAIN_DRUM_Y, MAIN_DRUM_R, 1);
    init_drum(&state.extra_drum, info->extra_max, EXTRA_DRUM_X, EXTRA_DRUM_Y, EXTRA_DRUM_R, 0);

    LotteryResult res;
    generate_draw(
        info->main_count, info->main_min, info->main_max,
        info->extra_count, info->extra_min, info->extra_max,
        &res,
        gui_draw_callback
    );

    Uint32 last = SDL_GetTicks();
    int running = 1;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT)
                running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
                running = 0;
        }

        Uint32 now = SDL_GetTicks();
        float dt = (now - last) / 1000.0f;
        if (dt > 0.05f) dt = 0.05f;
        last = now;

        /* update timers and maybe start a new pick */
        state.time_since_last_pick += dt;
        if (state.time_since_last_pick >= state.pick_interval) {
            state.time_since_last_pick = 0.0f;
            maybe_start_next_pick(&state);
        }

        /* physics: main drum always active */
        update_ball_physics(&state.main_drum, dt);
        update_glide_to_result(&state.main_drum, dt);

        /* extra drum only active during REVEALING_EXTRA and COMPLETE states */
        if (state.animation_state >= ANIMATION_STATE_REVEALING_EXTRA) {
            update_ball_physics(&state.extra_drum, dt);
            update_glide_to_result(&state.extra_drum, dt);
        }

        /* recompute how many balls are fully in the result row */
        update_revealed_counts(&state);

        SDL_SetRenderDrawColor(ren, 10, 10, 20, 255);
        SDL_RenderClear(ren);

        /* draw drums */
        render_drum_outline(ren, state.main_drum.cx, state.main_drum.cy, state.main_drum.radius);
        if (state.animation_state >= ANIMATION_STATE_REVEALING_EXTRA) {
            render_drum_outline(ren, state.extra_drum.cx, state.extra_drum.cy, state.extra_drum.radius);
        }

        /* draw balls in main drum (with rotation for IN_DRUM) */
        {
            float angle_rad = state.main_drum.rotation_angle * (float)M_PI / 180.0f;
            float cosA = cosf(angle_rad);
            float sinA = sinf(angle_rad);

            for (int i = 0; i < state.main_drum.count; i++) {
                Ball *b = &state.main_drum.balls[i];

                if (b->state == BALL_IN_DRUM) {
                    float relx = b->x - state.main_drum.cx;
                    float rely = b->y - state.main_drum.cy;
                    float rx = state.main_drum.cx + relx * cosA - rely * sinA;
                    float ry = state.main_drum.cy + relx * sinA + rely * cosA;
                    render_ball_at(ren, font, rx, ry, b->number, b->is_main);
                } else if (b->state == BALL_MOVING_TO_RESULT) {
                    render_ball_at(ren, font, b->x, b->y, b->number, b->is_main);
                }
            }
        }

        /* draw balls in extra drum (only after it starts) */
        if (state.animation_state >= ANIMATION_STATE_REVEALING_EXTRA) {
            float angle_rad = state.extra_drum.rotation_angle * (float)M_PI / 180.0f;
            float cosA = cosf(angle_rad);
            float sinA = sinf(angle_rad);

            for (int i = 0; i < state.extra_drum.count; i++) {
                Ball *b = &state.extra_drum.balls[i];

                if (b->state == BALL_IN_DRUM) {
                    float relx = b->x - state.extra_drum.cx;
                    float rely = b->y - state.extra_drum.cy;
                    float rx = state.extra_drum.cx + relx * cosA - rely * sinA;
                    float ry = state.extra_drum.cy + relx * sinA + rely * cosA;
                    render_ball_at(ren, font, rx, ry, b->number, b->is_main);
                } else if (b->state == BALL_MOVING_TO_RESULT) {
                    render_ball_at(ren, font, b->x, b->y, b->number, b->is_main);
                }
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
