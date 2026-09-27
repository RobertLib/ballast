/*
 * Procedural mines.
 *
 * A mine is a grid of 24x18 cells. The layout is a random tree grown from the
 * start room, so every room is reachable, plus a few loops inside the same key
 * region. The reactor sits at the deepest room and takes over free neighbouring
 * cells to become an arena two or four cells big (so does the Overseer's
 * foundry in the finale); the coloured doors lie on the path to it
 * (classic lock and key: each key waits in the region its door is the
 * gate of), the exit is a leaf far from the reactor and the vaults are dead
 * ends far from the exit - they only open once the reactor is gone, and the
 * deeper they are the more they pay.
 *
 * Every room is generated from an archetype (a pillared hall, a colonnade, a
 * cavern, a cross, a ring around a rock island, a split hall, an octagon, an
 * L-shaped hall, twin chambers),
 * flipped at random and joined to its neighbours by corridors that meet in the
 * middle of each cell side. Deeper sectors get more traps: plasma vents, mine
 * fields, sweeping lasers, gravity wells and laser gates in the corridors.
 *
 * The result is plain ASCII in the zones.c legend.
 *
 * Room markers (replaced here):
 *   ?  robot slot          %  feature slot (keys, hostages, weapons)
 *   $  optional loot       ;  guaranteed loot (secrets, vaults)
 *   &  robot generator slot
 */
#include "common.h"

#define CW 24
#define CH 18
#define GW_MAX 6
#define GH_MAX 5
#define NCELL (GW_MAX * GH_MAX)
#define RW_MAX (CW * 2)
#define RH_MAX (CH * 2)

enum { K_ROOM, K_ENERGY, K_START, K_REACTOR, K_EXIT, K_VAULT };
enum { A_HALL, A_COLONNADE, A_CAVERN, A_CROSS, A_RING, A_SPLIT, A_OCTAGON, A_LSHAPE, A_TWIN, A_ARENA, A_COUNT };
enum { TK_NONE, TK_VENTS, TK_MINES, TK_SWEEPER, TK_WELL };

enum { D_N, D_E, D_S, D_W };
static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};

enum { ROLE_NONE, ROLE_START, ROLE_REACTOR, ROLE_EXIT, ROLE_VAULT, ROLE_KEY, ROLE_MERGED };

typedef struct {
    bool used;
    int parent, pdir, depth, region;
    int links;       /* connected directions */
    char door[4];    /* door character in this cell's opening, 0 none */
    int role, key;   /* key: the lock index of the key in this room, -1 none */
    int kind, trap;
    bool on_path;
    int owner;       /* merged cells: the cell whose room covers them */
    int bx0, by0, bx1, by1; /* the cells the room covers (itself, or an arena block) */
} Cell;

static Cell cell[NCELL];
static int gw, gh;
static char grid[MAP_MAX_H][MAP_MAX_W + 1];
static const char *rowptr[MAP_MAX_H];
static int mw, mh;

static int cidx(int x, int y) { return y * gw + x; }
static int nbr(int c, int d) {
    int x = c % gw + DX[d], y = c / gw + DY[d];
    return x >= 0 && y >= 0 && x < gw && y < gh ? cidx(x, y) : -1;
}
static int degree(int c) {
    int n = 0;
    for (int d = 0; d < 4; d++) n += (cell[c].links >> d) & 1;
    return n;
}
static void link_cells(int a, int d) {
    int b = nbr(a, d);
    cell[a].links |= 1 << d;
    cell[b].links |= 1 << ((d + 2) & 3);
}
static int tree_dist(int a, int b) {
    int n = 0;
    while (a != b) {
        if (cell[a].depth >= cell[b].depth) a = cell[a].parent;
        else b = cell[b].parent;
        n++;
        if (a < 0 || b < 0) return 999;
    }
    return n;
}
static void cell_block(int c) {
    cell[c].bx0 = cell[c].bx1 = c % gw;
    cell[c].by0 = cell[c].by1 = c / gw;
    cell[c].owner = -1;
}
static int add_leaf(int from, int d) {
    int v = nbr(from, d);
    cell[v].used = true;
    cell[v].parent = from;
    cell[v].pdir = d;
    cell[v].depth = cell[from].depth + 1;
    cell[v].region = cell[from].region;
    cell[v].key = -1;
    cell_block(v);
    link_cells(from, d);
    return v;
}
/* grow a new leaf off a used cell with a free neighbour, the best by score (higher is better) */
static int attach_leaf(Rng *rng, int (*score)(int)) {
    int best = -1, bd = -1;
    float bs = -1e9f;
    for (int c = 0; c < gw * gh; c++) {
        if (!cell[c].used || cell[c].role == ROLE_VAULT || cell[c].role == ROLE_REACTOR || cell[c].role == ROLE_MERGED) continue;
        for (int d = 0; d < 4; d++) {
            int v = nbr(c, d);
            if (v < 0 || cell[v].used) continue;
            float s = (float)score(c) + rng_f(rng);
            if (s > bs) { bs = s; best = c; bd = d; }
        }
    }
    return best >= 0 ? add_leaf(best, bd) : -1;
}

static int exit_cell = -1, reactor_cell = -1;
static int score_exit(int c) { return tree_dist(c, reactor_cell) * 4 - cell[c].region * 6; }
static int score_vault(int c) { return exit_cell >= 0 ? tree_dist(c, exit_cell) : cell[c].depth; }

/* the map rectangle a cell's room covers, in tiles */
static void room_rect(int c, int *x0, int *y0, int *x1, int *y1) {
    *x0 = 1 + cell[c].bx0 * CW;
    *y0 = 1 + cell[c].by0 * CH;
    *x1 = 1 + (cell[c].bx1 + 1) * CW - 1;
    *y1 = 1 + (cell[c].by1 + 1) * CH - 1;
}

/* the tiles of a room holding a marker, in map coordinates */
static int find_marks(int c, char mark, int *xs, int *ys, int max) {
    int x0, y0, x1, y1, n = 0;
    room_rect(c, &x0, &y0, &x1, &y1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (grid[y][x] == mark && n < max) { xs[n] = x; ys[n] = y; n++; }
    return n;
}

/* put a feature into a room: a feature slot, else loot or plain floor away from walls */
static bool place_feature(Rng *rng, int c, char what) {
    static const char pref[3] = {'%', '$', '.'};
    static int xs[RW_MAX * RH_MAX], ys[RW_MAX * RH_MAX];
    for (int k = 0; k < 3; k++) {
        int n = find_marks(c, pref[k], xs, ys, RW_MAX * RH_MAX);
        if (k == 2) {
            /* plain floor needs open neighbours so nothing spawns in a corner */
            int m = 0;
            for (int i = 0; i < n; i++) {
                int x = xs[i], y = ys[i];
                bool open = true;
                for (int d = 0; d < 4; d++)
                    if (grid[y + DY[d]][x + DX[d]] == '#') open = false;
                if (open) { xs[m] = x; ys[m] = y; m++; }
            }
            n = m;
        }
        if (n > 0) {
            int i = rng_int(rng, n);
            grid[ys[i]][xs[i]] = what;
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------ room building */
static char rb[RH_MAX][RW_MAX];
static bool prot[RH_MAX][RW_MAX];  /* the exit booth: corridors and markers keep out */
static uint8_t dmap[RH_MAX][RW_MAX]; /* distance to the nearest rock */
static int rw, rh;
/* openings of the room being built: the first tile of each two-tile gap, and its side */
static int nop, opx[8], opy[8], opd[8];

static int imin(int a, int b) { return a < b ? a : b; }
static bool rb_in(int x, int y) { return x >= 0 && y >= 0 && x < rw && y < rh; }
static bool rb_floor(int x, int y) { return rb_in(x, y) && rb[y][x] != '#'; }
static void rb_set(int x, int y, char c) {
    if (x >= 1 && y >= 1 && x < rw - 1 && y < rh - 1) rb[y][x] = c;
}
static void rb_rect(int x0, int y0, int x1, int y1, char c) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) rb_set(x, y, c);
}
/* cut the corners of a rectangle into 45 degree slopes */
static void rb_chamfer(int x0, int y0, int x1, int y1, int k) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            int dx = imin(x - x0, x1 - x), dy = imin(y - y0, y1 - y);
            if (dx + dy < k) rb_set(x, y, '#');
        }
}
/* a block mirrored into all four quadrants of the floor rectangle */
static void rb_quad(int fx0, int fy0, int fx1, int fy1, int x, int y, int w, int h) {
    rb_rect(fx0 + x, fy0 + y, fx0 + x + w - 1, fy0 + y + h - 1, '#');
    rb_rect(fx1 - x - w + 1, fy0 + y, fx1 - x, fy0 + y + h - 1, '#');
    rb_rect(fx0 + x, fy1 - y - h + 1, fx0 + x + w - 1, fy1 - y, '#');
    rb_rect(fx1 - x - w + 1, fy1 - y - h + 1, fx1 - x, fy1 - y, '#');
}

static void arch_hall(Rng *rng, int fx0, int fy0, int fx1, int fy1) {
    int fw = fx1 - fx0 + 1, fh = fy1 - fy0 + 1;
    rb_rect(fx0, fy0, fx1, fy1, '.');
    rb_chamfer(fx0, fy0, fx1, fy1, 1 + rng_int(rng, 4));
    switch (rng_int(rng, 4)) {
    case 0: rb_quad(fx0, fy0, fx1, fy1, fw / 4, fh / 4, 2, 2); break;
    case 1: /* two long bars with a gap in the middle */
        for (int s = 0; s < 2; s++) {
            int y = s ? fy1 - fh / 3 : fy0 + fh / 3;
            rb_rect(fx0 + fw / 5, y, fx0 + fw / 2 - 3, y, '#');
            rb_rect(fx1 - fw / 2 + 3, y, fx1 - fw / 5, y, '#');
        }
        break;
    case 2:
        rb_rect(fx0 + fw / 2 - 2, fy0 + fh / 2 - 1, fx0 + fw / 2 + 1, fy0 + fh / 2, '#');
        rb_quad(fx0, fy0, fx1, fy1, 3, 3, 2, 2);
        break;
    default:
        for (int k = 0; k < 3; k++) rb_quad(fx0, fy0, fx1, fy1, 2 + rng_int(rng, fw / 2 - 4), 2 + rng_int(rng, fh / 2 - 4), 1, 1);
        break;
    }
}

static void arch_colonnade(Rng *rng, int fx0, int fy0, int fx1, int fy1) {
    rb_rect(fx0, fy0, fx1, fy1, '.');
    rb_chamfer(fx0, fy0, fx1, fy1, 2);
    int sp = 4 + rng_int(rng, 2), big = rng_int(rng, 2);
    int cx = (fx0 + fx1) / 2, cy = (fy0 + fy1) / 2;
    for (int y = fy0 + 2; y + big <= fy1 - 2; y += sp)
        for (int x = fx0 + 2; x + big <= fx1 - 2; x += sp) {
            /* keep the middle lines clear: that is where the corridors come in */
            if (abs(x - cx) <= 1 + big || abs(y - cy) <= 1 + big) continue;
            rb_rect(x, y, x + big, y + big, '#');
        }
}

static void arch_cavern(Rng *rng, int fx0, int fy0, int fx1, int fy1) {
    static char t[RH_MAX][RW_MAX];
    for (int y = fy0; y <= fy1; y++)
        for (int x = fx0; x <= fx1; x++) rb[y][x] = rng_f(rng) < 0.42f ? '#' : '.';
    for (int it = 0; it < 4; it++) {
        for (int y = fy0; y <= fy1; y++)
            for (int x = fx0; x <= fx1; x++) {
                int n = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                        if ((dx || dy) && (!rb_in(x + dx, y + dy) || x + dx < fx0 || x + dx > fx1 || y + dy < fy0 || y + dy > fy1 || rb[y + dy][x + dx] == '#'))
                            n++;
                t[y][x] = n >= 5 ? '#' : n <= 3 ? '.' : rb[y][x];
            }
        for (int y = fy0; y <= fy1; y++)
            for (int x = fx0; x <= fx1; x++) rb[y][x] = t[y][x];
    }
}

static void arch_cross(Rng *rng, int fx0, int fy0, int fx1, int fy1) {
    int fw = fx1 - fx0 + 1, fh = fy1 - fy0 + 1;
    int bx = fw / 4 + rng_int(rng, 2), by = fh / 4 + rng_int(rng, 2) - 1;
    rb_rect(fx0, fy0 + by, fx1, fy1 - by, '.');
    rb_rect(fx0 + bx, fy0, fx1 - bx, fy1, '.');
    rb_chamfer(fx0, fy0 + by, fx1, fy1 - by, 2);
    rb_chamfer(fx0 + bx, fy0, fx1 - bx, fy1, 2);
    if (rng_int(rng, 2)) {
        int cx = (fx0 + fx1) / 2, cy = (fy0 + fy1) / 2;
        rb_rect(cx - 1, cy - 1, cx, cy, '#');
        rb_quad(fx0, fy0, fx1, fy1, bx + 2, by + 2, 1, 1);
    }
}

static void arch_ring(Rng *rng, int fx0, int fy0, int fx1, int fy1) {
    int fw = fx1 - fx0 + 1, fh = fy1 - fy0 + 1;
    rb_rect(fx0, fy0, fx1, fy1, '.');
    rb_chamfer(fx0, fy0, fx1, fy1, 3);
    int iw = fw / 3 + rng_int(rng, 2), ih = fh / 3;
    int ix0 = fx0 + (fw - iw) / 2, iy0 = fy0 + (fh - ih) / 2;
    rb_rect(ix0, iy0, ix0 + iw - 1, iy0 + ih - 1, '#');
    if (iw >= 6 && ih >= 5 && rng_f(rng) < 0.6f) {
        /* a cache hidden in the island behind cracked rock */
        int cx = ix0 + iw / 2 - 1, cy = iy0 + ih / 2;
        rb[cy][cx] = ';';
        rb[cy][cx + 1] = ';';
        for (int x = cx + 2; x < ix0 + iw; x++) rb[cy][x] = x == ix0 + iw - 1 ? 'w' : '.';
    }
}

static void arch_split(Rng *rng, int fx0, int fy0, int fx1, int fy1) {
    int fw = fx1 - fx0 + 1, fh = fy1 - fy0 + 1;
    rb_rect(fx0, fy0, fx1, fy1, '.');
    rb_chamfer(fx0, fy0, fx1, fy1, 2);
    if (rng_int(rng, 2)) {
        int x = rng_int(rng, 2) ? fx0 + fw / 3 : fx1 - fw / 3;
        rb_rect(x, fy0, x + 1, fy1, '#');
        rb_rect(x, fy0 + 2, x + 1, fy0 + 4, '.');
        rb_rect(x, fy1 - 4, x + 1, fy1 - 2, '.');
        if (fh > 14) rb_rect(x, fy0 + fh / 2 - 1, x + 1, fy0 + fh / 2, '.');
    } else {
        int y = rng_int(rng, 2) ? fy0 + fh / 3 : fy1 - fh / 3;
        rb_rect(fx0, y, fx1, y, '#');
        rb_rect(fx0 + 2, y, fx0 + 5, y, '.');
        rb_rect(fx1 - 5, y, fx1 - 2, y, '.');
        rb_rect(fx0 + fw / 2 - 2, y, fx0 + fw / 2 + 1, y, '.');
    }
    rb_quad(fx0, fy0, fx1, fy1, fw / 6, fh / 5, 1, 1);
}

static void arch_octagon(Rng *rng, int fx0, int fy0, int fx1, int fy1) {
    int fw = fx1 - fx0 + 1, fh = fy1 - fy0 + 1;
    rb_rect(fx0, fy0, fx1, fy1, '.');
    rb_chamfer(fx0, fy0, fx1, fy1, (fw < fh ? fw : fh) / 3);
    if (rng_int(rng, 3)) rb_quad(fx0, fy0, fx1, fy1, fw / 4 + 1, fh / 4 + 1, 2, 2);
}

static void arch_lshape(Rng *rng, int fx0, int fy0, int fx1, int fy1) {
    int fw = fx1 - fx0 + 1, fh = fy1 - fy0 + 1;
    rb_rect(fx0, fy0, fx1, fy1, '.');
    /* cut away one quadrant, leaving the middle lines open */
    int qx = rng_int(rng, 2), qy = rng_int(rng, 2);
    int cw = fw / 2 - 2 - rng_int(rng, 2), ch = fh / 2 - 2 - rng_int(rng, 2);
    int x0 = qx ? fx1 - cw + 1 : fx0, y0 = qy ? fy1 - ch + 1 : fy0;
    rb_rect(x0, y0, x0 + cw - 1, y0 + ch - 1, '#');
    rb_chamfer(fx0, fy0, fx1, fy1, 2 + rng_int(rng, 2));
    rb_quad(fx0, fy0, fx1, fy1, fw / 4, fh / 4, 1, 1);
}

/* two chambers joined by a short wide passage */
static void arch_twin(Rng *rng, int fx0, int fy0, int fx1, int fy1) {
    int fw = fx1 - fx0 + 1, fh = fy1 - fy0 + 1;
    if (rng_int(rng, 2)) {
        int gap = 2 + rng_int(rng, 2);
        int ax1 = fx0 + fw / 2 - gap, bx0 = fx0 + fw / 2 + gap - 1;
        rb_rect(fx0, fy0, ax1, fy1, '.');
        rb_rect(bx0, fy0, fx1, fy1, '.');
        rb_chamfer(fx0, fy0, ax1, fy1, 2);
        rb_chamfer(bx0, fy0, fx1, fy1, 2);
        rb_rect(ax1, fy0 + fh / 2 - 3, bx0, fy0 + fh / 2 + 2, '.');
    } else {
        int gap = 2;
        int ay1 = fy0 + fh / 2 - gap, by0 = fy0 + fh / 2 + gap - 1;
        rb_rect(fx0, fy0, fx1, ay1, '.');
        rb_rect(fx0, by0, fx1, fy1, '.');
        rb_chamfer(fx0, fy0, fx1, ay1, 2);
        rb_chamfer(fx0, by0, fx1, fy1, 2);
        rb_rect(fx0 + fw / 2 - 4, ay1, fx0 + fw / 2 + 3, by0, '.');
    }
}

/* the reactor's and the Overseer's arena: wide open, with a ring of cover around the centre */
static void arch_arena(Rng *rng, int fx0, int fy0, int fx1, int fy1) {
    int fw = fx1 - fx0 + 1, fh = fy1 - fy0 + 1;
    rb_rect(fx0, fy0, fx1, fy1, '.');
    rb_chamfer(fx0, fy0, fx1, fy1, 3 + (fw > 30 || fh > 24 ? 2 : 0));
    float cx = (fx0 + fx1) * 0.5f, cy = (fy0 + fy1) * 0.5f;
    int n = fw > 30 && fh > 24 ? 8 : fw > 30 || fh > 24 ? 6 : 4;
    float a0 = rng_int(rng, 2) ? PI / n : 0;
    for (int i = 0; i < n; i++) {
        float a = a0 + TAU * i / n;
        int x = (int)roundf(cx + cosf(a) * fw * 0.3f), y = (int)roundf(cy + sinf(a) * fh * 0.3f);
        rb_rect(x - 1, y - 1, x, y, '#');
    }
    /* cover walls near the corners of big arenas */
    if (fw > 30 && fh > 24) rb_quad(fx0, fy0, fx1, fy1, fw / 7, fh / 7, 3, 1);
}

static void rb_flip(bool fx, bool fy) {
    for (int y = 0; y < rh; y++)
        for (int x = 0; x < rw; x++) {
            int sx = fx ? rw - 1 - x : x, sy = fy ? rh - 1 - y : y;
            if (sy * rw + sx <= y * rw + x) continue;
            char t = rb[y][x]; rb[y][x] = rb[sy][sx]; rb[sy][sx] = t;
            bool p = prot[y][x]; prot[y][x] = prot[sy][sx]; prot[sy][sx] = p;
        }
}

static void compute_dmap(void) {
    static int qx[RW_MAX * RH_MAX], qy[RW_MAX * RH_MAX];
    int qh = 0, qt = 0;
    for (int y = 0; y < rh; y++)
        for (int x = 0; x < rw; x++) {
            dmap[y][x] = rb[y][x] == '#' ? 0 : 255;
            if (!dmap[y][x]) { qx[qt] = x; qy[qt] = y; qt++; }
        }
    while (qh < qt) {
        int x = qx[qh], y = qy[qh];
        qh++;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (!rb_in(nx, ny) || dmap[ny][nx] <= dmap[y][x] + 1) continue;
            dmap[ny][nx] = (uint8_t)(dmap[y][x] + 1);
            qx[qt] = nx; qy[qt] = ny; qt++;
        }
    }
}

/* the largest connected patch of floor: what the corridors must reach */
static uint8_t region_of[RH_MAX][RW_MAX];
static void mark_main_region(void) {
    static int qx[RW_MAX * RH_MAX], qy[RW_MAX * RH_MAX];
    static int id[RH_MAX][RW_MAX];
    memset(id, 0, sizeof(id));
    int best = 0, best_n = 0, next = 0;
    for (int y = 1; y < rh - 1; y++)
        for (int x = 1; x < rw - 1; x++) {
            if (!rb_floor(x, y) || rb[y][x] == 'w' || id[y][x] || prot[y][x]) continue;
            next++;
            int qh = 0, qt = 0, n = 0;
            qx[qt] = x; qy[qt] = y; qt++;
            id[y][x] = next;
            while (qh < qt) {
                int cx = qx[qh], cy = qy[qh];
                qh++;
                n++;
                for (int d = 0; d < 4; d++) {
                    int nx = cx + DX[d], ny = cy + DY[d];
                    if (!rb_floor(nx, ny) || rb[ny][nx] == 'w' || id[ny][nx] || prot[ny][nx]) continue;
                    id[ny][nx] = next;
                    qx[qt] = nx; qy[qt] = ny; qt++;
                }
            }
            if (n > best_n) { best_n = n; best = next; }
        }
    for (int y = 0; y < rh; y++)
        for (int x = 0; x < rw; x++) region_of[y][x] = best && id[y][x] == best;
}

/* a corridor from an opening to the main floor: the shortest path of a two-by-two brush */
static void carve_opening(int sx, int sy, int d) {
    static int prev[RH_MAX * RW_MAX];
    static int q[RH_MAX * RW_MAX];
    int ax = sx, ay = sy;
    switch (d) {
    case D_N: ay = 1; break;
    case D_S: ay = rh - 3; break;
    case D_W: ax = 1; break;
    default: ax = rw - 3; break;
    }
    for (int i = 0; i < rw * rh; i++) prev[i] = -2;
    int qh = 0, qt = 0, goal = -1;
    q[qt++] = ay * rw + ax;
    prev[ay * rw + ax] = -1;
    int in = (d + 2) & 3; /* try to head straight in first */
    while (qh < qt) {
        int cur = q[qh++];
        int x = cur % rw, y = cur / rw;
        if (region_of[y][x] || region_of[y][x + 1] || region_of[y + 1][x] || region_of[y + 1][x + 1]) { goal = cur; break; }
        for (int k = 0; k < 4; k++) {
            int dd = (in + k) & 3;
            int nx = x + DX[dd], ny = y + DY[dd];
            if (nx < 1 || ny < 1 || nx > rw - 3 || ny > rh - 3) continue;
            if (prot[ny][nx] || prot[ny][nx + 1] || prot[ny + 1][nx] || prot[ny + 1][nx + 1]) continue;
            int ni = ny * rw + nx;
            if (prev[ni] != -2) continue;
            prev[ni] = cur;
            q[qt++] = ni;
        }
    }
    /* the gap in the cell side itself */
    if (d == D_N || d == D_S) rb[sy][sx] = rb[sy][sx + 1] = '.';
    else rb[sy][sx] = rb[sy + 1][sx] = '.';
    for (int cur = goal; cur >= 0; cur = prev[cur]) {
        int x = cur % rw, y = cur / rw;
        for (int dy = 0; dy <= 1; dy++)
            for (int dx = 0; dx <= 1; dx++)
                if (rb[y + dy][x + dx] == '#') rb[y + dy][x + dx] = '.';
    }
}

/* floor the corridors can't reach becomes rock again */
static void drop_unreachable(void) {
    static int qx[RW_MAX * RH_MAX], qy[RW_MAX * RH_MAX];
    static bool seen[RH_MAX][RW_MAX];
    memset(seen, 0, sizeof(seen));
    int qh = 0, qt = 0;
    for (int i = 0; i < nop; i++) {
        qx[qt] = opx[i]; qy[qt] = opy[i]; qt++;
        seen[opy[i]][opx[i]] = true;
    }
    while (qh < qt) {
        int x = qx[qh], y = qy[qh];
        qh++;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (!rb_floor(nx, ny) || seen[ny][nx]) continue;
            seen[ny][nx] = true;
            qx[qt] = nx; qy[qt] = ny; qt++;
        }
    }
    for (int y = 0; y < rh; y++)
        for (int x = 0; x < rw; x++)
            if (rb[y][x] != '#' && !seen[y][x]) rb[y][x] = '#';
}

static bool near_opening(int x, int y, int r) {
    for (int i = 0; i < nop; i++)
        if (abs(x - opx[i]) <= r && abs(y - opy[i]) <= r) return true;
    return false;
}

/* a random plain floor tile at least `open` from the rock and `far` from the openings, or false */
static bool pick_tile(Rng *rng, int open, int far, int *ox, int *oy) {
    for (int tries = 0; tries < 200; tries++) {
        int x = 1 + rng_int(rng, rw - 2), y = 1 + rng_int(rng, rh - 2);
        if (rb[y][x] != '.' || prot[y][x] || !region_of[y][x] || dmap[y][x] < open || near_opening(x, y, far)) continue;
        *ox = x; *oy = y;
        return true;
    }
    return false;
}

static bool spaced(int x, int y, const char *marks, int r) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (rb_in(x + dx, y + dy) && strchr(marks, rb[y + dy][x + dx])) return false;
    return true;
}

static int floor_area(void) {
    int n = 0;
    for (int y = 0; y < rh; y++)
        for (int x = 0; x < rw; x++) n += rb[y][x] != '#';
    return n;
}

static void place_markers(Rng *rng, int kind) {
    int area = floor_area(), x, y;
    int robots = kind == K_VAULT ? 1 : kind == K_START ? 0 : kind == K_REACTOR ? imin(area / 26, 8) : area / 26;
    for (int i = 0, tries = 0; i < robots && tries < 300; tries++) {
        if (!pick_tile(rng, 1, 5, &x, &y) || !spaced(x, y, "?", 2)) continue;
        rb[y][x] = '?';
        i++;
    }
    int feats = kind == K_VAULT ? 0 : 2;
    for (int i = 0, tries = 0; i < feats && tries < 200; tries++) {
        if (!pick_tile(rng, 2, 3, &x, &y) || !spaced(x, y, "%?", 1)) continue;
        rb[y][x] = '%';
        i++;
    }
    int loot = kind == K_VAULT ? 0 : 2 + rng_int(rng, 3);
    for (int i = 0, tries = 0; i < loot && tries < 200; tries++) {
        if (!pick_tile(rng, 1, 2, &x, &y) || dmap[y][x] != 1) continue;
        rb[y][x] = '$';
        i++;
    }
    if (kind == K_ROOM || kind == K_ENERGY || kind == K_EXIT)
        for (int tries = 0; tries < 100; tries++)
            if (pick_tile(rng, 2, 6, &x, &y) && spaced(x, y, "?%", 1)) { rb[y][x] = '&'; break; }
}

/* a cache behind cracked rock in the room's wall */
static void place_secret(Rng *rng) {
    for (int tries = 0; tries < 80; tries++) {
        int x = 2 + rng_int(rng, rw - 4), y = 2 + rng_int(rng, rh - 4);
        if (rb[y][x] != '.' || dmap[y][x] != 1) continue;
        for (int d = 0; d < 4; d++) {
            int x1 = x + DX[d], y1 = y + DY[d], x2 = x1 + DX[d], y2 = y1 + DY[d], x3 = x2 + DX[d], y3 = y2 + DY[d];
            if (x3 < 1 || y3 < 1 || x3 > rw - 2 || y3 > rh - 2) continue;
            if (rb[y1][x1] != '#' || rb[y2][x2] != '#' || rb[y3][x3] != '#' || prot[y1][x1]) continue;
            bool closed = true;
            for (int k = 0; k < 4 && closed; k++) {
                int ax = x2 + DX[k], ay = y2 + DY[k], bx = x3 + DX[k], by = y3 + DY[k];
                if (!(ax == x1 && ay == y1) && rb_floor(ax, ay)) closed = false;
                if (!(bx == x2 && by == y2) && rb_floor(bx, by)) closed = false;
            }
            if (!closed || near_opening(x1, y1, 3)) continue;
            rb[y1][x1] = 'w';
            rb[y2][x2] = ';';
            rb[y3][x3] = ';';
            return;
        }
    }
}

static void place_traps(Rng *rng, int trap, int depth) {
    int x, y;
    switch (trap) {
    case TK_VENTS: {
        int n = 2 + rng_int(rng, 2) + depth / 2;
        for (int i = 0, tries = 0; i < n && tries < 200; tries++)
            if (pick_tile(rng, 2, 4, &x, &y) && spaced(x, y, "~", 3)) { rb[y][x] = '~'; i++; }
    } break;
    case TK_MINES:
        if (pick_tile(rng, 2, 6, &x, &y)) {
            int n = 4 + rng_int(rng, 3) + depth;
            for (int i = 0, tries = 0; i < n && tries < 80; tries++) {
                int mx = x + rng_int(rng, 7) - 3, my = y + rng_int(rng, 7) - 3;
                if (!rb_in(mx, my) || rb[my][mx] != '.' || near_opening(mx, my, 4) || !spaced(mx, my, "n", 1)) continue;
                rb[my][mx] = 'n';
                i++;
            }
        }
        break;
    case TK_SWEEPER: {
        int cx = rw / 2, cy = rh / 2;
        for (int r = 0; r < 4; r++)
            for (int k = 0; k < 8; k++) {
                int sx = cx + (k % 3 - 1) * r, sy = cy + (k / 3 - 1) * r;
                if (rb_in(sx, sy) && rb[sy][sx] == '.' && dmap[sy][sx] >= 3) { rb[sy][sx] = 'O'; return; }
            }
    } break;
    case TK_WELL:
        if (pick_tile(rng, 3, 6, &x, &y)) rb[y][x] = 'Y';
        break;
    }
}

/* the booth at the end of the exit tunnel, in the top-left corner of the floor before the flips */
static void exit_booth(int fx0, int fy0) {
    rb_rect(fx0, fy0 - 1, fx0 + 3, fy0 + 3, '#');
    rb_rect(fx0 + 1, fy0, fx0 + 2, fy0 + 1, 'Z');
    rb_rect(fx0 + 1, fy0 + 2, fx0 + 2, fy0 + 2, '.');
    rb_rect(fx0 + 1, fy0 + 3, fx0 + 2, fy0 + 3, 'x');
    rb_rect(fx0, fy0 + 4, fx0 + 3, fy0 + 5, '.');
    for (int y = fy0 - 1; y <= fy0 + 3; y++)
        for (int x = fx0 - 1; x <= fx0 + 4; x++)
            if (rb_in(x, y)) prot[y][x] = true;
}

/* build the room of cell c (and the cells merged into it) and stamp it into the map */
static void build_room(Rng *rng, int c, const MapSpec *s) {
    Cell *e = &cell[c];
    rw = (e->bx1 - e->bx0 + 1) * CW;
    rh = (e->by1 - e->by0 + 1) * CH;
    memset(rb, '#', sizeof(rb));
    memset(prot, 0, sizeof(prot));
    bool arena = e->kind == K_REACTOR;
    /* the floor rectangle */
    int fx0, fy0, fx1, fy1;
    if (arena) { fx0 = fy0 = 2; fx1 = rw - 3; fy1 = rh - 3; }
    else if (e->kind == K_VAULT) { fx0 = (rw - 12) / 2; fy0 = (rh - 9) / 2; fx1 = rw - 1 - fx0; fy1 = rh - 1 - fy0; }
    else {
        /* uneven margins: rooms of many sizes, not all centred in their cell */
        fx0 = 1 + rng_int(rng, 4); fx1 = rw - 2 - rng_int(rng, 4);
        fy0 = 1 + rng_int(rng, 3); fy1 = rh - 2 - rng_int(rng, 3);
    }
    int arch;
    if (arena) arch = A_ARENA;
    else if (e->kind == K_VAULT) arch = -1;
    else if (e->kind == K_START || e->kind == K_EXIT) arch = rng_int(rng, 2) ? A_HALL : A_COLONNADE;
    else arch = rng_int(rng, A_ARENA);
    if (e->trap == TK_SWEEPER && (arch == A_RING || arch == A_COLONNADE || arch == A_CAVERN || arch == A_TWIN)) arch = A_HALL;
    switch (arch) {
    case A_HALL: arch_hall(rng, fx0, fy0, fx1, fy1); break;
    case A_COLONNADE: arch_colonnade(rng, fx0, fy0, fx1, fy1); break;
    case A_CAVERN: arch_cavern(rng, fx0, fy0, fx1, fy1); break;
    case A_CROSS: arch_cross(rng, fx0, fy0, fx1, fy1); break;
    case A_RING: arch_ring(rng, fx0, fy0, fx1, fy1); break;
    case A_SPLIT: arch_split(rng, fx0, fy0, fx1, fy1); break;
    case A_OCTAGON: arch_octagon(rng, fx0, fy0, fx1, fy1); break;
    case A_LSHAPE: arch_lshape(rng, fx0, fy0, fx1, fy1); break;
    case A_TWIN: arch_twin(rng, fx0, fy0, fx1, fy1); break;
    case A_ARENA: arch_arena(rng, fx0, fy0, fx1, fy1); break;
    default: /* a vault: a plain chamber */
        rb_rect(fx0, fy0, fx1, fy1, '.');
        rb_chamfer(fx0, fy0, fx1, fy1, 2);
        break;
    }
    if (e->kind == K_EXIT) exit_booth(fx0, fy0);
    rb_flip(rng_f(rng) < 0.5f, rng_f(rng) < 0.5f);

    /* openings: every link of every cell the room covers */
    nop = 0;
    for (int by = e->by0; by <= e->by1; by++)
        for (int bx = e->bx0; bx <= e->bx1; bx++) {
            int cc = cidx(bx, by);
            for (int d = 0; d < 4; d++) {
                if (!((cell[cc].links >> d) & 1) || nop >= 8) continue;
                int lx = (bx - e->bx0) * CW, ly = (by - e->by0) * CH;
                switch (d) {
                case D_N: opx[nop] = lx + CW / 2 - 1; opy[nop] = ly; break;
                case D_S: opx[nop] = lx + CW / 2 - 1; opy[nop] = ly + CH - 1; break;
                case D_W: opx[nop] = lx; opy[nop] = ly + CH / 2 - 1; break;
                default: opx[nop] = lx + CW - 1; opy[nop] = ly + CH / 2 - 1; break;
                }
                opd[nop++] = d;
            }
        }

    /* the room's own features, then the corridors */
    int cx = rw / 2, cy = rh / 2;
    if (e->kind == K_REACTOR) {
        rb_rect(cx - 3, cy - 3, cx + 2, cy + 2, '.');
        rb[cy][cx] = s->boss ? 'W' : 'C';
    }
    if (e->kind == K_START) {
        rb_rect(cx - 2, cy - 2, cx + 1, cy + 1, '.');
        rb[cy][cx] = 'S';
    }
    compute_dmap();
    mark_main_region();
    if (e->kind == K_START || e->kind == K_ENERGY) {
        /* an energy center on open floor */
        int w = e->kind == K_START ? 2 : 3;
        for (int tries = 0; tries < 200; tries++) {
            int x = 2 + rng_int(rng, rw - 4 - w), y = 2 + rng_int(rng, rh - 5);
            bool ok = true;
            for (int yy = y; yy < y + 2 && ok; yy++)
                for (int xx = x; xx < x + w && ok; xx++)
                    if (rb[yy][xx] != '.' || dmap[yy][xx] < 2 || prot[yy][xx] || !region_of[yy][xx] || near_opening(xx, yy, 3)) ok = false;
            if (!ok) continue;
            rb_rect(x, y, x + w - 1, y + 1, 'E');
            break;
        }
    }
    if (e->kind == K_REACTOR && !s->boss) {
        /* turret platforms around the core; the first mines keep only half of them */
        static const int qx[4] = {-1, 1, -1, 1}, qy[4] = {-1, -1, 1, 1};
        for (int k = 0; k < 4; k++) {
            if (s->depth < 1 && (k & 1)) continue;
            int tx = cx + qx[k] * rw / 3, ty = cy + qy[k] * rh / 3;
            if (rb_in(tx, ty) && rb[ty][tx] == '.') rb[ty][tx] = 't';
        }
    }
    if (e->kind == K_VAULT) {
        rb[cy][cx] = '@';
        static const int vx[4] = {-3, 3, -3, 3}, vy[4] = {-2, -2, 2, 2};
        for (int k = 0; k < 4; k++)
            if (rb[cy + vy[k]][cx + vx[k]] == '.') rb[cy + vy[k]][cx + vx[k]] = ';';
    }
    place_markers(rng, e->kind);
    if (e->kind == K_ROOM && rng_f(rng) < 0.3f) place_secret(rng);
    place_traps(rng, e->trap, s->depth);

    mark_main_region();
    for (int i = 0; i < nop; i++) carve_opening(opx[i], opy[i], opd[i]);
    drop_unreachable();

    /* stamp */
    int ox = 1 + e->bx0 * CW, oy = 1 + e->by0 * CH;
    for (int y = 0; y < rh; y++)
        for (int x = 0; x < rw; x++) grid[oy + y][ox + x] = rb[y][x];
}

/* ------------------------------------------------------------ robots */
typedef struct { char ch; float cost, weight; int depth; } Roster;
static const Roster ROSTER[3][11] = {
    {{'d', 1, 5, 0}, {'l', 1.2f, 3, 0}, {'g', 1.2f, 2, 0}, {'t', 1.5f, 1.2f, 0}, {'a', 1.4f, 1.6f, 1}},
    {{'d', 1, 2, 0}, {'l', 1.2f, 1, 0}, {'g', 1.2f, 1.2f, 0}, {'h', 3, 1.6f, 0}, {'s', 2.5f, 1.6f, 0}, {'v', 2.5f, 1.2f, 0},
     {'t', 1.5f, 0.8f, 0}, {'a', 1.4f, 1.6f, 0}, {'P', 3, 1.4f, 0}, {'B', 3, 1.2f, 0}, {'k', 2.5f, 1.1f, 3}},
    {{'l', 1.2f, 1.2f, 0}, {'h', 3, 1.2f, 0}, {'s', 2.5f, 1.2f, 0}, {'v', 2.5f, 1.5f, 0}, {'c', 2, 1.6f, 0}, {'u', 6, 1, 0},
     {'a', 1.4f, 1.2f, 0}, {'P', 3, 1.4f, 0}, {'B', 3, 1.2f, 0}, {'k', 2.5f, 1.4f, 0}, {'q', 7, 0.8f, 0}},
};
static const int ROSTER_N[3] = {5, 11, 11};

static const Roster *pick_robot(Rng *rng, int zone, int depth, float left) {
    float tot = 0;
    for (int i = 0; i < ROSTER_N[zone]; i++)
        if (ROSTER[zone][i].cost <= left && ROSTER[zone][i].depth <= depth) tot += ROSTER[zone][i].weight;
    if (tot <= 0) return NULL;
    float r = rng_f(rng) * tot;
    for (int i = 0; i < ROSTER_N[zone]; i++) {
        const Roster *e = &ROSTER[zone][i];
        if (e->cost > left || e->depth > depth) continue;
        if ((r -= e->weight) <= 0) return e;
    }
    return NULL;
}

/* ------------------------------------------------------------ loot */
static char minor_loot(Rng *rng, int zone) {
    float r = rng_f(rng);
    if (r < 0.4f) return '+';
    if (r < 0.7f) return '*';
    static const char missiles[3][3] = {{'m', 'm', 'p'}, {'m', 'o', 'p'}, {'o', 'i', 'M'}};
    return missiles[zone][rng_int(rng, 3)];
}

static char rare_loot(Rng *rng, int zone) {
    float r = rng_f(rng);
    if (r < 0.3f) return 'L';
    if (r < 0.45f) return 'Q';
    if (r < 0.6f) return 'K';
    if (r < 0.72f) return 'I';
    if (r < 0.86f) return zone >= 1 ? 'i' : 'o';
    return 'M';
}

/* ------------------------------------------------------------ layout */
/* how many free cells a room at c could take over to become an arena, and which */
static int arena_block(int c, int *bx0, int *by0, int *bx1, int *by1) {
    int x = c % gw, y = c / gw;
    *bx0 = *bx1 = x;
    *by0 = *by1 = y;
    int best = 1;
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) {
            int ax = x + sx, ay = y + sy;
            if (ax < 0 || ay < 0 || ax >= gw || ay >= gh) continue;
            if (cell[cidx(ax, y)].used || cell[cidx(x, ay)].used || cell[cidx(ax, ay)].used) continue;
            *bx0 = x < ax ? x : ax; *bx1 = x < ax ? ax : x;
            *by0 = y < ay ? y : ay; *by1 = y < ay ? ay : y;
            return 4;
        }
    for (int d = 0; d < 4; d++) {
        int v = nbr(c, d);
        if (v < 0 || cell[v].used) continue;
        int ax = v % gw, ay = v / gw;
        *bx0 = x < ax ? x : ax; *bx1 = x < ax ? ax : x;
        *by0 = y < ay ? y : ay; *by1 = y < ay ? ay : y;
        best = 2;
        break;
    }
    return best;
}

static bool try_build(const MapSpec *s, Rng *rng, MapInfo *info) {
    memset(cell, 0, sizeof(cell));
    int n = gw * gh;
    for (int i = 0; i < n; i++) { cell[i].parent = -1; cell[i].key = -1; cell_block(i); }
    int rooms = clampi(s->rooms, 3, n - 4 - s->vaults);

    /* 1. a random tree, grown mostly from the newest room for long winding branches */
    int start = rng_int(rng, n);
    cell[start].used = true;
    cell[start].role = ROLE_START;
    int order[NCELL], cnt = 1;
    order[0] = start;
    while (cnt < rooms) {
        int from = -1;
        if (rng_f(rng) < 0.55f) {
            for (int k = cnt - 1; k >= 0 && from < 0; k--)
                for (int d = 0; d < 4; d++) {
                    int v = nbr(order[k], d);
                    if (v >= 0 && !cell[v].used) { from = order[k]; break; }
                }
        }
        if (from < 0) {
            int cand[NCELL], nc = 0;
            for (int k = 0; k < cnt; k++)
                for (int d = 0; d < 4; d++) {
                    int v = nbr(order[k], d);
                    if (v >= 0 && !cell[v].used) { cand[nc++] = order[k]; break; }
                }
            if (!nc) break;
            from = cand[rng_int(rng, nc)];
        }
        int dirs[4], nd = 0;
        for (int d = 0; d < 4; d++) {
            int v = nbr(from, d);
            if (v >= 0 && !cell[v].used) dirs[nd++] = d;
        }
        order[cnt++] = add_leaf(from, dirs[rng_int(rng, nd)]);
    }
    if (cnt < 4) return false;

    /* 2. the reactor deep in the tree, where it can spread into an arena */
    reactor_cell = -1;
    int maxd = 0;
    for (int k = 1; k < cnt; k++)
        if (cell[order[k]].depth > maxd) maxd = cell[order[k]].depth;
    if (maxd < 2) return false;
    float bs = -1e9f;
    for (int k = 1; k < cnt; k++) {
        int c = order[k], b0, b1, b2, b3;
        if (cell[c].depth < maxd - 1 || cell[c].depth < 2) continue;
        float sc = arena_block(c, &b0, &b1, &b2, &b3) * 3.0f + cell[c].depth * 2.0f + (degree(c) == 1 ? 1.0f : 0.0f) + rng_f(rng);
        if (sc > bs) { bs = sc; reactor_cell = c; }
    }
    if (reactor_cell < 0) return false;
    {
        Cell *rc = &cell[reactor_cell];
        rc->role = ROLE_REACTOR;
        arena_block(reactor_cell, &rc->bx0, &rc->by0, &rc->bx1, &rc->by1);
        for (int y = rc->by0; y <= rc->by1; y++)
            for (int x = rc->bx0; x <= rc->bx1; x++) {
                int m = cidx(x, y);
                if (m == reactor_cell) continue;
                cell[m].used = true;
                cell[m].role = ROLE_MERGED;
                cell[m].owner = reactor_cell;
                cell[m].key = -1;
            }
    }
    int path[NCELL], np = 0;
    for (int c = reactor_cell; c >= 0; c = cell[c].parent) path[np++] = c;
    for (int i = 0; i < np / 2; i++) { int t = path[i]; path[i] = path[np - 1 - i]; path[np - 1 - i] = t; }
    for (int i = 0; i < np; i++) cell[path[i]].on_path = true;

    /* 3. coloured doors along the path, the last one guarding the reactor */
    int edges = np - 1;
    int keys = clampi(s->keys, 0, edges);
    int lock_at[3];
    for (int i = 0; i < keys; i++) lock_at[i] = i == keys - 1 ? edges - 1 : (edges - 1) * (i + 1) / keys;
    for (int i = 1; i < keys; i++)
        if (lock_at[i] <= lock_at[i - 1]) return false;
    for (int i = 0; i < keys; i++) {
        int child = path[lock_at[i] + 1];
        cell[child].door[(cell[child].pdir + 2) & 3] = "byr"[i];
    }
    /* regions: how many locks lie between the start and a room */
    for (int k = 1; k < cnt; k++) {
        int c = order[k];
        int back = (cell[c].pdir + 2) & 3;
        cell[c].region = cell[cell[c].parent].region + (cell[c].door[back] ? 1 : 0);
    }

    /* 4. each key waits in the region its door is the gate of, off the main path if it can */
    for (int i = 0; i < keys; i++) {
        int best = -1;
        float kb = -1e9f;
        for (int k = 0; k < cnt; k++) {
            int c = order[k];
            if (cell[c].region != i || cell[c].role == ROLE_REACTOR || cell[c].key >= 0) continue;
            float sc = cell[c].depth + (cell[c].on_path ? 0 : 6) + (cell[c].role == ROLE_START ? -20 : 0) + rng_f(rng);
            if (sc > kb) { kb = sc; best = c; }
        }
        if (best < 0) return false;
        cell[best].key = i;
        if (cell[best].role == ROLE_NONE) cell[best].role = ROLE_KEY;
    }

    /* 5. the exit: a dead end far from the reactor */
    exit_cell = -1;
    bs = -1e9f;
    for (int k = 1; k < cnt; k++) {
        int c = order[k];
        if (cell[c].role != ROLE_NONE || degree(c) != 1) continue;
        float sc = (float)score_exit(c) + rng_f(rng);
        if (sc > bs) { bs = sc; exit_cell = c; }
    }
    if (exit_cell < 0) exit_cell = attach_leaf(rng, score_exit);
    if (exit_cell < 0) return false;
    cell[exit_cell].role = ROLE_EXIT;

    /* 6. vaults: dead ends, the further from the exit the better */
    int vaults = 0;
    for (int v = 0; v < s->vaults; v++) {
        int best = -1;
        bs = -1e9f;
        for (int c = 0; c < n; c++) {
            if (!cell[c].used || cell[c].role != ROLE_NONE || degree(c) != 1) continue;
            float sc = (float)score_vault(c) + rng_f(rng);
            if (sc > bs) { bs = sc; best = c; }
        }
        if (best < 0) best = attach_leaf(rng, score_vault);
        if (best < 0) break;
        cell[best].role = ROLE_VAULT;
        cell[best].door[(cell[best].pdir + 2) & 3] = 'X';
        vaults++;
    }

    /* 7. a few loops inside a key region, never into a vault or the arena */
    int loops = rooms / 4;
    for (int tries = 0; tries < 40 && loops > 0; tries++) {
        int c = rng_int(rng, n), d = rng_int(rng, 4);
        int v = nbr(c, d);
        if (v < 0 || !cell[c].used || !cell[v].used || (cell[c].links >> d) & 1) continue;
        if (cell[c].region != cell[v].region || cell[c].role == ROLE_VAULT || cell[v].role == ROLE_VAULT) continue;
        if (cell[c].role == ROLE_MERGED || cell[v].role == ROLE_MERGED) continue;
        if (cell[c].role == ROLE_REACTOR && cell[v].owner == c) continue;
        link_cells(c, d);
        loops--;
    }

    /* 8. plain doors on some of the other openings */
    for (int c = 0; c < n; c++) {
        if (!cell[c].used) continue;
        for (int d = 0; d < 4; d++) {
            if (!((cell[c].links >> d) & 1) || cell[c].door[d]) continue;
            int v = nbr(c, d);
            if (cell[v].door[(d + 2) & 3]) continue;
            if (c < v && rng_f(rng) < 0.3f) cell[c].door[d] = '-';
        }
    }

    /* 9. room kinds and their traps */
    int energy_left = s->drained ? 0 : 1 + (rooms >= 9);
    float trap_p = clampf((0.22f + 0.1f * s->depth) * s->traps, 0, 0.9f);
    for (int c = 0; c < n; c++) {
        Cell *e = &cell[c];
        e->trap = TK_NONE;
        if (!e->used || e->role == ROLE_MERGED) continue;
        switch (e->role) {
        case ROLE_START: e->kind = K_START; break;
        case ROLE_REACTOR: e->kind = K_REACTOR; break;
        case ROLE_EXIT: e->kind = K_EXIT; break;
        case ROLE_VAULT: e->kind = K_VAULT; break;
        default: e->kind = K_ROOM; break;
        }
        if ((e->kind == K_ROOM || (e->kind == K_EXIT && s->depth >= 2)) && rng_f(rng) < trap_p) {
            int opts[4], no = 0;
            opts[no++] = TK_VENTS;
            opts[no++] = TK_MINES;
            if (s->depth >= 1) opts[no++] = TK_SWEEPER;
            if (s->depth >= 2) opts[no++] = TK_WELL;
            e->trap = opts[rng_int(rng, no)];
        }
    }
    for (int tries = 0; tries < 30 && energy_left > 0; tries++) {
        int c = order[rng_int(rng, cnt)];
        if (cell[c].kind != K_ROOM || cell[c].region > 1) continue;
        cell[c].kind = K_ENERGY;
        energy_left--;
    }

    /* 10. build the rooms and stamp them into the map */
    mw = gw * CW + 2;
    mh = gh * CH + 2;
    for (int y = 0; y < mh; y++) {
        memset(grid[y], '#', (size_t)mw);
        grid[y][mw] = 0;
    }
    for (int c = 0; c < n; c++)
        if (cell[c].used && cell[c].role != ROLE_MERGED) build_room(rng, c, s);
    for (int c = 0; c < n; c++) {
        Cell *e = &cell[c];
        if (!e->used) continue;
        int ox = 1 + (c % gw) * CW, oy = 1 + (c / gw) * CH;
        for (int d = 0; d < 4; d++) {
            if (!e->door[d]) continue;
            int x0, y0, x1, y1;
            switch (d) {
            case D_N: x0 = CW / 2 - 1; y0 = 0; x1 = CW / 2; y1 = 0; break;
            case D_S: x0 = CW / 2 - 1; y0 = CH - 1; x1 = CW / 2; y1 = CH - 1; break;
            case D_W: x0 = 0; y0 = CH / 2 - 1; x1 = 0; y1 = CH / 2; break;
            default: x0 = CW - 1; y0 = CH / 2 - 1; x1 = CW - 1; y1 = CH / 2; break;
            }
            grid[oy + y0][ox + x0] = e->door[d];
            grid[oy + y1][ox + x1] = e->door[d];
        }
    }

    /* 11. laser gates across some of the gaps between two rooms (the side of the gap without a door) */
    int gates = 0;
    float gate_p = clampf((0.08f + 0.07f * s->depth) * s->traps, 0, 0.7f);
    for (int c = 0; c < n; c++) {
        if (!cell[c].used || cell[c].role == ROLE_VAULT || (cell[c].role == ROLE_START && s->depth < 2)) continue;
        for (int d = 0; d < 4; d++) {
            int v = nbr(c, d);
            if (v < c || !((cell[c].links >> d) & 1) || rng_f(rng) >= gate_p) continue;
            if (cell[v].role == ROLE_VAULT || (cell[v].role == ROLE_START && s->depth < 2)) continue;
            for (int side = 0; side < 2; side++) {
                int cc = side ? v : c, dd = side ? (d + 2) & 3 : d;
                if (cell[cc].door[dd]) continue;
                int ox = 1 + (cc % gw) * CW, oy = 1 + (cc / gw) * CH, gx, gy, ax, ay;
                switch (dd) {
                case D_N: gx = ox + CW / 2 - 1; gy = oy; ax = gx + 1; ay = gy; break;
                case D_S: gx = ox + CW / 2 - 1; gy = oy + CH - 1; ax = gx + 1; ay = gy; break;
                case D_W: gx = ox; gy = oy + CH / 2 - 1; ax = gx; ay = gy + 1; break;
                default: gx = ox + CW - 1; gy = oy + CH / 2 - 1; ax = gx; ay = gy + 1; break;
                }
                if (grid[gy][gx] != '.' || grid[ay][ax] != '.') continue;
                grid[gy][gx] = '=';
                gates++;
                break;
            }
        }
    }

    /* 12. features: keys, hostages, weapons, the vault prizes */
    memset(info, 0, sizeof(*info));
    info->rooms = cnt;
    info->vaults = vaults;
    for (int c = 0; c < n; c++)
        if (cell[c].used && cell[c].key >= 0 && place_feature(rng, c, "123"[cell[c].key])) info->keys++;
    int deep_vault = -1;
    for (int c = 0, dv = -1; c < n; c++)
        if (cell[c].used && cell[c].role == ROLE_VAULT && score_vault(c) > dv) { dv = score_vault(c); deep_vault = c; }
    if (deep_vault >= 0) {
        if (s->beacon) place_feature(rng, deep_vault, 'U');
        if (s->crate) place_feature(rng, deep_vault, 'R');
    }
    for (int h = 0, tries = 0; h < s->hostages && tries < 60; tries++) {
        int c = order[rng_int(rng, cnt)];
        if (cell[c].role == ROLE_START || cell[c].role == ROLE_VAULT || cell[c].role == ROLE_REACTOR) continue;
        int xs[4], ys[4];
        if (tries < 30 && find_marks(c, 'H', xs, ys, 4) > 0) continue; /* spread them out */
        if (place_feature(rng, c, 'H')) { h++; info->hostages++; }
    }
    if (s->armory) {
        static const char weapons[4] = {'V', 'N', 'J', 'F'};
        int w0 = rng_int(rng, 4), w1 = (w0 + 1 + rng_int(rng, 3)) % 4;
        char give[3] = {weapons[w0], weapons[w1], 'L'};
        for (int g = 0, tries = 0; g < 3 && tries < 40; tries++) {
            int c = order[rng_int(rng, cnt)];
            if (cell[c].region > 0 || cell[c].role == ROLE_VAULT) continue;
            if (place_feature(rng, c, give[g])) g++;
        }
    }
    /* a laser upgrade somewhere early, now and then */
    if (rng_f(rng) < 0.6f - 0.06f * s->depth) {
        for (int tries = 0; tries < 20; tries++) {
            int c = order[rng_int(rng, cnt)];
            if (cell[c].region > 1 || cell[c].role == ROLE_VAULT || cell[c].role == ROLE_START) continue;
            if (place_feature(rng, c, 'L')) break;
        }
    }

    /* 13. robot generators, away from the start */
    int gens = 0;
    for (int tries = 0; tries < 40 && gens < s->generators; tries++) {
        int c = order[rng_int(rng, cnt)];
        if (cell[c].role == ROLE_START || cell[c].role == ROLE_VAULT || (cell[c].region == 0 && tries < 20)) continue;
        int xs[4], ys[4];
        if (find_marks(c, '&', xs, ys, 4) > 0) {
            grid[ys[0]][xs[0]] = 'G';
            gens++;
        }
    }
    info->generators = gens;

    /* 14. robots: the budget spread over the rooms, heavier behind the doors; the arena has its own fight */
    float wsum = 0, rwt[NCELL] = {0};
    static int xs[RW_MAX * RH_MAX], ys[RW_MAX * RH_MAX];
    for (int c = 0; c < n; c++) {
        if (!cell[c].used || cell[c].role == ROLE_START || cell[c].role == ROLE_MERGED) continue;
        int slots = find_marks(c, '?', xs, ys, RW_MAX * RH_MAX);
        rwt[c] = slots * (1.0f + 0.3f * cell[c].region) * (cell[c].role == ROLE_REACTOR ? (s->boss ? 0.3f : 0.6f) : 1.0f) *
                 (cell[c].role == ROLE_VAULT ? 0.5f : 1.0f);
        wsum += rwt[c];
    }
    for (int c = 0; c < n && wsum > 0; c++) {
        if (rwt[c] <= 0) continue;
        float left = s->budget * rwt[c] / wsum;
        int slots = find_marks(c, '?', xs, ys, RW_MAX * RH_MAX);
        for (int i = slots - 1; i > 0; i--) {
            int j = rng_int(rng, i + 1);
            int tx = xs[i], ty = ys[i];
            xs[i] = xs[j]; ys[i] = ys[j]; xs[j] = tx; ys[j] = ty;
        }
        for (int i = 0; i < slots && left > 0.5f; i++) {
            const Roster *e = pick_robot(rng, s->zone, s->depth, left + 0.4f);
            if (!e) break;
            grid[ys[i]][xs[i]] = e->ch;
            left -= e->cost;
            info->robots++;
        }
    }

    /* 15. loot, then clear the remaining markers */
    int traps = gates;
    for (int y = 0; y < mh; y++)
        for (int x = 0; x < mw; x++) {
            char *ch = &grid[y][x];
            switch (*ch) {
            case '$': {
                float r = rng_f(rng);
                *ch = r < 0.3f ? '$' : r < 0.48f ? minor_loot(rng, s->zone) : '.';
            } break;
            case ';': {
                float r = rng_f(rng);
                *ch = r < 0.55f ? '$' : r < 0.8f ? rare_loot(rng, s->zone) : '.';
            } break;
            case '?': case '%': case '&': *ch = '.'; break;
            case '~': case 'O': case 'Y': traps++; break;
            default: break;
            }
        }
    /* the vault's own loot is always salvage */
    for (int c = 0; c < n; c++) {
        if (!cell[c].used || cell[c].role != ROLE_VAULT) continue;
        int x0, y0, x1, y1;
        room_rect(c, &x0, &y0, &x1, &y1);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                char *ch = &grid[y][x];
                if (*ch == '+' || *ch == '*' || *ch == 'm' || *ch == 'o' || *ch == 'p') *ch = '$';
            }
    }
    info->traps = traps;
    return true;
}

int mapgen_build(const MapSpec *s, const char ***rows, MapInfo *info) {
    gw = clampi(s->gw, 2, GW_MAX);
    gh = clampi(s->gh, 2, GH_MAX);
    Rng rng = rng_make(s->seed, 0x4D494E45u);
    bool ok = false;
    for (int attempt = 0; attempt < 60 && !ok; attempt++) ok = try_build(s, &rng, info);
    if (!ok) {
        /* never happens with sane specs; fall back to the smallest mine that always works */
        MapSpec t = *s;
        t.rooms = 5;
        t.keys = 1;
        t.vaults = 0;
        gw = gh = 4;
        while (!try_build(&t, &rng, info)) {}
    }
    /* crop the rock nobody will ever see, keeping a border of two tiles */
    int x0 = mw, y0 = mh, x1 = -1, y1 = -1;
    for (int y = 0; y < mh; y++)
        for (int x = 0; x < mw; x++)
            if (grid[y][x] != '#') {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    x0 = x0 > 2 ? x0 - 2 : 0;
    y0 = y0 > 2 ? y0 - 2 : 0;
    x1 = x1 + 2 < mw - 1 ? x1 + 2 : mw - 1;
    y1 = y1 + 2 < mh - 1 ? y1 + 2 : mh - 1;
    int nw = x1 - x0 + 1, nh = y1 - y0 + 1;
    for (int y = 0; y < nh; y++) {
        memmove(grid[y], grid[y + y0] + x0, (size_t)nw);
        grid[y][nw] = 0;
    }
    mw = nw;
    mh = nh;
    for (int y = 0; y < mh; y++) rowptr[y] = grid[y];
    *rows = rowptr;
    return mh;
}
