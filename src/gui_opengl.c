#include <GL/gl.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "combogen.h"
#include "gui_opengl.h"
#include "log.h"

#define WINDOW_WIDTH 1400
#define WINDOW_HEIGHT 900

/* Camera & view settings */
#define CAMERA_Z -808.0f

/* Single large drum, sized for future 50-ball scene */
#define DRUM_RADIUS 250.0f
#define DRUM_X 0.0f
#define DRUM_Y -0.2f

/* Drum color */
#define COLOR_DRUM_R 0.20f
#define COLOR_DRUM_G 0.55f
#define COLOR_DRUM_B 0.95f
#define COLOR_GRID_R 0.96f
#define COLOR_GRID_G 0.98f
#define COLOR_GRID_B 1.00f

typedef struct
{
    LotteryInfo info;
    LotteryResult result;
    
    float drum_rotation_x;  /* Rotation angles for tumbling effect */
    float drum_rotation_y;
    float drum_rotation_z;
    
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

            /* Vertex 1 - bottom of strip */
            float x1 = radius * sin_phi0 * cos_theta;
            float y1 = radius * cos_phi0;
            float z1 = radius * sin_phi0 * sin_theta;
            
            glNormal3f(x1 / radius, y1 / radius, z1 / radius);
            glVertex3f(x1, y1, z1);

            /* Vertex 2 - top of strip */
            float x2 = radius * sin_phi1 * cos_theta;
            float y2 = radius * cos_phi1;
            float z2 = radius * sin_phi1 * sin_theta;
            
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

static void render_drum(float x, float y, float rot_x, float rot_y, float rot_z)
{
    glPushMatrix();
    glTranslatef(x, y, 0.0f);

    /* Rotate the drum with 3 axes for tumbling effect */
    glRotatef(rot_x, 1.0f, 0.0f, 0.0f);
    glRotatef(rot_y, 0.0f, 1.0f, 0.0f);
    glRotatef(rot_z, 0.0f, 0.0f, 1.0f);

    /* Draw solid drum sphere */
    glColor4f(COLOR_DRUM_R, COLOR_DRUM_G, COLOR_DRUM_B, 0.12f);
    draw_sphere(DRUM_RADIUS, 64, 44);

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
    glTranslatef(0.0f, 0.6f, CAMERA_Z);

    /* Single large drum preview */
    render_drum(DRUM_X, DRUM_Y, state->drum_rotation_x, state->drum_rotation_y,
                state->drum_rotation_z);

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
    /* Rotate drums with multiple axes for realistic tumbling */
    state->drum_rotation_x += 26.0f * delta_time;
    state->drum_rotation_y += 42.0f * delta_time;
    state->drum_rotation_z += 18.0f * delta_time;

    /* Normalize angles to prevent overflow */
    while (state->drum_rotation_x > 360.0f)
        state->drum_rotation_x -= 360.0f;
    while (state->drum_rotation_y > 360.0f)
        state->drum_rotation_y -= 360.0f;
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
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);

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

    log_info("Displaying %s - single large grid drum preview (capacity target: 50 balls)",
             game_name);

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
                    running = 0;
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
