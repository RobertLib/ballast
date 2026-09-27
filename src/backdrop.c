/*
 * The backdrop: open space under the mine floor, seen through the grid.
 * The camera looks straight down, so everything lies deeper than the floor
 * and scrolls slower the further down it is, and a zoom (the afterburner,
 * the arena) moves the near layers more than the far ones. While the camera
 * moves its eye trails off overhead and the deep layers slide the other way. From the deepest
 * up: a faint nebula, the milky band, far galaxies, three depths of stars,
 * the zone's planets (the Earth over Tycho, Jupiter over Io, a ringed giant
 * over Ceres) and two depths of drifting asteroids. The stars and asteroids
 * hash from the cells of their layer, so nothing is stored and a mine always
 * looks the same. The light falls from the upper left.
 */
#include "common.h"

/* the camera's focal length: at zoom z it hangs BD_F / z above the floor */
#define BD_F 900.0f

enum { BL_NEBULA, BL_BAND, BL_GALAXY, BL_STARS, BL_ROCKS = BL_STARS + 3 };

static uint32_t bd_seed;
static int bd_zone;
static V2 band_p, band_dir;   /* the milky band across the sky */

static const V3 LIGHT3 = {-0.55f, -0.65f, 0.52f};
static V2 light2(void) { return v2norm(v2(LIGHT3.x, LIGHT3.y)); }

/* ------------------------------------------------------------ one depth */
static float dz, dr;               /* virtual pixels per unit down there, and its parallax against the floor */
static V2 bc;                      /* the point of that plane in the middle of the screen */
static float bx0, by0, bx1, by1;   /* the part of that plane on screen */

static void depth_set(float depth) {
    float zoom = maxf(g_cam.zoom, 0.05f);
    dz = BD_F / (BD_F / zoom + depth);
    dr = dz / zoom;
    /* an eye leaning off overhead sees the deep layers slide the other way, the deeper the further */
    bc = v2sub(g_cam.pos, v2scale(g_cam.lean, (zoom - dz) / dz));
    float hw = (g_virt_w * 0.5f + 30) / dz, hh = (VIRT_H * 0.5f + 30) / dz;
    bx0 = bc.x - hw; bx1 = bc.x + hw;
    by0 = bc.y - hh; by1 = bc.y + hh;
}

static V2 dv(V2 q) {
    return v2((q.x - bc.x) * dz + g_virt_w * 0.5f + g_cam.shake.x * dr,
              (q.y - bc.y) * dz + VIRT_H * 0.5f + g_cam.shake.y * dr);
}

static bool on_screen(V2 v, float r) { return v.x + r > 0 && v.x - r < g_virt_w && v.y + r > 0 && v.y - r < VIRT_H; }

static Rng cell_rng(int layer, int cx, int cy) {
    Rng r = {hash32(bd_seed ^ hash32((uint32_t)cx * 0x8DA6B343u ^ (uint32_t)cy * 0xD8163841u ^ (uint32_t)layer * 0xCB1AB31Fu)) | 1u};
    return r;
}

/* the cells of `size` touching the screen, with room for content reaching `margin` out of its cell */
static void cells(float size, float margin, int *cx0, int *cy0, int *cx1, int *cy1) {
    *cx0 = (int)floorf((bx0 - margin) / size);
    *cx1 = (int)floorf((bx1 + margin) / size);
    *cy0 = (int)floorf((by0 - margin) / size);
    *cy1 = (int)floorf((by1 + margin) / size);
}

/* 1 on the milky band, falling off to 0 a couple of hundred pixels away */
static float band_at(V2 q) {
    float d = v2cross(band_dir, v2sub(q, band_p)) * dz;
    return expf(-d * d / (170.0f * 170.0f));
}

static float vnoise(float x, float y, uint32_t seed) {
    float fx = floorf(x), fy = floorf(y);
    int ix = (int)fx, iy = (int)fy;
    float tx = smooth01(x - fx), ty = smooth01(y - fy);
    float h[4];
    for (int k = 0; k < 4; k++)
        h[k] = (hash32(seed ^ hash32((uint32_t)(ix + (k & 1)) * 0x8DA6B343u ^ (uint32_t)(iy + (k >> 1)) * 0xD8163841u)) >> 8) * (1.0f / 16777216.0f);
    return lerpf(lerpf(h[0], h[1], tx), lerpf(h[2], h[3], tx), ty);
}

/* ------------------------------------------------------------ planets */
enum { PK_BANDED, PK_OCEAN, PK_ROCKY };
typedef struct {
    int kind;
    V2 at;               /* overhead when the camera is over this point of the mine */
    float depth, r;
    float tilt;          /* the axis leans this far on screen */
    float pitch;         /* and the north pole this far toward the camera */
    float spin, bright;
    float rings;         /* the outer ring's radius in planet radii, 0 none */
    float spot_lon, spot_lat, spot; /* banded: a storm */
    Col c0, c1, c2;      /* surface: the two band colours or sea and land, and the storm or the clouds */
    Col atmo;            /* the lit rim */
    uint32_t seed;
} Planet;
#define MAX_PLANETS 2
static Planet planets[MAX_PLANETS];
static int nplanets;

/* the surface colour at a longitude and a latitude (-1 the north pole .. 1 the south) */
static Col planet_surface(const Planet *pl, float lon, float v) {
    switch (pl->kind) {
    case PK_BANDED: {
        float w = sinf(v * 10.0f + 1.8f * sinf(v * 4.0f + (pl->seed & 7)) + 0.9f * vnoise(lon * 2.5f, v * 16, pl->seed));
        Col c = col_lerp(pl->c0, pl->c1, 0.5f + 0.5f * w);
        float du = (lon - pl->spot_lon) / 0.28f, dvv = (v - pl->spot_lat) / 0.08f;
        return col_lerp(c, pl->c2, pl->spot * expf(-(du * du + dvv * dvv)));
    }
    case PK_OCEAN: {
        float land = 0.65f * vnoise(lon * 2.2f + 5, v * 2.4f + 5, pl->seed) + 0.35f * vnoise(lon * 5, v * 5, pl->seed + 1);
        Col c = col_lerp(pl->c0, pl->c1, smooth01((land - 0.5f) / 0.08f));
        float cloud = 0.6f * vnoise(lon * 3 + v * 2, v * 7, pl->seed + 2) + 0.4f * vnoise(lon * 8, v * 11, pl->seed + 3);
        if (fabsf(v) > 0.84f) cloud = 1;
        return col_lerp(c, pl->c2, 0.9f * smooth01((cloud - 0.52f) / 0.14f));
    }
    default: {
        float n = 0.6f * vnoise(lon * 2.5f, v * 2.5f, pl->seed) + 0.4f * vnoise(lon * 7, v * 7, pl->seed + 1);
        return col_lerp(pl->c0, pl->c1, n);
    }
    }
}

/* a planet on screen: the body frame has y to the south pole, the view frame z toward the camera */
typedef struct { V2 c; float R, cs, sn, cp, sp; V3 L; } PFrame;
static V3 pf_view(const PFrame *f, V3 b) { return v3(b.x, b.y * f->cp + b.z * f->sp, -b.y * f->sp + b.z * f->cp); }
static V3 pf_body(const PFrame *f, V3 v) { return v3(v.x, v.y * f->cp - v.z * f->sp, v.y * f->sp + v.z * f->cp); }
static V2 pf_screen(const PFrame *f, V3 v) { return v2(f->c.x + (v.x * f->cs - v.y * f->sn) * f->R, f->c.y + (v.x * f->sn + v.y * f->cs) * f->R); }
static V3 sphere_at(float lon, float lat) {
    float w = sqrtf(maxf(0, 1 - lat * lat));
    return v3(w * sinf(lon), lat, w * cosf(lon));
}

/* a closed curve on the surface in body coordinates: drawn where it faces the camera, bright where the light falls */
static void surface_curve(const PFrame *f, const V3 *body, int n, float w, Col col) {
    V3 v[64];
    int s = -1;
    for (int k = 0; k < n; k++) {
        v[k] = pf_view(f, body[k]);
        if (v[k].z <= 0 && s < 0) s = k;
    }
    V2 p[64];
    Col cc[64];
    int m = 0;
    for (int i = 1; i <= n; i++) {
        int k = s < 0 ? i % n : (s + i) % n;
        if (v[k].z > 0) {
            float lit = maxf(0, v3dot(v[k], f->L));
            p[m] = pf_screen(f, v[k]);
            cc[m] = col_a(col, (0.04f + 0.96f * powf(lit, 0.8f)) * smooth01(v[k].z * 5));
            m++;
        }
        if (v[k].z <= 0 || i == n) {
            if (m >= 2) r_polyline_cols(p, cc, m, s < 0 && i == n, w);
            m = 0;
        }
    }
}

/* the rings lie in the equator; the half behind the disc is drawn before it */
static void draw_rings(const PFrame *f, const Planet *pl, bool far_half) {
    Col rc = col_white(col_lerp(pl->c0, pl->c1, 0.4f), 0.2f);
    for (int k = 0; k < 4; k++) {
        float a = lerpf(1.35f, pl->rings, k / 3.0f);
        V2 p[33];
        for (int j = 0; j <= 32; j++) {
            float th = (far_half ? PI : 0) + PI * j / 32;
            p[j] = pf_screen(f, pf_view(f, v3(cosf(th) * a, 0, sinf(th) * a)));
        }
        r_polyline(p, 33, false, maxf(1.5f, f->R * 0.04f), col_a(rc, (far_half ? 0.06f : 0.09f) + 0.03f * (k & 1)));
    }
}

#define PL_N 22

static void draw_planet(const Planet *pl, float t) {
    depth_set(pl->depth);
    PFrame f;
    f.c = dv(pl->at);
    f.R = pl->r * dz;
    if (!on_screen(f.c, f.R * maxf(1.2f, pl->rings + 0.2f))) return;
    f.cs = cosf(pl->tilt); f.sn = sinf(pl->tilt);
    f.cp = cosf(pl->pitch); f.sp = sinf(pl->pitch);
    V3 L = v3norm(LIGHT3);
    f.L = v3(L.x * f.cs + L.y * f.sn, -L.x * f.sn + L.y * f.cs, L.z);
    if (pl->rings > 0) draw_rings(&f, pl, true);
    /* the disc, row by row across the screen, lit and coloured per vertex; it stays dark
     * enough to keep under the bloom threshold, the glowing lines carry the detail */
    static V2 pts[(PL_N + 1) * (PL_N + 1)];
    static Col cols[(PL_N + 1) * (PL_N + 1)];
    static int idx[PL_N * PL_N * 6];
    int n = 0;
    for (int i = 0; i <= PL_N; i++) {
        float y = -1.0f + 2.0f * i / PL_N;
        float w = sqrtf(maxf(0, 1 - y * y));
        for (int j = 0; j <= PL_N; j++) {
            float x = (-1.0f + 2.0f * j / PL_N) * w;
            V3 v = v3(x, y, sqrtf(maxf(0, 1 - x * x - y * y)));
            V3 b = pf_body(&f, v);
            float shade = (0.05f + 0.95f * maxf(0, v3dot(v, f.L))) * pl->bright;
            Col s = planet_surface(pl, atan2f(b.x, b.z) + t * pl->spin, b.y);
            cols[n] = rgba(s.r * shade, s.g * shade, s.b * shade, 1);
            pts[n] = pf_screen(&f, v);
            n++;
        }
    }
    int m = 0;
    for (int i = 0; i < PL_N; i++)
        for (int j = 0; j < PL_N; j++) {
            int a = i * (PL_N + 1) + j, b = a + 1, d = a + PL_N + 1, e = d + 1;
            idx[m++] = a; idx[m++] = b; idx[m++] = e;
            idx[m++] = a; idx[m++] = e; idx[m++] = d;
        }
    r_mesh(pts, cols, n, idx, m);
    if (pl->kind == PK_BANDED) {
        /* the edges of the cloud bands, and the storm */
        Col bc = col_white(col_lerp(pl->c0, pl->c1, 0.3f), 0.15f);
        V3 ring[48];
        Rng r = {pl->seed | 1u};
        for (int k = 0; k < 7; k++) {
            float lat = -0.78f + 1.56f * (k + 0.25f + 0.5f * rng_f(&r)) / 7;
            for (int j = 0; j < 48; j++) ring[j] = sphere_at(TAU * j / 48, lat);
            surface_curve(&f, ring, 48, maxf(1.2f, f.R * 0.012f), col_a(bc, 0.1f + 0.05f * (k & 1)));
        }
        if (pl->spot > 0) {
            for (int j = 0; j < 24; j++) {
                float a = TAU * j / 24;
                ring[j] = sphere_at(pl->spot_lon + t * pl->spin + 0.26f * cosf(a), pl->spot_lat + 0.075f * sinf(a));
            }
            surface_curve(&f, ring, 24, maxf(1.2f, f.R * 0.016f), col_a(col_white(pl->c2, 0.2f), 0.25f));
        }
    }
    if (pl->rings > 0) draw_rings(&f, pl, false);
    /* the atmosphere glows along the lit rim */
    V2 rim[48];
    Col rc[48], hc[48];
    V2 l2 = light2();
    for (int k = 0; k < 48; k++) {
        V2 d = v2fromang(TAU * k / 48);
        float lit = maxf(0, v2dot(d, l2));
        rim[k] = v2mad(f.c, d, f.R);
        rc[k] = col_a(pl->atmo, 0.04f + 0.22f * lit);
        hc[k] = col_a(pl->atmo, 0.05f * lit);
    }
    r_polyline_cols(rim, hc, 48, true, maxf(4, f.R * 0.16f));
    r_polyline_cols(rim, rc, 48, true, maxf(1.6f, f.R * 0.025f));
}

/* ------------------------------------------------------------ nebula */
static void draw_nebula(Col wall, Col grid, Col accent, float t) {
    depth_set(19400);
    const float S = 6000;
    int cx0, cy0, cx1, cy1;
    cells(S, 8000, &cx0, &cy0, &cx1, &cy1);
    for (int cy = cy0; cy <= cy1; cy++)
        for (int cx = cx0; cx <= cx1; cx++) {
            Rng r = cell_rng(BL_NEBULA, cx, cy);
            float fx = rng_f(&r), fy = rng_f(&r);
            V2 q = v2((cx + fx) * S, (cy + fy) * S);
            if (rng_f(&r) > 0.12f + 0.8f * band_at(q)) continue;
            float rad = 3800 + 4200 * rng_f(&r);
            float mix = rng_f(&r), a = 0.045f + 0.06f * rng_f(&r);
            float rate = 0.1f + 0.15f * rng_f(&r), ph = rng_f(&r) * TAU;
            Col c = mix < 0.7f ? col_lerp(grid, wall, mix * 0.6f) : col_lerp(grid, accent, 0.5f);
            c.a = a * (0.75f + 0.25f * sinf(t * rate + ph));
            r_glow(dv(q), rad * dz, c);
        }
}

/* ------------------------------------------------------------ stars */
/* star colours by temperature, the common ones first, and how often each turns up */
static const Col STAR_COLS[5] = {{1, 1, 1, 1}, {0.74f, 0.85f, 1, 1}, {1, 0.94f, 0.8f, 1}, {1, 0.8f, 0.58f, 1}, {1, 0.62f, 0.52f, 1}};
static const float STAR_ODDS[5] = {0.42f, 0.7f, 0.86f, 0.96f, 1.0f};

static Col star_col(float x) {
    for (int k = 0; k < 4; k++)
        if (x < STAR_ODDS[k]) return STAR_COLS[k];
    return STAR_COLS[4];
}

/* the size of `px` virtual pixels at a depth, at the usual in-mine zoom */
#define BD_ZOOM 0.88f
static float depth_units(float px, float depth) { return px * (BD_F / BD_ZOOM + depth) / BD_F; }

/* pinpoints, mostly faint and a few bright (a power law), gathered in clumps and thick
 * along the milky band; the brightest near ones get a halo and short diffraction spikes */
static void draw_stars(int layer, float depth, float cell_px, float dens, float band_boost, float size, float bright, bool spikes, float t) {
    depth_set(depth);
    float cell = depth_units(cell_px, depth);
    int cx0, cy0, cx1, cy1;
    cells(cell, 0, &cx0, &cy0, &cx1, &cy1);
    for (int cy = cy0; cy <= cy1; cy++)
        for (int cx = cx0; cx <= cx1; cx++) {
            Rng r = cell_rng(layer, cx, cy);
            V2 mid = v2((cx + 0.5f) * cell, (cy + 0.5f) * cell);
            float clump = vnoise(mid.x / (cell * 7), mid.y / (cell * 7), bd_seed + (uint32_t)layer);
            float d = dens * (0.4f + 1.2f * clump * clump) * (1 + band_boost * band_at(mid));
            int n = (int)d + (rng_f(&r) < d - (int)d);
            if (n > 8) n = 8;
            for (int k = 0; k < n; k++) {
                float fx = rng_f(&r), fy = rng_f(&r);
                float m = rng_f(&r);
                m = m * m * m;
                /* the faint ones read as white, the colour shows in the bright ones */
                Col c = col_lerp(C_WHITE, star_col(rng_f(&r)), 0.3f + 0.7f * m);
                float tw = rng_f(&r), tw_ph = rng_f(&r) * TAU;
                V2 v = dv(v2((cx + fx) * cell, (cy + fy) * cell));
                if (!on_screen(v, 16)) continue;
                float a = (0.16f + 0.84f * m) * bright;
                if (tw < 0.3f) a *= 1 - 0.2f * (0.5f + 0.5f * sinf(t * (1.5f + 20 * tw) + tw_ph));
                float rc = (1.1f + 2.0f * m) * size;
                r_glow(v, rc, col_a(c, a));
                if (m > 0.35f) r_glow(v, rc * 3.8f, col_a(c, a * 0.16f));
                if (spikes && m > 0.8f) {
                    float L = (5 + 10 * m) * size;
                    Col sc = col_a(c, a * 0.28f);
                    r_line(v2(v.x - L, v.y), v2(v.x + L, v.y), 1.1f, sc);
                    r_line(v2(v.x, v.y - L), v2(v.x, v.y + L), 1.1f, sc);
                    r_line(v2(v.x - L * 0.45f, v.y), v2(v.x + L * 0.45f, v.y), 1.1f, sc);
                    r_line(v2(v.x, v.y - L * 0.45f), v2(v.x, v.y + L * 0.45f), 1.1f, sc);
                }
            }
        }
}

/* the milky band: a soft double strand of unresolved stars along the line the far stars crowd onto */
static void draw_milky_way(Col grid) {
    const float depth = 28000;
    depth_set(depth);
    V2 side = v2perp(band_dir);
    if (fabsf(v2dot(side, v2sub(bc, band_p))) * dz > g_virt_w + 300) return;
    float step = depth_units(95, depth);
    float s0 = v2dot(v2sub(bc, band_p), band_dir), half = depth_units(g_virt_w * 0.6f + VIRT_H * 0.6f + 300, depth);
    int k0 = (int)floorf((s0 - half) / step), k1 = (int)floorf((s0 + half) / step);
    Col col = col_lerp(rgba(0.8f, 0.85f, 1, 1), grid, 0.3f);
    for (int k = k0; k <= k1; k++) {
        Rng r = cell_rng(BL_BAND, k, 0);
        float along = (k + rng_f(&r)) * step;
        float wob = (vnoise(k * 0.13f, 0.5f, bd_seed + 7) - 0.5f) * 140;
        float strand = (rng_f(&r) < 0.5f ? -1.0f : 1.0f) * (18 + 45 * rng_f(&r));
        V2 q = v2add(v2mad(band_p, band_dir, along), v2scale(side, depth_units(wob + strand, depth)));
        float rad = depth_units(80 + 90 * rng_f(&r), depth);
        r_glow(dv(q), rad * dz, col_a(col, 0.025f + 0.03f * rng_f(&r)));
    }
}

/* far galaxies: faint tilted smudges with a brighter core */
static void draw_galaxies(float t) {
    const float depth = 30000;
    depth_set(depth);
    float cell = depth_units(560, depth);
    int cx0, cy0, cx1, cy1;
    cells(cell, 0, &cx0, &cy0, &cx1, &cy1);
    for (int cy = cy0; cy <= cy1; cy++)
        for (int cx = cx0; cx <= cx1; cx++) {
            Rng r = cell_rng(BL_GALAXY, cx, cy);
            if (rng_f(&r) > 0.16f) continue;
            float fx = rng_f(&r), fy = rng_f(&r);
            V2 v = dv(v2((cx + fx) * cell, (cy + fy) * cell));
            float len = 6 + 9 * rng_f(&r);
            if (!on_screen(v, len + 8)) continue;
            V2 ax = v2fromang(rng_f(&r) * PI);
            Col c = rng_f(&r) < 0.5f ? rgba(0.85f, 0.88f, 1, 1) : rgba(1, 0.92f, 0.8f, 1);
            float a = 0.5f + 0.5f * rng_f(&r);
            for (int j = -3; j <= 3; j++) {
                float f = 1 - fabsf((float)j) / 3.5f;
                r_glow(v2mad(v, ax, len * j / 6.0f), 2.2f + 3.2f * f, col_a(c, 0.07f * f * a));
            }
            r_glow(v, 1.5f, col_a(c, 0.5f * a));
        }
}

/* ------------------------------------------------------------ asteroids */
#define ROCK_MAXV 11

static void draw_asteroids(int layer, float depth, float cell, float prob, float rmin, float rmax, float dim, Col wall, Col rock, float t) {
    depth_set(depth);
    int cx0, cy0, cx1, cy1;
    cells(cell, rmax * 1.2f + 40, &cx0, &cy0, &cx1, &cy1);
    V2 light = light2();
    /* darker than the mine's own rock: they lie deeper in the shade, and stay under the bloom threshold */
    Col body = rgba((rock.r * 0.7f + 0.006f) * dim, (rock.g * 0.7f + 0.006f) * dim, (rock.b * 0.7f + 0.008f) * dim, 1);
    Col edge = col_lerp(wall, C_WHITE, 0.3f);
    typedef struct { V2 p[ROCK_MAXV]; int n; V2 c; float r, ang; uint32_t seed; } Outline;
    static Outline outl[256];
    int nout = 0;
    /* fills first and the rims after, so the batch changes texture once */
    for (int cy = cy0; cy <= cy1; cy++)
        for (int cx = cx0; cx <= cx1; cx++) {
            Rng r = cell_rng(layer, cx, cy);
            if (rng_f(&r) > prob || nout >= 256) continue;
            float fx = 0.2f + 0.6f * rng_f(&r), fy = 0.2f + 0.6f * rng_f(&r);
            float rad = lerpf(rmin, rmax, rng_f(&r) * rng_f(&r));
            int n = 7 + rng_int(&r, ROCK_MAXV - 7 + 1);
            float ang = rng_f(&r) * TAU + t * (rng_f(&r) - 0.5f) * 0.3f;
            float drift = rng_f(&r) * TAU, drate = 0.06f + 0.08f * rng_f(&r);
            V2 c = v2((cx + fx) * cell + rad * 0.5f * sinf(t * drate + drift), (cy + fy) * cell + rad * 0.5f * cosf(t * drate * 0.8f + drift));
            Outline *o = &outl[nout++];
            o->n = n;
            o->c = dv(c);
            o->r = rad * dz;
            o->ang = ang;
            o->seed = rng_u32(&r);
            for (int k = 0; k < n; k++) {
                float a = ang + TAU * (k + (rng_f(&r) - 0.5f) * 0.5f) / n;
                o->p[k] = dv(v2mad(c, v2fromang(a), rad * (0.7f + 0.3f * rng_f(&r))));
            }
            /* faceted: each wedge of the fan is lit by where its outer edge faces */
            for (int k = 0; k < n; k++) {
                V2 a = o->p[k], b = o->p[(k + 1) % n];
                float lit = maxf(0, v2dot(v2norm(v2perp(v2sub(a, b))), light));
                r_fill_tri(o->c, a, b, col_mul(body, 0.6f + 0.8f * lit));
            }
        }
    for (int i = 0; i < nout; i++) {
        Outline *o = &outl[i];
        Col cols[ROCK_MAXV];
        for (int k = 0; k < o->n; k++) {
            V2 prev = o->p[(k - 1 + o->n) % o->n], next = o->p[(k + 1) % o->n];
            float lit = maxf(0, v2dot(v2norm(v2perp(v2sub(prev, next))), light));
            cols[k] = col_a(edge, (0.06f + 0.3f * lit) * dim);
        }
        r_polyline_cols(o->p, cols, o->n, true, maxf(1.3f, o->r * 0.06f));
        /* a crater or two, turning with the rock; the wall facing the light is the dark one */
        if (o->r < 14) continue;
        Rng r = {o->seed | 1u};
        int nc = 1 + rng_int(&r, 2);
        for (int k = 0; k < nc; k++) {
            V2 at = v2mad(o->c, v2fromang(o->ang + rng_f(&r) * TAU), o->r * (0.15f + 0.3f * rng_f(&r)));
            float cr = o->r * (0.13f + 0.12f * rng_f(&r));
            V2 cp[10];
            Col cc[10];
            for (int j = 0; j < 10; j++) {
                V2 d = v2fromang(TAU * j / 10);
                cp[j] = v2mad(at, d, cr);
                cc[j] = col_a(edge, (0.03f + 0.16f * maxf(0, -v2dot(d, light))) * dim);
            }
            r_polyline_cols(cp, cc, 10, true, maxf(1.1f, o->r * 0.04f));
        }
    }
}

/* ------------------------------------------------------------ setup */
void backdrop_init(uint32_t seed, int zone, float world_w, float world_h) {
    bd_seed = hash32(seed ^ 0xBAC4D12Du);
    bd_zone = clampi(zone, 0, NUM_ZONES - 1);
    Rng r = {bd_seed | 1u};
    band_dir = v2fromang(rng_f(&r) * PI);
    band_p = v2(world_w * (0.3f + 0.4f * rng_f(&r)), world_h * (0.3f + 0.4f * rng_f(&r)));
    /* the zone's planets: a big one and a small one, in different parts of the mine */
    memset(planets, 0, sizeof(planets));
    nplanets = 2;
    Planet *big = &planets[0], *small = &planets[1];
    float bx = rng_f(&r), by = rng_f(&r);
    big->at = v2(world_w * (0.2f + 0.6f * bx), world_h * (0.2f + 0.6f * by));
    small->at = v2(world_w * (0.2f + 0.6f * (1 - bx)), world_h * (0.2f + 0.6f * rng_f(&r)));
    big->tilt = (rng_f(&r) - 0.5f) * 0.9f;
    small->tilt = (rng_f(&r) - 0.5f) * 0.6f;
    big->pitch = 0.22f + 0.2f * rng_f(&r);
    small->pitch = 0.3f;
    /* backdrop, not a landmark: dim enough to stay under the bloom threshold, a small disc a little brighter */
    big->bright = 0.09f;
    small->bright = 0.14f;
    big->seed = rng_u32(&r);
    small->seed = rng_u32(&r);
    float size = 0.85f + 0.3f * rng_f(&r);
    switch (bd_zone) {
    case 0: /* over the Moon: the Earth, and Mars far off */
        big->kind = PK_OCEAN;
        big->depth = 9000; big->r = 850 * size; big->spin = 0.012f;
        big->c0 = rgba(0.12f, 0.35f, 1, 1); big->c1 = rgba(0.3f, 0.7f, 0.3f, 1); big->c2 = rgba(1, 1, 1, 1);
        big->atmo = rgba(0.4f, 0.8f, 1, 1);
        small->kind = PK_ROCKY;
        small->depth = 7000; small->r = 170;
        small->c0 = rgba(0.95f, 0.45f, 0.25f, 1); small->c1 = rgba(0.6f, 0.28f, 0.18f, 1);
        small->atmo = rgba(1, 0.6f, 0.4f, 1);
        break;
    case 1: /* the Jupiter system: the giant and its icy Europa */
        big->kind = PK_BANDED;
        big->depth = 9500; big->r = 1150 * size;
        big->c0 = rgba(1, 0.82f, 0.6f, 1); big->c1 = rgba(0.72f, 0.45f, 0.3f, 1); big->c2 = rgba(1, 0.4f, 0.22f, 1);
        big->spot = 0.9f; big->spot_lon = -0.45f; big->spot_lat = 0.18f; big->spin = 0.006f;
        big->atmo = rgba(1, 0.8f, 0.55f, 1);
        small->kind = PK_ROCKY;
        small->depth = 7000; small->r = 150;
        small->c0 = rgba(0.95f, 0.92f, 0.85f, 1); small->c1 = rgba(0.7f, 0.58f, 0.45f, 1);
        small->atmo = rgba(0.9f, 0.9f, 1, 1);
        break;
    default: /* the belt: a ringed giant and an ice giant */
        big->kind = PK_BANDED;
        big->depth = 9000; big->r = 700 * size; big->rings = 2.25f;
        big->c0 = rgba(1, 0.9f, 0.65f, 1); big->c1 = rgba(0.82f, 0.7f, 0.5f, 1); big->c2 = big->c1;
        big->atmo = rgba(1, 0.9f, 0.7f, 1);
        small->kind = PK_ROCKY;
        small->depth = 7000; small->r = 150;
        small->c0 = rgba(0.45f, 0.75f, 1, 1); small->c1 = rgba(0.3f, 0.5f, 0.95f, 1);
        small->atmo = rgba(0.5f, 0.8f, 1, 1);
        break;
    }
}

void backdrop_draw(Col wall, Col grid, Col rock, Col accent, float t) {
    draw_nebula(wall, grid, accent, t);
    draw_milky_way(grid);
    draw_galaxies(t);
    /* the far stars are fine dust that crowds into the milky band, the near ones are few and bright */
    draw_stars(BL_STARS + 0, 28000, 34, 0.8f, 6.0f, 0.8f, 0.75f, false, t);
    draw_stars(BL_STARS + 1, 13600, 60, 0.9f, 1.5f, 1.0f, 0.9f, false, t);
    draw_stars(BL_STARS + 2, 7500, 110, 0.8f, 0.5f, 1.15f, 1.0f, true, t);
    /* the farther planet first */
    for (int i = 0; i < nplanets; i++) {
        int k = planets[0].depth >= planets[1].depth ? i : nplanets - 1 - i;
        draw_planet(&planets[k], t);
    }
    /* the belt around Ceres is thick with rocks */
    float belt = bd_zone == 2 ? 1.6f : 1.0f;
    draw_asteroids(BL_ROCKS + 0, 1820, 700, 0.2f * belt, 25, 65, 0.7f, wall, rock, t);
    draw_asteroids(BL_ROCKS + 1, 630, 600, 0.26f * belt, 45, 125, 1.0f, wall, rock, t);
}
