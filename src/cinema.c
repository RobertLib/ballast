/*
 * Wireframe cinematics: the holo briefing that acts out each objective of the
 * mine, the intro and the ending. Every scene is a pure function of its clock,
 * so looping, skipping and jumping between demos cost nothing.
 */
#include "game_internal.h"

/* ============================================================ helpers */
static float seg(float t, float a, float b) { return b > a ? clampf((t - a) / (b - a), 0, 1) : (t >= b ? 1.0f : 0.0f); }
static float ease(float x) { return smooth01(x); }
static float ease_out(float x) {
    x = clampf(x, 0, 1);
    return 1 - (1 - x) * (1 - x) * (1 - x);
}
static float ease_in(float x) {
    x = clampf(x, 0, 1);
    return x * x * x;
}
/* 0 before a, 1 between a+f and b-f, 0 after b */
static float env(float t, float a, float b, float f) { return seg(t, a, a + f) * (1 - seg(t, b - f, b)); }
/* when ease(seg(u, a, b)) reaches s */
static float ease_time(float a, float b, float s) {
    float lo = 0, hi = 1;
    for (int i = 0; i < 20; i++) {
        float m = (lo + hi) * 0.5f;
        if (ease(m) < s) lo = m;
        else hi = m;
    }
    return a + (b - a) * lo;
}
static float hf(uint32_t i) { return (hash32(i * 0x9E3779B1u + 0x7F4A7C15u) >> 8) * (1.0f / 16777216.0f); }
static float hs(uint32_t i) { return hf(i) * 2 - 1; }
static V3 hdir(uint32_t i) {
    float z = hs(i * 3 + 1), a = hf(i * 3 + 2) * TAU, r = sqrtf(maxf(0, 1 - z * z));
    return v3(r * cosf(a), z, r * sinf(a));
}
static V3 orbit(V3 target, float yaw, float pitch, float dist) {
    return v3add(target, v3(sinf(yaw) * cosf(pitch) * dist, sinf(pitch) * dist, cosf(yaw) * cosf(pitch) * dist));
}
static V3 mx(V3 pos, M3 rot, float s, V3 local) { return v3add(pos, m3mul(rot, v3scale(local, s))); }
static const M3 M_ID = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

/* Catmull-Rom through evenly timed points, s in 0..1 */
static V3 path_at(const V3 *p, int n, float s) {
    float f = clampf(s, 0, 1) * (n - 1);
    int i = (int)f;
    if (i > n - 2) i = n - 2;
    float t = f - i, t2 = t * t, t3 = t2 * t;
    V3 p0 = p[i > 0 ? i - 1 : 0], p1 = p[i], p2 = p[i + 1], p3 = p[i + 2 < n ? i + 2 : n - 1];
    V3 r = v3scale(p1, 2);
    r = v3add(r, v3scale(v3sub(p2, p0), t));
    r = v3add(r, v3scale(v3add(v3sub(v3scale(p0, 2), v3scale(p1, 5)), v3sub(v3scale(p2, 4), p3)), t2));
    r = v3add(r, v3scale(v3add(v3sub(v3scale(p1, 3), p0), v3sub(p3, v3scale(p2, 3))), t3));
    return v3scale(r, 0.5f);
}
static float path_heading(const V3 *p, int n, float s) {
    V3 a = path_at(p, n, s - 0.01f), b = path_at(p, n, s + 0.01f);
    return atan2f(b.z - a.z, b.x - a.x);
}
static float path_roll(const V3 *p, int n, float s) {
    float d = wrap_angle(path_heading(p, n, s + 0.04f) - path_heading(p, n, s - 0.04f));
    return clampf(d * 1.6f, -0.7f, 0.7f);
}

/* sounds keyed to a clock: each fires once as the clock passes it, never on a skip */
static float cue_prev = 0, cue_now = 0;
static bool cue_live = false;
static void cue_step(float t) {
    cue_live = t >= cue_now && t - cue_now < 0.25f;
    cue_prev = cue_now;
    cue_now = t;
}
static bool cue(float at) { return cue_live && cue_prev < at && cue_now >= at; }

/* ============================================================ models */
static WModel M_SHIP, M_KEY, M_GLOBE, M_ROCK, M_CORE_OUT, M_CORE_IN, M_BOSS, M_SHARD;
static WModel M_ROBOT[RB_COUNT];
static bool robot_built[RB_COUNT];
static bool models_ready = false;

static void build_ship(void) {
    WModel *m = &M_SHIP;
    wm_clear(m);
    const Stroke *h = &SHIP_SHAPE.s[0];
    V3 lo[18], hi[18];
    for (int k = 0; k < h->n; k++) {
        lo[k] = v3(h->p[k].x, -0.14f, h->p[k].y);
        hi[k] = v3(h->p[k].x * 0.82f + 0.05f, 0.16f, h->p[k].y * 0.6f);
    }
    wm_stroke(m, hi, h->n, true);
    wm_stroke(m, lo, h->n, true);
    for (int k = 0; k < h->n; k++) {
        V3 e[2] = {lo[k], hi[k]};
        wm_stroke(m, e, 2, false);
    }
    /* a raised canopy and the wing guns */
    const Stroke *c = &SHIP_SHAPE.s[1];
    V3 cp[3], cb[3];
    for (int k = 0; k < 3; k++) {
        cp[k] = v3(c->p[k].x, 0.3f, c->p[k].y * 1.3f);
        cb[k] = v3(c->p[k].x * 1.1f, 0.16f, c->p[k].y * 1.8f);
    }
    wm_stroke(m, cp, 3, true);
    for (int k = 0; k < 3; k++) {
        V3 e[2] = {cp[k], cb[k]};
        wm_stroke(m, e, 2, false);
    }
    for (int s = 2; s < 4; s++) {
        const Stroke *g = &SHIP_SHAPE.s[s];
        V3 e[2] = {v3(g->p[0].x, 0.02f, g->p[0].y), v3(g->p[1].x + 0.25f, 0.02f, g->p[1].y)};
        wm_stroke(m, e, 2, false);
    }
}

static void models_init(void) {
    if (models_ready) return;
    models_ready = true;
    build_ship();
    wm_clear(&M_KEY);
    wm_octahedron(&M_KEY, 1.0f, 1.5f);
    wm_clear(&M_SHARD);
    wm_octahedron(&M_SHARD, 1.0f, 0.7f);
    wm_clear(&M_GLOBE);
    wm_sphere(&M_GLOBE, 5, 12, 0, 0);
    wm_clear(&M_ROCK);
    wm_sphere(&M_ROCK, 6, 10, 0.16f, 0x5EC0FFEu);
    /* the reactor: an octagonal drum around a counter-turning cube */
    Shape oct = {0};
    oct.ns = 1;
    oct.s[0].n = 8;
    oct.s[0].closed = true;
    for (int i = 0; i < 8; i++) oct.s[0].p[i] = v2fromang(PI / 8 + TAU * i / 8);
    wm_clear(&M_CORE_OUT);
    wm_extrude(&M_CORE_OUT, &oct, -0.32f, 0.32f, 0.82f);
    Shape sq = {0};
    sq.ns = 1;
    sq.s[0].n = 4;
    sq.s[0].closed = true;
    for (int i = 0; i < 4; i++) sq.s[0].p[i] = v2fromang(TAU * i / 4);
    wm_clear(&M_CORE_IN);
    wm_extrude(&M_CORE_IN, &sq, -0.6f, 0.6f, 1.0f);
    /* the Overseer: a star-shaped hull inside its halo */
    wm_clear(&M_BOSS);
    Shape bs = {0};
    bs.ns = 1;
    bs.s[0] = RSHAPE[RB_BOSS].s[1];
    wm_extrude(&M_BOSS, &bs, -0.3f, 0.3f, 0.7f);
    Shape halo = {0};
    halo.ns = 2;
    halo.s[0] = RSHAPE[RB_BOSS].s[0];
    halo.s[1] = RSHAPE[RB_BOSS].s[2];
    wm_extrude(&M_BOSS, &halo, 0, 0, 1);
}

static const WModel *robot_model(int type) {
    models_init();
    if (!robot_built[type]) {
        robot_built[type] = true;
        wm_clear(&M_ROBOT[type]);
        wm_extrude(&M_ROBOT[type], &RSHAPE[type], -0.24f, 0.24f, 0.72f);
    }
    return &M_ROBOT[type];
}

static void ship_draw(V3 pos, M3 rot, float scale, Col c, float thrust, float reveal) {
    if (reveal < 1) wm_draw_reveal(&M_SHIP, pos, rot, scale, 2.6f, c, reveal);
    else wm_draw(&M_SHIP, pos, rot, scale, 2.6f, c);
    if (reveal < 1) return;
    w_glow(pos, scale * 2.4f, col_a(c, 0.1f));
    if (thrust > 0.02f) {
        float L = thrust * (0.7f + 0.3f * sinf(g_time * 47) * sinf(g_time * 23));
        V3 a = mx(pos, rot, scale, v3(-0.75f, 0, 0.24f)), b = mx(pos, rot, scale, v3(-0.75f, 0, -0.24f));
        V3 tip = mx(pos, rot, scale, v3(-0.8f - L * 1.3f, 0, 0));
        Col fc = col_lerp(C_ORANGE, C_YELLOW, 0.4f);
        w_line(a, tip, 2.4f, col_a(fc, 0.9f));
        w_line(b, tip, 2.4f, col_a(fc, 0.9f));
        w_glow(mx(pos, rot, scale, v3(-0.9f, 0, 0)), scale * (0.3f + L * 0.25f), col_a(fc, 0.45f));
    }
}

static void robot_draw3(int type, V3 pos, M3 rot, float scale, Col c) {
    const WModel *m = robot_model(type);
    wm_draw(m, pos, rot, scale, 2.4f, c);
    if (type == RB_TURRET) w_line(mx(pos, rot, scale, v3(0.5f, 0.1f, 0)), mx(pos, rot, scale, v3(1.35f, 0.1f, 0)), 3, c);
    w_glow(pos, scale * 1.8f, col_a(c, 0.1f));
}

/* the Overseer: its hull, a double halo of arcs and a hot core */
static void boss_draw3(V3 pos, float scale, float t, Col c, float jitter) {
    V3 p = v3add(pos, v3(hs((uint32_t)(t * 60)) * jitter, hs((uint32_t)(t * 60) + 7) * jitter * 0.5f, hs((uint32_t)(t * 60) + 13) * jitter));
    wm_draw(&M_BOSS, p, m3_euler(-t * 0.9f, 0, 0), scale, 2.8f, c);
    for (int ring = 0; ring < 2; ring++) {
        V3 ax = v3(1, 0, 0), ay = ring ? v3norm(v3(0, 0.45f, 0.9f)) : v3(0, 0, 1);
        for (int k = 0; k < 6; k++) {
            float a0 = (ring ? -1 : 1) * t * 0.6f + k * TAU / 6;
            w_arc(p, ax, ay, scale * (1.25f + ring * 0.2f), a0, a0 + 0.8f, 6, 3, col_a(c, 0.9f - ring * 0.25f));
        }
    }
    w_glow(p, scale * 0.9f, col_a(c, 0.5f));
    w_glow(p, scale * 0.35f, col_a(C_WHITE, 0.8f));
}

static void reactor_draw3(V3 pos, float scale, float t, Col c, float hp, float flash) {
    Col cc = col_white(c, flash * 0.8f);
    wm_draw(&M_CORE_OUT, pos, m3_euler(t * 0.7f, 0, 0), scale, 3, cc);
    wm_draw(&M_CORE_IN, pos, m3_mulm(m3_euler(-t * 1.3f, 0, 0), m3_axis(v3(1, 0, 1), 0.6f)), scale * 0.42f, 2.4f, col_white(cc, 0.3f));
    int rings = 1 + (int)(hp * 2.99f);
    for (int r = 0; r < rings; r++) {
        float rr = scale * (1.3f + r * 0.2f);
        for (int k = 0; k < 6; k++) {
            float a0 = (r & 1 ? -1 : 1) * t * (0.5f + 0.2f * r) + k * TAU / 6;
            w_arc(pos, v3(1, 0, 0), v3(0, 0, 1), rr, a0, a0 + 0.55f, 4, 2.4f, col_a(cc, 0.8f - r * 0.15f));
        }
    }
    float pulse = 0.6f + 0.4f * sinf(t * (5 + (1 - hp) * 12));
    Col core = col_lerp(rgba(1, 1, 0.8f, 1), rgba(1, 0.3f, 0.2f, 1), 1 - hp);
    w_glow(pos, scale * (0.5f + 0.3f * pulse), col_a(core, 0.85f));
    w_glow(pos, scale * 1.6f, col_a(c, 0.14f));
    w_line(v3(pos.x, 0, pos.z), v3(pos.x, pos.y + scale * 1.6f, pos.z), 2, col_a(c, 0.35f));
}

/* analytic sparks: n streaks flying out of p, te seconds after the blast */
static void sparks3(V3 p, float te, int n, float speed, Col c, uint32_t seed) {
    if (te < 0 || te > 1.2f) return;
    float a = 1 - te / 1.2f;
    for (int i = 0; i < n; i++) {
        V3 d = hdir(seed + i);
        float sp = speed * (0.4f + 0.6f * hf(seed * 3 + i));
        float r = sp * te / (1 + te * 1.5f);
        V3 q = v3mad(p, d, r);
        w_line(q, v3mad(q, d, -sp * 0.06f), 2, col_a(col_white(c, a * 0.5f), a));
    }
}

static void person3(V3 p, float s, float t, Col c) {
    /* a waving stick figure that always faces the camera */
    V3 r = v3norm(v3(g_wc.r.x, 0, g_wc.r.z)), u = v3(0, 1, 0);
#define PP(x, y) v3add(p, v3add(v3scale(r, (x) * s), v3scale(u, (y) * s)))
    w_ring(PP(0, 25), r, u, 4 * s, 12, 2.2f, c);
    w_line(PP(0, 21), PP(0, 10), 2.2f, c);
    float wave = sinf(t * 7) * 5;
    w_line(PP(0, 18), PP(-8, 23 + wave), 2.2f, c);
    w_line(PP(0, 18), PP(8, 23 - wave), 2.2f, c);
    w_line(PP(0, 10), PP(-5, 0), 2.2f, c);
    w_line(PP(0, 10), PP(5, 0), 2.2f, c);
#undef PP
}

/* ============================================================ holo briefing */
enum { HS_SCAN, HS_KEY, HS_MINERS, HS_CARGO, HS_CORE, HS_VAULT, HS_ESCAPE, HS_COUNT };
static const float HS_LEN[HS_COUNT] = {9.5f, 5.4f, 4.8f, 6.2f, 7.0f, 5.0f, 5.8f};
static const char *HS_TITLE[HS_COUNT] = {"SECTOR SCAN", "ACCESS KEYS", "TRAPPED MINERS", "GREED HAS MASS", "REACTOR CORE", "VAULTS", "ESCAPE"};

#define HW_MAX 7000
#define HM_MAX 1800
#define HR_MAX 1200
static char hmap[MAP_MAX_H][MAP_MAX_W];
static struct {
    int steps[HS_COUNT], nsteps;
    float clock;
    bool boss;
    int keys, hostages, vaults;
    Col wall, accent, grid;
    int mw, mh;
    V2 mc;                   /* centre and size of the open part of the mine */
    float mspan_x, mspan_y;
    int nwall;
    V2 wa[HW_MAX], wb[HW_MAX];
    int nmark;
    V2 mpos[HM_MAX];
    char mkind[HM_MAX];
    int nroute;
    V2 route[HR_MAX];
    float rlen[HR_MAX];      /* distance along the route */
    int nlegs;
    int leg_end[6];          /* the last route point of each leg */
    Col leg_col[6];
    const char *leg_name[6];
    /* the viewport of this frame */
    V2 vc;
    float vh;
    SDL_FRect clip;
    float alpha;
} H;

static bool hsolid(int x, int y) {
    if (x < 0 || y < 0 || x >= H.mw || y >= H.mh) return true;
    char c = hmap[y][x];
    return c == '#' || c == 'w';
}

static bool hpass(int x, int y, int keys, bool exit_open, bool loose) {
    if (hsolid(x, y)) return false;
    if (loose) return true;
    switch (hmap[y][x]) {
    case 'b': return keys >= 1;
    case 'y': return keys >= 2;
    case 'r': return keys >= 3;
    case 'x': return exit_open;
    case 'X': return false;
    default: return true;
    }
}

/* shortest path to the nearest cell holding one of the target chars; appended to the route */
static bool route_leg(int *sx, int *sy, const char *targets, int keys, bool exit_open) {
    static int parent[MAP_MAX_H * MAP_MAX_W], queue[MAP_MAX_H * MAP_MAX_W];
    for (int loose = 0; loose < 2; loose++) {
        int n = H.mw * H.mh;
        for (int i = 0; i < n; i++) parent[i] = -1;
        int start = *sy * H.mw + *sx, qh = 0, qt = 0, found = -1;
        parent[start] = start;
        queue[qt++] = start;
        while (qh < qt && found < 0) {
            int cur = queue[qh++], cx = cur % H.mw, cy = cur / H.mw;
            if (cur != start && strchr(targets, hmap[cy][cx])) {
                found = cur;
                break;
            }
            for (int d = 0; d < 8; d++) {
                static const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
                int nx = cx + DX[d], ny = cy + DY[d];
                if (nx < 0 || ny < 0 || nx >= H.mw || ny >= H.mh) continue;
                int ni = ny * H.mw + nx;
                if (parent[ni] >= 0 || !hpass(nx, ny, keys, exit_open, loose)) continue;
                if (d >= 4 && (!hpass(cx + DX[d], cy, keys, exit_open, loose) || !hpass(cx, cy + DY[d], keys, exit_open, loose))) continue;
                parent[ni] = cur;
                queue[qt++] = ni;
            }
        }
        if (found < 0) continue;
        /* walk back, then keep every third cell and round the corners off */
        static int cells[MAP_MAX_H * MAP_MAX_W];
        int nc = 0;
        for (int c = found; c != start; c = parent[c]) cells[nc++] = c;
        cells[nc++] = start;
        V2 raw[512];
        int nr = 0;
        for (int i = nc - 1; i >= 0 && nr < 511; i -= 3) raw[nr++] = v2(cells[i] % H.mw + 0.5f, cells[i] / H.mw + 0.5f);
        V2 end = v2(found % H.mw + 0.5f, found / H.mw + 0.5f);
        if (nr == 0 || v2dist(raw[nr - 1], end) > 0.1f) raw[nr++] = end;
        if (H.nroute == 0) H.route[H.nroute++] = raw[0]; /* later legs start where the last one ended */
        for (int i = 0; i + 1 < nr && H.nroute + 2 < HR_MAX; i++) {
            H.route[H.nroute++] = v2lerp(raw[i], raw[i + 1], 0.25f);
            H.route[H.nroute++] = v2lerp(raw[i], raw[i + 1], 0.75f);
        }
        if (H.nroute < HR_MAX) H.route[H.nroute++] = end;
        *sx = found % H.mw;
        *sy = found / H.mw;
        return true;
    }
    return false;
}

static void holo_add_leg(Col c, const char *name) {
    if (H.nlegs >= 6) return;
    H.leg_end[H.nlegs] = H.nroute - 1;
    H.leg_col[H.nlegs] = c;
    H.leg_name[H.nlegs] = name;
    H.nlegs++;
}

void holo_setup(void) {
    models_init();
    const LevelDef *d = run_level_def();
    memset(&H, 0, sizeof(H));
    H.wall = d->wall;
    H.accent = d->accent;
    H.grid = d->grid;
    H.mh = d->rows < MAP_MAX_H ? d->rows : MAP_MAX_H;
    for (int y = 0; y < H.mh; y++) {
        int len = d->map ? (int)strlen(d->map[y]) : 0;
        if (len > MAP_MAX_W) len = MAP_MAX_W;
        if (len > H.mw) H.mw = len;
    }
    int sx = -1, sy = -1;
    for (int y = 0; y < H.mh; y++) {
        int len = (int)strlen(d->map[y]);
        for (int x = 0; x < H.mw; x++) {
            char c = x < len ? d->map[y][x] : '#';
            hmap[y][x] = c;
            if (c == 'S') { sx = x; sy = y; }
            if (c >= '1' && c <= '3' && c - '0' > H.keys) H.keys = c - '0';
            if (c == 'H') H.hostages++;
            if (c == 'X') H.vaults = 1;
            if (c == 'W') H.boss = true;
            if (c != '#' && c != '.' && c != 'w' && H.nmark < HM_MAX) {
                H.mpos[H.nmark] = v2(x + 0.5f, y + 0.5f);
                H.mkind[H.nmark++] = c;
            }
        }
    }
    /* the rock outline: boundaries between solid and open cells, merged into runs */
    for (int y = 0; y <= H.mh && H.nwall < HW_MAX; y++)
        for (int x = 0; x < H.mw && H.nwall < HW_MAX;) {
            if (hsolid(x, y - 1) == hsolid(x, y)) { x++; continue; }
            int x0 = x;
            bool up = hsolid(x, y - 1);
            while (x < H.mw && hsolid(x, y - 1) != hsolid(x, y) && hsolid(x, y - 1) == up) x++;
            H.wa[H.nwall] = v2((float)x0, (float)y);
            H.wb[H.nwall++] = v2((float)x, (float)y);
        }
    for (int x = 0; x <= H.mw && H.nwall < HW_MAX; x++)
        for (int y = 0; y < H.mh && H.nwall < HW_MAX;) {
            if (hsolid(x - 1, y) == hsolid(x, y)) { y++; continue; }
            int y0 = y;
            bool left = hsolid(x - 1, y);
            while (y < H.mh && hsolid(x - 1, y) != hsolid(x, y) && hsolid(x - 1, y) == left) y++;
            H.wa[H.nwall] = v2((float)x, (float)y0);
            H.wb[H.nwall++] = v2((float)x, (float)y);
        }
    /* the route a pilot flies: the keys in order, the core, the exit */
    if (sx >= 0) {
        static const char *KEYCH[3] = {"1", "2", "3"};
        static const char *KEYNAME[3] = {"BLUE KEY", "YELLOW KEY", "RED KEY"};
        int x = sx, y = sy;
        for (int k = 0; k < H.keys; k++)
            if (route_leg(&x, &y, KEYCH[k], k, false)) holo_add_leg(key_color(LOCK_BLUE + k), KEYNAME[k]);
        if (route_leg(&x, &y, H.boss ? "W" : "C", H.keys, false)) holo_add_leg(H.boss ? RDEF[RB_BOSS].col : H.accent, H.boss ? "OVERSEER" : "CORE");
        if (route_leg(&x, &y, "Z", H.keys, true)) holo_add_leg(C_GREEN, "EXIT");
    }
    for (int i = 0; i < H.nroute; i++) H.rlen[i] = i ? H.rlen[i - 1] + v2dist(H.route[i - 1], H.route[i]) : 0;
    int bx0 = H.mw, by0 = H.mh, bx1 = 0, by1 = 0;
    for (int y = 0; y < H.mh; y++)
        for (int x = 0; x < H.mw; x++)
            if (!hsolid(x, y)) {
                if (x < bx0) bx0 = x;
                if (x > bx1) bx1 = x;
                if (y < by0) by0 = y;
                if (y > by1) by1 = y;
            }
    if (bx1 < bx0) { bx0 = 0; bx1 = H.mw; by0 = 0; by1 = H.mh; }
    H.mc = v2((bx0 + bx1 + 1) * 0.5f, (by0 + by1 + 1) * 0.5f);
    H.mspan_x = (float)(bx1 - bx0 + 3);
    H.mspan_y = (float)(by1 - by0 + 3);
    /* the demos this mine needs */
    H.steps[H.nsteps++] = HS_SCAN;
    if (H.keys) H.steps[H.nsteps++] = HS_KEY;
    if (H.hostages) H.steps[H.nsteps++] = HS_MINERS;
    H.steps[H.nsteps++] = HS_CARGO;
    H.steps[H.nsteps++] = HS_CORE;
    if (H.vaults) H.steps[H.nsteps++] = HS_VAULT;
    H.steps[H.nsteps++] = HS_ESCAPE;
}

void holo_update(float dt) { H.clock += dt; }

static float holo_total(void) {
    float t = 0;
    for (int i = 0; i < H.nsteps; i++) t += HS_LEN[H.steps[i]];
    return t;
}

/* the demo playing now, its local clock and how many loops ran */
static int holo_cur(float *local, int *loop) {
    float total = holo_total();
    if (total <= 0) return -1;
    float t = fmodf(H.clock, total);
    if (loop) *loop = (int)(H.clock / total);
    for (int i = 0; i < H.nsteps; i++) {
        float l = HS_LEN[H.steps[i]];
        if (t < l || i == H.nsteps - 1) {
            if (local) *local = t;
            return i;
        }
        t -= l;
    }
    return -1;
}

void holo_step(int dir) {
    float local;
    int loop, i = holo_cur(&local, &loop);
    if (i < 0) return;
    int j = i + dir;
    if (j < 0) { j += H.nsteps; loop--; }
    if (j >= H.nsteps) { j -= H.nsteps; loop++; }
    if (loop < 0) loop = 0;
    float t = loop * holo_total();
    for (int k = 0; k < j; k++) t += HS_LEN[H.steps[k]];
    H.clock = t + 0.001f;
}

int holo_focus_line(const char *brief) {
    int i = holo_cur(NULL, NULL);
    if (i < 0 || !brief) return -1;
    const char *key = NULL;
    switch (H.steps[i]) {
    case HS_KEY: key = "ACCESS KEY"; break;
    case HS_MINERS: key = "miners"; break;
    case HS_CARGO: key = "cargo"; break;
    case HS_CORE: key = H.boss ? "Destroy THE OVERSEER" : "REACTOR CORE"; break;
    case HS_VAULT: key = "vault"; break;
    case HS_ESCAPE: key = H.boss ? "Escape before" : "EXIT TUNNEL"; break;
    default: return -1;
    }
    const char *hit = strstr(brief, key);
    if (!hit) return -1;
    int line = 0;
    for (const char *c = brief; c < hit; c++) line += *c == '\n';
    return line;
}

/* the camera of a demo, fitted to the viewport */
static void hcam(V3 eye, V3 target, float fov) {
    wc_look(eye, target, fov, H.vc, H.vh);
    wc_clip(H.clip.x, H.clip.y, H.clip.w, H.clip.h);
    g_wc.alpha = H.alpha;
    float d = v3dist(eye, target);
    g_wc.fog_near = d * 0.7f;
    g_wc.fog_far = d * 1.9f;
}

static void hfloor(float half, float step) { w_grid(v3(0, 0, 0), half, step, 1.2f, col_a(col_white(H.grid, 0.25f), 0.55f)); }

/* 2D overlays inside the viewport follow the demo's fade */
static Col ha(Col c) { return col_a(c, H.alpha); }

static void hlabel(V3 p, const char *s, float size, Col c) {
    V2 v;
    if (!wc_project(p, &v, NULL)) return;
    float w = text_width(s, size) * 0.5f;
    v.x = clampf(v.x, H.clip.x + w + 4, H.clip.x + H.clip.w - w - 4);
    if (v.y < H.clip.y + 4 || v.y > H.clip.y + H.clip.h - size - 4) return;
    r_text(s, v.x, v.y, size, ha(c), AL_CENTER);
}

static void hflash(float a, Col c) {
    if (a > 0.001f) r_add_rect(H.clip.x, H.clip.y, H.clip.w, H.clip.h, col_a(c, a * H.alpha));
}

/* a small gauge in the viewport corner: the hold filling up */
static void hgauge(const char *label, float fill, Col c) {
    float x = H.clip.x + H.clip.w - 118, y = H.clip.y + 8;
    r_text(label, x, y, 8, ha(col_a(c, 0.8f)), AL_LEFT);
    r_frame(x, y + 13, 104, 8, 1.5f, ha(col_a(c, 0.6f)));
    if (fill > 0) r_add_rect(x + 2, y + 15, 100 * clampf(fill, 0, 1), 4, ha(col_a(c, 0.8f)));
}

static void hcountdown(float secs, float y, float size) {
    char buf[16];
    snprintf(buf, sizeof(buf), "0:%04.1f", maxf(0, secs));
    Col c = fmodf(g_time * 4, 1) < 0.6f ? C_RED : col_white(C_RED, 0.5f);
    r_text_glow(buf, H.clip.x + H.clip.w * 0.5f, y, size, ha(c), AL_CENTER);
}

static V3 mapw(V2 p, float y) { return v3(p.x - H.mc.x, y, p.y - H.mc.y); }

static void map_icon(char k, V2 s, float pop, float t) {
    float z = pop;
    switch (k) {
    case 'S': game_draw_ship_icon(s, -PI / 2, 6 * z, ha(C_WHITE)); break;
    case '1': case '2': case '3': {
        Col c = key_color(LOCK_BLUE + (k - '1'));
        V2 d[4] = {v2(s.x, s.y - 6 * z), v2(s.x + 4.5f * z, s.y), v2(s.x, s.y + 6 * z), v2(s.x - 4.5f * z, s.y)};
        r_polyline(d, 4, true, 2.4f, ha(c));
        r_glow(s, 14 * z, ha(col_a(c, 0.35f)));
    } break;
    case 'C': case 'W': {
        Col c = k == 'W' ? RDEF[RB_BOSS].col : H.accent;
        r_circle(s, 7 * z, 2.6f, ha(c), 8);
        r_circle(s, (10 + 3 * sinf(t * 5)) * z, 1.6f, ha(col_a(c, 0.6f)), 16);
        r_glow(s, 20 * z, ha(col_a(c, 0.35f)));
    } break;
    case 'Z': r_glow(s, 5 * z, ha(col_a(C_GREEN, 0.5f))); break;
    case 'H': r_glow(s, 7 * z, ha(col_a(rgba(0.45f, 1, 0.55f, 1), 0.7f + 0.3f * sinf(t * 6)))); break;
    case '@': r_glow(s, 7 * z, ha(col_a(powerup_color(PU_TREASURE), 0.8f))); break;
    case 'b': case 'y': case 'r': {
        Col c = key_color(k == 'b' ? LOCK_BLUE : k == 'y' ? LOCK_YELLOW : LOCK_RED);
        r_glow(s, 5 * z, ha(col_a(c, 0.9f)));
    } break;
    case 'X': r_glow(s, 4 * z, ha(col_a(rgba(0.75f, 0.45f, 0.18f, 1), 0.9f))); break;
    case 'x': r_glow(s, 4 * z, ha(col_a(C_RED, 0.8f))); break;
    case 'E': r_glow(s, 4 * z, ha(col_a(C_YELLOW, 0.5f))); break;
    case '~': case '=': case 'O': case 'Y': case 'n': r_glow(s, 3.5f * z, ha(col_a(C_ORANGE, 0.45f))); break;
    default:
        if (strchr("dlthsgvucaPkBq", k)) r_glow(s, 3.5f * z, ha(col_a(C_RED, 0.55f)));
        break;
    }
}

static void demo_scan(float u) {
    float t = g_time;
    /* the approach: the planet turns under a target lock, then the camera dives */
    if (u < 1.9f) {
        float dive = ease_in(seg(u, 0.2f, 1.9f));
        V3 P = v3(0, sinf(0.45f) * 100, cosf(0.45f) * 100);
        V3 eye = v3lerp(v3(0, 90, 430), v3add(P, v3(0, 14, 30)), dive);
        hcam(eye, v3lerp(v3(0, 10, 0), P, dive), 40);
        g_wc.fog_near = 0;
        g_wc.fog_far = 0;
        g_wc.alpha = H.alpha * (1 - seg(u, 1.45f, 1.9f));
        g_wc.glitch = seg(u, 1.4f, 1.9f);
        g_wc.glitch_seed = (uint32_t)(t * 30);
        wm_draw(&M_GLOBE, v3(0, 0, 0), m3_euler(t * 0.35f, 0, 0.35f), 100, 2, H.wall);
        w_glow(v3(0, 0, 0), 180, col_a(H.wall, 0.1f));
        float ping = fmodf(u * 1.4f, 1);
        V3 ax = v3(1, 0, 0), ay = v3norm(v3(0, -cosf(0.45f), sinf(0.45f)));
        w_ring(P, ax, ay, 6 + ping * 30, 24, 2, col_a(H.accent, 1 - ping));
        for (int k = 0; k < 4; k++) {
            float a0 = t * 1.5f + k * TAU / 4;
            w_arc(P, ax, ay, 16, a0, a0 + 0.9f, 5, 2.4f, H.accent);
        }
        return;
    }
    /* the scan of the mine itself */
    float aspect = H.clip.w / H.clip.h;
    float span = maxf(H.mspan_x / aspect, H.mspan_y * 1.2f);
    float dist = span * (1.45f + 0.5f * (1 - ease_out(seg(u, 1.8f, 3.2f))));
    float yaw = -0.14f + 0.16f * sinf(u * 0.33f);
    hcam(orbit(v3(0, 0, H.mspan_y * 0.03f), yaw, 0.98f, dist), v3(0, 0, H.mspan_y * 0.03f), 38);
    g_wc.fog_near = 0;
    g_wc.fog_far = 0;
    g_wc.glitch = 1 - seg(u, 1.9f, 2.3f);
    g_wc.glitch_seed = (uint32_t)(t * 30);
    float top = H.mc.y - H.mspan_y * 0.5f, bottom = H.mc.y + H.mspan_y * 0.5f;
    float sweep = lerpf(top - 6, bottom + 6, ease(seg(u, 1.9f, 3.6f)));
    Col wc = col_a(H.wall, 0.8f);
    for (int i = 0; i < H.nwall; i++) {
        float row = (H.wa[i].y + H.wb[i].y) * 0.5f;
        if (row > sweep) continue;
        float hot = clampf(1 - (sweep - row) / 5, 0, 1);
        Col c = hot > 0 ? col_white(wc, hot * 0.7f) : wc;
        w_line(mapw(H.wa[i], 0), mapw(H.wb[i], 0), 1.5f, c);
        w_line(mapw(H.wa[i], 2.2f), mapw(H.wb[i], 2.2f), 1.2f, col_a(c, 0.3f));
    }
    if (sweep < bottom + 5) {
        V3 a = mapw(v2(H.mc.x - H.mspan_x * 0.5f - 2, sweep), 0), b = mapw(v2(H.mc.x + H.mspan_x * 0.5f + 2, sweep), 0);
        w_line(a, b, 2.5f, col_a(col_white(H.accent, 0.4f), 0.8f));
        w_line(a, b, 8, col_a(H.accent, 0.15f));
    }
    /* markers pop up as the sweep finds them */
    for (int i = 0; i < H.nmark; i++) {
        float age = (sweep - H.mpos[i].y) / 14.0f;
        if (age < 0) continue;
        V2 s;
        if (!wc_project(mapw(H.mpos[i], 0.5f), &s, NULL)) continue;
        if (s.x < H.clip.x || s.x > H.clip.x + H.clip.w || s.y < H.clip.y || s.y > H.clip.y + H.clip.h) continue;
        float pop = 1 + 0.8f * (1 - clampf(age, 0, 1)) * (age < 1 ? 1 : 0);
        map_icon(H.mkind[i], s, pop, t + i);
    }
    /* the route: a comet runs it leg by leg */
    if (H.nroute > 1 && u > 3.6f) {
        float total = H.rlen[H.nroute - 1];
        float at = total * ease(seg(u, 3.7f, 8.4f));
        int leg = 0;
        static V3 pts[HR_MAX];
        int np = 0;
        for (int i = 0; i < H.nroute; i++) {
            bool last = i == H.nroute - 1 || H.rlen[i] >= at;
            V2 q = H.route[i];
            if (H.rlen[i] > at && i > 0) q = v2lerp(H.route[i - 1], H.route[i], (at - H.rlen[i - 1]) / maxf(1e-4f, H.rlen[i] - H.rlen[i - 1]));
            pts[np++] = mapw(q, 0.8f);
            if (i == H.leg_end[leg] || last) {
                if (np > 1) {
                    w_poly(pts + 0, np, false, 2.6f, col_a(H.leg_col[leg], 0.95f));
                    w_poly(pts + 0, np, false, 7, col_a(H.leg_col[leg], 0.14f));
                }
                if (last) {
                    w_glow(pts[np - 1], 3.5f, col_a(C_WHITE, 0.9f));
                    w_glow(pts[np - 1], 9, col_a(H.leg_col[leg], 0.5f));
                    break;
                }
                pts[0] = pts[np - 1];
                np = 1;
                if (leg < H.nlegs - 1) leg++;
            }
        }
        /* waypoints the comet has reached get a label */
        for (int l = 0; l < H.nlegs; l++) {
            int e = H.leg_end[l];
            if (H.rlen[e] > at + 0.01f) break;
            V3 p = mapw(H.route[e], 0.8f);
            float since = (at - H.rlen[e]) / maxf(1, total) * 5;
            if (since < 1) w_ring(p, v3(1, 0, 0), v3(0, 0, 1), 1 + since * 8, 20, 2, col_a(H.leg_col[l], 1 - since));
            V2 s;
            if (wc_project(p, &s, NULL)) r_text(H.leg_name[l], s.x, s.y - 22, 8, ha(col_a(col_white(H.leg_col[l], 0.3f), 0.95f)), AL_CENTER);
        }
    }
}

static void demo_key(float u, int loop) {
    int k = H.keys > 0 ? loop % H.keys : 0;
    Col kc = key_color(LOCK_BLUE + k);
    V3 tgt = v3(10, 0, 70);
    hcam(orbit(tgt, -0.22f + 0.08f * sinf(u * 0.6f), 0.7f, 500), tgt, 42);
    hfloor(440, 55);
    Col wc = col_a(H.wall, 0.85f);
    w_box(v3(-420, 0, -72), v3(-58, 46, -40), 2, wc);
    w_box(v3(58, 0, -72), v3(420, 46, -40), 2, wc);
    float open = ease(seg(u, 3.1f, 3.6f));
    for (int side = -1; side <= 1; side += 2) {
        float x0 = side < 0 ? -56 - open * 52 : open * 52, x1 = x0 + 56;
        Col dc = col_white(kc, 0.25f * (0.5f + 0.5f * sinf(g_time * 5)) * (1 - open));
        w_box(v3(x0, 0, -64), v3(x1, 40, -48), 2.4f, dc);
    }
    /* flight: to the key, to the door, through it */
    static const V3 A[6] = {{-340, 18, 250}, {-150, 18, 262}, {60, 18, 215}, {190, 18, 150}, {110, 18, 60}, {0, 18, 40}};
    static const V3 B[3] = {{0, 18, 40}, {0, 18, -150}, {0, 18, -360}};
    float sa = ease(seg(u, 0.25f, 3.0f)), sb = ease_in(seg(u, 3.45f, 5.1f));
    V3 ship;
    float head, roll = 0;
    if (u < 3.45f) {
        ship = path_at(A, 6, sa);
        head = u > 3.0f ? -PI / 2 : path_heading(A, 6, sa);
        if (u < 3.0f) roll = path_roll(A, 6, sa);
        if (u > 2.9f) head = lerpf(path_heading(A, 6, 1), -PI / 2, ease(seg(u, 2.9f, 3.2f)));
        ship.y += sinf(u * 4) * 2;
    } else {
        ship = path_at(B, 3, sb);
        head = -PI / 2;
    }
    bool got = sa >= 0.6f;
    V3 key = v3(190, 26 + 6 * sinf(g_time * 3), 150);
    if (!got) {
        wm_draw(&M_KEY, key, m3_euler(g_time * 2, 0, 0), 15, 2.4f, kc);
        w_glow(key, 44, col_a(kc, 0.3f));
    }
    ship_draw(ship, m3_euler(head, 0, roll), 22, rgba(0.85f, 0.95f, 1, 1), u < 3.45f ? 0.45f : 0.9f, 1);
    if (got) {
        float into = ease(seg(u, 2.95f, 3.2f));
        V3 orb = v3add(ship, v3(cosf(g_time * 5) * 28, 10, sinf(g_time * 5) * 28));
        V3 kp = v3lerp(orb, v3(0, 22, -56), into);
        if (u < 3.25f) wm_draw(&M_KEY, kp, m3_euler(g_time * 4, 0, 0), 6, 2, kc);
    }
    /* the pickup */
    float since = u - ease_time(0.25f, 3.0f, 0.6f);
    if (got && since < 0.8f) {
        w_ring(v3(190, 2, 150), v3(1, 0, 0), v3(0, 0, 1), 10 + since * 120, 32, 3, col_a(kc, 1 - since / 0.8f));
        sparks3(v3(190, 26, 150), since, 14, 260, kc, 77);
        hlabel(v3(190, 60 + since * 40, 150), k == 0 ? "BLUE KEY" : k == 1 ? "YELLOW KEY" : "RED KEY", 10, kc);
    }
    if (u > 3.2f && u < 3.8f) hflash(0.05f * (1 - seg(u, 3.2f, 3.8f)), kc);
}

static void demo_miners(float u) {
    V3 tgt = v3(20, 20, 20);
    hcam(orbit(tgt, 0.35f + 0.08f * sinf(u * 0.7f), 0.55f, 400), tgt, 42);
    hfloor(400, 55);
    Col wc = col_a(H.wall, 0.7f);
    w_box(v3(-260, 0, -160), v3(-200, 70, -100), 2, wc);
    w_box(v3(240, 0, -200), v3(300, 70, -140), 2, wc);
    w_box(v3(-120, 0, -230), v3(160, 30, -200), 2, wc);
    Col mc = rgba(0.45f, 1, 0.55f, 1);
    V3 hp = v3(60, 0, 40);
    static const V3 A[4] = {{-380, 34, 220}, {-160, 40, 120}, {10, 58, 60}, {60, 62, 40}};
    static const V3 B[4] = {{60, 62, 40}, {170, 50, -20}, {300, 44, -120}, {460, 40, -220}};
    V3 ship;
    float head, roll = 0;
    if (u < 2.9f) {
        float s = ease_out(seg(u, 0.2f, 1.7f));
        ship = path_at(A, 4, s);
        head = path_heading(A, 4, minf(s, 0.97f));
        roll = path_roll(A, 4, s) * (1 - s);
        ship.y += sinf(u * 5) * 2;
    } else {
        float s = ease_in(seg(u, 2.9f, 4.7f)) * 0.85f;
        ship = path_at(B, 4, s);
        ship.y -= 6 * s;
        head = path_heading(B, 4, s);
    }
    ship_draw(ship, m3_euler(head, 0, roll), 22, rgba(0.85f, 0.95f, 1, 1), u > 2.9f ? 0.8f : 0.35f, 1);
    float beam = env(u, 1.7f, 2.9f, 0.15f);
    float lift = ease(seg(u, 1.95f, 2.65f));
    if (lift < 1) {
        V3 at = v3(hp.x, lerpf(0, ship.y - 10, lift), hp.z);
        float s = 1.2f * (1 - lift * 0.8f);
        person3(at, s, g_time, col_a(mc, 1 - lift * 0.5f));
        float ring = fmodf(g_time * 0.8f, 1);
        w_ring(v3(hp.x, 1, hp.z), v3(1, 0, 0), v3(0, 0, 1), 18 + ring * 22, 28, 2, col_a(mc, (1 - ring) * 0.7f * (1 - lift)));
        if (fmodf(g_time, 1) < 0.6f && u < 1.7f) hlabel(v3(hp.x, 52, hp.z), "SOS", 10, mc);
    }
    if (beam > 0) {
        for (int i = 0; i < 8; i++) {
            float a = g_time * 2 + i * TAU / 8;
            V3 lo = v3(hp.x + cosf(a) * 20, 0, hp.z + sinf(a) * 20), hi = v3(ship.x + cosf(a) * 6, ship.y - 6, ship.z + sinf(a) * 6);
            w_line(lo, hi, 1.6f, col_a(mc, 0.5f * beam));
        }
        w_glow(v3lerp(hp, ship, 0.5f), 60, col_a(mc, 0.15f * beam));
    }
    float since = u - 2.65f;
    if (since > 0 && since < 1.2f) {
        hlabel(v3(ship.x, ship.y + 30 + since * 30, ship.z), "+1 MINER", 11, mc);
        w_ring(ship, v3(1, 0, 0), v3(0, 0, 1), 10 + since * 90, 28, 2.4f, col_a(mc, 1 - since / 1.2f));
    }
    hgauge("HOLD", u > 2.65f ? 0.2f : 0, mc);
}

static void demo_cargo(float u) {
    Col sc = powerup_color(PU_SALVAGE);
    V3 tgt = v3(60, 0, 40);
    hcam(orbit(tgt, -0.18f + 0.06f * sinf(u * 0.5f), 0.8f, 560), tgt, 42);
    hfloor(440, 55);
    /* wrecks spilling salvage */
    static const V2 WRECK[2] = {{-210, 150}, {110, -40}};
    wm_draw(robot_model(RB_HULK), v3(WRECK[0].x, 8, WRECK[0].y), m3_euler(0.7f, 0.25f, 0.4f), 34, 2, col_a(C_GREY, 0.55f));
    wm_draw(robot_model(RB_LIFTER), v3(WRECK[1].x, 6, WRECK[1].y), m3_euler(2.2f, -0.2f, 0.5f), 30, 2, col_a(C_GREY, 0.55f));
    static const V3 A[6] = {{-400, 20, 230}, {-190, 20, 150}, {-60, 20, 20}, {70, 20, -40}, {200, 20, 10}, {300, 20, 120}};
    /* the heavier the hold, the slower the ship */
    float s = 1 - powf(1 - seg(u, 0.2f, 4.2f), 1.7f);
    V3 ship = path_at(A, 6, s);
    const int NS = 12;
    int got = 0;
    for (int i = 0; i < NS; i++) {
        V2 w = WRECK[i & 1];
        float a = i * TAU / NS * 2 + 0.3f, r = 40 + 50 * hf(i + 5);
        V3 home = v3(w.x + cosf(a) * r, 10 + 4 * sinf(g_time * 3 + i), w.y + sinf(a) * r);
        /* when the ship passes closest, the magnet pulls the shard in */
        float best = 1e9f, bs = 0;
        for (int k = 0; k <= 48; k++) {
            float q = k / 48.0f;
            float dd = v3dist(path_at(A, 6, q), home);
            if (dd < best) { best = dd; bs = q; }
        }
        float pull = seg(s, bs - 0.07f, bs);
        if (pull >= 1) { got++; continue; }
        V3 p = v3lerp(home, ship, ease_in(pull));
        wm_draw(&M_SHARD, p, m3_euler(g_time * 2 + i, 0.4f, 0), 6 * (1 - pull * 0.6f), 1.8f, sc);
        w_glow(p, 16, col_a(sc, 0.3f));
    }
    float mass = got / (float)NS;
    float drop = u - 4.0f, boom = u - 4.6f;
    if (drop > 0) mass *= 0.5f;
    /* a sluggish, heavy ship leaves afterimages */
    for (int g = 1; g <= 3 && mass > 0.2f; g++) {
        float q = maxf(0, s - g * 0.018f);
        ship_draw(path_at(A, 6, q), m3_euler(path_heading(A, 6, q), 0, 0), 18, col_a(C_CYAN, 0.12f * mass / g), 0, 1);
    }
    ship_draw(v3add(ship, v3(0, -8 * mass, 0)), m3_euler(path_heading(A, 6, minf(s, 0.97f)), 0, path_roll(A, 6, s)), 18,
              rgba(0.85f, 0.95f, 1, 1), 0.3f + 0.6f * (1 - mass), 1);
    /* two drones chase the ship into the cargo bomb */
    V3 bomb_at = path_at(A, 6, 1 - powf(1 - seg(4.0f, 0.2f, 4.2f), 1.7f));
    for (int i = 0; i < 2; i++) {
        V3 from = i ? v3(-260, 22, -220) : v3(-300, 22, 260);
        V3 to = v3add(bomb_at, v3(i ? -40 : -70, 0, i ? -50 : 40));
        V3 p = v3lerp(from, to, ease(seg(u, 2.6f, 4.7f)));
        float die = boom - 0.08f * (i + 1);
        float head = atan2f(to.z - from.z, to.x - from.x);
        if (die < 0) robot_draw3(RB_DRONE, p, m3_euler(head, 0, 0), 16, RDEF[RB_DRONE].col);
        else {
            wm_draw_explode(robot_model(RB_DRONE), p, m3_euler(head, 0, 0), 16, 2, RDEF[RB_DRONE].col, die, 300 + i);
            sparks3(p, die, 12, 240, RDEF[RB_DRONE].col, 40 + i);
        }
    }
    if (drop > 0 && boom < 0) {
        V3 bp = v3add(bomb_at, v3(-drop * 60, 0, 0));
        float pulse = 0.5f + 0.5f * sinf(g_time * 18);
        w_glow(bp, 22 + 8 * pulse, col_a(sc, 0.7f));
        wm_draw(&M_SHARD, bp, m3_euler(g_time * 5, 0, 0), 9, 2, col_white(sc, pulse * 0.5f));
    }
    if (boom > 0 && boom < 1.2f) {
        V3 bp = v3add(bomb_at, v3(-0.6f * 60, 0, 0));
        float r = ease_out(seg(boom, 0, 0.7f)) * 190;
        w_ring(v3(bp.x, 2, bp.z), v3(1, 0, 0), v3(0, 0, 1), r, 48, 4, col_a(col_white(sc, 0.3f), 1 - boom / 1.2f));
        w_ring(v3(bp.x, 2, bp.z), v3(1, 0, 0), v3(0, 0, 1), r * 0.7f, 40, 2, col_a(sc, 0.6f * (1 - boom / 1.2f)));
        w_glow(bp, 70 * (1 - boom), col_a(sc, 0.5f * (1 - boom)));
        sparks3(bp, boom, 24, 420, sc, 99);
        hflash(0.08f * (1 - seg(boom, 0, 0.3f)), sc);
    }
    hgauge("MASS", mass, sc);
    if (u > 3.3f && u < 4.4f && fmodf(g_time * 3, 1) < 0.7f) {
        float x = H.clip.x + 20, y = H.clip.y + 10;
        r_frame(x, y, 22, 22, 2, ha(C_WHITE));
        r_text("F", x + 11, y + 6, 10, ha(C_WHITE), AL_CENTER);
        r_text("JETTISON", x + 30, y + 7, 8, ha(col_a(sc, 0.9f)), AL_LEFT);
    }
}

static void demo_core(float u) {
    float t = g_time;
    Col cc = H.boss ? RDEF[RB_BOSS].col : H.accent;
    V3 tgt = v3(0, 20, 0);
    hcam(orbit(tgt, 0.4f + u * 0.1f, 0.62f, 620), tgt, 42);
    hfloor(420, 60);
    w_ring(v3(0, 0, 0), v3(1, 0, 0), v3(0, 0, 1), 380, 64, 2, col_a(H.wall, 0.6f));
    for (int k = 0; k < 4; k++) {
        V2 p = v2scale(v2fromang(PI / 4 + k * PI / 2), 260);
        w_box(v3(p.x - 22, 0, p.y - 22), v3(p.x + 22, 60, p.y + 22), 2, col_a(H.wall, 0.8f));
    }
    const float BOOM = 5.4f;
    V3 core = v3(0, 34, 0);
    float hp = 1 - 0.92f * seg(u, 0.9f, 5.1f);
    /* the ship circles, dodging, and keeps its guns on the core */
    float th = 2.1f - u * 0.7f;
    float rad = 235 + 28 * sinf(u * 2.7f);
    V3 ship = v3(cosf(th) * rad, 24, sinf(th) * rad);
    float face = atan2f(-ship.z, -ship.x);
    /* bullet spiral */
    int arms = H.boss ? 3 : 2;
    if (u < BOOM) {
        for (float te = floorf(maxf(0.6f, u - 2.6f) / 0.07f) * 0.07f; te < minf(u, 5.2f); te += 0.07f) {
            if (te < 0.6f) continue;
            float r = 60 + (u - te) * 150;
            if (r > 390) continue;
            for (int a = 0; a < arms; a++) {
                float ang = te * 2.3f + a * TAU / arms;
                V3 b = v3(cosf(ang) * r, 30, sinf(ang) * r);
                w_dot(b, 3.2f, col_a(C_WHITE, 0.9f));
                w_glow(b, 9, col_a(cc, 0.7f));
            }
        }
        /* rotating beams: telegraphed, then live */
        if (u > 2.6f && u < 4.3f) {
            bool live = u > 3.0f;
            for (int b = 0; b < 2; b++) {
                float ang = u * 0.9f + b * PI;
                V3 e = v3(cosf(ang) * 390, 30, sinf(ang) * 390);
                if (live) {
                    w_line(core, e, 6, col_a(cc, 0.5f));
                    w_line(core, e, 2.4f, col_a(C_WHITE, 0.9f));
                } else w_line(core, e, 1.2f, col_a(cc, 0.5f + 0.5f * sinf(t * 30)));
            }
        }
    }
    /* laser bolts */
    float last_hit = -1;
    for (float te = floorf(maxf(0.9f, u - 0.4f) / 0.13f) * 0.13f; te < minf(u, 5.2f); te += 0.13f) {
        if (te < 0.9f) continue;
        float k = (u - te) / 0.3f;
        float th2 = 2.1f - te * 0.7f, rad2 = 235 + 28 * sinf(te * 2.7f);
        V3 from = v3(cosf(th2) * rad2, 24, sinf(th2) * rad2);
        if (k >= 1) {
            last_hit = maxf(last_hit, te + 0.3f);
            continue;
        }
        V3 d = v3norm(v3sub(core, from));
        V3 p = v3lerp(from, core, k);
        w_line(p, v3mad(p, d, 26), 2.6f, col_a(C_RED, 0.95f));
    }
    float flash = last_hit > 0 ? clampf(1 - (u - last_hit) / 0.08f, 0, 1) : 0;
    if (u < BOOM) {
        float jit = seg(u, 4.9f, BOOM) * 6;
        V3 cp = v3add(core, v3(hs((uint32_t)(t * 50)) * jit, 0, hs((uint32_t)(t * 50) + 3) * jit));
        if (H.boss) boss_draw3(cp, 50, t, col_white(cc, flash * 0.7f), jit);
        else reactor_draw3(cp, 52, t, cc, hp, flash);
    } else {
        float te = u - BOOM;
        if (H.boss) wm_draw_explode(&M_BOSS, core, m3_euler(-t * 0.9f, 0, 0), 44, 2.4f, cc, te, 11);
        else {
            wm_draw_explode(&M_CORE_OUT, core, m3_euler(t * 0.7f, 0, 0), 44, 2.4f, cc, te, 12);
            wm_draw_explode(&M_CORE_IN, core, M_ID, 18, 2, col_white(cc, 0.3f), te, 13);
        }
        float r = ease_out(seg(te, 0, 0.9f)) * 520;
        w_ring(v3(0, 2, 0), v3(1, 0, 0), v3(0, 0, 1), r, 64, 5, col_a(col_white(cc, 0.4f), 1 - seg(te, 0, 1.1f)));
        w_glow(core, 140 * (1 - seg(te, 0, 0.8f)), col_a(cc, 0.6f));
        sparks3(core, te, 30, 520, cc, 5);
        hflash(0.14f * (1 - seg(te, 0, 0.4f)), C_WHITE);
        w_glow(core, 30, col_a(C_ORANGE, 0.2f + 0.1f * sinf(t * 9)));
        if (fmodf(g_time * 2.5f, 1) < 0.65f) r_text_glow("SELF-DESTRUCT", H.clip.x + H.clip.w * 0.5f, H.clip.y + 16, 13, ha(C_RED), AL_CENTER);
    }
    ship_draw(ship, m3_euler(face, 0, 0.3f * sinf(u * 2.7f)), 20, rgba(0.85f, 0.95f, 1, 1), 0.5f, 1);
    if (u < BOOM) {
        /* the core's health */
        float x = H.clip.x + H.clip.w * 0.5f - 70, y = H.clip.y + 12;
        r_text(H.boss ? "THE OVERSEER" : "REACTOR CORE", x + 70, y, 8, ha(col_a(cc, 0.9f)), AL_CENTER);
        r_frame(x, y + 13, 140, 7, 1.5f, ha(col_a(cc, 0.6f)));
        r_add_rect(x + 2, y + 15, 136 * hp, 3, ha(col_a(cc, 0.9f)));
    }
}

static void demo_vault(float u) {
    Col gold = powerup_color(PU_TREASURE);
    V3 tgt = v3(0, 10, -30);
    hcam(orbit(tgt, -0.3f + 0.08f * sinf(u * 0.6f), 0.55f, 470), tgt, 42);
    hfloor(420, 55);
    Col wc = col_a(H.wall, 0.8f);
    w_box(v3(-360, 0, -76), v3(-70, 60, -48), 2, wc);
    w_box(v3(70, 0, -76), v3(360, 60, -48), 2, wc);
    w_box(v3(-150, 0, -260), v3(-110, 60, -76), 2, wc);
    w_box(v3(110, 0, -260), v3(150, 60, -76), 2, wc);
    w_box(v3(-150, 0, -290), v3(150, 60, -260), 2, wc);
    /* the core blows somewhere: the vault's bars rise */
    float open = ease(seg(u, 0.9f, 1.7f));
    Col dc = col_lerp(rgba(0.75f, 0.45f, 0.18f, 1), gold, open);
    for (int i = 0; i < 4; i++) {
        float x = -52 + i * 34.7f;
        w_box(v3(x - 6, open * 64, -68), v3(x + 6, 58 + open * 64, -56), 2, dc);
    }
    static const V3 P[5] = {{-320, 20, 230}, {-60, 20, 90}, {0, 22, -160}, {40, 20, 60}, {330, 20, 220}};
    float s = ease(seg(u, 0.5f, 4.8f));
    if (s < 0.3f) s = 0.3f * ease(seg(u, 0.5f, 1.9f));
    V3 ship = path_at(P, 5, s);
    bool got = s >= 0.5f;
    if (!got)
        for (int i = 0; i < 3; i++) {
            V3 p = v3(-22 + i * 22, 18 + 5 * sinf(g_time * 3 + i), -170 + (i == 1 ? -12 : 0));
            wm_draw(&M_SHARD, p, m3_euler(g_time * 1.5f + i, 0, 0), 11, 2, gold);
            w_glow(p, 26, col_a(gold, 0.3f));
        }
    ship_draw(ship, m3_euler(path_heading(P, 5, s), 0, path_roll(P, 5, s)), 21, rgba(0.85f, 0.95f, 1, 1), 0.6f, 1);
    float since = u - ease_time(0.5f, 4.8f, 0.5f);
    if (got && since < 1.1f) {
        sparks3(v3(0, 20, -165), since, 20, 300, gold, 21);
        hlabel(v3(0, 60 + since * 30, -165), "+ VAULT TREASURE", 10, gold);
    }
    if (u > 0.5f && u < 1.9f) {
        hflash(0.08f * (1 - seg(u, 0.5f, 1.0f)), C_WHITE);
        if (fmodf(g_time * 3, 1) < 0.7f) r_text("CORE DOWN - VAULTS OPEN", H.clip.x + H.clip.w * 0.5f, H.clip.y + 42, 9, ha(gold), AL_CENTER);
    }
    hcountdown(24.6f - u, H.clip.y + 12, 16);
    hgauge("HOLD", got ? 0.75f : 0.35f, powerup_color(PU_SALVAGE));
}

static void demo_escape(float u) {
    float t = g_time;
    static const V2 C[6] = {{-430, 250}, {-230, 240}, {-100, 80}, {130, 40}, {240, -140}, {430, -210}};
    V3 P[6];
    for (int i = 0; i < 6; i++) P[i] = v3(C[i].x, 18, C[i].y);
    float s = ease_in(seg(u, 0.2f, 4.5f)) * 0.55f + seg(u, 0.2f, 4.5f) * 0.45f;
    V3 ship = path_at(P, 6, s);
    V3 tgt = v3lerp(v3(0, 0, 20), v3(ship.x, 0, ship.z), 0.65f);
    V3 eye = orbit(tgt, -0.25f, 0.8f, 500);
    float quake = 3 + 2 * sinf(u * 3);
    eye = v3add(eye, v3(sinf(t * 41) * quake, sinf(t * 37) * quake, 0));
    hcam(eye, tgt, 42);
    float alarm = 0.5f + 0.5f * sinf(t * 7);
    Col wc = col_lerp(col_a(H.wall, 0.85f), col_a(C_RED, 0.85f), alarm * 0.6f);
    /* the corridor: walls either side of the centre line */
    V3 L[48], R[48];
    int n = 0;
    for (int k = 0; k <= 40; k++) {
        float q = k / 40.0f;
        V3 c = path_at(P, 6, q), d = v3sub(path_at(P, 6, q + 0.01f), path_at(P, 6, q - 0.01f));
        V3 nrm = v3norm(v3(-d.z, 0, d.x));
        L[n] = v3mad(v3(c.x, 0, c.z), nrm, 70);
        R[n] = v3mad(v3(c.x, 0, c.z), nrm, -70);
        n++;
    }
    V3 Lt[48], Rt[48];
    for (int i = 0; i < n; i++) {
        Lt[i] = v3add(L[i], v3(0, 46, 0));
        Rt[i] = v3add(R[i], v3(0, 46, 0));
    }
    w_poly(L, n, false, 2, col_a(wc, 0.6f));
    w_poly(R, n, false, 2, col_a(wc, 0.6f));
    w_poly(Lt, n, false, 2, wc);
    w_poly(Rt, n, false, 2, wc);
    for (int i = 0; i < n; i += 4) {
        w_line(L[i], Lt[i], 1.5f, col_a(wc, 0.6f));
        w_line(R[i], Rt[i], 1.5f, col_a(wc, 0.6f));
    }
    /* green chevrons pulse toward the exit */
    for (int k = 1; k < 24; k++) {
        float q = k / 24.0f;
        V3 c = path_at(P, 6, q), f = v3norm(v3sub(path_at(P, 6, q + 0.01f), path_at(P, 6, q - 0.01f)));
        V3 side = v3(-f.z, 0, f.x);
        float wave = fmodf(q * 3 - u * 1.6f + 10, 1);
        float b = 0.25f + 0.75f * powf(1 - wave, 3);
        V3 tip = v3mad(v3(c.x, 1, c.z), f, 12);
        V3 a = v3mad(v3mad(v3(c.x, 1, c.z), f, -8), side, 16), bb = v3mad(v3mad(v3(c.x, 1, c.z), f, -8), side, -16);
        V3 ch[3] = {a, tip, bb};
        w_poly(ch, 3, false, 2.4f, col_a(C_GREEN, b));
    }
    /* the exit tunnel */
    V3 ex = P[5], exd = v3norm(v3sub(P[5], P[4]));
    for (int r = 0; r < 5; r++) {
        V3 c = v3mad(v3add(ex, v3(0, 12, 0)), exd, r * 38);
        float pulse = fmodf(u * 2 - r * 0.2f + 10, 1);
        w_ring(c, v3(-exd.z, 0, exd.x), v3(0, 1, 0), 48 - r * 4, 16, 2.4f, col_a(C_GREEN, 0.4f + 0.6f * (1 - pulse)));
    }
    hlabel(v3add(ex, v3(0, 80, 0)), "EXIT", 10, C_GREEN);
    /* the roof coming down */
    for (int i = 0; i < 10; i++) {
        float per = 0.9f + hf(i) * 0.6f;
        float ph = fmodf(u / per + hf(i + 20), 1);
        float q = fmodf(hf(i + 40) + floorf(u / per + hf(i + 20)) * 0.37f, 1);
        V3 c = path_at(P, 6, q);
        V3 p = v3(c.x + hs(i + 60) * 50, 220 * (1 - ph), c.z + hs(i + 80) * 50);
        wm_draw(&M_SHARD, p, m3_euler(u * 4 + i, u * 3, 0), 7, 1.8f, col_a(col_lerp(C_GREY, C_ORANGE, 0.4f), 0.9f));
        if (ph > 0.9f) w_ring(v3(p.x, 1, p.z), v3(1, 0, 0), v3(0, 0, 1), (ph - 0.9f) * 300, 16, 1.6f, col_a(C_ORANGE, (1 - ph) * 8));
    }
    bool out = u > 4.5f;
    if (!out) {
        float head = path_heading(P, 6, s);
        for (int g = 1; g <= 3; g++) {
            float q = maxf(0, s - g * 0.012f);
            V3 gp = path_at(P, 6, q);
            wm_draw(&M_SHARD, v3add(gp, v3(0, 4, 0)), m3_euler(t * 3 + g, 0, 0), 4, 1.6f, col_a(powerup_color(PU_SALVAGE), 0.8f));
        }
        ship_draw(ship, m3_euler(head, 0, path_roll(P, 6, s)), 21, rgba(0.85f, 0.95f, 1, 1), 1, 1);
    } else {
        float since = u - 4.5f;
        hflash(0.1f * (1 - seg(since, 0, 0.4f)), C_GREEN);
        r_text_glow("CARGO BANKED", H.clip.x + H.clip.w * 0.5f, H.clip.y + H.clip.h * 0.42f, 16, ha(C_GREEN), AL_CENTER);
        r_textf(H.clip.x + H.clip.w * 0.5f, H.clip.y + H.clip.h * 0.42f + 28, 9, ha(col_a(C_WHITE, 0.8f)), AL_CENTER, "ESCAPED WITH %.1f S LEFT",
                9.4f - 4.5f * 1.65f);
    }
    hcountdown(out ? 9.4f - 4.5f * 1.65f : 9.4f - u * 1.65f, H.clip.y + 12, 16);
}

/* captions: what the pilot should take from each demo */
static void demo_caption(int kind, float u, char *buf, size_t n) {
    switch (kind) {
    case HS_SCAN:
        if (u < 3.7f) snprintf(buf, n, "SCANNING THE MINE...");
        else snprintf(buf, n, H.keys ? "YOUR ROUTE: KEYS, %s, EXIT" : "YOUR ROUTE: %s, THEN THE EXIT", H.boss ? "OVERSEER" : "CORE");
        break;
    case HS_KEY: snprintf(buf, n, H.keys > 1 ? "EACH KEY OPENS THE DOOR OF ITS COLOUR" : "GRAB THE KEY - IT OPENS ITS DOOR"); break;
    case HS_MINERS: snprintf(buf, n, "PICK UP MINERS - THEY RIDE IN THE HOLD"); break;
    case HS_CARGO: snprintf(buf, n, u < 3.3f ? "SALVAGE PAYS - BUT IT MAKES YOU HEAVY" : "F DUMPS HALF THE HOLD AS A BOMB"); break;
    case HS_CORE:
        if (u < 5.4f) snprintf(buf, n, H.boss ? "DESTROY THE OVERSEER - DODGE ITS PATTERNS" : "BREAK THE CORE - DODGE ITS PATTERNS");
        else snprintf(buf, n, "THE SELF-DESTRUCT COUNTDOWN STARTS");
        break;
    case HS_VAULT: snprintf(buf, n, u < 2.6f ? "VAULTS OPEN WHEN THE CORE BLOWS" : "THE DEEPER THE VAULT, THE BIGGER THE BET"); break;
    case HS_ESCAPE: snprintf(buf, n, u < 4.5f ? "RACE THE COUNTDOWN TO THE EXIT" : "THE HOLD IS BANKED AT THE EXIT"); break;
    default: buf[0] = 0;
    }
}

void holo_draw(float x, float y, float w, float h) {
    float u;
    int loop, i = holo_cur(&u, &loop);
    if (i < 0) return;
    int kind = H.steps[i];
    float len = HS_LEN[kind];
    Col edge = H.wall;
    /* nearly opaque: the menu background's explosions must not pass for part of the demo */
    r_panel(x, y, w, h, edge, 0.94f);
    /* header: which demo of how many */
    r_text("HOLO-SIM", x + 16, y + 12, 9, col_a(H.accent, 0.8f), AL_LEFT);
    r_text(kind == HS_CORE && H.boss ? "THE OVERSEER" : HS_TITLE[kind], x + w - 16, y + 12, 9, col_a(col_white(H.accent, 0.4f), 0.95f), AL_RIGHT);
    for (int k = 0; k < H.nsteps; k++) {
        float px = x + 16 + text_width("HOLO-SIM", 9) + 14 + k * 13;
        bool cur = k == i;
        V2 d[4] = {v2(px, y + 11), v2(px + 4, y + 15.5f), v2(px, y + 20), v2(px - 4, y + 15.5f)};
        if (cur) r_glow(v2(px, y + 15.5f), 10, col_a(H.accent, 0.5f));
        r_polyline(d, 4, true, cur ? 2.4f : 1.6f, col_a(cur ? C_WHITE : H.accent, cur ? 1 : 0.45f));
    }
    /* the viewport */
    H.clip = (SDL_FRect){x + 8, y + 32, w - 16, h - 68};
    H.vc = v2(H.clip.x + H.clip.w * 0.5f, H.clip.y + H.clip.h * 0.5f);
    H.vh = H.clip.h;
    H.alpha = env(u, 0, len, 0.3f);
    if (kind == HS_SCAN) H.alpha = seg(u, 0, 0.3f) * (1 - seg(u, len - 0.3f, len));
    r_line(v2(x + 8, y + 32), v2(x + w - 8, y + 32), 1.2f, col_a(edge, 0.35f));
    r_line(v2(x + 8, y + h - 36), v2(x + w - 8, y + h - 36), 1.2f, col_a(edge, 0.35f));
    switch (kind) {
    case HS_SCAN: demo_scan(u); break;
    case HS_KEY: demo_key(u, loop); break;
    case HS_MINERS: demo_miners(u); break;
    case HS_CARGO: demo_cargo(u); break;
    case HS_CORE: demo_core(u); break;
    case HS_VAULT: demo_vault(u); break;
    case HS_ESCAPE: demo_escape(u); break;
    }
    g_wc.glitch = 0;
    /* scanlines and a rolling bright band */
    for (float yy = H.clip.y; yy < H.clip.y + H.clip.h; yy += 4) r_add_rect(H.clip.x, yy, H.clip.w, 1, col_a(H.accent, 0.018f));
    float band = fmodf(g_time * 70, H.clip.h + 60) - 30;
    if (band > 0 && band < H.clip.h - 10) r_add_rect(H.clip.x, H.clip.y + band, H.clip.w, 10, col_a(H.accent, 0.03f));
    /* the caption */
    char cap[96];
    demo_caption(kind, u, cap, sizeof(cap));
    r_text(cap, x + w * 0.5f, y + h - 24, 10, col_a(col_white(H.accent, 0.5f), 0.95f * env(u, 0, len, 0.25f)), AL_CENTER);
    /* the demo's progress */
    r_add_rect(x + 8, y + h - 36, (w - 16) * clampf(u / len, 0, 1), 1.5f, col_a(H.accent, 0.5f));
}

void holo_threat(int type, V2 at, float scale, float t) {
    models_init();
    wc_look(orbit(v3(0, 0, 0), t * 0.9f, 0.55f, 4.2f), v3(0, 0, 0), 30, at, scale * 2.25f);
    Col c = RDEF[type].col;
    w_grid(v3(0, -0.45f, 0), 1.4f, 0.35f, 1, col_a(c, 0.25f));
    if (type == RB_BOSS) {
        boss_draw3(v3(0, 0, 0), 0.62f, t, c, 0);
        return;
    }
    float bob = sinf(t * 2 + type) * 0.06f;
    robot_draw3(type, v3(0, bob, 0), M_ID, 0.85f, c);
}

void holo_ship(V2 at, float scale, float t, Col c, float reveal) {
    models_init();
    wc_look(orbit(v3(0, 0, 0), t * 0.7f, 0.5f, 4.2f), v3(0, 0, 0), 30, at, scale * 2.25f);
    w_grid(v3(0, -0.45f, 0), 1.5f, 0.3f, 1, col_a(c, 0.22f * clampf(reveal * 2, 0, 1)));
    ship_draw(v3(0, sinf(t * 2) * 0.05f, 0), m3_euler(0, 0, sinf(t * 1.3f) * 0.12f), 1.0f, c, 0.35f + 0.15f * sinf(t * 7), reveal);
}

/* the models, lent to the other holo screens */
void holo_model(int model, V3 pos, M3 rot, float scale, float w, Col c, float reveal) {
    models_init();
    static const WModel *const M[HM_COUNT] = {&M_GLOBE, &M_ROCK, &M_KEY, &M_SHARD};
    const WModel *m = M[clampi(model, 0, HM_COUNT - 1)];
    if (reveal < 1) wm_draw_reveal(m, pos, rot, scale, w, c, reveal);
    else wm_draw(m, pos, rot, scale, w, c);
}
void holo_ship3(V3 pos, M3 rot, float scale, Col c, float thrust, float reveal) {
    models_init();
    ship_draw(pos, rot, scale, c, thrust, reveal);
}
void holo_boss3(V3 pos, float scale, float t, Col c) {
    models_init();
    boss_draw3(pos, scale, t, c, 0);
}
void holo_reactor3(V3 pos, float scale, float t, Col c, float hp) {
    models_init();
    reactor_draw3(pos, scale, t, c, hp, 0);
}

/* ============================================================ the solar system */
typedef struct { float orbit, size, speed, phase; Col col; } Planet;
static const Planet PLANETS[8] = {
    {95, 6, 1.6f, 0.4f, {0.7f, 0.7f, 0.75f, 1}},  {140, 10, 1.2f, 2.2f, {1, 0.8f, 0.45f, 1}},  {195, 11, 1.0f, 4.1f, {0.3f, 0.75f, 1, 1}},
    {250, 8, 0.8f, 1.1f, {1, 0.45f, 0.3f, 1}},    {430, 30, 0.42f, 5.3f, {1, 0.7f, 0.45f, 1}}, {540, 25, 0.33f, 0.8f, {1, 0.85f, 0.5f, 1}},
    {630, 16, 0.26f, 3.3f, {0.5f, 1, 0.95f, 1}}, {710, 16, 0.22f, 2.5f, {0.35f, 0.55f, 1, 1}},
};
/* the Consortium's mines: moons, the belt and the outer system */
typedef struct { int parent; float orbit, speed, phase; } MineNode;
static const MineNode NODES[] = {
    {2, 24, 2.0f, 0.5f}, {3, 15, 0.0f, 1.0f}, {-1, 330, 0.6f, 2.8f}, {-1, 305, 0.62f, 4.6f}, {-1, 352, 0.58f, 0.9f}, {-1, 318, 0.6f, 6.0f},
    {4, 48, 1.6f, 0.2f}, {4, 64, 1.2f, 3.2f},    {5, 44, 1.3f, 1.7f},   {6, 30, 1.5f, 5.0f},   {7, 30, 1.2f, 2.0f},
};
#define NNODES ((int)(sizeof(NODES) / sizeof(NODES[0])))
#define NODE_CERES 2
static const Col N_OK = {0.3f, 0.9f, 1, 1}, N_BAD = {1, 0.2f, 0.25f, 1}, N_OFF = {0.45f, 0.55f, 0.7f, 0.55f};
static const V3 SIGNAL = {1500, 110, -1100};
static const V3 SIGNAL2 = {-1700, 60, -1500};

static V3 planet_pos(int i, float t) {
    float a = PLANETS[i].phase + t * PLANETS[i].speed * 0.05f;
    return v3(cosf(a) * PLANETS[i].orbit, 0, sinf(a) * PLANETS[i].orbit);
}

static V3 node_pos(int i, float t) {
    const MineNode *n = &NODES[i];
    if (n->parent < 0) {
        float a = n->phase + t * n->speed * 0.05f;
        return v3(cosf(a) * n->orbit, 4, sinf(a) * n->orbit);
    }
    float a = n->phase + t * n->speed * 0.35f;
    return v3add(planet_pos(n->parent, t), v3(cosf(a) * n->orbit, 3, sinf(a) * n->orbit));
}

static void stars(int n, float alpha, uint32_t seed) {
    float fn = g_wc.fog_near, ff = g_wc.fog_far;
    g_wc.fog_near = g_wc.fog_far = 0;
    for (int i = 0; i < n; i++) {
        V3 d = hdir(seed + i);
        float b = 0.25f + 0.75f * hf(seed + i * 5);
        float tw = 0.75f + 0.25f * sinf(g_time * (1 + 2 * hf(i)) + i);
        w_dot(v3mad(g_wc.eye, d, 9000), 1.2f + b * 1.8f, rgba(0.7f, 0.8f, 1, alpha * b * tw));
    }
    g_wc.fog_near = fn;
    g_wc.fog_far = ff;
}
void holo_stars(int n, float alpha, uint32_t seed) { stars(n, alpha, seed); }

static void system_draw(float t, const Col *ncol, float links) {
    V3 o = v3(0, 0, 0);
    Col sun = rgba(1, 0.8f, 0.35f, 1);
    w_glow(o, 170, col_a(sun, 0.22f));
    w_glow(o, 48, col_a(sun, 0.6f));
    wm_draw(&M_GLOBE, o, m3_euler(t * 0.2f, 0, 0.3f), 30, 2, sun);
    for (int k = 0; k < 16; k++) {
        float a = k * TAU / 16 + t * 0.1f;
        float r1 = 44 + 10 * hf(k + (uint32_t)(t * 8) * 16);
        V3 d = v3add(v3scale(g_wc.r, cosf(a)), v3scale(g_wc.u, sinf(a)));
        w_line(v3scale(d, 36), v3scale(d, r1), 1.6f, col_a(sun, 0.6f));
    }
    for (int i = 0; i < 8; i++) w_ring(o, v3(1, 0, 0), v3(0, 0, 1), PLANETS[i].orbit, 96, 1.2f, rgba(0.35f, 0.5f, 0.9f, 0.3f));
    for (int k = 0; k < 130; k++) {
        float a = hf(k) * TAU + t * 0.03f, r = 298 + hf(k + 300) * 62;
        w_dot(v3(cosf(a) * r, hs(k + 600) * 6, sinf(a) * r), 1.4f, rgba(0.6f, 0.6f, 0.75f, 0.55f));
    }
    for (int i = 0; i < 8; i++) {
        const Planet *pl = &PLANETS[i];
        V3 p = planet_pos(i, t);
        wm_draw(&M_GLOBE, p, m3_euler(t * 0.5f + i, 0, 0.4f), pl->size, 1.6f, pl->col);
        w_glow(p, pl->size * 2.2f, col_a(pl->col, 0.14f));
        if (i == 5)
            for (int r = 0; r < 2; r++) w_ring(p, v3(1, 0, 0), v3norm(v3(0, 0.35f, 1)), pl->size * (1.7f + r * 0.35f), 40, 1.6f, col_a(pl->col, 0.7f));
    }
    V3 earth = planet_pos(2, t);
    for (int n = 0; n < NNODES; n++) {
        V3 p = node_pos(n, t);
        Col c = ncol[n];
        if (links > 0) {
            w_line(earth, p, 1.1f, col_a(c, 0.22f * links));
            float q = fmodf(t * 0.35f + n * 0.37f, 1);
            w_dot(v3lerp(earth, p, q), 2.4f, col_a(c, 0.75f * links));
        }
        w_ring(p, g_wc.r, g_wc.u, 10, 4, 2, c);
        w_dot(p, 3.5f, col_a(col_white(c, 0.4f), c.a));
        w_glow(p, 24, col_a(c, 0.4f * c.a));
    }
}

/* ============================================================ cinema staging */
static V2 scr_c(void) { return v2(g_virt_w * 0.5f, VIRT_H * 0.5f); }

#define BAR_H 78.0f
static void letterbox(float k, const char *tag, float t) {
    if (k <= 0) return;
    float h = BAR_H * ease(k);
    r_fill_rect(0, 0, g_virt_w, h, rgba(0, 0, 0, 1));
    r_fill_rect(0, VIRT_H - h, g_virt_w, h, rgba(0, 0, 0, 1));
    Col lc = rgba(0.3f, 0.6f, 1, 0.3f * k);
    r_line(v2(0, h), v2(g_virt_w, h), 1.5f, lc);
    r_line(v2(0, VIRT_H - h), v2(g_virt_w, VIRT_H - h), 1.5f, lc);
    if (!tag) return;
    Col tc = rgba(0.45f, 0.65f, 1, 0.6f * k);
    r_text(tag, 40, h - 30, 9, tc, AL_LEFT);
    int s = (int)t;
    r_textf(g_virt_w - 40, h - 30, 9, tc, AL_RIGHT, "%02d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    if (fmodf(g_time, 1) < 0.6f) r_glow(v2(g_virt_w - 40 - text_width("00:00:00", 9) - 14, h - 26), 9, col_a(C_RED, 0.9f * k));
}

typedef struct { float t0, t1; const char *text; } Caption;

/* a typed caption in the lower bar, each line centred on its final width */
static void caption_draw(const Caption *cs, int n, float t) {
    for (int i = 0; i < n; i++) {
        const Caption *c = &cs[i];
        if (t < c->t0 || t > c->t1) continue;
        float a = 1 - seg(t, c->t1 - 0.35f, c->t1);
        int shown = (int)((t - c->t0) * 42);
        const float size = 13;
        float y = VIRT_H - BAR_H + 16;
        const char *line = c->text;
        int done = 0;
        char buf[128];
        while (line && *line) {
            const char *end = strchr(line, '\n');
            int len = end ? (int)(end - line) : (int)strlen(line);
            float w = (len * FONT_ADV - 1.6f) * size / 6;
            int k = clampi(shown - done, 0, len < 127 ? len : 127);
            memcpy(buf, line, (size_t)k);
            buf[k] = 0;
            float x = g_virt_w * 0.5f - w * 0.5f;
            r_text(buf, x, y, size, rgba(0.85f, 0.92f, 1, a), AL_LEFT);
            if (k < len && k >= 0 && shown >= done) {
                if (fmodf(g_time * 3, 1) < 0.6f) r_add_rect(x + k * FONT_ADV * size / 6, y, size * 0.6f, size, rgba(0.6f, 0.9f, 1, 0.8f * a));
                break;
            }
            done += len + 1;
            y += size * 1.6f;
            line = end ? end + 1 : NULL;
        }
    }
}

/* a full-screen flash; the bloom roughly triples a flat fill, so a is kept small */
static void screen_flash(float a, Col c) {
    if (a > 0.001f) r_add_rect(0, 0, g_virt_w, VIRT_H, col_a(c, clampf(a * 0.45f, 0, 1)));
}

/* a winding tunnel rushing at the camera; returns where the tunnel's centre is at depth z */
static V2 tcurve(float s) { return v2(150 * sinf(s * 0.0021f) + 60 * sinf(s * 0.0047f + 1), 90 * sinf(s * 0.0016f + 1.3f)); }
static V3 tunnel_point(float dist, float z) {
    V2 o = v2sub(tcurve(dist + z), tcurve(dist));
    return v3(o.x, o.y, z);
}
static void tunnel_draw(float dist, Col c0, Col c1, float alpha, float fire, float whiteout) {
    enum { N = 26, SIDES = 10 };
    const float S = 70, R = 115;
    V3 ahead = tunnel_point(dist, 260);
    wc_look(v3(0, 0, 0), ahead, 80, scr_c(), VIRT_H);
    wc_roll(0.25f * sinf(dist * 0.0013f));
    g_wc.alpha = alpha;
    g_wc.fog_near = 80;
    g_wc.fog_far = N * S;
    float base = fmodf(dist, S);
    static V3 ring[N][SIDES];
    for (int k = 0; k < N; k++) {
        float z = (k + 1) * S - base, zz = dist + z;
        V3 c = tunnel_point(dist, z);
        float rot = zz * 0.0012f;
        for (int j = 0; j < SIDES; j++) {
            float a = rot + TAU * j / SIDES;
            ring[k][j] = v3(c.x + cosf(a) * R, c.y + sinf(a) * R, z);
        }
        bool lit = ((int)floorf(zz / S)) % 5 == 0;
        Col col = col_lerp(c0, c1, (float)k / N);
        if (lit) col = col_white(col, 0.5f);
        if (fire > 0 && k < 9) col = col_lerp(col, col_white(C_ORANGE, 0.2f), fire * (1 - k / 9.0f));
        col = col_white(col, whiteout);
        w_poly(ring[k], SIDES, true, lit ? 3.4f : 2.2f, col);
    }
    for (int j = 0; j < SIDES; j++) {
        V3 pts[N];
        for (int k = 0; k < N; k++) pts[k] = ring[k][j];
        w_poly(pts, N, false, 1.4f, col_a(col_white(c0, whiteout), 0.55f));
    }
}

static void tunnel_ship(float dist, float t, float thrust) {
    V3 p = tunnel_point(dist, 150);
    p.y -= 30;
    V3 d = v3sub(tunnel_point(dist, 190), tunnel_point(dist, 110));
    float roll = clampf(-d.x * 0.01f, -0.6f, 0.6f);
    float pitch = clampf(d.y * 0.004f, -0.3f, 0.3f);
    ship_draw(v3add(p, v3(sinf(t * 3) * 3, sinf(t * 4.3f) * 2, 0)), m3_euler(PI / 2, pitch, roll), 20, rgba(0.85f, 0.95f, 1, 1), thrust, 1);
}

/* ============================================================ the intro */
static float intro_node_k(int n, float t) {
    const float T0 = 5.6f, SPEED = 520;
    return clampf(((t - T0) * SPEED - v3dist(SIGNAL, node_pos(n, t))) / 70, 0, 1);
}

static void intro_system(float t, float a) {
    V3 tgt = v3(60, 0, 0);
    float pitch = lerpf(1.0f, 0.46f, ease(seg(t, 0, 9.5f)));
    float dist = lerpf(1450, 960, ease(seg(t, 0, 10.4f)));
    wc_look(orbit(tgt, 0.35f + t * 0.025f, pitch, dist), tgt, 45, scr_c(), VIRT_H);
    g_wc.alpha = a;
    stars(260, 1, 11);
    Col nc[NNODES];
    for (int n = 0; n < NNODES; n++) {
        float k = intro_node_k(n, t);
        float flash = k > 0 && k < 0.5f ? 1 - fabsf(k - 0.25f) * 4 : 0;
        nc[n] = col_white(col_lerp(N_OK, N_BAD, k), clampf(flash, 0, 1));
    }
    system_draw(t, nc, 1);
    /* the signal: a red star beyond Neptune, then a wavefront sweeping the system */
    if (t > 4.6f) {
        float p = 0.5f + 0.5f * sinf(g_time * 9);
        w_dot(SIGNAL, 4 + 3 * p, col_a(N_BAD, seg(t, 4.6f, 5.0f)));
        w_glow(SIGNAL, 90, col_a(N_BAD, 0.25f * seg(t, 4.6f, 5.0f)));
    }
    if (t > 5.6f) {
        float R = (t - 5.6f) * 520;
        float fa = 0.7f * (1 - seg(R, 1800, 3400));
        w_ring(SIGNAL, v3(1, 0, 0), v3(0, 0, 1), R, 128, 3, col_a(N_BAD, fa));
        w_ring(SIGNAL, v3(1, 0, 0), v3(0, 1, 0), R, 128, 1.6f, col_a(N_BAD, fa * 0.5f));
        w_ring(SIGNAL, v3(0, 0, 1), v3(0, 1, 0), R, 128, 1.6f, col_a(N_BAD, fa * 0.5f));
        w_ring(SIGNAL, v3(1, 0, 0), v3(0, 0, 1), R * 0.93f, 128, 1.4f, col_a(N_BAD, fa * 0.4f));
    }
    g_wc.glitch = 0;
    /* the title card */
    float k = seg(t, 0.4f, 1.4f), fa = a * (1 - seg(t, 3.6f, 4.4f));
    if (t < 4.4f) r_text_reveal("YEAR 2187", g_virt_w * 0.5f, VIRT_H * 0.5f - 24, 34, col_a(N_OK, fa), AL_CENTER, k, true);
}

static void intro_robots(float u, float a) {
    V3 tgt = v3(u * 14 - 30, 30, 0);
    V3 eye = v3(-120 + u * 30, 58, 250 - u * 18);
    wc_look(eye, tgt, 46, scr_c(), VIRT_H);
    g_wc.alpha = a;
    g_wc.fog_near = 220;
    g_wc.fog_far = 900;
    static const int T[5] = {RB_DRONE, RB_HULK, RB_SPIDER, RB_SUPERHULK, RB_DRILLER};
    static const float S[5] = {34, 42, 38, 50, 40};
    float gl = 0;
    for (int i = 0; i < 5; i++) gl = maxf(gl, env(u, 1.7f + i * 0.3f, 2.05f + i * 0.3f, 0.05f));
    g_wc.glitch = gl * 0.6f;
    g_wc.glitch_seed = (uint32_t)(g_time * 40);
    float alarm = u > 1.6f ? 0.5f + 0.5f * sinf(u * 6) : 0;
    Col fc = col_lerp(rgba(0.25f, 0.45f, 0.9f, 1), rgba(0.9f, 0.2f, 0.3f, 1), alarm * 0.7f);
    w_grid(v3(0, 0, -40), 720, 50, 1.2f, col_a(fc, 0.45f));
    for (int k = 0; k < 5; k++) {
        float x = -440 + k * 220;
        w_box(v3(x - 22, 0, -200), v3(x + 22, 140, -160), 2, col_a(fc, 0.8f));
        w_line(v3(x, 140, -180), v3(x + 220, 140, -180), 1.5f, col_a(fc, 0.5f));
    }
    for (int i = 0; i < 5; i++) {
        float x = -300 + i * 150, wk = u - (1.7f + i * 0.3f);
        Col idle = rgba(0.45f, 0.65f, 0.8f, 0.6f);
        Col c = col_lerp(idle, RDEF[T[i]].col, seg(wk, 0, 0.35f));
        if (wk > 0 && wk < 0.35f && hf((uint32_t)(u * 30) + i * 17) > 0.5f) c = col_white(c, 0.7f);
        float head = -PI / 2 + sinf(u * 1.3f + i) * 0.25f;
        if (wk > 0.3f) head = lerpf(head, PI / 2 + sinf(u + i) * 0.1f, ease(seg(wk, 0.3f, 0.9f)));
        V3 p = v3(x, 28 + sinf(u * 2 + i) * 2, maxf(0, wk - 0.9f) * 34);
        M3 rot = m3_euler(head, 0, 0);
        robot_draw3(T[i], p, rot, S[i], c);
        if (wk <= 0) {
            /* still digging: sparks off the rock face */
            if (hf((uint32_t)(u * 9) + i * 5) > 0.6f) sparks3(mx(p, rot, S[i], v3(1.1f, 0, 0)), fmodf(u * 9, 1) * 0.4f, 5, 120, C_ORANGE, (uint32_t)(u * 9) + i);
        } else {
            V3 e = mx(p, rot, S[i], v3(0.75f, 0.3f, 0));
            w_glow(e, 16, col_a(C_RED, 0.9f * seg(wk, 0, 0.3f)));
            w_dot(e, 2.5f, col_a(C_WHITE, seg(wk, 0, 0.3f)));
        }
    }
    /* the order arriving: a red scan across the hall */
    if (u > 1.5f && u < 3.4f) {
        float x = -320 + (u - 1.5f) * 380;
        w_line(v3(x, 0, -120), v3(x, 0, 120), 3, col_a(N_BAD, 0.9f));
        w_line(v3(x, 0, -120), v3(x, 160, -120), 2, col_a(N_BAD, 0.5f));
        w_line(v3(x, 160, -120), v3(x, 160, 120), 1.4f, col_a(N_BAD, 0.3f));
    }
    if (u > 1.6f) {
        float r = fmodf(u - 1.6f, 1.2f) / 1.2f;
        w_ring(v3(0, 1, 0), v3(1, 0, 0), v3(0, 0, 1), 40 + r * 500, 64, 2.4f, col_a(N_BAD, 0.5f * (1 - r)));
    }
}

static void intro_pilot(float u, float a) {
    if (u < 3.0f) {
        V3 tgt = v3(0, 0, 0);
        float yaw = 0.55f + u * 0.12f;
        V3 eye = orbit(tgt, yaw, 0.3f, 300);
        wc_look(eye, tgt, 40, scr_c(), VIRT_H);
        g_wc.alpha = a;
        Col bc = rgba(0.3f, 0.6f, 1, 1);
        w_grid(v3(0, -26, 0), 300, 40, 1, col_a(bc, 0.35f));
        w_ring(v3(0, -26, 0), v3(1, 0, 0), v3(0, 0, 1), 120, 64, 2, col_a(N_OK, 0.6f));
        for (int k = 0; k < 36; k++) {
            float ang = k * TAU / 36 + u * 0.4f;
            float l = k % 3 == 0 ? 14 : 7;
            w_line(v3(cosf(ang) * 120, -26, sinf(ang) * 120), v3(cosf(ang) * (120 + l), -26, sinf(ang) * (120 + l)), 1.4f, col_a(N_OK, 0.5f));
        }
        float launch = seg(u, 2.2f, 3.0f);
        /* the turntable spins it, then it turns its tail to the camera */
        float away = atan2f(-eye.z, -eye.x), h0 = 0.6f + 1.9f * 0.9f;
        float head = u < 1.9f ? 0.6f + u * 0.9f : h0 + wrap_angle(away - h0) * ease(seg(u, 1.9f, 2.3f));
        V3 fwd = v3(cosf(head), 0, sinf(head));
        V3 p = v3add(v3mad(v3(0, 0, 0), fwd, ease_in(launch) * 1400), v3(0, launch * 60, 0));
        float thrust = seg(u, 1.8f, 2.2f) * (0.6f + launch * 2.5f);
        float rev = seg(u, 0.1f, 1.7f);
        ship_draw(p, m3_euler(head, launch * 0.15f, 0), 64, rgba(0.8f, 0.95f, 1, 1), thrust, rev);
        /* blueprint callout */
        V2 nose;
        float la = a * seg(u, 0.8f, 1.2f) * (1 - seg(u, 2.2f, 2.5f));
        if (la > 0 && wc_project(mx(p, m3_euler(head, 0, 0), 64, v3(1.3f, 0.1f, 0)), &nose, NULL)) {
            V2 lab = v2(g_virt_w * 0.5f + 230, VIRT_H * 0.5f - 130);
            Col lc = col_a(N_OK, la);
            r_line(nose, v2(lab.x - 12, lab.y + 14), 1.5f, col_a(lc, 0.6f));
            r_line(v2(lab.x - 12, lab.y + 14), v2(lab.x + 170, lab.y + 14), 1.5f, col_a(lc, 0.6f));
            r_text_glow("WRAITH-7", lab.x, lab.y - 8, 16, lc, AL_LEFT);
            r_text("CONTRACT ASSAULT CRAFT", lab.x, lab.y + 22, 8, col_a(lc, 0.8f), AL_LEFT);
        }
        screen_flash(0.3f * env(u, 2.3f, 3.0f, 0.3f), N_OK);
    } else {
        float v = u - 3.0f;
        float dist = v * 950 + v * v * 260;
        tunnel_draw(dist, N_OK, rgba(0.55f, 0.35f, 1, 1), a, 0, ease_in(seg(u, 5.2f, 6.0f)));
        tunnel_ship(dist, u, 1);
        screen_flash(0.4f * (1 - seg(v, 0, 0.3f)), C_WHITE);
    }
}

/* the logo: every letter flies in from the dark and lands where the title screen draws it */
static void logo_line(const char *s, float y, float size, Col c, float u, int *gi) {
    const float D = 900;
    float cx = g_virt_w * 0.5f, cy = VIRT_H * 0.5f, uu = size / 6;
    int len = (int)strlen(s);
    float lx = cx - (len * FONT_ADV - 1.6f) * uu * 0.5f;
    float lw = maxf(2.2f, size * 0.30f);
    for (int i = 0; i < len; i++) {
        if (s[i] == ' ') continue;
        int id = (*gi)++;
        float ti = 0.15f + id * 0.085f;
        float e = ease_out(seg(u, ti, ti + 1.1f));
        if (e <= 0) continue;
        float gx = lx + i * FONT_ADV * uu;
        V2 gc = v2(gx + 2 * uu, y + 3 * uu);
        float z = (1 - e) * 4200, spin = (1 - e) * (id & 1 ? -1 : 1) * PI * 1.4f;
        float dx = (1 - e) * hs(id + 3) * 500, dy = (1 - e) * hs(id + 9) * 300;
        float cs = cosf(spin), sn = sinf(spin);
        const V2 *gp;
        const int *st, *ln;
        int ns = font_glyph((unsigned char)s[i], &gp, &st, &ln);
        Col cc = col_a(c, seg(e, 0, 0.3f));
        for (int k = 0; k < ns; k++) {
            V2 pts[40];
            float kk = 1;
            for (int j = 0; j < ln[k]; j++) {
                V2 q = gp[st[k] + j];
                float lxp = gx + q.x * uu - gc.x, lyp = y + q.y * uu - gc.y;
                float X = gc.x - cx + dx + lxp * cs, Y = gc.y - cy + dy + lyp, Z = z + lxp * sn;
                kk = D / (D + Z);
                pts[j] = v2(cx + X * kk, cy + Y * kk);
            }
            if (ln[k] == 1) {
                r_line(pts[0], pts[0], lw * kk, cc);
                continue;
            }
            r_polyline(pts, ln[k], false, lw * 2.6f * kk, col_a(cc, 0.35f));
            r_polyline(pts, ln[k], false, lw * kk, cc);
        }
    }
}

static void intro_logo(float u) {
    float cx = g_virt_w * 0.5f, y = 96;
    Col c1 = rgba(0.3f, 0.9f, 1, 1);
    Col c2 = col_lerp(rgba(1, 0.3f, 0.85f, 1), rgba(0.7f, 0.4f, 1, 1), 0.5f + 0.5f * sinf(g_time * 0.8f));
    int gi = 0;
    logo_line("BALLAST", y + 36, 110, c2, u, &gi);
    float w = text_width("BALLAST", 110) * 0.5f + 30;
    float k = ease_out(seg(u, 1.9f, 2.5f));
    if (k > 0) r_line(v2(cx - w * k, y + 170), v2(cx + w * k, y + 170), 4, col_a(c1, 0.7f));
    if (u > 2.5f) r_glow(v2(cx - w + fmodf(g_time * 300, w * 2), y + 170), 22, col_a(C_WHITE, 0.35f * seg(u, 2.5f, 2.8f)));
    Col sc = powerup_color(PU_SALVAGE);
    if (u > 2.4f) r_text_reveal("GREED HAS MASS", cx, 284, 15, col_a(sc, 0.95f), AL_CENTER, seg(u, 2.4f, 3.2f), true);
    if (u > 3.0f)
        r_text("LOOT THE MINE.  BLOW THE REACTOR.  ESCAPE - IF YOU CAN STILL FLY.", cx, 310, 10, rgba(0.6f, 0.75f, 1, 0.75f * seg(u, 3.0f, 3.6f)),
               AL_CENTER);
    screen_flash(0.55f * (1 - seg(u, 0, 0.5f)), C_WHITE);
}

static const Caption INTRO_CAPS[] = {
    {1.6f, 5.3f, "THE HELIX DEEP MINING CONSORTIUM RUNS\nAUTOMATED MINES ACROSS THE SOLAR SYSTEM."},
    {5.7f, 10.2f, "SEVENTY HOURS AGO AN UNKNOWN SIGNAL\nREACHED EVERY MINING ROBOT AT ONCE."},
    {11.0f, 12.9f, "THEY STOPPED DIGGING."},
    {12.9f, 15.4f, "THEN THEY STARTED KILLING."},
    {16.0f, 18.6f, "YOU ARE A CONTRACT PILOT."},
    {18.8f, 21.4f, "FLY IN. BREAK THE REACTOR. GET OUT ALIVE."},
};

void cine_intro_update(float t) {
    models_init();
    float prev = cue_now;
    cue_step(t);
    if (cue(0.45f)) snd_play(SND_BEEP, 0.3f, 0.8f);
    if (cue(4.7f)) snd_play(SND_CLOAK, 0.5f, 0.6f);
    if (cue(5.6f)) snd_play(SND_TELEPORT, 0.6f, 0.5f);
    if (cue_live)
        for (int n = 0; n < NNODES; n++)
            if (intro_node_k(n, prev) <= 0 && intro_node_k(n, t) > 0) snd_play(SND_BEEP, 0.3f, 1.5f - n * 0.07f);
    for (int i = 0; i < 5; i++)
        if (cue(10.4f + 1.7f + i * 0.3f)) snd_play(SND_SCREECH, 0.3f, 0.85f + i * 0.08f);
    if (cue(12.0f)) snd_play(SND_ALARM, 0.35f, 1.0f);
    if (cue(15.7f)) snd_play(SND_POWERUP, 0.4f, 0.7f);
    if (cue(17.4f)) snd_play(SND_BURNER, 0.5f, 1.0f);
    if (cue(17.8f)) snd_play(SND_TELEPORT, 0.55f, 1.1f);
    if (cue(18.6f)) snd_play(SND_TELEPORT, 0.5f, 1.5f);
    if (cue(21.6f)) snd_play(SND_EXPL_L, 0.6f, 0.8f);
    if (cue(23.9f)) snd_play(SND_POWERUP, 0.5f, 1.0f);
    if (cue(24.1f)) snd_play(SND_TALLY, 0.5f, 1.0f);
}

void cine_intro_draw(float t) {
    if (t < 10.4f) intro_system(t, seg(t, 0, 1.2f) * (1 - seg(t, 9.9f, 10.4f)));
    else if (t < 15.6f) intro_robots(t - 10.4f, env(t, 10.4f, 15.6f, 0.4f));
    else if (t < 21.6f) intro_pilot(t - 15.6f, seg(t, 15.6f, 16.0f));
    g_wc.glitch = 0;
    letterbox(1 - seg(t, 21.3f, 21.9f), "HELIX CONSORTIUM ARCHIVE", t);
    caption_draw(INTRO_CAPS, (int)(sizeof(INTRO_CAPS) / sizeof(INTRO_CAPS[0])), t);
    if (t >= 21.6f) intro_logo(t - 21.6f);
}

/* ============================================================ the ending */
static void end_overseer(float u, float a) {
    V3 tgt = v3(0, 40, 0), core = v3(0, 50, 0);
    V3 eye = orbit(tgt, 0.5f + u * 0.07f, 0.32f, lerpf(560, 430, ease(seg(u, 0, 5.5f))));
    float sh = u > 2.2f ? 14 * (1 - seg(u, 2.2f, 3.4f)) : seg(u, 0.3f, 2.2f) * 3;
    eye = v3add(eye, v3(sinf(g_time * 53) * sh, sinf(g_time * 47) * sh, 0));
    wc_look(eye, tgt, 45, scr_c(), VIRT_H);
    g_wc.alpha = a;
    g_wc.fog_near = 300;
    g_wc.fog_far = 1400;
    Col fl = rgba(0.32f, 0.1f, 0.58f, 1), bc = RDEF[RB_BOSS].col;
    w_grid(v3(0, 0, 0), 700, 60, 1.2f, col_a(col_white(fl, 0.3f), 0.6f));
    w_ring(v3(0, 0, 0), v3(1, 0, 0), v3(0, 0, 1), 420, 72, 2, col_a(ZONES[2].wall, 0.6f));
    for (int k = 0; k < 6; k++) {
        V2 p = v2scale(v2fromang(k * TAU / 6 + 0.3f), 330);
        w_box(v3(p.x - 24, 0, p.y - 24), v3(p.x + 24, 90, p.y + 24), 2, col_a(ZONES[2].wall, 0.8f));
    }
    if (u < 2.2f) {
        float flash = hf((uint32_t)(u * 14)) > 0.7f ? 0.8f : 0;
        boss_draw3(core, 70, g_time, col_white(bc, flash), seg(u, 0.3f, 2.2f) * 10);
        for (int k = 0; k < 6; k++) {
            float tk = 0.4f + k * 0.3f;
            if (u > tk && u < tk + 0.6f) {
                V3 p = v3mad(core, hdir(k + 50), 65);
                w_glow(p, 70 * (1 - (u - tk) / 0.6f), col_a(C_ORANGE, 0.8f));
                sparks3(p, u - tk, 12, 260, C_ORANGE, k * 13);
            }
        }
    } else {
        float te = u - 2.2f;
        wm_draw_explode(&M_BOSS, core, m3_euler(-2.2f * 0.9f, 0, 0), 70, 2.8f, bc, te * 0.8f, 91);
        for (int r = 0; r < 3; r++) {
            float R = ease_out(seg(te, r * 0.12f, 1.4f + r * 0.12f)) * (700 - r * 150);
            V3 ay = r == 0 ? v3(0, 0, 1) : r == 1 ? v3norm(v3(0, 1, 0.4f)) : v3norm(v3(0.5f, 1, -0.3f));
            w_ring(core, v3(1, 0, 0), ay, R, 72, 4 - r, col_a(col_white(bc, 0.5f), 1 - seg(te, 0, 1.6f)));
        }
        sparks3(core, te * 0.9f, 60, 800, col_white(bc, 0.3f), 1);
        w_glow(core, 170 * (1 - seg(te, 0, 1.0f)), col_a(C_WHITE, 0.45f));
        w_glow(core, 260 * (1 - seg(te, 0, 1.6f)), col_a(bc, 0.2f));
        w_glow(core, 60, col_a(C_ORANGE, 0.3f * (1 - seg(te, 1, 3))));
    }
    /* the ship keeps circling, then peels away for the exit */
    float ang = 1.2f + u * 0.55f, rad = 290 + maxf(0, u - 2.9f) * maxf(0, u - 2.9f) * 160;
    V3 sp = v3(cosf(ang) * rad, 40 + maxf(0, u - 2.9f) * 20, sinf(ang) * rad);
    float head = ang + PI / 2 - clampf((u - 2.9f) * 0.6f, 0, 0.7f);
    ship_draw(sp, m3_euler(head, 0, -0.35f), 20, rgba(0.85f, 0.95f, 1, 1), 0.7f, 1);
    if (u > 2.2f) screen_flash(0.5f * (1 - seg(u, 2.2f, 2.55f)) * a, C_WHITE);
}

static void end_escape(float u, float a) {
    Col c0 = ZONES[2].wall, c1 = rgba(0.55f, 0.2f, 0.9f, 1);
    if (u < 3.4f) {
        float dist = u * 1150 + u * u * 140;
        tunnel_draw(dist, c0, c1, a, 0.6f + 0.4f * sinf(u * 9), 0.7f * ease_in(seg(u, 2.9f, 3.4f)));
        tunnel_ship(dist, u, 1.2f);
        screen_flash(0.08f * (0.5f + 0.5f * sinf(u * 11)), C_ORANGE);
        char buf[16];
        snprintf(buf, sizeof(buf), "0:%04.1f", maxf(0, 3.3f - u));
        if (fmodf(g_time * 4, 1) < 0.65f) r_text_glow(buf, g_virt_w * 0.5f, BAR_H + 22, 22, col_a(C_RED, a), AL_CENTER);
        r_text("SELF-DESTRUCT", g_virt_w * 0.5f, BAR_H + 56, 9, col_a(C_RED, 0.8f * a), AL_CENTER);
        return;
    }
    float v = u - 3.4f;
    V3 tgt = v3(0, 0, 0);
    V3 eye = v3(-210 - v * 20, 90, 640 + v * 30);
    float sh = v > 1.0f ? 10 * (1 - seg(v, 1.0f, 2.2f)) : 0;
    eye = v3add(eye, v3(sinf(g_time * 51) * sh, sinf(g_time * 43) * sh, 0));
    wc_look(eye, tgt, 45, scr_c(), VIRT_H);
    g_wc.alpha = a;
    stars(240, 1, 23);
    Col rc = col_lerp(c0, C_GREY, 0.3f);
    M3 rr = m3_euler(g_time * 0.15f, 0.2f, 0.1f);
    if (v < 1.0f) {
        float crack = seg(v, 0.2f, 1.0f);
        wm_draw(&M_ROCK, tgt, rr, 150, 2, col_white(rc, crack * (hf((uint32_t)(g_time * 20)) > 0.5f ? 0.8f : 0.2f)));
        w_glow(tgt, 200, col_a(C_ORANGE, 0.18f * crack));
    } else {
        float te = v - 1.0f;
        wm_draw_explode(&M_ROCK, tgt, rr, 150, 2.2f, rc, te * 0.55f, 7);
        for (int r = 0; r < 2; r++) {
            float R = ease_out(seg(te, r * 0.15f, 1.6f)) * (900 - r * 300);
            w_ring(tgt, v3(1, 0, 0), r ? v3norm(v3(0, 1, 0.3f)) : v3(0, 0, 1), R, 96, 4, col_a(col_white(C_ORANGE, 0.4f), 1 - seg(te, 0, 1.8f)));
        }
        sparks3(tgt, te * 0.8f, 60, 900, C_ORANGE, 3);
        w_glow(tgt, 220 * (1 - seg(te, 0, 1.2f)), col_a(C_WHITE, 0.4f));
        w_glow(tgt, 380 * (1 - seg(te, 0, 2.0f)), col_a(C_ORANGE, 0.2f));
        screen_flash(0.45f * (1 - seg(te, 0, 0.35f)), C_WHITE);
    }
    /* the ship streaks out of the dust toward the camera */
    V3 from = v3(0, 20, 170), to = v3(-120, 70, 560);
    float s = ease_out(seg(v, 0, 2.6f));
    V3 p = v3lerp(from, to, s);
    V3 d = v3sub(to, from);
    ship_draw(p, m3_euler(atan2f(d.z, d.x), 0.12f, -0.3f), 20, rgba(0.85f, 0.95f, 1, 1), 1, 1);
    screen_flash(0.5f * (1 - seg(v, 0, 0.3f)), C_WHITE);
}

static float end_node_k(int n, float u) {
    float t = 400 + u;
    float R = (u - 0.6f) * 380;
    return clampf((R - v3dist(node_pos(n, t), node_pos(NODE_CERES, t))) / 80, 0, 1);
}

static void end_system(float u, float a) {
    float t = 400 + u;
    V3 tgt = v3(60, 0, 0);
    wc_look(orbit(tgt, 1.1f + u * 0.03f, lerpf(0.75f, 0.55f, ease(seg(u, 0, 7))), lerpf(1350, 1120, ease(seg(u, 0, 7)))), tgt, 45, scr_c(), VIRT_H);
    g_wc.alpha = a;
    stars(260, 1, 11);
    Col nc[NNODES];
    for (int n = 0; n < NNODES; n++) {
        float k = end_node_k(n, u);
        float flash = k > 0 && k < 0.5f ? 1 - fabsf(k - 0.25f) * 4 : 0;
        nc[n] = col_white(col_lerp(N_BAD, N_OFF, k), clampf(flash, 0, 1));
    }
    system_draw(t, nc, 1);
    V3 ceres = node_pos(NODE_CERES, t);
    if (u > 0.6f) {
        float R = (u - 0.6f) * 380;
        w_ring(ceres, v3(1, 0, 0), v3(0, 0, 1), R, 128, 2.6f, col_a(N_OK, 0.6f * (1 - seg(R, 900, 1800))));
    }
    /* the ship's long way home */
    V3 earth = planet_pos(2, t);
    float s = ease(seg(u, 0.4f, 6.6f));
    V3 pts[40];
    int n = 0;
    for (int k = 0; k < 40; k++) {
        float q = s - 0.25f + 0.25f * k / 39.0f;
        if (q < 0) continue;
        pts[n++] = v3add(v3lerp(ceres, earth, q), v3(0, sinf(q * PI) * 90, 0));
    }
    if (n > 1) {
        w_poly(pts, n, false, 2.2f, col_a(C_WHITE, 0.8f));
        w_dot(pts[n - 1], 4, C_WHITE);
        w_glow(pts[n - 1], 20, col_a(N_OK, 0.5f));
    }
}

static void end_home(float u, float a) {
    V3 earth = v3(0, 0, 0), station = v3(330, 60, -150);
    V3 tgt = v3lerp(v3(120, 30, 60), station, 0.55f);
    wc_look(orbit(tgt, 0.95f + u * 0.04f, 0.22f, lerpf(620, 470, ease(seg(u, 0, 6.5f)))), tgt, 45, scr_c(), VIRT_H);
    g_wc.alpha = a;
    stars(260, 1, 31);
    Col ec = rgba(0.3f, 0.75f, 1, 1);
    wm_draw(&M_GLOBE, earth, m3_euler(g_time * 0.08f, 0, 0.4f), 170, 2, ec);
    w_ring(earth, g_wc.r, g_wc.u, 182, 72, 3, col_a(ec, 0.35f));
    w_glow(earth, 300, col_a(ec, 0.12f));
    wm_draw(&M_GLOBE, v3(-380, 60, -260), m3_euler(g_time * 0.1f, 0, 0.2f), 34, 1.6f, col_a(C_GREY, 0.8f));
    /* the station: a turning ring on spokes */
    float sr = g_time * 0.25f;
    V3 n = v3norm(v3add(v3norm(v3sub(g_wc.eye, station)), v3(0.35f, 0.5f, 0)));
    V3 b0 = v3norm(v3cross(n, v3(0, 1, 0))), b1 = v3cross(n, b0);
    V3 ax = v3add(v3scale(b0, cosf(sr)), v3scale(b1, sinf(sr))), ay = v3add(v3scale(b1, cosf(sr)), v3scale(b0, -sinf(sr)));
    Col stc = rgba(0.8f, 0.9f, 1, 1);
    w_ring(station, ax, ay, 60, 40, 2.4f, stc);
    w_ring(station, ax, ay, 50, 40, 1.6f, col_a(stc, 0.6f));
    for (int k = 0; k < 6; k++) {
        float an = k * TAU / 6;
        V3 d = v3add(v3scale(ax, cosf(an)), v3scale(ay, sinf(an)));
        w_line(v3mad(station, d, 10), v3mad(station, d, 50), 1.4f, col_a(stc, 0.6f));
        bool on = fmodf(g_time * 1.5f + k * 0.17f, 1) < 0.3f;
        w_dot(v3mad(station, d, 60), 2.6f, on ? (u > 3.6f ? C_GREEN : C_RED) : col_a(stc, 0.3f));
    }
    w_ring(station, ax, ay, 10, 12, 2, stc);
    /* the ship comes in to dock */
    V3 from = v3add(station, v3(-420, 90, 380));
    float s = ease(seg(u, 0.3f, 3.6f));
    V3 p = v3lerp(from, station, s);
    V3 d = v3sub(station, from);
    if (s < 1) ship_draw(p, m3_euler(atan2f(d.z, d.x), -0.1f, 0), 16, rgba(0.85f, 0.95f, 1, 1), 0.6f * (1 - s) + 0.1f, 1);
    if (u > 3.6f && u < 4.8f) w_ring(station, ax, ay, 12 + (u - 3.6f) * 120, 40, 2.4f, col_a(C_GREEN, 1 - (u - 3.6f) / 1.2f));
    /* what the run brought home */
    const char *lab[3] = {"MINERS BROUGHT HOME", "SALVAGE HAULED", "ROBOTS DESTROYED"};
    int val[3] = {R.rescued, R.salvage_total, R.kills};
    for (int i = 0; i < 3; i++) {
        float k = seg(u, 3.9f + i * 0.35f, 4.5f + i * 0.35f);
        if (k <= 0) continue;
        float x = g_virt_w - 70, y = BAR_H + 50 + i * 58;
        char vb[24];
        snprintf(vb, sizeof(vb), "%d", val[i]);
        r_text_reveal(lab[i], x, y, 9, col_a(N_OK, 0.8f * a), AL_RIGHT, k, false);
        r_text_reveal(vb, x, y + 16, 22, col_a(i == 1 ? powerup_color(PU_SALVAGE) : C_WHITE, a), AL_RIGHT, k, true);
    }
}

static void end_signal(float u, float a) {
    float t = 407 + u;
    V3 tgt = v3lerp(v3(60, 0, 0), v3(-500, 0, -600), ease(seg(u, 0, 4.5f)));
    wc_look(orbit(tgt, 1.3f + u * 0.02f, lerpf(0.55f, 0.3f, ease(seg(u, 0, 5))), lerpf(1150, 2300, ease(seg(u, 0, 4.5f)))), tgt, 45, scr_c(), VIRT_H);
    g_wc.alpha = a;
    stars(260, 1, 11);
    Col nc[NNODES];
    for (int n = 0; n < NNODES; n++) nc[n] = N_OFF;
    system_draw(t, nc, 0.4f);
    if (u > 2.4f) {
        Col pc = rgba(0.75f, 0.3f, 1, 1);
        float p = 0.5f + 0.5f * sinf(g_time * 5);
        float in = seg(u, 2.4f, 3.0f);
        w_dot(SIGNAL2, 3 + 2 * p, col_a(pc, in));
        w_glow(SIGNAL2, 120, col_a(pc, 0.25f * in));
        for (int k = 0; k < 3; k++) {
            float r = fmodf((u - 2.4f) * 110 + k * 90, 270);
            w_ring(SIGNAL2, g_wc.r, g_wc.u, r, 48, 2, col_a(pc, in * (1 - r / 270)));
        }
        g_wc.glitch = 0.5f * env(u, 3.0f, 3.3f, 0.05f) + 0.4f * env(u, 4.4f, 4.6f, 0.05f);
        g_wc.glitch_seed = (uint32_t)(g_time * 40);
    }
}

static const Caption END_CAPS[] = {
    {0.6f, 5.2f, "THE OVERSEER IS SCRAP."},
    {5.9f, 8.8f, "ONE LAST COUNTDOWN."},
    {9.2f, 11.3f, "CERES TEARS ITSELF APART BEHIND YOU."},
    {12.0f, 18.2f, "ACROSS THE SOLAR SYSTEM THE INFECTED\nROBOTS FALL SILENT ONE BY ONE."},
    {19.0f, 24.7f, "THE CONSORTIUM THANKS YOU FOR YOUR SERVICE -\nAND FOR THE MINERS YOU BROUGHT HOME ALIVE."},
    {25.4f, 30.8f, "BUT DEEP-SPACE LISTENING POSTS HAVE PICKED UP\nA NEW SIGNAL FROM BEYOND THE ORBIT OF NEPTUNE..."},
};

void cine_ending_update(float t) {
    models_init();
    float prev = cue_now;
    cue_step(t);
    for (int k = 0; k < 6; k++)
        if (cue(0.4f + k * 0.3f)) snd_play(SND_EXPL_M, 0.35f, 0.9f + k * 0.08f);
    if (cue(2.2f)) {
        snd_play(SND_EXPL_L, 0.9f, 0.7f);
        snd_play(SND_EXPL_L, 0.6f, 1.1f);
    }
    if (cue(5.6f)) snd_play(SND_ALARM, 0.4f, 1.0f);
    if (cue(8.9f)) snd_play(SND_TELEPORT, 0.6f, 0.8f);
    if (cue(9.9f)) snd_play(SND_EXPL_L, 0.9f, 0.6f);
    if (cue_live && t > 11.5f && t < 18.5f)
        for (int n = 0; n < NNODES; n++)
            if (end_node_k(n, prev - 11.5f) <= 0 && end_node_k(n, t - 11.5f) > 0) snd_play(SND_BEEP, 0.25f, 1.2f - n * 0.05f);
    if (cue(22.1f)) snd_play(SND_POWERUP, 0.5f, 1.0f);
    for (int i = 0; i < 3; i++)
        if (cue(18.5f + 3.9f + i * 0.35f)) snd_play(SND_TALLY, 0.5f, 1.0f);
    if (cue(27.4f)) snd_play(SND_CLOAK, 0.5f, 0.5f);
    if (cue(28.0f)) snd_play(SND_TELEPORT, 0.5f, 0.4f);
}

void cine_ending_draw(float t) {
    if (t < 5.5f) end_overseer(t, seg(t, 0, 0.5f) * (1 - seg(t, 5.2f, 5.5f)));
    else if (t < 11.5f) end_escape(t - 5.5f, seg(t, 5.5f, 5.7f) * (1 - seg(t, 11.1f, 11.5f)));
    else if (t < 18.5f) end_system(t - 11.5f, env(t, 11.5f, 18.5f, 0.5f));
    else if (t < 25.0f) end_home(t - 18.5f, env(t, 18.5f, 25.0f, 0.5f));
    else if (t < ENDING_LEN) end_signal(t - 25.0f, seg(t, 25.0f, 25.5f) * (1 - seg(t, ENDING_LEN - 0.6f, ENDING_LEN)));
    g_wc.glitch = 0;
    letterbox(1 - seg(t, ENDING_LEN - 0.8f, ENDING_LEN), "PILOT DEBRIEF", t);
    caption_draw(END_CAPS, (int)(sizeof(END_CAPS) / sizeof(END_CAPS[0])), t);
}

void cine_backdrop(float t, float alpha) {
    models_init();
    float st = 420 + t;
    V3 tgt = v3(0, 0, 0);
    wc_look(orbit(tgt, 1.4f + t * 0.02f, 0.3f, 1500), tgt, 45, v2(g_virt_w * 0.5f, VIRT_H * 0.8f), VIRT_H);
    g_wc.alpha = alpha;
    stars(200, 1, 11);
    Col nc[NNODES];
    for (int n = 0; n < NNODES; n++) nc[n] = N_OFF;
    system_draw(st, nc, 0.4f);
    Col pc = rgba(0.75f, 0.3f, 1, 1);
    float r = fmodf(t * 110, 270);
    w_dot(SIGNAL2, 3 + 2 * sinf(g_time * 5), pc);
    w_ring(SIGNAL2, g_wc.r, g_wc.u, r, 48, 2, col_a(pc, 1 - r / 270));
}
