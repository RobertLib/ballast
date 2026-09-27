/*
 * HUD, message queue and powerup icons.
 */
#include "game_internal.h"

/* ------------------------------------------------------------ messages */
typedef struct { char text[96]; Col col; float t; } Msg;
#define MAXMSG 5
static Msg msgs[MAXMSG];

static char hint_text[128];
static float hint_t = 0;

void hud_msg_clear(void) {
    memset(msgs, 0, sizeof(msgs));
    hint_t = 0;
}

void hud_hint(const char *text) {
    snprintf(hint_text, sizeof(hint_text), "%s", text);
    hint_t = 6.0f;
}

void hud_msg(const char *text, Col c) {
    /* refresh duplicates instead of stacking them */
    for (int i = 0; i < MAXMSG; i++)
        if (msgs[i].t > 0 && strcmp(msgs[i].text, text) == 0) {
            msgs[i].t = 3.2f;
            return;
        }
    for (int i = MAXMSG - 1; i > 0; i--) msgs[i] = msgs[i - 1];
    snprintf(msgs[0].text, sizeof(msgs[0].text), "%s", text);
    msgs[0].col = c;
    msgs[0].t = 3.2f;
}

void hud_update(float dt) {
    for (int i = 0; i < MAXMSG; i++) msgs[i].t = maxf(0, msgs[i].t - dt);
    hint_t = maxf(0, hint_t - dt);
}

void hud_draw_messages(void) {
    float y = 128;
    for (int i = 0; i < MAXMSG; i++) {
        if (msgs[i].t <= 0) continue;
        float a = clampf(msgs[i].t / 0.5f, 0, 1);
        float pop = msgs[i].t > 3.0f ? 1 + (msgs[i].t - 3.0f) * 1.5f : 1;
        float size = (i == 0 ? 15 : 12) * pop;
        r_text(msgs[i].text, g_virt_w * 0.5f, y, size, col_a(msgs[i].col, a * (i == 0 ? 1 : 0.7f)), AL_CENTER);
        y += size * 1.7f;
    }
}

/* ------------------------------------------------------------ powerup icons */
Col powerup_color(int type) {
    switch (type) {
    case PU_SHIELD: return rgba(0.3f, 0.6f, 1, 1);
    case PU_ENERGY: return rgba(1, 0.85f, 0.2f, 1);
    case PU_LASER: return rgba(1, 0.35f, 0.3f, 1);
    case PU_QUAD: return rgba(0.4f, 1, 0.5f, 1);
    case PU_VULCAN: return rgba(1, 0.8f, 0.4f, 1);
    case PU_SPREAD: return rgba(0.4f, 0.65f, 1, 1);
    case PU_PLASMA: return rgba(0.3f, 1, 0.6f, 1);
    case PU_FUSION: return rgba(0.75f, 0.45f, 1, 1);
    case PU_CONC: return rgba(1, 0.6f, 0.25f, 1);
    case PU_HOMING: return rgba(0.4f, 1, 0.85f, 1);
    case PU_PROX: return rgba(1, 0.35f, 0.35f, 1);
    case PU_SMART: return rgba(1, 0.45f, 1, 1);
    case PU_MEGA: return rgba(1, 0.3f, 0.2f, 1);
    case PU_CLOAK: return rgba(0.7f, 0.8f, 1, 1);
    case PU_INVULN: return rgba(1, 0.95f, 0.4f, 1);
    case PU_LIFE: return rgba(0.5f, 1, 0.6f, 1);
    case PU_SALVAGE: return rgba(1, 0.68f, 0.2f, 1);
    case PU_TREASURE: return rgba(1, 0.85f, 0.3f, 1);
    case PU_CRATE: return rgba(1, 0.35f, 0.9f, 1);
    case PU_KEY_BLUE: return key_color(LOCK_BLUE);
    case PU_KEY_YELLOW: return key_color(LOCK_YELLOW);
    case PU_KEY_RED: return key_color(LOCK_RED);
    }
    return C_WHITE;
}

const char *powerup_name(int type) {
    static const char *names[PU_TYPES] = {"SHIELD ORB", "ENERGY ORB", "LASER UPGRADE", "QUAD LASERS", "VULCAN CANNON",
                                          "SPREADFIRE CANNON", "PLASMA CANNON", "FUSION CANNON", "CONCUSSION MISSILES",
                                          "HOMING MISSILES", "PROXIMITY BOMBS", "SMART MISSILES", "MEGA MISSILE",
                                          "CLOAKING DEVICE", "INVULNERABILITY", "EXTRA LIFE", "SALVAGE", "VAULT TREASURE", "R&D CRATE",
                                          "BLUE KEY", "YELLOW KEY", "RED KEY"};
    return type >= 0 && type < PU_TYPES ? names[type] : "?";
}

static void hex(V2 c, float r, float rot, float w, Col col) {
    V2 p[6];
    for (int i = 0; i < 6; i++) p[i] = v2add(c, v2scale(v2fromang(rot + i * TAU / 6), r));
    r_polyline(p, 6, true, w, col);
}

void draw_powerup_icon(int type, V2 c, float s, float t, float alpha) {
    Col col = col_a(powerup_color(type), alpha);
    if (type == PU_TREASURE) {
        /* a heap of shards under a pulsing halo */
        float pulse = 0.8f + 0.2f * sinf(t * 4);
        r_glow(c, 46 * s * pulse, col_a(col, 0.3f));
        for (int k = 0; k < 5; k++) {
            V2 o = v2add(c, v2scale(v2fromang(k * TAU / 5 + t * 0.6f), (k ? 9 : 0) * s));
            float r = (k ? 6 : 9) * s, rot = t * 2 + k;
            V2 dm[4] = {v2add(o, v2scale(v2fromang(rot), r)), v2add(o, v2scale(v2fromang(rot + PI / 2), r * 0.55f)),
                        v2add(o, v2scale(v2fromang(rot + PI), r)), v2add(o, v2scale(v2fromang(rot + PI * 1.5f), r * 0.55f))};
            r_polyline(dm, 4, true, 3 * s, col_white(col, 0.3f));
        }
        r_circle(c, 20 * s * pulse, 2.5f * s, col_a(col, 0.5f), 24);
        return;
    }
    if (type == PU_CRATE) {
        float rot = sinf(t * 1.5f) * 0.3f;
        V2 sq[4];
        for (int i = 0; i < 4; i++) sq[i] = v2add(c, v2scale(v2fromang(rot + PI / 4 + i * TAU / 4), 16 * s));
        r_glow(c, 40 * s, col_a(col, 0.25f));
        r_polyline(sq, 4, true, 4 * s, col);
        r_line(sq[0], sq[2], 2 * s, col_a(col, 0.4f));
        r_text("R&D", c.x, c.y - 3.5f * s, 6.5f * s, col_white(col, 0.5f), AL_CENTER);
        return;
    }
    if (type == PU_SALVAGE) {
        /* small spinning shard instead of a full powerup badge */
        float r = 6 * s, rot = t * 4;
        V2 dm[4] = {v2add(c, v2scale(v2fromang(rot), r)), v2add(c, v2scale(v2fromang(rot + PI / 2), r * 0.55f)),
                    v2add(c, v2scale(v2fromang(rot + PI), r)), v2add(c, v2scale(v2fromang(rot + PI * 1.5f), r * 0.55f))};
        r_glow(c, 14 * s, col_a(col, 0.3f));
        r_polyline(dm, 4, true, 2.5f * s, col_white(col, 0.2f));
        return;
    }
    float pulse = 0.85f + 0.15f * sinf(t * 5);
    r_glow(c, 30 * s, col_a(col, 0.18f * pulse));
    const char *label = NULL;
    switch (type) {
    case PU_SHIELD:
        r_circle(c, 11 * s, 4 * s, col, 20);
        r_arc(c, 6 * s, t * 3, t * 3 + 4.0f, 3 * s, col_white(col, 0.4f), 10);
        break;
    case PU_ENERGY: {
        r_circle(c, 11 * s, 4 * s, col, 20);
        V2 bolt[5] = {v2add(c, v2(2 * s, -8 * s)), v2add(c, v2(-4 * s, 1 * s)), v2add(c, v2(2 * s, 1 * s)),
                      v2add(c, v2(-2 * s, 8 * s)), v2add(c, v2(4 * s, -1 * s))};
        r_polyline(bolt, 5, false, 3 * s, col_white(col, 0.3f));
    } break;
    case PU_KEY_BLUE:
    case PU_KEY_YELLOW:
    case PU_KEY_RED: {
        float rot = t * 1.5f;
        V2 dm[4];
        for (int i = 0; i < 4; i++) dm[i] = v2add(c, v2scale(v2fromang(rot + i * TAU / 4), 14 * s));
        r_polyline(dm, 4, true, 5 * s, col);
        r_circle(c, 5 * s, 3 * s, col_white(col, 0.4f), 12);
        r_glow(c, 42 * s, col_a(col, 0.25f * pulse));
        label = "KEY";
    } break;
    case PU_LIFE: {
        r_circle(c, 13 * s, 3.5f * s, col, 20);
        game_draw_ship_icon(c, -PI / 2, 7 * s, col_white(col, 0.3f));
    } break;
    case PU_CLOAK:
    case PU_INVULN: {
        V2 sq[4];
        for (int i = 0; i < 4; i++) sq[i] = v2add(c, v2scale(v2fromang(PI / 4 + i * TAU / 4), 14 * s));
        r_polyline(sq, 4, true, 4 * s, col);
        label = type == PU_CLOAK ? "CL" : "IN";
    } break;
    default:
        hex(c, 13 * s, t * 0.8f, 4 * s, col);
        switch (type) {
        case PU_LASER: label = "L+"; break;
        case PU_QUAD: label = "Q4"; break;
        case PU_VULCAN: label = "VC"; break;
        case PU_SPREAD: label = "SF"; break;
        case PU_PLASMA: label = "PL"; break;
        case PU_FUSION: label = "FU"; break;
        case PU_CONC: label = "CM"; break;
        case PU_HOMING: label = "HM"; break;
        case PU_PROX: label = "PB"; break;
        case PU_SMART: label = "SM"; break;
        case PU_MEGA: label = "MG"; break;
        }
        break;
    }
    if (label && type != PU_KEY_BLUE && type != PU_KEY_YELLOW && type != PU_KEY_RED)
        r_text(label, c.x, c.y - 4.5f * s, 9 * s, col_white(col, 0.5f), AL_CENTER);
}

/* the salvage shard glyph followed by an amount */
void draw_salvage(const char *text, float x, float y, float size, Col c, int align) {
    float dw = size * 1.1f;
    float w = dw + text_width(text, size);
    float x0 = align == AL_CENTER ? x - w * 0.5f : align == AL_RIGHT ? x - w : x;
    V2 m = v2(x0 + size * 0.4f, y + size * 0.5f);
    float r = size * 0.55f;
    V2 dm[4] = {v2add(m, v2(0, -r)), v2add(m, v2(r * 0.6f, 0)), v2add(m, v2(0, r)), v2add(m, v2(-r * 0.6f, 0))};
    r_polyline(dm, 4, true, maxf(2.2f, size * 0.22f), c);
    r_glow(m, size * 1.4f, col_a(c, 0.25f));
    r_text(text, x0 + dw, y, size, c, AL_LEFT);
}

/* ------------------------------------------------------------ HUD */
static void bar(float x, float y, float w, float h, float frac, float frac2, Col c, int segs) {
    float gap = 2;
    float sw = (w - gap * (segs - 1)) / segs;
    for (int i = 0; i < segs; i++) {
        float f0 = (float)i / segs;
        float sx = x + i * (sw + gap);
        bool on = f0 < frac;
        bool on2 = f0 < frac2;
        if (on) r_add_rect(sx, y, sw, h, col_a(c, 0.85f));
        else if (on2) r_add_rect(sx, y, sw, h, col_a(c, 0.35f));
        else r_add_rect(sx, y, sw, h, col_a(c, 0.08f));
    }
}

const char *objective_text(void) {
    if (G.reactor_dead) return W.nvaults > 0 ? "ESCAPE! FOLLOW THE GREEN ARROWS - OR RAID THE VAULTS FIRST" : "ESCAPE! FOLLOW THE GREEN ARROWS TO THE EXIT";
    int need = W.level_keys & ~W.pl.keys;
    if (need & 1) return "FIND THE BLUE ACCESS KEY";
    if (need & 2) return "FIND THE YELLOW ACCESS KEY";
    if (need & 4) return "FIND THE RED ACCESS KEY";
    return G.boss_level ? "DESTROY THE OVERSEER" : "DESTROY THE REACTOR CORE";
}

void game_hud_draw(void) {
    Player *p = &W.pl;
    float VW = g_virt_w;
    float t = W.time;
    Col dim = rgba(0.55f, 0.7f, 0.9f, 0.8f);

    /* low shield / countdown vignette */
    float danger = 0;
    if (!p->dead && p->shield < 30) danger = (1 - p->shield / 30) * (0.5f + 0.5f * sinf(t * 6));
    if (G.reactor_dead && !G.escaping) danger = maxf(danger, 0.45f + 0.35f * sinf(t * 7));
    if (danger > 0.01f) {
        Col rc = rgba(1, 0.1f, 0.05f, 0.35f * danger);
        float bw = 90;
        for (int i = 0; i < 6; i++) {
            float k = 1 - i / 6.0f;
            float o = i * bw / 6;
            Col cc = col_a(rc, k * k * 0.5f);
            r_add_rect(o, 0, bw / 6, VIRT_H, cc);
            r_add_rect(VW - o - bw / 6, 0, bw / 6, VIRT_H, cc);
            r_add_rect(0, o, VW, bw / 6, cc);
            r_add_rect(0, VIRT_H - o - bw / 6, VW, bw / 6, cc);
        }
    }

    /* score */
    r_text("SCORE", 26, 20, 11, dim, AL_LEFT);
    r_textf(26, 38, 26, C_WHITE, AL_LEFT, "%d", G.score);
    if (G.chain >= 5) {
        int mult = 1 + (G.chain - 1 < 28 ? G.chain - 1 : 28) / 4;
        Col cc = col_lerp(C_YELLOW, C_ORANGE, clampf((mult - 2) / 6.0f, 0, 1));
        r_textf(26, 80, 16, cc, AL_LEFT, "X%d CHAIN", mult);
        r_add_rect(26, 104, 120 * clampf(G.chain_t / 3.2f, 0, 1), 3, col_a(cc, 0.8f));
    } else if (G.chain > 0) {
        r_textf(26, 80, 12, dim, AL_LEFT, "CHAIN %d", G.chain);
    }

    /* lives + level */
    for (int i = 0; i < G.lives && i < 8; i++) game_draw_ship_icon(v2(VW - 34 - i * 30, 40), -PI / 2, 10, rgba(0.7f, 0.9f, 1, 0.9f));
    if (G.lives > 8) r_textf(VW - 280, 34, 12, dim, AL_RIGHT, "%d", G.lives);
    r_textf(VW - 26, 64, 11, dim, AL_RIGHT, "SECTOR %d/%d  %s", G.level + 1, R.nlayers, cur_def()->name);
    int hz = run_hazard();
    if (hz) r_textf(VW - 26, 82, 10, col_a(C_ORANGE, 0.85f), AL_RIGHT, "%s  -  %s", hazard_name(hz), DIFF_NAMES[G.difficulty]);
    else r_textf(VW - 26, 82, 10, col_a(dim, 0.7f), AL_RIGHT, "%s%s", DIFF_NAMES[G.difficulty], run_heat() ? "  +HEAT" : "");
    {
        /* banked salvage; the hold's contents flash as shards come in */
        static int shown = -1;
        static float flash = 0, last_t = 0;
        flash = maxf(0, flash - (g_time - last_t) * 2.5f);
        last_t = g_time;
        if (p->cargo > shown && shown >= 0) flash = 1;
        shown = p->cargo;
        char sb[24];
        snprintf(sb, sizeof(sb), "%d", R.salvage);
        draw_salvage(sb, VW - 26, 100, 13, col_a(powerup_color(PU_SALVAGE), 0.8f), AL_RIGHT);
        if (p->cargo > 0)
            r_textf(VW - 26, 120, 10 + flash * 2, col_white(powerup_color(PU_SALVAGE), flash * 0.6f), AL_RIGHT, "+%d IN THE HOLD", p->cargo);
    }

    /* keys + hostages, top centre */
    float cx = VW * 0.5f;
    for (int k = 0; k < 3; k++) {
        int bit = 1 << k;
        if (!(W.level_keys & bit)) continue;
        Col kc = key_color(LOCK_BLUE + k);
        V2 c = v2(cx - 150 + k * 34, 30);
        bool have = (p->keys & bit) != 0;
        V2 dm[4] = {v2add(c, v2(0, -11)), v2add(c, v2(11, 0)), v2add(c, v2(0, 11)), v2add(c, v2(-11, 0))};
        r_polyline(dm, 4, true, 4, col_a(kc, have ? 1.0f : 0.3f));
        if (have) {
            r_glow(c, 22, col_a(kc, 0.35f));
            V2 inner[4] = {v2add(c, v2(0, -5)), v2add(c, v2(5, 0)), v2add(c, v2(0, 5)), v2add(c, v2(-5, 0))};
            r_polyline(inner, 4, true, 4, kc);
        }
    }
    {
        Col hc = rgba(0.45f, 1, 0.55f, 1);
        V2 c = v2(cx + 60, 30);
        r_circle(v2add(c, v2(0, -7)), 3, 2.5f, hc, 10);
        r_line(v2add(c, v2(0, -3)), v2add(c, v2(0, 5)), 2.5f, hc);
        r_line(v2add(c, v2(-6, -1)), v2add(c, v2(6, -1)), 2.5f, hc);
        r_line(v2add(c, v2(0, 5)), v2add(c, v2(-4, 11)), 2.5f, hc);
        r_line(v2add(c, v2(0, 5)), v2add(c, v2(4, 11)), 2.5f, hc);
        r_textf(cx + 76, 22, 14, hc, AL_LEFT, "%d ON BOARD", G.hostages_onboard);
        int remaining = 0;
        for (int i = 0; i < W.nhost; i++)
            if (W.host[i].active) remaining++;
        r_textf(cx + 76, 42, 9, col_a(hc, 0.6f), AL_LEFT, "%d OF %d STILL TRAPPED", remaining, G.hostages_total);
    }
    r_text(objective_text(), cx, 64, 10, col_a(G.reactor_dead ? C_GREEN : dim, G.reactor_dead ? 0.6f + 0.4f * sinf(t * 6) : 0.75f), AL_CENTER);

    /* countdown */
    if (G.reactor_dead && !G.escaping) {
        float cd = maxf(0, G.countdown);
        Col cc = cd < 10 ? (fmodf(t * 4, 1) < 0.5f ? C_RED : C_WHITE) : rgba(1, 0.35f, 0.25f, 1);
        r_text("SELF-DESTRUCT", cx, 88, 11, col_a(cc, 0.8f), AL_CENTER);
        char buf[32];
        snprintf(buf, sizeof(buf), "%02d.%d", (int)cd, (int)(fmodf(cd, 1.0f) * 10));
        r_text_glow(buf, cx, 104, 34, cc, AL_CENTER);
    }

    /* boss / reactor bars */
    if (W.boss_idx >= 0 && W.rob[W.boss_idx].active && W.rob[W.boss_idx].type == RB_BOSS && W.rob[W.boss_idx].aware) {
        Robot *b = &W.rob[W.boss_idx];
        float f = clampf(b->hp / b->maxhp, 0, 1);
        r_text("THE OVERSEER", cx, VIRT_H - 94, 12, C_MAGENTA, AL_CENTER);
        bar(cx - 220, VIRT_H - 74, 440, 8, f, f, C_MAGENTA, 40);
    } else if (W.reactor.exists && !W.reactor.dead && (W.reactor.awake || v2dist(W.reactor.pos, p->pos) < 720)) {
        const Reactor *rc = &W.reactor;
        float f = clampf(rc->hp / rc->maxhp, 0, 1);
        Col ac = cur_def()->accent;
        if (rc->awake) r_textf(cx, VIRT_H - 96, 11, ac, AL_CENTER, "REACTOR CORE  -  PHASE %d OF %d", rc->phase + 1, rc->nphases);
        else r_text("REACTOR CORE INTEGRITY", cx, VIRT_H - 94, 11, ac, AL_CENTER);
        bar(cx - 220, VIRT_H - 74, 440, 8, f, f, ac, 40);
        /* where the phases break */
        for (int i = 0; i + 1 < rc->nphases; i++) {
            float x = cx - 220 + 440 * rc->threshold[i] / rc->maxhp;
            r_line(v2(x, VIRT_H - 80), v2(x, VIRT_H - 60), 3, col_a(C_WHITE, 0.85f));
        }
        if (reactor_shielded()) {
            float pulse = 0.6f + 0.4f * sinf(t * 8);
            const char *m = rc->vent_t > 0 ? "CORE VENTING" : "SHIELDED  -  DESTROY THE PYLONS";
            r_text(m, cx, VIRT_H - 56, 10, col_a(RDEF[RB_PYLON].col, pulse), AL_CENTER);
        }
    }

    /* cargo and mass, above the shield */
    float bx = 26, by = VIRT_H - 104;
    {
        Col cc = powerup_color(PU_SALVAGE);
        float heavy = clampf((p->mass - 1) / 1.0f, 0, 1);
        r_text("CARGO", bx, by - 40, 11, col_a(cc, 0.9f), AL_LEFT);
        r_textf(bx + 250, by - 44, 18, cc, AL_RIGHT, "%d", p->cargo);
        Col mc = col_lerp(rgba(0.6f, 0.8f, 1, 0.9f), C_ORANGE, heavy);
        r_textf(bx + 70, by - 40, 11, col_a(mc, 0.9f), AL_LEFT, "MASS X%.2f", p->mass);
        bar(bx, by - 22, 250, 6, heavy, heavy, cc, 25);
    }
    /* shields / energy / burner */
    Col sc = p->shield < 30 ? col_lerp(C_RED, C_BLUE, 0.5f + 0.5f * sinf(t * 8)) : rgba(0.35f, 0.65f, 1, 1);
    r_text("SHIELD", bx, by, 11, col_a(sc, 0.9f), AL_LEFT);
    r_textf(bx + 250, by - 4, 18, sc, AL_RIGHT, "%d", (int)maxf(0, p->shield));
    bar(bx, by + 18, 250, 9, p->shield / 100.0f, p->shield / 200.0f, sc, 25);
    Col ec = rgba(1, 0.85f, 0.25f, 1);
    if (p->energy < 20) ec = col_lerp(ec, C_RED, 0.5f + 0.5f * sinf(t * 8));
    r_text("ENERGY", bx, by + 40, 11, col_a(ec, 0.9f), AL_LEFT);
    r_textf(bx + 250, by + 36, 18, ec, AL_RIGHT, "%d", (int)p->energy);
    bar(bx, by + 58, 250, 9, p->energy / 100.0f, p->energy / 200.0f, ec, 25);
    Col bc = p->burning ? C_WHITE : rgba(0.5f, 0.85f, 1, 0.8f);
    r_text("BURNER", bx, by + 76, 8, col_a(bc, 0.7f), AL_LEFT);
    bar(bx + 58, by + 77, 192, 5, p->burner, p->burner, bc, 16);
    if (p->cloak_t > 0) r_textf(bx + 270, by + 40, 11, rgba(0.7f, 0.8f, 1, 0.9f), AL_LEFT, "CLOAK %d", (int)ceilf(p->cloak_t));
    if (p->invuln_t > 0) r_textf(bx + 270, by + 58, 11, C_YELLOW, AL_LEFT, "INVULN %d", (int)ceilf(p->invuln_t));
    if (mod_on(MOD_PHOENIX))
        r_text(W.phoenix_used ? "PHOENIX SPENT" : "PHOENIX READY", bx + 270, by + 76, 8,
               W.phoenix_used ? col_a(C_GREY, 0.6f) : rgba(1, 0.65f, 0.25f, 0.85f), AL_LEFT);

    /* weapons (stacked bottom-right): two primary slots, one secondary */
    float wx = VW - 26, wy = VIRT_H - 142;
    Col pc = rgba(0.7f, 0.92f, 1, 1);
    char buf[64];
    r_text("PRIMARY", wx, wy, 10, col_a(pc, 0.7f), AL_RIGHT);
    if (p->primary == PW_LASER) snprintf(buf, sizeof(buf), "LASER LV%d%s", p->laser_level, p->quad ? " QUAD" : "");
    else snprintf(buf, sizeof(buf), "%s", PRIMARY_NAMES[p->primary]);
    r_text(buf, wx - 90, wy - 3, 15, pc, AL_RIGHT);
    {
        const char *slot2 = p->special >= 0 ? PRIMARY_NAMES[p->special] : "EMPTY";
        float x1 = wx - 6, x0 = x1 - text_width(slot2, 10) - 18;
        r_text("LASER", x0 - 16, wy + 20, 10, p->primary == PW_LASER ? pc : col_a(pc, 0.45f), AL_RIGHT);
        if (p->primary == PW_LASER) r_line(v2(x0 - 16 - text_width("LASER", 10), wy + 36), v2(x0 - 16, wy + 36), 3, pc);
        r_text(slot2, x1, wy + 20, 10, p->special < 0 ? col_a(pc, 0.2f) : p->primary == p->special ? pc : col_a(pc, 0.45f), AL_RIGHT);
        if (p->special >= 0 && p->primary == p->special) r_line(v2(x1 - text_width(slot2, 10), wy + 36), v2(x1, wy + 36), 3, pc);
    }
    Col mc = rgba(1, 0.72f, 0.45f, 1);
    float sy = wy + 58;
    r_text("SECONDARY", wx, sy, 10, col_a(mc, 0.7f), AL_RIGHT);
    if (p->secondary >= 0) snprintf(buf, sizeof(buf), "%s  X%d", SECONDARY_NAMES[p->secondary], p->missiles);
    else snprintf(buf, sizeof(buf), "EMPTY");
    r_text(buf, wx - 110, sy - 3, 15, p->secondary >= 0 && p->missiles > 0 ? mc : col_a(mc, 0.4f), AL_RIGHT);
    if (p->secondary >= 0) {
        int cap = game_missile_cap(p->secondary);
        bar(wx - 190, sy + 22, 184, 5, (float)p->missiles / cap, (float)p->missiles / cap, mc, clampi(cap, 1, 20));
    }
    if (p->charging) {
        float k = clampf(p->fusion_charge / 1.6f, 0, 1);
        r_text(p->fusion_charge > 2.4f ? "OVERCHARGE!" : "FUSION CHARGE", wx, wy - 24, 10, p->fusion_charge > 2.4f ? C_RED : C_PURPLE, AL_RIGHT);
        r_add_rect(wx - 140 * k, wy - 8, 140 * k, 4, col_a(C_PURPLE, 0.9f));
    }

    /* swap prompt for the weapon under the ship */
    if (p->swap_pu >= 0 && W.pu[p->swap_pu].active && !p->dead) {
        const Powerup *pu = &W.pu[p->swap_pu];
        const char *key = g_in.use_stick ? "RB" : "E";
        if (pu->type >= PU_CONC && pu->type <= PU_MEGA)
            snprintf(buf, sizeof(buf), "%s  SWAP %s X%d FOR %s X%d", key, SECONDARY_NAMES[p->secondary], p->missiles, SECONDARY_NAMES[pu->type - PU_CONC], pu->amount);
        else snprintf(buf, sizeof(buf), "%s  SWAP %s FOR %s", key, PRIMARY_NAMES[p->special], powerup_name(pu->type));
        V2 at = w2v(v2add(pu->pos, v2(0, -40)));
        float w = text_width(buf, 11) * 0.5f + 14;
        r_panel(at.x - w, at.y - 8, w * 2, 26, powerup_color(pu->type), 0.7f);
        r_text(buf, at.x, at.y, 11, col_white(powerup_color(pu->type), 0.4f), AL_CENTER);
    }

    /* the build: installed modules along the bottom */
    {
        int ids[MOD_COUNT], n = 0;
        for (int i = 0; i < MOD_COUNT; i++)
            if (R.mods[i] > 0) ids[n++] = i;
        float r = 13, gap = 32, x0 = cx - (n - 1) * gap * 0.5f, y = VIRT_H - 28;
        for (int k = 0; k < n; k++) {
            int id = ids[k];
            bool lit = true;
            if (id == MOD_PHOENIX) lit = !W.phoenix_used;
            if (id == MOD_REACTIVE) lit = p->reactive_cd <= 0;
            if (id == MOD_PHASE) lit = p->phase_cd <= 0;
            if (id == MOD_OVERDRIVE || id == MOD_ADRENALINE) lit = G.reactor_dead;
            if (id == MOD_ANCHOR) lit = p->mass > 1.15f;
            if (id == MOD_LIFESUPPORT) lit = G.hostages_onboard > 0;
            mod_draw_badge(id, v2(x0 + k * gap, y), r, 0.85f, lit);
            if (R.mods[id] > 1) r_textf(x0 + k * gap + 10, y + 4, 7, col_a(C_WHITE, 0.8f), AL_LEFT, "%d", R.mods[id]);
        }
    }

    hud_draw_messages();
    if (hint_t > 0) {
        float a = clampf(hint_t / 0.6f, 0, 1) * clampf((6.0f - hint_t) / 0.3f, 0, 1);
        float w = text_width(hint_text, 12) * 0.5f + 24;
        r_panel(cx - w, VIRT_H - 190, w * 2, 34, col_a(C_CYAN, a), 0.6f * a);
        r_text(hint_text, cx, VIRT_H - 179, 12, col_a(rgba(0.85f, 0.95f, 1, 1), a), AL_CENTER);
    }

    /* off-screen threat indicators */
    if (!p->dead && !G.escaping) {
        for (int i = 0; i < MAX_ROBOTS; i++) {
            Robot *r = &W.rob[i];
            if (!r->active || !r->aware) continue;
            if (RDEF[r->type].cloaked && r->cloak_vis < 0.5f) continue;
            float d = v2dist(r->pos, p->pos);
            if (d > 1100 || in_view(r->pos, -10)) continue;
            V2 sp = w2v(r->pos);
            V2 c = v2(VW * 0.5f, VIRT_H * 0.5f);
            V2 dir = v2norm(v2sub(sp, c));
            float kx = dir.x != 0 ? (VW * 0.5f - 22) / fabsf(dir.x) : 1e9f;
            float ky = dir.y != 0 ? (VIRT_H * 0.5f - 22) / fabsf(dir.y) : 1e9f;
            V2 e = v2mad(c, dir, minf(kx, ky));
            V2 nn = v2perp(dir);
            float al = clampf(1.2f - d / 1100, 0.25f, 0.9f);
            Col rc = col_a(RDEF[r->type].col, al);
            V2 tri[3] = {v2mad(e, dir, 9), v2add(v2mad(e, dir, -6), v2scale(nn, 7)), v2add(v2mad(e, dir, -6), v2scale(nn, -7))};
            r_polyline(tri, 3, true, 3.5f, rc);
        }
    }

    /* crosshair */
    if (!p->dead && !G.escaping && !g_in.use_stick && g_in.mouse_active) {
        V2 m = w2v(g_in.aim_world);
        Col cc = rgba(0.7f, 0.95f, 1, 0.85f);
        float r = 11;
        r_circle(m, r, 2.5f, col_a(cc, 0.6f), 20);
        for (int k = 0; k < 4; k++) {
            V2 d = v2fromang(k * PI / 2 + PI / 4);
            r_line(v2mad(m, d, r + 3), v2mad(m, d, r + 9), 2.5f, cc);
        }
        r_line(m, m, 3, cc);
    }

    /* death */
    if (p->dead && G.result == GR_NONE) {
        r_text_glow("SHIP DESTROYED", cx, VIRT_H * 0.42f, 34, C_RED, AL_CENTER);
        r_textf(cx, VIRT_H * 0.42f - 26, 11, col_a(rgba(1, 0.6f, 0.55f, 1), 0.9f), AL_CENTER, "KILLED BY %s", game_killer_text());
        if (G.lives > 0) {
            r_textf(cx, VIRT_H * 0.42f + 56, 14, C_WHITE, AL_CENTER, "%d SHIP%s LEFT", G.lives, G.lives == 1 ? "" : "S");
            if (p->dead_t > 2.2f && fmodf(t * 2, 1) < 0.7f)
                r_text("PRESS FIRE TO CONTINUE", cx, VIRT_H * 0.42f + 84, 12, dim, AL_CENTER);
        } else {
            r_text("NO SHIPS REMAINING", cx, VIRT_H * 0.42f + 56, 14, C_WHITE, AL_CENTER);
        }
    }
    if (G.escaping) {
        float a = clampf(G.escape_t / 0.6f, 0, 1);
        r_text_glow("ESCAPE SUCCESSFUL", cx, VIRT_H * 0.4f, 38, col_a(C_GREEN, a), AL_CENTER);
        r_textf(cx, VIRT_H * 0.4f + 60, 14, col_a(C_WHITE, a), AL_CENTER, "%d HOSTAGE%s RESCUED", G.hostages_onboard, G.hostages_onboard == 1 ? "" : "S");
        r_textf(cx, VIRT_H * 0.4f + 86, 13, col_a(powerup_color(PU_SALVAGE), a), AL_CENTER, "%d CARGO BANKED", p->cargo);
        if (G.escape_margin < 5)
            r_text_glow(G.escape_margin < 1 ? "BY A HAIR!" : "CLOSE CALL!", cx, VIRT_H * 0.4f - 50, 22, col_a(C_YELLOW, a), AL_CENTER);
    }
    if (G.failing) {
        float a = clampf(G.fail_t / 1.0f, 0, 1);
        r_text_glow("CAUGHT IN THE BLAST", cx, VIRT_H * 0.42f, 36, col_a(C_RED, a), AL_CENTER);
    }
}
