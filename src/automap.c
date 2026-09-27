/*
 * The automap as a holo model of the explored mine. The walls stand up from
 * the floor as glass panels with glowing rims, the doors are coloured slabs,
 * the keys, miners and the reactor float over the floor on tethers and the
 * ship hovers where it is. The map opens looking straight down and tilts into
 * 3D while a scan sweeps out from the ship and the walls rise behind it.
 * WASD pans, the wheel zooms, dragging or Q / E turn the model.
 */
#include "common.h"
#include "game_internal.h"

#define WALL_H 76.0f     /* how high the walls stand */
#define CAP 10.0f        /* how thick their tops are */
#define OPEN_T 0.6f      /* the tilt into 3D */
#define SWEEP_T 1.1f     /* the scan out to the farthest explored tile */
#define RISE_W 320.0f    /* the band behind the scan front where the walls rise */
#define PITCH 0.92f      /* the resting tilt, radians above the floor */
#define FOV 38.0f
#define RUN_MAX 256      /* points in one wall polyline (the wire renderer's limit) */

static const V3 AX = {1, 0, 0}, AZ = {0, 0, 1};

static struct {
    float t0;            /* g_time when the map opened */
    float yaw, pitch;    /* the viewer's turn and tilt */
    float zoom;          /* G.automap_zoom, eased */
    V2 lean;
    bool dragging;
    V2 drag_at;
    V2 ctr;              /* the explored part of the mine: centre and size */
    float span_x, span_y;
    float reach;         /* the farthest explored tile from the ship */
} A;

static float age(void) { return g_time - A.t0; }

/* ------------------------------------------------------------ the explored part */
static void measure(void) {
    int x0 = L.w, y0 = L.h, x1 = -1, y1 = -1;
    float reach = 0;
    V2 sp = W.pl.pos;
    for (int y = 0; y < L.h; y++)
        for (int x = 0; x < L.w; x++) {
            if (!L.explored[y][x]) continue;
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
            reach = maxf(reach, v2dist(tile_center(x, y), sp));
        }
    int px = clampi(tx_of(sp.x), 0, L.w - 1), py = clampi(tx_of(sp.y), 0, L.h - 1);
    if (x1 < 0) { x0 = x1 = px; y0 = y1 = py; }
    if (px < x0) x0 = px;
    if (px > x1) x1 = px;
    if (py < y0) y0 = py;
    if (py > y1) y1 = py;
    A.ctr = v2((x0 + x1 + 1) * 0.5f * TILE, (y0 + y1 + 1) * 0.5f * TILE);
    A.span_x = maxf((x1 - x0 + 3) * TILE, 26 * TILE);
    A.span_y = maxf((y1 - y0 + 3) * TILE, 18 * TILE);
    A.reach = reach + TILE * 2;
}

/* the scan front, and how far the walls at p have risen behind it */
static float front(void) {
    float k = 1 - clampf(age() / SWEEP_T, 0, 1);
    return (A.reach + RISE_W) * (1 - k * k);
}
static float rise_at(V2 p, float fr) {
    if (fr >= A.reach + RISE_W) return 1;
    return smooth01((fr - v2dist(p, W.pl.pos)) / RISE_W);
}
/* markers pop up with a little overshoot as the scan finds them */
static float pop(float r) { return r >= 1 ? 1 : r * (1 + 0.7f * sinf(r * PI)); }

/* ------------------------------------------------------------ interface */
void automap_open(void) {
    G.automap = true;
    G.automap_zoom = 1;
    G.automap_pan = v2(0, 0);
    memset(&A, 0, sizeof(A));
    A.t0 = g_time;
    A.zoom = 1;
    A.pitch = PITCH;
    measure();
}

void automap_update(float dt, V2 mouse, bool drag) {
    measure();
    A.zoom = lerpf(A.zoom, G.automap_zoom, damp_factor(10, dt));
    /* turn: Q / E, the right stick or a drag */
    A.yaw += (g_in.turn + g_in.stick_aim.x) * 1.9f * dt;
    A.pitch -= g_in.stick_aim.y * 1.2f * dt;
    if (drag) {
        if (A.dragging) {
            V2 d = v2sub(mouse, A.drag_at);
            A.yaw -= d.x * 0.006f;
            A.pitch += d.y * 0.005f;
        }
        A.drag_at = mouse;
    }
    A.dragging = drag;
    A.pitch = clampf(A.pitch, 0.5f, 1.42f);
    V2 lw = g_in.mouse_active && !g_in.use_stick && !drag ? v2(clampf(mouse.x / g_virt_w - 0.5f, -0.5f, 0.5f), clampf(mouse.y / VIRT_H - 0.5f, -0.5f, 0.5f)) : A.lean;
    A.lean = v2lerp(A.lean, lw, damp_factor(1.8f, dt));
    /* pan along the screen, whichever way the model is turned */
    V2 m = g_in.move;
    float c = cosf(A.yaw), s = sinf(A.yaw);
    V2 d = v2(c * m.x + s * m.y, -s * m.x + c * m.y);
    G.automap_pan = v2mad(G.automap_pan, d, 0.6f * maxf(A.span_x, A.span_y) * dt / maxf(A.zoom, 0.3f));
    V2 at = v2add(A.ctr, G.automap_pan);
    at = v2(clampf(at.x, 0, L.w * TILE), clampf(at.y, 0, L.h * TILE));
    G.automap_pan = v2sub(at, A.ctr);
}

/* ------------------------------------------------------------ the camera */
static void camera(void) {
    float in = ease_out_cubic(age() / OPEN_T);
    float vw = g_virt_w - 120, vh = VIRT_H - 170;
    float pitch = lerpf(1.42f, A.pitch, in) - A.lean.y * 0.08f;
    float focal = VIRT_H * 0.5f / tanf(FOV * PI / 360.0f);
    /* back off until the explored part fits, the far side foreshortened by the tilt */
    float fit = focal * maxf(A.span_x / vw, A.span_y * sinf(pitch) / vh) * 1.1f;
    float dist = fit / A.zoom * lerpf(1.15f, 1, in);
    float yaw = A.yaw + lerpf(-0.45f, 0, in) - A.lean.x * 0.1f + sinf(g_time * 0.23f) * 0.02f;
    V3 look = v3flat(v2add(A.ctr, G.automap_pan), 0);
    V3 eye = v3add(look, v3(sinf(yaw) * cosf(pitch) * dist, sinf(pitch) * dist, cosf(yaw) * cosf(pitch) * dist));
    wc_look(eye, look, FOV, v2(g_virt_w * 0.5f, VIRT_H * 0.5f + 12), VIRT_H);
    g_wc.fog_near = dist * 0.8f;
    g_wc.fog_far = dist * 2.4f;
    g_wc.wdepth = dist;
}

/* virtual pixels to world units at p, so markers keep their size on screen */
static float px_at(V3 p, float px) {
    float s = wc_scale_at(p);
    return s > 0 ? px / s : 0;
}

/* ------------------------------------------------------------ the floor */
static int floor_kind(int x, int y) {
    uint8_t f = L.flags[y][x];
    return f & TF_EXIT ? 2 : f & TF_ENERGY ? 1 : 0;
}

static Col floor_col(const LevelDef *d, int kind, int y) {
    float t = g_time;
    if (kind == 1) return col_a(C_YELLOW, 0.2f + 0.06f * sinf(t * 3 + y));
    if (kind == 2) return G.reactor_dead ? col_a(C_GREEN, 0.25f + 0.12f * sinf(t * 8 - y)) : col_a(C_GREEN, 0.1f);
    return col_a(d->grid, 0.14f);
}

static void floor_draw(const LevelDef *d, float fr) {
    for (int y = 0; y < L.h; y++)
        for (int x = 0; x < L.w;) {
            uint8_t t = L.tile[y][x];
            if (!L.explored[y][x] || t == T_SOLID || t == T_BREAK) { x++; continue; }
            int kind = floor_kind(x, y);
            float X0 = x * TILE, Y0 = y * TILE, X1 = X0 + TILE, Y1 = Y0 + TILE;
            if (t >= T_TRI_TL && t <= T_TRI_BR) {
                /* a slope: only its open half is floor */
                V2 a = v2(X0, Y0), b = v2(X1, Y0), c = v2(X1, Y1), dd = v2(X0, Y1);
                V2 tri[3];
                switch (t) {
                case T_TRI_TL: tri[0] = b; tri[1] = c; tri[2] = dd; break;
                case T_TRI_TR: tri[0] = a; tri[1] = c; tri[2] = dd; break;
                case T_TRI_BL: tri[0] = a; tri[1] = b; tri[2] = c; break;
                default: tri[0] = a; tri[1] = b; tri[2] = dd; break;
                }
                float k = rise_at(tile_center(x, y), fr);
                V3 p[3] = {v3flat(tri[0], 0), v3flat(tri[1], 0), v3flat(tri[2], 0)};
                if (k > 0) w_fill(p, 3, col_a(floor_col(d, kind, y), k));
                x++;
                continue;
            }
            /* a run of plain floor along the row */
            int x0 = x;
            while (x < L.w && x - x0 < 8 && L.explored[y][x] && L.tile[y][x] == T_EMPTY && floor_kind(x, y) == kind) x++;
            X1 = x * TILE;
            float k = rise_at(v2((X0 + X1) * 0.5f, (Y0 + Y1) * 0.5f), fr);
            if (k <= 0) continue;
            Col c = floor_col(d, kind, y);
            V3 q[4] = {v3(X0, 0, Y0), v3(X1, 0, Y0), v3(X1, 0, Y1), v3(X0, 0, Y1)};
            w_fill(q, 4, col_a(c, k));
        }
}

/* ------------------------------------------------------------ the walls */
static bool seen_side(V2 a, V2 b, V2 n) {
    V2 mid = v2mad(v2scale(v2add(a, b), 0.5f), n, 6);
    V2 pa = v2mad(a, n, 1), pb = v2mad(b, n, 1);
    int tx = tx_of(mid.x), ty = tx_of(mid.y);
    int ax = tx_of(pa.x), ay = tx_of(pa.y), bx = tx_of(pb.x), by = tx_of(pb.y);
    return (tile_in(tx, ty) && L.explored[ty][tx]) || (tile_in(ax, ay) && L.explored[ay][ax]) || (tile_in(bx, by) && L.explored[by][bx]);
}

/* one unbroken stretch of wall: its foot, its rim and the inner edge of its top */
static V3 rb[RUN_MAX], rt[RUN_MAX], re[RUN_MAX];
static int rn;

static void run_flush(Col wall) {
    if (rn >= 2) {
        w_poly(rb, rn, false, 1.3f, col_a(wall, 0.4f));
        w_poly(re, rn, false, 1.1f, col_a(wall, 0.35f));
        w_poly(rt, rn, false, 2.4f, wall);
    }
    rn = 0;
}

static void run_add(V2 p, V2 echo, float h, Col wall) {
    if (rn == RUN_MAX) {
        /* full: draw it and go on from its last point */
        V3 b = rb[rn - 1], t = rt[rn - 1], e = re[rn - 1];
        run_flush(wall);
        rb[0] = b; rt[0] = t; re[0] = e;
        rn = 1;
    }
    rb[rn] = v3flat(p, 0);
    rt[rn] = v3flat(p, h);
    re[rn] = v3flat(echo, h);
    rn++;
}

/* the offset of a contour corner into the rock, along the miter */
static V2 miter_in(const V2 *src, int n, bool closed, int k, float by) {
    V2 prev = src[closed ? (k - 1 + n) % n : (k > 0 ? k - 1 : k)];
    V2 next = src[closed ? (k + 1) % n : (k < n - 1 ? k + 1 : k)];
    V2 d0 = v2norm(v2sub(src[k], prev)), d1 = v2norm(v2sub(next, src[k]));
    if (v2len2(d0) < 0.5f) d0 = d1;
    if (v2len2(d1) < 0.5f) d1 = d0;
    V2 n0 = v2perp(d0), m = v2norm(v2add(n0, v2perp(d1)));
    if (v2len2(m) < 0.5f) m = n0;
    return v2mad(src[k], m, -by / maxf(0.4f, v2dot(m, n0)));
}

static void walls_draw(const LevelDef *d, float fr) {
    Col wall = d->wall;
    const V2 light = v2norm(v2(-0.55f, -0.83f));
    for (int ci = 0; ci < L.ncontours; ci++) {
        int n = L.clen[ci];
        if (n < 2) continue;
        const V2 *src = &L.cpts[L.cstart[ci]];
        bool closed = L.cclosed[ci];
        int edges = closed ? n : n - 1;
        rn = 0;
        for (int e = 0; e < edges; e++) {
            int k0 = e, k1 = (e + 1) % n;
            V2 a = src[k0], b = src[k1], ab = v2sub(b, a);
            float len = v2len(ab);
            if (len < 1) continue;
            V2 nrm = v2perp(v2scale(ab, 1 / len));
            V2 ea = miter_in(src, n, closed, k0, CAP), eb = miter_in(src, n, closed, k1, CAP);
            /* the glass: brighter where it faces the light */
            float lit = 0.5f + 0.5f * v2dot(nrm, light);
            Col face = col_a(wall, 0.06f + 0.16f * lit);
            int pieces = (int)(len / TILE + 0.5f);
            if (pieces < 1) pieces = 1;
            for (int j = 0; j < pieces; j++) {
                float t0 = (float)j / pieces, t1 = (float)(j + 1) / pieces;
                V2 pa = v2mad(a, ab, t0), pb = v2mad(a, ab, t1);
                float ha = WALL_H * rise_at(pa, fr), hb = WALL_H * rise_at(pb, fr);
                if (!seen_side(pa, pb, nrm) || (ha <= 0 && hb <= 0)) {
                    run_flush(wall);
                    continue;
                }
                V2 qa = j == 0 ? ea : v2mad(pa, nrm, -CAP), qb = j == pieces - 1 ? eb : v2mad(pb, nrm, -CAP);
                if (rn == 0) run_add(pa, qa, ha, wall);
                run_add(pb, qb, hb, wall);
                V3 f[4] = {v3flat(pa, 0), v3flat(pb, 0), v3flat(pb, hb), v3flat(pa, ha)};
                w_fill(f, 4, face);
                V3 cap[4] = {v3flat(pa, ha), v3flat(pb, hb), v3flat(qb, hb), v3flat(qa, ha)};
                w_fill(cap, 4, col_a(wall, 0.3f));
                /* an edge up the wall where it turns */
                if (j == 0) {
                    V2 prev = src[closed ? (k0 - 1 + n) % n : (k0 > 0 ? k0 - 1 : 0)];
                    V2 dp = v2norm(v2sub(a, prev));
                    if (v2len2(dp) > 0.5f && v2dot(dp, v2scale(ab, 1 / len)) < 0.95f) w_line(v3flat(pa, 0), v3flat(pa, ha), 1.3f, col_a(wall, 0.55f));
                }
            }
        }
        run_flush(wall);
    }
    /* cracked walls: dashed, in the accent colour */
    for (int i = 0; i < L.nbreak_segs; i++) {
        const Seg *s = &L.segs[L.break_segs[i]];
        if (!seen_side(s->a, s->b, s->n)) continue;
        V2 mid = v2scale(v2add(s->a, s->b), 0.5f);
        float h = WALL_H * rise_at(mid, fr);
        if (h <= 0) continue;
        V2 ab = v2sub(s->b, s->a);
        for (int k = 0; k < 4; k++) {
            V2 p0 = v2mad(s->a, ab, k / 4.0f + 0.04f), p1 = v2mad(s->a, ab, (k + 0.6f) / 4.0f);
            w_line(v3flat(p0, h), v3flat(p1, h), 2, col_a(d->accent, 0.7f));
            w_line(v3flat(p0, 0), v3flat(p1, 0), 1.2f, col_a(d->accent, 0.35f));
        }
        V3 f[4] = {v3flat(s->a, 0), v3flat(s->b, 0), v3flat(s->b, h), v3flat(s->a, h)};
        w_fill(f, 4, col_a(d->accent, 0.05f));
    }
}

static void doors_draw(float fr) {
    float t = g_time;
    for (int i = 0; i < L.ndoors; i++) {
        Door *dr = &L.doors[i];
        bool seen = false;
        for (int y = dr->y0 - 1; y <= dr->y1 + 1; y++)
            for (int x = dr->x0 - 1; x <= dr->x1 + 1; x++)
                if (tile_in(x, y) && L.explored[y][x]) seen = true;
        if (!seen) continue;
        V2 a, b;
        if (dr->horiz) { a = v2(dr->x0 * TILE, (dr->y0 + 0.5f) * TILE); b = v2((dr->x1 + 1) * TILE, (dr->y0 + 0.5f) * TILE); }
        else { a = v2((dr->x0 + 0.5f) * TILE, dr->y0 * TILE); b = v2((dr->x0 + 0.5f) * TILE, (dr->y1 + 1) * TILE); }
        V2 c2 = v2scale(v2add(a, b), 0.5f);
        float k = rise_at(c2, fr);
        if (k <= 0) continue;
        Col c = key_color(dr->lock);
        float jamb = WALL_H * 1.1f * k;
        w_line(v3flat(a, 0), v3flat(a, jamb), 2.4f, c);
        w_line(v3flat(b, 0), v3flat(b, jamb), 2.4f, c);
        w_line(v3flat(a, 0), v3flat(b, 0), 1.6f, col_a(c, 0.6f));
        /* the slab sinks into the floor as the door opens */
        float h = WALL_H * 0.92f * k * (1 - dr->open);
        if (h > 1) {
            V3 f[4] = {v3flat(a, 0), v3flat(b, 0), v3flat(b, h), v3flat(a, h)};
            w_fill(f, 4, col_a(c, dr->lock != LOCK_NONE ? 0.24f : 0.13f));
            w_line(v3flat(a, h), v3flat(b, h), 2.2f, c);
        }
        if (dr->lock != LOCK_NONE && dr->open < 0.2f) {
            V3 at = v3flat(c2, WALL_H * 1.45f + sinf(t * 2 + i) * 3);
            holo_model(HM_KEY, at, m3_euler(t * 1.4f + i, 0, 0), px_at(at, 4.5f) * pop(k), 1.8f, c, 1);
            w_glow(at, px_at(at, 12), col_a(c, 0.3f));
        }
    }
}

/* ------------------------------------------------------------ markers */
static bool seen_at(V2 p) {
    int x = tx_of(p.x), y = tx_of(p.y);
    return tile_in(x, y) && L.explored[y][x];
}

/* a marker's stalk down to the floor and the ring it stands in */
static void tether(V2 p, float h, Col c, float ring_px) {
    V3 f = v3flat(p, 0);
    w_line(f, v3flat(p, h), 1.2f, col_a(c, 0.45f));
    w_ring(f, AX, AZ, px_at(f, ring_px), 20, 1.5f, col_a(c, 0.7f));
}

static void label(V3 p, const char *s, float size, Col c) {
    V2 v;
    if (wc_project(p, &v, NULL)) r_text(s, v.x, v.y - size, size, c, AL_CENTER);
}

static void markers_draw(const LevelDef *d, float fr) {
    float t = g_time;
    for (int i = 0; i < W.nmat; i++) {
        V2 p = W.mat[i].pos;
        if (!seen_at(p)) continue;
        float k = pop(rise_at(p, fr));
        if (k <= 0) continue;
        float s = 18 * k;
        w_box(v3(p.x - s, 0, p.y - s), v3(p.x + s, 26 * k, p.y + s), 1.6f, C_MAGENTA);
        V3 at = v3flat(p, 36 * k);
        w_ring(at, v3(cosf(t), 0, sinf(t)), v3(0, 1, 0), s * 0.8f, 16, 1.4f, col_a(C_MAGENTA, 0.6f));
    }
    for (int i = 0; i < W.nhost; i++) {
        Hostage *h = &W.host[i];
        if (!h->active || !seen_at(h->pos)) continue;
        float k = pop(rise_at(h->pos, fr));
        if (k <= 0) continue;
        Col c = rgba(0.45f, 1, 0.55f, 1);
        float ht = WALL_H * 1.3f * k;
        tether(h->pos, ht, c, 5);
        V3 at = v3flat(h->pos, ht);
        w_glow(at, px_at(at, 9), col_a(c, 0.7f + 0.3f * sinf(t * 6 + i)));
        float ping = fmodf(t * 0.8f + i * 0.3f, 1);
        V3 f = v3flat(h->pos, 0);
        w_ring(f, AX, AZ, px_at(f, 5 + ping * 14), 20, 1.4f, col_a(c, (1 - ping) * 0.6f));
    }
    for (int i = 0; i < MAX_POWERUPS; i++) {
        Powerup *pu = &W.pu[i];
        if (!pu->active || (pu->type < PU_KEY_BLUE && pu->type != PU_TREASURE && pu->type != PU_CRATE)) continue;
        if (!seen_at(pu->pos)) continue;
        float k = pop(rise_at(pu->pos, fr));
        if (k <= 0) continue;
        Col c = powerup_color(pu->type);
        bool key = pu->type >= PU_KEY_BLUE;
        float ht = WALL_H * (key ? 1.1f : 0.8f) + sinf(t * 2.2f + i) * 4;
        tether(pu->pos, ht, c, 4);
        V3 at = v3flat(pu->pos, ht);
        holo_model(key ? HM_KEY : HM_SHARD, at, m3_euler(t * 1.6f + i, key ? 0 : 0.5f, 0), px_at(at, key ? 6 : 5) * k, 2, c, 1);
        w_glow(at, px_at(at, 16), col_a(c, 0.3f));
    }
    if (W.reactor.exists && seen_at(W.reactor.pos)) {
        float k = pop(rise_at(W.reactor.pos, fr));
        if (k > 0) {
            V3 at = v3flat(W.reactor.pos, WALL_H * 0.9f);
            float s = maxf(REACTOR_R * 0.8f, px_at(at, 11)) * k;
            if (W.reactor.dead) {
                w_ring(v3flat(W.reactor.pos, 0), AX, AZ, s * 1.3f, 24, 1.6f, col_a(C_GREY, 0.6f));
                w_ring(at, AX, AZ, s, 8, 2, col_a(C_GREY, 0.7f));
            } else holo_reactor3(at, s, t, d->accent, clampf(W.reactor.hp / maxf(1, W.reactor.maxhp), 0, 1));
            label(v3flat(W.reactor.pos, WALL_H * 0.9f + s * 1.9f), "REACTOR", 9, W.reactor.dead ? C_GREY : d->accent);
        }
    }
    if (W.boss_idx >= 0) {
        Robot *b = &W.rob[W.boss_idx];
        if (b->active && seen_at(b->pos)) {
            float k = pop(rise_at(b->pos, fr));
            V3 at = v3flat(b->pos, WALL_H);
            float s = maxf(40, px_at(at, 12)) * k;
            if (k > 0) {
                holo_boss3(at, s, t, RDEF[RB_BOSS].col);
                label(v3flat(b->pos, WALL_H + s * 1.9f), "OVERSEER", 9, RDEF[RB_BOSS].col);
            }
        }
    }
    if (G.reactor_dead) {
        /* the way out: a beam up from the exit */
        V2 p = W.exit_pos;
        float beam = WALL_H * 4;
        w_line(v3flat(p, 0), v3flat(p, beam), 3, col_a(C_GREEN, 0.8f));
        w_line(v3flat(p, 0), v3flat(p, beam), 10, col_a(C_GREEN, 0.15f));
        float ping = fmodf(t * 1.2f, 1);
        w_ring(v3flat(p, 0), AX, AZ, px_at(v3flat(p, 0), 6 + ping * 22), 24, 2, col_a(C_GREEN, 1 - ping));
        V2 v;
        if (wc_project(v3flat(p, beam), &v, NULL)) r_text_glow("EXIT", v.x, v.y - 20, 12, C_GREEN, AL_CENTER);
    }
}

static void ship_draw(void) {
    Player *pl = &W.pl;
    float t = g_time;
    V2 p = pl->pos;
    float h = WALL_H * 1.25f + sinf(t * 2) * 4;
    V3 at = v3flat(p, h), f = v3flat(p, 0);
    w_line(f, at, 1.4f, col_a(C_CYAN, 0.6f));
    for (int r = 0; r < 2; r++) {
        float ping = fmodf(t * 0.9f + r * 0.5f, 1);
        w_ring(f, AX, AZ, px_at(f, 7 + ping * 26), 32, 1.8f, col_a(C_CYAN, (1 - ping) * 0.7f));
    }
    holo_ship3(at, m3_euler(pl->ang, 0, sinf(t * 1.3f) * 0.15f), px_at(at, 11), C_WHITE, 0.3f + 0.1f * sinf(t * 9), 1);
    w_glow(at, px_at(at, 34), col_a(C_CYAN, 0.28f));
}

/* ------------------------------------------------------------ the frame */
void automap_draw(void) {
    float VW = g_virt_w;
    const LevelDef *d = cur_def();
    r_fill_rect(0, 0, VW, VIRT_H, rgba(0, 0.01f, 0.03f, 0.92f));
    camera();
    float fr = front();
    float fk = ease_out_cubic(age() / 0.5f);
    holo_stars(240, 0.6f * fk, 0xA070A9u);
    /* the holo table under the mine */
    w_grid(v3flat(A.ctr, -2), maxf(A.span_x, A.span_y) * 0.75f + 500, TILE * 4, 1.0f, col_a(d->grid, 0.28f * fk));
    floor_draw(d, fr);
    /* the scan front, then a quiet ping now and then */
    V3 sf = v3flat(W.pl.pos, 0);
    if (fr < A.reach + RISE_W) {
        float fade = 1 - smooth01(fr / (A.reach + RISE_W));
        w_ring(sf, AX, AZ, fr, 128, 2.6f, col_a(col_white(d->accent, 0.4f), 0.9f * fade + 0.1f));
        w_ring(sf, AX, AZ, fr, 128, 12, col_a(d->accent, 0.12f));
    } else {
        float ping = fmodf(age() - SWEEP_T, 3.5f) / 1.6f;
        if (ping < 1) w_ring(sf, AX, AZ, 60 + ping * 900, 96, 2, col_a(d->accent, 0.35f * (1 - ping)));
    }
    walls_draw(d, fr);
    doors_draw(fr);
    markers_draw(d, fr);
    ship_draw();
    /* scanlines and a rolling bright band */
    for (float y = 0; y < VIRT_H; y += 4) r_add_rect(0, y, VW, 1, col_a(d->accent, 0.012f));
    float band = fmodf(g_time * 90, VIRT_H + 200) - 100;
    r_add_rect(0, band, VW, 14, col_a(d->accent, 0.02f));
    r_panel(20, 20, VW - 40, 50, d->wall, 0.6f);
    char title[96];
    snprintf(title, sizeof(title), "AUTOMAP - SECTOR %d: %s", G.level + 1, d->name);
    float ts = minf(18, 18 * (VW - 80) / maxf(1, text_width(title, 18)));
    r_text(title, 40, 34 + (18 - ts) * 0.5f, ts, d->wall, AL_LEFT);
    const char *keys = g_in.use_stick ? "BACK CLOSE   LEFT STICK PAN   RIGHT STICK TURN" : "TAB CLOSE   WASD PAN   WHEEL ZOOM   DRAG / Q E TURN";
    /* the controls sit beside the title when there is room, under the objective when not */
    if (40 + text_width(title, ts) + 40 + text_width(keys, 10) <= VW - 40) r_text(keys, VW - 40, 40, 10, col_a(C_WHITE, 0.7f), AL_RIGHT);
    else r_text(keys, VW * 0.5f, VIRT_H - 20, 10, col_a(C_WHITE, 0.6f), AL_CENTER);
    r_text(objective_text(), VW * 0.5f, VIRT_H - 40, 12, col_a(C_WHITE, 0.8f), AL_CENTER);
}
