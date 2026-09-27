/*
 * Internal gameplay types shared by game.c, robots.c and hud.c.
 */
#pragma once
#include "common.h"

enum {
    PR_LASER, PR_VULCAN, PR_SPREAD, PR_PLASMA, PR_FUSION, PR_CONCUSSION, PR_HOMING, PR_PROX, PR_SMART, PR_BLOB, PR_MEGA,
    EP_PULSE, EP_BOLT, EP_NEEDLE, EP_VULCAN, EP_MISSILE, EP_HOMING, EP_MINE, EP_ORB, EP_REACTOR,
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
} Proj;
#define MAX_PROJ 1400

enum { AI_RUSHER, AI_MELEE, AI_TURRET, AI_KEEPER, AI_EVADER, AI_AMBUSH, AI_BOSS };
enum { RW_NONE, RW_PULSE, RW_BOLT, RW_NEEDLE, RW_VULCAN, RW_MISSILE, RW_HOMING, RW_MINE, RW_BABYLASER };

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
    /* boss */
    int phase;
    float dmg_accum, invis_t, tele_cd, spawn_cd, attack_cd, attack2_cd, spiral_t, spiral_ang, spiral_cd;
    bool announced;
} Robot;
#define MAX_ROBOTS 220

enum {
    PU_SHIELD, PU_ENERGY, PU_LASER, PU_QUAD, PU_VULCAN, PU_VAMMO, PU_SPREAD, PU_PLASMA, PU_FUSION,
    PU_CONC, PU_HOMING, PU_PROX, PU_SMART, PU_MEGA, PU_CLOAK, PU_INVULN, PU_LIFE,
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

typedef struct {
    bool exists, dead;
    V2 pos;
    float hp, maxhp;
    float fire_cd;
    float hit_flash;
    float t;
    float boom_t;
    int booms;
    bool provoked;
} Reactor;

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
    int primary;
    uint8_t owned;
    int laser_level;
    bool quad;
    int vulcan_ammo;
    int secondary;
    int missiles[SW_COUNT];
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
} World;

extern World W;
extern const RobotDef RDEF[RB_COUNT];
extern Shape RSHAPE[RB_COUNT];
extern Shape SHIP_SHAPE;

extern const float DIFF_DMG[5], DIFF_FIRE[5], DIFF_PSPD[5], DIFF_LEAD[5], DIFF_SPREAD[5], DIFF_HP[5];

const LevelDef *cur_def(void);

/* game.c */
Proj *spawn_proj(int type, V2 pos, V2 vel, float dmg, float life);
void explode(V2 pos, float radius, float dmg, bool by_player, int exclude_robot, Col c);
void player_damage(float dmg, V2 dir);
Powerup *spawn_powerup(int type, V2 pos, V2 vel, int amount);
void start_self_destruct(V2 at);
void chain_kill(V2 pos, int points);
bool player_visible_to(V2 from, float dist);
Col key_color(int lock);

/* robots.c */
void robots_init_shapes(void);
Robot *robot_spawn(int type, V2 pos, bool from_matcen);
void robots_update(float dt);
void robots_draw(void);
void robot_damage(Robot *r, float dmg, V2 dir, bool by_player);
void reactor_update(float dt);
void reactor_draw(void);
void reactor_damage(float dmg);
void matcens_update(float dt);
void matcens_draw(void);
void robot_draw_preview(int type, V2 vpos, float ang, float scale, float t);

bool in_view(V2 p, float margin);

/* hud.c */
void hud_hint(const char *text);
void draw_powerup_icon(int type, V2 vpos, float scale, float t, float alpha);
const char *powerup_name(int type);
Col powerup_color(int type);
