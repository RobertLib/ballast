/*
 * Internal gameplay types shared by game.c, robots.c and hud.c.
 */
#pragma once
#include "common.h"

enum {
    PR_LASER, PR_VULCAN, PR_SPREAD, PR_PLASMA, PR_FUSION, PR_CONCUSSION, PR_HOMING, PR_PROX, PR_SMART, PR_BLOB, PR_MEGA, PR_CARGO,
    EP_PULSE, EP_BOLT, EP_NEEDLE, EP_VULCAN, EP_MISSILE, EP_HOMING, EP_MINE, EP_ORB, EP_REACTOR,
    EP_PELLET, EP_SHARD, EP_FLAK, EP_SEEKER,
    PR_TYPES
};
#define IS_ENEMY_PROJ(t) ((t) >= EP_PULSE)

typedef struct {
    bool active;
    int type;
    V2 pos, vel;
    float life, age;
    float dmg, radius, splash;
    float turn;
    int target;       /* robot index, -2 reactor, -1 none */
    float retarget;
    Col col;
    int hits[8];
    int nhits;
    float hp;         /* >0: can be shot down */
    bool armed;
    float trail;
    float charge;
    int src;          /* who fired it: a robot type or a DS_ source, for the death recap */
    int bounces;      /* ricochets left */
    int cargo;        /* salvage packed into a jettisoned cargo bomb */
    /* bullet patterns */
    float curve;      /* the velocity turns this fast (rad/s), fading out */
    float accel, vmin, vmax; /* speeds up or slows down between the two */
    float wait;       /* hangs still this long, then flies off at `launch` */
    V2 launch;
    int burst;        /* flak and seekers: pellets released when they pop */
    bool grazed;      /* it brushed past the ship once */
} Proj;
#define MAX_PROJ 4000

/* what hurt the ship: robot types first, then these */
/* DS_SAFE: a friendly blast that never hurts the ship (detonator rounds) */
enum { DS_REACTOR = RB_COUNT, DS_SELF, DS_OVERCHARGE, DS_BLAST, DS_TRAP, DS_UNKNOWN, DS_SAFE };

enum { AI_RUSHER, AI_MELEE, AI_TURRET, AI_KEEPER, AI_EVADER, AI_AMBUSH, AI_STRAFER, AI_SNIPER, AI_KAMIKAZE, AI_BOSS };
enum {
    RW_NONE, RW_PULSE, RW_BOLT, RW_NEEDLE, RW_VULCAN, RW_MISSILE, RW_HOMING, RW_MINE, RW_BABYLASER,
    RW_SHOTGUN, RW_LANCE, RW_FLAK, RW_BARRAGE, RW_SPAWN
};

/* bullet patterns, shared by the reactor, the Overseer and the pattern robots */
enum { BP_SPIRAL, BP_RINGS, BP_FANS, BP_FLOWER, BP_LASERS, BP_SEEKERS, BP_NOVA, BP_WALLS, BP_COUNT };
#define BAR_BEAMS 6
typedef struct {
    int pat;            /* -1: resting */
    float t, dur;
    float cd, cd2;
    float ang, ang2, spin;
    int step;
    float dens, speed, dmg; /* bullets per volley, bullet speed and damage, 1 = the baseline */
    Col col, col2;
    int src;            /* who fired, for the death recap */
    /* rotating laser beams */
    int nbeams;
    float beam_ang, beam_spin, beam_warn, beam_w;
    float beam_len[BAR_BEAMS];
    bool beams_live;
} Barrage;

typedef struct {
    const char *name;
    float radius, hp, speed, accel, turn;
    int ai, weapon;
    float fire_delay;
    int burst;
    float burst_gap;
    float range, sight;
    int points;
    Col col;
    float contact;
    float drop;
    bool cloaked;
    float mass;
} RobotDef;

typedef struct {
    bool active;
    int type;
    V2 pos, vel;
    float ang;
    float radius;
    float hp, maxhp;
    bool aware, sees;
    V2 last_seen;
    float lost_t;
    float think_t;
    float fire_cd;
    int burst_left;
    float burst_cd;
    float hit_flash;
    float strafe_dir, strafe_t;
    float melee_cd;
    float anim;
    float cloak_vis;
    V2 home;
    bool from_matcen;
    float muzzle;
    float stuck_t;
    V2 last_pos;
    float ram_next;    /* W.time when the ram prow can hit this robot again */
    bool elite;        /* an empowered robot: tougher, faster to fire, pops in a burst of pellets */
    float charge;      /* lancers: the beam charging up; wasps: the dash */
    float aim;         /* lancers: where the beam points */
    float beam_t;      /* lancers: the shot beam fading */
    V2 beam_end;
    float spawn_t;     /* carriers: the next brood; pylons: rising out of the floor */
    Barrage bar;       /* pattern robots and the Overseer */
    Barrage bar2;
    /* boss */
    int phase;
    float dmg_accum, invis_t, tele_cd, spawn_cd, attack_cd, attack2_cd, spiral_cd;
    bool announced;
} Robot;
#define MAX_ROBOTS 220

/* the secondaries PU_CONC..PU_MEGA follow SW_ order, keys stay last */
enum {
    PU_SHIELD, PU_ENERGY, PU_LASER, PU_QUAD, PU_VULCAN, PU_SPREAD, PU_PLASMA, PU_FUSION,
    PU_CONC, PU_HOMING, PU_PROX, PU_SMART, PU_MEGA, PU_CLOAK, PU_INVULN, PU_LIFE, PU_SALVAGE, PU_TREASURE, PU_CRATE,
    PU_KEY_BLUE, PU_KEY_YELLOW, PU_KEY_RED, PU_TYPES
};

typedef struct {
    bool active;
    int type;
    int amount;
    V2 pos, vel;
    float t;
    float nopick;
    float msg_cd;
} Powerup;
#define MAX_POWERUPS 320

typedef struct { bool active; V2 pos; float t; } Hostage;
#define MAX_HOSTAGES 16

#define REACTOR_R 48.0f
#define REACTOR_PHASES 4
typedef struct {
    bool exists, dead;
    V2 pos;
    float hp, maxhp;
    float hit_flash;
    float t;
    float boom_t;
    bool provoked;
    /* the fight: phases of bullet patterns, broken up by shield pylons and core vents */
    bool awake;
    int phase, nphases;
    float threshold[REACTOR_PHASES]; /* hp where each phase ends */
    float vent_t;       /* between phases: invulnerable while the core vents */
    float rest_t;       /* between patterns */
    int last_pat;
    int pylons;         /* alive: the core is shielded */
    float shield_vis, shield_hit;
    float lost_t;       /* the ship out of reach */
    float wake_t;
    Barrage bar[2];
} Reactor;

/* traps built into the mine */
enum { TR_VENT, TR_GATE, TR_SWEEPER, TR_WELL };
typedef struct {
    int type;
    V2 pos, a, b;       /* gates: the ends of the beam */
    float t, period, offset;
    float ang, spin, len;
    int arms;
    float heat;         /* vents: the glow of the coming eruption */
    bool on;
} Trap;
#define MAX_TRAPS 96

typedef struct {
    V2 pos;
    bool triggered;
    float timer;
    int spawned, max;
    float glow;
} Matcen;
#define MAX_MATCENS 8

typedef struct {
    V2 pos, vel;
    float ang;
    float radius;
    float shield, energy, burner;
    int primary;      /* PW_LASER or the special weapon */
    int special;      /* the second primary slot: PW_VULCAN..PW_FUSION, -1 empty */
    int laser_level;
    bool quad;
    int secondary;    /* the one secondary slot: SW_*, -1 empty */
    int missiles;
    int cargo;        /* salvage in the hold: it adds mass and is banked at the exit */
    float mass;
    float jettison_cd;
    int swap_pu;      /* a weapon under the ship that the swap key would take, -1 none */
    int keys;
    float cloak_t, invuln_t, spawn_inv;
    float fire_cd, sec_cd;
    int fire_side, sec_side;
    float fusion_charge;
    bool charging;
    float over_t;
    bool dead;
    float dead_t;
    float hit_flash;
    float thrust;
    bool burning;
    int charge_voice, burn_voice;
    float energy_snd_t;
    float warn_cd;
    V2 start;
    float muzzle_flash;
    float bump_cd;
    float scrape_cd;
    float trail_t;
    V2 hit_dir;
    float hit_dir_t;
    /* upgrade modules */
    float calm_t;      /* time since the last hit, for nanite repair */
    float reactive_cd;
    float phase_t, phase_cd;
    bool was_burning;
    float drone_ang, drone_cd;
    float salvage_snd_t;
    int fab_kills;     /* toward the next fabricated missile */
} Player;

typedef struct {
    Player pl;
    Proj proj[MAX_PROJ];
    int proj_next;
    Robot rob[MAX_ROBOTS];
    Powerup pu[MAX_POWERUPS];
    Hostage host[MAX_HOSTAGES];
    int nhost;
    Reactor reactor;
    Matcen mat[MAX_MATCENS];
    int nmat;
    int boss_idx;
    float boss_death_t;
    V2 boss_death_pos;
    float flow_t, explore_t;
    float alarm_t, quake_t;
    int beep_sec;
    float screech_cd;
    float flash;
    Col flash_col;
    V2 exit_pos;
    float zoom_target;
    int level_keys;   /* keys that exist in this level */
    float time;       /* level time incl. pauses excluded */
    float time_scale; /* slow motion on big events */
    float tut_t;
    int tut_step;
    bool low_energy_hint;
    float secret_msg_next;
    bool phoenix_used;
    int last_src, last_proj; /* the last thing that hit the ship: DS_ or robot type, projectile type or -1 */
    int blast_kills;         /* robots killed by the cargo bomb currently exploding, -1 when none is */
    bool ramming;            /* robot_damage comes from the ram prow */
    bool cargo_hint, swap_hint, vault_msg;
    int nvaults;
    Trap trap[MAX_TRAPS];
    int ntraps;
    float hazard_acc, hazard_cd; /* beams and vents hurt in ticks */
    V2 hazard_dir;
    int hazard_src;
    int grazes;
    float graze_snd_t;
    bool arena_fight;        /* the reactor or the Overseer is fighting: the camera pulls back */
    uint32_t seed;           /* the sector's seed: which robots are elite */
} World;

extern World W;
extern const RobotDef RDEF[RB_COUNT];
extern Shape RSHAPE[RB_COUNT];
extern Shape SHIP_SHAPE;

extern const float DIFF_DMG[5], DIFF_FIRE[5], DIFF_PSPD[5], DIFF_LEAD[5], DIFF_SPREAD[5], DIFF_HP[5];

const LevelDef *cur_def(void);

/* game.c */
extern int g_proj_src; /* stamped on projectiles as their source */
Proj *spawn_proj(int type, V2 pos, V2 vel, float dmg, float life);
void explode(V2 pos, float radius, float dmg, bool by_player, int exclude_robot, Col c, int src);
void player_damage(float dmg, V2 dir, int src);
Powerup *spawn_powerup(int type, V2 pos, V2 vel, int amount);
void spawn_salvage(V2 pos, int amount, float speed);
void start_self_destruct(V2 at);
void chain_kill(V2 pos, int points);
float player_on_kill(Robot *r); /* on-kill modules, challenges and stats: returns the wreck's salvage multiplier */
bool player_visible_to(V2 from, float dist);
Col key_color(int lock);

void hazard_touch(float dps, float dt, V2 dir, int src); /* beams, vents: damage dealt in ticks */
bool player_hurt_by(V2 a, V2 b, float r);                  /* does a sweep from a to b of radius r touch the ship */

/* barrage.c */
Proj *bullet(int type, V2 pos, float ang, float speed, float dmg, Col c);
void bullet_ring(int type, V2 c, float r0, int n, float ang0, float speed, float dmg, Col col);
void bullet_fan(int type, V2 c, float r0, float ang, int n, float spread, float speed, float dmg, Col col);
float aim_lead(V2 from, float speed);   /* the angle to the ship, leading it by the skill level */
void barrage_start(Barrage *b, int pat, float dur);
void barrage_stop(Barrage *b);
void barrage_update(Barrage *b, V2 pos, float r, float dt);
void barrage_draw(const Barrage *b, V2 pos, float r);
V2 beam_end(V2 a, float ang, float maxlen);
void draw_beam(V2 a, V2 b, float w, Col c, float warn);
int bullets_cancel(V2 c, float radius);  /* clear enemy bullets: how many */
void flak_pop(Proj *pr);

/* reactor.c */
void reactor_init(V2 pos);
void reactor_update(float dt);
void reactor_draw(void);
void reactor_damage(float dmg);
bool reactor_shielded(void);

/* traps.c */
void trap_add(int type, int tx, int ty);
void traps_update(float dt);
void traps_draw(void);

/* robots.c */
void robots_init_shapes(void);
Robot *robot_spawn(int type, V2 pos, bool from_matcen);
void robots_update(float dt);
void robots_draw(void);
void robot_damage(Robot *r, float dmg, V2 dir, bool by_player);
void matcens_update(float dt);
void matcens_draw(void);
void robot_draw_preview(int type, V2 vpos, float ang, float scale, float t);

bool in_view(V2 p, float margin);

/* hud.c */
const char *objective_text(void); /* the HUD's current goal line */
void hud_hint(const char *text);
void draw_powerup_icon(int type, V2 vpos, float scale, float t, float alpha);
const char *powerup_name(int type);
void draw_salvage(const char *text, float x, float y, float size, Col c, int align);
Col powerup_color(int type);
