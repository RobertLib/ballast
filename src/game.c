/*
 * Core gameplay: level setup, the player ship, weapons, projectiles,
 * powerups, hostages, doors, the self-destruct countdown and world rendering.
 */
#include "game_internal.h"

GameState G;
World W;
Input g_in;
Config g_cfg = {0.7f, 0.8f, false, 2, 0, true, true};

const char *DIFF_NAMES[5] = {"TRAINEE", "ROOKIE", "HOTSHOT", "ACE", "INSANE"};
const char *PRIMARY_NAMES[PW_COUNT] = {"LASER", "VULCAN", "SPREADFIRE", "PLASMA", "FUSION"};
const char *SECONDARY_NAMES[SW_COUNT] = {"CONCUSSION", "HOMING", "PROXIMITY", "SMART", "MEGA"};

const float DIFF_DMG[5] = {0.45f, 0.7f, 1.0f, 1.3f, 1.6f};
const float DIFF_FIRE[5] = {0.6f, 0.8f, 1.0f, 1.2f, 1.45f};
const float DIFF_PSPD[5] = {0.8f, 0.9f, 1.0f, 1.1f, 1.22f};
const float DIFF_LEAD[5] = {0.0f, 0.15f, 0.45f, 0.75f, 0.95f};
const float DIFF_SPREAD[5] = {0.14f, 0.1f, 0.07f, 0.045f, 0.03f};
const float DIFF_HP[5] = {0.7f, 0.85f, 1.0f, 1.15f, 1.3f};
static const float DIFF_COUNTDOWN[5] = {50, 45, 40, 35, 30};
static const int DIFF_START_CONC[5] = {7, 6, 5, 4, 3};
static const float DIFF_ORB[5] = {30, 26, 22, 19, 16};
static const int MAX_MISSILES[SW_COUNT] = {20, 10, 10, 6, 5};
static int max_missiles(int s) { return (int)(MAX_MISSILES[s] * mod_missile_cap() + 0.5f); }
int game_missile_cap(int s) { return max_missiles(clampi(s, 0, SW_COUNT - 1)); }
int g_proj_src = DS_UNKNOWN;
static int hit_proj = -1; /* the projectile type behind the next player_damage, for the death recap */
/* detonator rounds: wrecks waiting to explode, a beat apart so the chain can be seen */
static V2 det_pos[32];
static float det_t[32];
static int ndet = 0;
static const float LASER_DMG[5] = {0, 10, 12.5f, 15, 18};
static const Col LASER_COL[5] = {{1, 1, 1, 1}, {1, 0.3f, 0.25f, 1}, {0.8f, 0.35f, 1, 1}, {0.35f, 0.65f, 1, 1}, {0.35f, 1, 0.45f, 1}};

Shape SHIP_SHAPE;

typedef struct { float radius, splash, hp; Col col; } ProjInfo;
static const ProjInfo PINFO[PR_TYPES] = {
    [PR_LASER] = {4, 0, 0, {1, 0.3f, 0.25f, 1}},
    [PR_VULCAN] = {3, 0, 0, {1, 0.9f, 0.45f, 1}},
    [PR_SPREAD] = {6, 0, 0, {0.35f, 0.6f, 1, 1}},
    [PR_PLASMA] = {6, 0, 0, {0.3f, 1, 0.55f, 1}},
    [PR_FUSION] = {12, 0, 0, {0.75f, 0.4f, 1, 1}},
    [PR_CONCUSSION] = {5, 80, 0, {1, 0.6f, 0.2f, 1}},
    [PR_HOMING] = {5, 80, 0, {0.4f, 1, 0.8f, 1}},
    [PR_PROX] = {8, 100, 0, {1, 0.3f, 0.3f, 1}},
    [PR_SMART] = {6, 90, 0, {1, 0.4f, 1, 1}},
    [PR_BLOB] = {6, 34, 0, {1, 0.5f, 0.9f, 1}},
    [PR_MEGA] = {9, 220, 0, {1, 0.35f, 0.2f, 1}},
    [PR_CARGO] = {11, 0, 0, {1, 0.62f, 0.2f, 1}},
    [EP_PULSE] = {6, 0, 0, {1, 0.45f, 0.2f, 1}},
    [EP_BOLT] = {4, 0, 0, {0.3f, 0.95f, 1, 1}},
    [EP_NEEDLE] = {3, 0, 0, {1, 0.6f, 0.95f, 1}},
    [EP_VULCAN] = {3, 0, 0, {1, 0.8f, 0.3f, 1}},
    [EP_MISSILE] = {6, 55, 10, {1, 0.35f, 0.25f, 1}},
    [EP_HOMING] = {6, 60, 12, {1, 0.3f, 0.9f, 1}},
    [EP_MINE] = {9, 65, 8, {1, 0.9f, 0.2f, 1}},
    [EP_ORB] = {9, 0, 6, {1, 0.25f, 0.45f, 1}},
    [EP_REACTOR] = {6, 0, 0, {0.8f, 0.6f, 1, 1}},
    [EP_PELLET] = {5, 0, 0, {1, 0.4f, 0.6f, 1}},
    [EP_SHARD] = {4, 0, 0, {1, 0.7f, 0.3f, 1}},
    [EP_FLAK] = {11, 0, 8, {1, 0.7f, 0.25f, 1}},
    [EP_SEEKER] = {9, 0, 12, {1, 0.3f, 0.5f, 1}},
};

const LevelDef *cur_def(void) { return run_level_def(); }

#define BASE_ZOOM 0.88f
#define ARENA_ZOOM 0.74f
/* the eye trails the camera's motion off straight overhead: the walls lean into it and show their sides */
#define LEAN_LAG 0.075f  /* eye offset per unit of camera speed */
#define LEAN_MAX 46.0f
#define WALL_LEAN 0.4f   /* how far the tops of the walls shift per unit of eye offset */

bool in_view(V2 p, float margin) {
    V2 v = w2v(p);
    return v.x > -margin && v.x < g_virt_w + margin && v.y > -margin && v.y < VIRT_H + margin;
}
const char *game_robot_name(int type) { return RDEF[type].name; }

Col key_color(int lock) {
    switch (lock) {
    case LOCK_BLUE: return rgba(0.3f, 0.55f, 1.0f, 1);
    case LOCK_YELLOW: return rgba(1.0f, 0.85f, 0.2f, 1);
    case LOCK_RED: return rgba(1.0f, 0.25f, 0.25f, 1);
    case LOCK_EXIT: return G.reactor_dead ? rgba(0.3f, 1.0f, 0.4f, 1) : rgba(1.0f, 0.3f, 0.3f, 1);
    case LOCK_VAULT: return G.reactor_dead ? rgba(1.0f, 0.75f, 0.25f, 1) : rgba(0.75f, 0.45f, 0.18f, 1);
    default: return cur_def()->accent;
    }
}
static int lock_bit(int lock) { return lock == LOCK_BLUE ? 1 : lock == LOCK_YELLOW ? 2 : lock == LOCK_RED ? 4 : 0; }

static void build_ship_shape(void) {
    Shape *s = &SHIP_SHAPE;
    memset(s, 0, sizeof(*s));
    static const V2 hull[10] = {{1.3f, 0}, {0.15f, 0.32f}, {-0.35f, 0.95f}, {-0.85f, 0.95f}, {-0.55f, 0.32f},
                                {-0.85f, 0}, {-0.55f, -0.32f}, {-0.85f, -0.95f}, {-0.35f, -0.95f}, {0.15f, -0.32f}};
    s->s[0].n = 10;
    s->s[0].closed = true;
    memcpy(s->s[0].p, hull, sizeof(hull));
    s->s[1].n = 3;
    s->s[1].closed = true;
    s->s[1].p[0] = v2(0.72f, 0);
    s->s[1].p[1] = v2(0.2f, 0.14f);
    s->s[1].p[2] = v2(0.2f, -0.14f);
    s->s[2].n = 2;
    s->s[2].p[0] = v2(-0.2f, 0.72f);
    s->s[2].p[1] = v2(0.45f, 0.72f);
    s->s[3].n = 2;
    s->s[3].p[0] = v2(-0.2f, -0.72f);
    s->s[3].p[1] = v2(0.45f, -0.72f);
    s->ns = 4;
}

void game_draw_ship_icon(V2 pos, float ang, float scale, Col c) { r_shape(&SHIP_SHAPE, pos, ang, scale, maxf(2.5f, scale * 0.3f), c); }

/* ------------------------------------------------------------ score */
void game_add_score(int pts) {
    /* threat protocols pay 10% more score per heat */
    G.score += pts * (10 + run_heat()) / 10;
    while (G.score >= G.next_life) {
        G.lives++;
        G.next_life += 50000;
        hud_msg("EXTRA LIFE!", C_GREEN);
        snd_play(SND_EXTRALIFE, 0.7f, 1.0f);
    }
}

void chain_kill(V2 pos, int points) {
    G.chain++;
    G.chain_t = 3.2f;
    int mult = 1 + (G.chain - 1 < 28 ? G.chain - 1 : 28) / 4;
    if (mult > R.best_chain) R.best_chain = mult;
    if (mult >= 8) profile_complete(CH_CHAIN);
    int pts = points * mult;
    game_add_score(pts);
    char buf[24];
    if (mult > 1) snprintf(buf, sizeof(buf), "%d X%d", points, mult);
    else snprintf(buf, sizeof(buf), "%d", points);
    fx_popup(pos, buf, mult > 1 ? col_lerp(C_YELLOW, C_ORANGE, clampf((mult - 2) / 6.0f, 0, 1)) : rgba(0.9f, 0.95f, 1, 1), 13 + mult);
}

/* ------------------------------------------------------------ loadout */
static void reset_loadout(void) {
    Player *p = &W.pl;
    p->shield = mod_start_shield();
    p->energy = mod_start_energy();
    p->burner = 1;
    p->primary = PW_LASER;
    p->special = -1;
    p->laser_level = mod_base_laser();
    p->quad = mod_on(MOD_QUAD);
    p->secondary = SW_CONCUSSION;
    p->missiles = clampi(DIFF_START_CONC[G.difficulty] + mod_start_missiles(), 0, max_missiles(SW_CONCUSSION));
    p->cargo = 0;
    p->mass = 1;
}

/* per-ship state of the upgrade modules */
static void reset_modules(void) {
    Player *p = &W.pl;
    p->calm_t = 0;
    p->reactive_cd = 0;
    p->phase_t = p->phase_cd = 0;
    p->was_burning = false;
    p->drone_cd = 1.0f;
    p->jettison_cd = 0;
    p->swap_pu = -1;
    p->fab_kills = 0;
}

void game_new(int difficulty, int lives) {
    memset(&G, 0, sizeof(G));
    G.difficulty = clampi(difficulty, 0, 4);
    G.lives = lives;
    G.next_life = 50000;
    G.level_score_start = -1;
    memset(&W.pl, 0, sizeof(W.pl));
    reset_loadout();
}

void game_debug_loadout(void) {
    Player *p = &W.pl;
    p->special = PW_FUSION;
    p->laser_level = 4;
    p->quad = true;
    p->secondary = SW_HOMING;
    p->missiles = 10;
    p->keys = 7;
}

/* ------------------------------------------------------------ campaign save */
void game_save(FILE *f) {
    const Player *p = &W.pl;
    fprintf(f, "difficulty %d\nlives %d\nscore %d\nnext_life %d\nlevel_score %d\n", G.difficulty, G.lives, G.score, G.next_life,
            G.level_score_start);
    /* special comes before primary: game_load_line checks the primary against it */
    fprintf(f, "shield %.1f\nenergy %.1f\nspecial %d\nprimary %d\nlaser %d\nquad %d\nsecondary %d\nmissiles %d\n", p->shield, p->energy,
            p->special, p->primary, p->laser_level, p->quad, p->secondary, p->missiles);
}

bool game_load_line(const char *key, const char *val) {
    Player *p = &W.pl;
    int v = atoi(val);
    if (!strcmp(key, "difficulty")) G.difficulty = clampi(v, 0, 4);
    else if (!strcmp(key, "lives")) G.lives = clampi(v, 0, 99);
    else if (!strcmp(key, "score")) G.score = v > 0 ? v : 0;
    else if (!strcmp(key, "next_life")) G.next_life = v > 0 ? v : 50000;
    else if (!strcmp(key, "level_score")) G.level_score_start = v; /* -1: the first mine still launches a fresh ship */
    else if (!strcmp(key, "shield")) p->shield = maxf(0, (float)atof(val));
    else if (!strcmp(key, "energy")) p->energy = maxf(0, (float)atof(val));
    else if (!strcmp(key, "special")) p->special = v > PW_LASER && v < PW_COUNT ? v : -1;
    else if (!strcmp(key, "primary")) p->primary = v == p->special && v >= 0 ? v : PW_LASER;
    else if (!strcmp(key, "laser")) p->laser_level = clampi(v, 1, 4);
    else if (!strcmp(key, "quad")) p->quad = v != 0;
    else if (!strcmp(key, "secondary")) p->secondary = clampi(v, -1, SW_COUNT - 1);
    else if (!strcmp(key, "missiles")) p->missiles = v > 0 ? v : 0;
    else return false;
    return true;
}

void game_shutdown_level(void) {
    Player *p = &W.pl;
    if (p->charge_voice) snd_stop(p->charge_voice);
    if (p->burn_voice) snd_stop(p->burn_voice);
    p->charge_voice = p->burn_voice = 0;
}

void game_audio_pause(bool paused) { audio_pause_loops(paused); }

static bool shape_ready = false;

void game_init(void) {
    if (shape_ready) return;
    build_ship_shape();
    robots_init_shapes();
    shape_ready = true;
}

void game_start_level(bool fresh_ship) {
    game_init();
    game_shutdown_level();
    G.level = R.layer;
    const LevelDef *d = cur_def();
    level_build_from_ascii(d->map, d->rows);
    float tier = run_tier();

    Player keep = W.pl;
    memset(&W, 0, sizeof(W));
    W.pl = keep;
    W.boss_idx = -1;
    W.blast_kills = -1;
    W.last_src = DS_UNKNOWN;
    W.last_proj = -1;
    ndet = 0;
    if (fresh_ship) reset_loadout();
    else {
        /* the ship carries over, but never launches below what its modules provide */
        W.pl.shield = maxf(W.pl.shield, mod_start_shield());
        W.pl.energy = maxf(W.pl.energy, mod_start_energy());
        W.pl.laser_level = clampi(W.pl.laser_level, mod_base_laser(), 4);
        if (mod_on(MOD_QUAD)) W.pl.quad = true;
        W.pl.cargo = 0;
    }
    reset_modules();
    if (mod_on(MOD_MAPPER)) memset(L.explored, 1, sizeof(L.explored));
    Player *p = &W.pl;
    p->keys = 0;
    p->vel = v2(0, 0);
    p->ang = -PI / 2;
    p->radius = 14;
    p->cloak_t = p->invuln_t = 0;
    p->spawn_inv = 2.0f;
    p->dead = false;
    p->charging = false;
    p->burner = 1;
    p->hit_flash = 0;
    p->mass = 1;

    grid_init(L.w * TILE, L.h * TILE, 32);
    fx_clear();
    hud_msg_clear();

    G.reactor_dead = false;
    G.countdown = 0;
    G.escaping = G.failing = false;
    G.escape_t = G.fail_t = 0;
    G.hostages_total = 0;
    G.hostages_onboard = 0;
    G.hostages_lost = 0;
    G.hostages_saved = 0;
    G.robots_killed = 0;
    G.robots_total = 0;
    G.level_time = 0;
    G.chain = 0;
    G.chain_t = 0;
    G.automap = false;
    G.automap_zoom = 1;
    G.automap_pan = v2(0, 0);
    G.result = GR_NONE;
    G.level_score_start = G.score;
    G.cargo_banked = G.cargo_bonus = 0;
    G.escape_margin = 0;
    G.ram_kills = G.best_bomb = 0;
    G.boss_level = false;
    int hazard = run_hazard();
    const SectorNode *node = run_cur_node();
    W.seed = node ? node->seed : 0;
    backdrop_init(W.seed ^ (uint32_t)(G.level * 0x9E3779B9u), node ? node->zone : 0, L.w * TILE, L.h * TILE);
    float elite_p = clampf(0.03f + 0.05f * G.level + 0.03f * run_protocol(TP_ELITE), 0, 0.4f);

    V2 exit_sum = v2(0, 0);
    int exit_n = 0;
    static V2 treasure[32];
    int ntreasure = 0;
    for (int y = 0; y < d->rows && y < L.h; y++) {
        const char *row = d->map[y];
        int len = (int)strlen(row);
        for (int x = 0; x < len && x < L.w; x++) {
            char c = row[x];
            V2 pos = tile_center(x, y);
            switch (c) {
            case 'S': p->start = pos; break;
            case 'H':
                if (W.nhost < MAX_HOSTAGES) {
                    W.host[W.nhost].active = true;
                    W.host[W.nhost].pos = pos;
                    W.host[W.nhost].t = frand() * 10;
                    W.nhost++;
                    G.hostages_total++;
                }
                break;
            case '1': spawn_powerup(PU_KEY_BLUE, pos, v2(0, 0), 1); W.level_keys |= 1; break;
            case '2': spawn_powerup(PU_KEY_YELLOW, pos, v2(0, 0), 1); W.level_keys |= 2; break;
            case '3': spawn_powerup(PU_KEY_RED, pos, v2(0, 0), 1); W.level_keys |= 4; break;
            case 'C': reactor_init(pos); break;
            case 'W': {
                Robot *r = robot_spawn(RB_BOSS, pos, false);
                if (r) { W.boss_idx = (int)(r - W.rob); G.boss_level = true; }
            } break;
            case 'G':
                if (W.nmat < MAX_MATCENS) {
                    Matcen *m = &W.mat[W.nmat++];
                    m->pos = pos;
                    m->max = 3 + G.difficulty / 2 + (int)tier + (run_protocol(TP_SWARM) ? 2 : 0) + (hazard == HZ_OVERCLOCK ? 3 : 0);
                    m->timer = 1.0f;
                    if (hazard == HZ_OVERCLOCK) m->triggered = true;
                }
                break;
            case 'd': robot_spawn(RB_DRONE, pos, false); break;
            case 'l': robot_spawn(RB_LIFTER, pos, false); break;
            case 't': robot_spawn(RB_TURRET, pos, false); break;
            case 'h': robot_spawn(RB_HULK, pos, false); break;
            case 's': robot_spawn(RB_SPIDER, pos, false); break;
            case 'g': robot_spawn(RB_GOPHER, pos, false); break;
            case 'v': robot_spawn(RB_DRILLER, pos, false); break;
            case 'u': robot_spawn(RB_SUPERHULK, pos, false); break;
            case 'c': robot_spawn(RB_CLOAKER, pos, false); break;
            case 'a': robot_spawn(RB_WASP, pos, false); break;
            case 'P': robot_spawn(RB_PULSAR, pos, false); break;
            case 'k': robot_spawn(RB_LANCER, pos, false); break;
            case 'B': robot_spawn(RB_BOMBER, pos, false); break;
            case 'q': robot_spawn(RB_CARRIER, pos, false); break;
            case '~': trap_add(TR_VENT, x, y); break;
            case '=': trap_add(TR_GATE, x, y); break;
            case 'O': trap_add(TR_SWEEPER, x, y); break;
            case 'Y': trap_add(TR_WELL, x, y); break;
            case 'n': {
                g_proj_src = DS_TRAP;
                Proj *m = spawn_proj(EP_MINE, pos, v2(0, 0), 16, 1e9f);
                if (m) m->age = 1;
                g_proj_src = DS_UNKNOWN;
            } break;
            case '+': spawn_powerup(PU_SHIELD, pos, v2(0, 0), 1); break;
            case '*': case 'A': spawn_powerup(PU_ENERGY, pos, v2(0, 0), 1); break;
            case 'L': spawn_powerup(PU_LASER, pos, v2(0, 0), 1); break;
            case 'Q': spawn_powerup(PU_QUAD, pos, v2(0, 0), 1); break;
            case 'V': spawn_powerup(PU_VULCAN, pos, v2(0, 0), 1); break;
            case 'N': spawn_powerup(PU_SPREAD, pos, v2(0, 0), 1); break;
            case 'J': spawn_powerup(PU_PLASMA, pos, v2(0, 0), 1); break;
            case 'F': spawn_powerup(PU_FUSION, pos, v2(0, 0), 1); break;
            case 'm': spawn_powerup(PU_CONC, pos, v2(0, 0), 4); break;
            case 'o': spawn_powerup(PU_HOMING, pos, v2(0, 0), 3); break;
            case 'p': spawn_powerup(PU_PROX, pos, v2(0, 0), 4); break;
            case 'i': spawn_powerup(PU_SMART, pos, v2(0, 0), 2); break;
            case 'M': spawn_powerup(PU_MEGA, pos, v2(0, 0), 1); break;
            case 'I': spawn_powerup(PU_INVULN, pos, v2(0, 0), 1); break;
            case 'K': spawn_powerup(PU_CLOAK, pos, v2(0, 0), 1); break;
            case 'U': spawn_powerup(PU_LIFE, pos, v2(0, 0), 1); break;
            case 'R': spawn_powerup(PU_CRATE, pos, v2(0, 0), 1); break;
            case '$': {
                /* a salvage cache: one big shard */
                spawn_powerup(PU_SALVAGE, pos, v2(0, 0), (int)((9 + 3 * tier) * run_salvage_mult() * mod_salvage_mult() + 0.5f));
            } break;
            case '@': if (ntreasure < 32) treasure[ntreasure++] = pos; break;
            case 'Z': exit_sum = v2add(exit_sum, pos); exit_n++; break;
            default: break;
            }
        }
    }
    if (exit_n) W.exit_pos = v2scale(exit_sum, 1.0f / exit_n);
    for (int i = 0; i < L.ndoors; i++) W.nvaults += L.doors[i].lock == LOCK_VAULT;
    /* vault treasure pays by the distance to the exit: the deeper, the richer */
    if (ntreasure) {
        int xs[64], ys[64], n = 0;
        for (int y = 0; y < L.h; y++)
            for (int x = 0; x < L.w; x++)
                if ((L.flags[y][x] & TF_EXIT) && n < 64) { xs[n] = x; ys[n] = y; n++; }
        flow_compute_multi(L.exitflow, xs, ys, n, true);
        for (int i = 0; i < ntreasure; i++) {
            int tx = tx_of(treasure[i].x), ty = tx_of(treasure[i].y);
            int dist = L.exitflow[ty][tx] < 60000 ? L.exitflow[ty][tx] : 40;
            int amount = (int)((25 + dist * 0.9f) * (1 + 0.15f * tier) * run_salvage_mult() * mod_salvage_mult() + 0.5f);
            spawn_powerup(PU_TREASURE, treasure[i], v2(0, 0), amount);
        }
    }
    p->pos = p->start;
    for (int i = 0; i < MAX_ROBOTS; i++) {
        Robot *r = &W.rob[i];
        if (!r->active) continue;
        G.robots_total++;
        /* elites: the same ones for everyone who flies this seed */
        int tx = tx_of(r->pos.x), ty = tx_of(r->pos.y);
        uint32_t h = hash32(W.seed ^ (uint32_t)tx * 73856093u ^ (uint32_t)ty * 19349663u ^ 0xE11Eu);
        if (r->type != RB_BOSS && r->type != RB_TURRET && (h >> 8) * (1.0f / 16777216.0f) < elite_p) {
            r->elite = true;
            r->maxhp = r->hp = r->maxhp * 1.8f;
            r->radius *= 1.15f;
        }
    }

    flow_compute(L.flow, tx_of(p->pos.x), tx_of(p->pos.y), false);
    explore_update(p->pos, 420);
    g_cam.pos = p->pos;
    g_cam.lean = v2(0, 0);
    g_cam.zoom = 0.6f;
    W.zoom_target = BASE_ZOOM;
    W.time_scale = 1.0f;
    g_trauma = 0;
    p->burn_voice = snd_loop(SND_BURNER, 0.0f, 1.0f);
    music_play(d->song);

    char buf[96];
    snprintf(buf, sizeof(buf), "SECTOR %d: %s", G.level + 1, d->name);
    hud_msg(buf, d->accent);
    if (hazard) {
        snprintf(buf, sizeof(buf), "HAZARD: %s", hazard_name(hazard));
        hud_msg(buf, C_ORANGE);
    }
    fx_ring(p->pos, 10, 90, rgba(0.6f, 0.9f, 1, 1), 0.8f, 6);
}

/* ------------------------------------------------------------ projectiles */
Proj *spawn_proj(int type, V2 pos, V2 vel, float dmg, float life) {
    for (int k = 0; k < MAX_PROJ; k++) {
        int i = (W.proj_next + k) % MAX_PROJ;
        if (W.proj[i].active) continue;
        W.proj_next = (i + 1) % MAX_PROJ;
        Proj *p = &W.proj[i];
        memset(p, 0, sizeof(*p));
        p->active = true;
        p->type = type;
        p->pos = pos;
        p->vel = vel;
        p->dmg = dmg;
        p->life = life;
        p->radius = PINFO[type].radius;
        p->splash = PINFO[type].splash;
        p->hp = PINFO[type].hp;
        p->col = PINFO[type].col;
        p->target = -1;
        p->src = IS_ENEMY_PROJ(type) ? g_proj_src : DS_SELF;
        return p;
    }
    return NULL;
}

static void break_tile(int x, int y) {
    if (!tile_in(x, y) || L.tile[y][x] != T_BREAK) return;
    L.tile[y][x] = T_EMPTY;
    V2 c = tile_center(x, y);
    Col ac = cur_def()->accent;
    for (int i = 0; i < 26; i++) {
        V2 p = v2add(c, v2(frandr(-20, 20), frandr(-20, 20)));
        fx_debris(p, v2scale(v2fromang(frand() * TAU), 60 + frand() * 220), frand() * TAU, frandr(-8, 8), 4 + frand() * 8,
                  col_a(ac, 0.9f), 0.8f + frand() * 0.8f);
    }
    fx_explosion(c, 30, ac);
    snd_play_at(SND_BREAK, c, 0.9f, 1.0f);
    shake_add(0.25f);
    level_rebuild_geometry();
    flow_compute(L.flow, tx_of(W.pl.pos.x), tx_of(W.pl.pos.y), false);
    spawn_salvage(c, 12, 220);
    if (W.time >= W.secret_msg_next) {
        hud_msg("SECRET PASSAGE OPENED!", ac);
        W.secret_msg_next = W.time + 3;
    }
}

static void damage_tile(int tile, float dmg) {
    int x = tile % L.w, y = tile / L.w;
    if (!tile_in(x, y) || L.tile[y][x] != T_BREAK) return;
    L.break_hp[y][x] -= dmg;
    fx_burst(v2add(tile_center(x, y), v2(frandr(-18, 18), frandr(-18, 18))), 4, cur_def()->accent, 180, 0.3f, 3);
    if (L.break_hp[y][x] <= 0) break_tile(x, y);
}

void explode(V2 pos, float radius, float dmg, bool by_player, int exclude, Col c, int src) {
    fx_explosion(pos, radius * 0.45f, c);
    int snd = radius < 90 ? SND_EXPL_S : radius < 170 ? SND_EXPL_M : SND_EXPL_L;
    snd_play_at(snd, pos, 0.85f, frandr(0.9f, 1.1f));
    float pd = v2dist(pos, W.pl.pos);
    shake_add(clampf(radius / 220.0f, 0.08f, 0.9f) * clampf(1 - pd / 900, 0, 1));
    for (int i = 0; i < MAX_ROBOTS; i++) {
        Robot *r = &W.rob[i];
        if (!r->active || i == exclude) continue;
        float d = v2dist(r->pos, pos) - r->radius;
        if (d >= radius) continue;
        if (!los(pos, r->pos) && d > 10) continue;
        float f = 1 - maxf(0, d) / radius;
        robot_damage(r, dmg * 0.7f * f, v2norm(v2sub(r->pos, pos)), by_player);
    }
    if (W.reactor.exists && !W.reactor.dead && by_player) {
        float d = v2dist(W.reactor.pos, pos) - REACTOR_R;
        if (d < radius) reactor_damage(dmg * 0.7f * (1 - maxf(0, d) / radius));
    }
    if (!W.pl.dead && !G.escaping && src != DS_SAFE) {
        float d = pd - W.pl.radius;
        if (d < radius && (los(pos, W.pl.pos) || d < 8)) {
            float f = 1 - maxf(0, d) / radius;
            float k = by_player ? 0.35f : 1.0f;
            player_damage(dmg * f * k, v2norm(v2sub(W.pl.pos, pos)), by_player ? DS_SELF : src);
        }
    }
    /* chain reaction: shootable projectiles in the blast detonate shortly after */
    for (int i = 0; i < MAX_PROJ; i++) {
        Proj *p = &W.proj[i];
        if (!p->active || p->splash <= 0) continue;
        if (p->type != EP_MINE && p->type != PR_PROX && p->type != EP_MISSILE && p->type != EP_HOMING) continue;
        if (v2dist(p->pos, pos) < radius * 0.8f && p->life > 0.12f) {
            p->life = 0.06f + frand() * 0.1f;
            p->armed = true;
        }
    }
    int x0 = tx_of(pos.x - radius), x1 = tx_of(pos.x + radius), y0 = tx_of(pos.y - radius), y1 = tx_of(pos.y + radius);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            if (!tile_in(x, y) || L.tile[y][x] != T_BREAK) continue;
            float d = v2dist(tile_center(x, y), pos) - TILE * 0.5f;
            if (d < radius) damage_tile(y * L.w + x, dmg * (1 - maxf(0, d) / radius));
        }
    grid_impulse(pos, radius * 2.2f, radius * 3.2f);
    hit_proj = -1;
}

/* a jettisoned cargo bomb: the more salvage went into it, the bigger the bang */
static void cargo_blast(const Proj *pr, V2 at) {
    float amt = (float)pr->cargo;
    bool shrap = mod_on(MOD_SHRAPNEL);
    float radius = (80 + sqrtf(amt) * 9) * (shrap ? 1.3f : 1.0f);
    float dmg = 30 + amt * 0.5f;
    W.blast_kills = 0;
    explode(at, radius, dmg, true, -1, rgba(1, 0.62f, 0.2f, 1), DS_SELF);
    int kills = W.blast_kills;
    W.blast_kills = -1;
    if (kills > G.best_bomb) G.best_bomb = kills;
    if (kills > R.best_bomb) R.best_bomb = kills;
    if (kills >= 3) profile_complete(CH_DEMOLITION);
    if (kills >= 2) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d KILLS!", kills);
        fx_popup(at, buf, C_ORANGE, 16 + kills * 2);
    }
    fx_explosion(at, radius * 0.3f, C_YELLOW);
    fx_ring(at, 20, radius, rgba(1, 0.7f, 0.3f, 1), 0.5f, 8);
    for (int i = 0; i < 30; i++)
        fx_debris(at, v2scale(v2fromang(frand() * TAU), 150 + frand() * 380), frand() * TAU, frandr(-9, 9), 3 + frand() * 5,
                  rgba(1, 0.7f, 0.25f, 1), 0.8f + frand() * 0.8f);
    W.flash = maxf(W.flash, 0.35f);
    W.flash_col = rgba(1, 0.6f, 0.2f, 1);
    if (shrap) {
        for (int i = 0; i < 6; i++) {
            Proj *b = spawn_proj(PR_BLOB, at, v2scale(v2fromang(TAU * i / 6 + frand() * 0.4f), 420), 24, 2.5f);
            if (b) { b->turn = 5.5f; b->retarget = 0.04f * i; b->col = rgba(1, 0.7f, 0.3f, 1); }
        }
    }
}

/* Cluster Warheads: missiles burst into small seekers */
static void cluster_burst(const Proj *pr, V2 at) {
    if ((pr->type != PR_CONCUSSION && pr->type != PR_HOMING) || !mod_on(MOD_CLUSTER)) return;
    float a0 = v2ang(pr->vel);
    for (int i = 0; i < 3; i++) {
        V2 v = v2scale(v2fromang(a0 + PI + (i - 1) * 1.1f + frandr(-0.2f, 0.2f)), 360);
        Proj *b = spawn_proj(PR_BLOB, at, v, 16, 2.0f);
        if (b) { b->turn = 5.0f; b->retarget = 0.08f * i; b->col = rgba(1, 0.65f, 0.3f, 1); }
    }
}

static void proj_detonate(Proj *pr, V2 at) {
    pr->active = false;
    bool mine = IS_ENEMY_PROJ(pr->type);
    if (pr->type == PR_CARGO) {
        cargo_blast(pr, at);
        return;
    }
    if (pr->splash > 0) {
        hit_proj = pr->type;
        explode(at, pr->splash, pr->dmg, !mine, -1, pr->col, pr->src);
        cluster_burst(pr, at);
        if (pr->type == PR_SMART) {
            for (int i = 0; i < 6; i++) {
                V2 v = v2scale(v2fromang(TAU * i / 6 + frand() * 0.5f), 380);
                Proj *b = spawn_proj(PR_BLOB, at, v, 20, 2.5f);
                if (b) { b->turn = 5.0f; b->retarget = 0.05f * i; }
            }
        }
    } else {
        fx_burst(at, 8, pr->col, 220, 0.3f, 3);
        fx_glow(at, v2(0, 0), col_a(pr->col, 0.6f), 0.2f, 14);
    }
}

static void proj_hit_fx(Proj *pr, V2 at, V2 n) {
    for (int i = 0; i < 6; i++) {
        V2 d = v2norm(v2add(n, v2scale(v2fromang(frand() * TAU), 0.9f)));
        fx_spark(at, v2scale(d, 120 + frand() * 260), col_white(pr->col, 0.3f), 0.25f + frand() * 0.2f, 3);
    }
    fx_glow(at, v2(0, 0), col_a(pr->col, 0.5f), 0.18f, 12 + pr->radius);
    grid_impulse(at, 50, 60);
}

static float seg_point_dist(V2 a, V2 b, V2 p) {
    V2 ab = v2sub(b, a);
    float l2 = v2len2(ab);
    float t = l2 > 1e-6f ? clampf(v2dot(v2sub(p, a), ab) / l2, 0, 1) : 0;
    return v2dist(v2mad(a, ab, t), p);
}

/* beams and vents hurt in ticks, so a sweep across the ship is one hit, not a hundred */
void hazard_touch(float dps, float dt, V2 dir, int src) {
    W.hazard_acc += dps * dt;
    W.hazard_dir = dir;
    W.hazard_src = src;
}

bool player_hurt_by(V2 a, V2 b, float r) {
    const Player *p = &W.pl;
    if (p->dead || G.escaping || G.failing) return false;
    return seg_point_dist(a, b, p->pos) < r + p->radius * 0.6f;
}

bool player_visible_to(V2 from, float dist) {
    if (W.pl.dead || G.escaping) return false;
    if (W.pl.cloak_t > 0 && dist > 130) return false;
    return los(from, W.pl.pos);
}

static int find_homing_target(V2 pos, V2 dir, float maxd, float cone) {
    int best = -1;
    float bestscore = 1e9f;
    for (int i = 0; i < MAX_ROBOTS; i++) {
        Robot *r = &W.rob[i];
        if (!r->active) continue;
        if (r->type == RB_BOSS && r->invis_t > 0) continue;
        V2 to = v2sub(r->pos, pos);
        float d = v2len(to);
        if (d > maxd || d < 1) continue;
        float c = v2dot(v2scale(to, 1.0f / d), dir);
        if (c < cone) continue;
        float score = d * (2.0f - c);
        if (score < bestscore && los(pos, r->pos)) { bestscore = score; best = i; }
    }
    if (W.reactor.exists && !W.reactor.dead) {
        V2 to = v2sub(W.reactor.pos, pos);
        float d = v2len(to);
        if (d < maxd && d > 1 && v2dot(v2scale(to, 1.0f / d), dir) > cone) {
            float score = d * 1.5f;
            if (score < bestscore && los(pos, W.reactor.pos)) best = -2;
        }
    }
    return best;
}

static void steer_proj(Proj *pr, V2 target, float dt) {
    float sp = v2len(pr->vel);
    float a = v2ang(pr->vel);
    float want = v2ang(v2sub(target, pr->pos));
    a = approach_angle(a, want, pr->turn * dt);
    pr->vel = v2scale(v2fromang(a), sp);
}

static int shootable[512];
static int nshootable;

static void proj_update(Proj *pr, float dt) {
    pr->age += dt;
    pr->life -= dt;
    bool enemy = IS_ENEMY_PROJ(pr->type);
    if (pr->life <= 0) {
        if (pr->type == EP_FLAK || pr->type == EP_SEEKER) {
            pr->active = false;
            flak_pop(pr);
        } else if (pr->type == PR_CARGO || (pr->splash > 0 && (pr->armed || pr->type == PR_CONCUSSION || pr->type == PR_HOMING || pr->type == PR_SMART ||
                                                        pr->type == PR_MEGA || pr->type == EP_MISSILE || pr->type == EP_HOMING)))
            proj_detonate(pr, pr->pos);
        else {
            pr->active = false;
            fx_glow(pr->pos, v2(0, 0), col_a(pr->col, 0.4f), 0.2f, 10);
        }
        return;
    }
    /* bullet patterns: hang still, curve, speed up or slow down */
    if (pr->wait > 0) {
        pr->wait -= dt;
        pr->life += dt;
        if (pr->wait > 0) return;
        pr->vel = pr->launch;
    }
    if (pr->curve != 0) {
        pr->vel = v2rot(pr->vel, pr->curve * dt);
        pr->curve *= expf(-1.1f * dt);
    }
    if (pr->accel != 0) {
        float sp = v2len(pr->vel);
        float ns = clampf(sp + pr->accel * dt, pr->vmin, pr->vmax);
        if (sp > 1e-3f) pr->vel = v2scale(pr->vel, ns / sp);
    }
    /* guidance */
    switch (pr->type) {
    case PR_HOMING:
    case PR_BLOB:
    case PR_MEGA: {
        pr->retarget -= dt;
        if (pr->retarget <= 0) {
            pr->retarget = 0.2f;
            float cone = pr->type == PR_BLOB ? -0.2f : 0.35f;
            pr->target = find_homing_target(pr->pos, v2norm(pr->vel), pr->type == PR_BLOB ? 500 : 750, cone);
        }
        if (pr->target >= 0 && W.rob[pr->target].active) steer_proj(pr, W.rob[pr->target].pos, dt);
        else if (pr->target == -2 && W.reactor.exists && !W.reactor.dead) steer_proj(pr, W.reactor.pos, dt);
        if (pr->type == PR_MEGA) {
            float sp = v2len(pr->vel);
            if (sp < 560) pr->vel = v2scale(pr->vel, (sp + 260 * dt) / sp);
        }
    } break;
    case EP_HOMING:
    case EP_SEEKER:
        if (pr->age > 0.25f && player_visible_to(pr->pos, v2dist(pr->pos, W.pl.pos))) steer_proj(pr, W.pl.pos, dt);
        break;
    case PR_CARGO:
        pr->vel = v2scale(pr->vel, expf(-2.0f * dt));
        break;
    case PR_PROX:
    case EP_MINE: {
        pr->vel = v2scale(pr->vel, expf(-3.0f * dt));
        if (pr->age > 0.6f && !pr->armed) {
            if (pr->type == PR_PROX) {
                for (int i = 0; i < MAX_ROBOTS; i++)
                    if (W.rob[i].active && v2dist(W.rob[i].pos, pr->pos) < 64 + W.rob[i].radius) {
                        pr->armed = true;
                        pr->life = minf(pr->life, 0.18f);
                        snd_play_at(SND_PROX, pr->pos, 0.6f, 1.4f);
                        break;
                    }
            } else if (!W.pl.dead && v2dist(W.pl.pos, pr->pos) < 58) {
                pr->armed = true;
                pr->life = minf(pr->life, 0.25f);
                snd_play_at(SND_PROX, pr->pos, 0.7f, 1.0f);
            }
        }
    } break;
    default: break;
    }
    /* trails */
    if (pr->type == PR_CONCUSSION || pr->type == PR_HOMING || pr->type == PR_SMART || pr->type == PR_MEGA ||
        pr->type == EP_MISSILE || pr->type == EP_HOMING) {
        pr->trail -= dt;
        while (pr->trail <= 0) {
            pr->trail += 0.012f;
            V2 back = v2scale(v2norm(pr->vel), -1);
            fx_spark(v2mad(pr->pos, back, 8), v2add(v2scale(back, 120 + frand() * 80), v2scale(v2fromang(frand() * TAU), 40)),
                     col_lerp(rgba(1, 0.8f, 0.4f, 1), pr->col, frand()), 0.3f + frand() * 0.2f, 3);
            if (frand() < 0.3f) fx_glow(v2mad(pr->pos, back, 10), v2scale(back, 30), col_a(pr->col, 0.25f), 0.35f, 10);
        }
    }
    V2 old = pr->pos;
    V2 np = v2mad(pr->pos, pr->vel, dt);
    RayHit hit;
    if (raycast(old, np, &hit)) {
        if (hit.seg >= 0 && L.segs[hit.seg].tile >= 0 && !enemy) damage_tile(L.segs[hit.seg].tile, pr->dmg);
        if (pr->type == PR_PROX || pr->type == EP_MINE || pr->type == PR_CARGO) {
            float vn = v2dot(pr->vel, hit.n);
            pr->vel = v2scale(v2mad(pr->vel, hit.n, -2 * vn), 0.4f);
            pr->pos = v2mad(hit.p, hit.n, 2);
            return;
        }
        if (pr->bounces > 0 && hit.door < 0) {
            /* ricochet coils: bounce off the rock once */
            pr->bounces--;
            float vn = v2dot(pr->vel, hit.n);
            pr->vel = v2mad(pr->vel, hit.n, -2 * vn);
            pr->pos = v2mad(hit.p, hit.n, 3);
            pr->life = maxf(pr->life, 0.35f);
            proj_hit_fx(pr, hit.p, hit.n);
            return;
        }
        V2 at = v2mad(hit.p, hit.n, 2);
        if (pr->type == EP_FLAK || pr->type == EP_SEEKER) {
            pr->pos = at;
            pr->active = false;
            flak_pop(pr);
        } else if (pr->splash > 0) proj_detonate(pr, at);
        else {
            proj_hit_fx(pr, at, hit.n);
            pr->active = false;
            if (!enemy && frand() < 0.5f) snd_play_at(SND_WALL, at, 0.35f, frandr(0.8f, 1.3f));
        }
        return;
    }
    pr->pos = np;

    if (!enemy) {
        if (pr->type == PR_PROX && !pr->armed) return;
        if (pr->type == PR_CARGO) {
            /* the bomb goes off early when it bumps into a robot */
            if (pr->age < 0.3f) return;
            for (int i = 0; i < MAX_ROBOTS; i++)
                if (W.rob[i].active && v2dist(W.rob[i].pos, pr->pos) < W.rob[i].radius + pr->radius) { proj_detonate(pr, pr->pos); return; }
            return;
        }
        for (int i = 0; i < MAX_ROBOTS; i++) {
            Robot *r = &W.rob[i];
            if (!r->active) continue;
            if (r->type == RB_BOSS && r->invis_t > 0) continue;
            if (seg_point_dist(old, np, r->pos) > r->radius + pr->radius) continue;
            if (pr->type == PR_FUSION) {
                bool already = false;
                for (int k = 0; k < pr->nhits; k++)
                    if (pr->hits[k] == i) already = true;
                if (already || pr->nhits >= 8) continue;
                pr->hits[pr->nhits++] = i;
                robot_damage(r, pr->dmg, v2norm(pr->vel), true);
                r->vel = v2mad(r->vel, v2norm(pr->vel), 240 * (0.5f + pr->charge) / RDEF[r->type].mass);
                proj_hit_fx(pr, r->pos, v2scale(v2norm(pr->vel), -1));
                continue;
            }
            if (pr->type == PR_PROX) { proj_detonate(pr, pr->pos); return; }
            robot_damage(r, pr->dmg, v2norm(pr->vel), true);
            if (pr->splash > 0) {
                pr->active = false;
                explode(pr->pos, pr->splash, pr->dmg, true, i, pr->col, DS_SELF);
                cluster_burst(pr, pr->pos);
                if (pr->type == PR_SMART) {
                    for (int k = 0; k < 6; k++) {
                        Proj *b = spawn_proj(PR_BLOB, pr->pos, v2scale(v2fromang(TAU * k / 6), 380), 20, 2.5f);
                        if (b) b->turn = 5.0f;
                    }
                }
            } else {
                proj_hit_fx(pr, pr->pos, v2scale(v2norm(pr->vel), -1));
                pr->active = false;
            }
            return;
        }
        float rr = reactor_shielded() ? REACTOR_R + 44 : REACTOR_R - 4;
        if (W.reactor.exists && !W.reactor.dead && seg_point_dist(old, np, W.reactor.pos) < rr + pr->radius) {
            reactor_damage(pr->dmg);
            if (pr->splash > 0) {
                pr->active = false;
                explode(pr->pos, pr->splash, pr->dmg * 0.5f, true, -1, pr->col, DS_SELF);
            } else {
                proj_hit_fx(pr, pr->pos, v2norm(v2sub(pr->pos, W.reactor.pos)));
                if (pr->type != PR_FUSION) pr->active = false;
            }
            if (pr->type == PR_FUSION) pr->active = false;
            return;
        }
        /* shoot down enemy missiles, mines and orbs */
        for (int k = 0; k < nshootable; k++) {
            Proj *e = &W.proj[shootable[k]];
            if (!e->active || e->hp <= 0) continue;
            if (seg_point_dist(old, np, e->pos) > e->radius + pr->radius + 3) continue;
            e->hp -= pr->dmg;
            if (e->hp <= 0) {
                if (e->splash > 0) { e->armed = true; e->life = 0.01f; }
                else {
                    e->active = false;
                    fx_burst(e->pos, 10, e->col, 200, 0.3f, 3);
                }
                game_add_score(25);
            }
            if (pr->type != PR_FUSION) {
                proj_hit_fx(pr, pr->pos, v2(0, -1));
                pr->active = false;
                return;
            }
        }
    } else {
        Player *pl = &W.pl;
        if (pl->dead || G.escaping) return;
        if (pr->type == EP_MINE) return;
        /* the ship's hull is smaller than its shield glow: bullets must really hit it */
        float d = seg_point_dist(old, np, pl->pos);
        if (!pr->grazed && d < pl->radius + pr->radius + 22 && pr->splash <= 0) {
            /* a graze: bullets that brush past feed the energy cells */
            pr->grazed = true;
            W.grazes++;
            pl->energy = minf(200, pl->energy + 0.6f);
            game_add_score(10);
            fx_spark(v2lerp(pr->pos, pl->pos, 0.5f), v2scale(v2norm(v2sub(pl->pos, pr->pos)), -140), rgba(0.8f, 0.95f, 1, 1), 0.2f, 2.5f);
            if (W.graze_snd_t <= 0) {
                snd_play(SND_PICKUP, 0.12f, 2.2f);
                W.graze_snd_t = 0.08f;
            }
        }
        if (d < pl->radius * 0.6f + pr->radius) {
            if (pr->splash > 0) proj_detonate(pr, pr->pos);
            else {
                hit_proj = pr->type;
                player_damage(pr->dmg, v2norm(pr->vel), pr->src);
                proj_hit_fx(pr, pr->pos, v2scale(v2norm(pr->vel), -1));
                pr->active = false;
            }
        }
    }
}

/* ------------------------------------------------------------ powerups */
Powerup *spawn_powerup(int type, V2 pos, V2 vel, int amount) {
    for (int i = 0; i < MAX_POWERUPS; i++) {
        Powerup *p = &W.pu[i];
        if (p->active) continue;
        memset(p, 0, sizeof(*p));
        p->active = true;
        p->type = type;
        p->pos = pos;
        p->vel = vel;
        p->amount = amount;
        p->t = frand() * 10;
        return p;
    }
    return NULL;
}

/* cargo for the hold; with the pool full of dropped weapons it goes straight in */
static void add_cargo(int amount) {
    if (amount > 0) W.pl.cargo += amount;
}

/* scatter exact amounts of salvage (a destroyed ship's cargo) as shards */
static void spawn_salvage_exact(V2 pos, int amount, float speed) {
    if (amount <= 0) return;
    int n = clampi((amount + 3) / 4, 1, 14);
    /* keep the powerup pool free for weapons dropped on death */
    int used = 0;
    for (int i = 0; i < MAX_POWERUPS; i++) used += W.pu[i].active;
    if (used + n > MAX_POWERUPS - 64) {
        Powerup *pu = spawn_powerup(PU_SALVAGE, pos, v2(0, 0), amount);
        if (!pu) add_cargo(amount);
        return;
    }
    for (int i = 0; i < n; i++) {
        int a = amount / n + (i < amount % n ? 1 : 0);
        V2 v = v2scale(v2fromang(frand() * TAU), speed * (0.4f + 0.6f * frand()));
        Powerup *pu = spawn_powerup(PU_SALVAGE, pos, v, a);
        if (pu) pu->nopick = 0.35f + frand() * 0.2f;
        else add_cargo(a);
    }
}

/* salvage from wrecks: the difficulty, the sector and the scanner scale it here, once */
void spawn_salvage(V2 pos, int amount, float speed) {
    static float frac = 0;
    float v = amount * run_salvage_mult() * mod_salvage_mult() + frac;
    int got = (int)v;
    frac = v - got;
    spawn_salvage_exact(pos, got, speed);
}

static void select_primary(int w, bool announce);

static bool is_special_pu(int t) { return t == PU_VULCAN || t == PU_SPREAD || t == PU_PLASMA || t == PU_FUSION; }
static int pu_weapon(int t) { return t == PU_VULCAN ? PW_VULCAN : t == PU_SPREAD ? PW_SPREAD : t == PU_PLASMA ? PW_PLASMA : PW_FUSION; }
static int weapon_pu(int w) { return w == PW_VULCAN ? PU_VULCAN : w == PW_SPREAD ? PU_SPREAD : w == PW_PLASMA ? PU_PLASMA : PU_FUSION; }
static bool is_secondary_pu(int t) { return t >= PU_CONC && t <= PU_MEGA; }
static const char *missile_word(int s, int n) { return s == SW_PROX ? (n == 1 ? "BOMB" : "BOMBS") : (n == 1 ? "MISSILE" : "MISSILES"); }

/* a weapon the ship can't simply take: it would replace the one in the slot */
static bool swap_candidate(const Powerup *pu) {
    const Player *p = &W.pl;
    if (is_special_pu(pu->type)) return p->special >= 0 && p->special != pu_weapon(pu->type);
    if (is_secondary_pu(pu->type)) return p->secondary >= 0 && p->missiles > 0 && p->secondary != pu->type - PU_CONC;
    return false;
}

static int random_module(void) {
    int ids[MOD_COUNT], n = 0;
    for (int i = 0; i < MOD_COUNT; i++) {
        if (!mod_unlocked(i) || R.mods[i] >= MODS[i].max_rank) continue;
        if (R.mods[i] == 0 && mod_installed() >= mod_slots()) continue;
        ids[n++] = i;
    }
    return n ? ids[irand(n)] : -1;
}

static bool try_pickup(Powerup *pu) {
    Player *p = &W.pl;
    char buf[96];
    bool msg_ok = pu->msg_cd <= 0;
    switch (pu->type) {
    case PU_SHIELD:
        if (p->shield >= 200) {
            if (msg_ok) hud_msg("YOUR SHIELD IS AT MAXIMUM", C_BLUE);
            pu->msg_cd = 3;
            return false;
        }
        p->shield = minf(200, p->shield + DIFF_ORB[G.difficulty]);
        snprintf(buf, sizeof(buf), "SHIELD BOOSTED TO %d", (int)p->shield);
        hud_msg(buf, rgba(0.4f, 0.7f, 1, 1));
        snd_play(SND_PICKUP, 0.6f, 0.9f);
        return true;
    case PU_ENERGY:
        if (p->energy >= 200) {
            if (msg_ok) hud_msg("YOUR ENERGY IS AT MAXIMUM", C_YELLOW);
            pu->msg_cd = 3;
            return false;
        }
        p->energy = minf(200, p->energy + 25);
        snprintf(buf, sizeof(buf), "ENERGY BOOSTED TO %d", (int)p->energy);
        hud_msg(buf, C_YELLOW);
        snd_play(SND_PICKUP, 0.6f, 1.15f);
        return true;
    case PU_LASER:
        if (p->laser_level < 4) {
            p->laser_level++;
            snprintf(buf, sizeof(buf), "LASER CANNON BOOSTED TO LEVEL %d!", p->laser_level);
            hud_msg(buf, LASER_COL[p->laser_level]);
            snd_play(SND_POWERUP, 0.7f, 1.0f);
            return true;
        }
        if (p->energy < 200) { p->energy = minf(200, p->energy + 20); hud_msg("MAXIMUM LASER LEVEL: +ENERGY", C_YELLOW); snd_play(SND_PICKUP, 0.6f, 1.1f); return true; }
        return false;
    case PU_QUAD:
        if (!p->quad) {
            p->quad = true;
            hud_msg("QUAD LASERS!", C_GREEN);
            snd_play(SND_POWERUP, 0.7f, 1.1f);
            return true;
        }
        if (p->energy < 200) { p->energy = minf(200, p->energy + 20); hud_msg("QUAD LASERS: +ENERGY", C_YELLOW); snd_play(SND_PICKUP, 0.6f, 1.1f); return true; }
        return false;
    case PU_VULCAN:
    case PU_SPREAD:
    case PU_PLASMA:
    case PU_FUSION: {
        int w = pu_weapon(pu->type);
        if (p->special < 0) {
            p->special = w;
            snprintf(buf, sizeof(buf), "%s CANNON!", PRIMARY_NAMES[w]);
            hud_msg(buf, powerup_color(pu->type));
            snd_play(SND_POWERUP, 0.7f, 1.0f);
            if (g_cfg.autoswitch) select_primary(w, false);
            return true;
        }
        if (p->special != w) return false; /* a swap, see powerups_update */
        if (p->energy < 200) {
            p->energy = minf(200, p->energy + 25);
            snprintf(buf, sizeof(buf), "%s: +ENERGY", PRIMARY_NAMES[w]);
            hud_msg(buf, C_YELLOW);
            snd_play(SND_PICKUP, 0.6f, 1.1f);
            return true;
        }
        if (msg_ok) { snprintf(buf, sizeof(buf), "YOU ALREADY HAVE THE %s CANNON", PRIMARY_NAMES[w]); hud_msg(buf, C_GREY); }
        pu->msg_cd = 3;
        return false;
    }
    case PU_CONC:
    case PU_HOMING:
    case PU_PROX:
    case PU_SMART:
    case PU_MEGA: {
        int s = pu->type - PU_CONC;
        if (p->secondary != s) {
            if (p->secondary >= 0 && p->missiles > 0) return false; /* a swap */
            p->secondary = s;
            p->missiles = 0;
        }
        int cap = max_missiles(s);
        if (p->missiles >= cap) {
            if (msg_ok) {
                snprintf(buf, sizeof(buf), "YOU CAN'T CARRY MORE %s", s == SW_PROX ? "PROXIMITY BOMBS" : "MISSILES OF THIS TYPE");
                hud_msg(buf, C_GREY);
            }
            pu->msg_cd = 3;
            return false;
        }
        int add = pu->amount;
        bool rest = false;
        if (p->missiles + add > cap) {
            pu->amount = p->missiles + add - cap; /* leave the rest */
            add = cap - p->missiles;
            rest = true;
        }
        p->missiles += add;
        if (add == 1) snprintf(buf, sizeof(buf), "%s %s!", SECONDARY_NAMES[s], missile_word(s, 1));
        else snprintf(buf, sizeof(buf), "%d %s %s!", add, SECONDARY_NAMES[s], missile_word(s, add));
        hud_msg(buf, rgba(1, 0.7f, 0.4f, 1));
        snd_play(SND_PICKUP, 0.6f, 1.0f);
        return !rest;
    }
    case PU_CLOAK:
        p->cloak_t = 30;
        hud_msg("CLOAKING DEVICE! ROBOTS CAN'T SEE YOU", rgba(0.6f, 0.8f, 1, 1));
        snd_play(SND_CLOAK, 0.7f, 1.0f);
        return true;
    case PU_INVULN:
        p->invuln_t = 30;
        hud_msg("INVULNERABILITY!", C_YELLOW);
        snd_play(SND_POWERUP, 0.8f, 0.8f);
        return true;
    case PU_LIFE:
        G.lives++;
        hud_msg("EXTRA LIFE!", C_GREEN);
        snd_play(SND_EXTRALIFE, 0.7f, 1.0f);
        return true;
    case PU_SALVAGE: {
        add_cargo(pu->amount);
        snprintf(buf, sizeof(buf), "+%d", pu->amount);
        fx_popup(pu->pos, buf, powerup_color(PU_SALVAGE), pu->amount >= 12 ? 13 : 10);
        int sr = mod_rank(MOD_SIPHON);
        if (sr > 0) {
            p->energy = minf(200, p->energy + 0.6f * pu->amount);
            if (sr >= 2) p->shield = minf(200, p->shield + 0.25f * pu->amount);
        }
        if (p->salvage_snd_t <= 0) {
            snd_play(SND_PICKUP, 0.3f, frandr(1.5f, 1.7f));
            p->salvage_snd_t = 0.06f;
        }
        if (!W.cargo_hint && p->cargo >= 25 && g_prof.runs < 3) {
            W.cargo_hint = true;
            hud_hint("SALVAGE IS CARGO: IT MAKES YOU HEAVIER UNTIL YOU BANK IT AT THE EXIT.  F DUMPS HALF AS A BOMB");
        }
        return true;
    }
    case PU_TREASURE:
        add_cargo(pu->amount);
        snprintf(buf, sizeof(buf), "VAULT TREASURE: +%d SALVAGE", pu->amount);
        hud_msg(buf, powerup_color(PU_SALVAGE));
        snprintf(buf, sizeof(buf), "+%d", pu->amount);
        fx_popup(pu->pos, buf, C_YELLOW, 20);
        fx_ring(pu->pos, 10, 160, powerup_color(PU_SALVAGE), 0.6f, 8);
        W.flash = 0.4f;
        W.flash_col = powerup_color(PU_SALVAGE);
        snd_play(SND_EXTRALIFE, 0.6f, 1.4f);
        profile_complete(CH_VAULT);
        return true;
    case PU_CRATE: {
        int m = random_module();
        if (m >= 0) {
            mod_add(m);
            snprintf(buf, sizeof(buf), "R&D CRATE: %s %s", MODS[m].name, R.mods[m] > 1 ? "UPGRADED" : "INSTALLED");
            hud_msg(buf, mod_color(MODS[m].cat));
            snprintf(buf, sizeof(buf), "%s", MODS[m].desc);
            for (char *c = buf; *c; c++) if (*c == '\n') *c = ' ';
            hud_hint(buf);
            if (m == MOD_QUAD) p->quad = true;
            if (m == MOD_CALIBRATE) p->laser_level = clampi(p->laser_level, mod_base_laser(), 4);
        } else {
            add_cargo(80);
            hud_msg("R&D CRATE: NOTHING FITS - 80 SALVAGE INSTEAD", powerup_color(PU_SALVAGE));
        }
        fx_ring(pu->pos, 10, 180, C_MAGENTA, 0.6f, 8);
        W.flash = 0.4f;
        W.flash_col = C_MAGENTA;
        snd_play(SND_EXTRALIFE, 0.7f, 1.1f);
        return true;
    }
    case PU_KEY_BLUE:
    case PU_KEY_YELLOW:
    case PU_KEY_RED: {
        int lock = LOCK_BLUE + (pu->type - PU_KEY_BLUE);
        p->keys |= lock_bit(lock);
        static const char *names[3] = {"BLUE ACCESS KEY!", "YELLOW ACCESS KEY!", "RED ACCESS KEY!"};
        hud_msg(names[lock - LOCK_BLUE], key_color(lock));
        snd_play(SND_KEY, 0.8f, 1.0f);
        W.flash = 0.5f;
        W.flash_col = key_color(lock);
        fx_ring(pu->pos, 10, 160, key_color(lock), 0.6f, 8);
        return true;
    }
    }
    return false;
}

/* take the weapon under the ship and leave the one from the slot in its place */
static void swap_weapon(void) {
    Player *p = &W.pl;
    if (p->swap_pu < 0) return;
    Powerup *pu = &W.pu[p->swap_pu];
    if (!pu->active || !swap_candidate(pu)) return;
    char buf[96];
    V2 at = pu->pos;
    if (is_special_pu(pu->type)) {
        int old = p->special;
        p->special = pu_weapon(pu->type);
        pu->type = weapon_pu(old);
        pu->amount = 1;
        if (p->primary == old) p->primary = p->special;
        snprintf(buf, sizeof(buf), "%s CANNON SWAPPED IN", PRIMARY_NAMES[p->special]);
    } else {
        int old = p->secondary, oldn = p->missiles;
        int s = pu->type - PU_CONC;
        int cap = max_missiles(s);
        p->secondary = s;
        p->missiles = pu->amount < cap ? pu->amount : cap;
        pu->type = PU_CONC + old;
        pu->amount = oldn;
        snprintf(buf, sizeof(buf), "%d %s %s SWAPPED IN", p->missiles, SECONDARY_NAMES[s], missile_word(s, p->missiles));
    }
    pu->nopick = 1.0f;
    pu->msg_cd = 1.0f;
    pu->vel = v2scale(v2fromang(frand() * TAU), 60);
    hud_msg(buf, rgba(0.7f, 0.9f, 1, 1));
    snd_play(SND_POWERUP, 0.6f, 1.2f);
    fx_ring(at, 6, 50, powerup_color(pu->type), 0.3f, 4);
    p->swap_pu = -1;
}

static void powerups_update(float dt) {
    Player *p = &W.pl;
    p->swap_pu = -1;
    for (int i = 0; i < MAX_POWERUPS; i++) {
        Powerup *pu = &W.pu[i];
        if (!pu->active) continue;
        pu->t += dt;
        pu->nopick -= dt;
        pu->msg_cd -= dt;
        if (!p->dead && !G.escaping && pu->nopick <= 0) {
            float d = v2dist(pu->pos, p->pos);
            bool swap = swap_candidate(pu);
            if (pu->type == PU_SALVAGE || pu->type == PU_TREASURE) {
                if (d < mod_salvage_magnet() && d > 1) pu->vel = v2mad(pu->vel, v2scale(v2sub(p->pos, pu->pos), 1.0f / d), 1400 * dt);
            } else if (d < mod_magnet() && d > 1 && pu->msg_cd <= 0 && !swap)
                pu->vel = v2mad(pu->vel, v2scale(v2sub(p->pos, pu->pos), 1.0f / d), 500 * dt);
            if (d < p->radius + 15) {
                if (swap) {
                    p->swap_pu = i;
                } else if (try_pickup(pu)) {
                    pu->active = false;
                    fx_ring(pu->pos, 6, 40, powerup_color(pu->type), 0.3f, 4);
                    fx_burst(pu->pos, 10, powerup_color(pu->type), 160, 0.35f, 3);
                    continue;
                } else if (pu->msg_cd <= 0) pu->msg_cd = 3;
            }
        }
        if (v2len2(pu->vel) > 1) {
            pu->pos = v2mad(pu->pos, pu->vel, dt);
            pu->vel = v2scale(pu->vel, expf(-2.5f * dt));
            circle_collide(&pu->pos, 12, &pu->vel, 0.5f);
        }
    }
    if (p->swap_pu >= 0 && !W.swap_hint) {
        W.swap_hint = true;
        hud_hint(g_in.use_stick ? "RB SWAPS THE WEAPON IN YOUR SLOT FOR THIS ONE" : "E SWAPS THE WEAPON IN YOUR SLOT FOR THIS ONE");
    }
    if (g_in.swap) swap_weapon();
}

static void hostages_update(float dt) {
    Player *p = &W.pl;
    for (int i = 0; i < W.nhost; i++) {
        Hostage *h = &W.host[i];
        if (!h->active) continue;
        h->t += dt;
        if (!p->dead && !G.escaping && v2dist(h->pos, p->pos) < p->radius + 18) {
            h->active = false;
            G.hostages_onboard++;
            char buf[64];
            snprintf(buf, sizeof(buf), "HOSTAGE RESCUED! %d ON BOARD", G.hostages_onboard);
            hud_msg(buf, C_GREEN);
            snd_play(SND_HOSTAGE, 0.7f, 1.0f);
            fx_ring(h->pos, 8, 70, C_GREEN, 0.5f, 5);
            fx_burst(h->pos, 16, C_GREEN, 180, 0.5f, 3);
            fx_popup(h->pos, "SAVED", C_GREEN, 14);
        }
    }
}

/* ------------------------------------------------------------ doors */
static bool can_open(const Door *d) {
    switch (d->lock) {
    case LOCK_NONE: return true;
    case LOCK_EXIT:
    case LOCK_VAULT: return G.reactor_dead;
    default: return (W.pl.keys & lock_bit(d->lock)) != 0;
    }
}

static void doors_update(float dt) {
    Player *p = &W.pl;
    for (int i = 0; i < L.ndoors; i++) {
        Door *d = &L.doors[i];
        d->msg_cd -= dt;
        bool want = false;
        if (!p->dead && !G.escaping) {
            float pd = door_dist(d, p->pos);
            if (pd < 64) {
                if (can_open(d)) want = true;
                else if (d->msg_cd <= 0 && pd < 34) {
                    const char *m = "";
                    switch (d->lock) {
                    case LOCK_BLUE: m = "BLUE ACCESS KEY REQUIRED"; break;
                    case LOCK_YELLOW: m = "YELLOW ACCESS KEY REQUIRED"; break;
                    case LOCK_RED: m = "RED ACCESS KEY REQUIRED"; break;
                    case LOCK_EXIT: m = G.boss_level ? "EXIT SEALED - DESTROY THE OVERSEER" : "EXIT SEALED - DESTROY THE REACTOR CORE"; break;
                    case LOCK_VAULT: m = "VAULT SEALED - IT OPENS WHEN THE REACTOR BLOWS"; break;
                    }
                    hud_msg(m, key_color(d->lock));
                    snd_play(SND_LOCKED, 0.6f, 1.0f);
                    d->msg_cd = 2.5f;
                }
            }
        }
        if (d->lock == LOCK_NONE || d->open > 0.05f) {
            for (int k = 0; k < MAX_ROBOTS && !want; k++) {
                Robot *r = &W.rob[k];
                if (!r->active) continue;
                float rd = door_dist(d, r->pos);
                if (d->lock == LOCK_NONE && r->aware && rd < 56) want = true;
                if (d->open > 0.05f && rd < r->radius + 4) want = true;
            }
            if (d->open > 0.05f && !p->dead && door_dist(d, p->pos) < p->radius + 6) want = true;
        }
        if (want) {
            if (!d->opening && d->open < 0.05f) snd_play_at(SND_DOOR, door_center(d), 0.7f, 1.0f);
            d->opening = true;
            d->hold = 1.4f;
        } else if (d->opening) {
            d->hold -= dt;
            if (d->hold <= 0) {
                d->opening = false;
                snd_play_at(SND_DOOR, door_center(d), 0.5f, 0.85f);
            }
        }
        float target = d->opening ? 1.0f : 0.0f;
        float sp = 2.6f * dt;
        if (d->open < target) d->open = minf(target, d->open + sp);
        else if (d->open > target) d->open = maxf(target, d->open - sp);
    }
}

/* ------------------------------------------------------------ player */
static V2 ship_local(V2 local) { return safe_muzzle(W.pl.pos, v2add(W.pl.pos, v2rot(local, W.pl.ang))); }

static void stop_charge(void) {
    Player *p = &W.pl;
    if (p->charge_voice) snd_stop(p->charge_voice);
    p->charge_voice = 0;
    p->charging = false;
    p->fusion_charge = 0;
}

/* two primary slots: the laser, and one special weapon */
static void select_primary(int w, bool announce) {
    Player *p = &W.pl;
    char buf[64];
    if (w != PW_LASER && w != p->special) {
        if (announce) {
            hud_msg("NO SPECIAL WEAPON IN YOUR SECOND SLOT", C_GREY);
            snd_play(SND_NOAMMO, 0.5f, 1.0f);
        }
        return;
    }
    if (p->primary == w) return;
    stop_charge();
    p->primary = w;
    snprintf(buf, sizeof(buf), "%s CANNON SELECTED", PRIMARY_NAMES[w]);
    hud_msg(buf, rgba(0.7f, 0.9f, 1, 1));
    snd_play(SND_MENU_MOVE, 0.5f, 0.8f);
    p->fire_cd = maxf(p->fire_cd, 0.15f);
}

static void fallback_primary(void) {
    if (W.pl.primary != PW_LASER) select_primary(PW_LASER, false);
}

static void player_die(void);

/* a shock pulse around the ship: damages robots and erases enemy shots */
static void ship_pulse(float radius, float dmg, Col c) {
    Player *p = &W.pl;
    for (int i = 0; i < MAX_ROBOTS; i++) {
        Robot *r = &W.rob[i];
        if (!r->active) continue;
        float d = v2dist(r->pos, p->pos) - r->radius;
        if (d > radius || !los(p->pos, r->pos)) continue;
        robot_damage(r, dmg * (1 - maxf(0, d) / radius * 0.5f), v2norm(v2sub(r->pos, p->pos)), true);
    }
    for (int i = 0; i < MAX_PROJ; i++) {
        Proj *e = &W.proj[i];
        if (!e->active || !IS_ENEMY_PROJ(e->type) || e->type == EP_MINE) continue;
        if (v2dist(e->pos, p->pos) > radius) continue;
        e->active = false;
        fx_burst(e->pos, 5, e->col, 150, 0.25f, 3);
    }
    fx_ring(p->pos, 20, radius, c, 0.35f, 7);
    fx_ring(p->pos, 10, radius * 0.6f, col_white(c, 0.5f), 0.25f, 4);
    grid_impulse(p->pos, radius * 1.6f, radius * 3);
    snd_play(SND_PROX, 0.5f, 0.7f);
}

/* the Phoenix Protocol turns one lethal hit per mine into a narrow escape */
static bool try_phoenix(void) {
    Player *p = &W.pl;
    if (!mod_on(MOD_PHOENIX) || W.phoenix_used) return false;
    W.phoenix_used = true;
    p->shield = 30;
    p->invuln_t = 3;
    ship_pulse(220, 60, rgba(1, 0.6f, 0.2f, 1));
    fx_explosion(p->pos, 40, rgba(1, 0.7f, 0.3f, 1));
    W.flash = 0.5f;
    W.flash_col = rgba(1, 0.6f, 0.2f, 1);
    W.time_scale = 0.35f;
    shake_add(0.6f);
    snd_play(SND_EXTRALIFE, 0.7f, 0.8f);
    hud_msg("PHOENIX PROTOCOL ENGAGED!", rgba(1, 0.65f, 0.25f, 1));
    return true;
}

/* the gravity anchor: a heavier ship shrugs off more */
static float anchor_factor(void) { return mod_on(MOD_ANCHOR) ? 1.0f - clampf((W.pl.mass - 1) * 0.4f, 0, 0.4f) : 1.0f; }

void player_damage(float dmg, V2 dir, int src) {
    Player *p = &W.pl;
    int proj = hit_proj;
    hit_proj = -1;
    if (p->dead || G.escaping || G.failing || dmg <= 0) return;
    if (p->invuln_t > 0 || p->spawn_inv > 0 || p->phase_t > 0) {
        fx_ring(p->pos, 18, 30, p->phase_t > 0 ? C_CYAN : C_YELLOW, 0.2f, 4);
        return;
    }
    dmg *= DIFF_DMG[G.difficulty] * mod_damage_taken() * anchor_factor();
    W.last_src = src;
    W.last_proj = proj;
    if (mod_on(MOD_SCRAP) && p->cargo > 1) {
        /* scrap armour: half the hit is paid in salvage from the hold */
        float pay = minf(dmg * 0.5f, p->cargo * 0.5f);
        p->cargo -= (int)ceilf(pay * 2);
        if (p->cargo < 0) p->cargo = 0;
        dmg -= pay;
        for (int i = 0; i < 6; i++) fx_spark(p->pos, v2scale(v2fromang(frand() * TAU), 120 + frand() * 160), powerup_color(PU_SALVAGE), 0.35f, 3);
    }
    p->shield -= dmg;
    p->calm_t = 0;
    if (mod_on(MOD_REACTIVE) && p->reactive_cd <= 0) {
        p->reactive_cd = 2.5f;
        ship_pulse(150, 30, rgba(0.4f, 0.75f, 1, 1));
    }
    p->hit_flash = 0.35f;
    if (v2len2(dir) > 0.01f) { p->hit_dir = v2scale(v2norm(dir), -1); p->hit_dir_t = 0.7f; }
    shake_add(clampf(0.12f + dmg * 0.025f, 0, 0.6f));
    p->vel = v2mad(p->vel, dir, clampf(dmg * 9, 30, 260) / p->mass);
    snd_play(SND_PLAYER_HIT, clampf(0.4f + dmg * 0.03f, 0.4f, 0.9f), frandr(0.9f, 1.1f));
    W.flash = maxf(W.flash, clampf(dmg * 0.02f, 0.05f, 0.3f));
    W.flash_col = rgba(1, 0.2f, 0.15f, 1);
    for (int i = 0; i < 8; i++)
        fx_spark(p->pos, v2scale(v2fromang(frand() * TAU), 100 + frand() * 200), rgba(0.5f, 0.8f, 1, 1), 0.3f, 3);
    if (p->shield < 0 && !try_phoenix()) player_die();
}

const char *game_killer_text(void) {
    static char buf[64];
    int s = W.last_src, pr = W.last_proj;
    const char *who = s >= 0 && s < RB_COUNT ? RDEF[s].name : s == DS_REACTOR ? "THE REACTOR CORE" : s == DS_SELF ? "YOUR OWN BLAST"
                      : s == DS_OVERCHARGE ? "FUSION OVERCHARGE" : s == DS_BLAST ? "THE SELF-DESTRUCT" : s == DS_TRAP ? "A MINE TRAP" : "THE MINE";
    const char *how = NULL;
    switch (pr) {
    case EP_PULSE: how = "PULSE SHOT"; break;
    case EP_BOLT: how = "LASER BOLT"; break;
    case EP_NEEDLE: how = "NEEDLES"; break;
    case EP_VULCAN: how = "VULCAN FIRE"; break;
    case EP_MISSILE: how = "MISSILE"; break;
    case EP_HOMING: how = "HOMING MISSILE"; break;
    case EP_MINE: how = "PROXIMITY MINE"; break;
    case EP_ORB: how = "PLASMA ORB"; break;
    case EP_REACTOR: how = NULL; break;
    case EP_PELLET: how = "PLASMA PELLETS"; break;
    case EP_SHARD: how = "SHARDS"; break;
    case EP_FLAK: how = "FLAK"; break;
    case EP_SEEKER: how = "SEEKER MINE"; break;
    default: if (s >= 0 && s < RB_COUNT && RDEF[s].contact > 0) how = "CLAWS"; break;
    }
    if (how) snprintf(buf, sizeof(buf), "%s - %s", who, how);
    else snprintf(buf, sizeof(buf), "%s", who);
    return buf;
}

static void drop_scatter(int type, int amount) {
    V2 v = v2scale(v2fromang(frand() * TAU), 90 + frand() * 160);
    Powerup *pu = spawn_powerup(type, W.pl.pos, v, amount);
    if (pu) pu->nopick = 0.5f;
}

static void player_die(void) {
    Player *p = &W.pl;
    p->dead = true;
    p->dead_t = 0;
    p->shield = 0;
    stop_charge();
    fx_explosion(p->pos, 60, rgba(0.6f, 0.9f, 1, 1));
    fx_explosion(p->pos, 35, rgba(1, 0.7f, 0.3f, 1));
    fx_shape_debris(&SHIP_SHAPE, p->pos, p->ang, 16, p->vel, rgba(0.8f, 0.95f, 1, 1));
    snd_play(SND_EXPL_L, 1.0f, 1.0f);
    shake_add(0.9f);
    W.flash = 0.6f;
    W.flash_col = rgba(1, 1, 1, 1);
    grid_impulse(p->pos, 360, 700);
    R.deaths++;
    /* everything you carried is left where you died, the cargo too */
    if (p->special >= 0) drop_scatter(weapon_pu(p->special), 1);
    /* levels the modules provide come back with the next ship anyway */
    for (int i = mod_base_laser(); i < p->laser_level; i++) drop_scatter(PU_LASER, 1);
    if (p->quad && !mod_on(MOD_QUAD)) drop_scatter(PU_QUAD, 1);
    if (p->secondary >= 0 && p->missiles > 0) drop_scatter(PU_CONC + p->secondary, p->missiles);
    drop_scatter(PU_ENERGY, 1);
    if (p->cargo > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%d CARGO SPILLED - FLY BACK FOR IT", p->cargo);
        hud_msg(buf, powerup_color(PU_SALVAGE));
        spawn_salvage_exact(p->pos, p->cargo, 260);
        p->cargo = 0;
    }
    if (G.hostages_onboard > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%d HOSTAGE%s LOST!", G.hostages_onboard, G.hostages_onboard > 1 ? "S" : "");
        hud_msg(buf, C_RED);
        G.hostages_lost += G.hostages_onboard;
        G.hostages_onboard = 0;
    }
    G.chain = 0;
    G.chain_t = 0;
    W.time_scale = 0.25f;
    hud_msg("SHIP DESTROYED", C_RED);
    if (G.lives <= 0) snprintf(R.killed_by, sizeof(R.killed_by), "%s", game_killer_text());
}

static void player_respawn(void) {
    Player *p = &W.pl;
    reset_loadout();
    p->pos = p->start;
    p->vel = v2(0, 0);
    p->ang = -PI / 2;
    p->dead = false;
    p->spawn_inv = 3.0f;
    p->hit_flash = 0;
    p->cloak_t = p->invuln_t = 0;
    reset_modules();
    g_cam.pos = p->pos;
    g_cam.lean = v2(0, 0);
    snd_play(SND_TELEPORT, 0.6f, 1.0f);
    fx_ring(p->pos, 10, 120, rgba(0.6f, 0.9f, 1, 1), 0.7f, 6);
    hud_msg("NEW SHIP DEPLOYED", rgba(0.6f, 0.9f, 1, 1));
    flow_compute(L.flow, tx_of(p->pos.x), tx_of(p->pos.y), false);
}

/* everything that scales the primary weapons right now */
static float primary_dmg(void) {
    const Player *p = &W.pl;
    float k = mod_primary_dmg();
    int mr = mod_rank(MOD_MOMENTUM);
    if (mr) k *= 1.0f + 0.25f * mr * clampf(v2len(p->vel) / 520.0f, 0, 1);
    if (mod_on(MOD_OVERDRIVE) && G.reactor_dead) k *= 1.35f;
    if (mod_on(MOD_LIFESUPPORT)) k *= 1.0f + 0.06f * G.hostages_onboard;
    return k;
}

static Proj *spawn_primary(int type, V2 pos, V2 vel, float dmg, float life) {
    Proj *pr = spawn_proj(type, pos, vel, dmg, life);
    if (pr && mod_on(MOD_RICOCHET)) pr->bounces = 1;
    return pr;
}

static void fire_primary(float dt) {
    Player *p = &W.pl;
    bool held = g_in.fire1;
    V2 dir = v2fromang(p->ang);
    if (p->primary == PW_FUSION) {
        if (held && !p->charging && p->fire_cd <= 0) {
            if (p->energy >= 2) {
                p->charging = true;
                p->fusion_charge = 0;
                p->over_t = 0;
                p->energy -= 2;
                p->charge_voice = snd_loop(SND_FUSION_CHARGE, 0.45f, 1.0f);
            } else {
                hud_msg("NOT ENOUGH ENERGY FOR FUSION", C_YELLOW);
                fallback_primary();
                return;
            }
        }
        if (p->charging) {
            p->fusion_charge += dt;
            p->energy = maxf(0, p->energy - 7 * dt);
            if (p->fusion_charge > 2.4f) {
                p->over_t += dt;
                if (p->invuln_t <= 0 && p->spawn_inv <= 0) {
                    p->shield -= 9 * dt * DIFF_DMG[G.difficulty] * mod_damage_taken();
                    W.last_src = DS_OVERCHARGE;
                    W.last_proj = -1;
                }
                shake_add(0.03f);
                p->hit_flash = maxf(p->hit_flash, 0.1f);
                if (p->warn_cd <= 0) { hud_msg("FUSION OVERCHARGE! RELEASE!", C_RED); p->warn_cd = 1.5f; }
                if (p->shield < 0) {
                    if (try_phoenix()) stop_charge();
                    else player_die();
                    return;
                }
            }
            if (!held) {
                float k = clampf(p->fusion_charge / 1.6f, 0, 1);
                Proj *pr = spawn_proj(PR_FUSION, ship_local(v2(20, 0)), v2add(v2scale(dir, 1150), v2scale(p->vel, 0.3f)), (40 + 140 * k) * primary_dmg(), 1.4f);
                if (pr) { pr->charge = k; pr->radius = 10 + 8 * k; }
                snd_play(SND_FUSION, 0.6f + 0.35f * k, 1.15f - 0.25f * k);
                stop_charge();
                p->fire_cd = 0.45f;
                p->vel = v2mad(p->vel, dir, -(60 + 140 * k) / p->mass);
                shake_add(0.1f + 0.2f * k);
                p->muzzle_flash = 1;
                grid_impulse(ship_local(v2(20, 0)), 120, 200 + 300 * k);
            }
        }
        return;
    }
    if (!held || p->fire_cd > 0) return;
    V2 base = v2add(v2scale(dir, 1000), v2scale(p->vel, 0.35f));
    float pd = primary_dmg();
    switch (p->primary) {
    case PW_LASER: {
        float cost = p->quad ? 0.7f : 0.5f;
        bool low = p->energy < cost;
        p->energy = maxf(0, p->energy - cost);
        float dmg = LASER_DMG[p->laser_level] * pd;
        Col c = LASER_COL[p->laser_level];
        V2 mz[4] = {v2(7, 11.5f), v2(7, -11.5f), v2(13, 5), v2(13, -5)};
        int n = p->quad ? 4 : 2;
        for (int i = 0; i < n; i++) {
            /* the inner quad bolts are lighter */
            Proj *pr = spawn_primary(PR_LASER, ship_local(mz[i]), base, i < 2 ? dmg : dmg * 0.6f, 0.8f);
            if (pr) pr->col = c;
        }
        snd_play(p->laser_level >= 3 ? SND_LASER2 : SND_LASER, 0.32f, frandr(0.95f, 1.05f) * (p->quad ? 0.92f : 1.0f));
        p->fire_cd = low ? 0.45f : 0.21f;
        if (low && p->warn_cd <= 0) { hud_msg("ENERGY DEPLETED - LASERS AT MINIMUM POWER", C_YELLOW); p->warn_cd = 4; }
    } break;
    case PW_VULCAN: {
        if (p->energy < 0.25f) { hud_msg("OUT OF ENERGY!", C_YELLOW); fallback_primary(); return; }
        p->energy -= 0.25f;
        float a = p->ang + frandr(-0.035f, 0.035f);
        V2 v = v2add(v2scale(v2fromang(a), 1500), v2scale(p->vel, 0.3f));
        spawn_primary(PR_VULCAN, ship_local(p->fire_side ? v2(7, 11.5f) : v2(7, -11.5f)), v, 5.5f * pd, 0.55f);
        p->fire_side ^= 1;
        snd_play(SND_VULCAN, 0.28f, frandr(0.9f, 1.1f));
        p->fire_cd = 0.065f;
        p->vel = v2mad(p->vel, dir, -6 / p->mass);
        shake_add(0.008f);
    } break;
    case PW_SPREAD: {
        if (p->energy < 0.75f) { hud_msg("OUT OF ENERGY!", C_YELLOW); fallback_primary(); return; }
        p->energy -= 0.75f;
        for (int i = -1; i <= 1; i++) {
            V2 v = v2add(v2scale(v2fromang(p->ang + i * 0.13f), 850), v2scale(p->vel, 0.3f));
            spawn_primary(PR_SPREAD, ship_local(v2(16, 0)), v, 12 * pd, 0.9f);
        }
        snd_play(SND_SPREAD, 0.3f, frandr(0.95f, 1.05f));
        p->fire_cd = 0.18f;
    } break;
    case PW_PLASMA: {
        if (p->energy < 0.6f) { hud_msg("OUT OF ENERGY!", C_YELLOW); fallback_primary(); return; }
        p->energy -= 0.6f;
        V2 v = v2add(v2scale(dir, 1100), v2scale(p->vel, 0.3f));
        spawn_primary(PR_PLASMA, ship_local(p->fire_side ? v2(8, 11) : v2(8, -11)), v, 15 * pd, 0.8f);
        p->fire_side ^= 1;
        snd_play(SND_PLASMA, 0.28f, frandr(0.95f, 1.08f));
        p->fire_cd = 0.09f;
    } break;
    }
    p->muzzle_flash = 1;
}

/* one secondary slot; proximity bombs are dropped behind the ship */
static void fire_secondary(void) {
    Player *p = &W.pl;
    V2 dir = v2fromang(p->ang);
    if (!g_in.fire2 || p->sec_cd > 0) return;
    int s = p->secondary;
    if (s < 0 || p->missiles <= 0) {
        if (g_in.fire2_pressed) {
            hud_msg("NO MISSILES", C_GREY);
            snd_play(SND_NOAMMO, 0.4f, 1.0f);
        }
        p->sec_cd = 0.5f;
        return;
    }
    p->missiles--;
    V2 side = p->sec_side ? v2(4, 9) : v2(4, -9);
    p->sec_side ^= 1;
    V2 mz = ship_local(side);
    V2 inherit = v2scale(p->vel, 0.4f);
    Proj *pr = NULL;
    switch (s) {
    case SW_CONCUSSION:
        spawn_proj(PR_CONCUSSION, mz, v2add(v2scale(dir, 720), inherit), 45, 3);
        p->sec_cd = 0.5f;
        snd_play(SND_MISSILE, 0.5f, 1.0f);
        break;
    case SW_HOMING:
        pr = spawn_proj(PR_HOMING, mz, v2add(v2scale(dir, 580), inherit), 45, 4);
        if (pr) pr->turn = 3.4f;
        p->sec_cd = 0.55f;
        snd_play(SND_MISSILE, 0.5f, 1.15f);
        break;
    case SW_PROX:
        spawn_proj(PR_PROX, ship_local(v2(-18, 0)), v2add(v2scale(p->vel, 0.3f), v2scale(dir, -80)), 70, 25);
        p->sec_cd = 0.4f;
        snd_play(SND_PROX, 0.5f, 1.0f);
        break;
    case SW_SMART:
        spawn_proj(PR_SMART, mz, v2add(v2scale(dir, 620), inherit), 50, 3);
        p->sec_cd = 0.8f;
        snd_play(SND_MISSILE, 0.55f, 0.85f);
        break;
    case SW_MEGA:
        pr = spawn_proj(PR_MEGA, ship_local(v2(14, 0)), v2add(v2scale(dir, 300), inherit), 170, 5);
        if (pr) pr->turn = 1.1f;
        p->sec_cd = 1.2f;
        p->vel = v2mad(p->vel, dir, -140 / p->mass);
        snd_play(SND_MEGA, 0.7f, 1.0f);
        shake_add(0.15f);
        break;
    }
    if (p->missiles == 0) hud_msg("SECONDARY SLOT EMPTY", C_GREY);
}

/* dump half the hold as a bomb: a lighter ship and a bang that grows with the salvage */
static void jettison_cargo(void) {
    Player *p = &W.pl;
    if (p->jettison_cd > 0) return;
    if (p->cargo < 10) {
        hud_msg(p->cargo > 0 ? "NOT ENOUGH CARGO FOR A BOMB" : "THE HOLD IS EMPTY", C_GREY);
        snd_play(SND_NOAMMO, 0.4f, 1.0f);
        p->jettison_cd = 0.4f;
        return;
    }
    int amount = p->cargo < 20 ? p->cargo : p->cargo / 2;
    p->cargo -= amount;
    V2 dir = v2fromang(p->ang);
    Proj *pr = spawn_proj(PR_CARGO, ship_local(v2(-18, 0)), v2add(v2scale(p->vel, 0.3f), v2scale(dir, -90)), 0, 1.1f);
    if (pr) {
        pr->cargo = amount;
        pr->radius = 9 + minf(10, sqrtf((float)amount) * 0.5f);
    }
    p->vel = v2mad(p->vel, dir, 60);
    p->jettison_cd = 0.6f;
    char buf[48];
    snprintf(buf, sizeof(buf), "%d CARGO JETTISONED!", amount);
    hud_msg(buf, powerup_color(PU_SALVAGE));
    snd_play(SND_PROX, 0.7f, 0.6f);
    fx_ring(p->pos, 10, 60, powerup_color(PU_SALVAGE), 0.3f, 5);
}

/* Ram Prow: the afterburning ship smashes robots it touches, harder the heavier it is */
static void ram_robots(void) {
    Player *p = &W.pl;
    float sp = v2len(p->vel);
    for (int i = 0; i < MAX_ROBOTS; i++) {
        Robot *r = &W.rob[i];
        if (!r->active || W.time < r->ram_next) continue;
        if (r->type == RB_BOSS && r->invis_t > 0) continue;
        V2 to = v2sub(r->pos, p->pos);
        if (v2len(to) > r->radius + p->radius + 4) continue;
        V2 dir = v2norm(to);
        r->ram_next = W.time + 0.35f;
        V2 at = v2mad(p->pos, dir, p->radius);
        W.ramming = true;
        robot_damage(r, (45 + sp * 0.06f) * p->mass, dir, true);
        W.ramming = false;
        p->vel = v2scale(p->vel, 0.8f + 0.1f * clampf(p->mass - 1, 0, 1));
        fx_burst(at, 14, rgba(0.6f, 1, 0.85f, 1), 260, 0.35f, 3);
        fx_ring(at, 6, 40 + 12 * p->mass, rgba(0.6f, 1, 0.85f, 1), 0.2f, 4);
        grid_impulse(at, 90, 160 * p->mass);
        shake_add(0.12f + 0.06f * p->mass);
        snd_play_at(SND_EXPL_S, at, 0.6f, 1.5f - 0.2f * p->mass);
    }
}

/* on-kill modules, challenges and stats; returns the salvage multiplier for the wreck */
float player_on_kill(Robot *r) {
    Player *p = &W.pl;
    R.kills++;
    g_prof.lifetime_kills++;
    if (W.blast_kills >= 0) W.blast_kills++;
    if (W.ramming) {
        G.ram_kills++;
        if (G.ram_kills >= 5) profile_complete(CH_RAM);
    }
    int vr = mod_rank(MOD_VAMPIRE);
    if (vr && !p->dead) p->shield = minf(200, p->shield + 2.0f * vr);
    int fr = mod_rank(MOD_FABRICATOR);
    if (fr && !p->dead && ++p->fab_kills >= (fr >= 2 ? 5 : 8)) {
        p->fab_kills = 0;
        int s = p->secondary >= 0 ? p->secondary : SW_CONCUSSION;
        if (p->secondary < 0) { p->secondary = s; p->missiles = 0; }
        if (p->missiles < max_missiles(s)) {
            p->missiles++;
            fx_popup(p->pos, "+1 MISSILE", rgba(1, 0.7f, 0.4f, 1), 11);
        }
    }
    if (mod_on(MOD_DETONATOR) && r->type != RB_BOSS && ndet < 32) {
        det_pos[ndet] = r->pos;
        det_t[ndet] = 0.08f + frand() * 0.08f;
        ndet++;
    }
    int mult = 1 + (G.chain - 1 < 28 ? G.chain - 1 : 28) / 4;
    return mult >= 3 ? 1.0f + 0.5f * mod_rank(MOD_BOUNTY) : 1.0f;
}

static void detonators_update(float dt) {
    for (int i = 0; i < ndet;) {
        det_t[i] -= dt;
        if (det_t[i] > 0) { i++; continue; }
        V2 at = det_pos[i];
        det_pos[i] = det_pos[ndet - 1];
        det_t[i] = det_t[ndet - 1];
        ndet--;
        explode(at, 75, 32, true, -1, rgba(1, 0.45f, 0.25f, 1), DS_SAFE);
    }
}

static V2 drone_pos(void) { return v2add(W.pl.pos, v2scale(v2fromang(W.pl.drone_ang), 34)); }

/* Guardian Drone: orbits the ship and snipes the nearest visible robot */
static void drone_update(float dt) {
    Player *p = &W.pl;
    p->drone_ang = wrap_angle(p->drone_ang + 2.2f * dt);
    p->drone_cd -= dt;
    if (p->drone_cd > 0) return;
    V2 dp = drone_pos();
    int best = -1;
    float bd = 460;
    for (int i = 0; i < MAX_ROBOTS; i++) {
        Robot *r = &W.rob[i];
        if (!r->active) continue;
        if (r->type == RB_BOSS && r->invis_t > 0) continue;
        if (RDEF[r->type].cloaked && r->cloak_vis < 0.5f) continue;
        float d = v2dist(r->pos, dp);
        if (d < bd && in_view(r->pos, 0) && los(dp, r->pos)) { bd = d; best = i; }
    }
    if (best < 0 && W.reactor.exists && !W.reactor.dead && v2dist(W.reactor.pos, dp) < bd && in_view(W.reactor.pos, 0) && los(dp, W.reactor.pos))
        best = -2;
    if (best == -1) { p->drone_cd = 0.15f; return; }
    V2 target = best >= 0 ? W.rob[best].pos : W.reactor.pos;
    V2 dir = v2norm(v2sub(target, dp));
    Proj *pr = spawn_proj(PR_LASER, safe_muzzle(p->pos, dp), v2add(v2scale(dir, 1000), v2scale(p->vel, 0.3f)), 7, 0.6f);
    if (pr) pr->col = rgba(1, 0.85f, 0.3f, 1);
    snd_play(SND_LASER, 0.14f, 1.6f);
    p->drone_cd = mod_rank(MOD_DRONE) >= 2 ? 0.28f : 0.55f;
}

/* salvage and hostages weigh the ship down until they are banked at the exit */
static float ship_mass(void) {
    const Player *p = &W.pl;
    float hostages = mod_on(MOD_LIFESUPPORT) ? 0 : G.hostages_onboard * 0.1f;
    float m = 1.0f + (p->cargo * 0.0015f * mod_cargo_k() + hostages) * run_cargo_mult();
    return minf(m, 3.0f);
}

static void player_update(float dt) {
    Player *p = &W.pl;
    if (G.escaping || G.failing) return;
    if (p->dead) {
        p->dead_t += dt;
        if (p->dead_t > 2.2f && (g_in.fire1 || g_in.fire2 || p->dead_t > 5.0f)) {
            if (G.lives > 0) {
                G.lives--;
                player_respawn();
            } else {
                G.result = GR_GAME_OVER;
            }
        }
        return;
    }
    p->cloak_t = maxf(0, p->cloak_t - dt);
    p->invuln_t = maxf(0, p->invuln_t - dt);
    p->spawn_inv = maxf(0, p->spawn_inv - dt);
    p->hit_flash = maxf(0, p->hit_flash - dt);
    p->hit_dir_t = maxf(0, p->hit_dir_t - dt);
    p->fire_cd -= dt;
    p->sec_cd -= dt;
    p->warn_cd -= dt;
    p->bump_cd -= dt;
    p->scrape_cd -= dt;
    p->jettison_cd -= dt;
    p->muzzle_flash = maxf(0, p->muzzle_flash - dt * 12);
    p->salvage_snd_t -= dt;
    p->reactive_cd -= dt;
    p->phase_t = maxf(0, p->phase_t - dt);
    p->phase_cd -= dt;
    p->calm_t += dt;
    if (p->calm_t > 4.0f && p->shield < mod_repair_cap()) p->shield = minf(mod_repair_cap(), p->shield + mod_repair_rate() * dt);
    if (p->cloak_t > 0 && p->cloak_t < dt * 1.5f) hud_msg("CLOAK DEACTIVATED", C_GREY);
    if (p->invuln_t > 0 && p->invuln_t < dt * 1.5f) hud_msg("INVULNERABILITY EXPIRED", C_GREY);

    /* aim */
    float target_ang = p->ang;
    if (g_in.use_stick) {
        if (v2len(g_in.stick_aim) > 0.3f) target_ang = v2ang(g_in.stick_aim);
        else if (v2len(g_in.move) > 0.3f) target_ang = v2ang(g_in.move);
    } else {
        V2 d = v2sub(g_in.aim_world, p->pos);
        if (v2len2(d) > 25) target_ang = v2ang(d);
    }
    p->ang = approach_angle(p->ang, target_ang, 22.0f * dt);

    /* movement: a heavy ship accelerates slowly and coasts further */
    p->mass = lerpf(p->mass, ship_mass(), damp_factor(6, dt));
    V2 mv = g_in.move;
    if (v2len(mv) > 1) mv = v2norm(mv);
    if (g_cfg.move_mode == 1 && !g_in.use_stick) {
        V2 fw = v2fromang(p->ang), rt = v2perp(fw);
        mv = v2add(v2scale(fw, -g_in.move.y), v2scale(rt, g_in.move.x));
        if (v2len(mv) > 1) mv = v2norm(mv);
    }
    float accel = 1250 * mod_accel() / p->mass, drag = 3.6f / sqrtf(p->mass);
    if (mod_on(MOD_OVERDRIVE) && G.reactor_dead) accel *= 1.2f;
    float ml = v2len(mv);
    bool burn = g_in.burner && p->burner > 0.02f && ml > 0.1f;
    if (burn) {
        accel *= 2.0f;
        drag = 3.9f / sqrtf(p->mass);
        p->burner = maxf(0, p->burner - 0.42f * mod_burner_drain() * dt);
    } else {
        p->burner = minf(1, p->burner + 0.16f * mod_burner_regen() * dt);
    }
    p->burning = burn;
    if (burn && !p->was_burning && mod_on(MOD_PHASE) && p->phase_cd <= 0) {
        p->phase_t = 0.5f;
        p->phase_cd = 2.5f;
        fx_ring(p->pos, 8, 46, C_CYAN, 0.3f, 4);
        snd_play(SND_CLOAK, 0.4f, 1.6f);
    }
    p->was_burning = burn;
    p->vel = v2mad(p->vel, mv, accel * dt);
    p->vel = v2scale(p->vel, 1.0f - minf(1, drag * dt));
    p->thrust = lerpf(p->thrust, ml * (burn ? 1.6f : 1.0f), damp_factor(10, dt));
    if (p->burn_voice) snd_loop_set(p->burn_voice, burn ? 0.55f : 0.0f, 1.0f / sqrtf(p->mass));

    /* exhaust particles */
    if (ml > 0.1f) {
        p->trail_t -= dt;
        while (p->trail_t <= 0) {
            p->trail_t += burn ? 0.008f : 0.02f;
            V2 back = v2scale(v2norm(mv), -1);
            V2 at = v2mad(p->pos, back, 12);
            Col c = burn ? rgba(0.5f, 0.8f, 1, 1) : rgba(1, 0.6f, 0.2f, 1);
            fx_spark(at, v2add(v2add(v2scale(back, 220 + frand() * 150), v2scale(p->vel, 0.5f)), v2scale(v2fromang(frand() * TAU), 40)),
                     c, 0.22f + frand() * 0.12f, burn ? 3.4f : 2.8f);
        }
        grid_impulse(v2mad(p->pos, mv, -20), 60, (burn ? 70 : 30) * ml * dt * 60 * 0.12f * p->mass);
    }

    /* integrate + collide */
    V2 delta = v2scale(p->vel, dt);
    int steps = (int)ceilf(v2len(delta) / 6.0f);
    if (steps < 1) steps = 1;
    float sp_before = v2len(p->vel);
    bool hit_wall = false;
    for (int i = 0; i < steps; i++) {
        p->pos = v2add(p->pos, v2scale(delta, 1.0f / steps));
        if (circle_collide(&p->pos, p->radius, &p->vel, 0.3f / p->mass)) hit_wall = true;
    }
    if (hit_wall && sp_before > 220 && p->scrape_cd <= 0) {
        snd_play(SND_WALL, clampf(sp_before / 600, 0.2f, 0.7f), 0.7f / sqrtf(p->mass));
        fx_burst(p->pos, 6, rgba(0.7f, 0.9f, 1, 1), 150, 0.25f, 2.5f);
        p->scrape_cd = 0.25f;
        shake_add(0.05f * p->mass);
    }

    if (burn && mod_on(MOD_RAM)) ram_robots();

    /* energy center */
    int tx = tx_of(p->pos.x), ty = tx_of(p->pos.y);
    float ecap = mod_start_energy();
    if (tile_in(tx, ty) && (L.flags[ty][tx] & TF_ENERGY) && p->energy < ecap) {
        p->energy = minf(ecap, p->energy + 32 * mod_energy_center_rate() * dt);
        p->energy_snd_t -= dt;
        if (p->energy_snd_t <= 0) {
            p->energy_snd_t = 0.09f;
            snd_play(SND_ENERGY, 0.35f, 0.8f + p->energy / 100.0f * 0.8f);
            fx_spark(v2add(p->pos, v2(frandr(-20, 20), 20)), v2(0, -120), C_YELLOW, 0.3f, 2.5f);
        }
    }

    fire_primary(dt);
    fire_secondary();
    if (g_in.jettison) jettison_cargo();
    if (mod_on(MOD_DRONE)) drone_update(dt);
}

/* ------------------------------------------------------------ self destruct */
void start_self_destruct(V2 at) {
    if (G.reactor_dead) return;
    G.reactor_dead = true;
    int xs[64], ys[64], n = 0;
    for (int y = 0; y < L.h; y++)
        for (int x = 0; x < L.w; x++)
            if ((L.flags[y][x] & TF_EXIT) && n < 64) { xs[n] = x; ys[n] = y; n++; }
    flow_compute_multi(L.exitflow, xs, ys, n, true);
    /* the fuse is longer the further the exit: the mines are big */
    int tx = clampi(tx_of(at.x), 0, L.w - 1), ty = clampi(tx_of(at.y), 0, L.h - 1);
    float path = L.exitflow[ty][tx] < 60000 ? (float)L.exitflow[ty][tx] : 80;
    float cd = DIFF_COUNTDOWN[G.difficulty] + maxf(0, path - 50) * 0.28f - (run_protocol(TP_FUSE) ? 8 : 0);
    if (run_hazard() == HZ_FUSE) cd *= 0.75f;
    cd = maxf(15, cd) + mod_countdown_bonus();
    G.countdown = G.countdown_max = cd;
    if (!G.boss_level && G.level_time < 150) profile_complete(CH_SPEED);
    for (int i = 0; i < W.nmat; i++) {
        W.mat[i].triggered = true;
        W.mat[i].max += 2 + G.difficulty / 2;
        W.mat[i].timer = minf(W.mat[i].timer, 3.0f);
    }
    music_play(SONG_ESCAPE);
    hud_msg(G.boss_level ? "THE OVERSEER IS DESTROYED!" : "REACTOR DESTROYED!", C_YELLOW);
    hud_msg("SELF-DESTRUCT SEQUENCE ACTIVATED", C_RED);
    hud_msg("ESCAPE THROUGH THE EXIT TUNNEL!", C_WHITE);
    if (W.nvaults > 0) hud_msg(W.nvaults > 1 ? "THE VAULTS ARE OPEN - IF YOU DARE" : "THE VAULT IS OPEN - IF YOU DARE", powerup_color(PU_SALVAGE));
    W.alarm_t = 0.5f;
    W.time_scale = 0.3f;
    W.beep_sec = 999;
    W.quake_t = 1.5f;
    if (!G.boss_level) game_add_score((int)(5000 * (run_tier() + 1)));
    shake_add(0.8f);
    W.flash = 0.8f;
    W.flash_col = rgba(1, 1, 1, 1);
}

static void countdown_update(float dt) {
    if (!G.reactor_dead || G.escaping || G.failing) return;
    G.countdown -= dt;
    W.alarm_t -= dt;
    if (W.alarm_t <= 0) {
        W.alarm_t = 1.0f;
        snd_play(SND_ALARM, 0.35f, 1.0f);
    }
    if (G.countdown < 10.5f) {
        int sec = (int)ceilf(G.countdown);
        if (sec != W.beep_sec && sec >= 0) {
            W.beep_sec = sec;
            snd_play(SND_BEEP, 0.6f, sec <= 3 ? 1.3f : 1.0f);
        }
    }
    float urgency = 1 - G.countdown / G.countdown_max;
    g_trauma = maxf(g_trauma, 0.08f + 0.22f * urgency);
    W.quake_t -= dt;
    if (W.quake_t <= 0) {
        W.quake_t = frandr(0.8f, 2.4f) * (1.2f - urgency * 0.6f);
        shake_add(0.25f + urgency * 0.3f);
        /* collapsing rock around the player */
        for (int k = 0; k < 2; k++) {
            V2 dir = v2fromang(frand() * TAU);
            RayHit hit;
            V2 from = W.pl.pos;
            if (raycast(from, v2mad(from, dir, 420), &hit)) {
                fx_explosion(hit.p, 22 + frand() * 18, col_lerp(C_ORANGE, C_RED, frand()));
                snd_play_at(SND_EXPL_M, hit.p, 0.5f, frandr(0.7f, 1.0f));
                for (int i = 0; i < 10; i++)
                    fx_debris(hit.p, v2scale(v2add(hit.n, v2scale(v2fromang(frand() * TAU), 0.8f)), 100 + frand() * 200),
                              frand() * TAU, frandr(-6, 6), 3 + frand() * 6, cur_def()->wall, 1.0f);
            }
        }
    }
    if (G.countdown <= 0) {
        G.countdown = 0;
        G.failing = true;
        W.last_src = DS_BLAST;
        W.last_proj = -1;
        G.fail_t = 0;
        stop_charge();
        snd_play(SND_EXPL_L, 1.0f, 0.7f);
        snd_play(SND_EXPL_L, 1.0f, 0.5f);
        hud_msg("THE MINE HAS BEEN DESTROYED", C_RED);
    }
}

static void escape_update(float dt) {
    Player *p = &W.pl;
    if (G.escaping) {
        G.escape_t += dt;
        if (frand() < dt * 14) {
            V2 at = v2add(g_cam.pos, v2(frandr(-600, 600), frandr(-350, 350)));
            fx_explosion(at, 30 + frand() * 40, col_lerp(C_ORANGE, C_RED, frand()));
            if (frand() < 0.4f) snd_play(SND_EXPL_M, 0.35f, frandr(0.6f, 1.0f));
        }
        g_trauma = maxf(g_trauma, 0.3f);
        W.zoom_target = 0.75f;
        if (G.escape_t > 3.4f && G.result == GR_NONE) {
            game_end_level();
            G.result = GR_LEVEL_DONE;
        }
        return;
    }
    if (G.failing) {
        G.fail_t += dt;
        W.flash = minf(1, G.fail_t * 0.8f);
        W.flash_col = rgba(1, 0.95f, 0.85f, 1);
        g_trauma = 1;
        if (frand() < dt * 25) fx_explosion(v2add(g_cam.pos, v2(frandr(-600, 600), frandr(-350, 350))), 40 + frand() * 50, C_ORANGE);
        if (G.fail_t > 3.2f && G.result == GR_NONE) {
            if (G.lives > 0) {
                G.lives--;
                G.result = GR_ESCAPE_FAIL;
            } else {
                snprintf(R.killed_by, sizeof(R.killed_by), "%s", game_killer_text());
                G.result = GR_GAME_OVER;
            }
        }
        return;
    }
    if (!G.reactor_dead || p->dead) return;
    int tx = tx_of(p->pos.x), ty = tx_of(p->pos.y);
    if (tile_in(tx, ty) && (L.flags[ty][tx] & TF_EXIT)) {
        G.escaping = true;
        G.escape_t = 0;
        G.escape_margin = G.countdown;
        stop_charge();
        if (p->burn_voice) snd_loop_set(p->burn_voice, 0, 1);
        snd_play(SND_TELEPORT, 0.8f, 0.8f);
        hud_msg("YOU ESCAPED THE MINE!", C_GREEN);
        fx_ring(p->pos, 10, 200, C_GREEN, 0.8f, 8);
    }
}

void game_end_level(void) {
    Player *p = &W.pl;
    float tier = run_tier();
    int saved = G.hostages_onboard;
    G.hostages_saved = saved;
    G.bonus_shield = (int)p->shield * 10 + (int)p->energy * 5;
    G.bonus_hostage = saved * 1000;
    G.bonus_full = (G.hostages_total > 0 && saved == G.hostages_total) ? (int)(2500 * (tier + 1)) : 0;
    G.bonus_skill = (int)((G.difficulty * 1500 + run_heat() * 800) * (tier + 1));
    int total = G.bonus_shield + G.bonus_hostage + G.bonus_full + G.bonus_skill;
    game_add_score(total);
    /* the hold is banked, and rescued crews and a full rescue pay out salvage too */
    int crew = (int)((saved * 8 + (G.bonus_full > 0 ? 25 * (tier + 1) : 0)) * run_salvage_mult() * mod_salvage_mult() + 0.5f);
    G.cargo_banked = p->cargo + crew;
    G.cargo_bonus = mod_on(MOD_ADRENALINE) && G.escape_margin < 10 ? (p->cargo + 1) / 2 : 0;
    R.salvage += G.cargo_banked + G.cargo_bonus;
    p->cargo = 0;
    G.hostages_onboard = 0;
    run_sector_escaped();
    profile_complete(CH_ESCAPE);
    if (G.escape_margin < 3) profile_complete(CH_CLOSE);
    if (G.cargo_banked + G.cargo_bonus >= 400) profile_complete(CH_HAULER);
    if (G.bonus_full > 0) profile_complete(CH_FULLHOUSE);
}

/* ------------------------------------------------------------ wakes */
/* everything flying over the floor ripples the grid: the ship (a full hold makes a heavier wake),
 * the robots by their mass, and missiles */
static void wakes_update(float dt) {
    Player *p = &W.pl;
    if (!p->dead && !G.escaping)
        grid_wake(p->pos, p->vel, 58 + 10 * sqrtf(p->mass), (p->burning ? 2000 : 1500) * sqrtf(p->mass), dt);
    for (int i = 0; i < MAX_ROBOTS; i++) {
        Robot *r = &W.rob[i];
        if (!r->active || RDEF[r->type].speed <= 0 || !in_view(r->pos, 200)) continue;
        float m = sqrtf(minf(RDEF[r->type].mass, 8));
        /* a cloaked robot only shimmers */
        float vis = RDEF[r->type].cloaked ? 0.35f + 0.65f * r->cloak_vis : 1.0f;
        grid_wake(r->pos, r->vel, r->radius * 2.6f + 18, 1000 * m * vis, dt);
    }
    for (int i = 0; i < MAX_PROJ; i++) {
        Proj *pr = &W.proj[i];
        if (!pr->active) continue;
        int t = pr->type;
        if (t != PR_CONCUSSION && t != PR_HOMING && t != PR_SMART && t != PR_MEGA && t != EP_MISSILE && t != EP_HOMING) continue;
        if (!in_view(pr->pos, 200)) continue;
        grid_wake(pr->pos, pr->vel, t == PR_MEGA ? 60 : 40, t == PR_MEGA ? 1100 : 600, dt);
    }
}

/* ------------------------------------------------------------ camera */
static void camera_update(float dt) {
    Player *p = &W.pl;
    V2 look = p->pos;
    if (!p->dead && !G.escaping) {
        if (g_in.use_stick) look = v2mad(look, g_in.stick_aim, 110);
        else look = v2add(look, v2clamplen(v2scale(v2sub(g_in.aim_world, p->pos), 0.22f), 170));
        look = v2mad(look, p->vel, 0.12f);
    }
    V2 was = g_cam.pos;
    g_cam.pos = v2lerp(g_cam.pos, look, damp_factor(G.escaping ? 1.5f : 7.0f, dt));
    /* the eye lags behind the camera's motion; at rest it is straight overhead again */
    V2 cv = dt > 0 ? v2scale(v2sub(g_cam.pos, was), 1 / dt) : v2(0, 0);
    g_cam.lean = v2lerp(g_cam.lean, v2clamplen(v2scale(cv, -LEAN_LAG), LEAN_MAX), damp_factor(5, dt));
    if (!G.escaping) W.zoom_target = (W.arena_fight ? ARENA_ZOOM : BASE_ZOOM) * (p->burning ? 0.94f : 1.0f);
    g_cam.zoom = lerpf(g_cam.zoom, W.zoom_target, damp_factor(2.5f, dt));
    g_trauma = maxf(0, g_trauma - 1.3f * dt);
    float amt = g_cfg.shake == 0 ? 0 : g_cfg.shake == 1 ? 0.5f : 1.0f;
    float s = g_trauma * g_trauma * 22.0f * amt;
    float t = W.time;
    g_cam.shake = v2(s * (sinf(t * 71.0f) * 0.6f + sinf(t * 43.0f) * 0.4f), s * (sinf(t * 67.0f + 1.3f) * 0.6f + sinf(t * 37.0f) * 0.4f));
    audio_set_listener(p->pos);
}

/* ------------------------------------------------------------ update */
void game_update(float dt) {
    if (G.result != GR_NONE) return;
    if (G.automap) return; /* automap_update turns and pans the map */
    float real_dt = dt;
    W.time_scale = minf(1.0f, W.time_scale + real_dt * 1.4f);
    dt *= W.time_scale;
    W.time += dt;
    G.level_time += real_dt;
    Player *p = &W.pl;
    /* first-mine tutorial hints and contextual tips */
    W.tut_t += real_dt;
    if (G.level == 0 && W.tut_step < 3 && g_prof.runs < 2) {
        static const float at[3] = {1.0f, 8.0f, 15.0f};
        if (W.tut_t > at[W.tut_step]) {
            static const char *kb[3] = {"WASD TO FLY   -   MOUSE TO AIM   -   LEFT BUTTON TO FIRE",
                                        "RIGHT BUTTON FIRES MISSILES   -   HOLD SHIFT FOR AFTERBURNER",
                                        "TAB OPENS THE AUTOMAP   -   FIND THE BLUE KEY TO REACH THE REACTOR"};
            static const char *gp[3] = {"LEFT STICK TO FLY   -   RIGHT STICK TO AIM   -   RIGHT TRIGGER TO FIRE",
                                        "LEFT TRIGGER FIRES MISSILES   -   HOLD A FOR AFTERBURNER",
                                        "BACK OPENS THE AUTOMAP   -   FIND THE BLUE KEY TO REACH THE REACTOR"};
            hud_hint(g_in.use_stick ? gp[W.tut_step] : kb[W.tut_step]);
            W.tut_step++;
        }
    }
    if (!W.low_energy_hint && !p->dead && p->energy < 25) {
        W.low_energy_hint = true;
        hud_hint("ENERGY LOW - RECHARGE AT A YELLOW ENERGY CENTER OR GRAB ENERGY ORBS");
    }
    if (!p->dead && !G.escaping && !G.failing) {
        if (g_in.select == 0) select_primary(PW_LASER, true);
        else if (g_in.select == 1 && p->special >= 0) select_primary(p->special, true);
        else if (g_in.select == 1) select_primary(-1, true);
        if (g_in.cycle_p && p->special >= 0) select_primary(p->primary == PW_LASER ? p->special : PW_LASER, true);
    }
    g_in.select = -1;
    g_in.cycle_p = 0;

    player_update(dt);
    g_in.jettison = false;
    g_in.fire2_pressed = false;
    doors_update(dt);
    W.flow_t -= dt;
    if (W.flow_t <= 0 && !p->dead) {
        W.flow_t = 0.2f;
        flow_compute(L.flow, tx_of(p->pos.x), tx_of(p->pos.y), false);
    }
    W.explore_t -= dt;
    if (W.explore_t <= 0 && !p->dead) {
        W.explore_t = 0.12f;
        explore_update(p->pos, run_hazard() == HZ_BLACKOUT ? 190 : 440);
    }
    W.arena_fight = false;
    robots_update(dt);
    reactor_update(dt);
    matcens_update(dt);
    traps_update(dt);
    W.graze_snd_t -= dt;
    W.hazard_cd -= dt;
    if (W.hazard_acc > 0 && W.hazard_cd <= 0) {
        player_damage(W.hazard_acc, W.hazard_dir, W.hazard_src);
        W.hazard_acc = 0;
        W.hazard_cd = 0.12f;
    }
    nshootable = 0;
    for (int i = 0; i < MAX_PROJ && nshootable < 512; i++)
        if (W.proj[i].active && IS_ENEMY_PROJ(W.proj[i].type) && W.proj[i].hp > 0) shootable[nshootable++] = i;
    for (int i = 0; i < MAX_PROJ; i++)
        if (W.proj[i].active) proj_update(&W.proj[i], dt);
    powerups_update(dt);
    g_in.swap = false;
    hostages_update(dt);
    detonators_update(dt);
    if (W.boss_death_t > 0) {
        W.boss_death_t -= dt;
        if (frand() < dt * 18) {
            V2 at = v2add(W.boss_death_pos, v2scale(v2fromang(frand() * TAU), frand() * 90));
            fx_explosion(at, 30 + frand() * 40, frand() < 0.5f ? C_RED : C_MAGENTA);
            snd_play_at(SND_EXPL_M, at, 0.6f, frandr(0.7f, 1.1f));
            shake_add(0.08f);
        }
        if (W.boss_death_t <= 0) {
            explode(W.boss_death_pos, 260, 0, true, -1, rgba(1, 0.4f, 0.8f, 1), DS_SELF);
            fx_explosion(W.boss_death_pos, 140, C_WHITE);
            start_self_destruct(W.boss_death_pos);
        }
    }
    countdown_update(dt);
    escape_update(dt);
    if (G.chain_t > 0) {
        G.chain_t -= dt;
        if (G.chain_t <= 0) G.chain = 0;
    }
    fx_update(dt);
    wakes_update(dt);
    grid_update(dt);
    hud_update(real_dt);
    camera_update(real_dt);
    if (!G.failing) W.flash = maxf(0, W.flash - dt * 2.5f);
}

/* ------------------------------------------------------------ drawing */
/* where the tops of the walls are drawn: off their feet while the eye leans */
static V2 wall_top(void) { return v2scale(g_cam.lean, -WALL_LEAN); }
static void tops_push(void) { r_push(v2scale(wall_top(), g_cam.zoom), v2(0, 0), 1, 1, 1); }
/* how much of the walls' sides shows, 0 straight overhead */
static float lean_k(void) { return clampf((v2len(wall_top()) - 0.1f) / 6.0f, 0, 1); }

/* a wall's side from its foot a-b up to its shifted top, if the leaning eye sees it */
static void wall_side(V2 a, V2 b, V2 n, Col foot, Col top) {
    V2 T = wall_top();
    if (v2dot(n, T) > -0.01f) return;
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    V2 p[4] = {w2v(a), w2v(b), w2v(v2add(b, T)), w2v(v2add(a, T))};
    Col c[4] = {foot, foot, top, top};
    r_mesh(p, c, 4, idx, 6);
}

static void draw_rock(Col rock, Col accent, Col wall) {
    V2 tl = v2w(v2(-20, -20)), br = v2w(v2(g_virt_w + 20, VIRT_H + 20));
    int x0 = tx_of(tl.x) - 1, x1 = tx_of(br.x) + 1;
    int y0 = tx_of(tl.y) - 1, y1 = tx_of(br.y) + 1;
    Col brk = col_lerp(rock, accent, 0.18f);
    /* the sides first, under the tops: dark at the foot, lit toward the light */
    if (lean_k() > 0) {
        const V2 light = v2norm(v2(-0.55f, -0.65f));
        for (int i = 0; i < L.nsegs; i++) {
            const Seg *s = &L.segs[i];
            if (!rw_visible(v2scale(v2add(s->a, s->b), 0.5f), v2dist(s->a, s->b) * 0.5f + 40)) continue;
            Col c = s->tile >= 0 ? brk : rock;
            float lit = 0.5f + 0.5f * v2dot(s->n, light);
            wall_side(s->a, s->b, s->n, col_mul(c, 0.6f), col_lerp(c, wall, 0.1f + 0.3f * lit));
        }
    }
    tops_push();
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            uint8_t t = tile_in(x, y) ? L.tile[y][x] : T_SOLID;
            if (t == T_EMPTY) continue;
            V2 a = w2v(v2(x * TILE, y * TILE)), b = w2v(v2((x + 1) * TILE, y * TILE));
            V2 c = w2v(v2((x + 1) * TILE, (y + 1) * TILE)), d = w2v(v2(x * TILE, (y + 1) * TILE));
            V2 ua = v2(0, 0), ub = v2(1, 0), uc = v2(1, 1), ud = v2(0, 1);
            switch (t) {
            case T_SOLID: r_rock_quad(a, b, c, d, ua, ub, uc, ud, rock); break;
            case T_BREAK: r_rock_quad(a, b, c, d, ua, ub, uc, ud, brk); break;
            case T_TRI_TL: r_rock_tri(a, b, d, ua, ub, ud, rock); break;
            case T_TRI_TR: r_rock_tri(a, b, c, ua, ub, uc, rock); break;
            case T_TRI_BL: r_rock_tri(a, c, d, ua, uc, ud, rock); break;
            case T_TRI_BR: r_rock_tri(b, c, d, ub, uc, ud, rock); break;
            }
        }
    r_pop();
}

static void draw_floor(void) {
    const LevelDef *def = cur_def();
    V2 tl = v2w(v2(-20, -20)), br = v2w(v2(g_virt_w + 20, VIRT_H + 20));
    int x0 = clampi(tx_of(tl.x), 0, L.w - 1), x1 = clampi(tx_of(br.x), 0, L.w - 1);
    int y0 = clampi(tx_of(tl.y), 0, L.h - 1), y1 = clampi(tx_of(br.y), 0, L.h - 1);
    float t = W.time;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            uint8_t f = L.flags[y][x];
            if (!(f & (TF_ENERGY | TF_EXIT))) continue;
            Col c = (f & TF_ENERGY) ? C_YELLOW : (G.reactor_dead ? C_GREEN : rgba(0.4f, 0.4f, 0.4f, 1));
            float pulse = (f & TF_ENERGY) ? 0.07f + 0.04f * sinf(t * 3 + x + y) : (G.reactor_dead ? 0.12f + 0.1f * sinf(t * 8 - y) : 0.03f);
            V2 a = w2v(v2(x * TILE, y * TILE)), b = w2v(v2((x + 1) * TILE, (y + 1) * TILE));
            r_add_rect(a.x, a.y, b.x - a.x, b.y - a.y, col_a(c, pulse));
            if (f & TF_ENERGY) {
                float sy = fmodf(t * 30 + (x * 7 % 11), TILE);
                rw_line(v2(x * TILE + 4, y * TILE + sy), v2((x + 1) * TILE - 4, y * TILE + sy), 3, col_a(c, 0.35f));
            }
            /* outline edges */
            static const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
            for (int s = 0; s < 4; s++) {
                int nx = x + dx[s], ny = y + dy[s];
                if (tile_in(nx, ny) && (L.flags[ny][nx] & (f & (TF_ENERGY | TF_EXIT)))) continue;
                V2 p0, p1;
                float X0 = x * TILE + 3, Y0 = y * TILE + 3, X1 = (x + 1) * TILE - 3, Y1 = (y + 1) * TILE - 3;
                switch (s) {
                case 0: p0 = v2(X0, Y0); p1 = v2(X1, Y0); break;
                case 1: p0 = v2(X1, Y0); p1 = v2(X1, Y1); break;
                case 2: p0 = v2(X0, Y1); p1 = v2(X1, Y1); break;
                default: p0 = v2(X0, Y0); p1 = v2(X0, Y1); break;
                }
                rw_line(p0, p1, 4, col_a(c, (f & TF_EXIT) && !G.reactor_dead ? 0.15f : 0.55f));
            }
            if ((f & TF_ENERGY) && !(tile_in(x - 1, y) && (L.flags[y][x - 1] & TF_ENERGY)) &&
                !(tile_in(x, y - 1) && (L.flags[y - 1][x] & TF_ENERGY))) {
                V2 lp = w2v(v2((x + 1) * TILE, y * TILE - 12));
                r_text("ENERGY", lp.x, lp.y, 10 * g_cam.zoom, col_a(C_YELLOW, 0.7f), AL_CENTER);
            }
        }
    (void)def;
}

static void draw_walls(Col wall, Col accent) {
    /* the feet of the sides the leaning eye sees, and the edges up their corners */
    float lk = lean_k();
    if (lk > 0) {
        V2 T = wall_top();
        for (int i = 0; i < L.nsegs; i++) {
            const Seg *s = &L.segs[i];
            if (v2dot(s->n, T) > -0.01f) continue;
            if (!rw_visible(v2scale(v2add(s->a, s->b), 0.5f), v2dist(s->a, s->b) * 0.5f + 40)) continue;
            rw_line(s->a, s->b, 3, col_a(wall, 0.35f * lk));
            rw_line(s->a, v2add(s->a, T), 2.5f, col_a(wall, 0.5f * lk));
            rw_line(s->b, v2add(s->b, T), 2.5f, col_a(wall, 0.5f * lk));
        }
    }
    tops_push();
    /* soft inner glow into the rock */
    for (int i = 0; i < L.nsegs; i++) {
        const Seg *s = &L.segs[i];
        V2 mid = v2scale(v2add(s->a, s->b), 0.5f);
        if (!rw_visible(mid, v2dist(s->a, s->b) * 0.5f + 30)) continue;
        V2 in = v2scale(s->n, -16);
        V2 a = w2v(s->a), b = w2v(s->b), c = w2v(v2add(s->b, in)), d = w2v(v2add(s->a, in));
        Col c0 = col_a(wall, s->tile >= 0 ? 0.08f : 0.16f);
        (void)c0;
        r_add_quad(a, b, v2lerp(b, c, 0.5f), v2lerp(a, d, 0.5f), col_a(wall, s->tile >= 0 ? 0.05f : 0.10f));
        r_add_quad(v2lerp(a, d, 0.5f), v2lerp(b, c, 0.5f), c, d, col_a(wall, s->tile >= 0 ? 0.02f : 0.04f));
    }
    static V2 pts[MAX_CPTS];
    static V2 echo[MAX_CPTS];
    float z = g_cam.zoom;
    for (int ci = 0; ci < L.ncontours; ci++) {
        int n = L.clen[ci];
        if (n < 2) continue;
        const V2 *src = &L.cpts[L.cstart[ci]];
        bool closed = L.cclosed[ci];
        for (int k = 0; k < n; k++) {
            pts[k] = w2v(src[k]);
            /* offset into the rock along the miter */
            V2 prev = src[closed ? (k - 1 + n) % n : (k > 0 ? k - 1 : k)];
            V2 next = src[closed ? (k + 1) % n : (k < n - 1 ? k + 1 : k)];
            V2 d0 = v2norm(v2sub(src[k], prev)), d1 = v2norm(v2sub(next, src[k]));
            if (v2len2(d0) < 0.5f) d0 = d1;
            if (v2len2(d1) < 0.5f) d1 = d0;
            V2 n0 = v2perp(d0), n1 = v2perp(d1);
            V2 m = v2norm(v2add(n0, n1));
            if (v2len2(m) < 0.5f) m = n0;
            float dm = maxf(0.4f, v2dot(m, n0));
            echo[k] = w2v(v2mad(src[k], m, -8.0f / dm));
        }
        r_polyline(echo, n, closed, 3.5f * z, col_a(wall, 0.32f));
        r_polyline(pts, n, closed, 8.0f * z, wall);
    }
    /* cracked (breakable) walls: dashed and dim */
    for (int i = 0; i < L.nbreak_segs; i++) {
        const Seg *s = &L.segs[L.break_segs[i]];
        V2 ab = v2sub(s->b, s->a);
        for (int k = 0; k < 6; k++) {
            float t0 = k / 6.0f + 0.03f, t1 = (k + 0.55f) / 6.0f;
            rw_line(v2mad(s->a, ab, t0), v2mad(s->a, ab, t1), 5, col_a(accent, 0.55f));
        }
        /* crack pattern inside the tile */
        int tile = s->tile;
        V2 c = tile_center(tile % L.w, tile / L.w);
        V2 in = v2scale(s->n, -1);
        V2 side = v2perp(in);
        V2 p0 = v2mad(v2scale(v2add(s->a, s->b), 0.5f), in, 2);
        V2 p1 = v2add(v2mad(p0, in, 10), v2scale(side, 6));
        V2 p2 = v2add(v2mad(p1, in, 10), v2scale(side, -8));
        V2 zig[3] = {p0, p1, p2};
        rw_polyline(zig, 3, false, 2.5f, col_a(accent, 0.35f));
        (void)c;
    }
    r_pop();
}

static void draw_doors(void) {
    for (int i = 0; i < L.ndoors; i++) {
        Door *d = &L.doors[i];
        V2 c = door_center(d);
        if (!rw_visible(c, 120)) continue;
        Col col = key_color(d->lock);
        bool locked = !can_open(d);
        float t = W.time;
        SDL_FRect r[2];
        bool slab = door_rects(d, &r[0], &r[1]);
        /* the slabs stand as high as the walls: their sides show while the eye leans */
        if (slab && lean_k() > 0)
            for (int k = 0; k < 2; k++) {
                if (r[k].w < 1 || r[k].h < 1) continue;
                V2 q[4] = {v2(r[k].x, r[k].y), v2(r[k].x + r[k].w, r[k].y), v2(r[k].x + r[k].w, r[k].y + r[k].h), v2(r[k].x, r[k].y + r[k].h)};
                static const V2 N[4] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
                Col foot = rgba(col.r * 0.05f, col.g * 0.05f, col.b * 0.05f, 1), top = rgba(col.r * 0.3f, col.g * 0.3f, col.b * 0.3f, 1);
                for (int e = 0; e < 4; e++) wall_side(q[e], q[(e + 1) % 4], N[e], foot, top);
            }
        tops_push();
        /* frame markers on the walls */
        V2 e0, e1, perp;
        if (d->horiz) {
            e0 = v2(d->x0 * TILE, (d->y0 + 0.5f) * TILE);
            e1 = v2((d->x1 + 1) * TILE, (d->y0 + 0.5f) * TILE);
            perp = v2(0, 1);
        } else {
            e0 = v2((d->x0 + 0.5f) * TILE, d->y0 * TILE);
            e1 = v2((d->x0 + 0.5f) * TILE, (d->y1 + 1) * TILE);
            perp = v2(1, 0);
        }
        rw_line(v2mad(e0, perp, -12), v2mad(e0, perp, 12), 6, col_a(col, 0.8f));
        rw_line(v2mad(e1, perp, -12), v2mad(e1, perp, 12), 6, col_a(col, 0.8f));
        if (!slab) {
            r_pop();
            continue;
        }
        for (int k = 0; k < 2; k++) {
            if (r[k].w < 1 || r[k].h < 1) continue;
            V2 a = w2v(v2(r[k].x, r[k].y)), b = w2v(v2(r[k].x + r[k].w, r[k].y + r[k].h));
            r_fill_rect(a.x, a.y, b.x - a.x, b.y - a.y, rgba(col.r * 0.12f, col.g * 0.12f, col.b * 0.12f, 1));
            r_frame(a.x, a.y, b.x - a.x, b.y - a.y, 4 * g_cam.zoom, col_a(col, locked ? 0.95f : 0.7f));
            /* hazard stripes */
            int stripes = (int)(maxf(r[k].w, r[k].h) / 12);
            for (int s = 0; s < stripes; s++) {
                float f = (s + 0.5f) / stripes;
                V2 p = d->horiz ? v2(r[k].x + r[k].w * f, r[k].y + r[k].h * 0.5f) : v2(r[k].x + r[k].w * 0.5f, r[k].y + r[k].h * f);
                V2 dd = v2(3, 3);
                rw_line(v2sub(p, dd), v2add(p, dd), 2.5f, col_a(col, 0.35f));
            }
        }
        if (d->open < 0.2f && d->lock != LOCK_NONE) {
            /* key diamond / lock emblem */
            float pulse = 0.7f + 0.3f * sinf(t * 4);
            V2 dm[4] = {v2add(c, v2(0, -11)), v2add(c, v2(11, 0)), v2add(c, v2(0, 11)), v2add(c, v2(-11, 0))};
            rw_polyline(dm, 4, true, 4, col_a(col, pulse));
            rw_glow(c, 26, col_a(col, 0.25f * pulse));
            if (d->lock == LOCK_EXIT || d->lock == LOCK_VAULT) {
                V2 v = w2v(v2mad(c, perp, -26));
                r_text(d->lock == LOCK_EXIT ? "EXIT" : "VAULT", v.x, v.y - 5, 10 * g_cam.zoom, col_a(col, 0.9f), AL_CENTER);
            }
        }
        r_pop();
    }
}

static void draw_hostages(void) {
    for (int i = 0; i < W.nhost; i++) {
        Hostage *h = &W.host[i];
        if (!h->active || !rw_visible(h->pos, 40)) continue;
        float t = h->t;
        V2 p = v2add(h->pos, v2(0, sinf(t * 2) * 3));
        Col c = rgba(0.45f, 1, 0.55f, 1);
        V2 head = v2add(p, v2(0, -11));
        rw_circle(head, 4, 3, c, 12);
        rw_line(v2add(p, v2(0, -7)), v2add(p, v2(0, 4)), 3, c);
        float wave = sinf(t * 6) * 5;
        rw_line(v2add(p, v2(0, -4)), v2add(p, v2(-8, -9 - wave)), 3, c);
        rw_line(v2add(p, v2(0, -4)), v2add(p, v2(8, -9 + wave)), 3, c);
        rw_line(v2add(p, v2(0, 4)), v2add(p, v2(-5, 13)), 3, c);
        rw_line(v2add(p, v2(0, 4)), v2add(p, v2(5, 13)), 3, c);
        float ring = fmodf(t * 0.8f, 1.0f);
        rw_circle(p, 14 + ring * 16, 3, col_a(c, (1 - ring) * 0.6f), 28);
        rw_glow(p, 30, col_a(c, 0.12f));
        if (v2dist(h->pos, W.pl.pos) < 520 && fmodf(t, 1.0f) < 0.6f) {
            V2 v = w2v(v2add(p, v2(0, -32)));
            r_text("SOS", v.x, v.y, 10 * g_cam.zoom, col_a(c, 0.9f), AL_CENTER);
        }
    }
}

static void draw_powerups(void) {
    for (int i = 0; i < MAX_POWERUPS; i++) {
        Powerup *pu = &W.pu[i];
        if (!pu->active || !rw_visible(pu->pos, 40)) continue;
        V2 p = v2add(pu->pos, v2(0, sinf(pu->t * 2.5f) * 2.5f));
        float alpha = pu->nopick > 0 ? 0.5f + 0.5f * sinf(pu->t * 30) : 1.0f;
        draw_powerup_icon(pu->type, w2v(p), g_cam.zoom, pu->t, alpha);
    }
}

static void draw_missile_body(V2 pos, V2 vel, float len, Col c) {
    V2 d = v2norm(vel), n = v2perp(d);
    V2 pts[4] = {v2mad(pos, d, len), v2add(v2mad(pos, d, -len * 0.6f), v2scale(n, len * 0.45f)), v2mad(pos, d, -len * 0.3f),
                 v2add(v2mad(pos, d, -len * 0.6f), v2scale(n, -len * 0.45f))};
    rw_polyline(pts, 4, true, 3.5f, c);
    rw_glow(v2mad(pos, d, -len * 0.7f), len * 1.2f, col_a(rgba(1, 0.7f, 0.3f, 1), 0.5f));
}

static void draw_projectiles(void) {
    float t = W.time;
    for (int i = 0; i < MAX_PROJ; i++) {
        Proj *p = &W.proj[i];
        if (!p->active || !rw_visible(p->pos, 60)) continue;
        V2 d = v2norm(p->vel);
        Col c = p->col;
        switch (p->type) {
        case PR_LASER:
            rw_line(v2mad(p->pos, d, -20), v2mad(p->pos, d, 4), 8, c);
            rw_line(v2mad(p->pos, d, -14), v2mad(p->pos, d, 2), 3, col_white(c, 0.7f));
            rw_glow(p->pos, 16, col_a(c, 0.25f));
            break;
        case PR_VULCAN:
            rw_line(v2mad(p->pos, d, -14), p->pos, 4, c);
            break;
        case PR_SPREAD:
            rw_glow(p->pos, 18, col_a(c, 0.55f));
            rw_circle(p->pos, 5, 3, col_white(c, 0.4f), 10);
            break;
        case PR_PLASMA:
            rw_glow(p->pos, 20, col_a(c, 0.6f));
            rw_line(v2mad(p->pos, d, -10), v2mad(p->pos, d, 3), 7, col_white(c, 0.5f));
            break;
        case PR_FUSION: {
            float r = p->radius;
            rw_glow(p->pos, r * 3.2f, col_a(c, 0.55f));
            rw_glow(p->pos, r * 1.4f, col_a(C_WHITE, 0.7f));
            for (int k = 0; k < 3; k++) {
                float a = frand() * TAU;
                V2 e = v2mad(p->pos, v2fromang(a), r * (1.2f + frand()));
                rw_line(p->pos, e, 2.5f, col_a(col_white(c, 0.5f), 0.8f));
            }
        } break;
        case PR_CONCUSSION:
        case PR_HOMING:
        case PR_SMART:
        case EP_MISSILE:
        case EP_HOMING:
            draw_missile_body(p->pos, p->vel, 9, c);
            break;
        case PR_MEGA:
            draw_missile_body(p->pos, p->vel, 14, c);
            rw_glow(p->pos, 40, col_a(c, 0.25f + 0.1f * sinf(t * 20)));
            break;
        case PR_BLOB:
            rw_glow(p->pos, 16, col_a(c, 0.7f));
            rw_glow(p->pos, 6, col_a(C_WHITE, 0.8f));
            break;
        case PR_CARGO: {
            /* a spinning crate, blinking faster as the fuse runs out */
            float r = p->radius, a = p->age * 5;
            float blink = fmodf(t * (4 + 14 * (1 - p->life / 1.1f)), 1.0f) < 0.5f ? 1.0f : 0.35f;
            V2 sq[4];
            for (int k = 0; k < 4; k++) sq[k] = v2add(p->pos, v2scale(v2fromang(a + k * TAU / 4 + PI / 4), r));
            rw_polyline(sq, 4, true, 4, c);
            rw_line(sq[0], sq[2], 2.5f, col_a(c, 0.6f));
            rw_line(sq[1], sq[3], 2.5f, col_a(c, 0.6f));
            rw_glow(p->pos, r * 2.4f, col_a(col_white(c, 0.3f), 0.45f * blink));
            rw_circle(p->pos, r * 1.6f + 20 * (p->life / 1.1f), 2, col_a(c, 0.25f), 24);
        } break;
        case PR_PROX:
        case EP_MINE: {
            float a = t * 3 + i;
            float blink = (p->armed || fmodf(t * 2 + i * 0.3f, 1.0f) < 0.3f) ? 1.0f : 0.3f;
            V2 pts[8];
            for (int k = 0; k < 8; k++) pts[k] = v2add(p->pos, v2scale(v2fromang(a + k * TAU / 8), (k & 1) ? 5 : 10));
            rw_polyline(pts, 8, true, 3, c);
            rw_glow(p->pos, 12, col_a(c, 0.5f * blink));
            if (p->age < 0.6f) rw_circle(p->pos, 14, 2, col_a(c, 0.3f), 16);
        } break;
        case EP_PULSE:
            rw_glow(p->pos, 18, col_a(c, 0.75f));
            rw_glow(p->pos, 6, col_a(C_WHITE, 0.7f));
            break;
        case EP_BOLT:
            rw_line(v2mad(p->pos, d, -12), v2mad(p->pos, d, 3), 7, c);
            rw_glow(p->pos, 12, col_a(c, 0.3f));
            break;
        case EP_NEEDLE:
            rw_line(v2mad(p->pos, d, -18), v2mad(p->pos, d, 3), 5, c);
            rw_line(v2mad(p->pos, d, -10), p->pos, 2, C_WHITE);
            break;
        case EP_VULCAN:
            rw_line(v2mad(p->pos, d, -10), p->pos, 4, c);
            break;
        case EP_ORB: {
            float pul = 0.8f + 0.2f * sinf(t * 20 + i);
            rw_glow(p->pos, 22 * pul, col_a(c, 0.7f));
            rw_circle(p->pos, 7, 3, col_white(c, 0.5f), 12);
        } break;
        case EP_REACTOR:
            rw_glow(p->pos, 20, col_a(c, 0.6f));
            rw_line(v2mad(p->pos, d, -14), v2mad(p->pos, d, 4), 6, col_white(c, 0.4f));
            break;
        case EP_PELLET:
            /* bullet hell needs readable bullets: a coloured halo and a white-hot core */
            rw_glow(p->pos, 15, col_a(c, 0.7f));
            rw_glow(p->pos, 6.5f, col_a(C_WHITE, 0.95f));
            break;
        case EP_SHARD:
            rw_line(v2mad(p->pos, d, -13), v2mad(p->pos, d, 5), 6, c);
            rw_line(v2mad(p->pos, d, -8), v2mad(p->pos, d, 3), 2.5f, C_WHITE);
            rw_glow(p->pos, 11, col_a(c, 0.35f));
            break;
        case EP_FLAK: {
            float pul = 0.75f + 0.25f * sinf(t * 26 + i);
            float fuse = clampf(p->life / 0.3f, 0, 1);
            rw_glow(p->pos, 28 * pul, col_a(c, 0.6f));
            rw_circle(p->pos, 10, 3.5f, col_white(c, 0.4f), 12);
            rw_circle(p->pos, 10 + 22 * (1 - fuse), 2, col_a(c, 0.5f * (1 - fuse)), 16);
        } break;
        case EP_SEEKER: {
            float a = t * 5 + i;
            V2 tri[3];
            for (int k = 0; k < 3; k++) tri[k] = v2add(p->pos, v2scale(v2fromang(a + k * TAU / 3), 11));
            rw_polyline(tri, 3, true, 3, c);
            rw_glow(p->pos, 20, col_a(c, 0.55f + 0.2f * sinf(t * 14 + i)));
            rw_glow(p->pos, 5, col_a(C_WHITE, 0.9f));
        } break;
        }
    }
}

static void draw_player(void) {
    Player *p = &W.pl;
    if (p->dead || G.escaping) return;
    float t = W.time;
    float alpha = 1;
    if (p->spawn_inv > 0 && fmodf(t * 10, 1.0f) < 0.4f) alpha = 0.35f;
    if (p->cloak_t > 0) alpha = 0.18f + 0.1f * sinf(t * 23);
    Col hull = col_a(rgba(0.8f, 0.95f, 1.0f, 1), alpha);
    V2 back = v2add(p->pos, v2rot(v2(-13, 0), p->ang));
    float th = p->thrust;
    if (th > 0.05f) {
        Col ec = p->burning ? rgba(0.5f, 0.8f, 1, 1) : rgba(1, 0.55f, 0.2f, 1);
        rw_glow(back, 10 + th * 12, col_a(ec, 0.55f * alpha * minf(1, th)));
    }
    rw_glow(p->pos, 46, col_a(rgba(0.4f, 0.7f, 1, 1), 0.10f * alpha));
    if (p->phase_t > 0) {
        /* phase shift afterimages trail behind the ship */
        for (int k = 1; k <= 3; k++)
            rw_shape(&SHIP_SHAPE, v2mad(p->pos, p->vel, -0.025f * k), p->ang, 16, 3, col_a(C_CYAN, 0.35f * p->phase_t / 0.5f / k));
        hull = col_white(col_a(C_CYAN, alpha), 0.5f);
    }
    rw_shape(&SHIP_SHAPE, p->pos, p->ang, 16, 5, hull);
    /* the hold: a tail of cargo pods that grows and sways with the mass */
    int pods = (int)ceilf((p->mass - 1.0f) / 0.12f);
    if (pods > 0) {
        Col pc = col_a(powerup_color(PU_SALVAGE), alpha);
        V2 back_dir = v2fromang(p->ang + PI);
        V2 prev = v2mad(p->pos, back_dir, 12);
        for (int k = 0; k < pods && k < 12; k++) {
            float sway = sinf(t * 3 - k * 0.7f) * (2 + k * 1.0f);
            V2 at = v2add(v2mad(p->pos, back_dir, 24 + k * 12.0f), v2scale(v2perp(back_dir), sway));
            rw_line(prev, at, 2.5f, col_a(pc, 0.45f));
            float rr = 6.5f - k * 0.2f;
            V2 dm[4] = {v2add(at, v2(0, -rr)), v2add(at, v2(rr, 0)), v2add(at, v2(0, rr)), v2add(at, v2(-rr, 0))};
            rw_polyline(dm, 4, true, 3, pc);
            rw_glow(at, rr * 2.2f, col_a(pc, 0.18f));
            prev = at;
        }
    }
    if (mod_on(MOD_DRONE)) {
        V2 dp = drone_pos();
        Col dc = col_a(rgba(1, 0.85f, 0.3f, 1), alpha);
        V2 tri[3];
        for (int k = 0; k < 3; k++) tri[k] = v2add(dp, v2scale(v2fromang(p->drone_ang * 3 + k * TAU / 3), 6));
        rw_polyline(tri, 3, true, 3, dc);
        rw_glow(dp, 14, col_a(dc, 0.35f));
    }
    if (p->muzzle_flash > 0) {
        Col mc = p->primary == PW_LASER ? LASER_COL[p->laser_level] : PINFO[p->primary == PW_VULCAN ? PR_VULCAN : p->primary == PW_SPREAD ? PR_SPREAD : p->primary == PW_PLASMA ? PR_PLASMA : PR_FUSION].col;
        rw_glow(ship_local(v2(9, 11.5f)), 10 * p->muzzle_flash, col_a(mc, 0.8f));
        rw_glow(ship_local(v2(9, -11.5f)), 10 * p->muzzle_flash, col_a(mc, 0.8f));
    }
    if (p->charging) {
        float k = clampf(p->fusion_charge / 1.6f, 0, 1);
        bool over = p->fusion_charge > 2.4f;
        Col cc = over ? C_RED : PINFO[PR_FUSION].col;
        V2 nose = ship_local(v2(22, 0));
        rw_glow(nose, 10 + 22 * k + frand() * 4, col_a(cc, 0.7f));
        rw_glow(nose, 4 + 8 * k, col_a(C_WHITE, 0.8f));
        for (int i = 0; i < 2; i++) {
            V2 e = v2mad(nose, v2fromang(frand() * TAU), 8 + 18 * k);
            rw_line(nose, e, 2, col_a(cc, 0.7f));
        }
    }
    if (p->hit_flash > 0) rw_circle(p->pos, 24, 4, col_a(rgba(0.4f, 0.8f, 1, 1), p->hit_flash * 2), 24);
    if (p->hit_dir_t > 0) {
        float a = v2ang(p->hit_dir);
        V2 arc[9];
        for (int i = 0; i < 9; i++) arc[i] = v2add(p->pos, v2scale(v2fromang(a - 0.6f + 1.2f * i / 8), 32));
        rw_polyline(arc, 9, false, 6, col_a(rgba(1, 0.25f, 0.2f, 1), p->hit_dir_t / 0.7f));
    }
    if (p->invuln_t > 0) {
        float blink = p->invuln_t < 3 ? (fmodf(t * 6, 1) < 0.5f ? 1 : 0.2f) : 1;
        for (int k = 0; k < 6; k++) {
            float a0 = t * 2 + k * TAU / 6;
            V2 pts[5];
            for (int j = 0; j < 5; j++) pts[j] = v2add(p->pos, v2scale(v2fromang(a0 + j * 0.12f), 27));
            rw_polyline(pts, 5, false, 3.5f, col_a(C_YELLOW, 0.8f * blink));
        }
    }
    if (p->spawn_inv > 0) rw_circle(p->pos, 26 + sinf(t * 8) * 2, 3, col_a(rgba(0.6f, 0.9f, 1, 1), 0.4f * minf(1, p->spawn_inv)), 28);
}

static void draw_exit_path(void) {
    if (!G.reactor_dead || G.escaping || W.pl.dead) return;
    V2 pts[24];
    int n = flow_path(L.exitflow, W.pl.pos, pts, 24);
    float t = W.time;
    Col gc = rgba(0.35f, 1, 0.45f, 1);
    for (int i = 0; i + 1 < n; i++) {
        V2 a = pts[i], b = pts[i + 1];
        if (v2dist(a, W.pl.pos) < 40) continue;
        V2 d = v2norm(v2sub(b, a));
        if (v2len2(d) < 0.5f) continue;
        V2 nn = v2perp(d);
        float ph = fmodf(t * 3.0f - i * 0.18f, 1.0f);
        float al = (0.35f + 0.65f * (1 - ph)) * clampf(1.3f - i / 24.0f, 0.25f, 1);
        V2 tip = v2mad(v2lerp(a, b, 0.5f), d, 8);
        V2 chev[3] = {v2add(v2mad(tip, d, -14), v2scale(nn, 13)), tip, v2add(v2mad(tip, d, -14), v2scale(nn, -13))};
        rw_polyline(chev, 3, false, 6, col_a(gc, al));
    }
    /* compass arrow around the ship */
    if (n >= 2) {
        V2 target = pts[n > 3 ? 3 : n - 1];
        V2 d = v2norm(v2sub(target, W.pl.pos));
        if (v2len2(d) > 0.5f) {
            V2 nn = v2perp(d);
            V2 tip = v2mad(W.pl.pos, d, 50);
            V2 tri[3] = {tip, v2add(v2mad(tip, d, -12), v2scale(nn, 9)), v2add(v2mad(tip, d, -12), v2scale(nn, -9))};
            rw_polyline(tri, 3, true, 4, col_a(gc, 0.6f + 0.4f * sinf(t * 10)));
        }
    }
}

void game_draw(void) {
    const LevelDef *d = cur_def();
    float t = W.time;
    Col wall = d->wall, grid = d->grid;
    if (G.reactor_dead) {
        /* red alert: the whole mine glows red and throbs */
        float k = 0.5f + 0.5f * sinf(t * 7);
        wall = col_mul(rgba(1, 0.28f, 0.16f, 1), 0.75f + 0.25f * k);
        grid = col_mul(rgba(0.5f, 0.06f, 0.08f, 1), 0.8f + 0.4f * k);
    }
    backdrop_draw(wall, grid, d->rock, d->accent, t);
    grid_draw(grid, 1.0f);
    draw_rock(d->rock, d->accent, wall);
    draw_floor();
    draw_walls(wall, d->accent);
    draw_doors();
    traps_draw();
    matcens_draw();
    draw_powerups();
    draw_hostages();
    reactor_draw();
    robots_draw();
    draw_player();
    draw_projectiles();
    fx_draw();
    draw_exit_path();
    fx_draw_popups();
    if (W.flash > 0) r_add_rect(0, 0, g_virt_w, VIRT_H, col_a(W.flash_col, W.flash * 0.45f));
}
