/*
 * Traps built into the mines: plasma vents that erupt in a rhythm, laser
 * gates that switch on and off across the corridors, sweepers with rotating
 * laser arms and gravity wells that drag the ship in - the heavier the hold,
 * the harder it is to pull away. Proximity mines are plain enemy projectiles.
 * Robots are the mine's own: the traps never hurt them.
 */
#include "game_internal.h"

static const Col VENT_COL = {1, 0.45f, 0.15f, 1};
static const Col GATE_COL = {1, 0.2f, 0.3f, 1};
static const Col SWEEP_COL = {1, 0.25f, 0.55f, 1};
static const Col WELL_COL = {0.55f, 0.4f, 1, 1};

/* a stable pseudo-random number per trap, so the same mine has the same rhythm */
static float trap_rand(int tx, int ty, int k) { return (hash32((uint32_t)tx * 73856093u ^ (uint32_t)ty * 19349663u ^ W.seed ^ (uint32_t)k * 83492791u) >> 8) * (1.0f / 16777216.0f); }

void trap_add(int type, int tx, int ty) {
    if (W.ntraps >= MAX_TRAPS) return;
    Trap *t = &W.trap[W.ntraps++];
    memset(t, 0, sizeof(*t));
    t->type = type;
    t->pos = tile_center(tx, ty);
    float tier = run_tier();
    switch (type) {
    case TR_VENT:
        t->period = 3.4f + trap_rand(tx, ty, 1) * 1.4f;
        t->offset = trap_rand(tx, ty, 2) * t->period;
        break;
    case TR_GATE: {
        /* the beam spans the narrow way across the corridor */
        V2 l = beam_end(t->pos, PI, 400), r = beam_end(t->pos, 0, 400);
        V2 u = beam_end(t->pos, -PI / 2, 400), d = beam_end(t->pos, PI / 2, 400);
        if (v2dist(l, r) < v2dist(u, d)) { t->a = l; t->b = r; }
        else { t->a = u; t->b = d; }
        t->period = 3.2f - 0.3f * tier;
        t->offset = trap_rand(tx, ty, 2) * t->period;
    } break;
    case TR_SWEEPER:
        t->arms = 1 + (trap_rand(tx, ty, 3) < 0.35f + 0.2f * tier) + (tier >= 1.3f && trap_rand(tx, ty, 4) < 0.4f);
        t->spin = (trap_rand(tx, ty, 5) < 0.5f ? -1 : 1) * (0.7f + 0.35f * trap_rand(tx, ty, 6) + 0.1f * tier);
        t->ang = trap_rand(tx, ty, 7) * TAU;
        t->len = 250 + 40 * tier;
        break;
    case TR_WELL: break;
    }
}

/* the gate cycle: on for the first half, a flicker of warning before */
static float gate_state(const Trap *t, float *warn) {
    float ph = fmodf(W.time + t->offset, t->period) / t->period;
    *warn = 0;
    if (ph < 0.5f) return 1;
    if (ph > 0.84f) *warn = (ph - 0.84f) / 0.16f;
    return 0;
}

void traps_update(float dt) {
    Player *p = &W.pl;
    bool alive = !p->dead && !G.escaping && !G.failing;
    float tier = run_tier();
    for (int i = 0; i < W.ntraps; i++) {
        Trap *t = &W.trap[i];
        switch (t->type) {
        case TR_VENT: {
            float ph = fmodf(W.time + t->offset, t->period);
            float erupt = t->period - 0.75f, warn = erupt - 1.1f;
            bool was = t->on;
            t->on = ph >= erupt;
            t->heat = ph >= warn && ph < erupt ? (ph - warn) / 1.1f : t->on ? 1 : 0;
            if (t->heat > 0 && !t->on && frand() < dt * 18 * t->heat)
                fx_spark(v2add(t->pos, v2(frandr(-14, 14), frandr(-14, 14))), v2scale(v2fromang(frand() * TAU), 40 + 80 * t->heat), VENT_COL, 0.3f, 2.5f);
            if (t->on && !was) {
                fx_explosion(t->pos, 34, VENT_COL);
                fx_ring(t->pos, 10, 78, col_a(VENT_COL, 0.9f), 0.35f, 5);
                grid_impulse(t->pos, 120, 220);
                snd_play_at(SND_EXPL_S, t->pos, 0.5f, 0.7f);
                /* the deeper vents spit plasma too */
                if (tier >= 0.8f && in_view(t->pos, 100)) {
                    int prev = g_proj_src;
                    g_proj_src = DS_TRAP;
                    bullet_ring(EP_PELLET, t->pos, 20, 6 + (int)(tier * 2), frand() * TAU, 150 * DIFF_PSPD[G.difficulty], 5, VENT_COL);
                    g_proj_src = prev;
                }
            }
            if (t->on) {
                if (frand() < dt * 60)
                    fx_glow(v2add(t->pos, v2(frandr(-20, 20), frandr(-20, 20))), v2scale(v2fromang(frand() * TAU), 90), col_a(VENT_COL, 0.5f), 0.4f, 16);
                if (alive && v2dist(p->pos, t->pos) < 66 + p->radius * 0.6f) hazard_touch(60, dt, v2norm(v2sub(p->pos, t->pos)), DS_TRAP);
            }
        } break;
        case TR_GATE: {
            float warn;
            t->on = gate_state(t, &warn) > 0;
            t->heat = warn;
            if (t->on && alive && player_hurt_by(t->a, t->b, 6)) {
                V2 n = v2perp(v2norm(v2sub(t->b, t->a)));
                if (v2dot(n, v2sub(p->pos, t->a)) < 0) n = v2scale(n, -1);
                hazard_touch(85, dt, n, DS_TRAP);
                p->vel = v2mad(p->vel, n, 900 * dt);
            }
        } break;
        case TR_SWEEPER: {
            t->ang = wrap_angle(t->ang + t->spin * dt);
            for (int k = 0; k < t->arms; k++) {
                float a = t->ang + TAU * k / t->arms;
                V2 e = beam_end(v2mad(t->pos, v2fromang(a), 14), a, t->len);
                if (alive && player_hurt_by(t->pos, e, 6)) {
                    V2 n = v2perp(v2fromang(a));
                    if (v2dot(n, v2sub(p->pos, t->pos)) < 0) n = v2scale(n, -1);
                    hazard_touch(60, dt, n, DS_TRAP);
                }
                if (frand() < dt * 12) fx_spark(e, v2scale(v2fromang(frand() * TAU), 100), SWEEP_COL, 0.25f, 2.5f);
            }
        } break;
        case TR_WELL: {
            const float R = 380;
            if (!alive) break;
            V2 d = v2sub(t->pos, p->pos);
            float dist = v2len(d);
            if (dist < R && dist > 1) {
                /* the pull is the same for every ship: a heavy one has less thrust to fight it */
                float f = 1 - dist / R;
                p->vel = v2mad(p->vel, v2scale(d, 1.0f / dist), 800 * f * f * dt);
                if (dist < 34 + p->radius * 0.6f) hazard_touch(35, dt, v2scale(d, -1.0f / dist), DS_TRAP);
            }
            /* loose salvage and pickups drift in too */
            for (int k = 0; k < MAX_POWERUPS; k++) {
                Powerup *pu = &W.pu[k];
                if (!pu->active) continue;
                V2 pd = v2sub(t->pos, pu->pos);
                float pl = v2len(pd);
                if (pl < R && pl > 20) pu->vel = v2mad(pu->vel, v2scale(pd, 1.0f / pl), 500 * (1 - pl / R) * dt);
            }
            if (frand() < dt * 20) {
                V2 from = v2mad(t->pos, v2fromang(frand() * TAU), frandr(120, R));
                fx_spark(from, v2scale(v2sub(t->pos, from), 1.6f), col_a(WELL_COL, 0.7f), 0.5f, 2.5f);
            }
        } break;
        }
    }
}

void traps_draw(void) {
    float time = W.time;
    for (int i = 0; i < W.ntraps; i++) {
        Trap *t = &W.trap[i];
        switch (t->type) {
        case TR_VENT: {
            if (!rw_visible(t->pos, 90)) break;
            /* a grate in the floor that glows hotter before it blows */
            float h = TILE * 0.38f;
            Col c = col_lerp(col_mul(VENT_COL, 0.55f), col_white(VENT_COL, 0.4f), t->heat);
            V2 sq[4] = {v2add(t->pos, v2(-h, -h)), v2add(t->pos, v2(h, -h)), v2add(t->pos, v2(h, h)), v2add(t->pos, v2(-h, h))};
            rw_polyline(sq, 4, true, 3.5f, col_a(c, 0.85f));
            for (int k = -1; k <= 1; k++) rw_line(v2add(t->pos, v2(-h + 4, k * 7.0f)), v2add(t->pos, v2(h - 4, k * 7.0f)), 2.5f, col_a(c, 0.6f));
            rw_glow(t->pos, 26 + 50 * t->heat, col_a(VENT_COL, 0.1f + 0.45f * t->heat));
            if (t->heat > 0 && !t->on) rw_circle(t->pos, 66, 2.5f, col_a(VENT_COL, 0.15f + 0.5f * t->heat * (0.6f + 0.4f * sinf(time * 30))), 28);
            if (t->on) {
                rw_glow(t->pos, 90, col_a(VENT_COL, 0.55f));
                rw_circle(t->pos, 66, 5, col_a(col_white(VENT_COL, 0.3f), 0.8f), 28);
            }
        } break;
        case TR_GATE: {
            V2 mid = v2lerp(t->a, t->b, 0.5f);
            if (!rw_visible(mid, 140)) break;
            V2 dir = v2norm(v2sub(t->b, t->a)), n = v2perp(dir);
            for (int k = 0; k < 2; k++) {
                V2 e = k ? t->b : t->a;
                V2 in = v2scale(dir, k ? -1.0f : 1.0f);
                V2 box[4] = {v2add(e, v2scale(n, -9)), v2add(e, v2scale(n, 9)), v2add(v2mad(e, in, 9), v2scale(n, 9)), v2add(v2mad(e, in, 9), v2scale(n, -9))};
                rw_polyline(box, 4, true, 3, col_a(GATE_COL, 0.9f));
                rw_glow(e, 16, col_a(GATE_COL, t->on ? 0.6f : 0.2f));
            }
            if (t->on) draw_beam(v2mad(t->a, dir, 9), v2mad(t->b, dir, -9), 10, GATE_COL, 0);
            else if (t->heat > 0) draw_beam(v2mad(t->a, dir, 9), v2mad(t->b, dir, -9), 10, GATE_COL, 1 - t->heat);
        } break;
        case TR_SWEEPER: {
            if (!rw_visible(t->pos, t->len + 20)) break;
            for (int k = 0; k < t->arms; k++) {
                float a = t->ang + TAU * k / t->arms;
                V2 s = v2mad(t->pos, v2fromang(a), 14);
                draw_beam(s, beam_end(s, a, t->len), 9, SWEEP_COL, 0);
            }
            V2 hub[6];
            for (int k = 0; k < 6; k++) hub[k] = v2add(t->pos, v2scale(v2fromang(t->ang * 2 + k * TAU / 6), 16));
            rw_polyline(hub, 6, true, 4, col_white(SWEEP_COL, 0.2f));
            rw_glow(t->pos, 34, col_a(SWEEP_COL, 0.4f));
            rw_circle(t->pos, t->len, 1.5f, col_a(SWEEP_COL, 0.08f), 48);
        } break;
        case TR_WELL: {
            if (!rw_visible(t->pos, 400)) break;
            for (int k = 0; k < 4; k++) {
                float f = fmodf(time * 0.5f + k * 0.25f, 1.0f);
                rw_circle(t->pos, 380 * (1 - f), 2.5f, col_a(WELL_COL, 0.25f * f), 48);
            }
            V2 sp[10];
            for (int k = 0; k < 10; k++) sp[k] = v2add(t->pos, v2scale(v2fromang(-time * 3 + k * TAU / 10), (k & 1) ? 14 : 30));
            rw_polyline(sp, 10, true, 3.5f, WELL_COL);
            rw_glow(t->pos, 60, col_a(WELL_COL, 0.35f));
            rw_glow(t->pos, 16, col_a(C_WHITE, 0.8f));
        } break;
        }
    }
}
