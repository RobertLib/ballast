/*
 * Infected mining robots: definitions, vector shapes, AI behaviours,
 * the reactor core, robot generators (matcens) and the Overseer boss.
 */
#include "game_internal.h"

const RobotDef RDEF[RB_COUNT] = {
    /* name, radius, hp, speed, accel, turn, ai, weapon, fire, burst, gap, range, sight, pts, col, contact, drop, cloaked, mass */
    [RB_DRONE] = {"CLASS 1 DRONE", 14, 30, 160, 520, 5, AI_RUSHER, RW_PULSE, 1.5f, 1, 0, 520, 600, 100, {0.3f, 1, 0.45f, 1}, 0, 0.28f, false, 1},
    [RB_LIFTER] = {"MEDIUM LIFTER", 16, 50, 250, 700, 6, AI_MELEE, RW_NONE, 0, 0, 0, 0, 620, 150, {1, 0.55f, 0.1f, 1}, 13, 0.22f, false, 1.2f},
    [RB_TURRET] = {"PLATFORM TURRET", 18, 80, 0, 0, 2.6f, AI_TURRET, RW_BOLT, 2.3f, 3, 0.13f, 620, 660, 200, {0.2f, 0.9f, 1, 1}, 0, 0.3f, false, 99},
    [RB_HULK] = {"MEDIUM HULK", 22, 160, 110, 300, 2.8f, AI_KEEPER, RW_MISSILE, 2.8f, 1, 0, 660, 680, 400, {0.7f, 0.4f, 1, 1}, 0, 0.6f, false, 3},
    [RB_SPIDER] = {"SPIDER PROCESSOR", 20, 100, 140, 400, 3.5f, AI_KEEPER, RW_NEEDLE, 1.7f, 2, 0.16f, 560, 620, 300, {1, 0.3f, 0.8f, 1}, 0, 0.3f, false, 2},
    [RB_BABY] = {"BABY SPIDER", 9, 14, 290, 900, 7, AI_RUSHER, RW_BABYLASER, 1.3f, 1, 0, 420, 600, 50, {1, 0.55f, 0.9f, 1}, 0, 0.06f, false, 0.6f},
    [RB_GOPHER] = {"CLASS 3 GOPHER", 12, 28, 270, 800, 6, AI_EVADER, RW_MINE, 1.6f, 1, 0, 400, 560, 200, {1, 0.9f, 0.2f, 1}, 0, 0.4f, false, 0.8f},
    [RB_DRILLER] = {"VULCAN DRILLER", 17, 120, 150, 450, 4, AI_AMBUSH, RW_VULCAN, 2.3f, 8, 0.07f, 520, 440, 450, {1, 0.25f, 0.2f, 1}, 0, 0.45f, false, 2},
    [RB_SUPERHULK] = {"SUPER HULK", 27, 380, 105, 300, 2.4f, AI_KEEPER, RW_HOMING, 3.2f, 2, 0.35f, 720, 720, 1000, {0.9f, 0.25f, 1, 1}, 0, 0.85f, false, 5},
    [RB_CLOAKER] = {"CLOAKED LIFTER", 16, 45, 265, 750, 6, AI_MELEE, RW_NONE, 0, 0, 0, 0, 640, 300, {0.6f, 0.9f, 1, 1}, 15, 0.4f, true, 1.2f},
    [RB_BOSS] = {"THE OVERSEER", 58, 4200, 95, 260, 1.5f, AI_BOSS, RW_NONE, 0, 0, 0, 900, 900, 25000, {1, 0.2f, 0.4f, 1}, 20, 0, false, 40},
};

Shape RSHAPE[RB_COUNT];

static void poly(Stroke *s, int n, float r, float rot) {
    s->n = n;
    s->closed = true;
    for (int i = 0; i < n; i++) s->p[i] = v2scale(v2fromang(rot + TAU * i / n), r);
}
static void star(Stroke *s, int n, float r0, float r1, float rot) {
    s->n = n * 2;
    s->closed = true;
    for (int i = 0; i < n * 2; i++) s->p[i] = v2scale(v2fromang(rot + PI * i / n), (i & 1) ? r1 : r0);
}
static void pts(Stroke *s, bool closed, int n, const float *xy) {
    s->n = n;
    s->closed = closed;
    for (int i = 0; i < n; i++) s->p[i] = v2(xy[i * 2], xy[i * 2 + 1]);
}

void robots_init_shapes(void) {
    memset(RSHAPE, 0, sizeof(RSHAPE));
    Shape *s;
    /* drone: GW-style diamond with a core */
    s = &RSHAPE[RB_DRONE];
    {
        static const float a[] = {1.1f, 0, 0, 0.8f, -0.9f, 0, 0, -0.8f};
        pts(&s->s[0], true, 4, a);
        static const float b[] = {0.35f, 0, 0, 0.3f, -0.35f, 0, 0, -0.3f};
        pts(&s->s[1], true, 4, b);
        s->ns = 2;
    }
    /* lifter: pincers */
    s = &RSHAPE[RB_LIFTER];
    {
        static const float body[] = {0.25f, 0, -0.35f, 0.5f, -0.95f, 0, -0.35f, -0.5f};
        pts(&s->s[0], true, 4, body);
        static const float c1[] = {-0.15f, -0.45f, 0.5f, -0.85f, 1.05f, -0.25f};
        pts(&s->s[1], false, 3, c1);
        static const float c2[] = {-0.15f, 0.45f, 0.5f, 0.85f, 1.05f, 0.25f};
        pts(&s->s[2], false, 3, c2);
        s->ns = 3;
    }
    RSHAPE[RB_CLOAKER] = RSHAPE[RB_LIFTER];
    {
        Shape *c = &RSHAPE[RB_CLOAKER];
        poly(&c->s[3], 6, 0.3f, 0);
        c->ns = 4;
    }
    /* turret: nested hexagons (barrel drawn separately) */
    s = &RSHAPE[RB_TURRET];
    poly(&s->s[0], 6, 1.0f, 0);
    poly(&s->s[1], 6, 0.5f, PI / 6);
    s->ns = 2;
    /* hulk: armoured box with launchers */
    s = &RSHAPE[RB_HULK];
    {
        static const float b[] = {1, 0.5f, 0.5f, 1, -0.7f, 1, -1, 0.6f, -1, -0.6f, -0.7f, -1, 0.5f, -1, 1, -0.5f};
        pts(&s->s[0], true, 8, b);
        static const float l1[] = {0.2f, 0.55f, 1.2f, 0.55f};
        pts(&s->s[1], false, 2, l1);
        static const float l2[] = {0.2f, -0.55f, 1.2f, -0.55f};
        pts(&s->s[2], false, 2, l2);
        poly(&s->s[3], 4, 0.35f, PI / 4);
        s->ns = 4;
    }
    /* spider: spiky star */
    s = &RSHAPE[RB_SPIDER];
    star(&s->s[0], 8, 1.05f, 0.6f, 0);
    poly(&s->s[1], 4, 0.35f, PI / 4);
    s->ns = 2;
    s = &RSHAPE[RB_BABY];
    star(&s->s[0], 5, 1.1f, 0.5f, 0);
    s->ns = 1;
    /* gopher: dart */
    s = &RSHAPE[RB_GOPHER];
    {
        static const float d[] = {1.1f, 0, -0.75f, 0.85f, -0.35f, 0, -0.75f, -0.85f};
        pts(&s->s[0], true, 4, d);
        s->ns = 1;
    }
    /* driller: pentagon with drill spike */
    s = &RSHAPE[RB_DRILLER];
    poly(&s->s[0], 5, 0.9f, PI);
    {
        static const float sp[] = {0.45f, -0.45f, 1.35f, 0, 0.45f, 0.45f};
        pts(&s->s[1], false, 3, sp);
        static const float f1[] = {-0.7f, 0.55f, -1.15f, 0.85f};
        pts(&s->s[2], false, 2, f1);
        static const float f2[] = {-0.7f, -0.55f, -1.15f, -0.85f};
        pts(&s->s[3], false, 2, f2);
        s->ns = 4;
    }
    /* super hulk */
    s = &RSHAPE[RB_SUPERHULK];
    poly(&s->s[0], 8, 1.0f, PI / 8);
    poly(&s->s[1], 4, 0.62f, 0);
    {
        static const float l1[] = {0.3f, 0.5f, 1.25f, 0.5f};
        pts(&s->s[2], false, 2, l1);
        static const float l2[] = {0.3f, -0.5f, 1.25f, -0.5f};
        pts(&s->s[3], false, 2, l2);
        poly(&s->s[4], 3, 0.25f, 0);
        s->ns = 5;
    }
    /* boss: debris shape only, drawn procedurally */
    s = &RSHAPE[RB_BOSS];
    poly(&s->s[0], 12, 1.0f, 0);
    star(&s->s[1], 6, 0.8f, 0.5f, 0);
    poly(&s->s[2], 16, 0.35f, 0);
    s->ns = 3;
}

Robot *robot_spawn(int type, V2 pos, bool from_matcen) {
    for (int i = 0; i < MAX_ROBOTS; i++) {
        Robot *r = &W.rob[i];
        if (r->active) continue;
        memset(r, 0, sizeof(*r));
        const RobotDef *d = &RDEF[type];
        r->active = true;
        r->type = type;
        r->pos = r->home = r->last_pos = pos;
        r->ang = frand() * TAU;
        r->radius = d->radius;
        r->maxhp = r->hp = d->hp * DIFF_HP[G.difficulty];
        r->think_t = frand() * 0.3f;
        r->fire_cd = 1.0f + frand();
        r->strafe_dir = frand() < 0.5f ? -1.0f : 1.0f;
        r->strafe_t = 1 + frand() * 2;
        r->anim = frand() * 10;
        r->from_matcen = from_matcen;
        r->cloak_vis = d->cloaked ? 0 : 1;
        if (type == RB_BOSS) {
            r->attack_cd = 2.0f;
            r->attack2_cd = 4.0f;
            r->spawn_cd = 8.0f;
            r->spiral_cd = 9.0f;
            r->ang = PI / 2;
        }
        return r;
    }
    return NULL;
}

static void alert(Robot *r) {
    if (r->aware) return;
    r->aware = true;
    r->fire_cd = maxf(r->fire_cd, 0.5f + frand() * 0.7f);
    if (W.screech_cd <= 0 && r->type != RB_TURRET && r->type != RB_BOSS) {
        static const float pitch[RB_COUNT] = {1.2f, 0.9f, 1, 0.7f, 1.0f, 1.5f, 1.3f, 0.8f, 0.6f, 1.0f, 0.5f};
        snd_play_at(SND_SCREECH, r->pos, 0.55f, pitch[r->type] * frandr(0.9f, 1.1f));
        W.screech_cd = 0.6f;
    }
    fx_ring(r->pos, r->radius, r->radius * 2.2f, col_a(RDEF[r->type].col, 0.6f), 0.3f, 3);
}

static void alert_nearby(V2 pos, float radius) {
    for (int i = 0; i < MAX_ROBOTS; i++) {
        Robot *o = &W.rob[i];
        if (!o->active || o->aware) continue;
        if (v2dist(o->pos, pos) < radius && los(o->pos, pos)) {
            alert(o);
            o->last_seen = W.pl.pos;
        }
    }
}

static void drop_loot(Robot *r) {
    const RobotDef *d = &RDEF[r->type];
    Player *p = &W.pl;
    float chance = d->drop;
    if (r->from_matcen) chance *= 0.5f;
    if (p->energy < 40) chance += 0.1f;
    if (frand() >= chance) return;
    int type = PU_ENERGY, amount = 1;
    float roll = frand();
    switch (r->type) {
    case RB_HULK:
    case RB_SUPERHULK:
        if (roll < 0.55f) { type = PU_CONC; amount = r->type == RB_SUPERHULK ? 4 : 2; }
        else if (roll < 0.85f) { type = PU_HOMING; amount = r->type == RB_SUPERHULK ? 2 : 1; }
        else type = PU_SHIELD;
        break;
    case RB_GOPHER:
        if (roll < 0.6f) { type = PU_PROX; amount = 2; }
        else type = PU_ENERGY;
        break;
    case RB_DRILLER:
        if (roll < 0.6f && (p->owned & (1 << PW_VULCAN))) { type = PU_VAMMO; amount = 300; }
        else type = roll < 0.8f ? PU_ENERGY : PU_SHIELD;
        break;
    case RB_CLOAKER:
        type = roll < 0.25f ? PU_CLOAK : PU_SHIELD;
        break;
    default:
        if (p->shield < 60 && roll < 0.5f) type = PU_SHIELD;
        else if (roll < 0.55f) type = PU_ENERGY;
        else if (roll < 0.85f) type = PU_SHIELD;
        else { type = PU_CONC; amount = 1; }
        break;
    }
    spawn_powerup(type, r->pos, v2scale(v2fromang(frand() * TAU), 60 + frand() * 80), amount);
}

static void robot_kill(Robot *r, bool by_player) {
    const RobotDef *d = &RDEF[r->type];
    r->active = false;
    G.robots_killed++;
    if (by_player) chain_kill(r->pos, d->points);
    fx_shape_debris(&RSHAPE[r->type], r->pos, r->ang, r->radius, r->vel, d->col);
    fx_explosion(r->pos, r->radius * 1.7f, d->col);
    int snd = r->radius < 15 ? SND_EXPL_S : r->radius < 24 ? SND_EXPL_M : SND_EXPL_L;
    snd_play_at(snd, r->pos, 0.8f, frandr(0.9f, 1.15f));
    float pd = v2dist(r->pos, W.pl.pos);
    shake_add(clampf(r->radius / 60.0f, 0.1f, 0.5f) * clampf(1 - pd / 900, 0, 1));
    drop_loot(r);
    if (r->type == RB_SPIDER) {
        for (int i = 0; i < 3; i++) {
            V2 off = v2scale(v2fromang(TAU * i / 3 + frand()), 14);
            Robot *b = robot_spawn(RB_BABY, v2add(r->pos, off), true);
            if (b) {
                b->vel = v2scale(v2norm(off), 260);
                b->aware = true;
                b->last_seen = W.pl.pos;
                b->fire_cd = 0.8f + frand() * 0.6f;
                G.robots_total++;
            }
        }
    }
    if (r->type == RB_BOSS) {
        W.boss_death_t = 2.6f;
        W.boss_death_pos = r->pos;
        snd_play(SND_BOSS, 1.0f, 0.7f);
        shake_add(1.0f);
        hud_msg("THE OVERSEER IS BREAKING APART!", C_MAGENTA);
        /* clear its summons for a clean finale */
        for (int i = 0; i < MAX_PROJ; i++)
            if (W.proj[i].active && IS_ENEMY_PROJ(W.proj[i].type)) W.proj[i].life = minf(W.proj[i].life, 0.05f + frand() * 0.4f);
    }
}

void robot_damage(Robot *r, float dmg, V2 dir, bool by_player) {
    if (!r->active || dmg <= 0) return;
    if (r->type == RB_BOSS && r->invis_t > 0) return;
    r->hp -= dmg;
    r->hit_flash = 0.12f;
    r->cloak_vis = 1;
    r->vel = v2mad(r->vel, dir, clampf(dmg * 4 / RDEF[r->type].mass, 0, 220));
    if (by_player) {
        if (!r->aware) alert(r);
        r->aware = true;
        r->last_seen = W.pl.pos;
        r->lost_t = 0;
        alert_nearby(r->pos, 320);
    }
    if (r->type == RB_BOSS) r->dmg_accum += dmg;
    snd_play_at(SND_HIT, r->pos, 0.4f, frandr(0.85f, 1.2f) * (r->radius > 24 ? 0.7f : 1.0f));
    if (r->hp <= 0) robot_kill(r, by_player);
}

/* ------------------------------------------------------------ AI helpers */
static V2 aim_at_player(Robot *r, float proj_speed, V2 from) {
    Player *p = &W.pl;
    V2 target = p->pos;
    if (p->cloak_t > 0) target = v2add(r->last_seen, v2(frandr(-40, 40), frandr(-40, 40)));
    float dist = v2dist(from, target);
    float t = dist / maxf(proj_speed, 1);
    target = v2mad(target, p->vel, t * DIFF_LEAD[G.difficulty]);
    float a = v2ang(v2sub(target, from)) + frandr(-1, 1) * DIFF_SPREAD[G.difficulty];
    return v2fromang(a);
}

static void robot_fire(Robot *r) {
    const RobotDef *d = &RDEF[r->type];
    V2 fw = v2fromang(r->ang);
    V2 muzzle = safe_muzzle(r->pos, v2mad(r->pos, fw, r->radius + 4));
    float ps = DIFF_PSPD[G.difficulty];
    float dm = 1.0f;
    Proj *pr;
    switch (d->weapon) {
    case RW_PULSE: {
        float sp = 390 * ps;
        spawn_proj(EP_PULSE, muzzle, v2scale(aim_at_player(r, sp, muzzle), sp), 6 * dm, 2.5f);
        snd_play_at(SND_EN_SHOT, r->pos, 0.45f, 1.1f);
    } break;
    case RW_BOLT: {
        float sp = 500 * ps;
        spawn_proj(EP_BOLT, muzzle, v2scale(aim_at_player(r, sp, muzzle), sp), 5 * dm, 2.0f);
        snd_play_at(SND_EN_SHOT, r->pos, 0.4f, 1.5f);
    } break;
    case RW_BABYLASER: {
        float sp = 450 * ps;
        spawn_proj(EP_BOLT, muzzle, v2scale(aim_at_player(r, sp, muzzle), sp), 3 * dm, 1.6f);
        snd_play_at(SND_EN_SHOT, r->pos, 0.3f, 1.9f);
    } break;
    case RW_NEEDLE: {
        float sp = 660 * ps;
        V2 side = v2scale(v2perp(fw), 8);
        V2 dir = aim_at_player(r, sp, muzzle);
        spawn_proj(EP_NEEDLE, v2add(muzzle, side), v2scale(dir, sp), 8 * dm, 1.6f);
        spawn_proj(EP_NEEDLE, v2sub(muzzle, side), v2scale(dir, sp), 8 * dm, 1.6f);
        snd_play_at(SND_EN_SHOT, r->pos, 0.45f, 1.3f);
    } break;
    case RW_VULCAN: {
        float sp = 720 * ps;
        V2 dir = aim_at_player(r, sp, muzzle);
        dir = v2rot(dir, frandr(-0.06f, 0.06f));
        spawn_proj(EP_VULCAN, muzzle, v2scale(dir, sp), 3.2f * dm, 1.2f);
        snd_play_at(SND_VULCAN, r->pos, 0.35f, 0.8f);
    } break;
    case RW_MISSILE: {
        float sp = 340 * ps;
        V2 side = v2scale(v2perp(fw), r->radius * 0.55f);
        V2 dir = aim_at_player(r, sp, muzzle);
        V2 mz = safe_muzzle(r->pos, v2add(v2mad(r->pos, fw, r->radius * 1.1f), (r->burst_left & 1) ? side : v2scale(side, -1)));
        spawn_proj(EP_MISSILE, mz, v2scale(dir, sp), 17 * dm, 3.5f);
        snd_play_at(SND_EN_MISSILE, r->pos, 0.6f, 1.0f);
    } break;
    case RW_HOMING: {
        float sp = 300 * ps;
        V2 side = v2scale(v2perp(fw), r->radius * 0.5f);
        V2 mz = safe_muzzle(r->pos, v2add(v2mad(r->pos, fw, r->radius * 1.1f), (r->burst_left & 1) ? side : v2scale(side, -1)));
        pr = spawn_proj(EP_HOMING, mz, v2scale(v2rot(fw, (r->burst_left & 1) ? 0.4f : -0.4f), sp), 20 * dm, 5.0f);
        if (pr) pr->turn = 1.9f + 0.2f * G.difficulty;
        snd_play_at(SND_EN_MISSILE, r->pos, 0.6f, 0.8f);
    } break;
    case RW_MINE: {
        V2 back = safe_muzzle(r->pos, v2mad(r->pos, fw, -r->radius - 6));
        spawn_proj(EP_MINE, back, v2add(v2scale(r->vel, 0.2f), v2scale(fw, -40)), 18 * dm, 18);
        snd_play_at(SND_PROX, r->pos, 0.4f, 0.8f);
    } break;
    }
    r->muzzle = 0;
}

static V2 wall_avoid(Robot *r, V2 desired) {
    if (v2len2(desired) < 0.01f) return desired;
    V2 ahead = v2mad(r->pos, v2norm(desired), r->radius + 34);
    RayHit h;
    if (raycast(r->pos, ahead, &h)) {
        V2 t = v2perp(h.n);
        if (v2dot(t, desired) < 0) t = v2scale(t, -1);
        return v2norm(v2add(v2scale(h.n, 0.6f), t));
    }
    return desired;
}

static void boss_update(Robot *r, float dt);

static void robot_update(Robot *r, float dt) {
    const RobotDef *d = &RDEF[r->type];
    Player *p = &W.pl;
    r->anim += dt;
    r->hit_flash = maxf(0, r->hit_flash - dt);
    r->melee_cd -= dt;
    V2 to = v2sub(p->pos, r->pos);
    float dist = v2len(to);
    V2 dir = dist > 1 ? v2scale(to, 1.0f / dist) : v2(1, 0);

    /* perception, staggered */
    r->think_t -= dt;
    if (r->think_t <= 0) {
        r->think_t = 0.15f + frand() * 0.1f;
        bool see = false;
        float sight = d->sight;
        if (!r->aware && d->ai == AI_AMBUSH) sight *= 0.8f;
        if (dist < sight && player_visible_to(r->pos, dist)) see = true;
        r->sees = see;
        if (see) {
            if (!r->aware) alert(r);
            r->aware = true;
            r->last_seen = p->pos;
            r->lost_t = 0;
        }
    }
    if (p->dead || G.escaping) r->sees = false;
    if (r->aware && !r->sees) {
        r->lost_t += dt;
        if (r->lost_t > 14 && !G.reactor_dead) r->aware = false;
    }
    if (d->cloaked) {
        float target = (dist < 170 || r->hit_flash > 0 || r->muzzle > 0) ? 1.0f : 0.06f;
        r->cloak_vis = lerpf(r->cloak_vis, target, damp_factor(4, dt));
    }

    if (d->ai == AI_BOSS) {
        boss_update(r, dt);
        return;
    }

    V2 desired = v2(0, 0);
    float speed = d->speed * (0.9f + 0.1f * G.difficulty / 2.0f);
    float face = r->ang;
    bool has_face = false;
    r->strafe_t -= dt;
    if (r->strafe_t <= 0) {
        r->strafe_t = 1.2f + frand() * 2.0f;
        if (frand() < 0.6f) r->strafe_dir = -r->strafe_dir;
    }
    V2 side = v2scale(v2perp(dir), r->strafe_dir);

    if (!r->aware) {
        /* idle hover around home */
        V2 wob = v2(sinf(r->anim * 0.7f + r->home.x) * 22, cosf(r->anim * 0.9f + r->home.y) * 22);
        V2 goal = v2add(r->home, wob);
        V2 dd = v2sub(goal, r->pos);
        if (v2len(dd) > 4) desired = v2scale(v2norm(dd), 0.3f);
        speed *= 0.5f;
        if (d->ai == AI_TURRET) { face = r->ang + dt * 0.6f; has_face = true; }
    } else if (!r->sees) {
        /* hunt: follow the flow field toward the player */
        if (d->ai != AI_TURRET) {
            V2 fdir = flow_dir(L.flow, r->pos, true);
            if (v2len2(fdir) < 0.01f) fdir = v2norm(v2sub(r->last_seen, r->pos));
            desired = fdir;
            if (d->ai == AI_EVADER) speed *= 0.8f;
        }
    } else {
        has_face = true;
        face = v2ang(to);
        switch (d->ai) {
        case AI_RUSHER: {
            float pref = r->type == RB_BABY ? 120 : 200;
            if (dist > pref) desired = v2add(dir, v2scale(side, 0.35f * sinf(r->anim * 2)));
            else desired = v2add(v2scale(side, 0.9f), v2scale(dir, -0.3f));
        } break;
        case AI_MELEE: {
            if (r->melee_cd <= 0) {
                V2 lead = v2mad(p->pos, p->vel, 0.25f);
                desired = v2norm(v2sub(lead, r->pos));
                speed *= 1.15f;
            } else {
                desired = v2add(v2scale(dir, -0.8f), v2scale(side, 0.8f));
            }
        } break;
        case AI_KEEPER:
        case AI_AMBUSH: {
            float pref = d->ai == AI_AMBUSH ? 230 : d->range * 0.52f;
            if (dist > pref + 70) desired = v2add(dir, v2scale(side, 0.3f));
            else if (dist < pref - 70) desired = v2add(v2scale(dir, -1), v2scale(side, 0.4f));
            else desired = side;
        } break;
        case AI_EVADER: {
            if (dist < 270) {
                V2 away = flow_dir(L.flow, r->pos, false);
                if (v2len2(away) < 0.01f) away = v2scale(dir, -1);
                desired = v2add(away, v2scale(side, 0.5f));
                face = v2ang(v2scale(desired, -1)); /* face away: mines drop from the rear toward the player */
                face = v2ang(away);
                speed *= 1.1f;
            } else if (dist > 380) desired = v2add(dir, v2scale(side, 0.5f));
            else desired = side;
        } break;
        case AI_TURRET:
        default: break;
        }
    }

    /* movement */
    if (d->speed > 0) {
        if (v2len2(desired) > 0.0001f) {
            if (v2len(desired) > 1) desired = v2norm(desired);
            desired = wall_avoid(r, desired);
        }
        V2 want = v2scale(desired, speed);
        V2 dv = v2sub(want, r->vel);
        r->vel = v2add(r->vel, v2clamplen(dv, d->accel * dt));
        r->pos = v2mad(r->pos, r->vel, dt);
        circle_collide(&r->pos, r->radius, &r->vel, 0.2f);
        if (!has_face && v2len2(r->vel) > 400) face = v2ang(r->vel), has_face = true;
    } else {
        r->vel = v2(0, 0);
    }
    if (has_face) r->ang = approach_angle(r->ang, face, d->turn * dt);
    else r->ang = approach_angle(r->ang, r->ang + 0.3f, d->turn * dt * 0.2f);

    /* melee contact */
    if (d->contact > 0 && !p->dead && !G.escaping && dist < r->radius + p->radius + 3 && r->melee_cd <= 0) {
        player_damage(d->contact, dir);
        r->melee_cd = 0.9f;
        r->vel = v2scale(dir, -220);
        p->vel = v2mad(p->vel, dir, 160);
        fx_burst(v2mad(r->pos, dir, r->radius), 10, d->col, 220, 0.3f, 3);
        snd_play_at(SND_HIT, r->pos, 0.6f, 0.6f);
    }

    /* weapons */
    if (d->weapon != RW_NONE && r->aware && r->sees && !p->dead && in_view(r->pos, 8)) {
        r->fire_cd -= dt * DIFF_FIRE[G.difficulty];
        float facing = fabsf(wrap_angle(v2ang(to) - r->ang));
        bool in_range = dist < d->range;
        if (d->weapon == RW_MINE) {
            /* gophers lay mines when the player is near and behind them */
            in_range = dist < 320;
            facing = 0;
        }
        if (r->fire_cd < 0.3f && in_range) r->muzzle = clampf(1 - r->fire_cd / 0.3f, 0, 1);
        if (r->burst_left > 0) {
            r->burst_cd -= dt;
            if (r->burst_cd <= 0) {
                robot_fire(r);
                r->burst_left--;
                r->burst_cd = d->burst_gap;
            }
        } else if (r->fire_cd <= 0 && in_range && facing < 0.5f) {
            r->burst_left = d->burst > 0 ? d->burst : 1;
            r->burst_cd = 0;
            r->fire_cd = d->fire_delay * frandr(0.8f, 1.25f);
        }
    } else {
        r->muzzle = maxf(0, r->muzzle - dt * 3);
        r->burst_left = 0;
    }
}

/* ------------------------------------------------------------ boss */
static void boss_teleport(Robot *r) {
    for (int tries = 0; tries < 60; tries++) {
        V2 cand = v2add(r->home, v2(frandr(-15, 15) * TILE, frandr(-6, 6) * TILE));
        int tx = tx_of(cand.x), ty = tx_of(cand.y);
        if (!tile_in(tx, ty) || L.tile[ty][tx] != T_EMPTY) continue;
        V2 c = tile_center(tx, ty);
        bool free = true;
        for (int dy = -1; dy <= 1 && free; dy++)
            for (int dx = -1; dx <= 1 && free; dx++)
                if (tile_blocks(tx + dx, ty + dy)) free = false;
        if (!free) continue;
        if (v2dist(c, W.pl.pos) < 260) continue;
        if (!los(c, r->home) && v2dist(c, r->home) > 200) continue;
        fx_ring(r->pos, r->radius * 2, 10, col_a(RDEF[RB_BOSS].col, 0.8f), 0.5f, 6);
        r->pos = c;
        r->vel = v2(0, 0);
        fx_ring(c, 10, r->radius * 2.5f, col_a(C_WHITE, 0.8f), 0.6f, 6);
        grid_impulse(c, 250, -300);
        return;
    }
}

static void boss_update(Robot *r, float dt) {
    Player *p = &W.pl;
    V2 to = v2sub(p->pos, r->pos);
    float dist = v2len(to);
    V2 dir = dist > 1 ? v2scale(to, 1.0f / dist) : v2(1, 0);
    float hpf = r->hp / r->maxhp;
    int phase = hpf > 0.6f ? 0 : hpf > 0.3f ? 1 : 2;
    if (phase != r->phase) {
        r->phase = phase;
        snd_play(SND_BOSS, 0.9f, 0.9f - phase * 0.1f);
        shake_add(0.5f);
        hud_msg(phase == 1 ? "THE OVERSEER SUMMONS REINFORCEMENTS!" : "THE OVERSEER IS ENRAGED!", C_MAGENTA);
        r->invis_t = 1.0f;
    }
    if (!r->aware) {
        if (dist < 820 && los(r->pos, p->pos) && !p->dead) {
            r->aware = true;
        } else return;
    }
    if (!r->announced) {
        r->announced = true;
        music_play(SONG_BOSS);
        snd_play(SND_BOSS, 1.0f, 1.0f);
        hud_msg("THE OVERSEER HAS DETECTED YOU!", C_MAGENTA);
        shake_add(0.6f);
    }
    /* teleport when hurt */
    if (r->invis_t > 0) {
        r->invis_t -= dt;
        if (r->invis_t <= 0.5f && r->invis_t + dt > 0.5f) {
            boss_teleport(r);
            snd_play_at(SND_TELEPORT, r->pos, 0.8f, 0.7f);
        }
        return;
    }
    if (r->dmg_accum > r->maxhp * 0.11f) {
        r->dmg_accum = 0;
        r->invis_t = 1.1f;
        snd_play_at(SND_TELEPORT, r->pos, 0.8f, 1.0f);
        return;
    }
    bool sees = !p->dead && player_visible_to(r->pos, dist) && in_view(r->pos, 40);
    /* movement: hover at range, strafe */
    r->strafe_t -= dt;
    if (r->strafe_t <= 0) { r->strafe_t = 2 + frand() * 2; r->strafe_dir = -r->strafe_dir; }
    V2 side = v2scale(v2perp(dir), r->strafe_dir);
    V2 desired = side;
    if (dist > 420) desired = v2add(dir, v2scale(side, 0.4f));
    else if (dist < 260) desired = v2add(v2scale(dir, -1), v2scale(side, 0.5f));
    desired = wall_avoid(r, v2norm(desired));
    float speed = RDEF[RB_BOSS].speed * (1 + phase * 0.25f);
    r->vel = v2add(r->vel, v2clamplen(v2sub(v2scale(desired, speed), r->vel), 260 * dt));
    r->pos = v2mad(r->pos, r->vel, dt);
    circle_collide(&r->pos, r->radius, &r->vel, 0.2f);
    r->ang = approach_angle(r->ang, v2ang(to), 2.0f * dt);
    if (!sees) return;
    float fm = DIFF_FIRE[G.difficulty];
    float ps = DIFF_PSPD[G.difficulty];
    /* fan of orbs */
    r->attack_cd -= dt * fm;
    if (r->attack_cd <= 0) {
        int n = 5 + phase * 2;
        float spread = 0.9f + phase * 0.2f;
        float base = v2ang(to);
        for (int i = 0; i < n; i++) {
            float a = base - spread * 0.5f + spread * i / (n - 1);
            spawn_proj(EP_ORB, safe_muzzle(r->pos, v2mad(r->pos, v2fromang(a), r->radius)), v2scale(v2fromang(a), 290 * ps), 10, 4);
        }
        snd_play_at(SND_EN_SHOT, r->pos, 0.8f, 0.6f);
        r->attack_cd = 1.7f - phase * 0.35f;
    }
    /* homing missiles */
    r->attack2_cd -= dt * fm;
    if (r->attack2_cd <= 0) {
        for (int k = -1; k <= 1; k += 2) {
            V2 v = v2scale(v2rot(dir, k * 1.2f), 280 * ps);
            Proj *pr = spawn_proj(EP_HOMING, safe_muzzle(r->pos, v2mad(r->pos, v2rot(dir, k * 1.2f), r->radius + 6)), v, 20, 5.5f);
            if (pr) pr->turn = 2.0f + phase * 0.3f;
        }
        snd_play_at(SND_EN_MISSILE, r->pos, 0.8f, 0.7f);
        r->attack2_cd = 4.6f - phase * 0.9f;
    }
    /* spiral burst */
    if (phase >= 1) {
        r->spiral_cd -= dt * fm;
        if (r->spiral_cd <= 0 && r->spiral_t <= 0) { r->spiral_t = 2.2f; r->spiral_cd = 8.5f - phase; }
        if (r->spiral_t > 0) {
            r->spiral_t -= dt;
            r->spiral_ang += dt * 5.5f;
            static float acc = 0;
            acc += dt;
            while (acc > 0.07f) {
                acc -= 0.07f;
                for (int k = 0; k < 3; k++) {
                    float a = r->spiral_ang + k * TAU / 3;
                    spawn_proj(EP_ORB, safe_muzzle(r->pos, v2mad(r->pos, v2fromang(a), r->radius)), v2scale(v2fromang(a), 230 * ps), 8, 4);
                }
            }
        }
        /* summon */
        r->spawn_cd -= dt;
        if (r->spawn_cd <= 0) {
            r->spawn_cd = 10.0f - phase * 2;
            int alive = 0;
            for (int i = 0; i < MAX_ROBOTS; i++)
                if (W.rob[i].active && W.rob[i].from_matcen) alive++;
            if (alive < 6) {
                const LevelDef *ld = cur_def();
                for (int k = 0; k < 2; k++) {
                    V2 at = v2mad(r->pos, v2fromang(frand() * TAU), r->radius + 40);
                    if (point_in_rock(at)) continue;
                    Robot *s = robot_spawn(ld->matcen_types[irand(ld->nmatcen)], at, true);
                    if (s) {
                        s->aware = true;
                        s->last_seen = p->pos;
                        G.robots_total++;
                        fx_ring(at, 60, 5, rgba(1, 0.4f, 0.8f, 1), 0.4f, 5);
                    }
                }
                snd_play_at(SND_MATCEN, r->pos, 0.8f, 0.8f);
            }
        }
    }
    /* contact damage */
    if (!p->dead && dist < r->radius + p->radius && r->melee_cd <= 0) {
        player_damage(RDEF[RB_BOSS].contact, dir);
        p->vel = v2mad(p->vel, dir, 400);
        r->melee_cd = 0.8f;
    }
}

static void draw_boss(Robot *r) {
    float t = r->anim;
    Col c = RDEF[RB_BOSS].col;
    if (r->phase == 2) c = col_lerp(c, rgba(1, 0.5f, 0.1f, 1), 0.4f + 0.3f * sinf(t * 10));
    float alpha = 1;
    if (r->invis_t > 0) alpha = clampf(fabsf(r->invis_t - 0.5f) * 2, 0, 1) * (0.5f + 0.5f * sinf(t * 40));
    if (r->hit_flash > 0) c = col_white(c, 0.7f);
    c = col_a(c, alpha);
    float R = r->radius;
    rw_glow(r->pos, R * 3.0f, col_a(c, 0.18f));
    /* armour ring: 6 plates with gaps */
    for (int k = 0; k < 6; k++) {
        float a0 = t * 0.6f + k * TAU / 6;
        V2 arc[6];
        for (int j = 0; j < 6; j++) arc[j] = v2add(r->pos, v2scale(v2fromang(a0 + j * 0.16f), R));
        rw_polyline(arc, 6, false, 7, c);
        V2 in0 = v2add(r->pos, v2scale(v2fromang(a0), R * 0.8f));
        V2 in1 = v2add(r->pos, v2scale(v2fromang(a0 + 0.8f), R * 0.8f));
        rw_line(arc[0], in0, 4, col_a(c, 0.7f));
        rw_line(arc[5], in1, 4, col_a(c, 0.7f));
    }
    /* counter-rotating star */
    Stroke st;
    star(&st, 6, R * 0.72f, R * 0.42f, -t * 1.1f);
    V2 sp[12];
    for (int i = 0; i < st.n; i++) sp[i] = v2add(r->pos, st.p[i]);
    rw_polyline(sp, st.n, true, 5, col_a(col_white(c, 0.2f), alpha));
    /* eye tracking the player */
    V2 look = v2norm(v2sub(W.pl.pos, r->pos));
    rw_circle(r->pos, R * 0.3f, 5, col_a(C_WHITE, 0.9f * alpha), 24);
    V2 pupil = v2mad(r->pos, look, R * 0.12f);
    rw_glow(pupil, R * 0.35f, col_a(c, 0.9f));
    rw_glow(pupil, R * 0.12f, col_a(C_WHITE, alpha));
}

/* ------------------------------------------------------------ update/draw */
void robots_update(float dt) {
    W.screech_cd -= dt;
    for (int i = 0; i < MAX_ROBOTS; i++)
        if (W.rob[i].active) robot_update(&W.rob[i], dt);
    /* separation */
    for (int i = 0; i < MAX_ROBOTS; i++) {
        Robot *a = &W.rob[i];
        if (!a->active) continue;
        for (int j = i + 1; j < MAX_ROBOTS; j++) {
            Robot *b = &W.rob[j];
            if (!b->active) continue;
            V2 d = v2sub(b->pos, a->pos);
            float rr = a->radius + b->radius + 2;
            float d2 = v2len2(d);
            if (d2 >= rr * rr || d2 < 1e-4f) continue;
            float dl = sqrtf(d2);
            V2 n = v2scale(d, 1.0f / dl);
            float pen = rr - dl;
            float ma = RDEF[a->type].mass, mb = RDEF[b->type].mass;
            float ka = mb / (ma + mb), kb = ma / (ma + mb);
            if (RDEF[a->type].speed <= 0) { ka = 0; kb = 1; }
            if (RDEF[b->type].speed <= 0) { kb = 0; ka = 1; }
            a->pos = v2mad(a->pos, n, -pen * ka);
            b->pos = v2mad(b->pos, n, pen * kb);
        }
    }
    /* player collisions (bumping) */
    Player *p = &W.pl;
    if (p->dead || G.escaping) return;
    for (int i = 0; i < MAX_ROBOTS; i++) {
        Robot *r = &W.rob[i];
        if (!r->active) continue;
        V2 d = v2sub(p->pos, r->pos);
        float rr = r->radius + p->radius;
        float d2 = v2len2(d);
        if (d2 >= rr * rr || d2 < 1e-4f) continue;
        float dl = sqrtf(d2);
        V2 n = v2scale(d, 1.0f / dl);
        float pen = rr - dl;
        bool fixed = RDEF[r->type].speed <= 0 || r->type == RB_BOSS;
        p->pos = v2mad(p->pos, n, fixed ? pen : pen * 0.6f);
        if (!fixed) r->pos = v2mad(r->pos, n, -pen * 0.4f);
        float rel = v2dot(v2sub(p->vel, r->vel), n);
        if (rel < 0) {
            p->vel = v2mad(p->vel, n, -rel * 1.2f);
            if (-rel > 160 && p->bump_cd <= 0) {
                p->bump_cd = 0.4f;
                player_damage(2, n);
                robot_damage(r, 4, v2scale(n, -1), true);
            }
        }
    }
}

void robots_draw(void) {
    for (int i = 0; i < MAX_ROBOTS; i++) {
        Robot *r = &W.rob[i];
        if (!r->active || !rw_visible(r->pos, r->radius * 3)) continue;
        if (r->type == RB_BOSS) { draw_boss(r); continue; }
        const RobotDef *d = &RDEF[r->type];
        Col c = d->col;
        if (!r->aware) c = col_mul(c, 0.8f);
        if (r->hit_flash > 0) c = col_white(c, 0.8f);
        float alpha = r->cloak_vis;
        if (d->cloaked && alpha < 0.3f) alpha *= 0.6f + 0.4f * sinf(r->anim * 30);
        c = col_a(c, alpha);
        float ang = r->ang;
        switch (r->type) {
        case RB_SPIDER:
        case RB_BABY: ang = r->anim * (r->type == RB_BABY ? 4 : 1.5f); break;
        case RB_TURRET: ang = 0; break;
        default: break;
        }
        rw_glow(r->pos, r->radius * 2.4f, col_a(c, 0.12f));
        rw_shape(&RSHAPE[r->type], r->pos, ang, r->radius, 5, c);
        if (r->type == RB_TURRET) {
            V2 f = v2fromang(r->ang);
            rw_line(v2mad(r->pos, f, r->radius * 0.5f), v2mad(r->pos, f, r->radius * 1.3f), 6, c);
        }
        if (r->type == RB_LIFTER || r->type == RB_CLOAKER) {
            /* snapping claws */
            float open = 0.5f + 0.5f * sinf(r->anim * (r->aware ? 14 : 4));
            V2 f = v2fromang(r->ang), s = v2perp(f);
            V2 tip = v2mad(r->pos, f, r->radius * 1.15f);
            rw_line(tip, v2add(tip, v2add(v2scale(f, 5), v2scale(s, 4 + open * 5))), 3, col_a(c, 0.8f));
            rw_line(tip, v2add(tip, v2add(v2scale(f, 5), v2scale(s, -4 - open * 5))), 3, col_a(c, 0.8f));
        }
        if (r->muzzle > 0) {
            V2 m = v2mad(r->pos, v2fromang(r->ang), r->radius + 4);
            rw_glow(m, 5 + r->muzzle * 13, col_a(col_white(d->col, 0.5f), r->muzzle * 0.9f * alpha));
        }
        if (r->type == RB_SUPERHULK || r->type == RB_HULK) {
            float hp = r->hp / r->maxhp;
            if (hp < 0.99f) {
                V2 a = v2add(r->pos, v2(-r->radius, r->radius + 8)), b = v2add(a, v2(r->radius * 2 * hp, 0));
                rw_line(a, b, 3, col_a(c, 0.7f));
            }
        }
    }
}

void robot_draw_preview(int type, V2 vpos, float ang, float scale, float t) {
    const RobotDef *d = &RDEF[type];
    if (type == RB_BOSS) {
        Col c = d->col;
        for (int k = 0; k < 6; k++) {
            float a0 = t * 0.6f + k * TAU / 6;
            V2 arc[6];
            for (int j = 0; j < 6; j++) arc[j] = v2add(vpos, v2scale(v2fromang(a0 + j * 0.16f), scale));
            r_polyline(arc, 6, false, 5, c);
        }
        Stroke st;
        star(&st, 6, scale * 0.72f, scale * 0.42f, -t * 1.1f);
        V2 sp[12];
        for (int i = 0; i < st.n; i++) sp[i] = v2add(vpos, st.p[i]);
        r_polyline(sp, st.n, true, 4, col_white(c, 0.2f));
        r_circle(vpos, scale * 0.3f, 4, C_WHITE, 20);
        r_glow(vpos, scale * 0.4f, col_a(c, 0.8f));
        return;
    }
    float a = ang;
    if (type == RB_SPIDER || type == RB_BABY) a = t * 1.5f;
    if (type == RB_TURRET) a = 0;
    r_glow(vpos, scale * 2.2f, col_a(d->col, 0.15f));
    r_shape(&RSHAPE[type], vpos, a, scale, 4, d->col);
    if (type == RB_TURRET) {
        V2 f = v2fromang(ang);
        r_line(v2mad(vpos, f, scale * 0.5f), v2mad(vpos, f, scale * 1.3f), 5, d->col);
    }
}

void game_draw_robot_preview(int type, V2 pos, float ang, float scale) { robot_draw_preview(type, pos, ang, scale, g_time); }

/* ------------------------------------------------------------ reactor */
void reactor_damage(float dmg) {
    Reactor *rc = &W.reactor;
    if (!rc->exists || rc->dead) return;
    rc->hp -= dmg;
    rc->hit_flash = 0.1f;
    rc->provoked = true;
    snd_play_at(SND_REACTOR_HIT, rc->pos, 0.5f, frandr(0.9f, 1.1f));
    if (rc->hp <= 0) {
        rc->dead = true;
        rc->boom_t = 1.8f;
        fx_explosion(rc->pos, 120, rgba(1, 0.9f, 0.6f, 1));
        snd_play(SND_EXPL_L, 1.0f, 0.8f);
        start_self_destruct(rc->pos);
    }
}

void reactor_update(float dt) {
    Reactor *rc = &W.reactor;
    if (!rc->exists) return;
    rc->t += dt;
    rc->hit_flash = maxf(0, rc->hit_flash - dt);
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
    bool active = (rc->provoked && dist < 800) || dist < 430;
    if (!active || p->dead || !in_view(rc->pos, 30) || !player_visible_to(rc->pos, dist)) return;
    rc->fire_cd -= dt * DIFF_FIRE[G.difficulty];
    if (rc->fire_cd <= 0) {
        rc->fire_cd = 1.5f * (0.45f + 0.55f * hpf);
        float sp = 430 * DIFF_PSPD[G.difficulty];
        V2 target = v2mad(p->pos, p->vel, dist / sp * DIFF_LEAD[G.difficulty]);
        float base = v2ang(v2sub(target, rc->pos));
        int n = 1 + G.level;
        for (int i = 0; i < n; i++) {
            float a = base + (i - (n - 1) * 0.5f) * 0.18f + frandr(-1, 1) * DIFF_SPREAD[G.difficulty];
            spawn_proj(EP_REACTOR, safe_muzzle(rc->pos, v2mad(rc->pos, v2fromang(a), 46)), v2scale(v2fromang(a), sp), 8, 2.5f);
        }
        snd_play_at(SND_EN_SHOT, rc->pos, 0.6f, 0.75f);
    }
}

void reactor_draw(void) {
    Reactor *rc = &W.reactor;
    if (!rc->exists || !rw_visible(rc->pos, 120)) return;
    float t = rc->t;
    Col acc = cur_def()->accent;
    if (rc->dead) {
        /* smouldering wreck */
        V2 ring[8];
        for (int i = 0; i < 8; i++) ring[i] = v2add(rc->pos, v2scale(v2fromang(i * TAU / 8 + 0.2f), 40 + (i & 1) * 6));
        rw_polyline(ring, 8, true, 3, col_a(C_GREY, 0.4f));
        rw_glow(rc->pos, 60, col_a(C_ORANGE, 0.15f + 0.1f * sinf(t * 9)));
        return;
    }
    float hpf = rc->hp / rc->maxhp;
    Col c = acc;
    if (rc->hit_flash > 0) c = col_white(c, 0.8f);
    float flick = hpf < 0.35f ? (0.7f + 0.3f * sinf(t * 37)) : 1.0f;
    c = col_a(c, flick);
    rw_glow(rc->pos, 130, col_a(c, 0.14f));
    /* outer rotating octagon */
    V2 oct[8];
    for (int i = 0; i < 8; i++) oct[i] = v2add(rc->pos, v2scale(v2fromang(t * 0.4f + i * TAU / 8), 46));
    rw_polyline(oct, 8, true, 7, c);
    /* middle counter-rotating square */
    V2 sq[4];
    for (int i = 0; i < 4; i++) sq[i] = v2add(rc->pos, v2scale(v2fromang(-t * 0.9f + i * TAU / 4), 30));
    rw_polyline(sq, 4, true, 5, col_white(c, 0.2f));
    /* conduits */
    for (int i = 0; i < 4; i++) {
        V2 a = v2add(rc->pos, v2scale(v2fromang(t * 0.4f + i * TAU / 4 + TAU / 16), 46));
        V2 b = v2add(rc->pos, v2scale(v2fromang(t * 0.4f + i * TAU / 4 + TAU / 16), 64));
        rw_line(a, b, 4, col_a(c, 0.7f));
    }
    /* pulsing core */
    float pulse = 0.6f + 0.4f * sinf(t * (4 + (1 - hpf) * 10));
    Col core = col_lerp(rgba(1, 1, 0.8f, 1), rgba(1, 0.3f, 0.2f, 1), 1 - hpf);
    rw_glow(rc->pos, 34 * pulse + 10, col_a(core, 0.8f));
    rw_glow(rc->pos, 12, col_a(C_WHITE, 0.9f));
    rw_circle(rc->pos, 14 + pulse * 4, 3, col_a(core, 0.8f), 20);
}

/* ------------------------------------------------------------ matcens */
void matcens_update(float dt) {
    Player *p = &W.pl;
    const LevelDef *ld = cur_def();
    for (int i = 0; i < W.nmat; i++) {
        Matcen *m = &W.mat[i];
        m->glow = maxf(0, m->glow - dt);
        if (!m->triggered) {
            if (!p->dead && v2dist(m->pos, p->pos) < 560 && los(m->pos, p->pos)) {
                m->triggered = true;
                m->timer = 1.5f;
                hud_msg("ROBOT GENERATOR ACTIVATED", rgba(1, 0.4f, 0.85f, 1));
            }
            continue;
        }
        if (m->spawned >= m->max) continue;
        if (p->dead) continue;
        m->timer -= dt * DIFF_FIRE[G.difficulty];
        if (m->timer <= 0) {
            bool blocked = false;
            for (int k = 0; k < MAX_ROBOTS; k++)
                if (W.rob[k].active && v2dist(W.rob[k].pos, m->pos) < 34) blocked = true;
            if (blocked) { m->timer = 1.0f; continue; }
            int type = ld->matcen_types[irand(ld->nmatcen)];
            Robot *r = robot_spawn(type, m->pos, true);
            if (r) {
                r->aware = true;
                r->last_seen = p->pos;
                G.robots_total++;
                m->spawned++;
                m->glow = 1.0f;
                fx_ring(m->pos, 70, 4, rgba(1, 0.4f, 0.8f, 1), 0.5f, 5);
                for (int k = 0; k < 18; k++) {
                    V2 from = v2add(m->pos, v2scale(v2fromang(frand() * TAU), 60 + frand() * 30));
                    fx_spark(from, v2scale(v2sub(m->pos, from), 3.5f), rgba(1, 0.5f, 0.9f, 1), 0.3f, 3);
                }
                grid_impulse(m->pos, 160, -220);
                snd_play_at(SND_MATCEN, m->pos, 0.7f, 1.0f);
            }
            m->timer = frandr(5.0f, 8.0f);
        }
    }
}

void matcens_draw(void) {
    float t = W.time;
    for (int i = 0; i < W.nmat; i++) {
        Matcen *m = &W.mat[i];
        if (!rw_visible(m->pos, 60)) continue;
        bool live = m->spawned < m->max;
        Col c = live ? rgba(1, 0.35f, 0.8f, 1) : rgba(0.4f, 0.3f, 0.4f, 1);
        float h = TILE * 0.46f;
        V2 corners[4] = {v2(-h, -h), v2(h, -h), v2(h, h), v2(-h, h)};
        for (int k = 0; k < 4; k++) {
            V2 cp = v2add(m->pos, corners[k]);
            V2 dx = v2(corners[k].x > 0 ? -10 : 10, 0), dy = v2(0, corners[k].y > 0 ? -10 : 10);
            rw_line(v2add(cp, dx), cp, 4, c);
            rw_line(cp, v2add(cp, dy), 4, c);
        }
        float spin = t * (m->triggered && live ? 2.5f : 0.5f);
        V2 dm[4];
        for (int k = 0; k < 4; k++) dm[k] = v2add(m->pos, v2scale(v2fromang(spin + k * TAU / 4), 13));
        rw_polyline(dm, 4, true, 3.5f, col_a(c, 0.8f));
        rw_glow(m->pos, 40 + m->glow * 40, col_a(c, 0.12f + m->glow * 0.4f));
    }
}
