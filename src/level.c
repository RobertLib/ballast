/*
 * Level geometry: tile map -> wall segments, collision, raycasts, doors,
 * breadth-first flow fields for robot navigation and exit guidance.
 */
#include "common.h"

Level L;

bool tile_in(int tx, int ty) { return tx >= 0 && ty >= 0 && tx < L.w && ty < L.h; }

static inline uint8_t tile_at(int tx, int ty) {
    if (!tile_in(tx, ty)) return T_SOLID;
    return L.tile[ty][tx];
}

static inline bool is_rock(uint8_t t) { return t == T_SOLID || t == T_BREAK; }

/* side solidity: 0 top, 1 right, 2 bottom, 3 left */
static bool side_solid(uint8_t t, int side) {
    switch (t) {
    case T_SOLID:
    case T_BREAK: return true;
    case T_TRI_TL: return side == 0 || side == 3;
    case T_TRI_TR: return side == 0 || side == 1;
    case T_TRI_BL: return side == 2 || side == 3;
    case T_TRI_BR: return side == 2 || side == 1;
    default: return false;
    }
}

/* ------------------------------------------------------------ building */
static void add_seg(float ax, float ay, float bx, float by, V2 n, int tile) {
    if (L.nsegs >= MAX_SEGS) return;
    Seg *s = &L.segs[L.nsegs++];
    /* orient so that n == perp(b - a) direction (open side on the left of travel) */
    V2 a = v2(ax, ay), b = v2(bx, by);
    V2 d = v2sub(b, a);
    if (v2dot(v2perp(d), n) < 0) { V2 t = a; a = b; b = t; }
    s->a = a;
    s->b = b;
    s->n = v2norm(n);
    s->tile = tile;
}

static int seg_cmp_key(const Seg *s, int *dir, float *line, float *start) {
    V2 d = v2sub(s->b, s->a);
    float ax = fabsf(d.x), ay = fabsf(d.y);
    if (ay < 0.01f) { *dir = 0; *line = s->a.y; *start = minf(s->a.x, s->b.x); }
    else if (ax < 0.01f) { *dir = 1; *line = s->a.x; *start = minf(s->a.y, s->b.y); }
    else if ((d.x > 0) == (d.y > 0)) { *dir = 2; *line = s->a.y - s->a.x; *start = minf(s->a.x, s->b.x); }
    else { *dir = 3; *line = s->a.y + s->a.x; *start = minf(s->a.x, s->b.x); }
    return 0;
}

static int seg_sort(const void *pa, const void *pb) {
    const Seg *a = (const Seg *)pa, *b = (const Seg *)pb;
    int da, db;
    float la, lb, sa, sb;
    seg_cmp_key(a, &da, &la, &sa);
    seg_cmp_key(b, &db, &lb, &sb);
    int ta = a->tile >= 0, tb = b->tile >= 0;
    if (ta != tb) return ta - tb;
    if (da != db) return da - db;
    /* group by normal direction */
    int na = (int)roundf(a->n.x * 2) * 3 + (int)roundf(a->n.y * 2);
    int nb = (int)roundf(b->n.x * 2) * 3 + (int)roundf(b->n.y * 2);
    if (na != nb) return na - nb;
    if (fabsf(la - lb) > 0.01f) return la < lb ? -1 : 1;
    if (fabsf(sa - sb) > 0.01f) return sa < sb ? -1 : 1;
    return 0;
}

static void merge_segs(void) {
    qsort(L.segs, (size_t)L.nsegs, sizeof(Seg), seg_sort);
    int out = 0;
    for (int i = 0; i < L.nsegs; i++) {
        Seg s = L.segs[i];
        if (out > 0 && s.tile < 0) {
            Seg *p = &L.segs[out - 1];
            int dp, ds;
            float lp, ls, sp, ss;
            seg_cmp_key(p, &dp, &lp, &sp);
            seg_cmp_key(&s, &ds, &ls, &ss);
            if (p->tile < 0 && dp == ds && fabsf(lp - ls) < 0.01f && v2dot(p->n, s.n) > 0.99f) {
                /* contiguous? the end of p must equal the start of s (in direction of travel) */
                if (v2dist2(p->b, s.a) < 0.01f) { p->b = s.b; continue; }
                if (v2dist2(p->a, s.b) < 0.01f) { p->a = s.a; continue; }
            }
        }
        L.segs[out++] = s;
    }
    L.nsegs = out;
}

static void build_index(void) {
    int cells = L.w * L.h;
    memset(L.cell_count, 0, sizeof(int) * (size_t)cells);
    /* count */
    for (int pass = 0; pass < 2; pass++) {
        int total = 0;
        if (pass == 1) {
            for (int c = 0; c < cells; c++) {
                L.cell_start[c] = total;
                total += L.cell_count[c];
                L.cell_count[c] = 0;
            }
            free(L.cell_refs);
            L.cell_refs = (int *)malloc(sizeof(int) * (size_t)(total + 1));
            L.ncell_refs = total;
        }
        for (int i = 0; i < L.nsegs; i++) {
            Seg *s = &L.segs[i];
            int x0 = tx_of(minf(s->a.x, s->b.x) - 1), x1 = tx_of(maxf(s->a.x, s->b.x) + 1);
            int y0 = tx_of(minf(s->a.y, s->b.y) - 1), y1 = tx_of(maxf(s->a.y, s->b.y) + 1);
            bool diag = fabsf(s->a.x - s->b.x) > 1 && fabsf(s->a.y - s->b.y) > 1;
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++) {
                    if (!tile_in(x, y)) continue;
                    if (diag) {
                        /* only cells the diagonal actually touches */
                        V2 c = tile_center(x, y);
                        V2 ab = v2sub(s->b, s->a);
                        float t = clampf(v2dot(v2sub(c, s->a), ab) / v2len2(ab), 0, 1);
                        V2 q = v2mad(s->a, ab, t);
                        if (v2dist(q, c) > TILE * 0.75f) continue;
                    }
                    int ci = y * L.w + x;
                    if (pass == 1) L.cell_refs[L.cell_start[ci] + L.cell_count[ci]] = i;
                    L.cell_count[ci]++;
                }
        }
    }
}

static int corner_key(V2 p) {
    int x = (int)lroundf(p.x / TILE), y = (int)lroundf(p.y / TILE);
    return y * (MAP_MAX_W + 1) + x;
}

static void build_contours(void) {
    /* link segments end->start into polylines for smooth, joint-free rendering */
    static int start_head[(MAP_MAX_W + 1) * (MAP_MAX_H + 1)], end_head[(MAP_MAX_W + 1) * (MAP_MAX_H + 1)];
    static int start_next[MAX_SEGS], end_next[MAX_SEGS];
    static bool used[MAX_SEGS];
    L.ncpts = 0;
    L.ncontours = 0;
    L.nbreak_segs = 0;
    for (int i = 0; i < (MAP_MAX_W + 1) * (MAP_MAX_H + 1); i++) start_head[i] = end_head[i] = -1;
    memset(used, 0, sizeof(used));
    for (int i = L.nsegs - 1; i >= 0; i--) {
        if (L.segs[i].tile >= 0) {
            if (L.nbreak_segs < 512) L.break_segs[L.nbreak_segs++] = i;
            used[i] = true;
            continue;
        }
        int ka = corner_key(L.segs[i].a), kb = corner_key(L.segs[i].b);
        start_next[i] = start_head[ka]; start_head[ka] = i;
        end_next[i] = end_head[kb]; end_head[kb] = i;
    }
    for (int i = 0; i < L.nsegs; i++) {
        if (used[i]) continue;
        if (L.ncontours >= MAX_CONTOURS || L.ncpts + 2 >= MAX_CPTS) break;
        /* walk backwards to the beginning of an open chain (or around a loop) */
        int start = i;
        for (int guard = 0; guard < L.nsegs; guard++) {
            int prev = -1;
            for (int j = end_head[corner_key(L.segs[start].a)]; j >= 0; j = end_next[j])
                if (!used[j] && j != start) { prev = j; break; }
            if (prev < 0 || prev == i) break;
            start = prev;
        }
        int ci = L.ncontours++;
        L.cstart[ci] = L.ncpts;
        L.cpts[L.ncpts++] = L.segs[start].a;
        int cur = start;
        used[cur] = true;
        bool closed = false;
        for (;;) {
            V2 endp = L.segs[cur].b;
            int next = -1;
            for (int j = start_head[corner_key(endp)]; j >= 0; j = start_next[j])
                if (!used[j]) { next = j; break; }
            if (next < 0) {
                if (v2dist2(endp, L.segs[start].a) < 0.01f) closed = true;
                else if (L.ncpts < MAX_CPTS) L.cpts[L.ncpts++] = endp;
                break;
            }
            if (L.ncpts >= MAX_CPTS - 1) break;
            L.cpts[L.ncpts++] = endp;
            used[next] = true;
            cur = next;
        }
        L.clen[ci] = L.ncpts - L.cstart[ci];
        L.cclosed[ci] = closed;
    }
}

void level_rebuild_geometry(void) {
    L.nsegs = 0;
    for (int y = 0; y < L.h; y++) {
        for (int x = 0; x < L.w; x++) {
            uint8_t t = L.tile[y][x];
            if (t == T_EMPTY) continue;
            float x0 = x * TILE, y0 = y * TILE, x1 = x0 + TILE, y1 = y0 + TILE;
            int tid = t == T_BREAK ? y * L.w + x : -1;
            /* axis-aligned sides */
            static const int nx[4] = {0, 1, 0, -1}, ny[4] = {-1, 0, 1, 0};
            for (int s = 0; s < 4; s++) {
                if (!side_solid(t, s)) continue;
                uint8_t nb = tile_at(x + nx[s], y + ny[s]);
                if (!tile_in(x + nx[s], y + ny[s])) continue;
                if (side_solid(nb, (s + 2) % 4)) continue;
                V2 n = v2((float)nx[s], (float)ny[s]);
                switch (s) {
                case 0: add_seg(x0, y0, x1, y0, n, tid); break;
                case 1: add_seg(x1, y0, x1, y1, n, tid); break;
                case 2: add_seg(x0, y1, x1, y1, n, tid); break;
                case 3: add_seg(x0, y0, x0, y1, n, tid); break;
                }
            }
            /* hypotenuses */
            const float k = 0.70710678f;
            switch (t) {
            case T_TRI_TL: add_seg(x1, y0, x0, y1, v2(k, k), -1); break;
            case T_TRI_TR: add_seg(x0, y0, x1, y1, v2(-k, k), -1); break;
            case T_TRI_BL: add_seg(x0, y0, x1, y1, v2(k, -k), -1); break;
            case T_TRI_BR: add_seg(x1, y0, x0, y1, v2(-k, -k), -1); break;
            default: break;
            }
        }
    }
    merge_segs();
    build_index();
    build_contours();
}

static bool bevel_skip(int x, int y) {
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (L.door_id[y + dy][x + dx] >= 0) return true;
    return L.flags[y][x] != 0;
}

static void bevel(uint8_t (*src)[MAP_MAX_W]) {
    /* pass 1: convex rock corners (two perpendicular open sides) become 45 degree slopes */
    for (int y = 1; y < L.h - 1; y++)
        for (int x = 1; x < L.w - 1; x++) {
            if (src[y][x] != T_SOLID || bevel_skip(x, y)) continue;
            bool up = is_rock(src[y - 1][x]), dn = is_rock(src[y + 1][x]);
            bool lf = is_rock(src[y][x - 1]), rt = is_rock(src[y][x + 1]);
            bool ul = is_rock(src[y - 1][x - 1]), ur = is_rock(src[y - 1][x + 1]);
            bool dl = is_rock(src[y + 1][x - 1]), dr = is_rock(src[y + 1][x + 1]);
            if (!up && !rt && dn && lf && !ur) L.tile[y][x] = T_TRI_BL;
            else if (!up && !lf && dn && rt && !ul) L.tile[y][x] = T_TRI_BR;
            else if (!dn && !rt && up && lf && !dr) L.tile[y][x] = T_TRI_TL;
            else if (!dn && !lf && up && rt && !dl) L.tile[y][x] = T_TRI_TR;
        }
    /* pass 2: concave room corners, judged on the updated map so slopes never collide */
    static uint8_t g[MAP_MAX_H][MAP_MAX_W];
    memcpy(g, L.tile, sizeof(g));
    for (int y = 1; y < L.h - 1; y++)
        for (int x = 1; x < L.w - 1; x++) {
            if (g[y][x] != T_EMPTY || bevel_skip(x, y)) continue;
            bool up = is_rock(g[y - 1][x]), dn = is_rock(g[y + 1][x]);
            bool lf = is_rock(g[y][x - 1]), rt = is_rock(g[y][x + 1]);
            bool ul = is_rock(g[y - 1][x - 1]), ur = is_rock(g[y - 1][x + 1]);
            bool dl = is_rock(g[y + 1][x - 1]), dr = is_rock(g[y + 1][x + 1]);
            if (up && lf && !dn && !rt && ul && !dr && g[y + 1][x] == T_EMPTY && g[y][x + 1] == T_EMPTY) L.tile[y][x] = T_TRI_TL;
            else if (up && rt && !dn && !lf && ur && !dl && g[y + 1][x] == T_EMPTY && g[y][x - 1] == T_EMPTY) L.tile[y][x] = T_TRI_TR;
            else if (dn && lf && !up && !rt && dl && !ur && g[y - 1][x] == T_EMPTY && g[y][x + 1] == T_EMPTY) L.tile[y][x] = T_TRI_BL;
            else if (dn && rt && !up && !lf && dr && !ul && g[y - 1][x] == T_EMPTY && g[y][x - 1] == T_EMPTY) L.tile[y][x] = T_TRI_BR;
        }
}

void level_build_from_ascii(const char **rows, int nrows) {
    int w = 0;
    for (int i = 0; i < nrows; i++) {
        int l = (int)strlen(rows[i]);
        if (l > w) w = l;
    }
    if (w > MAP_MAX_W) w = MAP_MAX_W;
    if (nrows > MAP_MAX_H) nrows = MAP_MAX_H;
    free(L.cell_refs);
    memset(&L, 0, sizeof(L));
    L.w = w;
    L.h = nrows;
    memset(L.door_id, -1, sizeof(L.door_id));
    for (int y = 0; y < L.h; y++) {
        int len = (int)strlen(rows[y]);
        for (int x = 0; x < L.w; x++) {
            char c = x < len ? rows[y][x] : '#';
            uint8_t t = T_EMPTY;
            if (c == '#') t = T_SOLID;
            else if (c == 'w') { t = T_BREAK; L.break_hp[y][x] = 45; }
            else if (c == 'E') L.flags[y][x] |= TF_ENERGY;
            else if (c == 'Z') L.flags[y][x] |= TF_EXIT;
            if (x == 0 || y == 0 || x == L.w - 1 || y == L.h - 1) t = T_SOLID;
            L.tile[y][x] = t;
        }
    }
    /* doors: group contiguous identical door characters */
    for (int y = 0; y < L.h; y++) {
        int len = (int)strlen(rows[y]);
        for (int x = 0; x < L.w && x < len; x++) {
            char c = rows[y][x];
            int lock = -1;
            if (c == '-') lock = LOCK_NONE;
            else if (c == 'b') lock = LOCK_BLUE;
            else if (c == 'y') lock = LOCK_YELLOW;
            else if (c == 'r') lock = LOCK_RED;
            else if (c == 'x') lock = LOCK_EXIT;
            if (lock < 0 || L.door_id[y][x] >= 0 || L.ndoors >= MAX_DOORS) continue;
            Door *d = &L.doors[L.ndoors];
            memset(d, 0, sizeof(*d));
            d->lock = lock;
            d->x0 = d->x1 = x;
            d->y0 = d->y1 = y;
            bool horiz = (x + 1 < len && rows[y][x + 1] == c);
            if (horiz) {
                while (d->x1 + 1 < len && rows[y][d->x1 + 1] == c) d->x1++;
            } else {
                while (d->y1 + 1 < L.h && (int)strlen(rows[d->y1 + 1]) > x && rows[d->y1 + 1][x] == c) d->y1++;
                /* single tile: decide orientation from neighbours */
                if (d->y1 == d->y0) horiz = is_rock(tile_at(x, y - 1)) ? false : true;
                if (d->y1 == d->y0 && is_rock(tile_at(x - 1, y)) && is_rock(tile_at(x + 1, y))) horiz = true;
            }
            d->horiz = horiz;
            for (int yy = d->y0; yy <= d->y1; yy++)
                for (int xx = d->x0; xx <= d->x1; xx++) {
                    L.door_id[yy][xx] = (int8_t)L.ndoors;
                    L.flags[yy][xx] |= TF_DOOR;
                }
            L.ndoors++;
        }
    }
    /* auto-bevel corners for smoother, more organic tunnels */
    static uint8_t src[MAP_MAX_H][MAP_MAX_W];
    memcpy(src, L.tile, sizeof(src));
    /* never bevel tiles that carry entities */
    for (int y = 0; y < L.h; y++) {
        int len = (int)strlen(rows[y]);
        for (int x = 0; x < L.w && x < len; x++) {
            char c = rows[y][x];
            if (c != '.' && c != '#') L.flags[y][x] |= 0x80;
        }
    }
    bevel(src);
    for (int y = 0; y < L.h; y++)
        for (int x = 0; x < L.w; x++) L.flags[y][x] &= 0x7F;
    level_rebuild_geometry();
}

/* ------------------------------------------------------------ doors */
V2 door_center(const Door *d) {
    return v2((d->x0 + d->x1 + 1) * TILE * 0.5f, (d->y0 + d->y1 + 1) * TILE * 0.5f);
}

#define DOOR_THICK 14.0f
bool door_rects(const Door *d, SDL_FRect *r0, SDL_FRect *r1) {
    if (d->open >= 0.97f) return false;
    float o = d->open;
    if (d->horiz) {
        float xa = d->x0 * TILE, xb = (d->x1 + 1) * TILE;
        float half = (xb - xa) * 0.5f * (1 - o);
        float yc = (d->y0 + 0.5f) * TILE;
        *r0 = (SDL_FRect){xa, yc - DOOR_THICK * 0.5f, half, DOOR_THICK};
        *r1 = (SDL_FRect){xb - half, yc - DOOR_THICK * 0.5f, half, DOOR_THICK};
    } else {
        float ya = d->y0 * TILE, yb = (d->y1 + 1) * TILE;
        float half = (yb - ya) * 0.5f * (1 - o);
        float xc = (d->x0 + 0.5f) * TILE;
        *r0 = (SDL_FRect){xc - DOOR_THICK * 0.5f, ya, DOOR_THICK, half};
        *r1 = (SDL_FRect){xc - DOOR_THICK * 0.5f, yb - half, DOOR_THICK, half};
    }
    return true;
}

float door_dist(const Door *d, V2 p) {
    float xa = d->x0 * TILE, xb = (d->x1 + 1) * TILE, ya = d->y0 * TILE, yb = (d->y1 + 1) * TILE;
    float dx = maxf(maxf(xa - p.x, 0), p.x - xb);
    float dy = maxf(maxf(ya - p.y, 0), p.y - yb);
    return sqrtf(dx * dx + dy * dy);
}

/* ------------------------------------------------------------ queries */
bool tile_blocks(int tx, int ty) {
    uint8_t t = tile_at(tx, ty);
    if (t != T_EMPTY) return true;
    int di = L.door_id[ty][tx];
    if (di >= 0 && L.doors[di].open < 0.9f) return true;
    return false;
}

bool point_in_rock(V2 p) {
    int tx = tx_of(p.x), ty = tx_of(p.y);
    uint8_t t = tile_at(tx, ty);
    float fx = p.x / TILE - tx, fy = p.y / TILE - ty;
    switch (t) {
    case T_EMPTY: return false;
    case T_SOLID:
    case T_BREAK: return true;
    case T_TRI_TL: return fx + fy < 1;
    case T_TRI_TR: return fx > fy;
    case T_TRI_BL: return fy > fx;
    case T_TRI_BR: return fx + fy > 1;
    }
    return true;
}

static bool circle_vs_rect(V2 *pos, float r, V2 *vel, float bounce, const SDL_FRect *rc) {
    float cx = clampf(pos->x, rc->x, rc->x + rc->w), cy = clampf(pos->y, rc->y, rc->y + rc->h);
    V2 d = v2(pos->x - cx, pos->y - cy);
    float d2 = v2len2(d);
    if (d2 >= r * r) return false;
    V2 n;
    float pen;
    if (d2 < 1e-6f) {
        /* centre inside: push out along the shortest axis */
        float l = pos->x - rc->x, rr = rc->x + rc->w - pos->x, t = pos->y - rc->y, b = rc->y + rc->h - pos->y;
        float m = minf(minf(l, rr), minf(t, b));
        if (m == l) n = v2(-1, 0);
        else if (m == rr) n = v2(1, 0);
        else if (m == t) n = v2(0, -1);
        else n = v2(0, 1);
        pen = m + r;
    } else {
        float dl = sqrtf(d2);
        n = v2scale(d, 1.0f / dl);
        pen = r - dl;
    }
    *pos = v2mad(*pos, n, pen);
    if (vel) {
        float vn = v2dot(*vel, n);
        if (vn < 0) *vel = v2mad(*vel, n, -vn * (1 + bounce));
    }
    return true;
}

bool circle_collide(V2 *pos, float r, V2 *vel, float bounce) {
    bool hit = false;
    for (int iter = 0; iter < 3; iter++) {
        bool any = false;
        int x0 = tx_of(pos->x - r), x1 = tx_of(pos->x + r), y0 = tx_of(pos->y - r), y1 = tx_of(pos->y + r);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                if (!tile_in(x, y)) continue;
                int ci = y * L.w + x;
                for (int k = 0; k < L.cell_count[ci]; k++) {
                    const Seg *s = &L.segs[L.cell_refs[L.cell_start[ci] + k]];
                    V2 ab = v2sub(s->b, s->a);
                    float t = clampf(v2dot(v2sub(*pos, s->a), ab) / v2len2(ab), 0, 1);
                    V2 q = v2mad(s->a, ab, t);
                    V2 d = v2sub(*pos, q);
                    float d2 = v2len2(d);
                    if (d2 >= r * r) continue;
                    float dl = sqrtf(d2);
                    V2 n = dl > 1e-4f ? v2scale(d, 1.0f / dl) : s->n;
                    /* centre behind the wall face (inside rock)? push out along the wall normal.
                       Only for the segment interior: near endpoints the corner vector is correct. */
                    if (t > 0.001f && t < 0.999f && v2dot(v2sub(*pos, s->a), s->n) < 0) { n = s->n; dl = -dl; }
                    *pos = v2mad(*pos, n, r - dl);
                    if (vel) {
                        float vn = v2dot(*vel, n);
                        if (vn < 0) *vel = v2mad(*vel, n, -vn * (1 + bounce));
                    }
                    any = hit = true;
                }
            }
        for (int i = 0; i < L.ndoors; i++) {
            const Door *d = &L.doors[i];
            if (door_dist(d, *pos) > r + 4) continue;
            SDL_FRect r0, r1;
            if (!door_rects(d, &r0, &r1)) continue;
            if (r0.w > 0.5f && r0.h > 0.5f && circle_vs_rect(pos, r, vel, bounce, &r0)) any = hit = true;
            if (r1.w > 0.5f && r1.h > 0.5f && circle_vs_rect(pos, r, vel, bounce, &r1)) any = hit = true;
        }
        if (!any) break;
    }
    return hit;
}

static bool ray_rect(V2 a, V2 d, const SDL_FRect *rc, float *t_out, V2 *n_out) {
    float tmin = 0, tmax = 1;
    V2 n = v2(0, 0);
    float lo[2] = {rc->x, rc->y}, hi[2] = {rc->x + rc->w, rc->y + rc->h};
    float o[2] = {a.x, a.y}, dd[2] = {d.x, d.y};
    for (int ax = 0; ax < 2; ax++) {
        if (fabsf(dd[ax]) < 1e-6f) {
            if (o[ax] < lo[ax] || o[ax] > hi[ax]) return false;
            continue;
        }
        float t1 = (lo[ax] - o[ax]) / dd[ax], t2 = (hi[ax] - o[ax]) / dd[ax];
        float sgn = -1;
        if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; sgn = 1; }
        if (t1 > tmin) {
            tmin = t1;
            n = ax == 0 ? v2(sgn, 0) : v2(0, sgn);
        }
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return false;
    }
    if (tmin <= 0) return false;
    *t_out = tmin;
    *n_out = n;
    return true;
}

bool raycast(V2 a, V2 b, RayHit *hit) {
    V2 d = v2sub(b, a);
    float best = 2.0f;
    int best_seg = -1, best_door = -1;
    V2 best_n = v2(0, 0);
    int cx = tx_of(a.x), cy = tx_of(a.y);
    int ex = tx_of(b.x), ey = tx_of(b.y);
    int sx = d.x > 0 ? 1 : -1, sy = d.y > 0 ? 1 : -1;
    float tdx = fabsf(d.x) > 1e-6f ? TILE / fabsf(d.x) : 1e9f;
    float tdy = fabsf(d.y) > 1e-6f ? TILE / fabsf(d.y) : 1e9f;
    float nxb = sx > 0 ? (cx + 1) * TILE : cx * TILE;
    float nyb = sy > 0 ? (cy + 1) * TILE : cy * TILE;
    float tmx = fabsf(d.x) > 1e-6f ? (nxb - a.x) / d.x : 1e9f;
    float tmy = fabsf(d.y) > 1e-6f ? (nyb - a.y) / d.y : 1e9f;
    for (int guard = 0; guard < 600; guard++) {
        if (tile_in(cx, cy)) {
            int ci = cy * L.w + cx;
            for (int k = 0; k < L.cell_count[ci]; k++) {
                int si = L.cell_refs[L.cell_start[ci] + k];
                const Seg *s = &L.segs[si];
                if (v2dot(d, s->n) >= 0) continue; /* only hit from the open side */
                V2 e = v2sub(s->b, s->a);
                float den = v2cross(d, e);
                if (fabsf(den) < 1e-6f) continue;
                V2 qa = v2sub(s->a, a);
                float t = v2cross(qa, e) / den;
                float u = v2cross(qa, d) / den;
                if (t >= 0 && t < best && u >= -0.001f && u <= 1.001f) {
                    best = t;
                    best_seg = si;
                    best_n = s->n;
                }
            }
        } else break;
        float tnext = minf(tmx, tmy);
        if (best <= tnext) break;
        if (cx == ex && cy == ey) break;
        if (tnext > 1.0f) break;
        if (tmx < tmy) { cx += sx; tmx += tdx; }
        else { cy += sy; tmy += tdy; }
    }
    for (int i = 0; i < L.ndoors; i++) {
        const Door *dr = &L.doors[i];
        SDL_FRect r0, r1;
        if (!door_rects(dr, &r0, &r1)) continue;
        V2 c = door_center(dr);
        float span = (maxf(dr->x1 - dr->x0, dr->y1 - dr->y0) + 1) * TILE;
        /* quick reject: segment-to-point distance */
        float tt = clampf(v2dot(v2sub(c, a), d) / maxf(v2len2(d), 1e-6f), 0, 1);
        if (v2dist(v2mad(a, d, tt), c) > span) continue;
        float t;
        V2 n;
        if (r0.w > 0.5f && r0.h > 0.5f && ray_rect(a, d, &r0, &t, &n) && t < best) { best = t; best_door = i; best_seg = -1; best_n = n; }
        if (r1.w > 0.5f && r1.h > 0.5f && ray_rect(a, d, &r1, &t, &n) && t < best) { best = t; best_door = i; best_seg = -1; best_n = n; }
    }
    if (best <= 1.0f) {
        if (hit) {
            hit->t = best;
            hit->n = best_n;
            hit->seg = best_seg;
            hit->door = best_door;
            hit->p = v2mad(a, d, best);
        }
        return true;
    }
    return false;
}

bool los(V2 a, V2 b) { return !raycast(a, b, NULL); }

V2 safe_muzzle(V2 from, V2 to) {
    RayHit h;
    if (!raycast(from, to, &h)) return to;
    return v2lerp(from, h.p, 0.5f);
}

bool los_wide(V2 a, V2 b, float r) {
    V2 n = v2scale(v2perp(v2norm(v2sub(b, a))), r);
    return los(v2add(a, n), v2add(b, n)) && los(v2sub(a, n), v2sub(b, n));
}

/* ------------------------------------------------------------ flow fields */
static bool flow_passable(int x, int y, bool exit_mode) {
    if (!tile_in(x, y)) return false;
    if (L.tile[y][x] != T_EMPTY) return false;
    int di = L.door_id[y][x];
    if (di >= 0) {
        const Door *d = &L.doors[di];
        if (exit_mode) return true;
        if (d->lock != LOCK_NONE && d->open < 0.5f) return false;
    }
    return true;
}

void flow_compute_multi(uint16_t field[MAP_MAX_H][MAP_MAX_W], const int *xs, const int *ys, int n, bool exit_mode) {
    static int qx[MAP_MAX_W * MAP_MAX_H], qy[MAP_MAX_W * MAP_MAX_H];
    for (int y = 0; y < L.h; y++)
        for (int x = 0; x < L.w; x++) field[y][x] = 0xFFFF;
    int head = 0, tail = 0;
    for (int i = 0; i < n; i++) {
        if (!tile_in(xs[i], ys[i])) continue;
        field[ys[i]][xs[i]] = 0;
        qx[tail] = xs[i]; qy[tail] = ys[i]; tail++;
    }
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    while (head < tail) {
        int x = qx[head], y = qy[head];
        head++;
        uint16_t v = field[y][x];
        for (int k = 0; k < 4; k++) {
            int nx = x + dx[k], ny = y + dy[k];
            if (!flow_passable(nx, ny, exit_mode)) continue;
            if (field[ny][nx] != 0xFFFF) continue;
            field[ny][nx] = v + 1;
            qx[tail] = nx; qy[tail] = ny; tail++;
        }
    }
}

void flow_compute(uint16_t field[MAP_MAX_H][MAP_MAX_W], int sx, int sy, bool exit_mode) {
    flow_compute_multi(field, &sx, &sy, 1, exit_mode);
}

static bool best_neighbor(uint16_t field[MAP_MAX_H][MAP_MAX_W], int tx, int ty, bool descend, int *ox, int *oy) {
    int cur = tile_in(tx, ty) ? field[ty][tx] : 0xFFFF;
    float best = descend ? (cur == 0xFFFF ? 1e9f : (float)cur) : (cur == 0xFFFF ? -1.0f : (float)cur);
    int bx = -1, by = -1;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dy) continue;
            int nx = tx + dx, ny = ty + dy;
            if (!tile_in(nx, ny)) continue;
            uint16_t v = field[ny][nx];
            if (v == 0xFFFF) continue;
            if (dx && dy && (!tile_in(nx, ty) || !tile_in(tx, ny) || field[ty][nx] == 0xFFFF || field[ny][tx] == 0xFFFF))
                continue; /* no corner cutting */
            float score = (float)v + ((dx && dy) ? 0.01f : 0.0f);
            if (descend ? score < best : score > best) {
                best = score;
                bx = nx; by = ny;
            }
        }
    if (bx < 0) return false;
    *ox = bx; *oy = by;
    return true;
}

V2 flow_dir(uint16_t field[MAP_MAX_H][MAP_MAX_W], V2 pos, bool descend) {
    int tx = tx_of(pos.x), ty = tx_of(pos.y);
    int nx, ny;
    if (!best_neighbor(field, tx, ty, descend, &nx, &ny)) return v2(0, 0);
    /* look one more step ahead to smooth the path */
    int nx2 = nx, ny2 = ny;
    int ax, ay;
    if (best_neighbor(field, nx, ny, descend, &ax, &ay)) { nx2 = ax; ny2 = ay; }
    V2 t1 = tile_center(nx, ny), t2 = tile_center(nx2, ny2);
    V2 target = v2lerp(t1, t2, 0.5f);
    if (!los(pos, target)) target = t1;
    return v2norm(v2sub(target, pos));
}

int flow_path(uint16_t field[MAP_MAX_H][MAP_MAX_W], V2 pos, V2 *out, int max) {
    int tx = tx_of(pos.x), ty = tx_of(pos.y);
    int n = 0;
    for (int i = 0; i < max; i++) {
        int nx, ny;
        if (!best_neighbor(field, tx, ty, true, &nx, &ny)) break;
        out[n++] = tile_center(nx, ny);
        tx = nx; ty = ny;
        if (field[ty][tx] == 0) break;
    }
    return n;
}

/* ------------------------------------------------------------ fog of war */
void explore_update(V2 p, float radius) {
    int r = (int)(radius / TILE) + 1;
    int cx = tx_of(p.x), cy = tx_of(p.y);
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++) {
            if (!tile_in(x, y) || L.explored[y][x]) continue;
            V2 c = tile_center(x, y);
            if (v2dist(c, p) > radius) continue;
            if (L.tile[y][x] == T_SOLID || L.tile[y][x] == T_BREAK) continue;
            if (los(p, c)) L.explored[y][x] = 1;
        }
}
