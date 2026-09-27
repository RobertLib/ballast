/*
 * Wireframe 3D: a small perspective line renderer on top of the glow lines,
 * for the holo briefing and the cinematics. Y points up and the game's 2D
 * plane lies in XZ, so a map point (x, y) sits at (x, 0, y).
 */
#include "common.h"

WCam g_wc;

#define W_NEAR 4.0f
#define W_MAXP 256

/* ---------------------------------------------------------------- rotations */
M3 m3_euler(float heading, float pitch, float roll) {
    float ch = cosf(heading), sh = sinf(heading);
    float cp = cosf(pitch), sp = sinf(pitch);
    float cr = cosf(roll), sr = sinf(roll);
    /* roll about the nose, pitch about the wing, then turn to the heading */
    M3 h = {v3(ch, 0, sh), v3(0, 1, 0), v3(-sh, 0, ch)};
    M3 p = {v3(cp, sp, 0), v3(-sp, cp, 0), v3(0, 0, 1)};
    M3 r = {v3(1, 0, 0), v3(0, cr, sr), v3(0, -sr, cr)};
    return m3_mulm(h, m3_mulm(p, r));
}

M3 m3_axis(V3 k, float a) {
    k = v3norm(k);
    float c = cosf(a), s = sinf(a), t = 1 - c;
    M3 m = {v3(t * k.x * k.x + c, t * k.x * k.y + s * k.z, t * k.x * k.z - s * k.y),
            v3(t * k.x * k.y - s * k.z, t * k.y * k.y + c, t * k.y * k.z + s * k.x),
            v3(t * k.x * k.z + s * k.y, t * k.y * k.z - s * k.x, t * k.z * k.z + c)};
    return m;
}

M3 m3_mulm(M3 a, M3 b) {
    M3 m = {m3mul(a, b.x), m3mul(a, b.y), m3mul(a, b.z)};
    return m;
}

/* ---------------------------------------------------------------- camera */
void wc_look(V3 eye, V3 target, float fov_deg, V2 center, float view_h) {
    WCam *c = &g_wc;
    c->eye = eye;
    c->f = v3norm(v3sub(target, eye));
    V3 up = fabsf(c->f.y) > 0.995f ? v3(0, 0, -1) : v3(0, 1, 0);
    c->r = v3norm(v3cross(c->f, up));
    c->u = v3cross(c->r, c->f);
    c->focal = view_h * 0.5f / tanf(fov_deg * PI / 360.0f);
    c->center = center;
    c->clip.w = 0;
    c->fog_near = c->fog_far = 0;
    c->alpha = 1;
    c->wdepth = 0;
    c->glitch = 0;
}

void wc_roll(float a) {
    V3 r = g_wc.r, u = g_wc.u;
    float c = cosf(a), s = sinf(a);
    g_wc.r = v3add(v3scale(r, c), v3scale(u, s));
    g_wc.u = v3sub(v3scale(u, c), v3scale(r, s));
}

void wc_clip(float x, float y, float w, float h) {
    g_wc.clip.x = x;
    g_wc.clip.y = y;
    g_wc.clip.w = w;
    g_wc.clip.h = h;
}

static inline V3 to_cam(V3 p) {
    V3 d = v3sub(p, g_wc.eye);
    return v3(v3dot(d, g_wc.r), v3dot(d, g_wc.u), v3dot(d, g_wc.f));
}

static inline V2 cam_to_screen(V3 c) {
    float k = g_wc.focal / c.z;
    V2 s = v2(g_wc.center.x + c.x * k, g_wc.center.y - c.y * k);
    if (g_wc.glitch > 0) {
        /* tearing: bands of the picture slide sideways */
        uint32_t h = hash32((uint32_t)(int)floorf(s.y / 9.0f) * 2654435761u ^ g_wc.glitch_seed);
        if ((h & 255) < g_wc.glitch * 200) s.x += (((h >> 8) & 255) / 255.0f - 0.5f) * 70 * g_wc.glitch;
    }
    return s;
}

bool wc_project(V3 p, V2 *out, float *depth) {
    V3 c = to_cam(p);
    if (depth) *depth = c.z;
    if (c.z < W_NEAR) return false;
    *out = cam_to_screen(c);
    return true;
}

float wc_scale_at(V3 p) {
    V3 c = to_cam(p);
    return c.z < W_NEAR ? 0 : g_wc.focal / c.z;
}

static inline Col shade(Col c, float z) {
    float a = g_wc.alpha;
    if (g_wc.fog_far > g_wc.fog_near) a *= 1.0f - 0.88f * clampf((z - g_wc.fog_near) / (g_wc.fog_far - g_wc.fog_near), 0, 1);
    return col_a(c, a);
}

static inline float width_at(float w, float z) { return g_wc.wdepth > 0 ? w * clampf(g_wc.wdepth / z, 0.35f, 2.5f) : w; }

static inline bool inside(V2 p) {
    const SDL_FRect *r = &g_wc.clip;
    return r->w <= 0 || (p.x >= r->x && p.x <= r->x + r->w && p.y >= r->y && p.y <= r->y + r->h);
}

/* Liang-Barsky against the clip rect */
static bool clip2(V2 *a, V2 *b) {
    const SDL_FRect *r = &g_wc.clip;
    if (r->w <= 0) return true;
    float dx = b->x - a->x, dy = b->y - a->y;
    float p[4] = {-dx, dx, -dy, dy};
    float q[4] = {a->x - r->x, r->x + r->w - a->x, a->y - r->y, r->y + r->h - a->y};
    float t0 = 0, t1 = 1;
    for (int i = 0; i < 4; i++) {
        if (fabsf(p[i]) < 1e-9f) {
            if (q[i] < 0) return false;
            continue;
        }
        float t = q[i] / p[i];
        if (p[i] < 0) {
            if (t > t1) return false;
            if (t > t0) t0 = t;
        } else {
            if (t < t0) return false;
            if (t < t1) t1 = t;
        }
    }
    V2 A = *a;
    *a = v2(A.x + dx * t0, A.y + dy * t0);
    *b = v2(A.x + dx * t1, A.y + dy * t1);
    return true;
}

/* ---------------------------------------------------------------- primitives */
static void line_cam(V3 ca, V3 cb, float w, Col c) {
    if (ca.z < W_NEAR && cb.z < W_NEAR) return;
    if (ca.z < W_NEAR) ca = v3lerp(ca, cb, (W_NEAR - ca.z) / (cb.z - ca.z));
    else if (cb.z < W_NEAR) cb = v3lerp(cb, ca, (W_NEAR - cb.z) / (ca.z - cb.z));
    V2 sa = cam_to_screen(ca), sb = cam_to_screen(cb);
    if (!clip2(&sa, &sb)) return;
    float z = (ca.z + cb.z) * 0.5f;
    r_line(sa, sb, width_at(w, z), shade(c, z));
}

void w_line(V3 a, V3 b, float w, Col c) { line_cam(to_cam(a), to_cam(b), w, c); }

static void poly_impl(const V3 *p, const Col *cols, Col base, int n, bool closed, float w) {
    if (n < 2) return;
    if (n > W_MAXP) n = W_MAXP;
    static V3 cp[W_MAXP];
    static V2 sp[W_MAXP];
    static Col sc[W_MAXP];
    bool simple = true;
    float zs = 0;
    for (int i = 0; i < n; i++) {
        cp[i] = to_cam(p[i]);
        if (cp[i].z < W_NEAR) {
            simple = false;
            continue;
        }
        sp[i] = cam_to_screen(cp[i]);
        if (!inside(sp[i])) simple = false;
        zs += cp[i].z;
        sc[i] = shade(cols ? cols[i] : base, cp[i].z);
    }
    if (simple) {
        r_polyline_cols(sp, sc, n, closed, width_at(w, zs / n));
        return;
    }
    /* crossing the near plane or the clip rect: segment by segment */
    int m = closed ? n : n - 1;
    for (int i = 0; i < m; i++) {
        int j = (i + 1) % n;
        line_cam(cp[i], cp[j], w, cols ? col_lerp(cols[i], cols[j], 0.5f) : base);
    }
}

void w_poly(const V3 *p, int n, bool closed, float w, Col c) { poly_impl(p, NULL, c, n, closed, w); }
void w_poly_cols(const V3 *p, const Col *cols, int n, bool closed, float w) { poly_impl(p, cols, C_WHITE, n, closed, w); }

void w_poly_reveal(const V3 *p, int n, bool closed, float w, Col c, float k) {
    if (k <= 0 || n < 2) return;
    if (k >= 1) {
        poly_impl(p, NULL, c, n, closed, w);
        return;
    }
    int m = closed ? n + 1 : n;
    if (m > W_MAXP) m = W_MAXP;
    float total = 0;
    for (int i = 1; i < m; i++) total += v3dist(p[(i - 1) % n], p[i % n]);
    float want = total * k;
    static V3 tmp[W_MAXP];
    int t = 0;
    tmp[t++] = p[0];
    for (int i = 1; i < m; i++) {
        V3 a = p[(i - 1) % n], b = p[i % n];
        float l = v3dist(a, b);
        if (want <= l) {
            tmp[t++] = v3lerp(a, b, l > 0 ? want / l : 1);
            break;
        }
        want -= l;
        tmp[t++] = b;
    }
    poly_impl(tmp, NULL, c, t, false, w);
    w_dot(tmp[t - 1], w * 3.2f, col_a(col_white(c, 0.6f), 0.8f * c.a));
}

void w_glow(V3 p, float radius, Col c) {
    V3 cp = to_cam(p);
    if (cp.z < W_NEAR) return;
    V2 s = cam_to_screen(cp);
    if (!inside(s)) return;
    r_glow(s, radius * g_wc.focal / cp.z, shade(c, cp.z));
}

void w_dot(V3 p, float px, Col c) {
    V3 cp = to_cam(p);
    if (cp.z < W_NEAR) return;
    V2 s = cam_to_screen(cp);
    if (!inside(s)) return;
    r_glow(s, px, shade(c, cp.z));
}

void w_arc(V3 c, V3 ax, V3 ay, float r, float a0, float a1, int segs, float w, Col col) {
    V3 pts[W_MAXP];
    if (segs > W_MAXP - 1) segs = W_MAXP - 1;
    if (segs < 1) segs = 1;
    for (int i = 0; i <= segs; i++) {
        float a = a0 + (a1 - a0) * i / segs;
        pts[i] = v3add(c, v3add(v3scale(ax, cosf(a) * r), v3scale(ay, sinf(a) * r)));
    }
    poly_impl(pts, NULL, col, segs + 1, false, w);
}

void w_ring(V3 c, V3 ax, V3 ay, float r, int segs, float w, Col col) {
    V3 pts[W_MAXP];
    if (segs > W_MAXP) segs = W_MAXP;
    if (segs < 3) segs = 3;
    for (int i = 0; i < segs; i++) {
        float a = TAU * i / segs;
        pts[i] = v3add(c, v3add(v3scale(ax, cosf(a) * r), v3scale(ay, sinf(a) * r)));
    }
    poly_impl(pts, NULL, col, segs, true, w);
}

void w_box(V3 lo, V3 hi, float w, Col c) {
    V3 b[4] = {v3(lo.x, lo.y, lo.z), v3(hi.x, lo.y, lo.z), v3(hi.x, lo.y, hi.z), v3(lo.x, lo.y, hi.z)};
    V3 t[4] = {v3(lo.x, hi.y, lo.z), v3(hi.x, hi.y, lo.z), v3(hi.x, hi.y, hi.z), v3(lo.x, hi.y, hi.z)};
    poly_impl(b, NULL, col_a(c, 0.6f), 4, true, w);
    poly_impl(t, NULL, c, 4, true, w);
    for (int i = 0; i < 4; i++) w_line(b[i], t[i], w, col_a(c, 0.75f));
}

void w_fill(const V3 *p, int n, Col c) {
    if (n < 3) return;
    if (n > 16) n = 16;
    /* Sutherland-Hodgman against the near plane, then a fan */
    V3 in[16], out[17];
    int m = 0;
    for (int i = 0; i < n; i++) in[i] = to_cam(p[i]);
    for (int i = 0; i < n; i++) {
        V3 a = in[i], b = in[(i + 1) % n];
        bool ia = a.z >= W_NEAR, ib = b.z >= W_NEAR;
        if (ia) out[m++] = a;
        if (ia != ib) out[m++] = v3lerp(a, b, (W_NEAR - a.z) / (b.z - a.z));
    }
    if (m < 3) return;
    V2 s[17];
    float zs = 0;
    for (int i = 0; i < m; i++) {
        s[i] = cam_to_screen(out[i]);
        zs += out[i].z;
    }
    Col sc = shade(c, zs / m);
    for (int i = 1; i + 1 < m; i += 2) r_add_quad(s[0], s[i], s[i + 1], i + 2 < m ? s[i + 2] : s[i + 1], sc);
}

void w_grid(V3 c, float half, float step, float w, Col col) {
    int n = (int)(half / step);
    V3 pts[17];
    Col cols[17];
    for (int axis = 0; axis < 2; axis++)
        for (int i = -n; i <= n; i++) {
            float off = i * step;
            for (int k = 0; k <= 16; k++) {
                float along = -half + 2 * half * k / 16;
                V3 d = axis ? v3(off, 0, along) : v3(along, 0, off);
                pts[k] = v3add(c, d);
                float r = sqrtf(off * off + along * along) / half;
                cols[k] = col_a(col, clampf(1 - r * r, 0, 1));
            }
            poly_impl(pts, cols, col, 17, false, w);
        }
}

void w_text(const char *s, V3 at, V3 ax, V3 ay, float size, float w, Col c, int align) {
    float u = size / 6.0f;
    int len = (int)strlen(s);
    float width = len > 0 ? (len * FONT_ADV - 1.6f) * u : 0;
    float x0 = align == AL_CENTER ? -width * 0.5f : align == AL_RIGHT ? -width : 0;
    for (int i = 0; i < len; i++) {
        const V2 *gp;
        const int *st, *ln;
        int ns = font_glyph((unsigned char)s[i], &gp, &st, &ln);
        for (int k = 0; k < ns; k++) {
            V3 pts[40];
            int n = ln[k];
            for (int j = 0; j < n && j < 40; j++) {
                V2 q = gp[st[k] + j];
                float gx = x0 + (i * FONT_ADV + q.x) * u, gy = q.y * u;
                pts[j] = v3add(at, v3add(v3scale(ax, gx), v3scale(ay, gy)));
            }
            if (n == 1) w_dot(pts[0], w, c);
            else poly_impl(pts, NULL, c, n, false, w);
        }
    }
}

/* ---------------------------------------------------------------- models */
void wm_clear(WModel *m) { memset(m, 0, sizeof(*m)); }

void wm_stroke(WModel *m, const V3 *p, int n, bool closed) {
    if (m->ns >= WM_MAXS || m->np + n > WM_MAXP || n < 2) return;
    m->start[m->ns] = m->np;
    m->len[m->ns] = n;
    m->closed[m->ns] = closed;
    memcpy(&m->p[m->np], p, sizeof(V3) * (size_t)n);
    m->np += n;
    m->ns++;
}

void wm_extrude(WModel *m, const Shape *s, float y0, float y1, float top_scale) {
    for (int i = 0; i < s->ns; i++) {
        const Stroke *st = &s->s[i];
        V3 lo[18], hi[18];
        for (int k = 0; k < st->n; k++) {
            lo[k] = v3(st->p[k].x, y0, st->p[k].y);
            hi[k] = v3(st->p[k].x * top_scale, y1, st->p[k].y * top_scale);
        }
        wm_stroke(m, hi, st->n, st->closed);
        if (y1 - y0 < 1e-4f) continue;
        wm_stroke(m, lo, st->n, st->closed);
        /* the first stroke is the hull: tie its corners together */
        if (i == 0)
            for (int k = 0; k < st->n; k++) {
                V3 e[2] = {lo[k], hi[k]};
                wm_stroke(m, e, 2, false);
            }
    }
}

static float bumpf(float lat, float lon, uint32_t seed) {
    float s1 = (seed & 255) * 0.1f, s2 = ((seed >> 8) & 255) * 0.1f, s3 = ((seed >> 16) & 255) * 0.1f;
    return 0.55f * sinf(3 * lon + s1) * sinf(2 * lat + s2) + 0.3f * sinf(5 * lon + s3) * cosf(3 * lat + s1) + 0.15f * sinf(9 * lon + 7 * lat + s2);
}

void wm_sphere(WModel *m, int lats, int lons, float bump, uint32_t seed) {
    m->sphere = true;
    const int ring = 32, meri = 17;
    for (int i = 1; i <= lats; i++) {
        float la = -PI / 2 + PI * i / (lats + 1);
        V3 pts[32];
        for (int k = 0; k < ring; k++) {
            float lo = TAU * k / ring;
            float r = 1 + bump * bumpf(la, lo, seed);
            pts[k] = v3(cosf(la) * cosf(lo) * r, sinf(la) * r, cosf(la) * sinf(lo) * r);
        }
        wm_stroke(m, pts, ring, true);
    }
    for (int k = 0; k < lons; k++) {
        float lo = TAU * k / lons;
        V3 pts[17];
        for (int i = 0; i < meri; i++) {
            float la = -PI / 2 + PI * i / (meri - 1);
            float r = 1 + bump * bumpf(la, lo, seed);
            pts[i] = v3(cosf(la) * cosf(lo) * r, sinf(la) * r, cosf(la) * sinf(lo) * r);
        }
        wm_stroke(m, pts, meri, false);
    }
}

void wm_octahedron(WModel *m, float r, float h) {
    V3 top = v3(0, h, 0), bot = v3(0, -h, 0);
    V3 eq[4] = {v3(r, 0, 0), v3(0, 0, r), v3(-r, 0, 0), v3(0, 0, -r)};
    wm_stroke(m, eq, 4, true);
    for (int i = 0; i < 4; i++) {
        V3 e[3] = {top, eq[i], bot};
        wm_stroke(m, e, 3, false);
    }
}

static inline V3 xform(V3 p, V3 pos, M3 rot, float scale) { return v3add(pos, m3mul(rot, v3scale(p, scale))); }

static void wm_stroke_draw(const WModel *m, int s, V3 pos, M3 rot, float scale, float w, Col c, float k) {
    V3 pts[W_MAXP];
    Col cols[W_MAXP];
    int n = m->len[s];
    if (n > W_MAXP) n = W_MAXP;
    for (int i = 0; i < n; i++) {
        V3 q = m->p[m->start[s] + i];
        pts[i] = xform(q, pos, rot, scale);
        if (m->sphere) {
            /* the far side of a globe shows through, dimmer */
            V3 nrm = v3norm(m3mul(rot, q));
            float facing = v3dot(nrm, v3norm(v3sub(g_wc.eye, pts[i])));
            cols[i] = col_a(c, lerpf(0.16f, 1.0f, clampf(facing * 3 + 0.5f, 0, 1)));
        }
    }
    if (k < 1) w_poly_reveal(pts, n, m->closed[s], w, c, k);
    else if (m->sphere) poly_impl(pts, cols, c, n, m->closed[s], w);
    else poly_impl(pts, NULL, c, n, m->closed[s], w);
}

void wm_draw(const WModel *m, V3 pos, M3 rot, float scale, float w, Col c) {
    for (int s = 0; s < m->ns; s++) wm_stroke_draw(m, s, pos, rot, scale, w, c, 1);
}

void wm_draw_reveal(const WModel *m, V3 pos, M3 rot, float scale, float w, Col c, float k) {
    if (k >= 1) {
        wm_draw(m, pos, rot, scale, w, c);
        return;
    }
    /* the strokes draw on one after another, overlapping */
    const float span = 0.45f;
    for (int s = 0; s < m->ns; s++) {
        float st = m->ns > 1 ? (float)s / (m->ns - 1) * (1 - span) : 0;
        float ks = clampf((k - st) / span, 0, 1);
        if (ks > 0) wm_stroke_draw(m, s, pos, rot, scale, w, c, ks);
    }
}

void wm_draw_explode(const WModel *m, V3 pos, M3 rot, float scale, float w, Col c, float te, uint32_t seed) {
    if (te < 0) {
        wm_draw(m, pos, rot, scale, w, c);
        return;
    }
    float fade = clampf(1 - te / 2.2f, 0, 1);
    if (fade <= 0) return;
    Col hot = col_a(col_white(c, clampf(1 - te * 2.5f, 0, 1) * 0.8f), fade);
    float travel = te / (1 + te * 0.9f);
    int id = 0;
    for (int s = 0; s < m->ns; s++) {
        int n = m->len[s];
        int segs = m->closed[s] ? n : n - 1;
        for (int i = 0; i < segs; i++, id++) {
            V3 a = xform(m->p[m->start[s] + i], v3(0, 0, 0), rot, scale);
            V3 b = xform(m->p[m->start[s] + (i + 1) % n], v3(0, 0, 0), rot, scale);
            V3 mid = v3scale(v3add(a, b), 0.5f), half = v3scale(v3sub(b, a), 0.5f);
            uint32_t h = hash32(seed + (uint32_t)id * 7919u);
            V3 dir = v3norm(v3add(v3norm(mid), v3((h & 255) / 128.0f - 1, ((h >> 8) & 255) / 128.0f - 1, ((h >> 16) & 255) / 128.0f - 1)));
            float speed = (1.2f + ((h >> 24) & 255) / 255.0f * 2.2f) * scale;
            V3 axis = v3norm(v3((h >> 3 & 255) / 128.0f - 1, (h >> 11 & 255) / 128.0f - 1, (h >> 19 & 255) / 128.0f - 1));
            M3 spin = m3_axis(axis, te * (3 + (h & 7)));
            V3 at = v3add(pos, v3mad(mid, dir, speed * travel * 2.2f));
            V3 hr = m3mul(spin, half);
            w_line(v3sub(at, hr), v3add(at, hr), w, hot);
        }
    }
}
