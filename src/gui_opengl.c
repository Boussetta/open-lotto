#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_ttf.h>
#include <math.h>
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
#define COLLISION_CELL_SIZE (BALL_RADIUS * 2.2f)
#define COLLISION_GRID_MAX_DIM 16
#define COLLISION_GRID_MAX_CELLS (COLLISION_GRID_MAX_DIM * COLLISION_GRID_MAX_DIM * COLLISION_GRID_MAX_DIM)
#define GPU_COMPUTE_LOCAL_SIZE 64

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
    float vx;
    float vy;
    float vz;
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
    int   ball_number;
    float x, y, z;       /* world position (outside drum) */
    float vx, vy, vz;    /* velocity while flying to target */
    float tx, ty, tz;    /* target world position */
    int   arrived;       /* 1 once it has reached its slot */
} PickedBallDisplay;

typedef struct
{
    LotteryInfo info;
    LotteryResult result;
    
    float drum_rotation_x;  /* Rotation angles for tumbling effect */
    float drum_rotation_y;
    float drum_rotation_z;

    DrumBall *balls;
    int ball_count;
    DrumPhase phase;

    /* Draw cycle */
    int picks_done;           /* how many balls have been drawn so far */
    int picks_total;          /* total to draw (main_count + extra_count) */
    float phase_timer;        /* seconds spent in current sub-phase */
    float spin_before_pick;   /* seconds of spinning before next stop */
    int   current_pick_idx;   /* index into balls[] of currently highlighted ball */
    float stop_omega;         /* current rotation speed during deceleration */

    /* Result display row (outside drum) */
    PickedBallDisplay result_balls[16]; /* max 7 main + 2 extra */
    int result_ball_count;

    float camera_pitch;
    float camera_yaw;
    float camera_roll;
    float camera_z;

    int mouse_dragging;
    int last_mouse_x;
    int last_mouse_y;
    
    SDL_Window *window;
    SDL_GLContext gl_context;

    int use_gpu_compute;
    int _gpu_attempt_enabled;  /* Whether to attempt GPU compute initialization */
    GLuint compute_program;
    GLuint ball_ssbo;
    GpuBall *gpu_ball_cache;

    TTF_Font *font;
    GLuint *number_textures;  /* one texture per ball, indexed 0..ball_count-1 */

    float sim_time;

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

    for (int i = 0; i < state->ball_count; i++)
    {
        state->gpu_ball_cache[i].px = state->balls[i].x;
        state->gpu_ball_cache[i].py = state->balls[i].y;
        state->gpu_ball_cache[i].pz = state->balls[i].z;
        state->gpu_ball_cache[i].settled = (float)state->balls[i].settled;
        state->gpu_ball_cache[i].vx = state->balls[i].vx;
        state->gpu_ball_cache[i].vy = state->balls[i].vy;
        state->gpu_ball_cache[i].vz = state->balls[i].vz;
        state->gpu_ball_cache[i].pad = 0.0f;
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, state->ball_ssbo);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GpuBall) * state->ball_count,
                    state->gpu_ball_cache);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

static void sync_gpu_balls_to_cpu(GuiState3D *state)
{
    if (!state->use_gpu_compute)
        return;

    /* Ensure all GPU operations are complete before reading data */
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    glFinish();

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, state->ball_ssbo);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GpuBall) * state->ball_count,
                       state->gpu_ball_cache);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    for (int i = 0; i < state->ball_count; i++)
    {
        state->balls[i].x = state->gpu_ball_cache[i].px;
        state->balls[i].y = state->gpu_ball_cache[i].py;
        state->balls[i].z = state->gpu_ball_cache[i].pz;
        state->balls[i].vx = state->gpu_ball_cache[i].vx;
        state->balls[i].vy = state->gpu_ball_cache[i].vy;
        state->balls[i].vz = state->gpu_ball_cache[i].vz;
        state->balls[i].settled = (state->gpu_ball_cache[i].settled > 0.5f) ? 1 : 0;
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
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GpuBall) * state->ball_count, NULL, GL_DYNAMIC_DRAW);
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
    if (!state->compute_program || state->compute_program == 0)
        return;

    GLuint groups = (GLuint)((state->ball_count + GPU_COMPUTE_LOCAL_SIZE - 1) / GPU_COMPUTE_LOCAL_SIZE);
    float omega_rad_per_sec = DRUM_ROTATION_SPEED_DEG * 3.14159265f / 180.0f;

    glUseProgram(state->compute_program);

    glUniform1i(glGetUniformLocation(state->compute_program, "uBallCount"), state->ball_count);
    glUniform1f(glGetUniformLocation(state->compute_program, "uDeltaTime"), delta_time);
    glUniform1f(glGetUniformLocation(state->compute_program, "uDrumRadius"), DRUM_RADIUS);
    glUniform1f(glGetUniformLocation(state->compute_program, "uBallRadius"), BALL_RADIUS);
    glUniform1f(glGetUniformLocation(state->compute_program, "uGravity"), BALL_GRAVITY);
    glUniform1f(glGetUniformLocation(state->compute_program, "uAirDamping"), BALL_AIR_DAMPING);
    glUniform1f(glGetUniformLocation(state->compute_program, "uOmega"), omega_rad_per_sec);
    glUniform1f(glGetUniformLocation(state->compute_program, "uDrumRotationZDeg"), state->drum_rotation_z);
    glUniform1f(glGetUniformLocation(state->compute_program, "uCollisionRestitution"),
                BALL_COLLISION_RESTITUTION);
    glUniform1f(glGetUniformLocation(state->compute_program, "uCollisionTangentialTransfer"),
                BALL_COLLISION_TANGENTIAL_TRANSFER);
    glUniform1f(glGetUniformLocation(state->compute_program, "uCollisionImpulseBoost"),
                BALL_COLLISION_IMPULSE_BOOST);
    glUniform1f(glGetUniformLocation(state->compute_program, "uCollisionBurstSpeed"),
                BALL_COLLISION_BURST_SPEED);
    glUniform1f(glGetUniformLocation(state->compute_program, "uContactFriction"), BALL_CONTACT_FRICTION);

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
        {
            float tvx = dvx - rel_vel_along_normal * nx;
            float tvy = dvy - rel_vel_along_normal * ny;
            float tvz = dvz - rel_vel_along_normal * nz;
            float t_impulse = BALL_COLLISION_TANGENTIAL_TRANSFER * 0.5f;

            ball_i->vx += tvx * t_impulse;
            ball_i->vy += tvy * t_impulse;
            ball_i->vz += tvz * t_impulse;

            ball_j->vx -= tvx * t_impulse;
            ball_j->vy -= tvy * t_impulse;
            ball_j->vz -= tvz * t_impulse;
        }
    }
}

static void init_balls(GuiState3D *state)
{
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
        state->balls[i].vx = 0.0f;
        state->balls[i].vy = 0.0f;
        state->balls[i].vz = 0.0f;
        state->balls[i].settled = 0;
        state->balls[i].picked = 0;
        state->balls[i].ball_number = i + 1;
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
    state->sim_time = 0.0f;

    state->ball_count = state->info.main_max;
    if (state->ball_count <= 0)
        state->ball_count = 50;
    if (state->ball_count > MAX_GAME_BALLS)
        state->ball_count = MAX_GAME_BALLS;

    state->balls = (DrumBall *)calloc((size_t)state->ball_count, sizeof(DrumBall));
    state->gpu_ball_cache = (GpuBall *)calloc((size_t)state->ball_count, sizeof(GpuBall));
    if (!state->balls || !state->gpu_ball_cache)
    {
        free(state->balls);
        free(state->gpu_ball_cache);
        free(state);
        return NULL;
    }

    srand((unsigned int)time(NULL));
    init_balls(state);

    /* Draw cycle initialisation */
    state->picks_done        = 0;
    state->picks_total       = info->main_count + info->extra_count;
    state->phase_timer       = 0.0f;
    state->spin_before_pick  = 3.0f + frand_range(0.0f, 2.0f); /* 3-5 s of spinning */
    state->current_pick_idx  = -1;
    state->stop_omega        = DRUM_ROTATION_SPEED_DEG;
    state->result_ball_count = 0;

    /* Only attempt GPU compute if explicitly enabled via environment variable */
    /* This prevents freezing on systems with incomplete OpenGL 4.3 support */
    const char *enable_gpu = getenv("LOTTO_GPU_COMPUTE");
    int try_gpu = (enable_gpu != NULL && (enable_gpu[0] == '1' || enable_gpu[0] == 'y' || enable_gpu[0] == 'Y'));
    
    if (try_gpu)
    {
        log_info("GPU compute mode requested (LOTTO_GPU_COMPUTE set)");
    }
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

    if (state->number_textures)
    {
        glDeleteTextures(state->ball_count, state->number_textures);
        free(state->number_textures);
    }
    if (state->font)
        TTF_CloseFont(state->font);

    free(state->balls);
    free(state->gpu_ball_cache);
    free(state);
}

/* ============================================================
   RENDERING
   ============================================================ */

/* Create an OpenGL texture from a ball number using SDL_ttf */
static GLuint make_number_texture(TTF_Font *font, int number)
{
    if (!font) return 0;

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", number);

    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface *text_surf = TTF_RenderText_Blended(font, buf, white);
    if (!text_surf) return 0;

    /* Square canvas, power-of-two for compatibility */
    int tex_size = 64;
    SDL_Surface *canvas = SDL_CreateRGBSurface(0, tex_size, tex_size, 32,
        0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0xFF000000u);
    if (!canvas) { SDL_FreeSurface(text_surf); return 0; }
    SDL_FillRect(canvas, NULL, 0);

    SDL_Rect dst = {
        (tex_size - text_surf->w) / 2,
        (tex_size - text_surf->h) / 2,
        text_surf->w, text_surf->h
    };
    SDL_BlitSurface(text_surf, NULL, canvas, &dst);
    SDL_FreeSurface(text_surf);

    SDL_Surface *conv = SDL_ConvertSurfaceFormat(canvas, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(canvas);
    if (!conv) return 0;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, conv->w, conv->h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, conv->pixels);
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
        log_warn("Failed to load font '%s': %s — numbers will be hidden", font_path, TTF_GetError());
        return;
    }

    state->number_textures = (GLuint *)calloc((size_t)state->ball_count, sizeof(GLuint));
    if (!state->number_textures) return;

    for (int i = 0; i < state->ball_count; i++)
        state->number_textures[i] = make_number_texture(state->font, i + 1);

    log_info("Ball number textures created (%d balls)", state->ball_count);
}

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
        if (ball->picked)
        {
            /* Gold highlight for drawn ball, pulse scale slightly */
            float pulse = 1.0f + 0.12f * sinf(state->sim_time * 6.0f);
            glScalef(pulse, pulse, pulse);
            glColor3f(1.0f, 0.82f, 0.0f);
        }
        else
        {
            glColor3f(0.15f, 0.75f, 0.25f);
        }
        draw_sphere(BALL_RADIUS, 18, 12);
        glPopMatrix();
    }

    /* Draw number billboard on each ball (always faces camera) */
    if (state->number_textures)
    {
        glDisable(GL_LIGHTING);
        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor3f(1.0f, 1.0f, 1.0f);

        for (int i = 0; i < state->ball_count; i++)
        {
            if (!state->number_textures[i]) continue;
            const DrumBall *ball = &state->balls[i];

            glPushMatrix();
            glTranslatef(ball->x, ball->y, ball->z);

            /* Cancel accumulated rotation so the quad always faces the camera */
            float mv[16];
            glGetFloatv(GL_MODELVIEW_MATRIX, mv);
            mv[0] = 1.0f; mv[1] = 0.0f; mv[2] = 0.0f;
            mv[4] = 0.0f; mv[5] = 1.0f; mv[6] = 0.0f;
            mv[8] = 0.0f; mv[9] = 0.0f; mv[10] = 1.0f;
            glLoadMatrixf(mv);

            float s = BALL_RADIUS * 0.80f;
            glBindTexture(GL_TEXTURE_2D, state->number_textures[i]);
            glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 1.0f); glVertex3f(-s, -s, 0.5f);
                glTexCoord2f(1.0f, 1.0f); glVertex3f( s, -s, 0.5f);
                glTexCoord2f(1.0f, 0.0f); glVertex3f( s,  s, 0.5f);
                glTexCoord2f(0.0f, 0.0f); glVertex3f(-s,  s, 0.5f);
            glEnd();

            glPopMatrix();
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_LIGHTING);
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

    /* Result row: picked balls placed outside the drum in world space */
    if (state->result_ball_count > 0)
    {
        for (int s = 0; s < state->result_ball_count; s++)
        {
            const PickedBallDisplay *pb = &state->result_balls[s];

            glPushMatrix();
            glTranslatef(pb->x, pb->y, pb->z);

            /* Gold sphere */
            glColor3f(1.0f, 0.82f, 0.0f);
            draw_sphere(BALL_RADIUS, 18, 12);

            /* Number label */
            if (state->number_textures)
            {
                int idx = pb->ball_number - 1;
                if (idx >= 0 && idx < state->ball_count && state->number_textures[idx])
                {
                    glDisable(GL_LIGHTING);
                    glEnable(GL_TEXTURE_2D);
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    glColor3f(1.0f, 1.0f, 1.0f);

                    /* Billboard: read current MV, zero the rotation part */
                    float mv[16];
                    glGetFloatv(GL_MODELVIEW_MATRIX, mv);
                    mv[0] = 1.0f; mv[1] = 0.0f; mv[2] = 0.0f;
                    mv[4] = 0.0f; mv[5] = 1.0f; mv[6] = 0.0f;
                    mv[8] = 0.0f; mv[9] = 0.0f; mv[10] = 1.0f;
                    glLoadMatrixf(mv);

                    float s2 = BALL_RADIUS * 0.80f;
                    glBindTexture(GL_TEXTURE_2D, state->number_textures[idx]);
                    glBegin(GL_QUADS);
                        glTexCoord2f(0.0f, 1.0f); glVertex3f(-s2, -s2, 0.5f);
                        glTexCoord2f(1.0f, 1.0f); glVertex3f( s2, -s2, 0.5f);
                        glTexCoord2f(1.0f, 0.0f); glVertex3f( s2,  s2, 0.5f);
                        glTexCoord2f(0.0f, 0.0f); glVertex3f(-s2,  s2, 0.5f);
                    glEnd();

                    glBindTexture(GL_TEXTURE_2D, 0);
                    glDisable(GL_TEXTURE_2D);
                    glEnable(GL_LIGHTING);
                }
            }

            glPopMatrix();
        }
    }

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
    state->sim_time += delta_time;

    if (state->phase == DRUM_PHASE_FALLING)
    {
        /* FALLING PHASE: Balls drop under gravity, drum stationary */
        int settled_count = 0;

        for (int i = 0; i < state->ball_count; i++)
        {
            DrumBall *ball = &state->balls[i];

            if (ball->settled)
            {
                settled_count++;
                continue;
            }

            /* Apply gravity only */
            ball->vy -= BALL_GRAVITY * delta_time;
            ball->y += ball->vy * delta_time;

            /* Curved floor collision (ball inside spherical drum) */
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

        /* Ball-to-ball collisions during falling (prevent stacking) */
        {
            float collision_dist = 2.0f * BALL_RADIUS;
            for (int i = 0; i < state->ball_count; i++)
            {
                for (int j = i + 1; j < state->ball_count; j++)
                {
                    float dx = state->balls[j].x - state->balls[i].x;
                    float dy = state->balls[j].y - state->balls[i].y;
                    float dz = state->balls[j].z - state->balls[i].z;
                    float dist_sq = dx * dx + dy * dy + dz * dz;

                    if (dist_sq < collision_dist * collision_dist && dist_sq > 1e-6f)
                    {
                        float dist = sqrtf(dist_sq);
                        float nx = dx / dist;
                        float ny = dy / dist;
                        float nz = dz / dist;

                        /* Separate overlapping balls */
                        float overlap = collision_dist - dist;
                        state->balls[i].x -= nx * (overlap * 0.5f);
                        state->balls[i].y -= ny * (overlap * 0.5f);
                        state->balls[i].z -= nz * (overlap * 0.5f);
                        state->balls[j].x += nx * (overlap * 0.5f);
                        state->balls[j].y += ny * (overlap * 0.5f);
                        state->balls[j].z += nz * (overlap * 0.5f);

                        /* During falling, use VERY LOW restitution to dampen bounces */
                        float rel_vx = state->balls[j].vx - state->balls[i].vx;
                        float rel_vy = state->balls[j].vy - state->balls[i].vy;
                        float rel_vz = state->balls[j].vz - state->balls[i].vz;
                        float rel_vel_normal = rel_vx * nx + rel_vy * ny + rel_vz * nz;

                        if (rel_vel_normal < 0.0f)
                        {
                            /* FALLING phase uses 0.3 restitution (very inelastic) */
                            /* This prevents bouncing and ensures balls settle quickly */
                            float falling_restitution = 0.3f;
                            float impulse = -((1.0f + falling_restitution) * rel_vel_normal) * 0.5f;
                            state->balls[i].vx -= impulse * nx;
                            state->balls[i].vy -= impulse * ny;
                            state->balls[i].vz -= impulse * nz;
                            state->balls[j].vx += impulse * nx;
                            state->balls[j].vy += impulse * ny;
                            state->balls[j].vz += impulse * nz;
                        }
                    }
                }
            }
        }

        /* Keep all balls strictly inside drum during falling */
        {
            float drum_interior_radius = DRUM_RADIUS - BALL_RADIUS;
            float drum_interior_radius_sq = drum_interior_radius * drum_interior_radius;
            for (int i = 0; i < state->ball_count; i++)
            {
                DrumBall *ball = &state->balls[i];
                float dist_sq = ball->x * ball->x + ball->y * ball->y + ball->z * ball->z;
                if (dist_sq > drum_interior_radius_sq && dist_sq > 0.0001f)
                {
                    float dist = sqrtf(dist_sq);
                    float scale = drum_interior_radius / dist;
                    ball->x *= scale;
                    ball->y *= scale;
                    ball->z *= scale;
                    /* Also kill outward velocity component */
                    float vx_radial = ball->vx * (ball->x / dist);
                    float vy_radial = ball->vy * (ball->y / dist);
                    float vz_radial = ball->vz * (ball->z / dist);
                    ball->vx -= vx_radial * (ball->x / dist);
                    ball->vy -= vy_radial * (ball->y / dist);
                    ball->vz -= vz_radial * (ball->z / dist);
                }
            }
        }

        /* Transition conditions (lenient):
           - 50% of balls settled and remaining balls are slow, OR
           - Hard timeout: force transition after 3 seconds regardless */
        int force_timeout = (state->sim_time >= 3.0f);
        int min_settled = state->ball_count / 2;  /* 50% */

        float max_remaining_speed = 0.0f;
        for (int i = 0; i < state->ball_count; i++)
        {
            if (!state->balls[i].settled)
            {
                float speed_sq = state->balls[i].vx * state->balls[i].vx +
                                 state->balls[i].vy * state->balls[i].vy +
                                 state->balls[i].vz * state->balls[i].vz;
                float speed = sqrtf(speed_sq);
                if (speed > max_remaining_speed)
                    max_remaining_speed = speed;
            }
        }

        int calm_enough = (settled_count >= min_settled) && (max_remaining_speed < 50.0f);
        int can_transition = force_timeout || (calm_enough && state->sim_time >= 0.5f);
        
        if (can_transition)
        {
            state->phase = DRUM_PHASE_ROTATING;
            state->phase_timer = 0.0f;
            for (int i = 0; i < state->ball_count; i++)
            {
                state->balls[i].vx *= 0.25f;
                state->balls[i].vy *= 0.25f;
                state->balls[i].vz *= 0.25f;
            }
            if (state->use_gpu_compute)
                sync_cpu_balls_to_gpu(state);
            log_info("Balls settled (%.1f%%); starting drum rotation at %.2f s",
                     (100.0f * settled_count) / state->ball_count, state->sim_time);
        }
        else if ((int)(state->sim_time * 10) % 10 == 0)
        {
            log_info("FALLING phase: %d/%d balls settled", settled_count, state->ball_count);
        }

        return;
    }

    /* ------------------------------------------------------------------ */
    /* DRAW CYCLE STATE MACHINE                                             */
    /* ------------------------------------------------------------------ */

    if (state->phase == DRUM_PHASE_DRAW_COMPLETE)
        return;

    state->phase_timer += delta_time;

    /* ---- ROTATING: spin for spin_before_pick seconds, then decelerate -- */
    if (state->phase == DRUM_PHASE_ROTATING)
    {
        if (state->use_gpu_compute)
        {
            update_animation_gpu(state, delta_time);
        }
        /* else: fall through to CPU physics below */

        state->drum_rotation_z += DRUM_ROTATION_SPEED_DEG * delta_time;
        while (state->drum_rotation_z > 360.0f)
            state->drum_rotation_z -= 360.0f;

        if (state->phase_timer >= state->spin_before_pick)
        {
            /* Start deceleration */
            state->phase = DRUM_PHASE_STOPPING;
            state->phase_timer = 0.0f;
            state->stop_omega = DRUM_ROTATION_SPEED_DEG;
            log_info("Drum stopping for pick %d/%d", state->picks_done + 1, state->picks_total);
        }
        if (state->use_gpu_compute) return;
    }

    /* ---- STOPPING: decelerate drum to zero over ~1.5 s ------------------ */
    if (state->phase == DRUM_PHASE_STOPPING)
    {
        float decel_time = 1.5f;
        state->stop_omega = DRUM_ROTATION_SPEED_DEG * (1.0f - state->phase_timer / decel_time);
        if (state->stop_omega < 0.0f) state->stop_omega = 0.0f;

        state->drum_rotation_z += state->stop_omega * delta_time;
        while (state->drum_rotation_z > 360.0f)
            state->drum_rotation_z -= 360.0f;

        if (state->phase_timer >= decel_time)
        {
            /* Drum stopped: pick the next ball from the result */
            int pick_num = -1;
            if (state->picks_done < state->result.main_count)
                pick_num = state->result.main_numbers[state->picks_done];
            else
            {
                int extra_idx = state->picks_done - state->result.main_count;
                if (extra_idx < state->result.extra_count)
                    pick_num = state->result.extra_numbers[extra_idx];
            }

            /* Find the ball with that number and mark it picked */
            state->current_pick_idx = -1;
            for (int i = 0; i < state->ball_count; i++)
            {
                if (state->balls[i].ball_number == pick_num && !state->balls[i].picked)
                {
                    state->current_pick_idx = i;
                    state->balls[i].picked = 1;
                    /* Kill velocity — ball will settle with gravity like falling phase */
                    state->balls[i].vx = 0.0f;
                    state->balls[i].vy = 0.0f;
                    state->balls[i].vz = 0.0f;
                    break;
                }
            }

            /* Place the picked ball into the result row display.
               Row sits below the drum in world space.
               Slots spaced by 2.5 ball diameters, centred on X. */
            if (state->current_pick_idx >= 0 && pick_num > 0)
            {
                int slot = state->result_ball_count;
                int total = state->picks_total;
                float slot_spacing = BALL_RADIUS * 2.5f;
                float row_start_x = -((total - 1) * 0.5f) * slot_spacing;
                float target_x = row_start_x + slot * slot_spacing;
                float target_y = -(DRUM_RADIUS + BALL_RADIUS * 3.5f); /* below drum */
                float target_z = 0.0f;

                PickedBallDisplay *pb = &state->result_balls[slot];
                pb->ball_number = pick_num;
                /* Start at the ball's current drum position (drum-local → world offset) */
                pb->x = state->balls[state->current_pick_idx].x + DRUM_X;
                pb->y = state->balls[state->current_pick_idx].y + DRUM_Y;
                pb->z = state->balls[state->current_pick_idx].z;
                pb->tx = target_x;
                pb->ty = target_y;
                pb->tz = target_z;
                pb->vx = pb->vy = pb->vz = 0.0f;
                pb->arrived = 0;
                state->result_ball_count++;

                /* Hide the original ball inside drum (moved far away, it won't render) */
                state->balls[state->current_pick_idx].x = 9999.0f;
                state->balls[state->current_pick_idx].y = 9999.0f;
                state->balls[state->current_pick_idx].z = 9999.0f;
            }

            state->picks_done++;
            state->phase = DRUM_PHASE_PICK_PAUSE;
            state->phase_timer = 0.0f;
            log_info("Picked ball #%d (%d/%d)", pick_num, state->picks_done, state->picks_total);
        }
        if (state->use_gpu_compute) return;
    }

    /* ---- PICK_PAUSE: balls re-settle inside drum; picked ball flies to row */
    if (state->phase == DRUM_PHASE_PICK_PAUSE)
    {
        /* Animate the flying ball toward its result-row slot */
        for (int s = 0; s < state->result_ball_count; s++)
        {
            PickedBallDisplay *pb = &state->result_balls[s];
            if (pb->arrived) continue;

            float dx = pb->tx - pb->x;
            float dy = pb->ty - pb->y;
            float dz = pb->tz - pb->z;
            float dist = sqrtf(dx*dx + dy*dy + dz*dz);
            if (dist < 2.0f)
            {
                pb->x = pb->tx; pb->y = pb->ty; pb->z = pb->tz;
                pb->vx = pb->vy = pb->vz = 0.0f;
                pb->arrived = 1;
            }
            else
            {
                /* Lerp toward target at constant speed */
                float speed = 350.0f;
                float step = speed * delta_time;
                if (step > dist) step = dist;
                pb->x += (dx / dist) * step;
                pb->y += (dy / dist) * step;
                pb->z += (dz / dist) * step;
            }
        }

        /* Balls inside drum fall and re-settle (same as FALLING phase logic) */
        for (int i = 0; i < state->ball_count; i++)
        {
            DrumBall *ball = &state->balls[i];
            if (ball->picked) continue; /* already removed */

            ball->vy -= BALL_GRAVITY * delta_time;
            ball->y  += ball->vy * delta_time;

            float radial_sq = ball->x * ball->x + ball->z * ball->z;
            float inside    = (DRUM_RADIUS - BALL_RADIUS) * (DRUM_RADIUS - BALL_RADIUS) - radial_sq;
            float floor_y   = (inside <= 0.0f) ? (-DRUM_RADIUS + BALL_RADIUS) : -sqrtf(inside);

            if (ball->y <= floor_y)
            {
                ball->y = floor_y;
                if (fabsf(ball->vy) < BALL_SETTLE_SPEED)
                    ball->vy = 0.0f;
                else
                    ball->vy = -ball->vy * BALL_BOUNCE_DAMPING;
            }
        }

        /* Light ball-to-ball separation so they don't stack oddly */
        {
            float cd = 2.0f * BALL_RADIUS;
            for (int i = 0; i < state->ball_count; i++)
            {
                if (state->balls[i].picked) continue;
                for (int j = i + 1; j < state->ball_count; j++)
                {
                    if (state->balls[j].picked) continue;
                    float dx = state->balls[j].x - state->balls[i].x;
                    float dy = state->balls[j].y - state->balls[i].y;
                    float dz = state->balls[j].z - state->balls[i].z;
                    float d2 = dx*dx + dy*dy + dz*dz;
                    if (d2 < cd*cd && d2 > 1e-6f)
                    {
                        float d = sqrtf(d2);
                        float overlap = (cd - d) * 0.5f;
                        float nx = dx/d, ny = dy/d, nz = dz/d;
                        state->balls[i].x -= nx * overlap;
                        state->balls[i].y -= ny * overlap;
                        state->balls[i].z -= nz * overlap;
                        state->balls[j].x += nx * overlap;
                        state->balls[j].y += ny * overlap;
                        state->balls[j].z += nz * overlap;
                    }
                }
            }
        }

        float pause_time = 2.5f;
        if (state->phase_timer >= pause_time)
        {
            if (state->picks_done >= state->picks_total)
            {
                state->phase = DRUM_PHASE_DRAW_COMPLETE;
                state->animation_complete = 1;
                log_info("Draw complete!");
            }
            else
            {
                state->phase = DRUM_PHASE_ROTATING;
                state->phase_timer = 0.0f;
                state->spin_before_pick = 3.0f + frand_range(0.0f, 2.0f);
                log_info("Resuming drum rotation (next pick in %.1f s)", state->spin_before_pick);
            }
        }
        if (state->use_gpu_compute) return;
    }

    /* CPU physics (shared by ROTATING / STOPPING) */
    if (state->phase == DRUM_PHASE_DRAW_COMPLETE || state->phase == DRUM_PHASE_PICK_PAUSE)
        return;

    /* ROTATING PHASE: gravity + shell contact drive (around Z) */
    {
    float omega_rad_per_sec = DRUM_ROTATION_SPEED_DEG * 3.14159265f / 180.0f;
    for (int i = 0; i < state->ball_count; i++)
    {
        DrumBall *ball = &state->balls[i];
        float drum_interior_radius = DRUM_RADIUS - BALL_RADIUS;
        float theta = state->drum_rotation_z * 3.14159265f / 180.0f;

        /* Gravity in rotating frame: world-down direction rotates through local axes */
        float gx = -BALL_GRAVITY * sinf(theta);
        float gy = -BALL_GRAVITY * cosf(theta);

        ball->vx += gx * delta_time;
        ball->vy += gy * delta_time;

        /* Air damping only (very light), motion stays free inside the sphere */
        ball->vx *= BALL_AIR_DAMPING;
        ball->vy *= BALL_AIR_DAMPING;
        ball->vz *= BALL_AIR_DAMPING;

        /* Update positions */
        ball->x += ball->vx * delta_time;
        ball->y += ball->vy * delta_time;
        ball->z += ball->vz * delta_time;

        /* Collision detection with drum interior */
        float radial_sq = ball->x * ball->x + ball->z * ball->z;
        float dist_from_center_sq = radial_sq + ball->y * ball->y;

        if (dist_from_center_sq > drum_interior_radius * drum_interior_radius)
        {
            /* Ball is outside; reflect it back inside */
            float dist_from_center = sqrtf(dist_from_center_sq);
            
            /* Normal vector pointing outward */
            float nx = ball->x / dist_from_center;
            float ny = ball->y / dist_from_center;
            float nz = ball->z / dist_from_center;

            /* Move ball back to interior surface */
            float overlap = dist_from_center - drum_interior_radius;
            ball->x -= nx * overlap;
            ball->y -= ny * overlap;
            ball->z -= nz * overlap;

            /* Reflect only incoming normal component */
            float v_dot_n = ball->vx * nx + ball->vy * ny + ball->vz * nz;
            if (v_dot_n > 0.0f)
            {
                float restitution = 0.55f;
                ball->vx -= (1.0f + restitution) * v_dot_n * nx;
                ball->vy -= (1.0f + restitution) * v_dot_n * ny;
                ball->vz -= (1.0f + restitution) * v_dot_n * nz;
            }

            /* Contact grip in drum direction (rotation around Z => XY tangent) */
            {
                float radial = sqrtf(ball->x * ball->x + ball->y * ball->y);
                if (radial > 0.1f)
                {
                    float tx = -ball->y / radial;
                    float ty = ball->x / radial;
                    float target_tangential = omega_rad_per_sec * radial;
                    float current_tangential = ball->vx * tx + ball->vy * ty;
                    float min_follow = target_tangential * 0.35f;
                    if (current_tangential < min_follow)
                    {
                        float grip = BALL_CONTACT_FRICTION * 0.35f;
                        float delta_t = (min_follow - current_tangential) * grip * delta_time;
                        ball->vx += tx * delta_t;
                        ball->vy += ty * delta_t;
                    }
                }
            }
        }
    }
    } /* end omega_rad_per_sec scope */

    /* Ball-to-ball collisions using uniform grid broad phase */
    {
        float collision_dist = 2.0f * BALL_RADIUS;

        float drum_interior_radius = DRUM_RADIUS - BALL_RADIUS;
        float grid_min = -drum_interior_radius;
        int grid_dim = (int)ceilf((2.0f * drum_interior_radius) / COLLISION_CELL_SIZE);
        if (grid_dim < 1)
            grid_dim = 1;
        if (grid_dim > COLLISION_GRID_MAX_DIM)
            grid_dim = COLLISION_GRID_MAX_DIM;

        int cell_count = grid_dim * grid_dim * grid_dim;
        int *cell_heads = (int *)malloc((size_t)cell_count * sizeof(int));
        int *next_in_cell = (int *)malloc((size_t)state->ball_count * sizeof(int));
        int *cell_x = (int *)malloc((size_t)state->ball_count * sizeof(int));
        int *cell_y = (int *)malloc((size_t)state->ball_count * sizeof(int));
        int *cell_z = (int *)malloc((size_t)state->ball_count * sizeof(int));

        if (!cell_heads || !next_in_cell || !cell_x || !cell_y || !cell_z)
        {
            /* Fallback to pairwise checks if temp buffers cannot be allocated */
            for (int i = 0; i < state->ball_count; i++)
            {
                for (int j = i + 1; j < state->ball_count; j++)
                {
                    resolve_ball_collision(&state->balls[i], &state->balls[j], collision_dist);
                }
            }
        }
        else
        {
            for (int c = 0; c < cell_count; c++)
                cell_heads[c] = -1;

            /* Insert balls into grid cells */
            for (int i = 0; i < state->ball_count; i++)
            {
                DrumBall *ball = &state->balls[i];

                int cx = (int)floorf((ball->x - grid_min) / COLLISION_CELL_SIZE);
                int cy = (int)floorf((ball->y - grid_min) / COLLISION_CELL_SIZE);
                int cz = (int)floorf((ball->z - grid_min) / COLLISION_CELL_SIZE);

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

                int cell_id = (cz * grid_dim + cy) * grid_dim + cx;
                cell_x[i] = cx;
                cell_y[i] = cy;
                cell_z[i] = cz;
                next_in_cell[i] = cell_heads[cell_id];
                cell_heads[cell_id] = i;
            }

            /* Resolve collisions only against balls in neighboring cells */
            for (int i = 0; i < state->ball_count; i++)
            {
                int cx = cell_x[i];
                int cy = cell_y[i];
                int cz = cell_z[i];

                int nx0 = (cx > 0) ? cx - 1 : 0;
                int ny0 = (cy > 0) ? cy - 1 : 0;
                int nz0 = (cz > 0) ? cz - 1 : 0;
                int nx1 = (cx + 1 < grid_dim) ? cx + 1 : grid_dim - 1;
                int ny1 = (cy + 1 < grid_dim) ? cy + 1 : grid_dim - 1;
                int nz1 = (cz + 1 < grid_dim) ? cz + 1 : grid_dim - 1;

                for (int nz = nz0; nz <= nz1; nz++)
                {
                    for (int ny = ny0; ny <= ny1; ny++)
                    {
                        for (int nx = nx0; nx <= nx1; nx++)
                        {
                            int neighbor_id = (nz * grid_dim + ny) * grid_dim + nx;
                            for (int j = cell_heads[neighbor_id]; j != -1; j = next_in_cell[j])
                            {
                                if (j <= i)
                                    continue;
                                resolve_ball_collision(&state->balls[i], &state->balls[j], collision_dist);
                            }
                        }
                    }
                }
            }
        }

        free(cell_heads);
        free(next_in_cell);
        free(cell_x);
        free(cell_y);
        free(cell_z);
    }

    /* Final safety pass: keep every ball strictly inside cage after pair separation */
    {
        float drum_interior_radius = DRUM_RADIUS - BALL_RADIUS;
        float drum_interior_radius_sq = drum_interior_radius * drum_interior_radius;
        for (int i = 0; i < state->ball_count; i++)
        {
            DrumBall *ball = &state->balls[i];
            float dist_sq = ball->x * ball->x + ball->y * ball->y + ball->z * ball->z;
            if (dist_sq > drum_interior_radius_sq && dist_sq > 0.0001f)
            {
                float dist = sqrtf(dist_sq);
                float scale = drum_interior_radius / dist;
                ball->x *= scale;
                ball->y *= scale;
                ball->z *= scale;
            }
        }
    }

    /* Rotate drum: speed depends on current phase */
    /* (STOPPING uses stop_omega set above; ROTATING uses full speed) */
    float effective_omega = (state->phase == DRUM_PHASE_STOPPING)
                            ? state->stop_omega
                            : DRUM_ROTATION_SPEED_DEG;
    state->drum_rotation_z += effective_omega * delta_time;

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

    SDL_GL_SetSwapInterval(1);  /* Enable vsync */

    setup_opengl();
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

    /* Generate the lottery draw (we won't display numbers yet, just show drums) */
    generate_draw(info->main_count, info->main_min, info->main_max, info->extra_count,
                  info->extra_min, info->extra_max, &state->result, on_draw_event);

    log_info("Displaying %s - %d balls (from game rules) start inside, fall first, then drum rotates",
             game_name, state->ball_count);
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

    destroy_gpu_compute(state);

    SDL_GL_DeleteContext(state->gl_context);
    SDL_DestroyWindow(state->window);
    TTF_Quit();
    SDL_Quit();
    gui_state_destroy(state);
}
