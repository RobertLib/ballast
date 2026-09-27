/*
 * Bullet patterns: spirals, rings, aimed fans, curving flowers, rotating
 * laser beams, seeker mines, flak novas and accelerating walls. The reactor
 * core, the Overseer and the pattern robots (pulsars, pylons, carriers) run
 * them through a Barrage: start a pattern, update it every frame from where
 * the emitter is, and draw its beams.
 */
#include "game_internal.h"

static float bullet_life(int type) {
    switch (type) {
    case EP_SHARD: return 4.0f;
    case EP_SEEKER: return 6.5f;
    case EP_FLAK: return 1.2f;
    default: return 6.0f;
    }
}

Proj *bullet(int type, V2 pos, float ang, float speed, float dmg, Col c) {
    if (point_in_rock(pos)) return NULL;
    Proj *p = spawn_proj(type, pos, v2scale(v2fromang(ang), speed), dmg, bullet_life(type));
    if (p) p->col = c;
    return p;
}

void bullet_ring(int type, V2 c, float r0, int n, float ang0, float speed, float dmg, Col col) {
    for (int i = 0; i < n; i++) {
        float a = ang0 + TAU * i / n;
        bullet(type, v2mad(c, v2fromang(a), r0), a, speed, dmg, col);
    }
}

void bullet_fan(int type, V2 c, float r0, float ang, int n, float spread, float speed, float dmg, Col col) {
    for (int i = 0; i < n; i++) {
        float a = n > 1 ? ang - spread * 0.5f + spread * i / (n - 1) : ang;
        bullet(type, v2mad(c, v2fromang(a), r0), a, speed, dmg, col);
    }
}

float aim_lead(V2 from, float speed) {
    Player *p = &W.pl;
    float t = v2dist(from, p->pos) / maxf(speed, 1);
    V2 target = v2mad(p->pos, p->vel, t * DIFF_LEAD[G.difficulty]);
    return v2ang(v2sub(target, from));
}

/* a flak shell or a seeker mine pops into a ring of pellets */
void flak_pop(Proj *pr) {
    if (pr->burst <= 0) return;
    int prev = g_proj_src;
    g_proj_src = pr->src;
    float sp = 175 * DIFF_PSPD[G.difficulty];
    bullet_ring(EP_PELLET, pr->pos, 4, pr->burst, frand() * TAU, sp, 5, pr->col);
    g_proj_src = prev;
    fx_ring(pr->pos, 6, 46, col_a(pr->col, 0.8f), 0.25f, 4);
    fx_glow(pr->pos, v2(0, 0), col_a(pr->col, 0.7f), 0.25f, 30);
    snd_play_at(SND_EN_SHOT, pr->pos, 0.45f, 0.55f);
}

int bullets_cancel(V2 c, float radius) {
    int n = 0;
    for (int i = 0; i < MAX_PROJ; i++) {
        Proj *e = &W.proj[i];
        if (!e->active || !IS_ENEMY_PROJ(e->type) || e->type == EP_MINE) continue;
        if (v2dist(e->pos, c) > radius) continue;
        e->active = false;
        n++;
        if (n < 200 || (n & 3) == 0) fx_spark(e->pos, v2scale(v2fromang(frand() * TAU), 60 + frand() * 80), col_white(e->col, 0.4f), 0.35f, 3);
    }
    return n;
}

/* ------------------------------------------------------------ beams */
V2 beam_end(V2 a, float ang, float maxlen) {
    V2 b = v2mad(a, v2fromang(ang), maxlen);
    RayHit h;
    if (raycast(a, b, &h)) return h.p;
    return b;
}

void draw_beam(V2 a, V2 b, float w, Col c, float warn) {
    float t = W.time;
    if (warn > 0) {
        /* the telegraph: a thin flickering line that thickens as the beam comes */
        float k = clampf(1 - warn, 0, 1);
        float fl = 0.45f + 0.35f * sinf(t * 42);
        rw_line(a, b, 2 + 3 * k, col_a(c, fl * (0.4f + 0.5f * k)));
        rw_glow(b, 10 + 8 * k, col_a(c, 0.4f * fl));
        return;
    }
    float jit = 0.85f + 0.15f * sinf(t * 57);
    rw_line(a, b, w * 2.6f * jit, col_a(c, 0.22f));
    rw_line(a, b, w * jit, c);
    rw_line(a, b, w * 0.35f, col_a(C_WHITE, 0.9f));
    rw_glow(b, w * 2.4f, col_a(col_white(c, 0.4f), 0.7f));
    rw_glow(a, w * 2.0f, col_a(col_white(c, 0.4f), 0.6f));
}

/* ------------------------------------------------------------ patterns */
void barrage_stop(Barrage *b) {
    b->pat = -1;
    b->beams_live = false;
    b->beam_warn = 0;
}

void barrage_start(Barrage *b, int pat, float dur) {
    b->pat = pat;
    b->t = 0;
    b->dur = dur;
    b->cd = 0.12f;
    b->cd2 = 0.6f;
    b->step = 0;
    float sign = frand() < 0.5f ? -1.0f : 1.0f;
    b->spin = sign * (1.5f + 0.4f * frand());
    b->ang = frand() * TAU;
    b->ang2 = b->ang;
    if (b->dens <= 0) b->dens = 1;
    if (b->speed <= 0) b->speed = 1;
    if (b->dmg <= 0) b->dmg = 1;
    if (pat == BP_LASERS) {
        b->nbeams = clampi(2 + (b->dens > 1.2f) + (b->dens > 1.6f), 2, BAR_BEAMS);
        b->beam_warn = 1.1f;
        b->beams_live = false;
        if (b->beam_w <= 0) b->beam_w = 9;
        b->beam_spin = sign * (0.42f + 0.16f * b->dens) * DIFF_PSPD[G.difficulty];
        for (int i = 0; i < BAR_BEAMS; i++) b->beam_len[i] = 0;
    }
}

static void beams_update(Barrage *b, V2 pos, float r, float dt) {
    Player *p = &W.pl;
    if (b->beam_warn > 0) {
        b->beam_warn -= dt;
        b->beam_ang += b->beam_spin * 0.25f * dt;
    } else {
        b->beams_live = true;
        /* the fiercer beams turn back halfway through */
        if (b->dens >= 1.4f && !(b->step & 256) && b->t > b->dur * 0.55f) {
            b->step |= 256;
            b->beam_spin = -b->beam_spin * 1.15f;
        }
        b->beam_ang += b->beam_spin * dt;
    }
    for (int i = 0; i < b->nbeams; i++) {
        float a = b->beam_ang + TAU * i / b->nbeams;
        V2 s = v2mad(pos, v2fromang(a), r);
        V2 e = beam_end(s, a, 1900);
        b->beam_len[i] = v2dist(s, e);
        if (b->beams_live && frand() < dt * 30) fx_spark(e, v2scale(v2fromang(frand() * TAU), 120 + frand() * 160), col_white(b->col, 0.3f), 0.25f, 3);
        if (b->beams_live && player_hurt_by(s, e, b->beam_w)) {
            V2 n = v2perp(v2fromang(a));
            if (v2dot(n, v2sub(p->pos, s)) < 0) n = v2scale(n, -1);
            hazard_touch(55 * b->dmg, dt, n, b->src);
        }
    }
}

void barrage_update(Barrage *b, V2 pos, float r, float dt) {
    if (b->pat < 0) return;
    b->t += dt;
    if (b->t >= b->dur) { barrage_stop(b); return; }
    float fm = DIFF_FIRE[G.difficulty];
    float sp = DIFF_PSPD[G.difficulty] * b->speed, dm = b->dmg, dn = b->dens;
    int prev_src = g_proj_src;
    g_proj_src = b->src;
    b->cd -= dt * fm;
    b->cd2 -= dt * fm;
    switch (b->pat) {
    case BP_SPIRAL: {
        int arms = clampi((int)roundf(3 * dn), 2, 8);
        b->ang += b->spin * dt;
        b->ang2 -= b->spin * 0.8f * dt;
        while (b->cd <= 0) {
            b->cd += 0.09f;
            for (int i = 0; i < arms; i++) {
                float a = b->ang + TAU * i / arms;
                bullet(EP_PELLET, v2mad(pos, v2fromang(a), r), a, 215 * sp, 5 * dm, b->col);
            }
            if (dn >= 1.4f && (b->step & 1)) {
                for (int i = 0; i < arms; i++) {
                    float a = b->ang2 + TAU * i / arms;
                    bullet(EP_PELLET, v2mad(pos, v2fromang(a), r), a, 165 * sp, 5 * dm, b->col2);
                }
            }
            b->step++;
        }
    } break;
    case BP_RINGS:
        if (b->cd <= 0) {
            b->cd = 0.85f;
            int n = clampi((int)(18 * dn), 10, 48);
            int gap = dn >= 1.3f ? irand(n) : -1;
            float off = b->ang + ((b->step & 1) ? PI / n : 0);
            float speed = (b->step % 3 == 2 ? 250 : 175) * sp;
            for (int i = 0; i < n; i++) {
                if (gap >= 0 && (i - gap + n) % n < 3) continue;
                float a = off + TAU * i / n;
                bullet(EP_PELLET, v2mad(pos, v2fromang(a), r), a, speed, 5 * dm, (b->step & 1) ? b->col2 : b->col);
            }
            b->step++;
            snd_play_at(SND_EN_SHOT, pos, 0.5f, 0.7f);
        }
        break;
    case BP_FANS:
        if (b->cd <= 0) {
            int n = clampi(5 + (int)(dn * 2), 5, 11);
            float speed = 330 * sp;
            bullet_fan(EP_SHARD, pos, r, aim_lead(pos, speed), n, 0.75f, speed, 6 * dm, b->col2);
            b->step++;
            b->cd = b->step % 3 == 0 ? 0.8f : 0.15f;
            snd_play_at(SND_EN_SHOT, pos, 0.45f, 1.2f);
        }
        break;
    case BP_FLOWER:
        if (b->cd <= 0) {
            b->cd = 0.5f;
            int n = clampi((int)(12 * dn), 8, 30);
            float sign = (b->step & 1) ? 1.0f : -1.0f;
            for (int i = 0; i < n; i++) {
                float a = b->ang + TAU * i / n;
                Proj *pr = bullet(EP_PELLET, v2mad(pos, v2fromang(a), r), a, 110 * sp, 5 * dm, (b->step & 1) ? b->col : b->col2);
                if (!pr) continue;
                pr->curve = sign * 1.5f;
                pr->accel = 95;
                pr->vmin = 60;
                pr->vmax = 300 * sp;
            }
            b->ang += 0.21f;
            b->step++;
            snd_play_at(SND_EN_SHOT, pos, 0.4f, 0.9f);
        }
        break;
    case BP_LASERS:
        beams_update(b, pos, r, dt);
        if (b->beams_live && b->cd2 <= 0) {
            b->cd2 = 1.35f;
            bullet_ring(EP_PELLET, pos, r, clampi((int)(10 * dn), 6, 24), frand() * TAU, 140 * sp, 5 * dm, b->col2);
        }
        break;
    case BP_SEEKERS:
        if (b->step == 0) {
            b->step = 1;
            int n = clampi((int)(4 + 2 * dn), 4, 10);
            for (int i = 0; i < n; i++) {
                float a = b->ang + TAU * i / n;
                Proj *pr = bullet(EP_SEEKER, v2mad(pos, v2fromang(a), r + 6), a, 150 * sp, 9 * dm, b->col);
                if (pr) { pr->turn = 1.4f; pr->burst = 8; }
            }
            snd_play_at(SND_EN_MISSILE, pos, 0.7f, 0.6f);
        }
        if (b->cd <= 0) {
            b->cd = 0.9f;
            float speed = 340 * sp;
            bullet_fan(EP_SHARD, pos, r, aim_lead(pos, speed), 3, 0.3f, speed, 6 * dm, b->col2);
        }
        break;
    case BP_NOVA:
        if (b->cd <= 0) {
            b->cd = 1.5f;
            int n = clampi((int)(6 * dn), 5, 12);
            float off = b->ang + ((b->step & 1) ? PI / n : 0);
            for (int i = 0; i < n; i++) {
                float a = off + TAU * i / n;
                Proj *pr = bullet(EP_FLAK, v2mad(pos, v2fromang(a), r + 4), a, 150 * sp, 10 * dm, b->col);
                if (pr) { pr->life = 1.15f + 0.1f * (i & 1); pr->burst = clampi((int)(8 * dn), 6, 14); }
            }
            b->step++;
            snd_play_at(SND_EN_MISSILE, pos, 0.6f, 0.5f);
        }
        break;
    case BP_WALLS:
        if (b->cd <= 0) {
            b->cd = 1.15f;
            int dirs = dn >= 1.4f ? 6 : 4;
            int k = clampi((int)(5 + 2 * dn), 5, 9);
            for (int d = 0; d < dirs; d++) {
                float a = b->ang + TAU * d / dirs;
                V2 f = v2fromang(a), side = v2perp(f);
                for (int j = 0; j < k; j++) {
                    V2 at = v2add(v2mad(pos, f, r + 6), v2scale(side, (j - (k - 1) * 0.5f) * 26));
                    Proj *pr = bullet(EP_SHARD, at, a, 35, 6 * dm, (d & 1) ? b->col2 : b->col);
                    if (pr) { pr->accel = 230; pr->vmin = 35; pr->vmax = 390 * sp; }
                }
            }
            b->ang += PI / dirs * 0.5f;
            b->step++;
            snd_play_at(SND_EN_SHOT, pos, 0.5f, 0.6f);
        }
        break;
    }
    g_proj_src = prev_src;
}

void barrage_draw(const Barrage *b, V2 pos, float r) {
    if (b->pat != BP_LASERS) return;
    for (int i = 0; i < b->nbeams; i++) {
        float a = b->beam_ang + TAU * i / b->nbeams;
        V2 s = v2mad(pos, v2fromang(a), r);
        V2 e = v2mad(s, v2fromang(a), b->beam_len[i]);
        draw_beam(s, e, b->beam_w * 2, b->col, b->beam_warn > 0 ? b->beam_warn : 0);
    }
}
