/*
 * BALLAST - a top-down vector mine shooter
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

#define GAME_TITLE "BALLAST"
#define VIRT_H 720.0f
#define TILE 48.0f
#define MAP_MAX_W 152
#define MAP_MAX_H 96
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
static inline float ease_out_cubic(float t) { t = 1 - clampf(t, 0, 1); return 1 - t * t * t; }
/* overshoots a little past 1 before settling: things that pop in */
static inline float ease_out_back(float t) { t = clampf(t, 0, 1) - 1; return 1 + t * t * (2.7f * t + 1.7f); }
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

/* seeded streams for everything a run seed must reproduce: maps, the sector chart, drafts */
typedef struct { uint32_t s; } Rng;
static inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
static inline Rng rng_make(uint32_t seed, uint32_t stream) { Rng r = {hash32(seed ^ hash32(stream + 0x51ED27u)) | 1u}; return r; }
static inline uint32_t rng_u32(Rng *r) {
    uint32_t x = r->s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return r->s = x;
}
static inline int rng_int(Rng *r, int n) { return n > 0 ? (int)(rng_u32(r) % (uint32_t)n) : 0; }
static inline float rng_f(Rng *r) { return (rng_u32(r) >> 8) * (1.0f / 16777216.0f); }

/* ------------------------------------------------------------------ shapes */
typedef struct { int n; bool closed; V2 p[18]; } Stroke;
typedef struct { int ns; Stroke s[6]; } Shape;

/* ------------------------------------------------------------------ render.c */
enum { AL_LEFT = 0, AL_CENTER = 1, AL_RIGHT = 2 };

typedef struct {
    V2 pos;
    float zoom;
    V2 shake;
    V2 lean;   /* the eye off straight overhead, trailing the camera's motion: the walls lean, space below shifts */
} Camera;

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
void r_mesh(const V2 *pts, const Col *cols, int npts, const int *idx, int nidx);
void r_add_rect(float x, float y, float w, float h, Col c);
void r_add_quad(V2 a, V2 b, V2 c, V2 d, Col col);
void r_rock_quad(V2 a, V2 b, V2 c, V2 d, V2 ua, V2 ub, V2 uc, V2 ud, Col col);
void r_rock_tri(V2 a, V2 b, V2 c, V2 ua, V2 ub, V2 uc, Col col);
void r_add_hband(float x, float y, float w, float h, Col c); /* additive, fading out to both ends */
void r_frame(float x, float y, float w, float h, float lw, Col c);
void r_panel(float x, float y, float w, float h, Col edge, float alpha);
/* everything drawn until the pop is scaled about org, then moved by off and faded by alpha;
 * pushes nest, so a whole group of UI can slide, pop or power on at once */
void r_push(V2 off, V2 org, float sx, float sy, float alpha);
void r_pop(void);

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
void r_text_glowk(const char *s, float x, float y, float size, Col c, int align, float glow); /* the halo scaled by glow */
/* draws the glyphs on stroke by stroke as k goes 0..1 */
void r_text_reveal(const char *s, float x, float y, float size, Col c, int align, float k, bool glow);
/* one glyph's strokes on the font's 4x6 grid; glyphs advance FONT_ADV grid units */
#define FONT_ADV 5.6f
int font_glyph(int ch, const V2 **pts, const int **start, const int **len);

/* ------------------------------------------------------------------ ui.c */
/* eased values kept by id between frames, for hovers, cursors and presses; ui_reset forgets them */
static inline uint32_t ui_id(uint32_t kind, uint32_t i) { return kind * 0x9E3779B1u + i * 0x85EBCA77u + 1; }
void ui_reset(void);
float ui_ease(uint32_t id, float target, float rate);          /* approaches the target; the first call snaps */
float ui_spring(uint32_t id, float target, float k, float damp); /* a spring after the target, it can overshoot */
void ui_snap(uint32_t id, float v);
void ui_press(uint32_t id);
float ui_pressed(uint32_t id);   /* 1 at the press, falling to 0 in under half a second */
float ui_shake(uint32_t id);     /* a sideways wobble after a press: something refused */

/* ------------------------------------------------------------------ wire.c */
/* wireframe 3D for the holo briefing and the cinematics: Y is up, the game's 2D plane is XZ */
typedef struct { float x, y, z; } V3;
static inline V3 v3(float x, float y, float z) { V3 r = {x, y, z}; return r; }
static inline V3 v3add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline V3 v3sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline V3 v3scale(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline V3 v3mad(V3 a, V3 b, float s) { return v3(a.x + b.x * s, a.y + b.y * s, a.z + b.z * s); }
static inline float v3dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 v3cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline float v3len(V3 a) { return sqrtf(v3dot(a, a)); }
static inline float v3dist(V3 a, V3 b) { return v3len(v3sub(a, b)); }
static inline V3 v3norm(V3 a) {
    float l = v3len(a);
    return l > 1e-6f ? v3scale(a, 1.0f / l) : v3(0, 0, 0);
}
static inline V3 v3lerp(V3 a, V3 b, float t) { return v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t); }
/* a point of the game plane */
static inline V3 v3flat(V2 p, float y) { return v3(p.x, y, p.y); }

/* rotation matrix as its columns: where the model's x (forward), y (up) and z axes point */
typedef struct { V3 x, y, z; } M3;
static inline V3 m3mul(M3 m, V3 v) { return v3add(v3add(v3scale(m.x, v.x), v3scale(m.y, v.y)), v3scale(m.z, v.z)); }
/* heading as a game angle (0 = +x, PI/2 = +z), then pitch the nose up and roll into a bank */
M3 m3_euler(float heading, float pitch, float roll);
M3 m3_axis(V3 axis, float ang);
M3 m3_mulm(M3 a, M3 b);

typedef struct {
    V3 eye, f, r, u;      /* camera position and basis */
    float focal;          /* virtual pixels per unit at distance 1 */
    V2 center;
    SDL_FRect clip;       /* lines are clipped to this rect, w <= 0: no clipping */
    float fog_near, fog_far; /* lines fade out with depth between these */
    float alpha;          /* multiplies every colour */
    float wdepth;         /* > 0: line widths scale with wdepth / depth */
    float glitch;         /* 0..1 horizontal tearing */
    uint32_t glitch_seed;
} WCam;
extern WCam g_wc;

void wc_look(V3 eye, V3 target, float fov_deg, V2 center, float view_h); /* resets clip, fog and alpha */
void wc_roll(float ang);
void wc_clip(float x, float y, float w, float h);
bool wc_project(V3 p, V2 *out, float *depth);
float wc_scale_at(V3 p);  /* virtual pixels per world unit at p, 0 behind the camera */
void w_line(V3 a, V3 b, float w, Col c);
void w_poly(const V3 *p, int n, bool closed, float w, Col c);
void w_poly_cols(const V3 *p, const Col *cols, int n, bool closed, float w);
void w_poly_reveal(const V3 *p, int n, bool closed, float w, Col c, float k);
void w_glow(V3 p, float radius, Col c);   /* world-sized */
void w_dot(V3 p, float px, Col c);        /* screen-sized */
void w_ring(V3 c, V3 ax, V3 ay, float r, int segs, float w, Col col);
void w_arc(V3 c, V3 ax, V3 ay, float r, float a0, float a1, int segs, float w, Col col);
void w_box(V3 lo, V3 hi, float w, Col c);
void w_fill(const V3 *p, int n, Col c);   /* a filled convex polygon, cut at the near plane but not at the clip rect */
void w_grid(V3 c, float half, float step, float w, Col col); /* a floor grid fading out to its rim */
void w_text(const char *s, V3 at, V3 ax, V3 ay, float size, float w, Col c, int align);

/* line models: polylines around a local origin, drawn with a position, rotation and scale */
#define WM_MAXP 480
#define WM_MAXS 64
typedef struct {
    int ns, np;
    int start[WM_MAXS], len[WM_MAXS];
    bool closed[WM_MAXS];
    V3 p[WM_MAXP];
    bool sphere;          /* the points lie on a sphere: the far side is drawn dim */
} WModel;
void wm_clear(WModel *m);
void wm_stroke(WModel *m, const V3 *p, int n, bool closed);
void wm_extrude(WModel *m, const Shape *s, float y0, float y1, float top_scale);
void wm_sphere(WModel *m, int lats, int lons, float bump, uint32_t seed); /* bump > 0: a lumpy rock */
void wm_octahedron(WModel *m, float r, float h);
void wm_draw(const WModel *m, V3 pos, M3 rot, float scale, float w, Col c);
void wm_draw_reveal(const WModel *m, V3 pos, M3 rot, float scale, float w, Col c, float k);
/* te seconds after the model blew apart: every segment flies off spinning */
void wm_draw_explode(const WModel *m, V3 pos, M3 rot, float scale, float w, Col c, float te, uint32_t seed);

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
/* music.c, driven by audio.c: music_render and music_request run with the audio lock held */
void music_init(void);
void music_shutdown(void);
void music_render(float *out, int frames, float vol);
void music_request(int song);

/* ------------------------------------------------------------------ level.c */
enum { T_EMPTY = 0, T_SOLID, T_TRI_TL, T_TRI_TR, T_TRI_BL, T_TRI_BR, T_BREAK };
#define TF_ENERGY 1
#define TF_EXIT 2
#define TF_DOOR 4

enum { LOCK_NONE = 0, LOCK_BLUE, LOCK_YELLOW, LOCK_RED, LOCK_EXIT, LOCK_VAULT };

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

#define MAX_SEGS 16000
#define MAX_DOORS 64
#define MAX_CPTS 32000
#define MAX_CONTOURS 3000

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
    int break_segs[1024];
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
void grid_wake(V2 p, V2 vel, float radius, float strength, float dt); /* a body flying over the grid */
void grid_update(float dt);
void grid_draw(Col base, float brightness);

/* ------------------------------------------------------------------ backdrop.c */
/* space under the mine floor in parallax depths: nebula, stars, the zone's planets, asteroids */
void backdrop_init(uint32_t seed, int zone, float world_w, float world_h);
void backdrop_draw(Col wall, Col grid, Col rock, Col accent, float t);

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
enum {
    RB_DRONE, RB_LIFTER, RB_TURRET, RB_HULK, RB_SPIDER, RB_BABY, RB_GOPHER, RB_DRILLER, RB_SUPERHULK, RB_CLOAKER,
    RB_WASP, RB_PULSAR, RB_LANCER, RB_BOMBER, RB_CARRIER, RB_MITE, RB_PYLON, RB_BOSS, RB_COUNT
};
enum { PW_LASER, PW_VULCAN, PW_SPREAD, PW_PLASMA, PW_FUSION, PW_COUNT };
enum { SW_CONCUSSION, SW_HOMING, SW_PROX, SW_SMART, SW_MEGA, SW_COUNT };

typedef struct {
    V2 move;          /* -1..1 each */
    V2 aim_world;     /* mouse aim point in world space */
    V2 stick_aim;     /* gamepad aim */
    bool use_stick;
    bool fire1, fire2, burner;
    bool fire2_pressed;
    int select;       /* -1 none, 0 the laser, 1 the special weapon */
    int cycle_p;      /* switch between the two primary slots */
    bool swap;        /* take the weapon under the ship and leave yours behind */
    bool jettison;    /* dump half the cargo as a bomb */
    bool mouse_active;
    float turn;       /* Q / E held: the automap turns */
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
    int level;       /* sector of the run being played */
    int difficulty;  /* combat difficulty 0-4: the chosen skill plus the veteran and elite protocols */
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
    int bonus_shield, bonus_hostage, bonus_full, bonus_skill;
    int cargo_banked, cargo_bonus; /* salvage banked from the hold, and the Adrenaline Bank's extra */
    float escape_margin;           /* self-destruct seconds left when you reached the exit */
    int ram_kills, best_bomb;      /* this mine, for challenges and the run report */
} GameState;
extern GameState G;

void game_init(void);
void game_new(int difficulty, int lives); /* a new run: score, lives and a fresh ship */
void game_start_level(bool fresh_ship);   /* the sector run_prepare_sector() set up */
void game_update(float dt);
void game_draw(void);
void game_end_level(void);
void game_shutdown_level(void);
void game_add_score(int pts);
void game_draw_robot_preview(int type, V2 pos, float ang, float scale);
const char *game_robot_name(int type);
void game_draw_ship_icon(V2 pos, float ang, float scale, Col c);
void game_hud_draw(void);
void game_audio_pause(bool p);
void game_save(FILE *f);       /* the run's part of the save file: score, lives and the ship */
bool game_load_line(const char *key, const char *val);
void game_debug_loadout(void); /* --allweapons */
int game_missile_cap(int s);   /* the secondary slot's capacity for a missile type */
const char *game_killer_text(void);

/* ------------------------------------------------------------------ hud.c */
void hud_msg(const char *text, Col c);
void hud_msg_clear(void);
void hud_update(float dt);
void hud_draw_messages(void);

/* ------------------------------------------------------------------ zones.c */
typedef struct {
    const char *name;
    const char *subtitle;
    const char **map;
    int rows;
    Col wall, grid, rock, accent;
    int song;
    const char *briefing;
    int threats[6];
    int nthreats;
    int matcen_types[4];
    int nmatcen;
} LevelDef;
/* the three zones of a run: their colours, music, robot rosters and generator spawns */
extern const LevelDef ZONES[3];
#define NUM_ZONES 3

/* ------------------------------------------------------------------ mapgen.c */
typedef struct {
    uint32_t seed;
    int zone;         /* 0 Tycho, 1 Io, 2 Ceres: the robot roster */
    int depth;        /* sector of the run, scales the loot */
    int gw, gh;       /* room grid */
    int rooms;
    int keys, vaults, hostages, generators;
    float budget;     /* robot weight to place (a drone weighs 1) */
    bool armory;      /* two special weapons and a laser upgrade */
    bool beacon;      /* an extra ship in the deepest vault */
    bool crate;       /* an R&D crate in the deepest vault */
    bool drained;     /* no energy centers */
    bool boss;        /* the finale: the Overseer's arena instead of a reactor */
    float traps;      /* how many traps the rooms get, 1 = the usual for the depth */
} MapSpec;
typedef struct { int rooms, keys, vaults, hostages, robots, generators, traps; } MapInfo;
/* builds an ASCII mine in the zones.c legend; the rows stay valid until the next call */
int mapgen_build(const MapSpec *s, const char ***rows, MapInfo *info);

/* ------------------------------------------------------------------ modules.c */
/* the run's build: modules drafted in the hangar between sectors */
enum { MC_HULL, MC_WEAPON, MC_ENGINE, MC_SYSTEM, MC_CARGO, MC_COUNT };
/* when a module acts, the tags that let you plan combinations */
enum { TG_PASSIVE, TG_KILL, TG_HIT, TG_BURN, TG_CARGO, TG_PICKUP, TG_COUNTDOWN, TG_COUNT };
enum {
    MOD_HULL, MOD_PLATING, MOD_REPAIR, MOD_PHOENIX, MOD_REACTIVE, MOD_VAMPIRE,
    MOD_CAPACITOR, MOD_CALIBRATE, MOD_RACKS, MOD_QUAD, MOD_CLUSTER, MOD_DETONATOR, MOD_RICOCHET, MOD_FABRICATOR,
    MOD_THRUST, MOD_COOLANT, MOD_RAM, MOD_PHASE, MOD_MOMENTUM,
    MOD_SCANNER, MOD_CELLS, MOD_OVERRIDE, MOD_MAPPER, MOD_DRONE, MOD_OVERDRIVE, MOD_BOUNTY, MOD_LIFESUPPORT,
    MOD_TRACTOR, MOD_DAMPERS, MOD_SIPHON, MOD_ANCHOR, MOD_SCRAP, MOD_SHRAPNEL, MOD_ADRENALINE,
    MOD_COUNT
};
typedef struct {
    const char *key;   /* id in the save file */
    const char *name;
    const char *label; /* two-letter glyph */
    int cat, tag;
    int max_rank;
    bool rare;         /* special modules: rarer in drafts, the whole offer of a derelict */
    const char *desc;
} ModDef;
extern const ModDef MODS[MOD_COUNT];
extern const char *MOD_CAT_NAMES[MC_COUNT];
extern const char *MOD_TAG_NAMES[TG_COUNT];
Col mod_color(int cat);
int mod_rank(int id);
bool mod_on(int id);
int mod_installed(void);   /* distinct modules on the ship */
int mod_slots(void);
bool mod_add(int id);      /* install or rank up; false when it does not fit */
void mod_remove(int id);
void mod_effect_text(int id, int rank, char *buf, size_t n);
void mod_draw_badge(int id, V2 c, float r, float alpha, bool lit);
float mod_start_shield(void);
float mod_damage_taken(void);
float mod_repair_cap(void);
float mod_repair_rate(void);
float mod_primary_dmg(void);
int mod_base_laser(void);
int mod_start_missiles(void);
float mod_missile_cap(void);
float mod_accel(void);
float mod_burner_regen(void);
float mod_burner_drain(void);
float mod_magnet(void);
float mod_salvage_magnet(void);
float mod_salvage_mult(void);
float mod_start_energy(void);
float mod_energy_center_rate(void);
float mod_countdown_bonus(void);
float mod_cargo_k(void);   /* mass per salvage in the hold, relative to the standard ship */

/* ------------------------------------------------------------------ run.c */
/* what a sector pays on top of the draft that follows it */
enum { SR_LAB, SR_RICH, SR_BEACON, SR_ARMORY, SR_DERELICT, SR_BOSS, SR_COUNT };
enum { HZ_NONE, HZ_FUSE, HZ_INFESTED, HZ_GRAVITY, HZ_OVERCLOCK, HZ_BLACKOUT, HZ_DRAINED, HZ_ARMORED, HZ_TRAPPED, HZ_COUNT };
/* threat protocols: optional handicaps that add heat, score and salvage */
enum { TP_VETERAN, TP_ELITE, TP_FUSE, TP_ARMORED, TP_SWARM, TP_HEAVY, TP_FRAGILE, TP_SCARCITY, TP_LASTSTAND, TP_COUNT };
enum { SHIP_WRAITH, SHIP_MULE, SHIP_KESTREL, SHIP_WARDEN, SHIP_COUNT };
enum { PH_HANGAR, PH_MAP, PH_BRIEF };

typedef struct {
    const char *name, *role, *desc;
    float shield, energy, accel, burner, cargo;
    int slots;
    int start_mod;   /* -1 none */
    Col col;
} ShipDef;
typedef struct { const char *name, *desc; int heat; } ProtocolDef;
extern const ShipDef SHIPS[SHIP_COUNT];
extern const ProtocolDef PROTOCOLS[TP_COUNT];

#define RUN_MAX_LAYERS 6
#define RUN_MAX_NODES 3
#define DRAFT_MAX 3
typedef struct {
    int reward, hazard, zone, rooms, keys;
    uint32_t seed;
    uint8_t links;   /* nodes of the next layer this one leads to */
    char name[28];
} SectorNode;

typedef struct {
    bool daily;
    uint32_t seed;
    int ship, base_diff;
    uint32_t heat;   /* protocol bitmask */
    int nlayers, nnodes[RUN_MAX_LAYERS];
    SectorNode node[RUN_MAX_LAYERS][RUN_MAX_NODES];
    int layer;       /* the next sector to fly, or the one being flown */
    int path[RUN_MAX_LAYERS];
    int phase;
    int salvage;     /* banked, spent in the hangar */
    uint8_t mods[MOD_COUNT];
    int draft[DRAFT_MAX], ndraft;
    bool draft_taken, draft_rare;
    int rerolls;
    int shop[2];
    bool shop_sold[2];
    int ships_bought, repairs;
    /* the run report */
    float best_escape, time;
    int best_chain, best_haul, best_bomb, kills, rescued, salvage_total, deaths, sectors_done;
    char killed_by[48];
    int unlocked[16], nunlocked; /* challenges completed during this run */
} Run;
extern Run R;

void run_new(int ship, int base_diff, uint32_t heat, uint32_t seed, bool daily);
void run_prepare_sector(void);              /* map and level def for the chosen node of R.layer */
const LevelDef *run_level_def(void);
const SectorNode *run_node(int layer, int i);
const SectorNode *run_cur_node(void);
bool run_reachable(int layer, int i);
int run_first_reachable(void);
float run_tier(void);                       /* 0 Tycho .. 2+ Ceres: scales reactors, generators and bonuses */
int run_hazard(void);
int run_reward(void);
bool run_protocol(int tp);
int run_heat(void);
float run_salvage_mult(void);
float run_cargo_mult(void);
int run_price(int base);
void run_roll_draft(void);
void run_roll_shop(void);
int run_module_price(int id);
void run_sector_escaped(void);               /* stats after a successful escape */
void run_advance(void);                     /* on to the hangar before the next sector */
bool run_final_sector(void);
void run_write(FILE *f);
bool run_read_line(const char *key, const char *val);
void run_rebuild(void);                     /* after loading: the chart, draft and shop from the seed */
const ShipDef *run_ship(void);
const char *reward_name(int r);
const char *reward_desc(int r);
const char *hazard_name(int h);
const char *hazard_desc(int h);
Col zone_color(int zone);
void seed_format(uint32_t seed, char *buf, size_t n);
bool seed_parse(const char *s, uint32_t *out);
uint32_t daily_seed(int *date_out);
void run_note_unlock(int ch);

/* ------------------------------------------------------------------ profile.c */
/* the persistent pilot record: challenges unlock ships, modules, sector types and the protocols */
enum { CH_ESCAPE, CH_CLOSE, CH_HAULER, CH_CHAIN, CH_FULLHOUSE, CH_DEMOLITION, CH_RAM, CH_VAULT, CH_SPEED, CH_DEEP, CH_WIN, CH_HOTSTREAK, CH_COUNT };
typedef struct { const char *name, *desc, *unlocks; } ChallengeDef;
extern const ChallengeDef CHALLENGES[CH_COUNT];
typedef struct {
    uint32_t done;
    int runs, wins, best_heat, best_sector, best_score;
    int lifetime_salvage, lifetime_kills;
    int daily_date, daily_best, daily_tries;
    bool seen_story;
} Profile;
extern Profile g_prof;
void profile_load(const char *path);
void profile_save(void);
void profile_reset(void);
bool profile_done(int ch);
void profile_complete(int ch);   /* marks it, saves and announces the unlocks */
bool ship_unlocked(int s);
bool mod_unlocked(int id);
bool reward_unlocked(int r);
bool protocols_unlocked(void);
int ship_challenge(int s);       /* the challenge that unlocks a ship, -1 for the starter */
int mod_challenge(int id);

/* ------------------------------------------------------------------ cinema.c */
/* the holo briefing: wireframe demos of each objective, built from the mine run_prepare_sector() made */
void holo_setup(void);
void holo_update(float dt);
void holo_draw(float x, float y, float w, float h);
void holo_step(int dir);                    /* jump to the next or previous demo */
int holo_focus_line(const char *briefing);  /* the briefing line the current demo shows, -1 none */
void holo_threat(int type, V2 at, float scale, float t); /* a turntable wireframe of a robot */
void holo_ship(V2 at, float scale, float t, Col c, float reveal); /* the same for the ship, drawing on as reveal goes 0..1 */
/* the cinematics' models for the other holo screens, in the current wire camera */
enum { HM_GLOBE, HM_ROCK, HM_KEY, HM_SHARD, HM_COUNT };
void holo_model(int model, V3 pos, M3 rot, float scale, float w, Col c, float reveal);
void holo_ship3(V3 pos, M3 rot, float scale, Col c, float thrust, float reveal);
void holo_boss3(V3 pos, float scale, float t, Col c);
void holo_reactor3(V3 pos, float scale, float t, Col c, float hp); /* hp 0..1 */
void holo_stars(int n, float alpha, uint32_t seed);   /* around the wire camera, infinitely far */
/* the intro and the ending, as functions of their clock */
#define INTRO_LEN 26.0f
#define ENDING_LEN 31.0f
void cine_intro_update(float t);
void cine_intro_draw(float t);
void cine_ending_update(float t);
void cine_ending_draw(float t);
void cine_backdrop(float t, float alpha);   /* the quiet solar system behind the final card */

/* ------------------------------------------------------------------ chart.c */
/* the sector chart as a holo table: wireframe moons and rocks over a glowing floor, routes arcing between them */
void chart_enter(int sel);                                  /* the camera swoops in */
void chart_update(float dt, int sel, V2 mouse, bool mouse_on);
void chart_draw(int sel, V2 mouse, bool mouse_on);
int chart_pick(V2 mouse);       /* the reachable sector under the mouse, -1 none */
void chart_launch(int sel);     /* the ship flies to the chosen sector and dives in */
float chart_launch_k(void);     /* -1 before a launch, then 0..1 over the flight */

/* ------------------------------------------------------------------ automap.c */
/* the automap as a holo model of the explored mine: walls rising as a scan sweeps out from the ship */
void automap_open(void);
void automap_update(float dt, V2 mouse, bool drag);
void automap_draw(void);

/* ------------------------------------------------------------------ main.c */
extern float g_time;       /* real time seconds */
extern bool g_gamepad_connected;
