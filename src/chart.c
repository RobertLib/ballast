/*
 * The sector chart as a holo table. The run's sectors float over a glowing
 * floor as wireframe moons (Tycho, Io) and rocks (Ceres), the Overseer waits
 * at the far end, the routes arc from sector to sector and the ship hovers
 * over the last one it cleared. Down through the floor hang the zones'
 * planets. The camera swoops in when the chart opens, leans with the mouse
 * and drifts after the selection; picking a sector flies the ship along its
 * route and dives it into the mine before the briefing.
 */
#include "common.h"

#define DX 228.0f      /* between two layers */
#define DZ 200.0f      /* between the sectors of a layer */
#define NODE_Y 58.0f   /* how high the sectors float */
#define START_L -0.8f  /* the hangar, in layers before the first */
#define ARC_UP 34.0f   /* how high the routes arch */
#define INTRO 1.7f     /* the camera's swoop */
#define LAUNCH 1.4f    /* the flight into the chosen sector */
#define FLY_END 0.86f  /* the part of the launch the flight takes, the dive after */

enum { UC_LIFT = 300, UC_PING };

static struct {
    float t0;         /* g_time when the chart opened */
    float launch_at;  /* g_time of the launch, < 0 none yet */
    int launch_to;
    float heading, roll;
    V3 look;          /* the camera's target, drifting after the selection */
    V2 lean;          /* and its lean after the mouse */
    int last_sel;
    /* this frame's layout, and where the sectors landed on screen for the mouse */
    V3 ctr[RUN_MAX_LAYERS][RUN_MAX_NODES];
    float rad[RUN_MAX_LAYERS][RUN_MAX_NODES], lift[RUN_MAX_LAYERS][RUN_MAX_NODES];
    V2 scr[RUN_MAX_LAYERS][RUN_MAX_NODES];
    float scr_r[RUN_MAX_LAYERS][RUN_MAX_NODES];
    bool seen[RUN_MAX_LAYERS][RUN_MAX_NODES];
    V3 ship;          /* where the ship is drawn */
    int tip;          /* the sector the tooltip is on, -1 none, and since when */
    float tip_at;
} C;

static const char *REWARD_GLYPH[SR_COUNT] = {"R&D", "$$$", "SOS", "ARM", "???", ""};
static const V3 AX = {1, 0, 0}, AZ = {0, 0, 1};

static float hfrac(uint32_t x) { return (hash32(x) >> 8) * (1.0f / 16777216.0f); }
static float age(void) { return g_time - C.t0; }
static V3 orbit(V3 target, float yaw, float pitch, float dist) {
    return v3add(target, v3(sinf(yaw) * cosf(pitch) * dist, sinf(pitch) * dist, cosf(yaw) * cosf(pitch) * dist));
}
static V3 bez(V3 a, V3 b, float up, float s) {
    V3 m = v3lerp(a, b, 0.5f);
    m.y += up;
    return v3lerp(v3lerp(a, m, s), v3lerp(m, b, s), s);
}

/* ------------------------------------------------------------ layout */
static float chart_x(float l) { return (l - (R.nlayers - 1 + START_L) * 0.5f) * DX; }

static V3 node_floor(int l, int i) {
    uint32_t s = run_node(l, i)->seed;
    float z = (i - (R.nnodes[l] - 1) * 0.5f) * DZ;
    return v3(chart_x((float)l) + (hfrac(s) - 0.5f) * 30, 0, z + (hfrac(s ^ 0x5A5Au) - 0.5f) * 40);
}
static bool node_boss(int l, int i) { return run_node(l, i)->reward == SR_BOSS; }
static bool node_done(int l, int i) { return l < R.layer && R.path[l] == i; }
static Col node_col(int l, int i) { return node_boss(l, i) ? C_MAGENTA : zone_color(run_node(l, i)->zone); }
static float node_alpha(int l, int i) {
    if (l < R.layer) return node_done(l, i) ? 0.75f : 0.16f;
    if (l == R.layer) return run_reachable(l, i) ? 1.0f : 0.3f;
    return 0.48f;
}
/* the sectors draw on layer by layer as the chart opens */
static float reveal_k(float l, float extra) { return clampf((age() - 0.3f - l * 0.11f - extra) / 0.5f, 0, 1); }

static V3 start_pos(void) { return v3(chart_x(START_L), NODE_Y * 0.8f, 0); }
static V3 home_floor(void) {
    if (R.layer > 0) return node_floor(R.layer - 1, R.path[R.layer - 1]);
    V3 s = start_pos();
    return v3(s.x, 0, s.z);
}
/* where the ship waits: over the last sector it cleared, or over the hangar */
static V3 ship_home(void) {
    if (R.layer > 0) {
        int l = R.layer - 1, i = R.path[l];
        return v3add(C.ctr[l][i], v3(0, C.rad[l][i] + 22, 0));
    }
    return v3add(start_pos(), v3(0, 24, 0));
}
static V3 look_want(int sel) {
    V3 t = node_floor(R.layer, sel);
    return v3(t.x * 0.1f, 0, t.z * 0.1f);
}

static void layout(int sel) {
    float a = age();
    for (int l = 0; l < R.nlayers; l++)
        for (int i = 0; i < R.nnodes[l]; i++) {
            V3 f = node_floor(l, i);
            bool cur = l == R.layer && i == sel && run_reachable(l, i);
            float lift = ui_spring(ui_id(UC_LIFT, l * 4 + i), cur ? 1.0f : 0.0f, 170, 15);
            float bob = sinf(a * 1.3f + l * 1.7f + i * 2.9f) * 4;
            bool boss = node_boss(l, i);
            C.lift[l][i] = lift;
            C.rad[l][i] = (boss ? 38 : 24) * (1 + 0.2f * lift);
            C.ctr[l][i] = v3(f.x, NODE_Y + (boss ? 16 : 0) + bob + lift * 22, f.z);
        }
}

/* ------------------------------------------------------------ interface */
void chart_enter(int sel) {
    memset(&C, 0, sizeof(C));
    C.t0 = g_time;
    C.launch_at = -1;
    C.last_sel = sel;
    C.tip = -1;
    V3 h = home_floor(), t = node_floor(R.layer, sel);
    C.heading = atan2f(t.z - h.z, t.x - h.x);
    C.look = look_want(sel);
}

void chart_update(float dt, int sel, V2 mouse, bool mouse_on) {
    V3 h = home_floor(), t = node_floor(R.layer, C.launch_at >= 0 ? C.launch_to : sel);
    float prev = C.heading;
    C.heading = approach_angle(C.heading, atan2f(t.z - h.z, t.x - h.x), dt * 3.5f);
    /* bank into the turn */
    float turn = dt > 0 ? wrap_angle(C.heading - prev) / dt : 0;
    C.roll = lerpf(C.roll, clampf(turn * 0.3f, -0.6f, 0.6f), damp_factor(5, dt));
    C.look = v3lerp(C.look, look_want(sel), damp_factor(2.5f, dt));
    V2 lw = mouse_on ? v2(clampf(mouse.x / g_virt_w - 0.5f, -0.5f, 0.5f), clampf(mouse.y / VIRT_H - 0.5f, -0.5f, 0.5f)) : v2(0, 0);
    C.lean = v2lerp(C.lean, lw, damp_factor(1.8f, dt));
    if (sel != C.last_sel) {
        ui_press(ui_id(UC_PING, R.layer * 4 + sel));
        C.last_sel = sel;
    }
}

int chart_pick(V2 mouse) {
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < R.nnodes[R.layer]; i++) {
        if (!C.seen[R.layer][i] || !run_reachable(R.layer, i)) continue;
        float d = v2dist(mouse, C.scr[R.layer][i]);
        if (d < maxf(C.scr_r[R.layer][i] * 1.5f, 30) && d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

void chart_launch(int sel) {
    if (C.launch_at >= 0) return;
    C.launch_at = g_time;
    C.launch_to = sel;
}

float chart_launch_k(void) { return C.launch_at < 0 ? -1 : clampf((g_time - C.launch_at) / LAUNCH, 0, 1); }

/* ------------------------------------------------------------ the camera */
static void camera(void) {
    float a = age();
    float in = ease_out_cubic(a / INTRO);
    float lk = chart_launch_k();
    float fly = lk < 0 ? 0 : smooth01(lk);
    V3 look = C.look;
    /* narrow windows back off until the table fits */
    float dist = lerpf(3200, 1440 * maxf(1, 1250 / g_virt_w), in);
    float pitch = lerpf(1.32f, 0.8f, in) - C.lean.y * 0.14f;
    float yaw = lerpf(-0.7f, 0, in) - C.lean.x * 0.2f + sinf(a * 0.21f) * 0.03f;
    if (fly > 0) {
        look = v3lerp(look, C.ship, fly);
        dist *= lerpf(1, 0.5f, fly);
        pitch -= 0.12f * fly;
    }
    wc_look(orbit(look, yaw, pitch, dist), look, 38, v2(g_virt_w * 0.5f, lerpf(296, 340, fly)), VIRT_H);
    g_wc.fog_near = dist * 0.75f;
    g_wc.fog_far = dist * 2.1f;
    g_wc.wdepth = dist;
}

/* ------------------------------------------------------------ the scene */
/* space under the table: the zones' planets, dim and far down */
static void deep_draw(float k) {
    static const struct { float l, y, z, r; Col c; int rings; } P[3] = {
        {0.2f, -900, -1000, 170, {0.35f, 0.65f, 1, 1}, 0},
        {2.6f, -1500, -1500, 330, {1, 0.68f, 0.4f, 1}, 0},
        {4.9f, -1100, -1100, 220, {1, 0.85f, 0.6f, 1}, 2},
    };
    float fn = g_wc.fog_near, ff = g_wc.fog_far;
    g_wc.fog_near = g_wc.fog_far = 0;
    for (int i = 0; i < 3; i++) {
        V3 p = v3(chart_x(P[i].l), P[i].y, P[i].z);
        holo_model(HM_GLOBE, p, m3_euler(age() * 0.04f + i * 2, 0, 0.4f), P[i].r, 1.5f, col_a(P[i].c, 0.16f * k), 1);
        w_glow(p, P[i].r * 1.7f, col_a(P[i].c, 0.05f * k));
        for (int r = 0; r < P[i].rings; r++)
            w_ring(p, AX, v3norm(v3(0, 0.32f, 1)), P[i].r * (1.75f + r * 0.32f), 72, 1.4f, col_a(P[i].c, 0.14f * k));
    }
    g_wc.fog_near = fn;
    g_wc.fog_far = ff;
}

static void floor_draw(float k) {
    w_grid(v3(0, 0, 0), 1150, 72, 1.1f, col_a(rgba(0.22f, 0.36f, 1, 1), 0.34f * k));
    float zh = DZ * 1.5f + 90;
    for (int l = 0; l < R.nlayers; l++) {
        float x = chart_x((float)l);
        const SectorNode *n0 = run_node(l, 0);
        Col zc = node_col(l, 0);
        float a = (l == R.layer ? 1.0f : l < R.layer ? 0.4f : 0.6f) * reveal_k((float)l, 0);
        const char *lab = n0->reward == SR_BOSS ? "FINALE" : n0->zone == 0 ? "TYCHO" : n0->zone == 1 ? "IO" : "CERES";
        char buf[16];
        snprintf(buf, sizeof(buf), "SECTOR %d", l + 1);
        w_text(buf, v3(x, 0, -zh), AX, AZ, 25, 2.8f, col_a(zc, 0.95f * a), AL_CENTER);
        w_text(lab, v3(x, 0, -zh + 36), AX, AZ, 15, 2.0f, col_a(zc, 0.65f * a), AL_CENTER);
        if (l == R.layer) {
            /* the layer to pick from: a lit lane across the table */
            float hw = DX * 0.46f, z0 = -zh + 58, z1 = zh - 20;
            for (int s = -1; s <= 1; s += 2) w_line(v3(x + s * hw, 0, z0), v3(x + s * hw, 0, z1), 1.4f, col_a(zc, 0.22f * a));
            float q = fmodf(age() * 0.35f, 1);
            w_line(v3(x - hw, 0, lerpf(z0, z1, q)), v3(x + hw, 0, lerpf(z0, z1, q)), 2, col_a(zc, 0.3f * sinf(q * PI) * a));
        }
    }
    /* a scan sweeps across the table now and then */
    float sw = fmodf(age() * 0.22f + 0.6f, 1.8f);
    if (sw < 1) {
        float x = lerpf(chart_x(START_L - 0.5f), chart_x(R.nlayers - 0.5f), sw);
        w_line(v3(x, 0, -zh + 40), v3(x, 0, zh - 40), 2.4f, col_a(C_CYAN, 0.16f * sinf(sw * PI) * k));
    }
}

static void station_draw(float k) {
    if (k <= 0) return;
    V3 p = start_pos(), f = v3(p.x, 0, p.z);
    Col c = rgba(0.6f, 0.85f, 1, 1);
    float a = age();
    w_ring(f, AX, AZ, 26, 32, 1.2f, col_a(c, 0.35f * k));
    w_line(f, p, 1.1f, col_a(c, 0.3f * k));
    w_ring(p, AX, AZ, 18, 6, 2, col_a(c, 0.85f * k));
    w_ring(p, v3(cosf(a * 0.8f), 0, sinf(a * 0.8f)), v3(0, 1, 0), 13, 24, 1.6f, col_a(c, 0.6f * k));
    w_glow(p, 40, col_a(c, 0.1f * k));
}

enum { RT_DONE, RT_NEXT, RT_CHOSEN, RT_AHEAD, RT_ONWARD, RT_PAST };

static void route(V3 a, float ra, V3 b, float rb, int kind, float k) {
    if (k <= 0) return;
    enum { N = 26 };
    float len = maxf(1, v3dist(a, b));
    float s0 = minf(0.4f, ra * 1.2f / len), s1 = 1 - minf(0.4f, rb * 1.2f / len);
    V3 p[N + 1];
    for (int q = 0; q <= N; q++) p[q] = bez(a, b, ARC_UP, lerpf(s0, s1, (float)q / N));
    switch (kind) {
    case RT_DONE: w_poly_reveal(p, N + 1, false, 2.6f, col_a(C_GREEN, 0.75f), k); break;
    case RT_NEXT:
    case RT_CHOSEN: {
        bool ch = kind == RT_CHOSEN;
        w_poly_reveal(p, N + 1, false, ch ? 3.4f : 2.2f, col_a(ch ? col_white(C_CYAN, 0.35f) : C_CYAN, ch ? 0.95f : 0.5f), k);
        if (k < 1) break;
        /* energy runs along the routes the ship can take */
        int n = ch ? 3 : 1;
        float speed = ch ? 0.75f : 0.4f;
        for (int q = 0; q < n; q++) {
            float s = fmodf(age() * speed + (float)q / n, 1);
            w_dot(bez(a, b, ARC_UP, lerpf(s0, s1, s)), ch ? 5 : 3.4f, col_a(C_WHITE, 0.85f * sinf(s * PI)));
        }
    } break;
    default: {
        Col c = kind == RT_ONWARD ? col_a(C_CYAN, 0.42f) : kind == RT_AHEAD ? rgba(0.45f, 0.55f, 0.8f, 0.34f) : rgba(0.4f, 0.45f, 0.6f, 0.1f);
        int shown = (int)(k * N);
        for (int q = 0; q + 1 <= shown; q += 2) w_line(p[q], p[q + 1], kind == RT_ONWARD ? 1.8f : 1.3f, c);
    } break;
    }
}

static void routes_draw(int sel) {
    /* out of the hangar */
    V3 st = start_pos();
    for (int j = 0; j < R.nnodes[0]; j++) {
        int kind = R.layer == 0 ? (j == sel ? RT_CHOSEN : RT_NEXT) : R.path[0] == j ? RT_DONE : RT_PAST;
        route(st, 16, C.ctr[0][j], C.rad[0][j], kind, reveal_k(-0.5f, 0.25f));
    }
    for (int l = 0; l + 1 < R.nlayers; l++)
        for (int i = 0; i < R.nnodes[l]; i++)
            for (int j = 0; j < R.nnodes[l + 1]; j++) {
                if (!((run_node(l, i)->links >> j) & 1)) continue;
                int kind;
                if (l + 1 < R.layer) kind = R.path[l] == i && R.path[l + 1] == j ? RT_DONE : RT_PAST;
                else if (l + 1 == R.layer) kind = R.path[l] != i ? RT_PAST : j == sel ? RT_CHOSEN : RT_NEXT;
                else if (l == R.layer && i == sel && run_reachable(l, i)) kind = RT_ONWARD;
                else kind = RT_AHEAD;
                route(C.ctr[l][i], C.rad[l][i], C.ctr[l + 1][j], C.rad[l + 1][j], kind, reveal_k(l + 0.5f, 0.25f));
            }
}

static void node_body(int l, int i, float alpha, float k) {
    const SectorNode *nd = run_node(l, i);
    V3 c = C.ctr[l][i];
    float r = C.rad[l][i], lift = C.lift[l][i];
    Col zc = col_white(node_col(l, i), 0.2f * lift);
    float a = age();
    float spin = a * (0.3f + 0.6f * lift) + hfrac(nd->seed) * TAU;
    float sa = g_wc.alpha;
    g_wc.alpha = sa * alpha;
    if (nd->reward == SR_BOSS) {
        g_wc.alpha *= k;
        holo_boss3(c, r * 0.95f, a, zc);
    } else if (nd->zone == 2) {
        /* Ceres: a tumbling rock among its gravel */
        holo_model(HM_ROCK, c, m3_euler(spin, 0.5f * sinf(a * 0.4f + i), 0.4f), r, 1.9f, zc, k);
        for (int q = 0; q < 6; q++) {
            float an = spin * 0.5f + q * TAU / 6 + hfrac(nd->seed + q);
            V3 p = v3add(c, v3(cosf(an) * r * 1.75f, sinf(an * 2 + q) * r * 0.22f, sinf(an) * r * 1.75f));
            w_dot(p, 2.2f, col_a(zc, 0.7f * k));
        }
    } else {
        holo_model(HM_GLOBE, c, m3_euler(spin, 0, 0.35f), r, 1.8f, zc, k);
        if (nd->zone == 0) {
            /* Tycho: a mining station circles the moon */
            V3 ax = AX, ay = v3norm(v3(0, 0.35f, 1));
            w_ring(c, ax, ay, r * 1.55f, 40, 1.1f, col_a(zc, 0.3f * k));
            float an = a * 1.1f + i * 2 + l;
            w_dot(v3add(c, v3add(v3scale(ax, cosf(an) * r * 1.55f), v3scale(ay, sinf(an) * r * 1.55f))), 3.4f, col_a(col_white(zc, 0.5f), k));
        } else {
            /* Io: volcanoes throw arcs of sulphur */
            for (int q = 0; q < 2; q++) {
                float lon = spin + q * PI + 0.6f, lat = 0.45f - q * 0.8f;
                V3 n = v3(cosf(lat) * cosf(lon), sinf(lat), cosf(lat) * sinf(lon));
                V3 tg = v3norm(v3cross(n, v3(0, 1, 0)));
                float ph = fmodf(a * 0.7f + q * 0.5f + hfrac(nd->seed + 9), 1);
                V3 pts[10];
                for (int j = 0; j < 10; j++) {
                    float s = j / 9.0f * ph;
                    pts[j] = v3add(c, v3add(v3scale(n, r * (1 + 0.8f * sinf(s * PI))), v3scale(tg, r * 0.6f * s)));
                }
                w_poly(pts, 10, false, 1.6f, col_a(C_YELLOW, 0.75f * (1 - ph) * k));
            }
        }
    }
    float pulse = 0.5f + 0.5f * sinf(a * 4 + i * 2);
    float glow = 0.07f + 0.08f * lift + (l == R.layer && run_reachable(l, i) ? 0.05f * pulse : 0);
    w_glow(c, r * 2.4f, col_a(zc, glow * k));
    g_wc.alpha = sa;
}

/* the pin under a sector, and the selection around it */
static void node_pin(int l, int i, float alpha, float k) {
    V3 c = C.ctr[l][i], f = v3(c.x, 0, c.z);
    float r = C.rad[l][i], lift = C.lift[l][i], a = age();
    Col zc = node_col(l, i);
    bool reach = run_reachable(l, i);
    Col pc = node_done(l, i) ? C_GREEN : zc;
    float pulse = 0.5f + 0.5f * sinf(a * 4 + i);
    w_ring(f, AX, AZ, r * 1.1f, 40, 1.5f, col_a(pc, (0.28f + (reach ? 0.22f * pulse : 0)) * alpha * k));
    w_line(f, v3(c.x, c.y - r, c.z), 1.1f, col_a(pc, 0.22f * alpha * k));
    if (lift > 0.01f) {
        /* a beam from the floor with rings climbing it, and brackets turning around the sector */
        w_line(f, v3(c.x, c.y - r, c.z), 3.2f * lift, col_a(col_white(zc, 0.3f), 0.5f * lift * k));
        w_glow(f, r * 1.6f, col_a(zc, 0.22f * lift * k));
        for (int q = 0; q < 3; q++) {
            float h = fmodf(a * 0.55f + q / 3.0f, 1);
            w_ring(v3(c.x, h * (c.y - r), c.z), AX, AZ, r * (1.15f - 0.35f * h), 32, 1.6f, col_a(zc, 0.55f * (1 - h) * lift * k));
        }
        for (int q = 0; q < 4; q++) {
            float a0 = -a * 0.9f + q * TAU / 4;
            w_arc(c, g_wc.r, g_wc.u, r * (1.45f + 0.08f * pulse) + (1 - lift) * 20, a0, a0 + 0.75f, 8, 3.2f, col_a(C_MAGENTA, lift * k));
        }
    }
    float ping = ui_pressed(ui_id(UC_PING, l * 4 + i));
    if (ping > 0) w_ring(f, AX, AZ, r * (1.2f + (1 - ping) * 2.4f), 48, 2.6f, col_a(col_white(zc, 0.4f), ping * k));
}

/* the ship: waiting and turning toward the selection, or flying the route and diving into the mine */
static void ship_draw(void) {
    Col sc = run_ship()->col;
    float lk = chart_launch_k(), a = age();
    V3 home = ship_home();
    float k = reveal_k(R.layer - 1.0f, 0.35f);
    if (lk < 0) {
        C.ship = v3add(home, v3(0, sinf(a * 2.6f) * 3, 0));
        holo_ship3(C.ship, m3_euler(C.heading, 0.05f * sinf(a * 1.7f), C.roll), 15, sc, 0.25f + 0.1f * sinf(a * 9), k);
        return;
    }
    int j = C.launch_to;
    V3 tgt = C.ctr[R.layer][j];
    float s = smooth01((lk - 0.05f) / (FLY_END - 0.05f));
    float up = 80;
    C.ship = bez(home, tgt, up, s);
    V3 d = v3sub(bez(home, tgt, up, minf(1, s + 0.02f)), bez(home, tgt, up, maxf(0, s - 0.02f)));
    float heading = v3len(d) > 1e-3f ? atan2f(d.z, d.x) : C.heading;
    float pitch = atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z) + 1e-4f);
    float dive = smooth01((s - 0.72f) / 0.28f);
    /* the exhaust trail along the route behind it */
    V3 tp[20];
    Col tc[20];
    for (int q = 0; q < 20; q++) {
        tp[q] = bez(home, tgt, up, maxf(0, s - q * 0.018f));
        tc[q] = col_a(col_lerp(C_YELLOW, C_ORANGE, q / 19.0f), 0.75f * (1 - q / 20.0f) * (1 - dive * 0.6f));
    }
    if (s > 0.01f) w_poly_cols(tp, tc, 20, false, 2.6f);
    if (dive < 0.98f) holo_ship3(C.ship, m3_euler(heading, pitch, C.roll * (1 - s)), 15 * (1 - 0.85f * dive), sc, 1, 1);
    /* the sector flares as the ship dives in */
    float fl = clampf((lk - FLY_END + 0.04f) / (1 - FLY_END + 0.04f), 0, 1);
    if (fl > 0) {
        float r = C.rad[R.layer][j];
        V3 f = v3(tgt.x, 0, tgt.z);
        w_glow(tgt, r * (2 + 5 * fl), col_a(C_WHITE, 0.55f * (1 - fl)));
        w_ring(f, AX, AZ, r * (1.3f + 5 * fl), 64, 3, col_a(col_white(node_col(R.layer, j), 0.5f), 1 - fl));
        w_ring(tgt, g_wc.r, g_wc.u, r * (1.2f + 2.5f * fl), 48, 2.4f, col_a(C_WHITE, 0.7f * (1 - fl)));
    }
}

/* ------------------------------------------------------------ labels, drawn flat over the scene */
static void labels_draw(int sel) {
    for (int l = 0; l < R.nlayers; l++)
        for (int i = 0; i < R.nnodes[l]; i++) {
            if (!C.seen[l][i]) continue;
            const SectorNode *nd = run_node(l, i);
            V2 p = C.scr[l][i];
            float rp = C.scr_r[l][i];
            float a = node_alpha(l, i) * reveal_k((float)l, 0.2f);
            if (a <= 0.01f) continue;
            Col zc = node_col(l, i);
            bool cur = l == R.layer && i == sel && run_reachable(l, i);
            bool past = l < R.layer && !node_done(l, i);
            float y = p.y + rp * 1.25f + 6;
            if (nd->reward != SR_BOSS) {
                float s = cur ? 12 : 10;
                r_text(REWARD_GLYPH[nd->reward], p.x, y, s, col_a(col_white(zc, cur ? 0.6f : 0.35f), a), AL_CENTER);
                y += s + 7;
            }
            if (node_done(l, i)) {
                V2 q = v2(p.x + rp * 0.95f, p.y - rp * 0.95f);
                r_polyline((V2[]){v2(q.x - 8, q.y), v2(q.x - 2, q.y + 6), v2(q.x + 9, q.y - 6)}, 3, false, 3.2f, col_a(C_GREEN, 0.95f));
            }
            if (nd->hazard && !past) {
                const char *hn = hazard_name(nd->hazard);
                float w = text_width(hn, 8), tx = p.x - w * 0.5f + 7;
                V2 tri[3] = {v2(tx - 13, y + 8), v2(tx - 8, y - 1), v2(tx - 3, y + 8)};
                r_polyline(tri, 3, true, 2, col_a(C_ORANGE, a));
                r_text(hn, tx, y, 8, col_a(C_ORANGE, a * 0.9f), AL_LEFT);
            }
        }
    V2 sp;
    float d;
    if (wc_project(start_pos(), &sp, &d)) r_text("HANGAR", sp.x, sp.y + 22, 8, rgba(0.6f, 0.85f, 1, (R.layer == 0 ? 0.6f : 0.35f) * reveal_k(-1, 0.2f)), AL_CENTER);
}

/* the sectors off the current layer tell what they are when the mouse is on them */
static void tooltip_draw(V2 mouse) {
    int hl = -1, hi = -1;
    float bd = 1e9f;
    for (int l = 0; l < R.nlayers; l++)
        for (int i = 0; i < R.nnodes[l]; i++) {
            if (!C.seen[l][i] || (l == R.layer && run_reachable(l, i))) continue;
            float d = v2dist(mouse, C.scr[l][i]);
            if (d < maxf(C.scr_r[l][i] * 1.4f, 24) && d < bd) {
                bd = d;
                hl = l;
                hi = i;
            }
        }
    if (hl < 0) {
        C.tip = -1;
        return;
    }
    if (C.tip != hl * 4 + hi) {
        C.tip = hl * 4 + hi;
        C.tip_at = g_time;
    }
    const SectorNode *nd = run_node(hl, hi);
    Col zc = node_col(hl, hi);
    float t = ease_out_cubic((g_time - C.tip_at) / 0.18f);
    const char *st = hl < R.layer ? (node_done(hl, hi) ? "CLEARED" : "PASSED BY") : hl == R.layer ? "OFF YOUR ROUTE" : "AHEAD";
    char status[48], bonus[48], hazard[48];
    snprintf(status, sizeof(status), "SECTOR %d  -  %s", hl + 1, st);
    snprintf(bonus, sizeof(bonus), "BONUS: %s", reward_name(nd->reward));
    snprintf(hazard, sizeof(hazard), "HAZARD: %s", hazard_name(nd->hazard));
    float w = maxf(text_width(nd->name, 12), maxf(text_width(bonus, 9), nd->hazard ? text_width(hazard, 9) : 0)) + 28;
    float h = nd->hazard ? 82 : 66;
    V2 p = C.scr[hl][hi];
    float x = p.x + C.scr_r[hl][hi] + 16, y = p.y - h * 0.5f;
    if (x + w > g_virt_w - 10) x = p.x - C.scr_r[hl][hi] - 16 - w;
    y = clampf(y, 80, VIRT_H - h - 10);
    r_push(v2((1 - t) * -8, 0), v2(x, y + h * 0.5f), 1, t, t);
    r_panel(x, y, w, h, zc, 0.9f);
    r_text(nd->name, x + 14, y + 12, 12, zc, AL_LEFT);
    r_text(status, x + 14, y + 31, 8, col_a(zc, 0.7f), AL_LEFT);
    r_text(bonus, x + 14, y + 46, 9, rgba(0.85f, 0.9f, 1, 0.9f), AL_LEFT);
    if (nd->hazard) r_text(hazard, x + 14, y + 62, 9, C_ORANGE, AL_LEFT);
    r_pop();
}

void chart_draw(int sel, V2 mouse, bool mouse_on) {
    float a = age();
    layout(sel);
    /* the ship's place first: the camera follows it on the flight */
    if (C.launch_at >= 0) {
        float lk = chart_launch_k();
        V3 home = ship_home(), tgt = C.ctr[R.layer][C.launch_to];
        C.ship = bez(home, tgt, 80, smooth01((lk - 0.05f) / (FLY_END - 0.05f)));
    }
    camera();
    float fk = ease_out_cubic(a / 0.9f);
    holo_stars(260, 0.9f * fk, 0xC4A27u);
    deep_draw(fk);
    floor_draw(fk);
    routes_draw(sel);
    for (int l = 0; l < R.nlayers; l++)
        for (int i = 0; i < R.nnodes[l]; i++) {
            float k = reveal_k((float)l, 0), al = node_alpha(l, i);
            node_pin(l, i, al, k);
            node_body(l, i, al, k);
        }
    station_draw(reveal_k(-1, 0));
    ship_draw();
    for (int l = 0; l < R.nlayers; l++)
        for (int i = 0; i < R.nnodes[l]; i++) {
            float d;
            C.seen[l][i] = wc_project(C.ctr[l][i], &C.scr[l][i], &d);
            C.scr_r[l][i] = C.seen[l][i] ? C.rad[l][i] * g_wc.focal / d : 0;
        }
    labels_draw(sel);
    if (mouse_on && C.launch_at < 0 && a > INTRO * 0.6f) tooltip_draw(mouse);
    else C.tip = -1;
}
