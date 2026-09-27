/*
 * NEON DESCENT - a top-down vector mine shooter
 * Shared types, math helpers and module interfaces.
 */
#pragma once

#include <SDL3/SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GAME_TITLE "NEON DESCENT"
#define VIRT_H 720.0f
#define TILE 48.0f
#define MAP_MAX_W 96
#define MAP_MAX_H 72
#define PI 3.14159265358979f
#define TAU 6.28318530717958f
#define SIM_DT (1.0f / 120.0f)

/* ------------------------------------------------------------------ math */

typedef struct { float x, y; } V2;
typedef struct { float r, g, b, a; } Col;

static inline V2 v2(float x, float y) { V2 r = {x, y}; return r; }
static inline V2 v2add(V2 a, V2 b) { return v2(a.x + b.x, a.y + b.y); }
static inline V2 v2sub(V2 a, V2 b) { return v2(a.x - b.x, a.y - b.y); }
static inline V2 v2scale(V2 a, float s) { return v2(a.x * s, a.y * s); }
static inline V2 v2mad(V2 a, V2 b, float s) { return v2(a.x + b.x * s, a.y + b.y * s); }
static inline float v2dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
static inline float v2cross(V2 a, V2 b) { return a.x * b.y - a.y * b.x; }
static inline float v2len2(V2 a) { return a.x * a.x + a.y * a.y; }
static inline float v2len(V2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
static inline float v2dist(V2 a, V2 b) { return v2len(v2sub(a, b)); }
static inline float v2dist2(V2 a, V2 b) { return v2len2(v2sub(a, b)); }
static inline V2 v2perp(V2 a) { return v2(-a.y, a.x); }
static inline V2 v2lerp(V2 a, V2 b, float t) { return v2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }
static inline V2 v2norm(V2 a) {
    float l = v2len(a);
    return l > 1e-6f ? v2scale(a, 1.0f / l) : v2(0, 0);
}
static inline V2 v2fromang(float a) { return v2(cosf(a), sinf(a)); }
static inline float v2ang(V2 a) { return atan2f(a.y, a.x); }
static inline V2 v2rot(V2 a, float ang) {
    float c = cosf(ang), s = sinf(ang);
    return v2(a.x * c - a.y * s, a.x * s + a.y * c);
}
static inline V2 v2clamplen(V2 a, float m) {
    float l = v2len(a);
    return l > m ? v2scale(a, m / l) : a;
}

static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline int clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }
static inline float smooth01(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }
static inline float wrap_angle(float a) {
    while (a > PI) a -= TAU;
    while (a < -PI) a += TAU;
    return a;
}
static inline float approach_angle(float cur, float target, float maxstep) {
    float d = wrap_angle(target - cur);
    if (d > maxstep) d = maxstep;
    if (d < -maxstep) d = -maxstep;
    return wrap_angle(cur + d);
}
/* frame-rate independent exponential smoothing factor */
static inline float damp_factor(float rate, float dt) { return 1.0f - expf(-rate * dt); }

static inline Col rgba(float r, float g, float b, float a) { Col c = {r, g, b, a}; return c; }
static inline Col col_a(Col c, float a) { c.a *= a; return c; }
static inline Col col_mul(Col c, float s) { return rgba(c.r * s, c.g * s, c.b * s, c.a); }
static inline Col col_lerp(Col a, Col b, float t) {
    return rgba(lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t), lerpf(a.a, b.a, t));
}
static inline Col col_white(Col c, float t) { return col_lerp(c, rgba(1, 1, 1, c.a), t); }

#define C_WHITE rgba(1, 1, 1, 1)
#define C_CYAN rgba(0.25f, 0.9f, 1.0f, 1)
#define C_BLUE rgba(0.25f, 0.5f, 1.0f, 1)
#define C_YELLOW rgba(1.0f, 0.85f, 0.2f, 1)
#define C_ORANGE rgba(1.0f, 0.55f, 0.15f, 1)
#define C_RED rgba(1.0f, 0.22f, 0.2f, 1)
#define C_GREEN rgba(0.3f, 1.0f, 0.45f, 1)
#define C_MAGENTA rgba(1.0f, 0.3f, 0.85f, 1)
#define C_PURPLE rgba(0.65f, 0.35f, 1.0f, 1)
#define C_GREY rgba(0.55f, 0.6f, 0.7f, 1)

/* ------------------------------------------------------------------ rng */
uint32_t rng_next(void);
void rng_seed(uint32_t s);
float frand(void);                  /* [0,1) */
float frandr(float a, float b);     /* [a,b) */
int irand(int n);                   /* [0,n) */

/* ------------------------------------------------------------------ shapes */
typedef struct { int n; bool closed; V2 p[18]; } Stroke;
typedef struct { int ns; Stroke s[6]; } Shape;

/* ------------------------------------------------------------------ render.c */
enum { AL_LEFT = 0, AL_CENTER = 1, AL_RIGHT = 2 };

typedef struct { V2 pos; float zoom; V2 shake; } Camera;

extern SDL_Window *g_window;
extern SDL_Renderer *g_ren;
extern int g_pix_w, g_pix_h;
extern float g_scale;   /* pixels per virtual unit */
extern float g_virt_w;  /* virtual width, height is always VIRT_H */
extern Camera g_cam;
extern const char *g_screenshot_path;

bool render_init(void);
void render_shutdown(void);
void render_check_resize(void);
void render_begin(void);
void render_end(bool bloom, float bloom_strength);
void r_flush(void);

V2 w2v(V2 w);
V2 v2w(V2 v);
bool rw_visible(V2 p, float r);

void r_line(V2 a, V2 b, float w, Col c);
void r_polyline(const V2 *p, int n, bool closed, float w, Col c);
void r_polyline_cols(const V2 *p, const Col *cols, int n, bool closed, float w);
void r_glow(V2 p, float radius, Col c);
void r_circle(V2 p, float radius, float w, Col c, int segs);
void r_arc(V2 p, float radius, float a0, float a1, float w, Col c, int segs);
void r_fill_rect(float x, float y, float w, float h, Col c);
void r_fill_tri(V2 a, V2 b, V2 c, Col col);
void r_fill_quad(V2 a, V2 b, V2 c, V2 d, Col col);
void r_add_rect(float x, float y, float w, float h, Col c);
void r_add_quad(V2 a, V2 b, V2 c, V2 d, Col col);
void r_rock_quad(V2 a, V2 b, V2 c, V2 d, V2 ua, V2 ub, V2 uc, V2 ud, Col col);
void r_rock_tri(V2 a, V2 b, V2 c, V2 ua, V2 ub, V2 uc, Col col);
void r_frame(float x, float y, float w, float h, float lw, Col c);
void r_panel(float x, float y, float w, float h, Col edge, float alpha);

void rw_line(V2 a, V2 b, float w, Col c);
void rw_polyline(const V2 *p, int n, bool closed, float w, Col c);
void rw_glow(V2 p, float radius, Col c);
void rw_circle(V2 p, float radius, float w, Col c, int segs);
void rw_shape(const Shape *s, V2 pos, float ang, float scale, float w, Col c);
void r_shape(const Shape *s, V2 pos, float ang, float scale, float w, Col c);

float text_width(const char *s, float size);
void r_text(const char *s, float x, float y, float size, Col c, int align);
void r_textf(float x, float y, float size, Col c, int align, const char *fmt, ...);
void r_text_glow(const char *s, float x, float y, float size, Col c, int align);

/* ------------------------------------------------------------------ audio.c */
enum {
    SND_LASER, SND_LASER2, SND_VULCAN, SND_SPREAD, SND_PLASMA, SND_FUSION_CHARGE, SND_FUSION,
    SND_MISSILE, SND_MEGA, SND_PROX, SND_EXPL_S, SND_EXPL_M, SND_EXPL_L, SND_HIT, SND_WALL,
    SND_PLAYER_HIT, SND_PICKUP, SND_POWERUP, SND_KEY, SND_HOSTAGE, SND_DOOR, SND_LOCKED,
    SND_ALARM, SND_ENERGY, SND_MENU_MOVE, SND_MENU_SEL, SND_EN_SHOT, SND_EN_MISSILE,
    SND_SCREECH, SND_TALLY, SND_EXTRALIFE, SND_REACTOR_HIT, SND_BOSS, SND_TELEPORT,
    SND_MATCEN, SND_BEEP, SND_BREAK, SND_BURNER, SND_CLOAK, SND_NOAMMO, SND_COUNT
};
enum { SONG_NONE = -1, SONG_MENU, SONG_L1, SONG_L2, SONG_L3, SONG_BOSS, SONG_ESCAPE, SONG_BRIEF, SONG_GAMEOVER, SONG_VICTORY, SONG_COUNT };

void audio_init(void);
void audio_shutdown(void);
void snd_play(int id, float vol, float pitch);
void snd_play_at(int id, V2 pos, float vol, float pitch);
int snd_loop(int id, float vol, float pitch);
void snd_loop_set(int handle, float vol, float pitch);
void snd_stop(int handle);
void audio_set_listener(V2 p);
void music_play(int song);
void audio_set_volumes(float music, float sfx);
void audio_pause_loops(bool paused);

/* ------------------------------------------------------------------ level.c */
enum { T_EMPTY = 0, T_SOLID, T_TRI_TL, T_TRI_TR, T_TRI_BL, T_TRI_BR, T_BREAK };
#define TF_ENERGY 1
#define TF_EXIT 2
#define TF_DOOR 4

enum { LOCK_NONE = 0, LOCK_BLUE, LOCK_YELLOW, LOCK_RED, LOCK_EXIT };

typedef struct {
    V2 a, b, n;
    int tile; /* tile index for breakable walls, -1 otherwise */
} Seg;

typedef struct {
    int x0, y0, x1, y1; /* tile range, inclusive */
    bool horiz;         /* slab runs along x (blocks vertical travel) */
    int lock;
    float open;         /* 0 closed .. 1 open */
    float hold;         /* time to stay open */
    float msg_cd;
    bool opening;
} Door;

#define MAX_SEGS 6000
#define MAX_DOORS 48
#define MAX_CPTS 12000
#define MAX_CONTOURS 1200

typedef struct {
    int w, h;
    uint8_t tile[MAP_MAX_H][MAP_MAX_W];
    uint8_t flags[MAP_MAX_H][MAP_MAX_W];
    int8_t door_id[MAP_MAX_H][MAP_MAX_W];
    float break_hp[MAP_MAX_H][MAP_MAX_W];
    uint8_t explored[MAP_MAX_H][MAP_MAX_W];
    uint16_t flow[MAP_MAX_H][MAP_MAX_W];
    uint16_t exitflow[MAP_MAX_H][MAP_MAX_W];

    Seg segs[MAX_SEGS];
    int nsegs;
    int cell_start[MAP_MAX_H * MAP_MAX_W];
    int cell_count[MAP_MAX_H * MAP_MAX_W];
    int *cell_refs;
    int ncell_refs;

    V2 cpts[MAX_CPTS];
    int ncpts;
    int cstart[MAX_CONTOURS], clen[MAX_CONTOURS];
    bool cclosed[MAX_CONTOURS];
    int ncontours;
    int break_segs[512];
    int nbreak_segs;

    Door doors[MAX_DOORS];
    int ndoors;
} Level;

extern Level L;

typedef struct {
    float t;
    V2 n;
    int seg;
    int door;
    V2 p;
} RayHit;

void level_build_from_ascii(const char **rows, int nrows);
void level_rebuild_geometry(void);
bool tile_blocks(int tx, int ty);
bool point_in_rock(V2 p);
bool circle_collide(V2 *pos, float r, V2 *vel, float bounce);
bool raycast(V2 a, V2 b, RayHit *hit);
bool los(V2 a, V2 b);
bool los_wide(V2 a, V2 b, float r);
V2 safe_muzzle(V2 from, V2 to);
void flow_compute(uint16_t field[MAP_MAX_H][MAP_MAX_W], int sx, int sy, bool exit_mode);
void flow_compute_multi(uint16_t field[MAP_MAX_H][MAP_MAX_W], const int *xs, const int *ys, int n, bool exit_mode);
V2 flow_dir(uint16_t field[MAP_MAX_H][MAP_MAX_W], V2 pos, bool descend);
int flow_path(uint16_t field[MAP_MAX_H][MAP_MAX_W], V2 pos, V2 *out, int max);
void explore_update(V2 p, float radius);
V2 door_center(const Door *d);
bool door_rects(const Door *d, SDL_FRect *r0, SDL_FRect *r1);
float door_dist(const Door *d, V2 p);
static inline int tx_of(float x) { return (int)floorf(x / TILE); }
static inline V2 tile_center(int tx, int ty) { return v2((tx + 0.5f) * TILE, (ty + 0.5f) * TILE); }
bool tile_in(int tx, int ty);

/* ------------------------------------------------------------------ fx.c */
void grid_init(float world_w, float world_h, float spacing);
void grid_free(void);
void grid_impulse(V2 p, float radius, float strength);
void grid_update(float dt);
void grid_draw(Col base, float brightness);

void fx_clear(void);
void fx_update(float dt);
void fx_draw(void);
void fx_spark(V2 p, V2 v, Col c, float life, float width);
void fx_glow(V2 p, V2 v, Col c, float life, float size);
void fx_debris(V2 p, V2 v, float ang, float angv, float len, Col c, float life);
void fx_ring(V2 p, float r0, float r1, Col c, float life, float width);
void fx_light(V2 p, float radius, Col c, float life);
void fx_burst(V2 p, int n, Col c, float speed, float life, float width);
void fx_explosion(V2 p, float size, Col c);
void fx_shape_debris(const Shape *s, V2 pos, float ang, float scale, V2 vel, Col c);
void fx_popup(V2 p, const char *text, Col c, float size);
void fx_draw_popups(void);
void shake_add(float trauma);
extern float g_trauma;
extern bool g_fx_collide;

/* ------------------------------------------------------------------ game.c */
enum { RB_DRONE, RB_LIFTER, RB_TURRET, RB_HULK, RB_SPIDER, RB_BABY, RB_GOPHER, RB_DRILLER, RB_SUPERHULK, RB_CLOAKER, RB_BOSS, RB_COUNT };
enum { PW_LASER, PW_VULCAN, PW_SPREAD, PW_PLASMA, PW_FUSION, PW_COUNT };
enum { SW_CONCUSSION, SW_HOMING, SW_PROX, SW_SMART, SW_MEGA, SW_COUNT };

typedef struct {
    V2 move;          /* -1..1 each */
    V2 aim_world;     /* mouse aim point in world space */
    V2 stick_aim;     /* gamepad aim */
    bool use_stick;
    bool fire1, fire2, burner;
    bool fire2_pressed;
    int select;       /* -1 none, 0-4 primary, 5-9 secondary */
    int cycle_p, cycle_s;
    bool drop_bomb;
    bool mouse_active;
} Input;
extern Input g_in;

typedef struct {
    float music_vol, sfx_vol;
    bool fullscreen;
    int shake;          /* 0 off, 1 low, 2 full */
    int move_mode;      /* 0 absolute (twin-stick), 1 ship-relative */
    bool bloom;
    bool autoswitch;
} Config;
extern Config g_cfg;

extern const char *DIFF_NAMES[5];
extern const char *PRIMARY_NAMES[PW_COUNT];
extern const char *SECONDARY_NAMES[SW_COUNT];

typedef enum {
    GR_NONE = 0,
    GR_LEVEL_DONE,   /* escaped: go to tally */
    GR_ESCAPE_FAIL,  /* caught in the blast */
    GR_GAME_OVER,
} GameResult;

typedef struct {
    int level;
    int difficulty;
    int lives;
    int score;
    int next_life;
    int level_score_start;
    float level_time;
    int hostages_total, hostages_saved, hostages_onboard, hostages_lost;
    int robots_killed, robots_total;
    bool reactor_dead;
    bool boss_level;
    float countdown, countdown_max;
    bool escaping;
    float escape_t;
    bool failing;
    float fail_t;
    int chain;
    float chain_t;
    bool automap;
    float automap_zoom;
    V2 automap_pan;
    GameResult result;
    /* tally, computed on escape */
    int bonus_shield, bonus_energy, bonus_hostage, bonus_full, bonus_skill;
} GameState;
extern GameState G;

void game_init(void);
void game_new(int difficulty);
void game_start_level(int level, bool fresh_ship);
void game_update(float dt);
void game_draw(void);
void game_end_level(void);
void game_shutdown_level(void);
void game_add_score(int pts);
int game_level_count(void);
const char *game_level_name(int i);
const char *game_level_subtitle(int i);
const char *game_level_briefing(int i);
void game_draw_robot_preview(int type, V2 pos, float ang, float scale);
int game_level_threats(int i, int *types, int max);
const char *game_robot_name(int type);
void game_draw_ship_icon(V2 pos, float ang, float scale, Col c);
void game_hud_draw(void);
void game_automap_draw(void);
void game_audio_pause(bool p);

/* ------------------------------------------------------------------ hud.c */
void hud_msg(const char *text, Col c);
void hud_msg_clear(void);
void hud_update(float dt);
void hud_draw_messages(void);

/* ------------------------------------------------------------------ levels.c */
typedef struct {
    const char *name;
    const char *subtitle;
    const char **map;
    int rows;
    Col wall, grid, rock, accent;
    int song;
    float countdown_bonus;
    const char *briefing;
    int threats[6];
    int nthreats;
    int matcen_types[4];
    int nmatcen;
} LevelDef;
extern const LevelDef LEVELS[3];
#define NUM_LEVELS 3

/* ------------------------------------------------------------------ main.c */
extern float g_time;       /* real time seconds */
extern bool g_gamepad_connected;
void app_request_state_game_over(void);
