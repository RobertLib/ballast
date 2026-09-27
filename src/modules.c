/*
 * Ship modules: the build of a run. The hangar between sectors offers a draft
 * of modules; each takes one of the ship's slots and some rank up when drafted
 * again. The tags say when a module acts, so combinations can be planned:
 * cargo mass feeds the ram prow and the gravity anchor, kills feed the
 * detonator, the fabricator and the vampire coils, and so on.
 */
#include "common.h"

const char *MOD_CAT_NAMES[MC_COUNT] = {"HULL", "WEAPONS", "ENGINES", "SYSTEMS", "CARGO"};
const char *MOD_TAG_NAMES[TG_COUNT] = {"PASSIVE", "ON KILL", "ON HIT", "ON BURN", "CARGO", "ON PICKUP", "COUNTDOWN"};

/* key, name, label, category, tag, max rank, rare, description */
const ModDef MODS[MOD_COUNT] = {
    [MOD_HULL] = {"hull", "REINFORCED HULL", "HP", MC_HULL, TG_PASSIVE, 3, false, "EVERY SHIP LAUNCHES WITH 20 MORE SHIELD PER RANK."},
    [MOD_PLATING] = {"plating", "ABLATIVE PLATING", "AR", MC_HULL, TG_PASSIVE, 3, false, "REDUCES ALL DAMAGE YOUR SHIP TAKES\nBY 8% PER RANK."},
    [MOD_REPAIR] = {"repair", "NANITE REPAIR", "NR", MC_HULL, TG_PASSIVE, 3, false,
                    "SHIELD SLOWLY REGENERATES AFTER 4 SECONDS\nWITHOUT TAKING A HIT."},
    [MOD_PHOENIX] = {"phoenix", "PHOENIX PROTOCOL", "PX", MC_HULL, TG_HIT, 1, true,
                     "ONCE PER MINE A LETHAL HIT LEAVES YOU AT 30 SHIELD\nWITH 3 SECONDS OF INVULNERABILITY AND A SHOCKWAVE."},
    [MOD_REACTIVE] = {"reactive", "REACTIVE ARMOR", "RA", MC_HULL, TG_HIT, 1, true,
                      "TAKING A HIT RELEASES A PULSE THAT DAMAGES NEARBY\nROBOTS AND ERASES ENEMY SHOTS. 2.5 SECOND COOLDOWN."},
    [MOD_VAMPIRE] = {"vampire", "VAMPIRE COILS", "VC", MC_HULL, TG_KILL, 2, false, "EVERY ROBOT YOU DESTROY RESTORES\n2 SHIELD PER RANK."},

    [MOD_CAPACITOR] = {"capacitor", "CAPACITOR BANKS", "DM", MC_WEAPON, TG_PASSIVE, 3, false, "ALL PRIMARY WEAPONS DEAL 10% MORE\nDAMAGE PER RANK."},
    [MOD_CALIBRATE] = {"calibrate", "LASER CALIBRATION", "L+", MC_WEAPON, TG_PASSIVE, 2, false,
                       "EVERY SHIP LAUNCHES WITH A HIGHER LASER LEVEL.\nDYING NO LONGER COSTS YOU THOSE LEVELS."},
    [MOD_RACKS] = {"racks", "MISSILE RACKS", "MR", MC_WEAPON, TG_PASSIVE, 3, false,
                   "25% MORE ROOM FOR MISSILES AND TWO MORE\nAT LAUNCH PER RANK."},
    [MOD_QUAD] = {"quad", "QUAD PROTOTYPE", "Q4", MC_WEAPON, TG_PASSIVE, 1, true, "EVERY SHIP LAUNCHES WITH QUAD LASERS INSTALLED."},
    [MOD_CLUSTER] = {"cluster", "CLUSTER WARHEADS", "CW", MC_WEAPON, TG_HIT, 1, true,
                     "CONCUSSION AND HOMING MISSILES BURST INTO THREE\nSEEKING BOMBLETS WHEN THEY DETONATE."},
    [MOD_DETONATOR] = {"detonator", "DETONATOR ROUNDS", "DT", MC_WEAPON, TG_KILL, 1, true,
                       "ROBOTS YOU DESTROY EXPLODE AND DAMAGE THEIR\nNEIGHBOURS. THE BLASTS CAN CHAIN."},
    [MOD_RICOCHET] = {"ricochet", "RICOCHET COILS", "RC", MC_WEAPON, TG_PASSIVE, 1, true,
                      "LASER, VULCAN, SPREADFIRE AND PLASMA SHOTS\nBOUNCE OFF WALLS ONCE."},
    [MOD_FABRICATOR] = {"fabricator", "MISSILE FABRICATOR", "MF", MC_WEAPON, TG_KILL, 2, false,
                        "EVERY 8 KILLS (5 AT RANK 2) BUILDS A MISSILE\nFOR YOUR SECONDARY SLOT."},

    [MOD_THRUST] = {"thrust", "THRUSTER TUNING", "TH", MC_ENGINE, TG_PASSIVE, 3, false,
                    "8% MORE THRUST PER RANK: FASTER ACCELERATION\nAND A HIGHER TOP SPEED."},
    [MOD_COOLANT] = {"coolant", "BURNER COOLANT", "BC", MC_ENGINE, TG_BURN, 3, false, "THE AFTERBURNER RECHARGES FASTER\nAND DRAINS SLOWER."},
    [MOD_RAM] = {"ram", "RAM PROW", "RP", MC_ENGINE, TG_BURN, 1, true,
                 "RAMMING ROBOTS WITH THE AFTERBURNER SMASHES THEM.\nTHE HEAVIER YOUR SHIP, THE HARDER IT HITS."},
    [MOD_PHASE] = {"phase", "PHASE SHIFT", "PS", MC_ENGINE, TG_BURN, 1, true,
                   "IGNITING THE AFTERBURNER MAKES YOU INVULNERABLE FOR\nHALF A SECOND. 2.5 SECOND COOLDOWN."},
    [MOD_MOMENTUM] = {"momentum", "MOMENTUM ROUNDS", "MO", MC_ENGINE, TG_PASSIVE, 2, false,
                      "PRIMARY WEAPONS DEAL UP TO 25% MORE DAMAGE PER\nRANK THE FASTER YOUR SHIP IS MOVING."},

    [MOD_SCANNER] = {"scanner", "SALVAGE SCANNER", "SS", MC_SYSTEM, TG_PASSIVE, 3, false,
                     "RECOVER 15% MORE SALVAGE PER RANK FROM\nEVERYTHING YOU DESTROY."},
    [MOD_CELLS] = {"cells", "POWER CELLS", "PC", MC_SYSTEM, TG_PASSIVE, 3, false,
                   "MORE ENERGY AT LAUNCH. ENERGY CENTERS CHARGE\nFASTER AND FURTHER."},
    [MOD_OVERRIDE] = {"override", "COUNTDOWN OVERRIDE", "CO", MC_SYSTEM, TG_COUNTDOWN, 3, false,
                      "DELAYS THE SELF-DESTRUCT: 5 MORE SECONDS\nTO ESCAPE PER RANK."},
    [MOD_MAPPER] = {"mapper", "DEEP SCANNER", "DS", MC_SYSTEM, TG_PASSIVE, 1, true,
                    "THE AUTOMAP STARTS FULLY REVEALED, SHOWING EVERY\nKEY, VAULT, HOSTAGE AND THE REACTOR."},
    [MOD_DRONE] = {"drone", "GUARDIAN DRONE", "GD", MC_SYSTEM, TG_PASSIVE, 2, true,
                   "A COMBAT DRONE ORBITS YOUR SHIP AND SNIPES THE\nNEAREST ROBOT. RANK 2 FIRES TWICE AS FAST."},
    [MOD_OVERDRIVE] = {"overdrive", "OVERDRIVE", "OD", MC_SYSTEM, TG_COUNTDOWN, 1, true,
                       "DURING THE SELF-DESTRUCT YOUR WEAPONS DEAL 35%\nMORE DAMAGE AND YOUR THRUSTERS GAIN 20%."},
    [MOD_BOUNTY] = {"bounty", "BOUNTY HUNTER", "BH", MC_SYSTEM, TG_KILL, 2, false,
                    "KILLS AT AN X3 CHAIN OR MORE DROP 50% MORE\nSALVAGE PER RANK."},
    [MOD_LIFESUPPORT] = {"lifesupport", "LIFE SUPPORT", "LS", MC_SYSTEM, TG_CARGO, 1, true,
                         "HOSTAGES WEIGH NOTHING, AND EACH ONE ON BOARD\nADDS 6% TO YOUR WEAPON DAMAGE."},

    [MOD_TRACTOR] = {"tractor", "TRACTOR BEAM", "TB", MC_CARGO, TG_PICKUP, 3, false, "PULLS POWERUPS AND SALVAGE IN FROM\nFURTHER AWAY."},
    [MOD_DAMPERS] = {"dampers", "INERTIAL DAMPERS", "ID", MC_CARGO, TG_CARGO, 2, false, "CARGO ADDS 30% LESS MASS PER RANK."},
    [MOD_SIPHON] = {"siphon", "SALVAGE SIPHON", "SI", MC_CARGO, TG_PICKUP, 2, false,
                    "EVERY SALVAGE SHARD YOU COLLECT RESTORES\nENERGY, AND SHIELD AT RANK 2."},
    [MOD_ANCHOR] = {"anchor", "GRAVITY ANCHOR", "GA", MC_CARGO, TG_CARGO, 1, true,
                    "THE HEAVIER YOUR SHIP, THE LESS DAMAGE IT TAKES:\nUP TO 40% LESS AT DOUBLE MASS."},
    [MOD_SCRAP] = {"scrap", "SCRAP ARMOR", "SA", MC_CARGO, TG_HIT, 1, true,
                   "HALF OF EVERY HIT IS PAID FROM YOUR CARGO INSTEAD\nOF YOUR SHIELD, AT 2 SALVAGE PER POINT."},
    [MOD_SHRAPNEL] = {"shrapnel", "SHRAPNEL CHARGE", "SC", MC_CARGO, TG_CARGO, 1, true,
                      "JETTISONED CARGO BOMBS BLAST 30% WIDER AND\nSPRAY SIX SEEKING FRAGMENTS."},
    [MOD_ADRENALINE] = {"adrenaline", "ADRENALINE BANK", "AB", MC_CARGO, TG_COUNTDOWN, 1, true,
                        "ESCAPE WITH LESS THAN 10 SECONDS LEFT AND THE\nHOLD PAYS OUT 50% MORE SALVAGE."},
};

Col mod_color(int cat) {
    switch (cat) {
    case MC_HULL: return rgba(0.35f, 0.65f, 1.0f, 1);
    case MC_WEAPON: return rgba(1.0f, 0.38f, 0.3f, 1);
    case MC_ENGINE: return rgba(0.3f, 1.0f, 0.75f, 1);
    case MC_SYSTEM: return rgba(1.0f, 0.8f, 0.25f, 1);
    default: return rgba(1.0f, 0.55f, 0.2f, 1);
    }
}

/* ------------------------------------------------------------ the build */
int mod_rank(int id) { return id >= 0 && id < MOD_COUNT ? R.mods[id] : 0; }
bool mod_on(int id) { return mod_rank(id) > 0; }

int mod_installed(void) {
    int n = 0;
    for (int i = 0; i < MOD_COUNT; i++) n += R.mods[i] > 0;
    return n;
}

int mod_slots(void) { return run_ship()->slots; }

bool mod_add(int id) {
    if (id < 0 || id >= MOD_COUNT) return false;
    if (R.mods[id] > 0) {
        if (R.mods[id] >= MODS[id].max_rank) return false;
        R.mods[id]++;
        return true;
    }
    if (mod_installed() >= mod_slots()) return false;
    R.mods[id] = 1;
    return true;
}

void mod_remove(int id) {
    if (id >= 0 && id < MOD_COUNT) R.mods[id] = 0;
}

/* what a module does at a given rank, for the NOW / NEXT lines of the hangar */
void mod_effect_text(int id, int rank, char *buf, size_t n) {
    if (rank <= 0) { snprintf(buf, n, "-"); return; }
    switch (id) {
    case MOD_HULL: snprintf(buf, n, "+%d LAUNCH SHIELD", 20 * rank); break;
    case MOD_PLATING: snprintf(buf, n, "-%d%% DAMAGE TAKEN", 8 * rank); break;
    case MOD_REPAIR: snprintf(buf, n, "REGENERATE TO %d AT %.1f/S", 25 + 15 * (rank - 1), 1.5f + 0.5f * rank); break;
    case MOD_VAMPIRE: snprintf(buf, n, "+%d SHIELD PER KILL", 2 * rank); break;
    case MOD_CAPACITOR: snprintf(buf, n, "+%d%% PRIMARY DAMAGE", 10 * rank); break;
    case MOD_CALIBRATE: snprintf(buf, n, "LAUNCH WITH LASER LEVEL %d", 1 + rank); break;
    case MOD_RACKS: snprintf(buf, n, "+%d MISSILES, +%d%% CAPACITY", 2 * rank, 25 * rank); break;
    case MOD_FABRICATOR: snprintf(buf, n, "A MISSILE EVERY %d KILLS", rank >= 2 ? 5 : 8); break;
    case MOD_THRUST: snprintf(buf, n, "+%d%% THRUST", 8 * rank); break;
    case MOD_COOLANT: snprintf(buf, n, "+%d%% RECHARGE, -%d%% DRAIN", 30 * rank, 10 * rank); break;
    case MOD_MOMENTUM: snprintf(buf, n, "UP TO +%d%% DAMAGE AT SPEED", 25 * rank); break;
    case MOD_SCANNER: snprintf(buf, n, "+%d%% SALVAGE", 15 * rank); break;
    case MOD_CELLS: snprintf(buf, n, "LAUNCH ENERGY +%d, CHARGE +%d%%", 15 * rank, 25 * rank); break;
    case MOD_OVERRIDE: snprintf(buf, n, "+%d SECONDS TO ESCAPE", 5 * rank); break;
    case MOD_DRONE: snprintf(buf, n, rank >= 2 ? "DRONE FIRES TWICE AS FAST" : "ONE GUARDIAN DRONE"); break;
    case MOD_BOUNTY: snprintf(buf, n, "+%d%% SALVAGE AT X3 CHAIN", 50 * rank); break;
    case MOD_TRACTOR: snprintf(buf, n, "PICKUP RANGE +%d%%", (int)(45.0f * rank / 80.0f * 100 + 0.5f)); break;
    case MOD_DAMPERS: snprintf(buf, n, "-%d%% CARGO MASS", 30 * rank); break;
    case MOD_SIPHON: snprintf(buf, n, rank >= 2 ? "+ENERGY AND SHIELD PER SHARD" : "+ENERGY PER SHARD"); break;
    default: snprintf(buf, n, "INSTALLED"); break;
    }
}

/* hexagon (octagon for rare modules) with the module's glyph */
void mod_draw_badge(int id, V2 c, float r, float alpha, bool lit) {
    const ModDef *m = &MODS[id];
    Col col = col_a(mod_color(m->cat), alpha);
    int n = m->rare ? 8 : 6;
    float rot = m->rare ? PI / 8 : 0;
    V2 p[8];
    for (int i = 0; i < n; i++) p[i] = v2add(c, v2scale(v2fromang(rot + i * TAU / n), r));
    Col fill = lit ? rgba(col.r * 0.18f, col.g * 0.18f, col.b * 0.18f, 0.92f * alpha) : rgba(0.01f, 0.015f, 0.04f, 0.9f * alpha);
    for (int i = 0; i < n; i++) r_fill_tri(c, p[i], p[(i + 1) % n], fill);
    if (lit) r_glow(c, r * 1.9f, col_a(col, 0.18f));
    r_polyline(p, n, true, maxf(2, r * 0.13f), lit ? col : col_a(col, 0.55f));
    r_text(m->label, c.x, c.y - r * 0.3f, r * 0.42f, lit ? col_white(col, 0.6f) : col_a(col, 0.7f), AL_CENTER);
}

/* ------------------------------------------------------------ gameplay modifiers */
float mod_start_shield(void) { return run_ship()->shield + 20.0f * mod_rank(MOD_HULL) - (run_protocol(TP_FRAGILE) ? 25 : 0); }
float mod_damage_taken(void) { return 1.0f - 0.08f * mod_rank(MOD_PLATING); }
float mod_repair_cap(void) { int r = mod_rank(MOD_REPAIR); return r ? 25.0f + 15.0f * (r - 1) : 0; }
float mod_repair_rate(void) { return 1.5f + 0.5f * mod_rank(MOD_REPAIR); }
float mod_primary_dmg(void) { return 1.0f + 0.10f * mod_rank(MOD_CAPACITOR); }
int mod_base_laser(void) { return 1 + mod_rank(MOD_CALIBRATE); }
int mod_start_missiles(void) { return 2 * mod_rank(MOD_RACKS); }
float mod_missile_cap(void) { return 1.0f + 0.25f * mod_rank(MOD_RACKS); }
float mod_accel(void) { return run_ship()->accel * (1.0f + 0.08f * mod_rank(MOD_THRUST)); }
float mod_burner_regen(void) { return run_ship()->burner * (1.0f + 0.3f * mod_rank(MOD_COOLANT)); }
float mod_burner_drain(void) { return 1.0f - 0.1f * mod_rank(MOD_COOLANT); }
float mod_magnet(void) { return 80.0f + 45.0f * mod_rank(MOD_TRACTOR); }
float mod_salvage_magnet(void) { return 130.0f + 60.0f * mod_rank(MOD_TRACTOR); }
float mod_salvage_mult(void) { return 1.0f + 0.15f * mod_rank(MOD_SCANNER); }
float mod_start_energy(void) { return run_ship()->energy + 15.0f * mod_rank(MOD_CELLS); }
float mod_energy_center_rate(void) { return 1.0f + 0.25f * mod_rank(MOD_CELLS); }
float mod_countdown_bonus(void) { return 5.0f * mod_rank(MOD_OVERRIDE); }
float mod_cargo_k(void) { return run_ship()->cargo * (1.0f - 0.3f * mod_rank(MOD_DAMPERS)); }
