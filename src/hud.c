/*
 * HUD, message queue, automap and powerup icons.
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
    case PU_VULCAN:
    case PU_VAMMO: return rgba(1, 0.8f, 0.4f, 1);
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
    case PU_KEY_BLUE: return key_color(LOCK_BLUE);
    case PU_KEY_YELLOW: return key_color(LOCK_YELLOW);
    case PU_KEY_RED: return key_color(LOCK_RED);
    }
    return C_WHITE;
}

const char *powerup_name(int type) {
    static const char *names[PU_TYPES] = {"SHIELD ORB", "ENERGY ORB", "LASER UPGRADE", "QUAD LASERS", "VULCAN CANNON", "VULCAN AMMO",
                                          "SPREADFIRE CANNON", "PLASMA CANNON", "FUSION CANNON", "CONCUSSION MISSILES",
                                          "HOMING MISSILES", "PROXIMITY BOMBS", "SMART MISSILES", "MEGA MISSILE",
                                          "CLOAKING DEVICE", "INVULNERABILITY", "EXTRA LIFE", "BLUE KEY", "YELLOW KEY", "RED KEY"};
    return type >= 0 && type < PU_TYPES ? names[type] : "?";
}

static void hex(V2 c, float r, float rot, float w, Col col) {
    V2 p[6];
    for (int i = 0; i < 6; i++) p[i] = v2add(c, v2scale(v2fromang(rot + i * TAU / 6), r));
    r_polyline(p, 6, true, w, col);
}

void draw_powerup_icon(int type, V2 c, float s, float t, float alpha) {
    Col col = col_a(powerup_color(type), alpha);
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
        case PU_VAMMO: label = "AM"; break;
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

static const char *objective_text(void) {
    if (G.reactor_dead) return "ESCAPE! FOLLOW THE GREEN ARROWS TO THE EXIT";
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
    r_textf(VW - 26, 64, 11, dim, AL_RIGHT, "MINE %d  %s", G.level + 1, cur_def()->name);
    r_textf(VW - 26, 82, 10, col_a(dim, 0.7f), AL_RIGHT, "%s", DIFF_NAMES[G.difficulty]);

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
    } else if (W.reactor.exists && !W.reactor.dead && v2dist(W.reactor.pos, p->pos) < 720) {
        float f = clampf(W.reactor.hp / W.reactor.maxhp, 0, 1);
        r_text("REACTOR CORE INTEGRITY", cx, VIRT_H - 94, 11, cur_def()->accent, AL_CENTER);
        bar(cx - 180, VIRT_H - 74, 360, 7, f, f, cur_def()->accent, 30);
    }

    /* shields / energy / burner */
    float bx = 26, by = VIRT_H - 104;
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

    /* weapons (stacked bottom-right) */
    float wx = VW - 26, wy = VIRT_H - 142;
    Col pc = rgba(0.7f, 0.92f, 1, 1);
    char buf[64];
    r_text("PRIMARY", wx, wy, 10, col_a(pc, 0.7f), AL_RIGHT);
    switch (p->primary) {
    case PW_LASER: snprintf(buf, sizeof(buf), "LASER LV%d%s", p->laser_level, p->quad ? " QUAD" : ""); break;
    case PW_VULCAN: snprintf(buf, sizeof(buf), "VULCAN %d", p->vulcan_ammo); break;
    default: snprintf(buf, sizeof(buf), "%s", PRIMARY_NAMES[p->primary]); break;
    }
    r_text(buf, wx - 150, wy - 3, 15, pc, AL_RIGHT);
    for (int i = 0; i < PW_COUNT; i++) {
        bool own = (p->owned & (1 << i)) != 0;
        float x = wx - (PW_COUNT - 1 - i) * 24 - 6;
        Col ic = i == p->primary ? pc : col_a(pc, own ? 0.45f : 0.12f);
        r_textf(x, wy + 20, 11, ic, AL_CENTER, "%d", i + 1);
        if (i == p->primary) r_line(v2(x - 7, wy + 36), v2(x + 7, wy + 36), 3, pc);
    }
    Col mc = rgba(1, 0.72f, 0.45f, 1);
    float sy = wy + 58;
    r_text("SECONDARY", wx, sy, 10, col_a(mc, 0.7f), AL_RIGHT);
    snprintf(buf, sizeof(buf), "%s", SECONDARY_NAMES[p->secondary]);
    r_text(buf, wx - 150, sy - 3, 15, p->missiles[p->secondary] > 0 ? mc : col_a(mc, 0.4f), AL_RIGHT);
    for (int i = 0; i < SW_COUNT; i++) {
        float x = wx - (SW_COUNT - 1 - i) * 24 - 6;
        Col ic = i == p->secondary ? mc : col_a(mc, p->missiles[i] > 0 ? 0.45f : 0.12f);
        r_textf(x, sy + 20, 11, ic, AL_CENTER, "%d", p->missiles[i]);
        if (i == p->secondary) r_line(v2(x - 8, sy + 36), v2(x + 8, sy + 36), 3, mc);
    }
    if (p->charging) {
        float k = clampf(p->fusion_charge / 1.6f, 0, 1);
        r_text(p->fusion_charge > 2.4f ? "OVERCHARGE!" : "FUSION CHARGE", wx, wy - 24, 10, p->fusion_charge > 2.4f ? C_RED : C_PURPLE, AL_RIGHT);
        r_add_rect(wx - 140 * k, wy - 8, 140 * k, 4, col_a(C_PURPLE, 0.9f));
        (void)sy;
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
    }
    if (G.failing) {
        float a = clampf(G.fail_t / 1.0f, 0, 1);
        r_text_glow("CAUGHT IN THE BLAST", cx, VIRT_H * 0.42f, 36, col_a(C_RED, a), AL_CENTER);
    }
}

/* ------------------------------------------------------------ automap */
void game_automap_draw(void) {
    float VW = g_virt_w;
    r_fill_rect(0, 0, VW, VIRT_H, rgba(0, 0.01f, 0.03f, 0.9f));
    float mw = L.w * TILE, mh = L.h * TILE;
    float fit = minf((VW - 120) / mw, (VIRT_H - 150) / mh);
    float sc = fit * G.automap_zoom;
    V2 center = v2add(v2(mw * 0.5f, mh * 0.5f), G.automap_pan);
    V2 origin = v2(VW * 0.5f, VIRT_H * 0.5f + 10);
#define AM(p) v2(origin.x + ((p).x - center.x) * sc, origin.y + ((p).y - center.y) * sc)
    const LevelDef *d = cur_def();
    float t = g_time;
    /* explored floor tint */
    for (int y = 0; y < L.h; y++)
        for (int x = 0; x < L.w; x++) {
            if (!L.explored[y][x]) continue;
            V2 a = AM(v2(x * TILE, y * TILE));
            Col fc = col_a(d->grid, 0.18f);
            if (L.flags[y][x] & TF_ENERGY) fc = col_a(C_YELLOW, 0.35f);
            if (L.flags[y][x] & TF_EXIT) fc = col_a(C_GREEN, 0.35f);
            r_add_rect(a.x, a.y, TILE * sc + 0.5f, TILE * sc + 0.5f, fc);
        }
    for (int i = 0; i < L.nsegs; i++) {
        const Seg *s = &L.segs[i];
        V2 mid = v2mad(v2scale(v2add(s->a, s->b), 0.5f), s->n, 6);
        int tx = tx_of(mid.x), ty = tx_of(mid.y);
        V2 pa = v2mad(s->a, s->n, 1), pb = v2mad(s->b, s->n, 1);
        int ax = tx_of(pa.x), ay = tx_of(pa.y), bx = tx_of(pb.x), by = tx_of(pb.y);
        bool seen = (tile_in(tx, ty) && L.explored[ty][tx]) || (tile_in(ax, ay) && L.explored[ay][ax]) || (tile_in(bx, by) && L.explored[by][bx]);
        if (!seen) continue;
        r_line(AM(s->a), AM(s->b), 3.0f, s->tile >= 0 ? col_a(d->accent, 0.5f) : d->wall);
    }
    for (int i = 0; i < L.ndoors; i++) {
        Door *dr = &L.doors[i];
        bool seen = false;
        for (int y = dr->y0 - 1; y <= dr->y1 + 1; y++)
            for (int x = dr->x0 - 1; x <= dr->x1 + 1; x++)
                if (tile_in(x, y) && L.explored[y][x]) seen = true;
        if (!seen) continue;
        Col c = key_color(dr->lock);
        V2 a, b;
        if (dr->horiz) { a = v2(dr->x0 * TILE, (dr->y0 + 0.5f) * TILE); b = v2((dr->x1 + 1) * TILE, (dr->y0 + 0.5f) * TILE); }
        else { a = v2((dr->x0 + 0.5f) * TILE, dr->y0 * TILE); b = v2((dr->x0 + 0.5f) * TILE, (dr->y1 + 1) * TILE); }
        r_line(AM(a), AM(b), 5, c);
    }
    /* points of interest */
    for (int i = 0; i < W.nhost; i++) {
        Hostage *h = &W.host[i];
        if (!h->active || !L.explored[tx_of(h->pos.y)][tx_of(h->pos.x)]) continue;
        V2 p = AM(h->pos);
        r_circle(p, 4, 2.5f, C_GREEN, 10);
    }
    for (int i = 0; i < MAX_POWERUPS; i++) {
        Powerup *pu = &W.pu[i];
        if (!pu->active || pu->type < PU_KEY_BLUE) continue;
        if (!L.explored[tx_of(pu->pos.y)][tx_of(pu->pos.x)]) continue;
        V2 p = AM(pu->pos);
        Col c = powerup_color(pu->type);
        V2 dm[4] = {v2add(p, v2(0, -6)), v2add(p, v2(6, 0)), v2add(p, v2(0, 6)), v2add(p, v2(-6, 0))};
        r_polyline(dm, 4, true, 3, c);
    }
    if (W.reactor.exists && L.explored[tx_of(W.reactor.pos.y)][tx_of(W.reactor.pos.x)]) {
        V2 p = AM(W.reactor.pos);
        r_circle(p, 8, 3, W.reactor.dead ? C_GREY : d->accent, 16);
        r_text("REACTOR", p.x, p.y + 12, 8, d->accent, AL_CENTER);
    }
    for (int i = 0; i < W.nmat; i++) {
        if (!L.explored[tx_of(W.mat[i].pos.y)][tx_of(W.mat[i].pos.x)]) continue;
        V2 p = AM(W.mat[i].pos);
        r_frame(p.x - 4, p.y - 4, 8, 8, 2.5f, C_MAGENTA);
    }
    if (G.reactor_dead) {
        V2 p = AM(W.exit_pos);
        r_text("EXIT", p.x, p.y - 16, 10, C_GREEN, AL_CENTER);
    }
    /* player */
    Player *pl = &W.pl;
    if (fmodf(t * 2, 1) < 0.75f) {
        V2 p = AM(pl->pos);
        game_draw_ship_icon(p, pl->ang, 8, C_WHITE);
        r_glow(p, 20, col_a(C_CYAN, 0.5f));
    }
#undef AM
    r_panel(20, 20, VW - 40, 50, d->wall, 0.6f);
    r_textf(40, 34, 18, d->wall, AL_LEFT, "AUTOMAP - MINE %d: %s", G.level + 1, d->name);
    r_text("TAB CLOSE   WASD PAN   WHEEL ZOOM", VW - 40, 40, 10, col_a(C_WHITE, 0.7f), AL_RIGHT);
    r_text(objective_text(), VW * 0.5f, VIRT_H - 40, 12, col_a(C_WHITE, 0.8f), AL_CENTER);
}
