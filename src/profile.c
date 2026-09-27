/*
 * The pilot record: what survives every run. Progress is horizontal: challenges
 * unlock new ships, new modules for the draft pool, new sector types and the
 * threat protocols, never raw power. Saved as profile.txt.
 */
#include "common.h"

Profile g_prof;
static char profile_path[1024];

/* name, what to do, what it unlocks */
const ChallengeDef CHALLENGES[CH_COUNT] = {
    [CH_ESCAPE] = {"ESCAPE ARTIST", "ESCAPE FROM A MINE", "MODULE: SCRAP ARMOR"},
    [CH_CLOSE] = {"CLOSE CALL", "ESCAPE WITH LESS THAN 3 SECONDS LEFT", "SHIP: KESTREL  +  MODULE: ADRENALINE BANK"},
    [CH_HAULER] = {"HEAVY HAULER", "BANK 400 SALVAGE FROM A SINGLE MINE", "SHIP: MULE"},
    [CH_CHAIN] = {"CHAIN MASTER", "REACH AN X8 KILL CHAIN", "SHIP: WARDEN"},
    [CH_FULLHOUSE] = {"FULL HOUSE", "RESCUE EVERY HOSTAGE IN A MINE", "MODULE: LIFE SUPPORT"},
    [CH_DEMOLITION] = {"DEMOLITION", "DESTROY 3 ROBOTS WITH ONE CARGO BOMB", "MODULE: SHRAPNEL CHARGE"},
    [CH_RAM] = {"BATTERING RAM", "DESTROY 5 ROBOTS BY RAMMING IN ONE MINE", "MODULE: GRAVITY ANCHOR"},
    [CH_VAULT] = {"VAULT BREAKER", "LOOT THE TREASURE OF A VAULT", "SECTORS: DERELICTS  +  MODULE: BOUNTY HUNTER"},
    [CH_SPEED] = {"SPEED DEMON", "DESTROY A REACTOR WITHIN 150 SECONDS", "SECTORS: ARMORIES  +  MODULE: RICOCHET COILS"},
    [CH_DEEP] = {"DEEP DIVER", "REACH THE FOURTH SECTOR OF A RUN", "THREAT PROTOCOLS"},
    [CH_WIN] = {"OVERSEER DOWN", "WIN A SECTOR RUN", "MODULES: DETONATOR ROUNDS, OVERDRIVE"},
    [CH_HOTSTREAK] = {"HOT STREAK", "WIN A SECTOR RUN AT HEAT 3 OR MORE", "MODULE: MISSILE FABRICATOR"},
};

/* the challenge that adds a module to the draft pool, -1: in the pool from the start */
static const int MOD_UNLOCK[MOD_COUNT] = {
    [MOD_HULL] = -1, [MOD_PLATING] = -1, [MOD_REPAIR] = -1, [MOD_PHOENIX] = -1, [MOD_REACTIVE] = -1, [MOD_VAMPIRE] = -1,
    [MOD_CAPACITOR] = -1, [MOD_CALIBRATE] = -1, [MOD_RACKS] = -1, [MOD_QUAD] = -1, [MOD_CLUSTER] = -1,
    [MOD_DETONATOR] = CH_WIN, [MOD_RICOCHET] = CH_SPEED, [MOD_FABRICATOR] = CH_HOTSTREAK,
    [MOD_THRUST] = -1, [MOD_COOLANT] = -1, [MOD_RAM] = -1, [MOD_PHASE] = -1, [MOD_MOMENTUM] = -1,
    [MOD_SCANNER] = -1, [MOD_CELLS] = -1, [MOD_OVERRIDE] = -1, [MOD_MAPPER] = -1, [MOD_DRONE] = -1,
    [MOD_OVERDRIVE] = CH_WIN, [MOD_BOUNTY] = CH_VAULT, [MOD_LIFESUPPORT] = CH_FULLHOUSE,
    [MOD_TRACTOR] = -1, [MOD_DAMPERS] = -1, [MOD_SIPHON] = -1,
    [MOD_ANCHOR] = CH_RAM, [MOD_SCRAP] = CH_ESCAPE, [MOD_SHRAPNEL] = CH_DEMOLITION, [MOD_ADRENALINE] = CH_CLOSE,
};

bool profile_done(int ch) { return ch >= 0 && ch < CH_COUNT && (g_prof.done & (1u << ch)) != 0; }

int ship_challenge(int s) {
    switch (s) {
    case SHIP_MULE: return CH_HAULER;
    case SHIP_KESTREL: return CH_CLOSE;
    case SHIP_WARDEN: return CH_CHAIN;
    default: return -1;
    }
}
bool ship_unlocked(int s) { return ship_challenge(s) < 0 || profile_done(ship_challenge(s)); }
int mod_challenge(int id) { return id >= 0 && id < MOD_COUNT ? MOD_UNLOCK[id] : -1; }
bool mod_unlocked(int id) { return mod_challenge(id) < 0 || profile_done(mod_challenge(id)); }
bool protocols_unlocked(void) { return profile_done(CH_DEEP); }

bool reward_unlocked(int r) {
    if (r == SR_DERELICT) return profile_done(CH_VAULT);
    if (r == SR_ARMORY) return profile_done(CH_SPEED);
    return true;
}

void profile_complete(int ch) {
    if (ch < 0 || ch >= CH_COUNT || profile_done(ch)) return;
    g_prof.done |= 1u << ch;
    profile_save();
    run_note_unlock(ch);
    char buf[96];
    snprintf(buf, sizeof(buf), "CHALLENGE COMPLETE: %s", CHALLENGES[ch].name);
    hud_msg(buf, C_YELLOW);
    snprintf(buf, sizeof(buf), "UNLOCKED %s", CHALLENGES[ch].unlocks);
    hud_msg(buf, rgba(1, 0.85f, 0.45f, 1));
    snd_play(SND_EXTRALIFE, 0.6f, 1.25f);
}

/* ------------------------------------------------------------ file */
void profile_reset(void) {
    memset(&g_prof, 0, sizeof(g_prof));
    g_prof.best_heat = -1;
}

void profile_load(const char *path) {
    snprintf(profile_path, sizeof(profile_path), "%s", path);
    profile_reset();
    FILE *f = fopen(profile_path, "r");
    if (!f) return;
    char key[32];
    long v;
    while (fscanf(f, "%31s %ld", key, &v) == 2) {
        if (!strcmp(key, "done")) g_prof.done = (uint32_t)v;
        else if (!strcmp(key, "runs")) g_prof.runs = (int)v;
        else if (!strcmp(key, "wins")) g_prof.wins = (int)v;
        else if (!strcmp(key, "best_heat")) g_prof.best_heat = (int)v;
        else if (!strcmp(key, "best_sector")) g_prof.best_sector = (int)v;
        else if (!strcmp(key, "best_score")) g_prof.best_score = (int)v;
        else if (!strcmp(key, "salvage")) g_prof.lifetime_salvage = (int)v;
        else if (!strcmp(key, "kills")) g_prof.lifetime_kills = (int)v;
        else if (!strcmp(key, "daily_date")) g_prof.daily_date = (int)v;
        else if (!strcmp(key, "daily_best")) g_prof.daily_best = (int)v;
        else if (!strcmp(key, "daily_tries")) g_prof.daily_tries = (int)v;
        else if (!strcmp(key, "story")) g_prof.seen_story = v != 0;
    }
    fclose(f);
}

void profile_save(void) {
    if (!profile_path[0]) return;
    FILE *f = fopen(profile_path, "w");
    if (!f) return;
    fprintf(f, "done %u\nruns %d\nwins %d\nbest_heat %d\nbest_sector %d\nbest_score %d\nsalvage %d\nkills %d\n", g_prof.done, g_prof.runs,
            g_prof.wins, g_prof.best_heat, g_prof.best_sector, g_prof.best_score, g_prof.lifetime_salvage, g_prof.lifetime_kills);
    fprintf(f, "daily_date %d\ndaily_best %d\ndaily_tries %d\nstory %d\n", g_prof.daily_date, g_prof.daily_best, g_prof.daily_tries,
            g_prof.seen_story);
    fclose(f);
}
