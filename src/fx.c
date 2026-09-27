/*
 * Visual effects: spring-mass warping background grid, particles,
 * debris, shock rings, lights and floating score popups.
 */
#include "common.h"

/* ------------------------------------------------------------ popups */
typedef struct { V2 pos; char text[24]; Col col; float life, size; } Popup;
#define MAXPOP 48
static Popup pops[MAXPOP];
static int popn;


/* ------------------------------------------------------------ grid */
static int gw, gh;
static float gsp;
static V2 *gd, *gv;

void grid_free(void) {
    free(gd);
    free(gv);
    gd = gv = NULL;
    gw = gh = 0;
}

void grid_init(float world_w, float world_h, float spacing) {
    grid_free();
    gsp = spacing;
    gw = (int)(world_w / spacing) + 2;
    gh = (int)(world_h / spacing) + 2;
    gd = (V2 *)calloc((size_t)(gw * gh), sizeof(V2));
    gv = (V2 *)calloc((size_t)(gw * gh), sizeof(V2));
}

void grid_impulse(V2 p, float radius, float strength) {
    if (!gd) return;
    int x0 = (int)((p.x - radius) / gsp), x1 = (int)((p.x + radius) / gsp) + 1;
    int y0 = (int)((p.y - radius) / gsp), y1 = (int)((p.y + radius) / gsp) + 1;
    x0 = clampi(x0, 1, gw - 2); x1 = clampi(x1, 1, gw - 2);
    y0 = clampi(y0, 1, gh - 2); y1 = clampi(y1, 1, gh - 2);
    float r2 = radius * radius;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            int i = y * gw + x;
            V2 pos = v2(x * gsp + gd[i].x, y * gsp + gd[i].y);
            V2 d = v2sub(pos, p);
            float d2 = v2len2(d);
            if (d2 >= r2 || d2 < 1e-4f) continue;
            float dist = sqrtf(d2);
            float f = 1.0f - dist / radius;
            f *= f;
            gv[i] = v2mad(gv[i], v2scale(d, 1.0f / dist), strength * f);
        }
}

void grid_update(float dt) {
    if (!gd) return;
    const float K = 55.0f, A = 14.0f, D = 4.5f;
    float lim = gsp * 0.85f;
    /* only simulate around the camera: the rest settles naturally */
    int cx = (int)(g_cam.pos.x / gsp), cy = (int)(g_cam.pos.y / gsp);
    int rx = (int)(g_virt_w / g_cam.zoom / gsp) / 2 + 12, ry = (int)(VIRT_H / g_cam.zoom / gsp) / 2 + 12;
    int x0 = clampi(cx - rx, 1, gw - 2), x1 = clampi(cx + rx, 1, gw - 2);
    int y0 = clampi(cy - ry, 1, gh - 2), y1 = clampi(cy + ry, 1, gh - 2);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            int i = y * gw + x;
            V2 lap = v2(gd[i - 1].x + gd[i + 1].x + gd[i - gw].x + gd[i + gw].x - 4 * gd[i].x,
                        gd[i - 1].y + gd[i + 1].y + gd[i - gw].y + gd[i + gw].y - 4 * gd[i].y);
            V2 acc = v2(K * lap.x - A * gd[i].x - D * gv[i].x, K * lap.y - A * gd[i].y - D * gv[i].y);
            gv[i] = v2mad(gv[i], acc, dt);
        }
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            int i = y * gw + x;
            gd[i] = v2mad(gd[i], gv[i], dt);
            float l2 = v2len2(gd[i]);
            if (l2 > lim * lim) gd[i] = v2scale(gd[i], lim / sqrtf(l2));
        }
}

void grid_draw(Col base, float brightness) {
    if (!gd) return;
    V2 tl = v2w(v2(-40, -40)), br = v2w(v2(g_virt_w + 40, VIRT_H + 40));
    int x0 = clampi((int)(tl.x / gsp) - 1, 0, gw - 1), x1 = clampi((int)(br.x / gsp) + 1, 0, gw - 1);
    int y0 = clampi((int)(tl.y / gsp) - 1, 0, gh - 1), y1 = clampi((int)(br.y / gsp) + 1, 0, gh - 1);
    static V2 pts[512];
    static Col cols[512];
    float w = 2.6f * g_cam.zoom;
    for (int pass = 0; pass < 2; pass++) {
        int outer0 = pass == 0 ? y0 : x0, outer1 = pass == 0 ? y1 : x1;
        int inner0 = pass == 0 ? x0 : y0, inner1 = pass == 0 ? x1 : y1;
        for (int o = outer0; o <= outer1; o++) {
            bool major = (o % 4) == 0;
            float kb = (major ? 0.55f : 0.28f) * brightness;
            int n = 0;
            for (int k = inner0; k <= inner1 && n < 512; k++) {
                int x = pass == 0 ? k : o, y = pass == 0 ? o : k;
                int i = y * gw + x;
                V2 p = v2(x * gsp + gd[i].x, y * gsp + gd[i].y);
                pts[n] = w2v(p);
                float disp = minf(1.0f, sqrtf(v2len2(gd[i])) / (gsp * 0.5f));
                float e = kb * (1.0f + disp * 2.2f);
                cols[n] = rgba(base.r * e + disp * 0.25f, base.g * e + disp * 0.25f, base.b * e + disp * 0.3f, 1);
                n++;
            }
            if (n >= 2) r_polyline_cols(pts, cols, n, false, major ? w * 1.3f : w);
        }
    }
}

/* ------------------------------------------------------------ particles */
enum { P_SPARK, P_GLOW, P_DEBRIS, P_RING, P_LIGHT };
typedef struct {
    V2 pos, vel;
    float life, maxlife;
    Col col;
    float size;
    float ang, angv, len;
    float drag;
    uint8_t type;
} Particle;

#define MAXP 9000
static Particle parts[MAXP];
static int pnext;
static int pcount;

static Particle *new_part(void) {
    Particle *p = &parts[pnext];
    pnext = (pnext + 1) % MAXP;
    if (p->life <= 0) pcount++;
    memset(p, 0, sizeof(*p));
    return p;
}

void fx_clear(void) {
    memset(parts, 0, sizeof(parts));
    memset(pops, 0, sizeof(pops));
    pnext = 0;
    pcount = 0;
}

void fx_spark(V2 pos, V2 vel, Col c, float life, float width) {
    Particle *p = new_part();
    p->type = P_SPARK;
    p->pos = pos; p->vel = vel;
    p->life = p->maxlife = life;
    p->col = c;
    p->size = width;
    p->drag = 2.6f;
}

void fx_glow(V2 pos, V2 vel, Col c, float life, float size) {
    Particle *p = new_part();
    p->type = P_GLOW;
    p->pos = pos; p->vel = vel;
    p->life = p->maxlife = life;
    p->col = c;
    p->size = size;
    p->drag = 1.5f;
}

void fx_debris(V2 pos, V2 vel, float ang, float angv, float len, Col c, float life) {
    Particle *p = new_part();
    p->type = P_DEBRIS;
    p->pos = pos; p->vel = vel;
    p->ang = ang; p->angv = angv; p->len = len;
    p->life = p->maxlife = life;
    p->col = c;
    p->size = 4.0f;
    p->drag = 1.4f;
}

void fx_ring(V2 pos, float r0, float r1, Col c, float life, float width) {
    Particle *p = new_part();
    p->type = P_RING;
    p->pos = pos;
    p->ang = r0; p->len = r1;
    p->life = p->maxlife = life;
    p->col = c;
    p->size = width;
}

void fx_light(V2 pos, float radius, Col c, float life) {
    Particle *p = new_part();
    p->type = P_LIGHT;
    p->pos = pos;
    p->size = radius;
    p->life = p->maxlife = life;
    p->col = c;
}

void fx_burst(V2 pos, int n, Col c, float speed, float life, float width) {
    for (int i = 0; i < n; i++) {
        float a = frand() * TAU;
        float s = speed * (0.25f + 0.75f * frand());
        fx_spark(pos, v2scale(v2fromang(a), s), col_white(c, frand() * 0.4f), life * (0.5f + 0.5f * frand()), width);
    }
}

void fx_explosion(V2 pos, float size, Col c) {
    int n = (int)(18 + size * 1.6f);
    if (n > 220) n = 220;
    fx_burst(pos, n, c, 180 + size * 9, 0.6f + size * 0.008f, 3.6f + size * 0.02f);
    fx_burst(pos, n / 3, rgba(1, 0.95f, 0.8f, 1), 120 + size * 5, 0.35f, 3.0f);
    for (int i = 0; i < 4 + (int)(size / 12); i++)
        fx_glow(v2add(pos, v2scale(v2fromang(frand() * TAU), frand() * size * 0.4f)),
                v2scale(v2fromang(frand() * TAU), 40 + frand() * 60), col_a(c, 0.5f), 0.4f + frand() * 0.4f,
                size * (0.5f + frand() * 0.6f));
    fx_ring(pos, size * 0.2f, size * 1.8f + 20, col_white(c, 0.3f), 0.35f + size * 0.004f, 5 + size * 0.05f);
    fx_light(pos, size * 3.5f + 60, col_a(c, 0.55f), 0.5f + size * 0.004f);
    grid_impulse(pos, size * 3.0f + 60, size * 6.0f + 120);
}

void fx_shape_debris(const Shape *s, V2 pos, float ang, float scale, V2 vel, Col c) {
    for (int i = 0; i < s->ns; i++) {
        const Stroke *st = &s->s[i];
        int segs = st->closed ? st->n : st->n - 1;
        for (int k = 0; k < segs; k++) {
            V2 a = v2rot(v2scale(st->p[k], scale), ang);
            V2 b = v2rot(v2scale(st->p[(k + 1) % st->n], scale), ang);
            V2 mid = v2scale(v2add(a, b), 0.5f);
            V2 dir = v2sub(b, a);
            float len = v2len(dir) * 0.5f;
            V2 out = v2norm(mid);
            if (v2len2(out) < 0.01f) out = v2fromang(frand() * TAU);
            V2 v = v2add(v2scale(vel, 0.4f), v2scale(out, 90 + frand() * 200));
            v = v2add(v, v2scale(v2fromang(frand() * TAU), 40));
            fx_debris(v2add(pos, mid), v, v2ang(dir), frandr(-9, 9), len, c, 0.9f + frand() * 0.7f);
        }
    }
}

void fx_update(float dt) {
    for (int i = 0; i < MAXPOP; i++)
        if (pops[i].life > 0) pops[i].life -= dt;
    for (int i = 0; i < MAXP; i++) {
        Particle *p = &parts[i];
        if (p->life <= 0) continue;
        p->life -= dt;
        if (p->life <= 0) { pcount--; continue; }
        if (p->type == P_RING || p->type == P_LIGHT) continue;
        p->pos = v2mad(p->pos, p->vel, dt);
        float k = expf(-p->drag * dt);
        p->vel = v2scale(p->vel, k);
        p->ang += p->angv * dt;
        if (p->type == P_SPARK || p->type == P_DEBRIS) {
            /* sparks bounce off walls */
            if (g_fx_collide && point_in_rock(p->pos)) {
                p->vel = v2scale(p->vel, -0.4f);
                p->pos = v2mad(p->pos, p->vel, dt * 2);
            }
        }
    }
}

void fx_draw(void) {
    for (int i = 0; i < MAXP; i++) {
        Particle *p = &parts[i];
        if (p->life <= 0) continue;
        float t = p->life / p->maxlife;
        switch (p->type) {
        case P_SPARK: {
            if (!rw_visible(p->pos, 60)) break;
            float sp = v2len(p->vel);
            V2 tail = v2mad(p->pos, p->vel, -minf(0.05f, 26.0f / maxf(sp, 1.0f)) * (0.4f + t));
            Col c = p->col;
            c.a *= t * 1.2f;
            rw_line(tail, p->pos, p->size, c);
        } break;
        case P_GLOW: {
            if (!rw_visible(p->pos, p->size)) break;
            Col c = p->col;
            c.a *= t;
            rw_glow(p->pos, p->size * (1.3f - 0.3f * t), c);
        } break;
        case P_DEBRIS: {
            if (!rw_visible(p->pos, p->len + 10)) break;
            V2 d = v2scale(v2fromang(p->ang), p->len);
            Col c = p->col;
            c.a *= minf(1, t * 1.6f);
            rw_line(v2sub(p->pos, d), v2add(p->pos, d), p->size, c);
        } break;
        case P_RING: {
            float r = lerpf(p->len, p->ang, t * t);
            if (!rw_visible(p->pos, r + 10)) break;
            Col c = p->col;
            c.a *= t;
            rw_circle(p->pos, r, p->size * (0.4f + t), c, 48);
        } break;
        case P_LIGHT: {
            if (!rw_visible(p->pos, p->size)) break;
            Col c = p->col;
            c.a *= t * t;
            rw_glow(p->pos, p->size, c);
        } break;
        }
    }
}

void fx_popup(V2 pos, const char *text, Col c, float size) {
    Popup *p = &pops[popn];
    popn = (popn + 1) % MAXPOP;
    p->pos = pos;
    snprintf(p->text, sizeof(p->text), "%s", text);
    p->col = c;
    p->life = 1.1f;
    p->size = size;
}

void fx_draw_popups(void) {
    for (int i = 0; i < MAXPOP; i++) {
        Popup *p = &pops[i];
        if (p->life <= 0) continue;
        float t = clampf(p->life / 1.1f, 0, 1);
        V2 v = w2v(v2(p->pos.x, p->pos.y - (1 - t) * 40));
        Col c = p->col;
        c.a *= minf(1, t * 2.5f);
        r_text(p->text, v.x, v.y, p->size, c, AL_CENTER);
    }
}

/* ------------------------------------------------------------ shake */
float g_trauma = 0;
void shake_add(float trauma) { g_trauma = minf(1.0f, g_trauma + trauma); }
