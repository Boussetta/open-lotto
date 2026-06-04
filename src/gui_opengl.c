#include <GL/gl.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "combogen.h"
#include "gui_opengl.h"
#include "log.h"

#define WINDOW_WIDTH 1400
#define WINDOW_HEIGHT 900

/* Camera & view settings */
#define CAMERA_Z -808.0f
#define CAMERA_TRIMETRIC_X 0.0f
#define CAMERA_TRIMETRIC_Y 143.0f
#define CAMERA_TRIMETRIC_Z 0.0f
#define CAMERA_PITCH_MIN -85.0f
#define CAMERA_PITCH_MAX 85.0f
#define MOUSE_ORBIT_SENSITIVITY 0.25f
#define MOUSE_ZOOM_STEP 20.0f
#define CAMERA_Z_MIN -1500.0f
#define CAMERA_Z_MAX -120.0f

/* Single large drum, sized for future 50-ball scene */
#define DRUM_RADIUS 250.0f
#define DRUM_X 0.0f
#define DRUM_Y -0.2f

/* Ball simulation (initial state only) */
#define DRUM_BALL_COUNT 50
#define BALL_RADIUS 27.0f
#define BALL_GRAVITY 360.0f
#define BALL_BOUNCE_DAMPING 0.35f
#define BALL_SETTLE_SPEED 8.0f

/* Drum color */
#define COLOR_DRUM_R 0.20f
#define COLOR_DRUM_G 0.55f
#define COLOR_DRUM_B 0.95f
#define COLOR_GRID_R 0.96f
#define COLOR_GRID_G 0.98f
#define COLOR_GRID_B 1.00f

typedef struct
{
    float x;
    float y;
    float z;
    float vy;
    int settled;
} DrumBall;

typedef enum
{
    DRUM_PHASE_FALLING = 0,
    DRUM_PHASE_ROTATING
} DrumPhase;

typedef struct
{
    LotteryInfo info;
    LotteryResult result;
    
    float drum_rotation_x;  /* Rotation angles for tumbling effect */
    float drum_rotation_y;
    float drum_rotation_z;

    DrumBall balls[DRUM_BALL_COUNT];
    int ball_count;
    DrumPhase phase;

    float camera_pitch;
    float camera_yaw;
    float camera_roll;
    float camera_z;

    int mouse_dragging;
    int last_mouse_x;
    int last_mouse_y;
    
    SDL_Window *window;
    SDL_GLContext gl_context;
    
    int animation_complete;
} GuiState3D;

/* ============================================================
   OPENGL UTILITIES
   ============================================================ */

static void check_gl_error(const char *context)
{
    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
    {
        log_warn("OpenGL error in %s: %x", context, err);
    }
}

static float frand_range(float a, float b)
{
    return a + (b - a) * ((float)rand() / (float)RAND_MAX);
}

static void init_balls(GuiState3D *state)
{
    state->ball_count = DRUM_BALL_COUNT;
    state->phase = DRUM_PHASE_FALLING;

    for (int i = 0; i < state->ball_count; i++)
    {
        float x, z;
        float spawn_r = DRUM_RADIUS * 0.60f;
        do
        {
            x = frand_range(-spawn_r, spawn_r);
            z = frand_range(-spawn_r, spawn_r);
        } while ((x * x + z * z) > (spawn_r * spawn_r));

        state->balls[i].x = x;
        state->balls[i].z = z;
        state->balls[i].y = frand_range(DRUM_RADIUS * 0.25f, DRUM_RADIUS * 0.75f);
        state->balls[i].vy = 0.0f;
        state->balls[i].settled = 0;
    }
}

/* Draw a smooth sphere using triangle strips with proper normals */
static void draw_sphere(float radius, int slices, int stacks)
{
    for (int i = 0; i < stacks; i++)
    {
        float phi0 = (float)i / stacks * M_PI;
        float phi1 = (float)(i + 1) / stacks * M_PI;
        float sin_phi0 = sinf(phi0);
        float sin_phi1 = sinf(phi1);
        float cos_phi0 = cosf(phi0);
        float cos_phi1 = cosf(phi1);

        glBegin(GL_TRIANGLE_STRIP);
        for (int j = 0; j <= slices; j++)
        {
            float theta = (float)j / slices * 2.0f * M_PI;
            float cos_theta = cosf(theta);
            float sin_theta = sinf(theta);

            /* Vertex 1 - poles aligned on Z axis */
            float x1 = radius * sin_phi0 * cos_theta;
            float y1 = radius * sin_phi0 * sin_theta;
            float z1 = radius * cos_phi0;
            
            glNormal3f(x1 / radius, y1 / radius, z1 / radius);
            glVertex3f(x1, y1, z1);

            /* Vertex 2 - poles aligned on Z axis */
            float x2 = radius * sin_phi1 * cos_theta;
            float y2 = radius * sin_phi1 * sin_theta;
            float z2 = radius * cos_phi1;
            
            glNormal3f(x2 / radius, y2 / radius, z2 / radius);
            glVertex3f(x2, y2, z2);
        }
        glEnd();
    }
}

/* Draw a wireframe outline of the sphere for definition */
static void draw_sphere_frame(float radius, int slices, int stacks)
{
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glLineWidth(3.0f);
    glColor3f(COLOR_GRID_R, COLOR_GRID_G, COLOR_GRID_B);
    glDisable(GL_LIGHTING);
    
    draw_sphere(radius * 1.005f, slices, stacks);
    
    glEnable(GL_LIGHTING);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glLineWidth(1.0f);
}

/* ============================================================
   SETUP & INITIALIZATION
   ============================================================ */

static void setup_opengl(void)
{
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
    glClearDepth(1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* Lighting */
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHT1);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glShadeModel(GL_SMOOTH);

    /* Main light from upper right */
    float light0_pos[] = {5.0f, 5.0f, 5.0f, 1.0f};
    float light0_amb[] = {0.3f, 0.3f, 0.3f, 1.0f};
    float light0_diff[] = {1.0f, 1.0f, 1.0f, 1.0f};
    float light0_spec[] = {1.0f, 1.0f, 1.0f, 1.0f};

    glLightfv(GL_LIGHT0, GL_POSITION, light0_pos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, light0_amb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light0_diff);
    glLightfv(GL_LIGHT0, GL_SPECULAR, light0_spec);

    /* Fill light from opposite side */
    float light1_pos[] = {-5.0f, -3.0f, 5.0f, 1.0f};
    float light1_amb[] = {0.0f, 0.0f, 0.0f, 1.0f};
    float light1_diff[] = {0.5f, 0.5f, 0.5f, 1.0f};

    glLightfv(GL_LIGHT1, GL_POSITION, light1_pos);
    glLightfv(GL_LIGHT1, GL_AMBIENT, light1_amb);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, light1_diff);

    /* Material properties */
    float mat_specular[] = {1.0f, 1.0f, 1.0f, 1.0f};
    float mat_shininess[] = {100.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mat_specular);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, mat_shininess);

    check_gl_error("setup_opengl");
}

static GuiState3D *gui_state_create(const char *unused_game_name, const LotteryInfo *info)
{
    (void)unused_game_name;

    GuiState3D *state = (GuiState3D *)malloc(sizeof(GuiState3D));
    if (!state)
        return NULL;

    memset(state, 0, sizeof(GuiState3D));
    state->info = *info;
    state->animation_complete = 0;

    state->camera_pitch = CAMERA_TRIMETRIC_X;
    state->camera_yaw = CAMERA_TRIMETRIC_Y;
    state->camera_roll = CAMERA_TRIMETRIC_Z;
    state->camera_z = CAMERA_Z;
    state->mouse_dragging = 0;

    srand((unsigned int)time(NULL));
    init_balls(state);

    return state;
}

static void gui_state_destroy(GuiState3D *state)
{
    if (state)
        free(state);
}

/* ============================================================
   RENDERING
   ============================================================ */

static void render_drum(float x, float y, float rot_x, float rot_y, float rot_z,
                        const GuiState3D *state)
{
    glPushMatrix();
    glTranslatef(x, y, 0.0f);

    /* Rotate the drum with 3 axes for tumbling effect */
    glRotatef(rot_x, 1.0f, 0.0f, 0.0f);
    glRotatef(rot_y, 0.0f, 1.0f, 0.0f);
    glRotatef(rot_z, 0.0f, 0.0f, 1.0f);

    /* Balls inside drum */
    for (int i = 0; i < state->ball_count; i++)
    {
        const DrumBall *ball = &state->balls[i];
        glPushMatrix();
        glTranslatef(ball->x, ball->y, ball->z);
        if (ball->settled)
            glColor3f(1.0f, 0.86f, 0.25f);
        else
            glColor3f(0.96f, 0.96f, 0.96f);
        draw_sphere(BALL_RADIUS, 18, 12);
        glPopMatrix();
    }

    /* Draw solid drum sphere */
    glDepthMask(GL_FALSE);
    glColor4f(COLOR_DRUM_R, COLOR_DRUM_G, COLOR_DRUM_B, 0.12f);
    draw_sphere(DRUM_RADIUS, 64, 44);
    glDepthMask(GL_TRUE);

    /* Draw wireframe outline for definition */
    draw_sphere_frame(DRUM_RADIUS, 28, 20);

    glPopMatrix();
}

static void render_scene(GuiState3D *state)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    
    float aspect = (float)WINDOW_WIDTH / (float)WINDOW_HEIGHT;
    float fov = 50.0f * M_PI / 180.0f;
    float f = 1.0f / tanf(fov / 2.0f);
    
    glFrustum(-1.2f / f * aspect, 1.2f / f * aspect, -1.2f / f, 1.2f / f, 1.0f, 5000.0f);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.6f, state->camera_z);

    /* Trimetric camera view: three distinct axis rotations */
    glRotatef(state->camera_pitch, 1.0f, 0.0f, 0.0f);
    glRotatef(state->camera_yaw, 0.0f, 1.0f, 0.0f);
    glRotatef(state->camera_roll, 0.0f, 0.0f, 1.0f);

    /* Single large drum preview */
    render_drum(DRUM_X, DRUM_Y, state->drum_rotation_x, state->drum_rotation_y,
                state->drum_rotation_z, state);

    check_gl_error("render_scene");
}

/* ============================================================
   ANIMATION
   ============================================================ */

static void on_draw_event(DrawEvent event, const LotteryResult *res)
{
    (void)event;
    (void)res;
}

static void update_animation(GuiState3D *state, float delta_time)
{
    if (state->phase == DRUM_PHASE_FALLING)
    {
        int settled_count = 0;

        for (int i = 0; i < state->ball_count; i++)
        {
            DrumBall *ball = &state->balls[i];

            if (ball->settled)
            {
                settled_count++;
                continue;
            }

            ball->vy -= BALL_GRAVITY * delta_time;
            ball->y += ball->vy * delta_time;

            float radial_sq = ball->x * ball->x + ball->z * ball->z;
            float floor_y;
            float inside = (DRUM_RADIUS - BALL_RADIUS) * (DRUM_RADIUS - BALL_RADIUS) - radial_sq;

            if (inside <= 0.0f)
            {
                floor_y = -DRUM_RADIUS + BALL_RADIUS;
            }
            else
            {
                floor_y = -sqrtf(inside);
            }

            if (ball->y <= floor_y)
            {
                ball->y = floor_y;
                if (fabsf(ball->vy) < BALL_SETTLE_SPEED)
                {
                    ball->vy = 0.0f;
                    ball->settled = 1;
                    settled_count++;
                }
                else
                {
                    ball->vy = -ball->vy * BALL_BOUNCE_DAMPING;
                }
            }
        }

        if (settled_count == state->ball_count)
        {
            state->phase = DRUM_PHASE_ROTATING;
            log_info("Balls settled at drum bottom; starting drum rotation");
        }

        return;
    }

    /* Rotate drum around one axis only (Z axis) */
    state->drum_rotation_z += 70.0f * delta_time;

    /* Normalize angles to prevent overflow */
    while (state->drum_rotation_z > 360.0f)
        state->drum_rotation_z -= 360.0f;
}

/* ============================================================
   MAIN GUI FUNCTION
   ============================================================ */

void gui_run_opengl(const char *game_name, const LotteryInfo *info)
{
    log_info("Launching 3D OpenGL GUI (Sphere Drums) for %s", game_name);

    GuiState3D *state = gui_state_create(game_name, info);
    if (!state)
    {
        log_error("Failed to create GUI state");
        return;
    }

    /* Initialize SDL with OpenGL */
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        log_error("SDL initialization failed: %s", SDL_GetError());
        gui_state_destroy(state);
        return;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    state->window = SDL_CreateWindow(
        game_name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);

    if (!state->window)
    {
        log_error("Window creation failed: %s", SDL_GetError());
        SDL_Quit();
        gui_state_destroy(state);
        return;
    }

    state->gl_context = SDL_GL_CreateContext(state->window);
    if (!state->gl_context)
    {
        log_error("OpenGL context creation failed: %s", SDL_GetError());
        SDL_DestroyWindow(state->window);
        SDL_Quit();
        gui_state_destroy(state);
        return;
    }

    SDL_GL_SetSwapInterval(1);  /* Enable vsync */

    setup_opengl();

    /* Generate the lottery draw (we won't display numbers yet, just show drums) */
    generate_draw(info->main_count, info->main_min, info->main_max, info->extra_count,
                  info->extra_min, info->extra_max, &state->result, on_draw_event);

    log_info("Displaying %s - 50 balls start inside, fall first, then drum rotates",
             game_name);
    log_info("Mouse controls: hold left button and drag to orbit, wheel to zoom");

    /* Main loop */
    int running = 1;
    Uint32 last_time = SDL_GetTicks();

    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
                case SDL_QUIT:
                    running = 0;
                    break;
                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_ESCAPE)
                        running = 0;
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT)
                    {
                        state->mouse_dragging = 1;
                        state->last_mouse_x = event.button.x;
                        state->last_mouse_y = event.button.y;
                    }
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (event.button.button == SDL_BUTTON_LEFT)
                    {
                        state->mouse_dragging = 0;
                    }
                    break;
                case SDL_MOUSEMOTION:
                    if (state->mouse_dragging)
                    {
                        int dx = event.motion.x - state->last_mouse_x;

                        state->camera_yaw += dx * MOUSE_ORBIT_SENSITIVITY;

                        state->last_mouse_x = event.motion.x;
                        state->last_mouse_y = event.motion.y;
                    }
                    break;
                case SDL_MOUSEWHEEL:
                    state->camera_z += event.wheel.y * MOUSE_ZOOM_STEP;
                    if (state->camera_z < CAMERA_Z_MIN)
                        state->camera_z = CAMERA_Z_MIN;
                    if (state->camera_z > CAMERA_Z_MAX)
                        state->camera_z = CAMERA_Z_MAX;
                    break;
            }
        }

        Uint32 current_time = SDL_GetTicks();
        float delta_time = (current_time - last_time) / 1000.0f;
        if (delta_time > 0.05f)
            delta_time = 0.05f;  /* Cap at 50ms */
        last_time = current_time;

        /* Update animation */
        update_animation(state, delta_time);

        /* Render */
        render_scene(state);
        SDL_GL_SwapWindow(state->window);

        SDL_Delay(16);  /* ~60 FPS */
    }

    log_info("Closing 3D Drum Display");

    SDL_GL_DeleteContext(state->gl_context);
    SDL_DestroyWindow(state->window);
    SDL_Quit();
    gui_state_destroy(state);
}
