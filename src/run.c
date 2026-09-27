/*
 * A run: five generated sectors and the Overseer's foundry. Between sectors the hangar offers
 * a draft of modules and a shop, and the sector chart lets you pick the next
 * mine: each node pays a bonus (a vault crate, rich salvage, an armory...) and
 * may carry a hazard that pays extra salvage for the trouble.
 *
 * Everything the chart and the drafts show comes from the run seed, so a seed
 * (or the daily challenge) replays the same mines and offers for everyone.
 */
#include "common.h"

Run R;

const ShipDef SHIPS[SHIP_COUNT] = {
    [SHIP_WRAITH] = {"WRAITH-7", "ASSAULT CRAFT", "THE CONSORTIUM'S ALL-ROUNDER.\nSIX MODULE SLOTS.", 100, 100, 1.0f, 1.0f, 1.0f, 6, -1,
                     {0.8f, 0.95f, 1.0f, 1}},
    [SHIP_MULE] = {"MULE", "ORE HAULER", "ARMOURED AND SLUGGISH. CARGO WEIGHS HALF AS MUCH\nAND A RAM PROW COMES INSTALLED. FIVE SLOTS.", 140, 100,
                   0.85f, 0.9f, 0.5f, 5, MOD_RAM, {1.0f, 0.62f, 0.25f, 1}},
    [SHIP_KESTREL] = {"KESTREL", "INTERCEPTOR", "FAST AND FRAGILE, WITH PHASE SHIFT AND A QUICK\nBURNER. CARGO WEIGHS 40% MORE.", 75, 100, 1.22f, 1.5f,
                      1.4f, 6, MOD_PHASE, {0.4f, 1.0f, 0.7f, 1}},
    [SHIP_WARDEN] = {"WARDEN", "GUNSHIP", "BIG ENERGY CELLS AND A GUARDIAN DRONE,\nBUT ONLY FIVE MODULE SLOTS.", 105, 140, 0.95f, 1.0f, 1.0f, 5,
                     MOD_DRONE, {1.0f, 0.4f, 0.9f, 1}},
};

const ProtocolDef PROTOCOLS[TP_COUNT] = {
    [TP_VETERAN] = {"VETERAN ROBOTS", "ROBOTS AIM, FIRE AND HIT LIKE ONE SKILL LEVEL HIGHER.", 2},
    [TP_ELITE] = {"ELITE ROBOTS", "ONE MORE SKILL LEVEL. STACKS WITH VETERAN ROBOTS.", 2},
    [TP_FUSE] = {"SHORT FUSE", "EIGHT SECONDS LESS TO ESCAPE EVERY MINE.", 1},
    [TP_ARMORED] = {"ARMOURED", "ROBOTS HAVE 25% MORE HULL.", 1},
    [TP_SWARM] = {"SWARM", "40% MORE ROBOTS, AND GENERATORS BUILD TWO MORE.", 1},
    [TP_HEAVY] = {"HEAVY CARGO", "CARGO WEIGHS 40% MORE.", 1},
    [TP_FRAGILE] = {"FRAGILE HULL", "EVERY SHIP LAUNCHES WITH 25 LESS SHIELD.", 1},
    [TP_SCARCITY] = {"SCARCITY", "HANGAR PRICES +30%, AND ROBOTS DROP LESS.", 1},
    [TP_LASTSTAND] = {"LAST STAND", "START WITH TWO SHIPS INSTEAD OF THREE.", 1},
};

static const char *REWARD_NAMES[SR_COUNT] = {"R&D LAB", "RICH VEIN", "RESCUE BEACON", "ARMORY", "DERELICT", "THE OVERSEER"};
static const char *REWARD_DESC[SR_COUNT] = {
    "AN R&D CRATE WAITS IN THE DEEPEST VAULT: A FREE MODULE.",
    "50% MORE SALVAGE AND ONE MORE VAULT.",
    "MORE TRAPPED MINERS, AND A SPARE SHIP IN THE DEEPEST VAULT.",
    "TWO SPECIAL WEAPONS AND A LASER UPGRADE NEAR THE START.",
    "A WRECKED RESEARCH SHIP: THE NEXT DRAFT OFFERS ONLY RARE MODULES.",
    "THE FOUNDRY ON CERES. DESTROY THE OVERSEER TO WIN THE RUN.",
};
static const char *HAZARD_NAMES[HZ_COUNT] = {"", "SHORT FUSE", "INFESTED", "HIGH GRAVITY", "OVERCLOCKED", "BLACKOUT", "DRAINED", "ARMOURED", "BOOBY-TRAPPED"};
static const char *HAZARD_DESC[HZ_COUNT] = {
    "",
    "25% LESS TIME TO ESCAPE.  +30% SALVAGE",
    "HALF AGAIN AS MANY ROBOTS.  +30% SALVAGE",
    "CARGO WEIGHS 50% MORE.  +25% SALVAGE",
    "GENERATORS RUN FROM THE START AND BUILD MORE.  +25% SALVAGE",
    "SENSORS DOWN: YOU MAP LESS OF THE MINE AS YOU FLY.  +20% SALVAGE",
    "NO ENERGY CENTERS.  +20% SALVAGE",
    "ROBOTS HAVE 35% MORE HULL.  +25% SALVAGE",
    "TWICE THE TRAPS: VENTS, LASER GATES, MINES.  +25% SALVAGE",
};
static const float HAZARD_SALVAGE[HZ_COUNT] = {1, 1.3f, 1.3f, 1.25f, 1.25f, 1.2f, 1.2f, 1.25f, 1.25f};

const char *reward_name(int r) { return r >= 0 && r < SR_COUNT ? REWARD_NAMES[r] : ""; }
const char *reward_desc(int r) { return r >= 0 && r < SR_COUNT ? REWARD_DESC[r] : ""; }
const char *hazard_name(int h) { return h > 0 && h < HZ_COUNT ? HAZARD_NAMES[h] : ""; }
const char *hazard_desc(int h) { return h > 0 && h < HZ_COUNT ? HAZARD_DESC[h] : ""; }
Col zone_color(int zone) { return ZONES[clampi(zone, 0, NUM_ZONES - 1)].wall; }

const ShipDef *run_ship(void) { return &SHIPS[clampi(R.ship, 0, SHIP_COUNT - 1)]; }
bool run_protocol(int tp) { return (R.heat >> tp) & 1; }
int run_heat(void) {
    int h = 0;
    for (int i = 0; i < TP_COUNT; i++)
        if (run_protocol(i)) h += PROTOCOLS[i].heat;
    return h;
}

/* ------------------------------------------------------------ seeds */
void seed_format(uint32_t seed, char *buf, size_t n) { snprintf(buf, n, "%04X-%04X", (unsigned)(seed >> 16), (unsigned)(seed & 0xFFFF)); }

bool seed_parse(const char *s, uint32_t *out) {
    uint32_t v = 0;
    int digits = 0;
    for (; *s; s++) {
        char c = *s;
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (c == '-' || c == ' ') continue;
        if (d < 0 || digits >= 8) return false;
        v = v << 4 | (uint32_t)d;
        digits++;
    }
    if (!digits) return false;
    *out = v;
    return true;
}

uint32_t daily_seed(int *date_out) {
    SDL_Time now = 0;
    SDL_DateTime dt;
    int date = 20260101;
    if (SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &dt, true)) date = dt.year * 10000 + dt.month * 100 + dt.day;
    if (date_out) *date_out = date;
    return hash32((uint32_t)date * 2654435761u ^ 0xDA11C0DEu);
}

/* ------------------------------------------------------------ the sector chart */
static const char *ZONE_WORDS[3][4] = {
    {"TYCHO", "CLAVIUS", "COPERNICUS", "PLATO"}, {"IO", "PELE", "LOKI", "TVASHTAR"}, {"CERES", "OCCATOR", "AHUNA", "KERWAN"}};
static const char *SITE_WORDS[10] = {"SHAFT", "DIGS", "WORKS", "PIT", "GALLERY", "SMELTER", "CRUCIBLE", "VEIN", "DEEP", "FOUNDRY"};

static int layer_zone(int l) { return l <= 1 ? 0 : l <= 3 ? 1 : 2; }

static int pick_reward(Rng *rng, int layer, const int *taken, int ntaken) {
    static const float W[SR_BOSS] = {3, 3, 1.5f, 2, 2};
    for (int tries = 0; tries < 20; tries++) {
        float tot = 0, w[SR_BOSS];
        for (int r = 0; r < SR_BOSS; r++) {
            w[r] = reward_unlocked(r) ? W[r] : 0;
            if (r == SR_BEACON && layer == 0) w[r] = 0;
            if (r == SR_ARMORY && layer > 3) w[r] = 0;
            if (r == SR_DERELICT && layer == 0) w[r] = 0;
            tot += w[r];
        }
        float x = rng_f(rng) * tot;
        int pick = SR_LAB;
        for (int r = 0; r < SR_BOSS; r++)
            if ((x -= w[r]) <= 0 && w[r] > 0) { pick = r; break; }
        bool dup = false;
        for (int i = 0; i < ntaken; i++) dup |= taken[i] == pick;
        if (!dup || tries == 19) return pick;
    }
    return SR_LAB;
}

static void build_chart(void) {
    Rng rng = rng_make(R.seed, 0xC4A27u);
    static const int NODES[RUN_MAX_LAYERS] = {2, 3, 3, 3, 2, 1};
    R.nlayers = RUN_MAX_LAYERS;
    for (int l = 0; l < R.nlayers; l++) {
        R.nnodes[l] = NODES[l];
        int taken[RUN_MAX_NODES], nt = 0;
        for (int i = 0; i < R.nnodes[l]; i++) {
            SectorNode *n = &R.node[l][i];
            memset(n, 0, sizeof(*n));
            n->seed = rng_u32(&rng);
            n->zone = layer_zone(l);
            if (l == R.nlayers - 1) {
                n->reward = SR_BOSS;
                n->zone = 2;
                n->rooms = 9;
                n->keys = 3;
                snprintf(n->name, sizeof(n->name), "%s", ZONES[2].name);
                continue;
            }
            n->reward = pick_reward(&rng, l, taken, nt);
            taken[nt++] = n->reward;
            float hz = n->reward == SR_DERELICT ? 1.0f : l == 0 ? 0.25f : 0.45f;
            if (rng_f(&rng) < hz) n->hazard = 1 + rng_int(&rng, HZ_COUNT - 1);
            n->rooms = 7 + l + rng_int(&rng, 2);
            static const int KEYS[5][2] = {{1, 1}, {1, 2}, {2, 2}, {2, 2}, {2, 3}};
            n->keys = KEYS[l][rng_int(&rng, 2)];
            snprintf(n->name, sizeof(n->name), "%s %s %c", ZONE_WORDS[n->zone][rng_int(&rng, 4)], SITE_WORDS[rng_int(&rng, 10)],
                     rng_f(&rng) < 0.5f ? 'A' + rng_int(&rng, 6) : '2' + rng_int(&rng, 8));
        }
    }
    /* links: every node leads on, every node can be reached */
    for (int l = 0; l + 1 < R.nlayers; l++) {
        int n = R.nnodes[l], m = R.nnodes[l + 1];
        uint8_t reached = 0;
        for (int i = 0; i < n; i++) {
            int j = n == 1 ? m / 2 : (i * (m - 1) * 2 + (n - 1)) / (2 * (n - 1));
            R.node[l][i].links = (uint8_t)(1 << j);
            int k = j + (rng_f(&rng) < 0.5f ? -1 : 1);
            if (k >= 0 && k < m && rng_f(&rng) < 0.6f) R.node[l][i].links |= (uint8_t)(1 << k);
            reached |= R.node[l][i].links;
        }
        for (int j = 0; j < m; j++) {
            if ((reached >> j) & 1) continue;
            int i = m == 1 ? 0 : (j * (n - 1) * 2 + (m - 1)) / (2 * (m - 1));
            R.node[l][i].links |= (uint8_t)(1 << j);
        }
    }
}

const SectorNode *run_node(int layer, int i) {
    if (layer < 0 || layer >= R.nlayers || i < 0 || i >= R.nnodes[layer]) return NULL;
    return &R.node[layer][i];
}

const SectorNode *run_cur_node(void) {
    const SectorNode *n = run_node(R.layer, R.path[R.layer]);
    return n ? n : run_node(R.layer, 0);
}

bool run_reachable(int layer, int i) {
    if (layer != R.layer || i < 0 || i >= R.nnodes[layer]) return false;
    if (layer == 0) return true;
    const SectorNode *prev = run_node(layer - 1, R.path[layer - 1]);
    return prev && ((prev->links >> i) & 1);
}

int run_first_reachable(void) {
    for (int i = 0; i < R.nnodes[R.layer]; i++)
        if (run_reachable(R.layer, i)) return i;
    return 0;
}

int run_hazard(void) { const SectorNode *n = run_cur_node(); return n ? n->hazard : HZ_NONE; }
int run_reward(void) { const SectorNode *n = run_cur_node(); return n ? n->reward : SR_LAB; }
bool run_final_sector(void) { return R.layer >= R.nlayers - 1; }

float run_tier(void) { return R.layer * 0.45f; }

float run_salvage_mult(void) {
    static const float DIFF[5] = {0.8f, 0.9f, 1.0f, 1.15f, 1.3f};
    float m = DIFF[clampi(G.difficulty, 0, 4)] * HAZARD_SALVAGE[run_hazard()] * (1.0f + 0.05f * run_heat());
    if (run_reward() == SR_RICH) m *= 1.5f;
    return m;
}

float run_cargo_mult(void) { return (run_protocol(TP_HEAVY) ? 1.4f : 1.0f) * (run_hazard() == HZ_GRAVITY ? 1.5f : 1.0f); }

int run_price(int base) { return run_protocol(TP_SCARCITY) ? (base * 13 + 5) / 10 : base; }
int run_module_price(int id) { return run_price(MODS[id].rare ? 140 : 90); }

/* ------------------------------------------------------------ a new run */
void run_new(int ship, int base_diff, uint32_t heat, uint32_t seed, bool daily) {
    memset(&R, 0, sizeof(R));
    R.daily = daily;
    R.seed = seed;
    R.ship = ship_unlocked(ship) || daily ? ship : SHIP_WRAITH;
    R.base_diff = clampi(base_diff, 0, 2);
    R.heat = (protocols_unlocked() || daily) ? heat : 0;
    R.best_escape = -1;
    for (int l = 0; l < RUN_MAX_LAYERS; l++) R.path[l] = -1;
    build_chart();
    const ShipDef *sd = run_ship();
    if (sd->start_mod >= 0) R.mods[sd->start_mod] = 1;
    int diff = R.base_diff + run_protocol(TP_VETERAN) + run_protocol(TP_ELITE);
    game_new(diff, run_protocol(TP_LASTSTAND) ? 2 : 3);
    R.phase = PH_HANGAR;
    R.draft_rare = false;
    run_roll_draft();
    run_roll_shop();
}

/* ------------------------------------------------------------ the hangar's offers */
static float draft_weight(int id, bool rare_only) {
    const ModDef *m = &MODS[id];
    if (!mod_unlocked(id) || R.mods[id] >= m->max_rank) return 0;
    float w = rare_only ? (m->rare ? 1.0f : 0.1f) : (m->rare ? 0.45f : 1.0f);
    if (R.mods[id] > 0) w *= 0.8f;                             /* rank ups */
    else if (mod_installed() >= mod_slots()) w *= 0.6f;        /* would need a free slot */
    return w;
}

static int draw_offers(Rng *rng, int *out, int want, const int *avoid, int navoid, bool rare_only) {
    int n = 0;
    for (int k = 0; k < want; k++) {
        float w[MOD_COUNT], tot = 0;
        for (int i = 0; i < MOD_COUNT; i++) {
            w[i] = draft_weight(i, rare_only);
            for (int j = 0; j < n; j++) if (out[j] == i) w[i] = 0;
            for (int j = 0; j < navoid; j++) if (avoid[j] == i) w[i] = 0;
            tot += w[i];
        }
        if (tot <= 0) break;
        float x = rng_f(rng) * tot;
        int pick = -1;
        for (int i = 0; i < MOD_COUNT && pick < 0; i++)
            if (w[i] > 0 && (x -= w[i]) <= 0) pick = i;
        if (pick < 0) break;
        out[n++] = pick;
    }
    return n;
}

void run_roll_draft(void) {
    Rng rng = rng_make(R.seed, 0xD4AF7u + (uint32_t)R.layer * 131u + (uint32_t)R.rerolls * 7919u);
    R.ndraft = draw_offers(&rng, R.draft, DRAFT_MAX, NULL, 0, R.draft_rare);
    R.draft_taken = false;
}

void run_roll_shop(void) {
    Rng rng = rng_make(R.seed, 0x5409u + (uint32_t)R.layer * 977u);
    int n = draw_offers(&rng, R.shop, 2, R.draft, R.ndraft, false);
    for (int i = n; i < 2; i++) R.shop[i] = -1;
    R.shop_sold[0] = R.shop_sold[1] = false;
}

/* ------------------------------------------------------------ sectors */
static LevelDef gen_def;
static char gen_name[32], gen_sub[64], gen_brief[720];
static const LevelDef *cur_def_ptr = &ZONES[0];

const LevelDef *run_level_def(void) { return cur_def_ptr; }

static void keys_phrase(int keys, char *buf, size_t n) {
    static const char *names[3] = {"BLUE", "YELLOW", "RED"};
    if (keys <= 1) snprintf(buf, n, "- Find the BLUE ACCESS KEY.");
    else if (keys == 2) snprintf(buf, n, "- Find the %s and %s ACCESS KEYS.", names[0], names[1]);
    else snprintf(buf, n, "- Find the %s, %s and %s ACCESS KEYS.", names[0], names[1], names[2]);
}

void run_prepare_sector(void) {
    const SectorNode *node = run_cur_node();
    if (!node) return;
    bool boss = node->reward == SR_BOSS;
    int zone = node->zone;
    MapSpec s;
    memset(&s, 0, sizeof(s));
    s.seed = node->seed;
    s.zone = zone;
    s.depth = R.layer;
    s.rooms = node->rooms;
    s.gw = s.rooms <= 8 ? 5 : 6;
    s.gh = s.rooms <= 9 ? 4 : 5;
    s.keys = node->keys;
    s.boss = boss;
    s.vaults = boss ? 1 : 1 + (R.layer >= 2) + (node->reward == SR_RICH);
    s.hostages = boss ? 4 : 2 + R.layer / 2 + (node->reward == SR_BEACON ? 2 : 0);
    s.generators = boss ? 2 : 1 + (R.layer >= 2) + (node->hazard == HZ_OVERCLOCK);
    static const float DIFF_BUDGET[3] = {0.8f, 0.9f, 1.0f};
    s.budget = (boss ? 46 : 22 + 7.0f * R.layer) * DIFF_BUDGET[R.base_diff] * (node->hazard == HZ_INFESTED ? 1.5f : 1.0f) *
               (run_protocol(TP_SWARM) ? 1.4f : 1.0f);
    s.armory = node->reward == SR_ARMORY;
    s.beacon = node->reward == SR_BEACON;
    s.crate = node->reward == SR_LAB;
    s.drained = node->hazard == HZ_DRAINED;
    s.traps = node->hazard == HZ_TRAPPED ? 2.0f : 1.0f;
    const char **rows;
    MapInfo info;
    int nrows = mapgen_build(&s, &rows, &info);

    gen_def = ZONES[zone];
    gen_def.map = rows;
    gen_def.rows = nrows;
    snprintf(gen_name, sizeof(gen_name), "%s", node->name);
    snprintf(gen_sub, sizeof(gen_sub), "%s", ZONES[zone].subtitle);
    gen_def.name = gen_name;
    gen_def.subtitle = gen_sub;
    if (boss) {
        static const int threats[5] = {RB_SUPERHULK, RB_CARRIER, RB_LANCER, RB_CLOAKER, RB_BOSS};
        memcpy(gen_def.threats, threats, sizeof(threats));
        gen_def.nthreats = 5;
    }

    char keys[96], extra[256] = "";
    keys_phrase(info.keys, keys, sizeof(keys));
    size_t e = 0;
    if (info.hostages) e += snprintf(extra + e, sizeof(extra) - e, "- Rescue the %d trapped miners.\n", info.hostages);
    if (info.vaults)
        e += snprintf(extra + e, sizeof(extra) - e, "- %d vault%s open%s when the %s.\n", info.vaults, info.vaults > 1 ? "s" : "",
                      info.vaults > 1 ? "" : "s", boss ? "Overseer falls" : "reactor blows");
    if (boss) {
        snprintf(gen_brief, sizeof(gen_brief),
                 "The infection originates here. A command unit we\n"
                 "call THE OVERSEER controls every infected robot in\n"
                 "the system. It has sealed itself in the foundry\n"
                 "arena behind the red door.\n\nOBJECTIVES:\n%s\n- Destroy THE OVERSEER.\n%s- Escape before Ceres tears itself apart.",
                 keys, extra);
    } else {
        static const char *OPENERS[3] = {"Survey drones mapped %d chambers before the\nrobots shot them down. Salvage is everywhere.",
                                         "The refinery crews sealed %d halls behind them.\nThe robots are still processing ore - and people.",
                                         "%d chambers deep in the asteroid. The infection\nis strongest here, and so is the pay."};
        char opener[160];
        snprintf(opener, sizeof(opener), OPENERS[zone], info.rooms);
        char hz[48] = "";
        if (node->hazard) snprintf(hz, sizeof(hz), "   -   HAZARD: %s", hazard_name(node->hazard));
        snprintf(gen_brief, sizeof(gen_brief),
                 "%s\n\nOBJECTIVES:\n%s\n- Break the REACTOR CORE: it fights back.\n%s- Bank your cargo at the EXIT TUNNEL.\n\nBONUS: %s%s", opener, keys,
                 extra, reward_name(node->reward), hz);
    }
    gen_def.briefing = gen_brief;
    cur_def_ptr = &gen_def;
    if (R.layer >= 3) profile_complete(CH_DEEP);
}

/* ------------------------------------------------------------ progress */
void run_note_unlock(int ch) {
    for (int i = 0; i < R.nunlocked; i++)
        if (R.unlocked[i] == ch) return;
    if (R.nunlocked < 16) R.unlocked[R.nunlocked++] = ch;
}

void run_sector_escaped(void) {
    R.sectors_done++;
    R.salvage_total += G.cargo_banked + G.cargo_bonus;
    if (R.best_escape < 0 || G.escape_margin < R.best_escape) R.best_escape = G.escape_margin;
    if (G.cargo_banked + G.cargo_bonus > R.best_haul) R.best_haul = G.cargo_banked + G.cargo_bonus;
    R.rescued += G.hostages_saved;
    g_prof.lifetime_salvage += G.cargo_banked + G.cargo_bonus;
}

void run_advance(void) {
    R.draft_rare = run_reward() == SR_DERELICT;
    R.layer++;
    R.phase = PH_HANGAR;
    R.rerolls = 0;
    run_roll_draft();
    run_roll_shop();
}

/* ------------------------------------------------------------ save file */
void run_write(FILE *f) {
    fprintf(f, "daily %d\nseed %u\nship %d\nbase_diff %d\nheat %u\nlayer %d\nphase %d\nsalvage %d\n", R.daily, R.seed, R.ship, R.base_diff,
            R.heat, R.layer, R.phase, R.salvage);
    for (int l = 0; l < RUN_MAX_LAYERS; l++) fprintf(f, "path_%d %d\n", l, R.path[l]);
    fprintf(f, "draft_taken %d\ndraft_rare %d\nrerolls %d\nshop_sold %d\nships_bought %d\nrepairs %d\n", R.draft_taken, R.draft_rare, R.rerolls,
            R.shop_sold[0] | R.shop_sold[1] << 1, R.ships_bought, R.repairs);
    for (int i = 0; i < MOD_COUNT; i++)
        if (R.mods[i] > 0) fprintf(f, "mod_%s %d\n", MODS[i].key, R.mods[i]);
    fprintf(f, "best_escape %d\nrun_time %d\nbest_chain %d\nbest_haul %d\nbest_bomb %d\nkills %d\nrescued %d\nsalvage_total %d\ndeaths %d\nsectors_done %d\n",
            (int)(R.best_escape * 100), (int)R.time, R.best_chain, R.best_haul, R.best_bomb, R.kills, R.rescued, R.salvage_total, R.deaths,
            R.sectors_done);
    for (int i = 0; i < R.nunlocked; i++) fprintf(f, "unlocked %d\n", R.unlocked[i]);
}

/* the chart, draft and shop are rebuilt from the seed once the header is read */
bool run_read_line(const char *key, const char *val) {
    int v = atoi(val);
    if (!strcmp(key, "mode")) {} /* the classic campaign is gone: old saves carry on as a sector run */
    else if (!strcmp(key, "daily")) R.daily = v != 0;
    else if (!strcmp(key, "seed")) R.seed = (uint32_t)strtoul(val, NULL, 10);
    else if (!strcmp(key, "ship")) R.ship = clampi(v, 0, SHIP_COUNT - 1);
    else if (!strcmp(key, "base_diff")) R.base_diff = clampi(v, 0, 2);
    else if (!strcmp(key, "heat")) R.heat = (uint32_t)strtoul(val, NULL, 10);
    else if (!strcmp(key, "layer")) R.layer = clampi(v, 0, RUN_MAX_LAYERS - 1);
    else if (!strcmp(key, "phase")) R.phase = clampi(v, 0, PH_BRIEF);
    else if (!strcmp(key, "salvage")) R.salvage = v > 0 ? v : 0;
    else if (!strncmp(key, "path_", 5)) {
        int l = atoi(key + 5);
        if (l >= 0 && l < RUN_MAX_LAYERS) R.path[l] = clampi(v, -1, RUN_MAX_NODES - 1);
    } else if (!strcmp(key, "draft_taken")) R.draft_taken = v != 0;
    else if (!strcmp(key, "draft_rare")) R.draft_rare = v != 0;
    else if (!strcmp(key, "rerolls")) R.rerolls = clampi(v, 0, 999);
    else if (!strcmp(key, "shop_sold")) { R.shop_sold[0] = v & 1; R.shop_sold[1] = (v >> 1) & 1; }
    else if (!strcmp(key, "ships_bought")) R.ships_bought = clampi(v, 0, 99);
    else if (!strcmp(key, "repairs")) R.repairs = clampi(v, 0, 99);
    else if (!strncmp(key, "mod_", 4)) {
        for (int i = 0; i < MOD_COUNT; i++)
            if (!strcmp(key + 4, MODS[i].key)) R.mods[i] = (uint8_t)clampi(v, 0, MODS[i].max_rank);
    } else if (!strcmp(key, "best_escape")) R.best_escape = v / 100.0f;
    else if (!strcmp(key, "run_time")) R.time = (float)v;
    else if (!strcmp(key, "best_chain")) R.best_chain = v;
    else if (!strcmp(key, "best_haul")) R.best_haul = v;
    else if (!strcmp(key, "best_bomb")) R.best_bomb = v;
    else if (!strcmp(key, "kills")) R.kills = v;
    else if (!strcmp(key, "rescued")) R.rescued = v;
    else if (!strcmp(key, "salvage_total")) R.salvage_total = v;
    else if (!strcmp(key, "deaths")) R.deaths = v;
    else if (!strcmp(key, "sectors_done")) R.sectors_done = v;
    else if (!strcmp(key, "unlocked")) run_note_unlock(v);
    else return false;
    return true;
}

/* after loading: rebuild what the seed determines, keeping the saved progress */
void run_rebuild(void) {
    bool taken = R.draft_taken, s0 = R.shop_sold[0], s1 = R.shop_sold[1];
    build_chart();
    R.layer = clampi(R.layer, 0, R.nlayers - 1);
    run_roll_draft();
    run_roll_shop();
    R.draft_taken = taken;
    R.shop_sold[0] = s0;
    R.shop_sold[1] = s1;
}
