/*
 * The reactor core: the fight at the heart of every mine.
 *
 * The core sleeps until the ship comes close or shoots it. Then it runs bullet
 * patterns from a pool that grows with every phase: spirals, rings, aimed fans,
 * curving flowers, rotating laser beams, seeker mines, flak novas and walls of
 * accelerating shards; later phases run two patterns at once. Its hull is split
 * into three or four phases. When one breaks the core vents - invulnerable, it
 * wipes the bullets and throws the ship back - and then raises shield pylons
 * that must be destroyed before it can be hurt again, or calls in guards. The
 * last phase is a meltdown: everything faster and denser.
 */
#include "game_internal.h"

static Col core_col(void) { return cur_def()->accent; }

void reactor_init(V2 pos) {
    Reactor *rc = &W.reactor;
    memset(rc, 0, sizeof(*rc));
    float tier = run_tier();
    rc->exists = true;
    rc->pos = pos;
    rc->maxhp = rc->hp = (1600.0f + 900.0f * tier) * DIFF_HP[G.difficulty];
    rc->nphases = tier >= 1.3f ? 4 : 3;
    for (int i = 0; i < rc->nphases; i++) rc->threshold[i] = rc->maxhp * (1.0f - (i + 1.0f) / rc->nphases);
    rc->last_pat = -1;
    for (int k = 0; k < 2; k++) barrage_stop(&rc->bar[k]);
}

bool reactor_shielded(void) {
    const Reactor *rc = &W.reactor;
    return rc->exists && !rc->dead && (rc->pylons > 0 || rc->vent_t > 0);
}

void reactor_damage(float dmg) {
    Reactor *rc = &W.reactor;
    if (!rc->exists || rc->dead) return;
    rc->provoked = true;
    if (reactor_shielded()) {
        rc->shield_hit = 0.25f;
        snd_play_at(SND_WALL, rc->pos, 0.35f, 1.6f);
        return;
    }
    rc->hp -= dmg;
    rc->hit_flash = 0.1f;
    snd_play_at(SND_REACTOR_HIT, rc->pos, 0.5f, frandr(0.9f, 1.1f));
    /* a phase always ends at its threshold: one big hit can't skip the next one */
    if (rc->phase < rc->nphases - 1 && rc->hp < rc->threshold[rc->phase]) rc->hp = rc->threshold[rc->phase];
    if (rc->hp <= 0) {
        rc->dead = true;
        rc->boom_t = 1.8f;
        for (int k = 0; k < 2; k++) barrage_stop(&rc->bar[k]);
        int n = bullets_cancel(rc->pos, 3000);
        if (n > 0) game_add_score(n * 10);
        fx_explosion(rc->pos, 140, rgba(1, 0.9f, 0.6f, 1));
        fx_ring(rc->pos, 20, 520, col_a(core_col(), 0.9f), 0.9f, 10);
        grid_impulse(rc->pos, 700, 1400);
        snd_play(SND_EXPL_L, 1.0f, 0.8f);
        spawn_salvage(rc->pos, (int)(40 + 30 * run_tier()), 280);
        W.arena_fight = false;
        start_self_destruct(rc->pos);
    }
}

/* ------------------------------------------------------------ phases */
static void spawn_pylons(void) {
    Reactor *rc = &W.reactor;
    float tier = run_tier();
    int n = clampi(2 + (tier >= 0.9f) + (rc->phase >= 3), 2, 4);
    float a0 = frand() * TAU;
    int made = 0;
    for (int i = 0; i < n; i++) {
        for (int tries = 0; tries < 24; tries++) {
            float a = a0 + TAU * i / n + frandr(-0.3f, 0.3f);
            V2 at = v2mad(rc->pos, v2fromang(a), frandr(220, 320));
            int tx = tx_of(at.x), ty = tx_of(at.y);
            bool free = tile_in(tx, ty);
            for (int dy = -1; dy <= 1 && free; dy++)
                for (int dx = -1; dx <= 1 && free; dx++)
                    if (tile_blocks(tx + dx, ty + dy)) free = false;
            if (!free || !los(at, rc->pos)) continue;
            Robot *r = robot_spawn(RB_PYLON, tile_center(tx, ty), true);
            if (!r) break;
            r->aware = true;
            r->spawn_t = 1.0f;
            r->maxhp = r->hp = r->maxhp * (1 + 0.3f * tier);
            G.robots_total++;
            fx_ring(r->pos, 70, 6, col_a(RDEF[RB_PYLON].col, 0.9f), 0.5f, 6);
            grid_impulse(r->pos, 160, -300);
            made++;
            break;
        }
    }
    if (made) {
        rc->pylons = made;
        hud_msg("SHIELD PYLONS ONLINE - DESTROY THEM!", RDEF[RB_PYLON].col);
        snd_play(SND_MATCEN, 0.8f, 0.7f);
    }
}

static void spawn_guards(void) {
    Reactor *rc = &W.reactor;
    const LevelDef *ld = cur_def();
    int n = 2 + (int)(run_tier() + 0.5f);
    for (int i = 0; i < n; i++) {
        V2 at = v2mad(rc->pos, v2fromang(TAU * i / n + frand()), REACTOR_R + 70);
        if (point_in_rock(at)) continue;
        Robot *r = robot_spawn(ld->matcen_types[irand(ld->nmatcen)], at, true);
        if (!r) continue;
        r->aware = true;
        r->last_seen = W.pl.pos;
        G.robots_total++;
        fx_ring(at, 60, 5, rgba(1, 0.4f, 0.8f, 1), 0.4f, 5);
    }
    hud_msg("THE CORE CALLS FOR GUARDS", rgba(1, 0.4f, 0.85f, 1));
    snd_play(SND_MATCEN, 0.8f, 0.9f);
}

static void phase_break(void) {
    Reactor *rc = &W.reactor;
    Player *p = &W.pl;
    rc->phase++;
    rc->vent_t = 2.4f;
    rc->rest_t = 0;
    for (int k = 0; k < 2; k++) barrage_stop(&rc->bar[k]);
    int n = bullets_cancel(rc->pos, 3000);
    if (n > 0) {
        game_add_score(n * 10);
        char buf[32];
        snprintf(buf, sizeof(buf), "%d BULLETS CANCELLED", n);
        fx_popup(v2add(rc->pos, v2(0, -90)), buf, C_YELLOW, 13);
    }
    /* the vent throws the ship back */
    V2 d = v2sub(p->pos, rc->pos);
    float dist = v2len(d);
    if (!p->dead && dist < 380) p->vel = v2mad(p->vel, v2norm(d), (380 - dist) * 2.2f / p->mass);
    fx_ring(rc->pos, 30, 420, col_a(C_WHITE, 0.9f), 0.6f, 9);
    fx_ring(rc->pos, 20, 300, col_a(core_col(), 0.9f), 0.5f, 6);
    fx_explosion(rc->pos, 70, core_col());
    grid_impulse(rc->pos, 500, 1100);
    shake_add(0.6f);
    W.flash = maxf(W.flash, 0.45f);
    W.flash_col = core_col();
    W.time_scale = 0.4f;
    snd_play(SND_EXPL_L, 0.9f, 0.6f);
    snd_play(SND_BOSS, 0.7f, 1.3f + 0.1f * rc->phase);
    char buf[64];
    if (rc->phase == rc->nphases - 1) snprintf(buf, sizeof(buf), "CORE MELTDOWN IMMINENT - FINAL PHASE");
    else snprintf(buf, sizeof(buf), "CORE BREACH - PHASE %d OF %d", rc->phase + 1, rc->nphases);
    hud_msg(buf, core_col());
}

/* the next pattern: a pool that grows with the phases, never the same one twice running */
static void next_pattern(void) {
    Reactor *rc = &W.reactor;
    static const int POOL[REACTOR_PHASES][4] = {
        {BP_SPIRAL, BP_FANS, BP_RINGS, BP_NOVA},
        {BP_LASERS, BP_FLOWER, BP_SEEKERS, BP_SPIRAL},
        {BP_LASERS, BP_WALLS, BP_NOVA, BP_FLOWER},
        {BP_LASERS, BP_SPIRAL, BP_WALLS, BP_SEEKERS},
    };
    float tier = run_tier();
    int ph = clampi(rc->phase, 0, REACTOR_PHASES - 1);
    bool final = rc->phase == rc->nphases - 1;
    int n = ph == 0 && tier < 0.8f ? 3 : 4;
    int pat = POOL[ph][irand(n)];
    for (int k = 0; k < 4 && pat == rc->last_pat; k++) pat = POOL[ph][irand(n)];
    rc->last_pat = pat;
    Barrage *b = &rc->bar[0];
    b->dens = (0.85f + 0.2f * tier) * (1 + 0.12f * rc->phase) + (final ? 0.25f : 0);
    b->speed = 0.9f + 0.06f * tier + 0.04f * rc->phase;
    b->dmg = 1;
    b->src = DS_REACTOR;
    b->col = core_col();
    b->col2 = col_lerp(core_col(), C_WHITE, 0.45f);
    b->beam_w = 9;
    barrage_start(b, pat, pat == BP_LASERS ? 6.5f : 4.6f + frand());
    /* from the third phase on a lighter second pattern runs alongside */
    Barrage *a = &rc->bar[1];
    if (rc->phase >= 2) {
        int aux = pat == BP_LASERS ? BP_SPIRAL : pat == BP_WALLS ? BP_FANS : pat == BP_FLOWER ? BP_RINGS : BP_FANS;
        *a = *b;
        a->dens = b->dens * (final ? 0.7f : 0.55f);
        a->speed = b->speed * 0.9f;
        a->col = rgba(1, 0.45f, 0.3f, 1);
        a->col2 = rgba(1, 0.75f, 0.4f, 1);
        barrage_start(a, aux, b->dur);
    }
}

void reactor_update(float dt) {
    Reactor *rc = &W.reactor;
    if (!rc->exists) return;
    rc->t += dt;
    rc->hit_flash = maxf(0, rc->hit_flash - dt);
    rc->shield_hit = maxf(0, rc->shield_hit - dt);
    if (rc->dead) {
        if (rc->boom_t > 0) {
            rc->boom_t -= dt;
            if (frand() < dt * 16) {
                V2 at = v2add(rc->pos, v2scale(v2fromang(frand() * TAU), frand() * 70));
                fx_explosion(at, 25 + frand() * 35, frand() < 0.5f ? C_ORANGE : C_YELLOW);
                snd_play_at(SND_EXPL_M, at, 0.6f, frandr(0.8f, 1.2f));
            }
        }
        if (frand() < dt * 6) fx_spark(rc->pos, v2scale(v2fromang(frand() * TAU), 100 + frand() * 200), C_ORANGE, 0.5f, 3);
        return;
    }
    Player *p = &W.pl;
    float dist = v2dist(rc->pos, p->pos);
    float hpf = rc->hp / rc->maxhp;
    if (hpf < 0.5f && frand() < dt * 8) fx_spark(rc->pos, v2scale(v2fromang(frand() * TAU), 150 + frand() * 150), C_YELLOW, 0.4f, 3);

    /* the pylons alive */
    int pylons = 0;
    for (int i = 0; i < MAX_ROBOTS; i++)
        if (W.rob[i].active && W.rob[i].type == RB_PYLON) pylons++;
    if (rc->pylons > 0 && pylons == 0) {
        hud_msg("CORE SHIELD DOWN!", C_WHITE);
        snd_play(SND_POWERUP, 0.7f, 0.7f);
        fx_ring(rc->pos, 90, 10, col_a(RDEF[RB_PYLON].col, 0.9f), 0.4f, 6);
    }
    rc->pylons = pylons;
    rc->shield_vis = lerpf(rc->shield_vis, reactor_shielded() ? 1.0f : 0.0f, damp_factor(6, dt));

    /* waking up */
    if (!rc->awake) {
        if (!p->dead && dist < 1300 && (rc->provoked || (dist < 640 && los(rc->pos, p->pos)))) {
            rc->awake = true;
            rc->wake_t = 1.6f;
            music_play(SONG_BOSS);
            snd_play(SND_BOSS, 1.0f, 1.25f);
            snd_play(SND_ALARM, 0.5f, 0.8f);
            hud_msg("REACTOR CORE DEFENSES ONLINE", core_col());
            shake_add(0.4f);
            fx_ring(rc->pos, 20, 360, col_a(core_col(), 0.8f), 0.8f, 7);
        } else return;
    }
    W.arena_fight = !p->dead && dist < 1500;
    if (rc->wake_t > 0) {
        rc->wake_t -= dt;
        return;
    }

    /* between phases */
    if (rc->vent_t > 0) {
        rc->vent_t -= dt;
        if (frand() < dt * 40) {
            V2 d = v2fromang(frand() * TAU);
            fx_spark(v2mad(rc->pos, d, REACTOR_R), v2scale(d, 250 + frand() * 250), col_white(core_col(), 0.5f), 0.5f, 3.5f);
        }
        if (rc->vent_t <= 0) {
            bool pylon_phase = rc->phase == 1 || rc->phase == 3;
            if (pylon_phase) spawn_pylons();
            else spawn_guards();
            rc->rest_t = 0.8f;
        }
        return;
    }
    if (rc->phase < rc->nphases - 1 && rc->hp <= rc->threshold[rc->phase] + 0.5f) {
        phase_break();
        return;
    }

    /* the ship out of reach: the patterns wind down */
    bool reach = !p->dead && !G.escaping && dist < 1300;
    rc->lost_t = reach ? 0 : rc->lost_t + dt;
    if (!reach) {
        if (rc->lost_t > 1.5f)
            for (int k = 0; k < 2; k++) barrage_stop(&rc->bar[k]);
        if (rc->lost_t > 10) {
            /* long gone (or a new ship far away): the core goes dormant and keeps its damage */
            rc->awake = rc->provoked = false;
            rc->lost_t = 0;
            if (!G.escaping) music_play(cur_def()->song);
        }
        return;
    }
    for (int k = 0; k < 2; k++) barrage_update(&rc->bar[k], rc->pos, REACTOR_R, dt);
    if (rc->bar[0].pat < 0) {
        barrage_stop(&rc->bar[1]);
        rc->rest_t -= dt;
        if (rc->rest_t <= 0) {
            next_pattern();
            rc->rest_t = rc->phase == rc->nphases - 1 ? 0.55f : 0.9f;
        } else if (rc->rest_t < 0.5f && frand() < dt * 3) {
            /* a lone aimed shot while it winds up the next one */
            int prev = g_proj_src;
            g_proj_src = DS_REACTOR;
            float sp = 430 * DIFF_PSPD[G.difficulty];
            float a = aim_lead(rc->pos, sp);
            bullet(EP_REACTOR, v2mad(rc->pos, v2fromang(a), REACTOR_R), a, sp, 8, core_col());
            g_proj_src = prev;
        }
    }

    /* the ship can't fly through the core */
    V2 d = v2sub(p->pos, rc->pos);
    float dl = v2len(d), rr = REACTOR_R + p->radius;
    if (!p->dead && dl < rr && dl > 1e-3f) {
        V2 n = v2scale(d, 1.0f / dl);
        p->pos = v2mad(rc->pos, n, rr);
        float vn = v2dot(p->vel, n);
        if (vn < 0) p->vel = v2mad(p->vel, n, -vn * 1.6f);
        if (p->bump_cd <= 0) {
            p->bump_cd = 0.4f;
            player_damage(6, n, DS_REACTOR);
        }
    }
}

/* ------------------------------------------------------------ drawing */
void reactor_draw(void) {
    Reactor *rc = &W.reactor;
    if (!rc->exists) return;
    float t = rc->t;
    Col acc = core_col();
    for (int k = 0; k < 2; k++) barrage_draw(&rc->bar[k], rc->pos, REACTOR_R);
    if (!rw_visible(rc->pos, 160)) return;
    if (rc->dead) {
        /* smouldering wreck */
        V2 ring[8];
        for (int i = 0; i < 8; i++) ring[i] = v2add(rc->pos, v2scale(v2fromang(i * TAU / 8 + 0.2f), 42 + (i & 1) * 7));
        rw_polyline(ring, 8, true, 3, col_a(C_GREY, 0.4f));
        rw_glow(rc->pos, 70, col_a(C_ORANGE, 0.15f + 0.1f * sinf(t * 9)));
        return;
    }
    float hpf = rc->hp / rc->maxhp;
    bool final = rc->awake && rc->phase == rc->nphases - 1;
    Col c = acc;
    if (final) c = col_lerp(acc, rgba(1, 0.3f, 0.2f, 1), 0.5f + 0.5f * sinf(t * 9));
    if (rc->hit_flash > 0) c = col_white(c, 0.8f);
    if (rc->vent_t > 0) c = col_white(c, 0.5f + 0.4f * sinf(t * 30));
    float flick = hpf < 0.3f ? (0.75f + 0.25f * sinf(t * 37)) : 1.0f;
    c = col_a(c, flick);
    float spin = rc->awake ? 1.0f + 0.35f * rc->phase : 0.3f;
    float R = REACTOR_R;
    rw_glow(rc->pos, 150, col_a(c, rc->awake ? 0.18f : 0.1f));
    /* outer rotating octagon, and armour plates: one ring per phase left */
    V2 oct[8];
    for (int i = 0; i < 8; i++) oct[i] = v2add(rc->pos, v2scale(v2fromang(t * 0.4f * spin + i * TAU / 8), R));
    rw_polyline(oct, 8, true, 7, c);
    int left = rc->nphases - rc->phase;
    for (int ring = 0; ring < left; ring++) {
        float rr = R + 14 + ring * 9;
        float a0 = (ring & 1 ? -1 : 1) * t * (0.5f + 0.2f * ring) * spin;
        for (int k = 0; k < 6; k++) {
            float a = a0 + k * TAU / 6;
            V2 arc[4];
            for (int j = 0; j < 4; j++) arc[j] = v2add(rc->pos, v2scale(v2fromang(a + j * 0.14f), rr));
            rw_polyline(arc, 4, false, 4, col_a(c, 0.75f - ring * 0.12f));
        }
    }
    /* middle counter-rotating square */
    V2 sq[4];
    for (int i = 0; i < 4; i++) sq[i] = v2add(rc->pos, v2scale(v2fromang(-t * 0.9f * spin + i * TAU / 4), R * 0.64f));
    rw_polyline(sq, 4, true, 5, col_white(c, 0.2f));
    /* conduits */
    for (int i = 0; i < 4; i++) {
        V2 a = v2add(rc->pos, v2scale(v2fromang(t * 0.4f * spin + i * TAU / 4 + TAU / 16), R));
        V2 b = v2add(rc->pos, v2scale(v2fromang(t * 0.4f * spin + i * TAU / 4 + TAU / 16), R + 20));
        rw_line(a, b, 4, col_a(c, 0.7f));
    }
    /* pulsing core */
    float pulse = 0.6f + 0.4f * sinf(t * (4 + (1 - hpf) * 10 + (rc->awake ? 4 : 0)));
    Col core = col_lerp(rgba(1, 1, 0.8f, 1), rgba(1, 0.3f, 0.2f, 1), 1 - hpf);
    rw_glow(rc->pos, 36 * pulse + 12, col_a(core, 0.8f));
    rw_glow(rc->pos, 13, col_a(C_WHITE, 0.9f));
    rw_circle(rc->pos, 15 + pulse * 4, 3, col_a(core, 0.8f), 20);
    if (rc->wake_t > 0) rw_circle(rc->pos, R + 20 + (1.6f - rc->wake_t) * 220, 4, col_a(c, rc->wake_t / 1.6f), 48);
    /* the shield: a hexagon bubble, and the links to the pylons that hold it up */
    if (rc->shield_vis > 0.02f) {
        Col sc = col_a(RDEF[RB_PYLON].col, rc->shield_vis * (0.55f + 0.25f * sinf(t * 7) + rc->shield_hit * 1.5f));
        V2 hex[6];
        for (int i = 0; i < 6; i++) hex[i] = v2add(rc->pos, v2scale(v2fromang(t * 0.3f + i * TAU / 6), R + 44));
        rw_polyline(hex, 6, true, 4, sc);
        rw_glow(rc->pos, R + 60, col_a(sc, 0.12f));
        for (int i = 0; i < MAX_ROBOTS; i++) {
            Robot *r = &W.rob[i];
            if (!r->active || r->type != RB_PYLON) continue;
            V2 a = r->pos, b = rc->pos;
            V2 pts[7];
            V2 n = v2perp(v2norm(v2sub(b, a)));
            for (int k = 0; k < 7; k++) {
                float f = k / 6.0f;
                float wob = (k == 0 || k == 6) ? 0 : sinf(t * 23 + k * 1.7f + i) * 7;
                pts[k] = v2add(v2lerp(a, b, f), v2scale(n, wob));
            }
            rw_polyline(pts, 7, false, 3, col_a(RDEF[RB_PYLON].col, 0.6f * (r->spawn_t > 0 ? 1 - r->spawn_t : 1)));
        }
    }
}
