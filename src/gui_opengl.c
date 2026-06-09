/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifdef _WIN32
/* On Windows, GLEW provides OpenGL extension prototypes and loads function
 * pointers via wglGetProcAddress — required because opengl32.dll only exports
 * OpenGL 1.1 symbols. GLEW must be included before any other GL headers. */
#include <GL/glew.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#endif
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_ttf.h>
#include <math.h>
#include <omp.h> // NOLINT(clang-diagnostic-error)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef PROJECT_ROOT_DIR
#define PROJECT_ROOT_DIR "."
#endif

#include "combogen.h"
#include "gui_opengl.h"
#include "log.h"
#include "theme.h"

#define WINDOW_WIDTH 1400
#define WINDOW_HEIGHT 900

/* Camera & view settings */
#define CAMERA_Z (-808.0f)
#define CAMERA_TRIMETRIC_X 0.0f
#define CAMERA_TRIMETRIC_Y 143.0f
#define CAMERA_TRIMETRIC_Z 0.0f
#define CAMERA_TOP_X (-90.0f)
#define CAMERA_TOP_Y 0.0f
#define CAMERA_FRONT_X 0.0f
#define CAMERA_FRONT_Y 0.0f
#define CAMERA_SIDE_X 0.0f
#define CAMERA_SIDE_Y 90.0f
#define CAMERA_PITCH_MIN (-85.0f)
#define CAMERA_PITCH_MAX 85.0f
#define MOUSE_ORBIT_SENSITIVITY 0.25f
#define MOUSE_ZOOM_STEP 20.0f
#define CAMERA_Z_MIN (-1500.0f)
#define CAMERA_Z_MAX (-120.0f)

/* Animation speed control */
#define ANIMATION_SPEED_MIN 0.1f
#define ANIMATION_SPEED_MAX 4.0f
#define ANIMATION_SPEED_STEP 0.2f
#define ANIMATION_SPEED_DEFAULT 1.0f

/* Main drum */
#define DRUM_RADIUS 250.0f
#define DRUM_X 0.0f
#define DRUM_Y (-0.2f)
/* Extra drum (smaller, to the right) */
#define EXTRA_DRUM_RADIUS 150.0f
#define EXTRA_DRUM_X 0.0f
#define EXTRA_DRUM_Y (-0.2f)

/* Ball simulation (initial state only) */
#define MAX_GAME_BALLS 128
#define BALL_RADIUS 27.0f
#define BALL_GRAVITY 620.0f
#define BALL_BOUNCE_DAMPING 0.28f
#define BALL_SETTLE_SPEED 10.0f
#define DRUM_ROTATION_SPEED_DEG 140.0f
#define BALL_AIR_DAMPING 0.9988f
#define BALL_CONTACT_FRICTION 6.0f
#define BALL_COLLISION_RESTITUTION 0.97f
#define BALL_COLLISION_TANGENTIAL_TRANSFER 0.14f
#define BALL_COLLISION_IMPULSE_BOOST 1.55f
#define BALL_COLLISION_BURST_SPEED 13.0f
#define BALL_ROLLING_FRICTION 0.45f /* rolling resistance damping */
#define BALL_ANGULAR_DAMPING 0.985f /* air damping for rotation */
#define BALL_SHELL_FRICTION 0.22f   /* friction coefficient on drum shell */
#define BALL_MOMENT_OF_INERTIA 0.4f /* I = (2/5) * m * r^2 for sphere */
#define BALL_TRAIL_LENGTH 10        /* ring-buffer depth for trail visualization */
#define COLLISION_CELL_SIZE (BALL_RADIUS * 2.2f)
#define COLLISION_GRID_MAX_DIM 16
#define COLLISION_GRID_MAX_CELLS                                                                   \
    (COLLISION_GRID_MAX_DIM * COLLISION_GRID_MAX_DIM * COLLISION_GRID_MAX_DIM)
#define GPU_COMPUTE_LOCAL_SIZE 64

/* Drum color */
#define COLOR_DRUM_R 0.05f
#define COLOR_DRUM_G 0.05f
#define COLOR_DRUM_B 0.05f
#define COLOR_GRID_R 0.30f
#define COLOR_GRID_G 0.30f
#define COLOR_GRID_B 0.30f

/* Unified ball color for all balls (drum + picked overlay) */
#define BALL_COLOR_R 0.20f
#define BALL_COLOR_G 0.72f
#define BALL_COLOR_B 0.35f
#define SUPER_BALL_COLOR_R 0.95f
#define SUPER_BALL_COLOR_G 0.82f
#define SUPER_BALL_COLOR_B 0.20f

typedef struct
{
    float x;
    float y;
    float z;
    float vx;
    float vy;
    float vz;
    float rot_x; /* angular velocity (rotation around X axis) */
    float rot_y; /* angular velocity (rotation around Y axis) */
    float rot_z; /* angular velocity (rotation around Z axis) */
    int settled;
    int picked;      /* 1 = this ball has been drawn */
    int ball_number; /* 1-based number shown on the ball */
} DrumBall;

typedef struct
{
    float px;
    float py;
    float pz;
    float settled;
    float vx;
    float vy;
    float vz;
    float pad;
} GpuBall;

typedef enum
{
    DRUM_PHASE_FALLING = 0,
    DRUM_PHASE_ROTATING,     /* drum spinning, balls tumbling */
    DRUM_PHASE_STOPPING,     /* drum decelerating before a pick */
    DRUM_PHASE_PICK_PAUSE,   /* drum stopped, picked ball highlighted / floats out */
    DRUM_PHASE_DRAW_COMPLETE /* all numbers drawn */
} DrumPhase;

/* A ball that has been drawn and is sitting outside the drum in the result row */
typedef struct
{
    int ball_number;
    float x, y, z;    /* world position (outside drum) */
    float vx, vy, vz; /* velocity while flying to target */
    float tx, ty, tz; /* target world position */
    int arrived;      /* 1 once it has reached its slot */
} PickedBallDisplay;

/* Self-contained state for one physical drum (main or extra) */
typedef struct
{
    /* Balls in this drum */
    DrumBall *balls;
    int ball_count; /* total ball count for this drum */
    int ball_min;   /* ball_number = ball_min + i  (so Superzahl shows 0..9) */

    /* Drum rotation */
    float drum_rotation_x;
    float drum_rotation_y;
    float drum_rotation_z;
    float drum_radius; /* physical radius of this drum */

    /* Physics phase */
    DrumPhase phase;
    float sim_time; /* local time since drum was started (used for FALLING timeout) */

    /* Draw cycle */
    int picks_done;
    int picks_total;
    float phase_timer;
    float spin_before_pick;
    int current_pick_idx;
    float stop_omega;

    /* Numbers to draw (filled from LotteryResult before drum starts) */
    int draw_numbers[16]; /* up to 12 extra or 7 main */

    /* Result row for this drum */
    PickedBallDisplay result_balls[16];
    int result_ball_count;

    /* World position of this drum centre */
    float world_x;
    float world_y;

    /* Textures: one per ball, indexed by ball_number */
    GLuint *number_textures; /* size = ball_min + ball_count (use ball_number as index) */
    int texture_count;       /* = ball_min + ball_count */

    /* Waiting for another drum to finish before starting */
    int waiting;  /* 1 = stays in FALLING until cleared externally */
    int is_extra; /* 1 for superzahl/euro-number drum */

    /* Ball trail ring buffers for debug visualization (NULL when not allocated) */
    float *trail_x; /* [ball_count * BALL_TRAIL_LENGTH] */
    float *trail_y;
    float *trail_z;
    int *trail_head;         /* [ball_count] next-write index per ball */
    int trail_frame_counter; /* counts frames between trail recordings */
} DrumInstance;

typedef struct
{
    LotteryInfo info;
    LotteryResult result;

    /* Two independent drums */
    DrumInstance *main_drum;  /* always present */
    DrumInstance *extra_drum; /* NULL if extra_count == 0 */

    /* Camera */
    float camera_pitch;
    float camera_yaw;
    float camera_roll;
    float camera_z;

    int mouse_dragging;
    int last_mouse_x;
    int last_mouse_y;

    SDL_Window *window;
    SDL_GLContext gl_context;

    /* GPU compute (main drum only) */
    int use_gpu_compute;
    int _gpu_attempt_enabled;
    GLuint compute_program;
    GLuint ball_ssbo;
    GpuBall *gpu_ball_cache;

    TTF_Font *font;         /* ball number font */
    TTF_Font *overlay_font; /* smaller HUD font for debug overlay */

    int animation_complete;

    /* Debug overlay & pause */
    int debug_overlay;    /* 1 when --debug-overlay is active */
    int paused;           /* 1 when animation is paused (Space bar) */
    float fps_current;    /* smoothed frames-per-second */
    int fps_frame_count;  /* frames counted since last FPS sample */
    Uint32 fps_last_time; /* SDL ticks at last FPS sample */

    /* Theme & dark mode */
    int dark_mode; /* -1=auto, 0=off (light), 1=on (dark) */

    /* Animation speed control */
    float animation_speed_multiplier; /* 0.25 = 0.25x speed, 1.0 = normal, 4.0 = 4x speed */

    /* CPU usage tracking (per-core percentages) */
    float *cpu_usage;        /* [num_cores] usage percentages */
    int num_cores;           /* number of CPU cores */
    unsigned long *prev_cpu; /* [num_cores * 4] previous CPU stats (user, nice, system, idle) */
    Uint32 cpu_last_update;  /* last time CPU stats were updated */
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

/* Read CPU usage statistics from /proc/stat and calculate per-core percentages */
static void update_cpu_usage(GuiState3D *state)
{
    Uint32 now = SDL_GetTicks();
    /* Update CPU stats every 500ms to avoid excessive file I/O */
    if (now - state->cpu_last_update < 500)
        return;
    state->cpu_last_update = now;

    FILE *fp = fopen("/proc/stat", "r");
    if (!fp)
        return;

    char line[256];
    int core_idx = 0;
    while (fgets(line, sizeof(line), fp) && core_idx < state->num_cores)
    {
        if (strncmp(line, "cpu", 3) != 0)
            break;
        if (line[3] < '0' || line[3] > '9')
            continue; /* skip "cpu" line, only process cpu0, cpu1, etc */

        unsigned long user, nice, system, idle;
        if (sscanf(line, "cpu%*d %lu %lu %lu %lu", &user, &nice, &system, &idle) == 4)
        {
            unsigned long prev_user = state->prev_cpu[core_idx * 4];
            unsigned long prev_nice = state->prev_cpu[core_idx * 4 + 1];
            unsigned long prev_system = state->prev_cpu[core_idx * 4 + 2];
            unsigned long prev_idle = state->prev_cpu[core_idx * 4 + 3];

            unsigned long total_diff =
                (user + nice + system + idle) - (prev_user + prev_nice + prev_system + prev_idle);

            if (total_diff > 0)
            {
                unsigned long idle_diff = idle - prev_idle;
                state->cpu_usage[core_idx] = 100.0f * (1.0f - (float)idle_diff / (float)total_diff);
            }
            else
            {
                state->cpu_usage[core_idx] = 0.0f;
            }

            state->prev_cpu[core_idx * 4] = user;
            state->prev_cpu[core_idx * 4 + 1] = nice;
            state->prev_cpu[core_idx * 4 + 2] = system;
            state->prev_cpu[core_idx * 4 + 3] = idle;
            core_idx++;
        }
    }

    fclose(fp);
}

static const char *BALL_COMPUTE_SHADER_SRC =
    "#version 430\n"
    "layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;\n"
    "struct Ball { vec4 pos; vec4 vel; };\n"
    "layout(std430, binding = 0) buffer BallBuffer { Ball balls[]; };\n"
    "uniform int uBallCount;\n"
    "uniform float uDeltaTime;\n"
    "uniform float uDrumRadius;\n"
    "uniform float uBallRadius;\n"
    "uniform float uGravity;\n"
    "uniform float uAirDamping;\n"
    "uniform float uOmega;\n"
    "uniform float uDrumRotationZDeg;\n"
    "uniform float uCollisionRestitution;\n"
    "uniform float uCollisionTangentialTransfer;\n"
    "uniform float uCollisionImpulseBoost;\n"
    "uniform float uCollisionBurstSpeed;\n"
    "uniform float uContactFriction;\n"
    "void main() {\n"
    "  uint i = gl_GlobalInvocationID.x;\n"
    "  if (i >= uint(uBallCount)) return;\n"
    "  vec3 pos = balls[i].pos.xyz;\n"
    "  vec3 vel = balls[i].vel.xyz;\n"
    "  float theta = radians(uDrumRotationZDeg);\n"
    "  vec3 g = vec3(-uGravity * sin(theta), -uGravity * cos(theta), 0.0);\n"
    "  vel += g * uDeltaTime;\n"
    "  vel *= uAirDamping;\n"
    "  vel.z *= 0.88;\n" /* drum spins around Z: kill Z drift each step */
    "  float collisionDist = 2.0 * uBallRadius;\n"
    "  for (int j = 0; j < uBallCount; ++j) {\n"
    "    if (j == int(i)) continue;\n"
    "    vec3 op = balls[j].pos.xyz;\n"
    "    vec3 ov = balls[j].vel.xyz;\n"
    "    vec3 d = op - pos;\n"
    "    float distSq = dot(d, d);\n"
    "    if (distSq < collisionDist * collisionDist && distSq > 1e-6) {\n"
    "      float dist = sqrt(distSq);\n"
    "      vec3 n = d / dist;\n"
    "      float overlap = collisionDist - dist;\n"
    "      pos -= n * (overlap * 0.35);\n"
    "      vec3 dv = ov - vel;\n"
    "      float relN = dot(dv, n);\n"
    "      if (relN < 0.0) {\n"
    "        float impulse = -((1.0 + uCollisionRestitution) * relN) * 0.5;\n"
    "        float burst = uCollisionBurstSpeed * (overlap / collisionDist);\n"
    "        impulse = impulse * uCollisionImpulseBoost + burst;\n"
    "        vel -= impulse * n;\n"
    "        vec3 tv = dv - relN * n;\n"
    "        vel += tv * (uCollisionTangentialTransfer * 0.5);\n"
    "      }\n"
    "    }\n"
    "  }\n"
    "  pos += vel * uDeltaTime;\n"
    "  float innerR = uDrumRadius - uBallRadius;\n"
    "  float distCenter = length(pos);\n"
    "  if (distCenter > innerR && distCenter > 1e-6) {\n"
    "    vec3 n = pos / distCenter;\n"
    "    pos -= n * (distCenter - innerR);\n"
    "    float vdotn = dot(vel, n);\n"
    "    if (vdotn > 0.0) {\n"
    "      float shellRestitution = 0.55;\n"
    "      vel -= (1.0 + shellRestitution) * vdotn * n;\n"
    "    }\n"
    "    float radial = length(pos.xy);\n"
    "    if (radial > 0.1) {\n"
    "      vec2 t = vec2(-pos.y / radial, pos.x / radial);\n"
    "      float targetTan = uOmega * radial;\n"
    "      float currentTan = dot(vel.xy, t);\n"
    "      float minFollow = targetTan * 0.35;\n"
    "      if (currentTan < minFollow) {\n"
    "        float grip = uContactFriction * 0.35;\n"
    "        float deltaTan = (minFollow - currentTan) * grip * uDeltaTime;\n"
    "        vel.xy += t * deltaTan;\n"
    "      }\n"
    "    }\n"
    "  }\n"
    "  balls[i].pos = vec4(pos, balls[i].pos.w);\n"
    "  balls[i].vel = vec4(vel, 0.0);\n"
    "}\n";

static void sync_cpu_balls_to_gpu(GuiState3D *state)
{
    if (!state->use_gpu_compute)
        return;

    DrumInstance *drum = state->main_drum;
#pragma omp parallel for default(none) shared(state, drum)
    for (int i = 0; i < drum->ball_count; i++)
    {
        state->gpu_ball_cache[i].px = drum->balls[i].x;
        state->gpu_ball_cache[i].py = drum->balls[i].y;
        state->gpu_ball_cache[i].pz = drum->balls[i].z;
        state->gpu_ball_cache[i].settled = (float)drum->balls[i].settled;
        state->gpu_ball_cache[i].vx = drum->balls[i].vx;
        state->gpu_ball_cache[i].vy = drum->balls[i].vy;
        state->gpu_ball_cache[i].vz = drum->balls[i].vz;
        state->gpu_ball_cache[i].pad = 0.0f;
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, state->ball_ssbo);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GpuBall) * drum->ball_count,
                    state->gpu_ball_cache);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

static void sync_gpu_balls_to_cpu(GuiState3D *state)
{
    if (!state->use_gpu_compute)
        return;

    DrumInstance *drum = state->main_drum;

    /* Ensure all GPU operations are complete before reading data */
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    glFinish();

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, state->ball_ssbo);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GpuBall) * drum->ball_count,
                       state->gpu_ball_cache);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

#pragma omp parallel for default(none) shared(state, drum)
    for (int i = 0; i < drum->ball_count; i++)
    {
        drum->balls[i].x = state->gpu_ball_cache[i].px;
        drum->balls[i].y = state->gpu_ball_cache[i].py;
        drum->balls[i].z = state->gpu_ball_cache[i].pz;
        drum->balls[i].vx = state->gpu_ball_cache[i].vx;
        drum->balls[i].vy = state->gpu_ball_cache[i].vy;
        drum->balls[i].vz = state->gpu_ball_cache[i].vz;
        drum->balls[i].settled = (state->gpu_ball_cache[i].settled > 0.5f) ? 1 : 0;
    }
}

static int init_gpu_compute(GuiState3D *state)
{
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    if (shader == 0)
    {
        log_warn("GPU compute unavailable: cannot create compute shader");
        return 0;
    }

    glShaderSource(shader, 1, &BALL_COMPUTE_SHADER_SRC, NULL);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE)
    {
        char logbuf[1024];
        GLsizei len = 0;
        glGetShaderInfoLog(shader, sizeof(logbuf), &len, logbuf);
        log_warn("Compute shader compile failed: %s", logbuf);
        glDeleteShader(shader);
        return 0;
    }

    state->compute_program = glCreateProgram();
    glAttachShader(state->compute_program, shader);
    glLinkProgram(state->compute_program);
    glDeleteShader(shader);

    GLint linked = GL_FALSE;
    glGetProgramiv(state->compute_program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE)
    {
        char logbuf[1024];
        GLsizei len = 0;
        glGetProgramInfoLog(state->compute_program, sizeof(logbuf), &len, logbuf);
        log_warn("Compute program link failed: %s", logbuf);
        glDeleteProgram(state->compute_program);
        state->compute_program = 0;
        return 0;
    }

    glGenBuffers(1, &state->ball_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, state->ball_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GpuBall) * state->main_drum->ball_count, NULL,
                 GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    state->use_gpu_compute = 1;
    sync_cpu_balls_to_gpu(state);
    log_info("GPU compute physics enabled (OpenGL 4.3 compute shader)");
    return 1;
}

static void destroy_gpu_compute(GuiState3D *state)
{
    if (state->ball_ssbo != 0)
    {
        glDeleteBuffers(1, &state->ball_ssbo);
        state->ball_ssbo = 0;
    }

    if (state->compute_program != 0)
    {
        glDeleteProgram(state->compute_program);
        state->compute_program = 0;
    }

    state->use_gpu_compute = 0;
}

static void update_animation_gpu(GuiState3D *state, float delta_time)
{
    /* Early return if GPU compute is not available */
    if (state->compute_program == 0)
        return;

    DrumInstance *drum = state->main_drum;
    GLuint groups =
        (GLuint)((drum->ball_count + GPU_COMPUTE_LOCAL_SIZE - 1) / GPU_COMPUTE_LOCAL_SIZE);
    float omega_rad_per_sec = DRUM_ROTATION_SPEED_DEG * 3.14159265f / 180.0f;

    glUseProgram(state->compute_program);

    glUniform1i(glGetUniformLocation(state->compute_program, "uBallCount"), drum->ball_count);
    glUniform1f(glGetUniformLocation(state->compute_program, "uDeltaTime"), delta_time);
    glUniform1f(glGetUniformLocation(state->compute_program, "uDrumRadius"), drum->drum_radius);
    glUniform1f(glGetUniformLocation(state->compute_program, "uBallRadius"), BALL_RADIUS);
    glUniform1f(glGetUniformLocation(state->compute_program, "uGravity"), BALL_GRAVITY);
    glUniform1f(glGetUniformLocation(state->compute_program, "uAirDamping"), BALL_AIR_DAMPING);
    glUniform1f(glGetUniformLocation(state->compute_program, "uOmega"), omega_rad_per_sec);
    glUniform1f(glGetUniformLocation(state->compute_program, "uDrumRotationZDeg"),
                drum->drum_rotation_z);
    glUniform1f(glGetUniformLocation(state->compute_program, "uCollisionRestitution"),
                BALL_COLLISION_RESTITUTION);
    glUniform1f(glGetUniformLocation(state->compute_program, "uCollisionTangentialTransfer"),
                BALL_COLLISION_TANGENTIAL_TRANSFER);
    glUniform1f(glGetUniformLocation(state->compute_program, "uCollisionImpulseBoost"),
                BALL_COLLISION_IMPULSE_BOOST);
    glUniform1f(glGetUniformLocation(state->compute_program, "uCollisionBurstSpeed"),
                BALL_COLLISION_BURST_SPEED);
    glUniform1f(glGetUniformLocation(state->compute_program, "uContactFriction"),
                BALL_CONTACT_FRICTION);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, state->ball_ssbo);
    glDispatchCompute(groups, 1, 1);

    /* Sync data back from GPU (includes proper memory barriers) */
    sync_gpu_balls_to_cpu(state);
    glUseProgram(0);
}

static void resolve_ball_collision(DrumBall *ball_i, DrumBall *ball_j, float collision_dist)
{
    float dx = ball_j->x - ball_i->x;
    float dy = ball_j->y - ball_i->y;
    float dz = ball_j->z - ball_i->z;

    float dist_sq = dx * dx + dy * dy + dz * dz;
    if (dist_sq >= collision_dist * collision_dist || dist_sq <= 0.001f)
        return;

    float dist = sqrtf(dist_sq);
    float nx = dx / dist;
    float ny = dy / dist;
    float nz = dz / dist;

    /* Relative velocity of j relative to i */
    float dvx = ball_j->vx - ball_i->vx;
    float dvy = ball_j->vy - ball_i->vy;
    float dvz = ball_j->vz - ball_i->vz;
    float rel_vel_along_normal = dvx * nx + dvy * ny + dvz * nz;

    /* Separate overlapping balls to avoid sticking */
    float overlap = collision_dist - dist;
    if (overlap > 0.0f)
    {
        float separation = overlap / 2.0f + 0.1f;
        ball_i->x -= separation * nx;
        ball_i->y -= separation * ny;
        ball_i->z -= separation * nz;

        ball_j->x += separation * nx;
        ball_j->y += separation * ny;
        ball_j->z += separation * nz;
    }

    /* Velocity impulse only if approaching */
    if (rel_vel_along_normal < 0.0f)
    {
        /* Equal-mass billiard collision along contact normal */
        float impulse = -((1.0f + BALL_COLLISION_RESTITUTION) * rel_vel_along_normal) / 2.0f;
        float burst = BALL_COLLISION_BURST_SPEED * (overlap / collision_dist);
        impulse = impulse * BALL_COLLISION_IMPULSE_BOOST + burst;

        ball_i->vx -= impulse * nx;
        ball_i->vy -= impulse * ny;
        ball_i->vz -= impulse * nz;

        ball_j->vx += impulse * nx;
        ball_j->vy += impulse * ny;
        ball_j->vz += impulse * nz;

        /* Small tangential transfer for less perfectly mirrored paths */
        float tvx = dvx - rel_vel_along_normal * nx;
        float tvy = dvy - rel_vel_along_normal * ny;
        float tvz = dvz - rel_vel_along_normal * nz;
        {
            float t_impulse = BALL_COLLISION_TANGENTIAL_TRANSFER * 0.5f;

            ball_i->vx += tvx * t_impulse;
            ball_i->vy += tvy * t_impulse;
            ball_i->vz += tvz * t_impulse;

            ball_j->vx -= tvx * t_impulse;
            ball_j->vy -= tvy * t_impulse;
            ball_j->vz -= tvz * t_impulse;
        }

        /* Apply rotational impulses from collision (spin transfer) */
        {
            float tangent_len = sqrtf(tvx * tvx + tvy * tvy + tvz * tvz);
            if (tangent_len > 0.1f)
            {
                /* Create spin from tangential contact */
                float rot_impulse = 0.15f * impulse;

                /* Cross product: tangent × normal gives rotation axis */
                ball_i->rot_x += (tvy * nz - tvz * ny) * rot_impulse;
                ball_i->rot_y += (tvz * nx - tvx * nz) * rot_impulse;
                ball_i->rot_z += (tvx * ny - tvy * nx) * rot_impulse;

                ball_j->rot_x -= (tvy * nz - tvz * ny) * rot_impulse;
                ball_j->rot_y -= (tvz * nx - tvx * nz) * rot_impulse;
                ball_j->rot_z -= (tvx * ny - tvy * nx) * rot_impulse;
            }
        }
    }
}

/* Initialise balls for a drum */
static void drum_instance_init_balls(DrumInstance *drum)
{
    drum->phase = DRUM_PHASE_FALLING;
    drum->sim_time = 0.0f;

    float spawn_r = drum->drum_radius * 0.60f;
#pragma omp parallel for default(none) shared(drum, spawn_r)
    for (int i = 0; i < drum->ball_count; i++)
    {
        float x, z;
        do
        {
            x = frand_range(-spawn_r, spawn_r);
            z = frand_range(-spawn_r, spawn_r);
        } while ((x * x + z * z) > (spawn_r * spawn_r));

        drum->balls[i].x = x;
        drum->balls[i].z = z;
        drum->balls[i].y = frand_range(drum->drum_radius * 0.25f, drum->drum_radius * 0.75f);
        drum->balls[i].vx = 0.0f;
        drum->balls[i].vy = 0.0f;
        drum->balls[i].vz = 0.0f;
        drum->balls[i].rot_x = frand_range(-5.0f, 5.0f); /* Initial random spin */
        drum->balls[i].rot_y = frand_range(-5.0f, 5.0f);
        drum->balls[i].rot_z = frand_range(-5.0f, 5.0f);
        drum->balls[i].settled = 0;
        drum->balls[i].picked = 0;
        drum->balls[i].ball_number = drum->ball_min + i;

        /* Pre-fill trail with the ball's starting position to avoid 0,0,0 artefacts */
        if (drum->trail_x)
        {
            for (int j = 0; j < BALL_TRAIL_LENGTH; j++)
            {
                int base = i * BALL_TRAIL_LENGTH + j;
                drum->trail_x[base] = drum->balls[i].x;
                drum->trail_y[base] = drum->balls[i].y;
                drum->trail_z[base] = drum->balls[i].z;
            }
            drum->trail_head[i] = 0;
        }
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
static void draw_sphere_frame(float radius, int slices, int stacks, float gr, float gg, float gb)
{
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glLineWidth(3.0f);
    glColor3f(gr, gg, gb);
    glDisable(GL_LIGHTING);

    draw_sphere(radius * 1.005f, slices, stacks);

    glEnable(GL_LIGHTING);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glLineWidth(1.0f);
}

/* ============================================================
   DEBUG OVERLAY
   ============================================================ */

/** Return a short name for a drum phase (for the debug overlay). */
static const char *drum_phase_name(DrumPhase phase)
{
    switch (phase)
    {
    case DRUM_PHASE_FALLING:
        return "FALLING";
    case DRUM_PHASE_ROTATING:
        return "ROTATING";
    case DRUM_PHASE_STOPPING:
        return "STOPPING";
    case DRUM_PHASE_PICK_PAUSE:
        return "PICK";
    case DRUM_PHASE_DRAW_COMPLETE:
        return "DONE";
    default:
        return "?";
    }
}

/**
 * @brief Set camera pitch and yaw angles.
 */
static void set_camera_view(GuiState3D *state, float pitch, float yaw)
{
    state->camera_pitch = pitch;
    state->camera_yaw = yaw;
    state->camera_z = CAMERA_Z;
}

/**
 * @brief Render a text string at pixel position (x, y) in an already-active 2D
 *        orthographic projection (top-left origin, y-down).  Creates and
 *        destroys a temporary GL texture each call — intended for debug use.
 */
static void render_text_2d_at(TTF_Font *font, const char *text, float x, float y, float r, float g,
                              float b)
{
    if (!font || !text || text[0] == '\0')
        return;

    SDL_Color color = {(Uint8)(r * 255.0f), (Uint8)(g * 255.0f), (Uint8)(b * 255.0f), 255};
    SDL_Surface *surf = TTF_RenderText_Blended(font, text, color);
    if (!surf)
        return;

    SDL_Surface *conv = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(surf);
    if (!conv)
        return;

    int tw = conv->w;
    int th = conv->h;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, conv->pixels);
    SDL_FreeSurface(conv);

    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor3f(1.0f, 1.0f, 1.0f);

    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(x, y);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(x + (float)tw, y);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(x + (float)tw, y + (float)th);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(x, y + (float)th);
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);

    glDeleteTextures(1, &tex);
}

/**
 * @brief Render the HUD debug overlay (top-left corner).
 *        Must be called while an orthographic 2D projection is active.
 */
static void render_debug_overlay(const GuiState3D *state)
{
    if (!state->debug_overlay || !state->overlay_font)
        return;

    /* Update CPU stats for display */
    update_cpu_usage((GuiState3D *)state);

    TTF_Font *font = state->overlay_font;
    float x = 12.0f;
    float y = 12.0f;
    float line_h = 22.0f;
    char buf[160];

    /* Background tint so text is readable on any scene */
    Theme overlay_theme = theme_get(state->dark_mode);
    float obr = ((overlay_theme.overlay_bg >> 24) & 0xff) / 255.0f;
    float obg = ((overlay_theme.overlay_bg >> 16) & 0xff) / 255.0f;
    float obb = ((overlay_theme.overlay_bg >> 8) & 0xff) / 255.0f;
    float oba = (overlay_theme.overlay_bg & 0xff) / 255.0f;
    float txr = ((overlay_theme.text_primary >> 24) & 0xff) / 255.0f;
    float txg = ((overlay_theme.text_primary >> 16) & 0xff) / 255.0f;
    float txb = ((overlay_theme.text_primary >> 8) & 0xff) / 255.0f;
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(obr, obg, obb, oba);
    float pad = 4.0f;
    int lines = 8 + (state->num_cores > 0 ? 1 : 0) + (state->extra_drum ? 1 : 0) +
                (state->paused ? 1 : 0) + 1;
    float box_h = (float)lines * line_h + pad * 2.0f;
    float box_w = 520.0f;
    glBegin(GL_QUADS);
    glVertex2f(x - pad, y - pad);
    glVertex2f(x + box_w, y - pad);
    glVertex2f(x + box_w, y + box_h);
    glVertex2f(x - pad, y + box_h);
    glEnd();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);

    /* FPS */
    snprintf(buf, sizeof(buf), "FPS: %.0f", state->fps_current);
    render_text_2d_at(font, buf, x, y, txr, txg, txb);
    y += line_h;

    /* Physics mode */
    snprintf(buf, sizeof(buf), "Physics: %s", state->use_gpu_compute ? "GPU compute" : "CPU");
    render_text_2d_at(font, buf, x, y, txr, txg, txb);
    y += line_h;

    /* Animation speed */
    snprintf(buf, sizeof(buf), "Speed: %.1fx  (press +/- or N)", state->animation_speed_multiplier);
    render_text_2d_at(font, buf, x, y, txr, txg, txb);
    y += line_h;

    /* CPU usage (OpenMP threads + per-core percentages) */
    {
        int num_procs = omp_get_num_procs();
        int max_threads = omp_get_max_threads();
        snprintf(buf, sizeof(buf), "OpenMP: %d threads  Cores: %d", max_threads, num_procs);
        render_text_2d_at(font, buf, x, y, 0.85f, 0.85f, 0.20f);
        y += line_h;

        /* Display per-core CPU usage */
        if (state->cpu_usage && state->num_cores > 0)
        {
            int cols = (state->num_cores <= 4) ? state->num_cores : 4;
            int rows = (state->num_cores + cols - 1) / cols;
            for (int r = 0; r < rows; r++)
            {
                char *bp = buf;
                int remaining = (int)sizeof(buf);
                for (int c = 0; c < cols && r * cols + c < state->num_cores; c++)
                {
                    int core = r * cols + c;
                    int len = snprintf(bp, (size_t)remaining, "CPU%d:%.0f%% ", core,
                                       state->cpu_usage[core]);
                    bp += len;
                    remaining -= len;
                }
                render_text_2d_at(font, buf, x, y, 0.70f, 0.90f, 0.70f);
                y += line_h;
            }
        }
    }

    /* GPU information */
    {
        const char *gpu_renderer = (const char *)glGetString(GL_RENDERER);
        if (gpu_renderer)
        {
            snprintf(buf, sizeof(buf), "GPU: %s", gpu_renderer);
            render_text_2d_at(font, buf, x, y, 0.20f, 0.85f, 1.00f);
            y += line_h;
        }
    }

    /* Main drum status */
    if (state->main_drum)
    {
        snprintf(buf, sizeof(buf), "Main drum : %-8s  pick %d/%d  %d balls",
                 drum_phase_name(state->main_drum->phase), state->main_drum->picks_done,
                 state->main_drum->picks_total, state->main_drum->ball_count);
        render_text_2d_at(font, buf, x, y, txr, txg, txb);
        y += line_h;
    }

    /* Extra drum status (if present) */
    if (state->extra_drum)
    {
        snprintf(buf, sizeof(buf), "Extra drum: %-8s  pick %d/%d  %d balls",
                 drum_phase_name(state->extra_drum->phase), state->extra_drum->picks_done,
                 state->extra_drum->picks_total, state->extra_drum->ball_count);
        render_text_2d_at(font, buf, x, y, txr, txg, txb);
        y += line_h;
    }

    /* Pause indicator */
    if (state->paused)
    {
        render_text_2d_at(font, "[ PAUSED — press Space to resume ]", x, y, 1.00f, 0.80f, 0.10f);
        y += line_h;
    }

    /* Controls hint */
    render_text_2d_at(font, "Cam:R T F S I  Speed:+/- N  Esc", x, y, 0.65f, 0.65f, 0.65f);
}

/* ============================================================
   SETUP & INITIALIZATION
   ============================================================ */

static void setup_opengl(int dark_mode)
{
    glEnable(GL_DEPTH_TEST);
    Theme t = theme_get(dark_mode);
    float r = ((t.background >> 24) & 0xff) / 255.0f;
    float g = ((t.background >> 16) & 0xff) / 255.0f;
    float b = ((t.background >> 8) & 0xff) / 255.0f;
    glClearColor(r, g, b, 1.0f);
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

static GuiState3D *gui_state_create(const char *unused_game_name, const LotteryInfo *info,
                                    int debug_overlay, int dark_mode)
{
    (void)unused_game_name;

    GuiState3D *state = (GuiState3D *)malloc(sizeof(GuiState3D));
    if (!state)
        return NULL;

    memset(state, 0, sizeof(GuiState3D));
    state->info = *info;
    state->animation_complete = 0;
    state->debug_overlay = debug_overlay;
    state->dark_mode = dark_mode;

    state->camera_pitch = CAMERA_TRIMETRIC_X;
    state->camera_yaw = CAMERA_TRIMETRIC_Y;
    state->camera_roll = CAMERA_TRIMETRIC_Z;
    state->camera_z = CAMERA_Z;

    /* Initialize animation speed control */
    state->animation_speed_multiplier = ANIMATION_SPEED_DEFAULT;

    srand((unsigned int)time(NULL));

    /* ---- Main drum ---- */
    {
        DrumInstance *drum = (DrumInstance *)calloc(1, sizeof(DrumInstance));
        if (!drum)
        {
            free(state);
            return NULL;
        }

        int ball_count = info->main_max;
        if (ball_count <= 0)
            ball_count = 50;
        if (ball_count > MAX_GAME_BALLS)
            ball_count = MAX_GAME_BALLS;

        drum->balls = (DrumBall *)calloc((size_t)ball_count, sizeof(DrumBall));
        if (!drum->balls)
        {
            free(drum);
            free(state);
            return NULL;
        }

        drum->ball_count = ball_count;
        drum->ball_min = info->main_min; /* e.g. 1 for Lotto/Eurojackpot */
        drum->drum_radius = DRUM_RADIUS;
        drum->world_x = DRUM_X;
        drum->world_y = DRUM_Y;
        drum->picks_total = info->main_count;
        drum->spin_before_pick = 3.0f + frand_range(0.0f, 2.0f);
        drum->stop_omega = DRUM_ROTATION_SPEED_DEG;
        drum->current_pick_idx = -1;
        drum->waiting = 0;
        drum->is_extra = 0;

        /* Trail ring buffers for debug overlay (non-fatal if allocation fails) */
        drum->trail_x = (float *)calloc((size_t)ball_count * BALL_TRAIL_LENGTH, sizeof(float));
        drum->trail_y = (float *)calloc((size_t)ball_count * BALL_TRAIL_LENGTH, sizeof(float));
        drum->trail_z = (float *)calloc((size_t)ball_count * BALL_TRAIL_LENGTH, sizeof(float));
        drum->trail_head = (int *)calloc((size_t)ball_count, sizeof(int));

        drum_instance_init_balls(drum);
        state->main_drum = drum;

        /* GPU cache for main drum */
        state->gpu_ball_cache = (GpuBall *)calloc((size_t)ball_count, sizeof(GpuBall));
    }

    /* ---- Extra drum (only when game has extra numbers) ---- */
    if (info->extra_count > 0 && info->extra_max >= info->extra_min)
    {
        DrumInstance *drum = (DrumInstance *)calloc(1, sizeof(DrumInstance));
        if (drum)
        {
            int ball_count = info->extra_max - info->extra_min + 1;
            if (ball_count > MAX_GAME_BALLS)
                ball_count = MAX_GAME_BALLS;

            drum->balls = (DrumBall *)calloc((size_t)ball_count, sizeof(DrumBall));
            if (drum->balls)
            {
                drum->ball_count = ball_count;
                drum->ball_min = info->extra_min;
                drum->drum_radius = EXTRA_DRUM_RADIUS;
                drum->world_x = EXTRA_DRUM_X;
                drum->world_y = EXTRA_DRUM_Y;
                drum->picks_total = info->extra_count;
                drum->spin_before_pick = 3.0f + frand_range(0.0f, 2.0f);
                drum->stop_omega = DRUM_ROTATION_SPEED_DEG;
                drum->current_pick_idx = -1;
                drum->waiting = 1; /* wait for main drum to finish */
                drum->is_extra = 1;

                /* Trail ring buffers for debug overlay (non-fatal if allocation fails) */
                drum->trail_x =
                    (float *)calloc((size_t)ball_count * BALL_TRAIL_LENGTH, sizeof(float));
                drum->trail_y =
                    (float *)calloc((size_t)ball_count * BALL_TRAIL_LENGTH, sizeof(float));
                drum->trail_z =
                    (float *)calloc((size_t)ball_count * BALL_TRAIL_LENGTH, sizeof(float));
                drum->trail_head = (int *)calloc((size_t)ball_count, sizeof(int));

                drum_instance_init_balls(drum);
                state->extra_drum = drum;
                log_info("Extra drum created: %d balls (numbers %d-%d), waiting for main draw",
                         ball_count, info->extra_min, info->extra_max);
            }
            else
            {
                free(drum);
            }
        }
    }

    /* CPU usage tracking initialization */
    state->num_cores = omp_get_num_procs();
    if (state->num_cores > 0)
    {
        state->cpu_usage = (float *)calloc((size_t)state->num_cores, sizeof(float));
        state->prev_cpu =
            (unsigned long *)calloc((size_t)state->num_cores * 4, sizeof(unsigned long));
        state->cpu_last_update = SDL_GetTicks();
        log_info("CPU usage tracking initialized for %d cores", state->num_cores);
    }

    /* GPU compute opt-in */
    const char *enable_gpu = getenv("LOTTO_GPU_COMPUTE");
    int try_gpu = (enable_gpu != NULL &&
                   (enable_gpu[0] == '1' || enable_gpu[0] == 'y' || enable_gpu[0] == 'Y'));
    if (try_gpu)
        log_info("GPU compute mode requested (LOTTO_GPU_COMPUTE set)");
    else
    {
        log_info("Using CPU physics (GPU compute disabled by default)");
        log_info("To enable GPU compute, set: LOTTO_GPU_COMPUTE=1");
    }
    state->_gpu_attempt_enabled = try_gpu;

    return state;
}

static void gui_state_destroy(GuiState3D *state)
{
    if (!state)
        return;

    if (state->main_drum)
    {
        if (state->main_drum->number_textures)
        {
            glDeleteTextures(state->main_drum->texture_count, state->main_drum->number_textures);
            free(state->main_drum->number_textures);
        }
        free(state->main_drum->trail_x);
        free(state->main_drum->trail_y);
        free(state->main_drum->trail_z);
        free(state->main_drum->trail_head);
        free(state->main_drum->balls);
        free(state->main_drum);
    }

    if (state->extra_drum)
    {
        if (state->extra_drum->number_textures)
        {
            glDeleteTextures(state->extra_drum->texture_count, state->extra_drum->number_textures);
            free(state->extra_drum->number_textures);
        }
        free(state->extra_drum->trail_x);
        free(state->extra_drum->trail_y);
        free(state->extra_drum->trail_z);
        free(state->extra_drum->trail_head);
        free(state->extra_drum->balls);
        free(state->extra_drum);
    }

    if (state->font)
        TTF_CloseFont(state->font);

    if (state->overlay_font)
        TTF_CloseFont(state->overlay_font);

    free(state->gpu_ball_cache);
    free(state->cpu_usage);
    free(state->prev_cpu);
    free(state);
}

/* ============================================================
   RENDERING
   ============================================================ */

/* Create an OpenGL texture from a ball number using SDL_ttf */
static GLuint make_number_texture(TTF_Font *font, int number)
{
    if (!font)
        return 0;

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", number);

    SDL_Color black = {0, 0, 0, 255};
    SDL_Surface *text_surf = TTF_RenderText_Blended(font, buf, black);
    if (!text_surf)
        return 0;

    /* Square canvas, power-of-two for compatibility */
    int tex_size = 64;
    SDL_Surface *canvas = SDL_CreateRGBSurface(0, tex_size, tex_size, 32, 0x000000FFu, 0x0000FF00u,
                                               0x00FF0000u, 0xFF000000u);
    if (!canvas)
    {
        SDL_FreeSurface(text_surf);
        return 0;
    }
    SDL_FillRect(canvas, NULL, 0);

    SDL_Rect dst = {(tex_size - text_surf->w) / 2, (tex_size - text_surf->h) / 2, text_surf->w,
                    text_surf->h};
    SDL_BlitSurface(text_surf, NULL, canvas, &dst);
    SDL_FreeSurface(text_surf);

    SDL_Surface *conv = SDL_ConvertSurfaceFormat(canvas, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(canvas);
    if (!conv)
        return 0;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, conv->w, conv->h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 conv->pixels);
    SDL_FreeSurface(conv);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

static void init_ball_textures(GuiState3D *state)
{
    if (TTF_Init() < 0)
    {
        log_warn("TTF_Init failed: %s", TTF_GetError());
        return;
    }

    char font_path[512];
    snprintf(font_path, sizeof(font_path), "%s/fonts/Roboto-Bold.ttf", PROJECT_ROOT_DIR);
    state->font = TTF_OpenFont(font_path, 36);
    if (!state->font)
    {
        log_warn("Failed to load font '%s': %s — numbers will be hidden", font_path,
                 TTF_GetError());
        return;
    }

    /* Smaller overlay font for the HUD debug display */
    state->overlay_font = TTF_OpenFont(font_path, 18);
    if (!state->overlay_font)
        log_warn("Failed to load overlay font '%s': %s", font_path, TTF_GetError());

    /* Create textures for main drum */
    {
        DrumInstance *drum = state->main_drum;
        /* texture_count covers indices 0 .. (ball_min + ball_count - 1) */
        drum->texture_count = drum->ball_min + drum->ball_count;
        drum->number_textures = (GLuint *)calloc((size_t)drum->texture_count, sizeof(GLuint));
        if (drum->number_textures)
        {
            for (int i = 0; i < drum->ball_count; i++)
            {
                int num = drum->ball_min + i;
                drum->number_textures[num] = make_number_texture(state->font, num);
            }
            log_info("Main drum textures created (%d balls, min=%d)", drum->ball_count,
                     drum->ball_min);
        }
    }

    /* Create textures for extra drum (if present) */
    if (state->extra_drum)
    {
        DrumInstance *drum = state->extra_drum;
        drum->texture_count = drum->ball_min + drum->ball_count;
        drum->number_textures = (GLuint *)calloc((size_t)drum->texture_count, sizeof(GLuint));
        if (drum->number_textures)
        {
            for (int i = 0; i < drum->ball_count; i++)
            {
                int num = drum->ball_min + i;
                drum->number_textures[num] = make_number_texture(state->font, num);
            }
            log_info("Extra drum textures created (%d balls, min=%d)", drum->ball_count,
                     drum->ball_min);
        }
    }
}

static void render_drum_instance(const DrumInstance *drum, float sim_time, int debug_overlay,
                                 Theme theme)
{
    (void)sim_time;

    float x = drum->world_x;
    float y = drum->world_y;
    float drum_radius = drum->drum_radius;
    float ball_radius = BALL_RADIUS;

    glPushMatrix();
    glTranslatef(x, y, 0.0f);

    /* Rotate the drum with 3 axes for tumbling effect */
    glRotatef(drum->drum_rotation_x, 1.0f, 0.0f, 0.0f);
    glRotatef(drum->drum_rotation_y, 0.0f, 1.0f, 0.0f);
    glRotatef(drum->drum_rotation_z, 0.0f, 0.0f, 1.0f);

    /* Balls inside drum */
    for (int i = 0; i < drum->ball_count; i++)
    {
        const DrumBall *ball = &drum->balls[i];
        if (ball->picked)
            continue; /* picked balls are removed from drum rendering */

        glPushMatrix();
        glTranslatef(ball->x, ball->y, ball->z);

        /* Apply rotation to show tumbling animation */
        if (ball->rot_x != 0.0f)
            glRotatef(ball->rot_x * 0.5f, 1.0f, 0.0f, 0.0f);
        if (ball->rot_y != 0.0f)
            glRotatef(ball->rot_y * 0.5f, 0.0f, 1.0f, 0.0f);
        if (ball->rot_z != 0.0f)
            glRotatef(ball->rot_z * 0.5f, 0.0f, 0.0f, 1.0f);

        if (drum->is_extra)
            glColor3f(SUPER_BALL_COLOR_R, SUPER_BALL_COLOR_G, SUPER_BALL_COLOR_B);
        else
            glColor3f(BALL_COLOR_R, BALL_COLOR_G, BALL_COLOR_B);
        draw_sphere(ball_radius, 18, 12);
        glPopMatrix();
    }

    /* Ball trails — rendered only when debug overlay is active */
    if (debug_overlay && drum->trail_x)
    {
        float tr = drum->is_extra ? SUPER_BALL_COLOR_R : BALL_COLOR_R;
        float tg = drum->is_extra ? SUPER_BALL_COLOR_G : BALL_COLOR_G;
        float tb = drum->is_extra ? SUPER_BALL_COLOR_B : BALL_COLOR_B;

        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glLineWidth(2.0f);

        for (int i = 0; i < drum->ball_count; i++)
        {
            if (drum->balls[i].picked)
                continue;

            int head = drum->trail_head[i];
            glBegin(GL_LINE_STRIP);
            for (int j = 0; j < BALL_TRAIL_LENGTH; j++)
            {
                int idx = (head + j) % BALL_TRAIL_LENGTH;
                float alpha = (float)(j + 1) / (float)BALL_TRAIL_LENGTH * 0.55f;
                glColor4f(tr, tg, tb, alpha);
                int base = i * BALL_TRAIL_LENGTH + idx;
                glVertex3f(drum->trail_x[base], drum->trail_y[base], drum->trail_z[base]);
            }
            glEnd();
        }

        glLineWidth(1.0f);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_LIGHTING);
    }

    /* Draw number billboards */
    if (drum->number_textures)
    {
        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor3f(1.0f, 1.0f, 1.0f);

        for (int i = 0; i < drum->ball_count; i++)
        {
            const DrumBall *ball = &drum->balls[i];
            if (ball->picked)
                continue;

            int idx = ball->ball_number;
            if (idx < 0 || idx >= drum->texture_count)
                continue;
            if (!drum->number_textures[idx])
                continue;

            glPushMatrix();
            glTranslatef(ball->x, ball->y, ball->z);

            float mv[16];
            glGetFloatv(GL_MODELVIEW_MATRIX, mv);
            mv[0] = 1.0f;
            mv[1] = 0.0f;
            mv[2] = 0.0f;
            mv[4] = 0.0f;
            mv[5] = 1.0f;
            mv[6] = 0.0f;
            mv[8] = 0.0f;
            mv[9] = 0.0f;
            mv[10] = 1.0f;
            glLoadMatrixf(mv);

            float s = ball_radius * 0.80f;
            glBindTexture(GL_TEXTURE_2D, drum->number_textures[idx]);
            glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 1.0f);
            glVertex3f(-s, -s, 0.5f);
            glTexCoord2f(1.0f, 1.0f);
            glVertex3f(s, -s, 0.5f);
            glTexCoord2f(1.0f, 0.0f);
            glVertex3f(s, s, 0.5f);
            glTexCoord2f(0.0f, 0.0f);
            glVertex3f(-s, s, 0.5f);
            glEnd();

            glPopMatrix();
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_LIGHTING);
    }

    /* Physics debug overlay: velocity vectors and collision hulls */
    if (debug_overlay)
    {
        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        /* Velocity vectors: yellow line from ball centre along velocity direction */
        glColor4f(1.0f, 0.95f, 0.15f, 0.80f);
        glLineWidth(1.5f);
        float vel_scale = 0.12f; /* scale factor: units / (units/sec) */
        glBegin(GL_LINES);
        for (int i = 0; i < drum->ball_count; i++)
        {
            const DrumBall *b = &drum->balls[i];
            if (b->picked)
                continue;
            float ex = b->x + b->vx * vel_scale;
            float ey = b->y + b->vy * vel_scale;
            float ez = b->z + b->vz * vel_scale;
            glVertex3f(b->x, b->y, b->z);
            glVertex3f(ex, ey, ez);
        }
        glEnd();

        /* Collision hulls: red circle at 2*BALL_RADIUS around each ball (XY plane) */
        glColor4f(0.85f, 0.20f, 0.20f, 0.40f);
        glLineWidth(1.0f);
        int segs = 16;
        for (int i = 0; i < drum->ball_count; i++)
        {
            const DrumBall *b = &drum->balls[i];
            if (b->picked)
                continue;
            float r2 = 2.0f * ball_radius;
            glBegin(GL_LINE_LOOP);
            for (int s = 0; s < segs; s++)
            {
                float a = (float)s / (float)segs * 2.0f * (float)M_PI;
                glVertex3f(b->x + r2 * cosf(a), b->y + r2 * sinf(a), b->z);
            }
            glEnd();
        }

        glLineWidth(1.0f);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_LIGHTING);
    }

    /* Transparent drum shell */
    float dr = ((theme.drum >> 24) & 0xff) / 255.0f;
    float dg = ((theme.drum >> 16) & 0xff) / 255.0f;
    float db = ((theme.drum >> 8) & 0xff) / 255.0f;
    float gr = ((theme.drum_grid >> 24) & 0xff) / 255.0f;
    float gg = ((theme.drum_grid >> 16) & 0xff) / 255.0f;
    float gb = ((theme.drum_grid >> 8) & 0xff) / 255.0f;
    glDepthMask(GL_FALSE);
    glColor4f(dr, dg, db, 0.18f);
    draw_sphere(drum_radius, 64, 44);
    glDepthMask(GL_TRUE);

    /* Wireframe outline */
    draw_sphere_frame(drum_radius, 28, 20, gr, gg, gb);

    glPopMatrix();
}

static void render_overlay_ball_2d(const DrumInstance *drum, const PickedBallDisplay *pb, float cx,
                                   float cy, float r, float g, float b)
{
    /* Draw a lit 3D sphere in fixed screen coordinates (orthographic overlay). */
    glEnable(GL_LIGHTING);
    glPushMatrix();
    glTranslatef(cx, cy, 0.0f);
    glColor3f(r, g, b);
    draw_sphere(BALL_RADIUS, 18, 12);
    glPopMatrix();
    glDisable(GL_LIGHTING);

    if (drum->number_textures)
    {
        int idx = pb->ball_number;
        if (idx >= 0 && idx < drum->texture_count && drum->number_textures[idx])
        {
            glEnable(GL_TEXTURE_2D);
            glColor3f(1.0f, 1.0f, 1.0f);
            float s2 = BALL_RADIUS * 0.80f;
            glBindTexture(GL_TEXTURE_2D, drum->number_textures[idx]);
            glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 1.0f);
            glVertex3f(cx - s2, cy + s2, BALL_RADIUS * 0.70f);
            glTexCoord2f(1.0f, 1.0f);
            glVertex3f(cx + s2, cy + s2, BALL_RADIUS * 0.70f);
            glTexCoord2f(1.0f, 0.0f);
            glVertex3f(cx + s2, cy - s2, BALL_RADIUS * 0.70f);
            glTexCoord2f(0.0f, 0.0f);
            glVertex3f(cx - s2, cy - s2, BALL_RADIUS * 0.70f);
            glEnd();

            glBindTexture(GL_TEXTURE_2D, 0);
            glDisable(GL_TEXTURE_2D);
        }
    }
}

/* Render final result as one fixed on-screen line: main numbers + superzahl. */
static void render_combined_result_overlay_2d(const GuiState3D *state, float row_y_px)
{
    const DrumInstance *main_drum = state->main_drum;
    const DrumInstance *extra_drum = state->extra_drum;
    int main_count = main_drum ? main_drum->result_ball_count : 0;
    int extra_count = extra_drum ? extra_drum->result_ball_count : 0;

    if (main_count == 0 && extra_count == 0)
        return;

    float slot_spacing = BALL_RADIUS * 2.2f;
    int show_plus = (main_count > 0 && extra_count > 0) ? 1 : 0;
    int total_slots = main_count + extra_count + show_plus;
    float row_start_x =
        ((float)WINDOW_WIDTH * 0.5f) - (((float)total_slots - 1.0f) * 0.5f * slot_spacing);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    int slot = 0;
    for (int i = 0; i < main_count; i++, slot++)
    {
        float cx = row_start_x + (float)slot * slot_spacing;
        const PickedBallDisplay *pb = &main_drum->result_balls[i];
        render_overlay_ball_2d(main_drum, pb, cx, row_y_px, BALL_COLOR_R, BALL_COLOR_G,
                               BALL_COLOR_B);
    }

    if (show_plus)
    {
        float plus_x = row_start_x + (float)slot * slot_spacing;
        float plus_size = BALL_RADIUS * 0.55f;
        glColor3f(0.1f, 0.1f, 0.1f);
        glLineWidth(3.0f);
        glBegin(GL_LINES);
        glVertex2f(plus_x - plus_size, row_y_px);
        glVertex2f(plus_x + plus_size, row_y_px);
        glVertex2f(plus_x, row_y_px - plus_size);
        glVertex2f(plus_x, row_y_px + plus_size);
        glEnd();
        glLineWidth(1.0f);
        slot++;
    }

    for (int i = 0; i < extra_count; i++, slot++)
    {
        float cx = row_start_x + (float)slot * slot_spacing;
        const PickedBallDisplay *pb = &extra_drum->result_balls[i];
        render_overlay_ball_2d(extra_drum, pb, cx, row_y_px, SUPER_BALL_COLOR_R, SUPER_BALL_COLOR_G,
                               SUPER_BALL_COLOR_B);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
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

    /* Show only one drum at a time:
       - main drum until main draw is complete
       - then extra drum (superzahl/euro numbers) */
    int show_main_drum =
        (state->main_drum->phase != DRUM_PHASE_DRAW_COMPLETE) || (state->extra_drum == NULL);
    int show_extra_drum =
        (state->extra_drum != NULL) && (state->main_drum->phase == DRUM_PHASE_DRAW_COMPLETE);

    Theme scene_theme = theme_get(state->dark_mode);

    if (show_main_drum)
        render_drum_instance(state->main_drum, state->main_drum->sim_time, state->debug_overlay,
                             scene_theme);

    if (show_extra_drum)
    {
        render_drum_instance(state->extra_drum, state->extra_drum->sim_time, state->debug_overlay,
                             scene_theme);
    }

    /* Screen-fixed picked-ball rows (unaffected by camera orbit/zoom):
       switch to 2D overlay projection after 3D scene. */
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (double)WINDOW_WIDTH, (double)WINDOW_HEIGHT, 0.0, -1000.0, 1000.0);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    render_combined_result_overlay_2d(state, (float)WINDOW_HEIGHT - 78.0f);
    render_debug_overlay(state);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);

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

/* Apply rolling friction and angular damping to a ball */
static void apply_rolling_friction(DrumBall *ball, float delta_time, float contact_with_shell)
{
    (void)contact_with_shell; /* Parameter reserved for future enhancements */

    /* Dampen angular velocity due to air resistance */
    ball->rot_x *= BALL_ANGULAR_DAMPING;
    ball->rot_y *= BALL_ANGULAR_DAMPING;
    ball->rot_z *= BALL_ANGULAR_DAMPING;

    /* If ball is in contact with the drum shell, apply rolling friction */
    if (contact_with_shell > 0.5f)
    {
        /* Rolling resistance: friction torque opposes rotation */
        float rot_speed = sqrtf(ball->rot_x * ball->rot_x + ball->rot_y * ball->rot_y +
                                ball->rot_z * ball->rot_z);
        if (rot_speed > 0.1f)
        {
            float friction_factor = BALL_ROLLING_FRICTION * delta_time;
            ball->rot_x *= (1.0f - friction_factor);
            ball->rot_y *= (1.0f - friction_factor);
            ball->rot_z *= (1.0f - friction_factor);
        }

        /* Couple linear and angular motion: spin-up when rolling */
        float linear_speed = sqrtf(ball->vx * ball->vx + ball->vy * ball->vy);
        if (linear_speed > 5.0f)
        {
            /* Tangential velocity at surface = omega * radius */
            float spin_coupling = 0.08f * delta_time;
            if (fabsf(ball->vx) > 0.1f || fabsf(ball->vy) > 0.1f)
            {
                ball->rot_z += (ball->vx * spin_coupling);
                ball->rot_x += (ball->vy * spin_coupling);
            }
        }
    }
}

/* Generic per-drum animation update — works for both main and extra drums */
static void update_drum_instance(DrumInstance *drum, float delta_time)
{
    if (drum->waiting)
        return; /* waiting for another drum to finish before starting */

    drum->sim_time += delta_time;
    float drum_radius = drum->drum_radius;

    if (drum->phase == DRUM_PHASE_FALLING)
    {
        int settled_count = 0;

#pragma omp parallel for default(none) shared(drum, delta_time, drum_radius)                       \
    reduction(+ : settled_count)
        for (int i = 0; i < drum->ball_count; i++)
        {
            DrumBall *ball = &drum->balls[i];
            if (ball->settled)
            {
                settled_count++;
                continue;
            }

            ball->vy -= BALL_GRAVITY * delta_time;
            ball->y += ball->vy * delta_time;

            float radial_sq = ball->x * ball->x + ball->z * ball->z;
            float inside = (drum_radius - BALL_RADIUS) * (drum_radius - BALL_RADIUS) - radial_sq;
            float floor_y = (inside <= 0.0f) ? (-drum_radius + BALL_RADIUS) : -sqrtf(inside);

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

            /* Apply rolling friction during falling phase */
            float contact = ball->settled ? 1.0f : 0.0f;
            apply_rolling_friction(ball, delta_time, contact);
        }

        /* Ball-to-ball collisions during falling (inelastic) */
        {
            float collision_dist = 2.0f * BALL_RADIUS;
            for (int i = 0; i < drum->ball_count; i++)
            {
                for (int j = i + 1; j < drum->ball_count; j++)
                {
                    float dx = drum->balls[j].x - drum->balls[i].x;
                    float dy = drum->balls[j].y - drum->balls[i].y;
                    float dz = drum->balls[j].z - drum->balls[i].z;
                    float dist_sq = dx * dx + dy * dy + dz * dz;
                    if (dist_sq < collision_dist * collision_dist && dist_sq > 1e-6f)
                    {
                        float dist = sqrtf(dist_sq);
                        float nx = dx / dist, ny = dy / dist, nz = dz / dist;
                        float overlap = collision_dist - dist;
                        drum->balls[i].x -= nx * (overlap * 0.5f);
                        drum->balls[i].y -= ny * (overlap * 0.5f);
                        drum->balls[i].z -= nz * (overlap * 0.5f);
                        drum->balls[j].x += nx * (overlap * 0.5f);
                        drum->balls[j].y += ny * (overlap * 0.5f);
                        drum->balls[j].z += nz * (overlap * 0.5f);
                        float rel_vx = drum->balls[j].vx - drum->balls[i].vx;
                        float rel_vy = drum->balls[j].vy - drum->balls[i].vy;
                        float rel_vz = drum->balls[j].vz - drum->balls[i].vz;
                        float rel_vn = rel_vx * nx + rel_vy * ny + rel_vz * nz;
                        if (rel_vn < 0.0f)
                        {
                            float impulse = -((1.0f + 0.3f) * rel_vn) * 0.5f;
                            drum->balls[i].vx -= impulse * nx;
                            drum->balls[i].vy -= impulse * ny;
                            drum->balls[i].vz -= impulse * nz;
                            drum->balls[j].vx += impulse * nx;
                            drum->balls[j].vy += impulse * ny;
                            drum->balls[j].vz += impulse * nz;
                        }
                    }
                }
            }
        }

        /* Boundary containment */
        {
            float inner_r = drum_radius - BALL_RADIUS;
            float inner_r_sq = inner_r * inner_r;
#pragma omp parallel for default(none) shared(drum, inner_r, inner_r_sq)
            for (int i = 0; i < drum->ball_count; i++)
            {
                DrumBall *ball = &drum->balls[i];
                float dist_sq = ball->x * ball->x + ball->y * ball->y + ball->z * ball->z;
                if (dist_sq > inner_r_sq && dist_sq > 0.0001f)
                {
                    float dist = sqrtf(dist_sq);
                    float scale = inner_r / dist;
                    ball->x *= scale;
                    ball->y *= scale;
                    ball->z *= scale;
                    float vxr = ball->vx * (ball->x / dist);
                    float vyr = ball->vy * (ball->y / dist);
                    float vzr = ball->vz * (ball->z / dist);
                    ball->vx -= vxr * (ball->x / dist);
                    ball->vy -= vyr * (ball->y / dist);
                    ball->vz -= vzr * (ball->z / dist);
                }
            }
        }

        int force_timeout = (drum->sim_time >= 3.0f);
        int min_settled = drum->ball_count / 2;
        float max_speed = 0.0f;
#pragma omp parallel for default(none) shared(drum) reduction(max : max_speed)
        for (int i = 0; i < drum->ball_count; i++)
        {
            if (!drum->balls[i].settled)
            {
                float sp = sqrtf(drum->balls[i].vx * drum->balls[i].vx +
                                 drum->balls[i].vy * drum->balls[i].vy +
                                 drum->balls[i].vz * drum->balls[i].vz);
                if (sp > max_speed)
                    max_speed = sp;
            }
        }
        int calm = (settled_count >= min_settled) && (max_speed < 50.0f);
        if (force_timeout || (calm && drum->sim_time >= 0.5f))
        {
            drum->phase = DRUM_PHASE_ROTATING;
            drum->phase_timer = 0.0f;
#pragma omp parallel for default(none) shared(drum)
            for (int i = 0; i < drum->ball_count; i++)
            {
                drum->balls[i].vx *= 0.25f;
                drum->balls[i].vy *= 0.25f;
                drum->balls[i].vz *= 0.25f;
            }
            log_info("Drum settled (%.0f%%); starting rotation at %.2fs",
                     (100.0f * settled_count) / drum->ball_count, drum->sim_time);
        }
        return;
    }

    if (drum->phase == DRUM_PHASE_DRAW_COMPLETE)
        return;

    drum->phase_timer += delta_time;

    /* ---- ROTATING ---- */
    if (drum->phase == DRUM_PHASE_ROTATING)
    {
        if (drum->phase_timer >= drum->spin_before_pick)
        {
            drum->phase = DRUM_PHASE_STOPPING;
            drum->phase_timer = 0.0f;
            drum->stop_omega = DRUM_ROTATION_SPEED_DEG;
            log_info("Drum stopping for pick %d/%d", drum->picks_done + 1, drum->picks_total);
        }
    }

    /* ---- STOPPING ---- */
    if (drum->phase == DRUM_PHASE_STOPPING)
    {
        float decel_time = 1.5f;
        drum->stop_omega = DRUM_ROTATION_SPEED_DEG * (1.0f - drum->phase_timer / decel_time);
        if (drum->stop_omega < 0.0f)
            drum->stop_omega = 0.0f;

        if (drum->phase_timer >= decel_time)
        {
            int pick_num =
                (drum->picks_done < drum->picks_total) ? drum->draw_numbers[drum->picks_done] : -1;

            drum->current_pick_idx = -1;
            for (int i = 0; i < drum->ball_count; i++)
            {
                if (drum->balls[i].ball_number == pick_num && !drum->balls[i].picked)
                {
                    drum->current_pick_idx = i;
                    drum->balls[i].picked = 1;
                    drum->balls[i].vx = drum->balls[i].vy = drum->balls[i].vz = 0.0f;
                    break;
                }
            }

            if (drum->current_pick_idx >= 0 && pick_num >= 0)
            {
                int slot = drum->result_ball_count;
                int total = drum->picks_total;
                float slot_spacing = BALL_RADIUS * 2.5f;
                float row_start_x = drum->world_x - ((total - 1) * 0.5f) * slot_spacing;
                float target_x = row_start_x + slot * slot_spacing;
                float target_y = drum->world_y - (drum_radius + BALL_RADIUS * 3.5f);
                float target_z = 0.0f;

                PickedBallDisplay *pb = &drum->result_balls[slot];
                pb->ball_number = pick_num;
                pb->x = drum->balls[drum->current_pick_idx].x + drum->world_x;
                pb->y = drum->balls[drum->current_pick_idx].y + drum->world_y;
                pb->z = drum->balls[drum->current_pick_idx].z;
                pb->tx = target_x;
                pb->ty = target_y;
                pb->tz = target_z;
                pb->vx = pb->vy = pb->vz = 0.0f;
                pb->arrived = 0;
                drum->result_ball_count++;

                drum->balls[drum->current_pick_idx].x = 9999.0f;
                drum->balls[drum->current_pick_idx].y = 9999.0f;
                drum->balls[drum->current_pick_idx].z = 9999.0f;
            }

            drum->picks_done++;
            drum->phase = DRUM_PHASE_PICK_PAUSE;
            drum->phase_timer = 0.0f;
#pragma omp parallel for default(none) shared(drum)
            for (int i = 0; i < drum->ball_count; i++)
            {
                if (!drum->balls[i].picked)
                {
                    drum->balls[i].vx = 0.0f;
                    drum->balls[i].vz = 0.0f;
                }
            }
            log_info("Drum picked ball #%d (%d/%d)", pick_num, drum->picks_done, drum->picks_total);
        }
    }

    /* ---- PICK_PAUSE ---- */
    if (drum->phase == DRUM_PHASE_PICK_PAUSE)
    {
#pragma omp parallel for default(none) shared(drum, delta_time)
        for (int s = 0; s < drum->result_ball_count; s++)
        {
            PickedBallDisplay *pb = &drum->result_balls[s];
            if (pb->arrived)
                continue;
            float dx = pb->tx - pb->x, dy = pb->ty - pb->y, dz = pb->tz - pb->z;
            float dist = sqrtf(dx * dx + dy * dy + dz * dz);
            if (dist < 2.0f)
            {
                pb->x = pb->tx;
                pb->y = pb->ty;
                pb->z = pb->tz;
                pb->vx = pb->vy = pb->vz = 0.0f;
                pb->arrived = 1;
            }
            else
            {
                float step = 350.0f * delta_time;
                if (step > dist)
                    step = dist;
                pb->x += (dx / dist) * step;
                pb->y += (dy / dist) * step;
                pb->z += (dz / dist) * step;
            }
        }

#pragma omp parallel for default(none) shared(drum, delta_time, drum_radius)
        for (int i = 0; i < drum->ball_count; i++)
        {
            DrumBall *ball = &drum->balls[i];
            if (ball->picked)
                continue;
            ball->vx *= 0.85f;
            ball->vz *= 0.85f;
            /* Drum stopped at angle theta — transform world gravity to local frame */
            float theta_p = drum->drum_rotation_z * 3.14159265f / 180.0f;
            ball->vx += -BALL_GRAVITY * sinf(theta_p) * delta_time;
            ball->vy += -BALL_GRAVITY * cosf(theta_p) * delta_time;
            ball->x += ball->vx * delta_time;
            ball->y += ball->vy * delta_time;
            ball->z += ball->vz * delta_time;
            float rsq = ball->x * ball->x + ball->z * ball->z;
            float ins = (drum_radius - BALL_RADIUS) * (drum_radius - BALL_RADIUS) - rsq;
            float fy = (ins <= 0.0f) ? (-drum_radius + BALL_RADIUS) : -sqrtf(ins);
            if (ball->y <= fy)
            {
                ball->y = fy;
                if (fabsf(ball->vy) < BALL_SETTLE_SPEED)
                    ball->vy = 0.0f;
                else
                    ball->vy = -ball->vy * BALL_BOUNCE_DAMPING;
            }
            float dsq = ball->x * ball->x + ball->y * ball->y + ball->z * ball->z;
            float mr = drum_radius - BALL_RADIUS;
            if (dsq > mr * mr && dsq > 0.0001f)
            {
                float d = sqrtf(dsq), sc = mr / d;
                ball->x *= sc;
                ball->y *= sc;
                ball->z *= sc;
                ball->vx = ball->vy = ball->vz = 0.0f;
            }
        }

        {
            float cd = 2.0f * BALL_RADIUS;
            for (int i = 0; i < drum->ball_count; i++)
            {
                if (drum->balls[i].picked)
                    continue;
                for (int j = i + 1; j < drum->ball_count; j++)
                {
                    if (drum->balls[j].picked)
                        continue;
                    float dx = drum->balls[j].x - drum->balls[i].x;
                    float dy = drum->balls[j].y - drum->balls[i].y;
                    float dz = drum->balls[j].z - drum->balls[i].z;
                    float d2 = dx * dx + dy * dy + dz * dz;
                    if (d2 < cd * cd && d2 > 1e-6f)
                    {
                        float d = sqrtf(d2), ov = (cd - d) * 0.5f;
                        float nx = dx / d, ny = dy / d, nz = dz / d;
                        drum->balls[i].x -= nx * ov;
                        drum->balls[i].y -= ny * ov;
                        drum->balls[i].z -= nz * ov;
                        drum->balls[j].x += nx * ov;
                        drum->balls[j].y += ny * ov;
                        drum->balls[j].z += nz * ov;
                    }
                }
            }
        }

        if (drum->phase_timer >= 2.5f)
        {
            if (drum->picks_done >= drum->picks_total)
            {
                drum->phase = DRUM_PHASE_DRAW_COMPLETE;
                log_info("Drum draw complete!");
            }
            else
            {
                drum->phase = DRUM_PHASE_ROTATING;
                drum->phase_timer = 0.0f;
                drum->spin_before_pick = 3.0f + frand_range(0.0f, 2.0f);
            }
        }
        return;
    }

    if (drum->phase == DRUM_PHASE_DRAW_COMPLETE || drum->phase == DRUM_PHASE_PICK_PAUSE)
        return;

    /* CPU physics for ROTATING / STOPPING */
    {
        float inner_r = drum_radius - BALL_RADIUS;
        /* Gravity points along fixed world Y axis (0, -BALL_GRAVITY, 0).
         * Transform to drum's local rotating frame by inverse rotation.
         * Drum rotated by theta around Z, so transform by -theta. */
        float theta = drum->drum_rotation_z * 3.14159265f / 180.0f;
        float gx = -BALL_GRAVITY * sinf(theta); /* negative sin */
        float gy = -BALL_GRAVITY * cosf(theta);

        for (int i = 0; i < drum->ball_count; i++)
        {
            DrumBall *ball = &drum->balls[i];
            ball->vx += gx * delta_time;
            ball->vy += gy * delta_time;

            /* Drum spins around Z: damp Z velocity every step so balls
             * stay in the XY plane regardless of 3-D shell-bounce noise. */
            ball->vz *= 0.88f;
            if (drum->phase == DRUM_PHASE_STOPPING)
            {
                /* Extra kill on sideways drift while stopping for a pick. */
                ball->vx *= 0.92f;
                ball->vz *= 0.88f; /* applied twice this step = 0.88*0.88 */
            }

            ball->vx *= BALL_AIR_DAMPING;
            ball->vy *= BALL_AIR_DAMPING;
            ball->vz *= BALL_AIR_DAMPING;
            ball->x += ball->vx * delta_time;
            ball->y += ball->vy * delta_time;
            ball->z += ball->vz * delta_time;

            float dc_sq = ball->x * ball->x + ball->y * ball->y + ball->z * ball->z;
            if (dc_sq > inner_r * inner_r)
            {
                float dc = sqrtf(dc_sq);
                float nx = ball->x / dc, ny = ball->y / dc, nz = ball->z / dc;
                float ov = dc - inner_r;
                ball->x -= nx * ov;
                ball->y -= ny * ov;
                ball->z -= nz * ov;
                float vdn = ball->vx * nx + ball->vy * ny + ball->vz * nz;
                if (vdn > 0.0f)
                {
                    float rest = 0.55f;
                    ball->vx -= (1.0f + rest) * vdn * nx;
                    ball->vy -= (1.0f + rest) * vdn * ny;
                    ball->vz -= (1.0f + rest) * vdn * nz;
                }
                if (drum->phase != DRUM_PHASE_STOPPING)
                {
                    float radial = sqrtf(ball->x * ball->x + ball->y * ball->y);
                    if (radial > 0.1f)
                    {
                        float tx = -ball->y / radial, ty = ball->x / radial;
                        float eff_omega = drum->stop_omega * 3.14159265f / 180.0f;
                        float tgt = eff_omega * radial;
                        float cur = ball->vx * tx + ball->vy * ty;
                        float mf = tgt * 0.35f;
                        if (cur < mf)
                        {
                            float dt2 = (mf - cur) * BALL_CONTACT_FRICTION * 0.35f * delta_time;
                            ball->vx += tx * dt2;
                            ball->vy += ty * dt2;
                        }
                    }
                }
            }

            /* Apply rolling friction when ball is near drum shell */
            float contact_with_shell = (dc_sq > (inner_r * inner_r * 0.98f)) ? 1.0f : 0.0f;
            apply_rolling_friction(ball, delta_time, contact_with_shell);
        }
    }

    /* Ball-to-ball collisions — spatial grid */
    {
        float collision_dist = 2.0f * BALL_RADIUS;
        float inner_r = drum_radius - BALL_RADIUS;
        int grid_dim = (int)ceilf((2.0f * inner_r) / COLLISION_CELL_SIZE);
        if (grid_dim < 1)
            grid_dim = 1;
        if (grid_dim > COLLISION_GRID_MAX_DIM)
            grid_dim = COLLISION_GRID_MAX_DIM;
        int cell_count = grid_dim * grid_dim * grid_dim;
        int *cell_heads = (int *)malloc((size_t)cell_count * sizeof(int));
        int *next_in_cell = (int *)malloc((size_t)drum->ball_count * sizeof(int));
        int *cx_arr = (int *)malloc((size_t)drum->ball_count * sizeof(int));
        int *cy_arr = (int *)malloc((size_t)drum->ball_count * sizeof(int));
        int *cz_arr = (int *)malloc((size_t)drum->ball_count * sizeof(int));

        if (!cell_heads || !next_in_cell || !cx_arr || !cy_arr || !cz_arr)
        {
            for (int i = 0; i < drum->ball_count; i++)
                for (int j = i + 1; j < drum->ball_count; j++)
                    resolve_ball_collision(&drum->balls[i], &drum->balls[j], collision_dist);
        }
        else
        {
            float grid_min = -inner_r;
            for (int c = 0; c < cell_count; c++)
                cell_heads[c] = -1;
            for (int i = 0; i < drum->ball_count; i++)
            {
                int cx = (int)floorf((drum->balls[i].x - grid_min) / COLLISION_CELL_SIZE);
                int cy = (int)floorf((drum->balls[i].y - grid_min) / COLLISION_CELL_SIZE);
                int cz = (int)floorf((drum->balls[i].z - grid_min) / COLLISION_CELL_SIZE);
                if (cx < 0)
                    cx = 0;
                else if (cx >= grid_dim)
                    cx = grid_dim - 1;
                if (cy < 0)
                    cy = 0;
                else if (cy >= grid_dim)
                    cy = grid_dim - 1;
                if (cz < 0)
                    cz = 0;
                else if (cz >= grid_dim)
                    cz = grid_dim - 1;
                int cid = (cz * grid_dim + cy) * grid_dim + cx;
                cx_arr[i] = cx;
                cy_arr[i] = cy;
                cz_arr[i] = cz;
                next_in_cell[i] = cell_heads[cid];
                cell_heads[cid] = i;
            }
            for (int i = 0; i < drum->ball_count; i++)
            {
                int cx = cx_arr[i], cy = cy_arr[i], cz = cz_arr[i];
                int nx0 = (cx > 0) ? cx - 1 : 0, ny0 = (cy > 0) ? cy - 1 : 0,
                    nz0 = (cz > 0) ? cz - 1 : 0;
                int nx1 = (cx + 1 < grid_dim) ? cx + 1 : grid_dim - 1;
                int ny1 = (cy + 1 < grid_dim) ? cy + 1 : grid_dim - 1;
                int nz1 = (cz + 1 < grid_dim) ? cz + 1 : grid_dim - 1;
                for (int nzz = nz0; nzz <= nz1; nzz++)
                    for (int nyy = ny0; nyy <= ny1; nyy++)
                        for (int nxx = nx0; nxx <= nx1; nxx++)
                        {
                            int nid = (nzz * grid_dim + nyy) * grid_dim + nxx;
                            for (int j = cell_heads[nid]; j != -1; j = next_in_cell[j])
                            {
                                if (j <= i)
                                    continue;
                                resolve_ball_collision(&drum->balls[i], &drum->balls[j],
                                                       collision_dist);
                            }
                        }
            }
        }
        free(cell_heads);
        free(next_in_cell);
        free(cx_arr);
        free(cy_arr);
        free(cz_arr);
    }

    /* Final safety boundary pass */
    {
        float inner_r = drum_radius - BALL_RADIUS;
        float inner_r_sq = inner_r * inner_r;
        for (int i = 0; i < drum->ball_count; i++)
        {
            DrumBall *ball = &drum->balls[i];
            float dsq = ball->x * ball->x + ball->y * ball->y + ball->z * ball->z;
            if (dsq > inner_r_sq && dsq > 0.0001f)
            {
                float d = sqrtf(dsq), sc = inner_r / d;
                ball->x *= sc;
                ball->y *= sc;
                ball->z *= sc;
            }
        }
    }

    float eff_omega =
        (drum->phase == DRUM_PHASE_STOPPING) ? drum->stop_omega : DRUM_ROTATION_SPEED_DEG;

    /* Keep agreed behavior: rotate only around Z axis. */
    drum->drum_rotation_z += eff_omega * delta_time;
    while (drum->drum_rotation_z > 360.0f)
        drum->drum_rotation_z -= 360.0f;

    /* Record ball positions for trail visualization (sampled every 3 frames) */
    if (drum->trail_x)
    {
        drum->trail_frame_counter++;
        if (drum->trail_frame_counter >= 3)
        {
            drum->trail_frame_counter = 0;
            for (int i = 0; i < drum->ball_count; i++)
            {
                if (drum->balls[i].picked)
                    continue;
                int head = drum->trail_head[i];
                int base = i * BALL_TRAIL_LENGTH + head;
                drum->trail_x[base] = drum->balls[i].x;
                drum->trail_y[base] = drum->balls[i].y;
                drum->trail_z[base] = drum->balls[i].z;
                drum->trail_head[i] = (head + 1) % BALL_TRAIL_LENGTH;
            }
        }
    }

    /* Ensure no residual tumble on X/Y axes. */
    drum->drum_rotation_x = 0.0f;
    drum->drum_rotation_y = 0.0f;
}

static void update_animation(GuiState3D *state, float delta_time)
{
    update_drum_instance(state->main_drum, delta_time);

    if (state->extra_drum)
    {
        /* Unlock extra drum once main draw is complete */
        if (state->main_drum->phase == DRUM_PHASE_DRAW_COMPLETE)
            state->extra_drum->waiting = 0;
        update_drum_instance(state->extra_drum, delta_time);
    }

    /* Mark animation complete only once both drums finish */
    int main_done = (state->main_drum->phase == DRUM_PHASE_DRAW_COMPLETE);
    int extra_done =
        (state->extra_drum == NULL || state->extra_drum->phase == DRUM_PHASE_DRAW_COMPLETE);
    state->animation_complete = (main_done && extra_done) ? 1 : 0;

    /* GPU compute path for main drum (ROTATING phase only).
       Keep buffers synchronized in non-rotating phases so picked balls
       do not reappear when switching back to GPU updates. */
    if (state->use_gpu_compute)
    {
        if (state->main_drum->phase == DRUM_PHASE_ROTATING)
            update_animation_gpu(state, delta_time);
        else
            sync_cpu_balls_to_gpu(state);
    }
}

/* ============================================================
   MAIN GUI FUNCTION
   ============================================================ */

void gui_run_opengl(const char *game_name, const LotteryInfo *info, int debug_overlay,
                    int dark_mode)
{
    log_info("Launching 3D OpenGL GUI (Sphere Drums) for %s", game_name);

    GuiState3D *state = gui_state_create(game_name, info, debug_overlay, dark_mode);
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

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    state->gl_context = SDL_GL_CreateContext(state->window);

    if (!state->gl_context)
    {
        log_warn("OpenGL 4.3 context unavailable, falling back to OpenGL 2.1");
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        state->gl_context = SDL_GL_CreateContext(state->window);
    }

    if (!state->gl_context)
    {
        log_error("OpenGL context creation failed: %s", SDL_GetError());
        SDL_DestroyWindow(state->window);
        SDL_Quit();
        gui_state_destroy(state);
        return;
    }

    SDL_GL_SetSwapInterval(1); /* Enable vsync */

#ifdef _WIN32
    /* Initialise GLEW to load OpenGL extension function pointers on Windows */
    glewExperimental = GL_TRUE;
    GLenum glew_err = glewInit();
    if (glew_err != GLEW_OK)
    {
        log_error("GLEW initialisation failed: %s", glewGetErrorString(glew_err));
        SDL_GL_DeleteContext(state->gl_context);
        SDL_DestroyWindow(state->window);
        SDL_Quit();
        gui_state_destroy(state);
        return;
    }
    log_info("GLEW version: %s", glewGetString(GLEW_VERSION));
#endif

    setup_opengl(state->dark_mode);
    init_ball_textures(state);

    {
        const char *gl_version = (const char *)glGetString(GL_VERSION);
        if (gl_version)
            log_info("OpenGL runtime version: %s", gl_version);
    }

    if (state->_gpu_attempt_enabled && !init_gpu_compute(state))
    {
        state->use_gpu_compute = 0;
        log_info("GPU compute initialization failed; using CPU physics path");
    }
    else if (!state->_gpu_attempt_enabled)
    {
        state->use_gpu_compute = 0;
    }

    /* Generate the lottery draw */
    generate_draw(info->main_count, info->main_min, info->main_max, info->extra_count,
                  info->extra_min, info->extra_max, &state->result, on_draw_event);

    /* Copy draw results into per-drum draw_numbers arrays */
    {
        for (int i = 0; i < state->result.main_count && i < MAX_MAIN_NUMBERS; i++)
            state->main_drum->draw_numbers[i] = state->result.main_numbers[i];
    }
    if (state->extra_drum)
    {
        for (int i = 0; i < state->result.extra_count && i < MAX_EXTRA_NUMBERS; i++)
            state->extra_drum->draw_numbers[i] = state->result.extra_numbers[i];
    }

    log_info("Displaying %s - %d main balls, %d extra balls", game_name,
             state->main_drum->ball_count, state->extra_drum ? state->extra_drum->ball_count : 0);
    log_info("Controls: drag to orbit | wheel to zoom | Space to pause | R/T/F/S/I: camera views");
    if (state->debug_overlay)
        log_info("Debug overlay enabled: shows FPS, physics mode, and drum status (+/- for speed)");

    /* Main loop */
    int running = 1;
    Uint32 last_time = SDL_GetTicks();
    state->fps_last_time = last_time;

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
                else if (event.key.keysym.sym == SDLK_SPACE)
                    state->paused = !state->paused;
                else if (event.key.keysym.sym == SDLK_r || event.key.keysym.sym == SDLK_i)
                {
                    /* R: reset camera, I: isometric view (both use trimetric angles) */
                    set_camera_view(state, CAMERA_TRIMETRIC_X, CAMERA_TRIMETRIC_Y);
                }
                else if (event.key.keysym.sym == SDLK_PLUS || event.key.keysym.sym == SDLK_EQUALS)
                {
                    state->animation_speed_multiplier += ANIMATION_SPEED_STEP;
                    if (state->animation_speed_multiplier > ANIMATION_SPEED_MAX)
                        state->animation_speed_multiplier = ANIMATION_SPEED_MAX;
                }
                else if (event.key.keysym.sym == SDLK_MINUS)
                {
                    state->animation_speed_multiplier -= ANIMATION_SPEED_STEP;
                    if (state->animation_speed_multiplier < ANIMATION_SPEED_MIN)
                        state->animation_speed_multiplier = ANIMATION_SPEED_MIN;
                }
                else if (event.key.keysym.sym == SDLK_n)
                {
                    state->animation_speed_multiplier = ANIMATION_SPEED_DEFAULT;
                }
                else if (event.key.keysym.sym == SDLK_t)
                {
                    set_camera_view(state, CAMERA_TOP_X, CAMERA_TOP_Y);
                }
                else if (event.key.keysym.sym == SDLK_f)
                {
                    set_camera_view(state, CAMERA_FRONT_X, CAMERA_FRONT_Y);
                }
                else if (event.key.keysym.sym == SDLK_s)
                {
                    set_camera_view(state, CAMERA_SIDE_X, CAMERA_SIDE_Y);
                }
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
                    int dy = event.motion.y - state->last_mouse_y;
                    state->camera_yaw += (float)dx * MOUSE_ORBIT_SENSITIVITY;
                    state->camera_pitch += (float)dy * MOUSE_ORBIT_SENSITIVITY;
                    if (state->camera_pitch < CAMERA_PITCH_MIN)
                        state->camera_pitch = CAMERA_PITCH_MIN;
                    if (state->camera_pitch > CAMERA_PITCH_MAX)
                        state->camera_pitch = CAMERA_PITCH_MAX;
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
            default:
                break;
            }
        }

        Uint32 current_time = SDL_GetTicks();
        float delta_time = (current_time - last_time) / 1000.0f;
        if (delta_time > 0.05f)
            delta_time = 0.05f; /* Cap at 50ms */
        last_time = current_time;

        /* FPS tracking — update sample every 500 ms */
        state->fps_frame_count++;
        if (current_time - state->fps_last_time >= 500)
        {
            float elapsed = (float)(current_time - state->fps_last_time) / 1000.0f;
            state->fps_current = (elapsed > 0.0f) ? (float)state->fps_frame_count / elapsed : 0.0f;
            state->fps_frame_count = 0;
            state->fps_last_time = current_time;
        }

        /* Update animation (skip when paused) */
        if (!state->paused)
            update_animation(state, delta_time * state->animation_speed_multiplier);

        /* Render */
        render_scene(state);
        SDL_GL_SwapWindow(state->window);

        SDL_Delay(16); /* ~60 FPS */
    }

    log_info("Closing 3D Drum Display");

    destroy_gpu_compute(state);
    gui_state_destroy(state);

    SDL_GL_DeleteContext(state->gl_context);
    SDL_DestroyWindow(state->window);
    TTF_Quit();
    SDL_Quit();
}

static void draw_box_prism(float w, float h, float d)
{
    float x = w * 0.5f;
    float y = h;
    float z = d * 0.5f;

    glBegin(GL_QUADS);
    /* front */
    glVertex3f(-x, 0.0f, z);
    glVertex3f(x, 0.0f, z);
    glVertex3f(x, y, z);
    glVertex3f(-x, y, z);
    /* back */
    glVertex3f(-x, 0.0f, -z);
    glVertex3f(-x, y, -z);
    glVertex3f(x, y, -z);
    glVertex3f(x, 0.0f, -z);
    /* left */
    glVertex3f(-x, 0.0f, -z);
    glVertex3f(-x, 0.0f, z);
    glVertex3f(-x, y, z);
    glVertex3f(-x, y, -z);
    /* right */
    glVertex3f(x, 0.0f, -z);
    glVertex3f(x, y, -z);
    glVertex3f(x, y, z);
    glVertex3f(x, 0.0f, z);
    /* top */
    glVertex3f(-x, y, -z);
    glVertex3f(-x, y, z);
    glVertex3f(x, y, z);
    glVertex3f(x, y, -z);
    /* bottom */
    glVertex3f(-x, 0.0f, -z);
    glVertex3f(x, 0.0f, -z);
    glVertex3f(x, 0.0f, z);
    glVertex3f(-x, 0.0f, z);
    glEnd();
}

/* -----------------------------------------------------------------------
 * Shared OpenGL analytics camera helpers  (mirrors drum camera style)
 * ----------------------------------------------------------------------- */

typedef struct
{
    float pitch;          /* X-axis rotation  (matches drum camera_pitch) */
    float yaw;            /* Y-axis rotation  (matches drum camera_yaw) */
    float cam_z;          /* Z translate (zoom) */
    int   mouse_dragging;
    int   last_mx, last_my;
} AnalyticsCamera;

#define ACAM_PITCH_DEFAULT  24.0f
#define ACAM_YAW_DEFAULT     0.0f
#define ACAM_Z_DEFAULT     -28.0f
#define ACAM_Z_MIN         -80.0f
#define ACAM_Z_MAX          -6.0f
#define ACAM_ORBIT_SENS    MOUSE_ORBIT_SENSITIVITY   /* 0.25 */
#define ACAM_ZOOM_STEP     (MOUSE_ZOOM_STEP * 0.08f) /* 1.6 units */

static void acam_init(AnalyticsCamera *c)
{
    c->pitch          = ACAM_PITCH_DEFAULT;
    c->yaw            = ACAM_YAW_DEFAULT;
    c->cam_z          = ACAM_Z_DEFAULT;
    c->mouse_dragging = 0;
}

static int acam_handle_event(AnalyticsCamera *c, SDL_Event *ev, int *quit)
{
    switch (ev->type)
    {
    case SDL_QUIT:
        *quit = 1;
        break;
    case SDL_KEYDOWN:
        if (ev->key.keysym.sym == SDLK_ESCAPE) { *quit = 1; break; }
        if (ev->key.keysym.sym == SDLK_r || ev->key.keysym.sym == SDLK_i)
            { c->pitch = ACAM_PITCH_DEFAULT; c->yaw = ACAM_YAW_DEFAULT; }
        else if (ev->key.keysym.sym == SDLK_t)
            { c->pitch = -89.9f; c->yaw = 0.0f; }
        else if (ev->key.keysym.sym == SDLK_f)
            { c->pitch = 0.0f;  c->yaw = 0.0f; }
        else if (ev->key.keysym.sym == SDLK_s)
            { c->pitch = 0.0f;  c->yaw = 90.0f; }
        break;
    case SDL_MOUSEBUTTONDOWN:
        if (ev->button.button == SDL_BUTTON_LEFT)
        { c->mouse_dragging = 1; c->last_mx = ev->button.x; c->last_my = ev->button.y; }
        break;
    case SDL_MOUSEBUTTONUP:
        if (ev->button.button == SDL_BUTTON_LEFT) c->mouse_dragging = 0;
        break;
    case SDL_MOUSEMOTION:
        if (c->mouse_dragging)
        {
            c->yaw   += (float)(ev->motion.x - c->last_mx) * ACAM_ORBIT_SENS;
            c->pitch += (float)(ev->motion.y - c->last_my) * ACAM_ORBIT_SENS;
            if (c->pitch < -85.0f) c->pitch = -85.0f;
            if (c->pitch >  85.0f) c->pitch =  85.0f;
            c->last_mx = ev->motion.x;
            c->last_my = ev->motion.y;
        }
        break;
    case SDL_MOUSEWHEEL:
        c->cam_z += ev->wheel.y * ACAM_ZOOM_STEP;
        if (c->cam_z < ACAM_Z_MIN) c->cam_z = ACAM_Z_MIN;
        if (c->cam_z > ACAM_Z_MAX) c->cam_z = ACAM_Z_MAX;
        break;
    default:
        break;
    }
    return 0;
}

/* Apply the camera transform for a 1000x700 analytics window */
static void acam_apply(const AnalyticsCamera *c)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float aspect = 1000.0f / 700.0f;
    glFrustum(-aspect, aspect, -1.0, 1.0, 1.5, 200.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, -4.0f, c->cam_z);
    glRotatef(c->pitch, 1.0f, 0.0f, 0.0f);
    glRotatef(c->yaw,   0.0f, 1.0f, 0.0f);
}

/* Draw floor grid (XZ plane) */
static void draw_floor_grid(float half, float step)
{
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    for (float v = -half; v <= half + 0.001f; v += step)
    {
        glVertex3f(v, 0.0f, -half);
        glVertex3f(v, 0.0f,  half);
        glVertex3f(-half, 0.0f, v);
        glVertex3f( half, 0.0f, v);
    }
    glEnd();
    glLineWidth(1.0f);
}

/* Draw Y-axis spine + tick marks */
static void draw_y_axis(float height)
{
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f(0.0f, height, 0.0f);
    glEnd();
    glLineWidth(1.0f);
}

/* Ortho HUD overlay — same technique as drum debug overlay */
static void draw_analytics_hud_3d(const char *subtitle, int dark_mode)
{
    (void)subtitle;
    (void)dark_mode;
    /* Future: render TTF text via SDL_Renderer overlay or glBitmap.
     * Current: noop placeholder so the architecture is wired. */
}

/* Read optional timeout (same logic as SDL helper) */
static Uint32 analytics_gl_timeout_ms(void)
{
    const char *env = getenv("OPEN_LOTTO_ANALYTICS_GUI_TIMEOUT_MS");
    if (!env || env[0] == '\0') return 0;
    long v = atol(env);
    return (v > 0) ? (Uint32)v : 0;
}

int gui_render_frequency_3d(const char *title, const FrequencyReport *report, int dark_mode)
{
    if (!report) return -1;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return -1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window *window = SDL_CreateWindow(
        title ? title : "Frequency Distribution (3D)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1000, 700, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!window) { SDL_Quit(); return -1; }

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) { SDL_DestroyWindow(window); SDL_Quit(); return -1; }

#ifdef _WIN32
    glewInit();
#endif

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    int max_count = 1;
    for (int n = report->number_min; n <= report->number_max; n++)
        if (report->counts[n] > max_count) max_count = report->counts[n];

    const int   bars  = report->number_max - report->number_min + 1;
    const float bar_w = bars > 30 ? 0.20f : 0.32f;
    const float bar_gap = 0.06f;
    const float depth  = 0.30f;
    const float span   = bars * (bar_w + bar_gap);

    AnalyticsCamera cam;
    acam_init(&cam);

    Uint32 start   = SDL_GetTicks();
    Uint32 timeout = analytics_gl_timeout_ms();
    float  anim    = 0.0f;
    Uint32 last_t  = start;
    int    running = 1;

    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
            acam_handle_event(&cam, &ev, &running);
        if (timeout > 0 && SDL_GetTicks() - start >= timeout) running = 0;

        Uint32 now = SDL_GetTicks();
        float dt = (now - last_t) / 1000.0f;
        last_t = now;
        anim += dt / 0.5f;
        if (anim > 1.0f) anim = 1.0f;

        if (dark_mode == 1) glClearColor(0.06f, 0.07f, 0.11f, 1.0f);
        else                glClearColor(0.93f, 0.94f, 0.97f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        acam_apply(&cam);

        /* Floor grid */
        glColor4f(0.35f, 0.40f, 0.50f, 0.5f);
        draw_floor_grid(span * 0.55f, (bar_w + bar_gap));

        /* Y-axis */
        glColor4f(0.55f, 0.60f, 0.70f, 0.8f);
        draw_y_axis(11.5f);

        /* Bars */
        float x0 = -span * 0.5f;
        for (int i = 0; i < bars; i++)
        {
            int   number = report->number_min + i;
            float full_h = 0.15f + (10.5f * (float)report->counts[number]) / (float)max_count;
            float h = full_h * (anim < 1.0f ? anim : 1.0f);
            float x = x0 + i * (bar_w + bar_gap);
            float t = (float)i / (float)(bars > 1 ? bars - 1 : 1);

            glPushMatrix();
            glTranslatef(x, 0.0f, 0.0f);
            /* Gradient blue→gold matching main ball palette */
            glColor3f(0.18f + 0.65f * t, 0.55f + 0.30f * (1.0f - t),
                      0.82f - 0.60f * t);
            draw_box_prism(bar_w, h, depth);
            /* Bright top cap */
            glColor4f(1.0f, 1.0f, 1.0f, 0.35f);
            draw_box_prism(bar_w, 0.06f, depth + 0.02f);
            glPopMatrix();
            glTranslatef(0, h, 0);
        }

        draw_analytics_hud_3d(title, dark_mode);
        SDL_GL_SwapWindow(window);
        SDL_Delay(16);
    }

    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

int gui_render_barometer_3d(const char *title, const BarometerReport *report, int dark_mode)
{
    if (!report) return -1;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return -1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window *window = SDL_CreateWindow(
        title ? title : "Barometer \u2014 Overdue Factor (3D)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1000, 700, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!window) { SDL_Quit(); return -1; }

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) { SDL_DestroyWindow(window); SDL_Quit(); return -1; }
#ifdef _WIN32
    glewInit();
#endif
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    double max_factor = 0.01;
    for (int n = report->number_min; n <= report->number_max; n++)
        if (report->factors[n] > max_factor) max_factor = report->factors[n];

    const int   bars    = report->number_max - report->number_min + 1;
    const float bar_w   = bars > 30 ? 0.20f : 0.32f;
    const float bar_gap = 0.06f;
    const float depth   = 0.30f;
    const float span    = bars * (bar_w + bar_gap);

    AnalyticsCamera cam;
    acam_init(&cam);

    Uint32 start   = SDL_GetTicks();
    Uint32 timeout = analytics_gl_timeout_ms();
    float  anim    = 0.0f;
    Uint32 last_t  = start;
    int    running = 1;

    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
            acam_handle_event(&cam, &ev, &running);
        if (timeout > 0 && SDL_GetTicks() - start >= timeout) running = 0;

        Uint32 now = SDL_GetTicks();
        anim += (now - last_t) / 1000.0f / 0.5f;
        last_t = now;
        if (anim > 1.0f) anim = 1.0f;

        if (dark_mode == 1) glClearColor(0.06f, 0.07f, 0.11f, 1.0f);
        else                glClearColor(0.93f, 0.94f, 0.97f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        acam_apply(&cam);

        /* Floor grid */
        glColor4f(0.35f, 0.40f, 0.50f, 0.5f);
        draw_floor_grid(span * 0.55f, bar_w + bar_gap);

        /* Reference line at factor = 1.0 (expected interval) */
        float ref_h = (float)(1.0 / max_factor) * 10.5f;
        glColor4f(0.25f, 0.85f, 0.30f, 0.75f);
        glLineWidth(2.0f);
        glBegin(GL_LINES);
        glVertex3f(-span * 0.55f, ref_h, 0.0f);
        glVertex3f( span * 0.55f, ref_h, 0.0f);
        glEnd();
        glLineWidth(1.0f);

        draw_y_axis(11.5f);

        float x0 = -span * 0.5f;
        for (int i = 0; i < bars; i++)
        {
            int   number = report->number_min + i;
            float full_h = 0.15f + (float)(report->factors[number] / max_factor) * 10.5f;
            float h = full_h * (anim < 1.0f ? anim : 1.0f);
            float x = x0 + i * (bar_w + bar_gap);
            /* Orange→red gradient (overdue = hot) */
            float t = (float)i / (float)(bars > 1 ? bars - 1 : 1);

            glPushMatrix();
            glTranslatef(x, 0.0f, 0.0f);
            glColor3f(0.95f, 0.55f - 0.30f * t, 0.15f + 0.10f * t);
            draw_box_prism(bar_w, h, depth);
            glColor4f(1.0f, 1.0f, 1.0f, 0.35f);
            draw_box_prism(bar_w, 0.06f, depth + 0.02f);
            glPopMatrix();
            glTranslatef(0, h, 0);
        }

        draw_analytics_hud_3d(title, dark_mode);
        SDL_GL_SwapWindow(window);
        SDL_Delay(16);
    }

    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

int gui_render_hot_cold_3d(const char *title, const HotColdReport *report, int dark_mode)
{
    if (!report) return -1;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return -1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window *window = SDL_CreateWindow(
        title ? title : "Hot / Cold Numbers (3D)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1000, 700, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!window) { SDL_Quit(); return -1; }

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) { SDL_DestroyWindow(window); SDL_Quit(); return -1; }
#ifdef _WIN32
    glewInit();
#endif
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    int max_count = 1;
    for (int i = 0; i < report->top_n; i++)
    {
        if (report->hot[i].count  > max_count) max_count = report->hot[i].count;
        if (report->cold[i].count > max_count) max_count = report->cold[i].count;
    }

    const int   bars    = report->top_n;
    const float bar_w   = bars > 16 ? 0.28f : 0.40f;
    const float bar_gap = 0.12f;
    const float depth   = 0.28f;
    /* Two rows: front row = hot, back row = cold, separated in Z */
    const float row_sep = depth * 2.5f;
    const float span    = bars * (bar_w + bar_gap);

    AnalyticsCamera cam;
    acam_init(&cam);
    cam.pitch = 20.0f;
    cam.yaw   = 15.0f;

    Uint32 start   = SDL_GetTicks();
    Uint32 timeout = analytics_gl_timeout_ms();
    float  anim    = 0.0f;
    Uint32 last_t  = start;
    int    running = 1;

    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
            acam_handle_event(&cam, &ev, &running);
        if (timeout > 0 && SDL_GetTicks() - start >= timeout) running = 0;

        Uint32 now = SDL_GetTicks();
        anim += (now - last_t) / 1000.0f / 0.5f;
        last_t = now;
        if (anim > 1.0f) anim = 1.0f;

        if (dark_mode == 1) glClearColor(0.06f, 0.07f, 0.11f, 1.0f);
        else                glClearColor(0.93f, 0.94f, 0.97f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        acam_apply(&cam);

        /* Floor grid */
        glColor4f(0.35f, 0.40f, 0.50f, 0.4f);
        draw_floor_grid(span * 0.55f + row_sep, bar_w + bar_gap);
        draw_y_axis(11.5f);

        float x0 = -span * 0.5f;
        for (int i = 0; i < bars; i++)
        {
            float x = x0 + i * (bar_w + bar_gap);

            float h_hot  = (0.15f + (10.5f * (float)report->hot[i].count)  / (float)max_count)
                           * (anim < 1.0f ? anim : 1.0f);
            float h_cold = (0.15f + (10.5f * (float)report->cold[i].count) / (float)max_count)
                           * (anim < 1.0f ? anim : 1.0f);

            /* Hot row — front (positive Z), red, main-drum ball colour */
            glPushMatrix();
            glTranslatef(x, 0.0f, row_sep);
            glColor3f(0.92f, 0.22f, 0.20f);
            draw_box_prism(bar_w, h_hot, depth);
            glColor4f(1.0f, 0.6f, 0.3f, 0.50f);
            draw_box_prism(bar_w, 0.06f, depth + 0.02f);
            glPopMatrix();

            /* Cold row — back (negative Z), blue, extra-drum ball colour */
            glPushMatrix();
            glTranslatef(x, 0.0f, -row_sep);
            glColor3f(0.18f, 0.45f, 0.92f);
            draw_box_prism(bar_w, h_cold, depth);
            glColor4f(0.5f, 0.8f, 1.0f, 0.50f);
            draw_box_prism(bar_w, 0.06f, depth + 0.02f);
            glPopMatrix();
        }

        draw_analytics_hud_3d(title, dark_mode);
        SDL_GL_SwapWindow(window);
        SDL_Delay(16);
    }

    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
