/*
 * Renderer: batched additive glow geometry on top of SDL_Renderer,
 * a multi-resolution bloom post-process and a stroked vector font.
 */
#include "common.h"
#include <stdarg.h>

SDL_Window *g_window;
SDL_Renderer *g_ren;
int g_pix_w = 1280, g_pix_h = 720;
float g_scale = 1.0f;
float g_virt_w = 1280.0f;
Camera g_cam = {{0, 0}, 1.0f, {0, 0}, {0, 0}};
const char *g_screenshot_path = NULL;

/* ---------------------------------------------------------------- rng */
static uint32_t rng_state = 0x9E3779B9u;
uint32_t rng_next(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}
void rng_seed(uint32_t s) { rng_state = s ? s : 0x9E3779B9u; }
float frand(void) { return (rng_next() >> 8) * (1.0f / 16777216.0f); }
float frandr(float a, float b) { return a + (b - a) * frand(); }
int irand(int n) { return n > 0 ? (int)(rng_next() % (uint32_t)n) : 0; }

/* ---------------------------------------------------------------- textures */
#define ATLAS_W 192
#define ATLAS_H 64
static SDL_Texture *tex_add, *tex_blend, *tex_rock;
static SDL_Texture *scene;
#define NBLOOM 5
static SDL_Texture *bl[NBLOOM], *blt[NBLOOM];
static int blw[NBLOOM], blh[NBLOOM];
static SDL_BlendMode bm_sub;
static bool has_sub = false;

/* uv regions inside the atlas */
static const float LU0 = 0.5f / ATLAS_W, LUM = 32.0f / ATLAS_W, LU1 = 63.5f / ATLAS_W;
static const float GU0 = 64.5f / ATLAS_W, GU1 = 127.5f / ATLAS_W;
static const float SU = 160.0f / ATLAS_W, SV = 0.5f;
static const float AV0 = 0.5f / ATLAS_H, AVM = 32.0f / ATLAS_H, AV1 = 63.5f / ATLAS_H;

static SDL_Texture *make_atlas(SDL_BlendMode mode) {
    SDL_Surface *s = SDL_CreateSurface(ATLAS_W, ATLAS_H, SDL_PIXELFORMAT_RGBA32);
    if (!s) return NULL;
    uint8_t *px = (uint8_t *)s->pixels;
    for (int y = 0; y < ATLAS_H; y++) {
        for (int x = 0; x < ATLAS_W; x++) {
            float a = 0;
            int lx = x % 64;
            float dx = (lx + 0.5f - 32.0f) / 32.0f, dy = (y + 0.5f - 32.0f) / 32.0f;
            float d = sqrtf(dx * dx + dy * dy);
            if (x < 64) {
                /* glow line profile: bright core + soft halo */
                float core = 1.0f - smooth01((d - 0.16f) / 0.16f);
                float halo = d < 1.0f ? 0.42f * powf(1.0f - d, 2.0f) : 0.0f;
                a = maxf(core, halo);
            } else if (x < 128) {
                /* soft gaussian blob */
                a = d < 1.0f ? expf(-d * d * 4.5f) * (1.0f - d * d) : 0.0f;
            } else {
                a = 1.0f;
            }
            uint8_t *p = px + y * s->pitch + x * 4;
            p[0] = 255; p[1] = 255; p[2] = 255;
            p[3] = (uint8_t)clampf(a * 255.0f + 0.5f, 0, 255);
        }
    }
    SDL_Texture *t = SDL_CreateTextureFromSurface(g_ren, s);
    SDL_DestroySurface(s);
    if (t) {
        SDL_SetTextureBlendMode(t, mode);
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    }
    return t;
}

static SDL_Texture *make_rock(void) {
    const int N = 64;
    SDL_Surface *s = SDL_CreateSurface(N, N, SDL_PIXELFORMAT_RGBA32);
    if (!s) return NULL;
    uint8_t *px = (uint8_t *)s->pixels;
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            /* faint diagonal hatching, seamless per tile */
            int k = (x + y) % 16;
            float v = 0.55f;
            if (k == 0) v = 1.0f;
            else if (k == 1 || k == 15) v = 0.75f;
            /* subtle speckle */
            uint32_t h = (uint32_t)(x * 73856093u) ^ (uint32_t)(y * 19349663u);
            h = (h ^ (h >> 13)) * 0x5bd1e995u;
            v += ((h >> 24) & 15) / 255.0f;
            uint8_t *p = px + y * s->pitch + x * 4;
            uint8_t c = (uint8_t)clampf(v * 255, 0, 255);
            p[0] = c; p[1] = c; p[2] = c; p[3] = 255;
        }
    }
    SDL_Texture *t = SDL_CreateTextureFromSurface(g_ren, s);
    SDL_DestroySurface(s);
    if (t) {
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    }
    return t;
}

static void destroy_targets(void) {
    if (scene) SDL_DestroyTexture(scene);
    scene = NULL;
    for (int i = 0; i < NBLOOM; i++) {
        if (bl[i]) SDL_DestroyTexture(bl[i]);
        if (blt[i]) SDL_DestroyTexture(blt[i]);
        bl[i] = blt[i] = NULL;
    }
}

static SDL_Texture *make_target(int w, int h) {
    SDL_Texture *t = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (t) SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    return t;
}

static void create_targets(void) {
    destroy_targets();
    scene = make_target(g_pix_w, g_pix_h);
    int w = g_pix_w / 2, h = g_pix_h / 2;
    for (int i = 0; i < NBLOOM; i++) {
        if (w < 2) w = 2;
        if (h < 2) h = 2;
        blw[i] = w; blh[i] = h;
        bl[i] = make_target(w, h);
        blt[i] = make_target(w, h);
        w /= 2; h /= 2;
    }
}

static void update_metrics(void) {
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(g_ren, &w, &h);
    if (w < 16) w = 16;
    if (h < 16) h = 16;
    g_pix_w = w; g_pix_h = h;
    g_scale = (float)h / VIRT_H;
    g_virt_w = (float)w / g_scale;
}

bool render_init(void) {
    update_metrics();
    tex_add = make_atlas(SDL_BLENDMODE_ADD);
    tex_blend = make_atlas(SDL_BLENDMODE_BLEND);
    tex_rock = make_rock();
    if (!tex_add || !tex_blend || !tex_rock) return false;
    bm_sub = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT,
                                        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
    has_sub = SDL_SetRenderDrawBlendMode(g_ren, bm_sub);
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
    create_targets();
    return scene != NULL;
}

void render_shutdown(void) {
    destroy_targets();
    if (tex_add) SDL_DestroyTexture(tex_add);
    if (tex_blend) SDL_DestroyTexture(tex_blend);
    if (tex_rock) SDL_DestroyTexture(tex_rock);
}

void render_check_resize(void) {
    int ow = g_pix_w, oh = g_pix_h;
    update_metrics();
    if (ow != g_pix_w || oh != g_pix_h || !scene) create_targets();
}

/* ---------------------------------------------------------------- batching */
#define MAXV 60000
#define MAXI 90000
static SDL_Vertex vb[MAXV];
static int ib[MAXI];
static int nv, ni;
static SDL_Texture *cur_tex;

void r_flush(void) {
    if (nv > 0 && ni > 0) SDL_RenderGeometry(g_ren, cur_tex, vb, nv, ib, ni);
    nv = ni = 0;
}

static inline void use_tex(SDL_Texture *t, int needv, int needi) {
    if (t != cur_tex) {
        r_flush();
        cur_tex = t;
    }
    if (nv + needv > MAXV || ni + needi > MAXI) r_flush();
}

/* the group transform of r_push: p * s + b, alpha multiplied */
typedef struct { float sx, sy, bx, by, a; } Xform;
#define XF_DEPTH 8
static Xform xf_stack[XF_DEPTH];
static int xf_n = 0;
static Xform xf = {1, 1, 0, 0, 1};

void r_push(V2 off, V2 org, float sx, float sy, float alpha) {
    if (xf_n < XF_DEPTH) xf_stack[xf_n] = xf;
    xf_n++;
    /* the local transform org + (p - org) * s + off, then the parent's */
    float lbx = org.x * (1 - sx) + off.x, lby = org.y * (1 - sy) + off.y;
    xf.bx += lbx * xf.sx;
    xf.by += lby * xf.sy;
    xf.sx *= sx;
    xf.sy *= sy;
    xf.a *= alpha;
}

void r_pop(void) {
    if (xf_n <= 0) return;
    xf_n--;
    if (xf_n < XF_DEPTH) xf = xf_stack[xf_n];
}

static inline int pv(V2 p, Col c, float u, float v) {
    SDL_Vertex *q = &vb[nv];
    q->position.x = (p.x * xf.sx + xf.bx) * g_scale;
    q->position.y = (p.y * xf.sy + xf.by) * g_scale;
    q->color.r = c.r; q->color.g = c.g; q->color.b = c.b; q->color.a = clampf(c.a * xf.a, 0, 1);
    q->tex_coord.x = u;
    q->tex_coord.y = v;
    return nv++;
}

/* polyline corners sharper than this (cos of half the turn) get a round outer join */
#define MITER_MIN 0.5f
#define JOIN_STEPS 6

static inline void quad(int a, int b, int c, int d) {
    ib[ni++] = a; ib[ni++] = b; ib[ni++] = c;
    ib[ni++] = a; ib[ni++] = c; ib[ni++] = d;
}

void render_begin(void) {
    xf_n = 0;
    xf = (Xform){1, 1, 0, 0, 1};
    SDL_SetRenderTarget(g_ren, scene);
    SDL_SetRenderDrawColorFloat(g_ren, 0, 0, 0, 1);
    SDL_RenderClear(g_ren);
    cur_tex = NULL;
}

static void blit(SDL_Texture *src, SDL_Texture *dst, SDL_BlendMode mode, float k, float ox, float oy, int dw, int dh) {
    SDL_SetTextureBlendMode(src, mode);
    SDL_SetTextureColorModFloat(src, k, k, k);
    SDL_FRect r = {ox, oy, (float)dw, (float)dh};
    (void)dst;
    SDL_RenderTexture(g_ren, src, NULL, &r);
    SDL_SetTextureColorModFloat(src, 1, 1, 1);
}

void render_end(bool bloom, float strength) {
    r_flush();
    if (bloom && bl[0]) {
        /* downsample + threshold */
        SDL_SetRenderTarget(g_ren, bl[0]);
        blit(scene, bl[0], SDL_BLENDMODE_NONE, 1.0f, 0, 0, blw[0], blh[0]);
        if (has_sub) {
            SDL_SetRenderDrawBlendMode(g_ren, bm_sub);
            SDL_SetRenderDrawColorFloat(g_ren, 0.10f, 0.10f, 0.10f, 1);
            SDL_RenderFillRect(g_ren, NULL);
            SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
        }
        SDL_Texture *prev = bl[0];
        for (int i = 1; i < NBLOOM; i++) {
            SDL_SetRenderTarget(g_ren, bl[i]);
            blit(prev, bl[i], SDL_BLENDMODE_NONE, 1.0f, 0, 0, blw[i], blh[i]);
            /* small tent blur into blt[i] */
            SDL_SetRenderTarget(g_ren, blt[i]);
            SDL_SetRenderDrawColorFloat(g_ren, 0, 0, 0, 1);
            SDL_RenderClear(g_ren);
            const float o = 1.0f;
            blit(bl[i], blt[i], SDL_BLENDMODE_ADD, 0.25f, -o, -o, blw[i], blh[i]);
            blit(bl[i], blt[i], SDL_BLENDMODE_ADD, 0.25f, o, -o, blw[i], blh[i]);
            blit(bl[i], blt[i], SDL_BLENDMODE_ADD, 0.25f, -o, o, blw[i], blh[i]);
            blit(bl[i], blt[i], SDL_BLENDMODE_ADD, 0.25f, o, o, blw[i], blh[i]);
            prev = blt[i];
        }
    }
    SDL_SetRenderTarget(g_ren, NULL);
    SDL_SetRenderDrawColorFloat(g_ren, 0, 0, 0, 1);
    SDL_RenderClear(g_ren);
    blit(scene, NULL, SDL_BLENDMODE_NONE, 1.0f, 0, 0, g_pix_w, g_pix_h);
    if (bloom && bl[0]) {
        static const float wts[NBLOOM] = {0.0f, 0.55f, 0.65f, 0.75f, 0.85f};
        for (int i = 1; i < NBLOOM; i++) {
            float k = clampf(wts[i] * strength, 0, 1);
            if (k <= 0.001f) continue;
            blit(blt[i], NULL, SDL_BLENDMODE_ADD, k, 0, 0, g_pix_w, g_pix_h);
            if (wts[i] * strength > 1.0f) blit(blt[i], NULL, SDL_BLENDMODE_ADD, wts[i] * strength - 1.0f, 0, 0, g_pix_w, g_pix_h);
        }
    }
    if (g_screenshot_path) {
        SDL_Surface *s = SDL_RenderReadPixels(g_ren, NULL);
        if (s) {
            size_t n = strlen(g_screenshot_path);
            bool png = n > 4 && !strcmp(g_screenshot_path + n - 4, ".png");
#if SDL_VERSION_ATLEAST(3, 4, 0)
            if (png) SDL_SavePNG(s, g_screenshot_path);
            else
#endif
                SDL_SaveBMP(s, g_screenshot_path);
            (void)png;
            SDL_DestroySurface(s);
        }
        g_screenshot_path = NULL;
    }
    SDL_RenderPresent(g_ren);
}

/* ---------------------------------------------------------------- camera */
V2 w2v(V2 w) {
    return v2((w.x - g_cam.pos.x) * g_cam.zoom + g_virt_w * 0.5f + g_cam.shake.x,
              (w.y - g_cam.pos.y) * g_cam.zoom + VIRT_H * 0.5f + g_cam.shake.y);
}
V2 v2w(V2 v) {
    return v2((v.x - g_virt_w * 0.5f - g_cam.shake.x) / g_cam.zoom + g_cam.pos.x,
              (v.y - VIRT_H * 0.5f - g_cam.shake.y) / g_cam.zoom + g_cam.pos.y);
}
bool rw_visible(V2 p, float r) {
    V2 v = w2v(p);
    float rr = r * g_cam.zoom;
    return v.x + rr > -20 && v.x - rr < g_virt_w + 20 && v.y + rr > -20 && v.y - rr < VIRT_H + 20;
}

/* ---------------------------------------------------------------- primitives */
void r_line(V2 a, V2 b, float w, Col c) {
    use_tex(tex_add, 8, 18);
    V2 d = v2sub(b, a);
    float L = v2len(d);
    if (L < 1e-4f) d = v2(1, 0);
    else d = v2scale(d, 1.0f / L);
    float h = w * 0.5f;
    V2 n = v2scale(v2perp(d), h), dh = v2scale(d, h);
    int i0 = pv(v2add(v2sub(a, dh), n), c, LU0, AV0);
    int i1 = pv(v2sub(v2sub(a, dh), n), c, LU0, AV1);
    int i2 = pv(v2add(a, n), c, LUM, AV0);
    int i3 = pv(v2sub(a, n), c, LUM, AV1);
    int i4 = pv(v2add(b, n), c, LUM, AV0);
    int i5 = pv(v2sub(b, n), c, LUM, AV1);
    int i6 = pv(v2add(v2add(b, dh), n), c, LU1, AV0);
    int i7 = pv(v2sub(v2add(b, dh), n), c, LU1, AV1);
    quad(i0, i2, i3, i1);
    quad(i2, i4, i5, i3);
    quad(i4, i6, i7, i5);
}

static void polyline_impl(const V2 *p, const Col *cols, Col base, int n, bool closed, float w) {
    if (n < 2) return;
    if (n == 2 && !closed) {
        r_line(p[0], p[1], w, cols ? cols[0] : base);
        return;
    }
    float h = w * 0.5f;
    int needv = n * (JOIN_STEPS + 3) + 4, needi = n * (JOIN_STEPS + 4) * 3 + 12;
    if (needv > MAXV || needi > MAXI) return;
    use_tex(tex_add, needv, needi);
    /* vertex pairs (+ side, - side) ending the segment into a point and starting the one out of it */
    int first_p = 0, first_m = 0, prev_p = 0, prev_m = 0;
    for (int i = 0; i < n; i++) {
        V2 cur = p[i];
        bool has_prev = closed || i > 0, has_next = closed || i < n - 1;
        V2 prev = p[(i - 1 + n) % n], next = p[(i + 1) % n];
        V2 d0 = has_prev ? v2norm(v2sub(cur, prev)) : v2norm(v2sub(next, cur));
        V2 d1 = has_next ? v2norm(v2sub(next, cur)) : d0;
        if (v2len2(d0) < 1e-8f) d0 = d1;
        if (v2len2(d1) < 1e-8f) d1 = d0;
        V2 n0 = v2perp(d0), n1 = v2perp(d1);
        V2 m = v2add(n0, n1);
        float ml = v2len(m);
        if (ml < 1e-3f) m = n0;
        else m = v2scale(m, 1.0f / ml);
        float dm = v2dot(m, n0);
        Col c = cols ? cols[i] : base;
        int in_p, in_m, out_p, out_m;
        if (dm >= MITER_MIN) {
            V2 off = v2scale(m, h / dm);
            in_p = out_p = pv(v2add(cur, off), c, LUM, AV0);
            in_m = out_m = pv(v2sub(cur, off), c, LUM, AV1);
        } else {
            /* sharp corner: exact miter on the inner side, round join on the outer one,
             * so both edges keep their full width and the corner stays symmetric */
            float s = v2cross(d0, d1) > 0 ? 1.0f : -1.0f;
            float reach = minf(h / maxf(dm, 0.2f), minf(v2dist(cur, prev), v2dist(cur, next)));
            float v_in = s > 0 ? AV0 : AV1, v_out = s > 0 ? AV1 : AV0;
            int inner = pv(v2add(cur, v2scale(m, s * reach)), c, LUM, v_in);
            int centre = pv(cur, c, LUM, AVM);
            float turn = atan2f(v2cross(d0, d1), v2dot(d0, d1));
            V2 r0 = v2scale(n0, -s * h);
            int o0 = pv(v2add(cur, r0), c, LUM, v_out), ok = o0;
            for (int k = 1; k <= JOIN_STEPS; k++) {
                int o = pv(v2add(cur, v2rot(r0, turn * k / JOIN_STEPS)), c, LUM, v_out);
                ib[ni++] = centre; ib[ni++] = ok; ib[ni++] = o;
                ok = o;
            }
            ib[ni++] = centre; ib[ni++] = inner; ib[ni++] = o0;
            ib[ni++] = centre; ib[ni++] = ok; ib[ni++] = inner;
            if (s > 0) { in_p = out_p = inner; in_m = o0; out_m = ok; }
            else { in_m = out_m = inner; in_p = o0; out_p = ok; }
        }
        if (i == 0) {
            first_p = in_p;
            first_m = in_m;
        } else {
            quad(prev_p, in_p, in_m, prev_m);
        }
        prev_p = out_p;
        prev_m = out_m;
    }
    if (closed) quad(prev_p, first_p, first_m, prev_m);
    if (!closed) {
        V2 d = v2norm(v2sub(p[1], p[0]));
        V2 nn = v2scale(v2perp(d), h), dh = v2scale(d, h);
        Col c0 = cols ? cols[0] : base;
        int e0 = pv(v2add(v2sub(p[0], dh), nn), c0, LU0, AV0);
        int e1 = pv(v2sub(v2sub(p[0], dh), nn), c0, LU0, AV1);
        quad(e0, first_p, first_m, e1);
        d = v2norm(v2sub(p[n - 1], p[n - 2]));
        nn = v2scale(v2perp(d), h); dh = v2scale(d, h);
        Col c1 = cols ? cols[n - 1] : base;
        int f0 = pv(v2add(v2add(p[n - 1], dh), nn), c1, LU1, AV0);
        int f1 = pv(v2sub(v2add(p[n - 1], dh), nn), c1, LU1, AV1);
        quad(prev_p, f0, f1, prev_m);
    }
}

void r_polyline(const V2 *p, int n, bool closed, float w, Col c) { polyline_impl(p, NULL, c, n, closed, w); }
void r_polyline_cols(const V2 *p, const Col *cols, int n, bool closed, float w) {
    polyline_impl(p, cols, rgba(1, 1, 1, 1), n, closed, w);
}

void r_glow(V2 p, float radius, Col c) {
    use_tex(tex_add, 4, 6);
    int a = pv(v2(p.x - radius, p.y - radius), c, GU0, AV0);
    int b = pv(v2(p.x + radius, p.y - radius), c, GU1, AV0);
    int d = pv(v2(p.x + radius, p.y + radius), c, GU1, AV1);
    int e = pv(v2(p.x - radius, p.y + radius), c, GU0, AV1);
    quad(a, b, d, e);
}

void r_circle(V2 p, float radius, float w, Col c, int segs) {
    V2 pts[96];
    if (segs > 96) segs = 96;
    if (segs < 3) segs = 3;
    for (int i = 0; i < segs; i++) {
        float a = TAU * i / segs;
        pts[i] = v2(p.x + cosf(a) * radius, p.y + sinf(a) * radius);
    }
    r_polyline(pts, segs, true, w, c);
}

void r_arc(V2 p, float radius, float a0, float a1, float w, Col c, int segs) {
    V2 pts[97];
    if (segs > 96) segs = 96;
    if (segs < 1) segs = 1;
    for (int i = 0; i <= segs; i++) {
        float a = a0 + (a1 - a0) * i / segs;
        pts[i] = v2(p.x + cosf(a) * radius, p.y + sinf(a) * radius);
    }
    r_polyline(pts, segs + 1, false, w, c);
}

void r_fill_quad(V2 a, V2 b, V2 c, V2 d, Col col) {
    use_tex(tex_blend, 4, 6);
    int i0 = pv(a, col, SU, SV), i1 = pv(b, col, SU, SV), i2 = pv(c, col, SU, SV), i3 = pv(d, col, SU, SV);
    quad(i0, i1, i2, i3);
}
void r_fill_rect(float x, float y, float w, float h, Col c) {
    r_fill_quad(v2(x, y), v2(x + w, y), v2(x + w, y + h), v2(x, y + h), c);
}
void r_fill_tri(V2 a, V2 b, V2 c, Col col) {
    use_tex(tex_blend, 3, 3);
    ib[ni++] = pv(a, col, SU, SV);
    ib[ni++] = pv(b, col, SU, SV);
    ib[ni++] = pv(c, col, SU, SV);
}
/* solid triangles with a colour per vertex, blended smoothly across each one */
void r_mesh(const V2 *pts, const Col *cols, int npts, const int *idx, int nidx) {
    if (npts > MAXV || nidx > MAXI) return;
    use_tex(tex_blend, npts, nidx);
    int base = nv;
    for (int i = 0; i < npts; i++) pv(pts[i], cols[i], SU, SV);
    for (int i = 0; i < nidx; i++) ib[ni++] = base + idx[i];
}
void r_add_quad(V2 a, V2 b, V2 c, V2 d, Col col) {
    use_tex(tex_add, 4, 6);
    int i0 = pv(a, col, SU, SV), i1 = pv(b, col, SU, SV), i2 = pv(c, col, SU, SV), i3 = pv(d, col, SU, SV);
    quad(i0, i1, i2, i3);
}
void r_add_rect(float x, float y, float w, float h, Col c) {
    r_add_quad(v2(x, y), v2(x + w, y), v2(x + w, y + h), v2(x, y + h), c);
}
void r_add_hband(float x, float y, float w, float h, Col c) {
    use_tex(tex_add, 6, 12);
    Col z = col_a(c, 0);
    int a0 = pv(v2(x, y), z, SU, SV), a1 = pv(v2(x, y + h), z, SU, SV);
    int b0 = pv(v2(x + w * 0.5f, y), c, SU, SV), b1 = pv(v2(x + w * 0.5f, y + h), c, SU, SV);
    int c0 = pv(v2(x + w, y), z, SU, SV), c1 = pv(v2(x + w, y + h), z, SU, SV);
    quad(a0, b0, b1, a1);
    quad(b0, c0, c1, b1);
}
void r_rock_quad(V2 a, V2 b, V2 c, V2 d, V2 ua, V2 ub, V2 uc, V2 ud, Col col) {
    use_tex(tex_rock, 4, 6);
    int i0 = pv(a, col, ua.x, ua.y), i1 = pv(b, col, ub.x, ub.y), i2 = pv(c, col, uc.x, uc.y), i3 = pv(d, col, ud.x, ud.y);
    quad(i0, i1, i2, i3);
}
void r_rock_tri(V2 a, V2 b, V2 c, V2 ua, V2 ub, V2 uc, Col col) {
    use_tex(tex_rock, 3, 3);
    ib[ni++] = pv(a, col, ua.x, ua.y);
    ib[ni++] = pv(b, col, ub.x, ub.y);
    ib[ni++] = pv(c, col, uc.x, uc.y);
}

void r_frame(float x, float y, float w, float h, float lw, Col c) {
    V2 p[4] = {v2(x, y), v2(x + w, y), v2(x + w, y + h), v2(x, y + h)};
    r_polyline(p, 4, true, lw, c);
}

void r_panel(float x, float y, float w, float h, Col edge, float alpha) {
    r_fill_rect(x, y, w, h, rgba(0.01f, 0.015f, 0.04f, alpha));
    float c = 10;
    V2 p[8] = {v2(x + c, y), v2(x + w - c, y), v2(x + w, y + c), v2(x + w, y + h - c),
               v2(x + w - c, y + h), v2(x + c, y + h), v2(x, y + h - c), v2(x, y + c)};
    r_polyline(p, 8, true, 5, col_a(edge, 0.8f));
}

/* world-space wrappers */
void rw_line(V2 a, V2 b, float w, Col c) { r_line(w2v(a), w2v(b), w * g_cam.zoom, c); }
void rw_polyline(const V2 *p, int n, bool closed, float w, Col c) {
    V2 tmp[128];
    if (n > 128) n = 128;
    for (int i = 0; i < n; i++) tmp[i] = w2v(p[i]);
    r_polyline(tmp, n, closed, w * g_cam.zoom, c);
}
void rw_glow(V2 p, float radius, Col c) { r_glow(w2v(p), radius * g_cam.zoom, c); }
void rw_circle(V2 p, float radius, float w, Col c, int segs) {
    r_circle(w2v(p), radius * g_cam.zoom, w * g_cam.zoom, c, segs);
}
void r_shape(const Shape *s, V2 pos, float ang, float scale, float w, Col c) {
    float cs = cosf(ang) * scale, sn = sinf(ang) * scale;
    for (int i = 0; i < s->ns; i++) {
        const Stroke *st = &s->s[i];
        V2 tmp[18];
        for (int k = 0; k < st->n; k++) {
            V2 q = st->p[k];
            tmp[k] = v2(pos.x + q.x * cs - q.y * sn, pos.y + q.x * sn + q.y * cs);
        }
        r_polyline(tmp, st->n, st->closed, w, c);
    }
}
void rw_shape(const Shape *s, V2 pos, float ang, float scale, float w, Col c) {
    r_shape(s, w2v(pos), ang, scale * g_cam.zoom, w * g_cam.zoom, c);
}

/* ---------------------------------------------------------------- vector font */
/* glyphs on a 4x6 grid, "xy xy|xy xy" = polylines */
static const char *GLYPHS[96] = {
    /* 32 ' ' */ "",
    /* !  */ "20 24|26",
    /* "  */ "10 11|30 31",
    /* #  */ "10 16|30 36|02 42|04 44",
    /* $  */ "41 30 10 01 02 13 33 44 45 36 16 05|2- 27",
    /* %  */ "06 40|00 10 11 01 00|35 45 46 36 35",
    /* &  */ "46 12 11 20 31 32 04 05 16 26 44",
    /* '  */ "20 21",
    /* (  */ "30 21 25 36",
    /* )  */ "10 21 25 16",
    /* *  */ "11 35|31 15|03 43",
    /* +  */ "03 43|21 25",
    /* ,  */ "26 17",
    /* -  */ "13 33",
    /* .  */ "26",
    /* /  */ "06 40",
    /* 0  */ "10 30 41 45 36 16 05 01 10|41 05",
    /* 1  */ "11 20 26|16 36",
    /* 2  */ "01 10 30 41 42 06 46",
    /* 3  */ "01 10 30 41 42 33 13|33 44 45 36 16 05",
    /* 4  */ "36 30 04 44",
    /* 5  */ "40 00 03 33 44 45 36 16 05",
    /* 6  */ "30 10 01 05 16 36 45 44 33 03",
    /* 7  */ "00 40 42 26",
    /* 8  */ "10 30 41 42 33 13 02 01 10|13 04 05 16 36 45 44 33",
    /* 9  */ "43 13 02 01 10 30 41 45 36 06",
    /* :  */ "22|25",
    /* ;  */ "22|25 17",
    /* <  */ "31 03 35",
    /* =  */ "02 42|04 44",
    /* >  */ "11 43 15",
    /* ?  */ "01 10 30 41 42 23 24|26",
    /* @  */ "33 22 23 34 43 41 30 10 01 05 16 46",
    /* A  */ "06 01 10 30 41 46|03 43",
    /* B  */ "06 00 30 41 42 33 03|33 44 45 36 06",
    /* C  */ "41 30 10 01 05 16 36 45",
    /* D  */ "00 30 41 45 36 06 00",
    /* E  */ "40 00 06 46|03 33",
    /* F  */ "40 00 06|03 33",
    /* G  */ "41 30 10 01 05 16 36 45 43 23",
    /* H  */ "00 06|40 46|03 43",
    /* I  */ "10 30|20 26|16 36",
    /* J  */ "40 45 36 16 05",
    /* K  */ "00 06|40 03 46",
    /* L  */ "00 06 46",
    /* M  */ "06 00 23 40 46",
    /* N  */ "06 00 46 40",
    /* O  */ "10 30 41 45 36 16 05 01 10",
    /* P  */ "06 00 30 41 42 33 03",
    /* Q  */ "10 30 41 45 36 16 05 01 10|24 46",
    /* R  */ "06 00 30 41 42 33 03|23 46",
    /* S  */ "41 30 10 01 02 13 33 44 45 36 16 05",
    /* T  */ "00 40|20 26",
    /* U  */ "00 05 16 36 45 40",
    /* V  */ "00 26 40",
    /* W  */ "00 06 23 46 40",
    /* X  */ "00 46|40 06",
    /* Y  */ "00 23 40|23 26",
    /* Z  */ "00 40 06 46",
    /* [  */ "30 10 16 36",
    /* \  */ "00 46",
    /* ]  */ "10 30 36 16",
    /* ^  */ "13 20 33",
    /* _  */ "06 46",
    /* `  */ "10 21",
};

typedef struct { int ns; int start[6]; int len[6]; V2 pts[40]; } Glyph;
static Glyph glyphs[96];
static bool font_ready = false;

static void font_init(void) {
    for (int g = 0; g < 96; g++) {
        Glyph *gl = &glyphs[g];
        memset(gl, 0, sizeof(*gl));
        const char *s = GLYPHS[g];
        if (!s) continue;
        int np = 0;
        gl->ns = 0;
        gl->start[0] = 0;
        bool in_stroke = false;
        for (const char *c = s; *c;) {
            if (*c == ' ') { c++; continue; }
            if (*c == '|') {
                if (in_stroke) { gl->len[gl->ns] = np - gl->start[gl->ns]; gl->ns++; in_stroke = false; }
                c++;
                continue;
            }
            if (c[0] && c[1]) {
                float x = (float)(c[0] - '0');
                float y = c[1] == '-' ? -1.0f : (float)(c[1] - '0');
                if (!in_stroke) {
                    if (gl->ns >= 6) break;
                    gl->start[gl->ns] = np;
                    in_stroke = true;
                }
                if (np < 40) gl->pts[np++] = v2(x, y);
                c += 2;
            } else break;
        }
        if (in_stroke && gl->ns < 6) { gl->len[gl->ns] = np - gl->start[gl->ns]; gl->ns++; }
    }
    font_ready = true;
}

#define GLYPH_ADV 5.6f

float text_width(const char *s, float size) {
    float u = size / 6.0f;
    int n = 0, best = 0;
    for (const char *c = s; *c; c++) {
        if (*c == '\n') { if (n > best) best = n; n = 0; continue; }
        n++;
    }
    if (n > best) best = n;
    if (best == 0) return 0;
    return (best * GLYPH_ADV - 1.6f) * u;
}

/* the first k (0..1) of a polyline's length, with a bright pen at the tip */
static void polyline_part(const V2 *p, int n, float k, float w, Col c, bool pen) {
    if (k <= 0 || n < 1) return;
    if (n == 1) {
        r_line(p[0], p[0], w, c);
        return;
    }
    if (k >= 1) {
        r_polyline(p, n, false, w, c);
        return;
    }
    float want = 0;
    for (int i = 1; i < n; i++) want += v2dist(p[i - 1], p[i]);
    want *= k;
    V2 tmp[40];
    int m = 0;
    tmp[m++] = p[0];
    for (int i = 1; i < n && m < 40; i++) {
        float l = v2dist(p[i - 1], p[i]);
        if (want <= l) {
            tmp[m++] = v2lerp(p[i - 1], p[i], l > 0 ? want / l : 1);
            break;
        }
        want -= l;
        tmp[m++] = p[i];
    }
    if (m >= 2) r_polyline(tmp, m, false, w, c);
    if (pen) r_glow(tmp[m - 1], w * 2.4f, col_a(col_white(c, 0.6f), 0.7f * c.a));
}

/* reveal < 0 draws the whole text; 0..1 draws the glyphs on stroke by stroke, left to right */
static void text_impl(const char *s, float x, float y, float size, Col c, int align, float wmul, float reveal) {
    if (!font_ready) font_init();
    float u = size / 6.0f;
    float lw = maxf(2.2f, size * 0.30f) * wmul;
    float cx = x;
    float lx = x;
    int nglyph = 0, gi = 0;
    if (reveal >= 0)
        for (const char *q = s; *q; q++) nglyph += *q != ' ' && *q != '\n';
    const float span = 0.35f;
    /* per-line alignment */
    const char *line = s;
    while (line && *line) {
        const char *end = strchr(line, '\n');
        int len = end ? (int)(end - line) : (int)strlen(line);
        float w = len > 0 ? (len * GLYPH_ADV - 1.6f) * u : 0;
        if (align == AL_CENTER) lx = x - w * 0.5f;
        else if (align == AL_RIGHT) lx = x - w;
        else lx = x;
        cx = lx;
        for (int i = 0; i < len; i++) {
            int ch = (unsigned char)line[i];
            if (ch >= 'a' && ch <= 'z') ch -= 32;
            if (ch < 32 || ch >= 128) ch = '?';
            const Glyph *g = &glyphs[ch - 32];
            float kg = 1;
            if (reveal >= 0 && ch != ' ') {
                float st = nglyph > 1 ? (float)gi / (nglyph - 1) * (1 - span) : 0;
                kg = clampf((reveal - st) / span, 0, 1);
                gi++;
            }
            for (int k = 0; k < g->ns && kg > 0; k++) {
                V2 tmp[40];
                int n = g->len[k];
                for (int j = 0; j < n; j++) {
                    V2 q = g->pts[g->start[k] + j];
                    tmp[j] = v2(cx + q.x * u, y + q.y * u);
                }
                if (kg < 1) polyline_part(tmp, n, kg, lw, c, wmul == 1.0f);
                else if (n == 1) r_line(tmp[0], tmp[0], lw, c);
                else r_polyline(tmp, n, false, lw, c);
            }
            cx += GLYPH_ADV * u;
        }
        y += size * 1.6f;
        line = end ? end + 1 : NULL;
    }
}

void r_text(const char *s, float x, float y, float size, Col c, int align) { text_impl(s, x, y, size, c, align, 1.0f, -1); }

void r_text_glow(const char *s, float x, float y, float size, Col c, int align) { r_text_glowk(s, x, y, size, c, align, 1); }

void r_text_glowk(const char *s, float x, float y, float size, Col c, int align, float glow) {
    if (glow > 0.01f) text_impl(s, x, y, size, col_a(c, 0.35f * glow), align, 2.6f, -1);
    text_impl(s, x, y, size, c, align, 1.0f, -1);
}

void r_text_reveal(const char *s, float x, float y, float size, Col c, int align, float k, bool glow) {
    if (glow) text_impl(s, x, y, size, col_a(c, 0.35f), align, 2.6f, clampf(k, 0, 1));
    text_impl(s, x, y, size, c, align, 1.0f, clampf(k, 0, 1));
}

int font_glyph(int ch, const V2 **pts, const int **start, const int **len) {
    if (!font_ready) font_init();
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < 32 || ch >= 128) ch = '?';
    const Glyph *g = &glyphs[ch - 32];
    *pts = g->pts;
    *start = g->start;
    *len = g->len;
    return g->ns;
}

void r_textf(float x, float y, float size, Col c, int align, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    r_text(buf, x, y, size, c, align);
}
