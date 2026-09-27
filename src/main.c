/*
 * BALLAST - entry point, application state machine, menus and screens.
 */
#include "game_internal.h"
#include <SDL3/SDL_main.h>

float g_time;
bool g_gamepad_connected;
bool g_fx_collide = true;

typedef enum {
    S_TITLE, S_SETUP, S_STORY, S_MAP, S_BRIEFING, S_PLAY, S_PAUSE, S_TALLY,
    S_GAMEOVER, S_VICTORY, S_SUMMARY, S_NAME, S_SCORES, S_OPTIONS, S_HELP, S_HANGAR, S_RECORD, S_INTRO
} AppState;

static AppState state = S_TITLE, pending_state;
static bool pending = false;
static float fade = 1.0f;
static float state_t = 0;
static AppState return_state = S_TITLE;
static bool running = true;
static SDL_Gamepad *pad = NULL;
static int menu_sel = 0;
static int help_page = 0;
static bool retry_level = false;
static int new_score_rank = -1;
static char name_buf[16];
static float stick_repeat = 0;
static bool run_won = false;
static int tally_unlock_from = 0;
static float idle_t = 0;          /* seconds without input: the title replays the intro */
static bool logo_landed = false;  /* the intro ended on the logo, so the title shows it at once */

/* debug / test options */
static int opt_frames = -1;
static const char *opt_shot = NULL;
static bool opt_reveal = false;
static bool opt_bot = false, opt_god = false, opt_destroy = false, opt_automap = false, opt_allweapons = false;
static int opt_salvage = 0, opt_cargo = 0;
static int opt_tp_x = -1, opt_tp_y = -1;
static bool opt_arena = false;
static const char *opt_spawn = NULL;
static bool opt_elite = false;
static const char *opt_mods = NULL;
static bool opt_autorun = false; /* test: click through a whole run, the bot flying every mine */
static const char *opt_prefdir = NULL; /* test: a sandbox for the save files, which then are written */
static int frame_count = 0;

typedef struct {
    bool up, down, left, right, confirm, back;
    bool click, alt;
    V2 mouse;
    bool mouse_moved;
    float wheel;
} UIInput;
static UIInput ui;
static bool ui_point = false;     /* the mouse is on something clickable: the pointer cursor */

/* ------------------------------------------------------------ persistence */
typedef struct { char name[16]; int score; int level; int diff; int heat; int ship; int daily; } Score;
static Score scores[10];
static int nscores = 0;
static char *pref_path = NULL;

static void path_join(char *out, size_t n, const char *file) { snprintf(out, n, "%s%s", pref_path ? pref_path : "", file); }

static void scores_load(void) {
    char p[1024];
    path_join(p, sizeof(p), "hiscores.txt");
    FILE *f = fopen(p, "r");
    nscores = 0;
    if (f) {
        char line[128];
        while (nscores < 10 && fgets(line, sizeof(line), f)) {
            Score s;
            memset(&s, 0, sizeof(s));
            if (sscanf(line, "%15s %d %d %d %d %d %d", s.name, &s.score, &s.level, &s.diff, &s.heat, &s.ship, &s.daily) < 4) break;
            for (char *c = s.name; *c; c++)
                if (*c == '_') *c = ' ';
            s.diff = clampi(s.diff, 0, 4);
            s.ship = clampi(s.ship, 0, SHIP_COUNT - 1);
            scores[nscores++] = s;
        }
        fclose(f);
    }
    if (nscores == 0) {
        static const char *names[5] = {"DRAVIS", "MATERIAL", "DEFENDER", "PYRO", "GUIDEBOT"};
        static const int sc[5] = {150000, 100000, 60000, 30000, 10000};
        for (int i = 0; i < 5; i++) {
            snprintf(scores[i].name, sizeof(scores[i].name), "%s", names[i]);
            scores[i].score = sc[i];
            scores[i].level = 6 - i;
            scores[i].diff = 2;
        }
        nscores = 5;
    }
}

static void scores_save(void) {
    char p[1024];
    path_join(p, sizeof(p), "hiscores.txt");
    FILE *f = fopen(p, "w");
    if (!f) return;
    for (int i = 0; i < nscores; i++) {
        char n[16];
        snprintf(n, sizeof(n), "%s", scores[i].name[0] ? scores[i].name : "PILOT");
        for (char *c = n; *c; c++)
            if (*c == ' ') *c = '_';
        fprintf(f, "%s %d %d %d %d %d %d\n", n, scores[i].score, scores[i].level, scores[i].diff, scores[i].heat, scores[i].ship, scores[i].daily);
    }
    fclose(f);
}

static int score_rank(int score) {
    for (int i = 0; i < 10; i++)
        if (i >= nscores || score > scores[i].score) return i;
    return -1;
}

static void score_insert(int rank, const char *name) {
    if (rank < 0 || rank >= 10) return;
    if (nscores < 10) nscores++;
    for (int i = nscores - 1; i > rank; i--) scores[i] = scores[i - 1];
    Score *s = &scores[rank];
    snprintf(s->name, sizeof(s->name), "%s", name[0] ? name : "PILOT");
    s->score = G.score;
    s->level = R.layer + 1;
    s->diff = R.base_diff;
    s->heat = run_heat();
    s->ship = R.ship;
    s->daily = R.daily;
    scores_save();
}

static void config_save(void) {
    char p[1024];
    path_join(p, sizeof(p), "config.txt");
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "music %d\nsfx %d\nfullscreen %d\nshake %d\nmovemode %d\nbloom %d\nautoswitch %d\n", (int)(g_cfg.music_vol * 10 + 0.5f),
            (int)(g_cfg.sfx_vol * 10 + 0.5f), g_cfg.fullscreen, g_cfg.shake, g_cfg.move_mode, g_cfg.bloom, g_cfg.autoswitch);
    fclose(f);
}

static void config_load(void) {
    char p[1024];
    path_join(p, sizeof(p), "config.txt");
    FILE *f = fopen(p, "r");
    if (!f) return;
    char key[32];
    int v;
    while (fscanf(f, "%31s %d", key, &v) == 2) {
        if (!strcmp(key, "music")) g_cfg.music_vol = clampi(v, 0, 10) / 10.0f;
        else if (!strcmp(key, "sfx")) g_cfg.sfx_vol = clampi(v, 0, 10) / 10.0f;
        else if (!strcmp(key, "fullscreen")) g_cfg.fullscreen = v != 0;
        else if (!strcmp(key, "shake")) g_cfg.shake = clampi(v, 0, 2);
        else if (!strcmp(key, "movemode")) g_cfg.move_mode = clampi(v, 0, 1);
        else if (!strcmp(key, "bloom")) g_cfg.bloom = v != 0;
        else if (!strcmp(key, "autoswitch")) g_cfg.autoswitch = v != 0;
    }
    fclose(f);
}

/* ------------------------------------------------------------ run save */
/*
 * The run is saved at checkpoints: in the hangar after every change, on the
 * sector chart, before a mine launches and when one has been escaped.
 * CONTINUE resumes at the checkpoint. Progress inside a mine is never saved:
 * aborting it or quitting the game rolls back to the briefing before it.
 */
typedef struct { GameState g; Player pl; Run run; bool retry; } Campaign;
static Campaign camp;             /* the last checkpoint, restored when a mine is aborted */
static bool has_campaign = false; /* a run is in progress: the title offers CONTINUE */
static bool campaign_io = true;   /* debug and test starts never touch the save files */

static void campaign_snapshot(void) {
    camp.g = G;
    camp.pl = W.pl;
    camp.run = R;
    camp.retry = retry_level;
    has_campaign = true;
}

static void campaign_save(void) {
    campaign_snapshot();
    if (!campaign_io) return;
    char p[1024];
    path_join(p, sizeof(p), "run.txt");
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "retry %d\n", retry_level);
    run_write(f);
    game_save(f);
    fclose(f);
}

static void campaign_load(void) {
    has_campaign = false;
    char p[1024];
    path_join(p, sizeof(p), "run.txt");
    FILE *f = fopen(p, "r");
    if (!f) return;
    memset(&R, 0, sizeof(R));
    game_new(2, 3);
    retry_level = false;
    bool ok = false;
    char key[32], val[64];
    while (fscanf(f, "%31s %63s", key, val) == 2) {
        if (!strcmp(key, "retry")) retry_level = atoi(val) != 0;
        else if (run_read_line(key, val)) ok |= !strcmp(key, "seed");
        else game_load_line(key, val);
    }
    fclose(f);
    if (!ok) {
        memset(&R, 0, sizeof(R));
        game_new(2, 3);
        return;
    }
    run_rebuild();
    G.level = R.layer;
    W.pl.charge_voice = W.pl.burn_voice = 0;
    campaign_snapshot();
}

static void campaign_restore(void) {
    G = camp.g;
    W.pl = camp.pl;
    W.pl.charge_voice = W.pl.burn_voice = 0;
    R = camp.run;
    retry_level = camp.retry;
}

/* game over or victory: the next run starts from scratch */
static void campaign_end(void) {
    has_campaign = false;
    if (!campaign_io) return;
    char p[1024];
    path_join(p, sizeof(p), "run.txt");
    remove(p);
}

/* the pilot record learns how the run went */
static void run_finish(bool won) {
    run_won = won;
    g_prof.runs++;
    if (R.layer + 1 > g_prof.best_sector) g_prof.best_sector = R.layer + 1;
    if (G.score > g_prof.best_score) g_prof.best_score = G.score;
    if (won) {
        g_prof.wins++;
        if (run_heat() > g_prof.best_heat) g_prof.best_heat = run_heat();
    }
    if (R.daily) {
        int date = 0;
        daily_seed(&date);
        if (g_prof.daily_date != date) { g_prof.daily_date = date; g_prof.daily_best = 0; }
        if (G.score > g_prof.daily_best) g_prof.daily_best = G.score;
    }
    profile_save();
}

/* ------------------------------------------------------------ state changes */
static void enter_state(AppState s);

static void goto_state(AppState s) {
    if (pending) return;
    pending = true;
    pending_state = s;
}

static void set_state_now(AppState s) {
    pending = false;
    enter_state(s);
}

/* ------------------------------------------------------------ background (menus) */
typedef struct { int type; V2 pos, vel; float ang, spin, scale; } Flyer;
static Flyer flyers[7];
static float bg_boom_t = 0;
static bool bg_ready = false;

static void bg_init(void) {
    grid_init(2600, 1500, 40);
    fx_clear();
    g_fx_collide = false;
    g_cam.pos = v2(1300, 750);
    g_cam.zoom = 1.0f;
    g_cam.shake = v2(0, 0);
    static const int types[7] = {RB_DRONE, RB_LIFTER, RB_SPIDER, RB_HULK, RB_GOPHER, RB_SUPERHULK, RB_DRILLER};
    for (int i = 0; i < 7; i++) {
        flyers[i].type = types[i];
        flyers[i].pos = v2(frandr(0, g_virt_w), frandr(0, VIRT_H));
        flyers[i].vel = v2scale(v2fromang(frand() * TAU), 12 + frand() * 18);
        flyers[i].ang = frand() * TAU;
        flyers[i].spin = frandr(-0.4f, 0.4f);
        flyers[i].scale = 14 + frand() * 10;
    }
    bg_ready = true;
}

static void bg_update_ex(float dt, bool booms) {
    if (!bg_ready) bg_init();
    bg_boom_t -= dt;
    if (bg_boom_t <= 0 && booms) {
        bg_boom_t = frandr(0.8f, 2.2f);
        V2 at = v2add(g_cam.pos, v2(frandr(-600, 600), frandr(-320, 320)));
        static const Col cols[5] = {{0.3f, 0.9f, 1, 1}, {1, 0.3f, 0.8f, 1}, {1, 0.6f, 0.2f, 1}, {0.4f, 1, 0.5f, 1}, {0.7f, 0.4f, 1, 1}};
        fx_explosion(at, 20 + frand() * 30, cols[irand(5)]);
    }
    /* invisible "ship" pushing the grid along a lissajous path */
    float t = g_time;
    V2 ghost = v2add(g_cam.pos, v2(sinf(t * 0.37f) * 520, sinf(t * 0.61f) * 260));
    grid_impulse(ghost, 90, 18);
    for (int i = 0; i < 7; i++) {
        Flyer *f = &flyers[i];
        f->pos = v2mad(f->pos, f->vel, dt);
        f->ang += f->spin * dt;
        if (f->pos.x < -60) f->pos.x = g_virt_w + 50;
        if (f->pos.x > g_virt_w + 60) f->pos.x = -50;
        if (f->pos.y < -60) f->pos.y = VIRT_H + 50;
        if (f->pos.y > VIRT_H + 60) f->pos.y = -50;
    }
    fx_update(dt);
    grid_update(dt);
}

static void bg_update(float dt) { bg_update_ex(dt, true); }

static void bg_draw(float dim) {
    grid_draw(rgba(0.18f, 0.2f, 0.7f, 1), dim);
    for (int i = 0; i < 7; i++) {
        Flyer *f = &flyers[i];
        const RobotDef *d = &RDEF[f->type];
        r_shape(&RSHAPE[f->type], f->pos, f->ang, f->scale, 3.5f, col_a(d->col, 0.28f * dim));
    }
    fx_draw();
}

/* ------------------------------------------------------------ animation */
/* 0..1 as a screen's pieces arrive one after another after it opens */
static float intro_k(float delay) { return ease_out_cubic((state_t - delay) / 0.4f); }

/* what is drawn until the r_pop arrives: it slides in from (dx, dy) and fades up */
static void slide_in(float delay, float dx, float dy) {
    float k = intro_k(delay);
    r_push(v2(dx * (1 - k), dy * (1 - k)), v2(0, 0), 1, 1, k);
}

/* a panel and everything in it power on from a line across the middle; pair with r_pop */
static void power_on(float cx, float cy, float delay) {
    float k = intro_k(delay);
    r_push(v2(0, 0), v2(cx, cy), 1, 0.03f + 0.97f * k, k);
}

/* the pointer cursor over anything clickable */
static void cursor_update(void) {
    static SDL_Cursor *arrow = NULL, *hand = NULL;
    static bool was = false;
    if (!arrow) {
        arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
        hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    }
    bool want = ui_point && state != S_PLAY;
    if (want != was && arrow && hand) {
        SDL_SetCursor(want ? hand : arrow);
        was = want;
    }
}

/* ------------------------------------------------------------ menus */
/* what the eased values of ui.c animate on these screens */
enum { UA_HOVER = 1, UA_ITEM, UA_CURSOR, UA_VALUE, UA_BAR, UA_CHIP, UA_SHIP, UA_LIFT, UA_HIT, UA_DENY };

/* a click on a menu sends a ripple through the background grid */
static void ui_ripple(V2 at, Col c, float power) {
    if (!bg_ready) return;
    V2 w = v2w(at);
    grid_impulse(w, 120 * power, 260 * power);
    fx_ring(w, 8, 70 * power, c, 0.4f, 3);
}

static int menu_update(int n, float y0, float spacing, float size, const float *widths) {
    if (ui.up) { menu_sel = (menu_sel - 1 + n) % n; snd_play(SND_MENU_MOVE, 0.5f, 1.0f); }
    if (ui.down) { menu_sel = (menu_sel + 1) % n; snd_play(SND_MENU_MOVE, 0.5f, 1.0f); }
    int hover = -1;
    for (int i = 0; i < n; i++) {
        float w = widths ? widths[i] : 300;
        float y = y0 + i * spacing;
        if (fabsf(ui.mouse.x - g_virt_w * 0.5f) < w * 0.5f + 20 && ui.mouse.y > y - 6 && ui.mouse.y < y + size + 6) hover = i;
    }
    if (hover >= 0) ui_point = true;
    if (hover >= 0 && ui.mouse_moved && hover != menu_sel) { menu_sel = hover; snd_play(SND_MENU_MOVE, 0.4f, 1.0f); }
    if (ui.confirm || (ui.click && hover >= 0)) {
        if (ui.click && hover >= 0) menu_sel = hover;
        snd_play(SND_MENU_SEL, 0.6f, 1.0f);
        ui_press(ui_id(UA_ITEM, menu_sel));
        return menu_sel;
    }
    return -1;
}

static void menu_draw(const char **items, int n, float y0, float spacing, float size, float *widths_out) {
    float cx = g_virt_w * 0.5f;
    float pulse = 0.5f + 0.5f * sinf(g_time * 6);
    /* the cursor springs after the selection */
    float cy = ui_spring(ui_id(UA_CURSOR, 0), y0 + menu_sel * spacing, 380, 26);
    float cw = ui_spring(ui_id(UA_CURSOR, 1), text_width(items[menu_sel], size * 1.08f), 380, 26);
    float press = ui_pressed(ui_id(UA_ITEM, menu_sel));
    float ck = intro_k(0.1f + menu_sel * 0.05f);
    r_add_hband(cx - cw * 0.5f - 170, cy - 9, cw + 340, size + 18, col_a(rgba(0.25f, 0.55f, 1, 1), (0.1f + 0.22f * press) * ck));
    for (int i = 0; i < n; i++) {
        /* the items slide in one by one, and swell and light up under the cursor */
        float k = intro_k(0.1f + i * 0.05f);
        float h = ui_ease(ui_id(UA_HOVER, i), i == menu_sel ? 1.0f : 0.0f, 16);
        float p = ui_pressed(ui_id(UA_ITEM, i));
        float sz = size * (1 + 0.08f * h + 0.1f * p);
        if (widths_out) widths_out[i] = text_width(items[i], size);
        float y = y0 + i * spacing + (1 - k) * 22 - (sz - size) * 0.5f;
        Col c = col_white(col_lerp(rgba(0.45f, 0.6f, 0.85f, 0.85f), rgba(0.6f, 0.95f, 1, 1), h), 0.75f * p);
        r_push(v2(0, 0), v2(0, 0), 1, 1, k);
        r_text_glowk(items[i], cx, y, sz, c, AL_CENTER, h);
        r_pop();
    }
    /* the brackets ride the cursor and kick out on a press */
    float bx = cw * 0.5f + 22 + pulse * 4 + press * 18;
    Col bc = col_a(col_white(C_MAGENTA, press), ck);
    V2 l[3] = {v2(cx - bx + 8, cy - 2), v2(cx - bx, cy + size * 0.5f), v2(cx - bx + 8, cy + size + 2)};
    V2 r[3] = {v2(cx + bx - 8, cy - 2), v2(cx + bx, cy + size * 0.5f), v2(cx + bx - 8, cy + size + 2)};
    r_polyline(l, 3, false, 4, bc);
    r_polyline(r, 3, false, 4, bc);
    /* now and then a glint runs across the selection */
    float g = fmodf(g_time * 0.55f, 2.2f);
    if (g < 1) r_glow(v2(cx - cw * 0.5f + cw * g, cy + size * 0.5f), size * 1.4f, col_a(C_WHITE, 0.14f * sinf(g * PI) * ck));
}

static float menu_widths[16];

static void draw_logo(float y, float alpha) {
    float cx = g_virt_w * 0.5f;
    float t = g_time;
    Col c1 = col_a(rgba(0.3f, 0.9f, 1, 1), alpha);
    Col c2 = col_a(col_lerp(rgba(1, 0.3f, 0.85f, 1), rgba(0.7f, 0.4f, 1, 1), 0.5f + 0.5f * sinf(t * 0.8f)), alpha);
    r_text_glow("BALLAST", cx, y + 36, 110, c2, AL_CENTER);
    float w = text_width("BALLAST", 110) * 0.5f + 30;
    r_line(v2(cx - w, y + 170), v2(cx + w, y + 170), 4, col_a(c1, 0.7f));
    r_glow(v2(cx - w + fmodf(t * 300, w * 2), y + 170), 22, col_a(C_WHITE, 0.35f * alpha));
}

static void hint(const char *s) { r_text(s, g_virt_w * 0.5f, VIRT_H - 36, 11, rgba(0.5f, 0.65f, 0.9f, 0.75f), AL_CENTER); }

/* ------------------------------------------------------------ typewriter */
static void typewriter(const char *text, float x, float y, float size, Col c, float t, float cps) {
    int n = t > 0 ? (int)(t * cps) : 0;
    int len = (int)strlen(text);
    if (n > len) n = len;
    if (len > 2047) len = n = 2047 < n ? 2047 : n;
    static char buf[2048];
    memcpy(buf, text, (size_t)n);
    buf[n] = 0;
    r_text(buf, x, y, size, c, AL_LEFT);
    if (n < len && fmodf(g_time * 3, 1) < 0.6f) {
        /* cursor at end of last line */
        const char *last = strrchr(buf, '\n');
        const char *line = last ? last + 1 : buf;
        int lines = 0;
        for (int i = 0; i < n; i++)
            if (buf[i] == '\n') lines++;
        float cx = x + text_width(line, size) + size * 0.4f;
        r_add_rect(cx, y + lines * size * 1.6f, size * 0.6f, size, col_a(c, 0.8f));
    }
}
static bool typewriter_done(const char *text, float t, float cps) { return t > 0 && (int)(t * cps) >= (int)strlen(text); }

static const char *STORY =
    "YEAR 2187.\n"
    "\n"
    "The Helix Deep Mining Consortium runs automated mines\n"
    "across the solar system. Seventy hours ago an unknown\n"
    "signal reached every mining robot at the same moment.\n"
    "They stopped digging. Then they started killing.\n"
    "\n"
    "You are a contract pilot. Fly into the infected mines,\n"
    "destroy their reactors and get out before each mine\n"
    "tears itself apart. The Consortium pays for every\n"
    "shard of salvage you haul home - but a full hold\n"
    "flies like a brick, and a dead pilot banks nothing.\n"
    "\n"
    "The pay is excellent. The survival rate is not.";

/* ------------------------------------------------------------ enter state */
static void hangar_enter(void);
static void setup_enter(void);
static float tally_t = 0;
static int tally_line = 0;
static bool tally_final = false; /* the escaped sector was the last one: the tally leads to the victory */
static int map_sel = 0;
static int map_shown = -1;        /* the sector the chart's panel shows, and since when: it re-reveals on a change */
static float map_shown_t = 0;
static bool summary_saved = false;
static char summary_path[1024];

/* test starts: modules by key, extra cargo, the debug loadout */
static void apply_debug_start(void) {
    if (opt_god) W.pl.spawn_inv = 1e9f;
    if (opt_allweapons) game_debug_loadout();
    if (opt_cargo > 0) W.pl.cargo = opt_cargo;
    if (opt_tp_x >= 0) {
        W.pl.pos = tile_center(opt_tp_x, opt_tp_y);
        g_cam.pos = W.pl.pos;
        opt_tp_x = -1;
    }
    if (opt_arena) {
        /* into the arena, a little way from the reactor or the Overseer */
        V2 c = W.reactor.exists ? W.reactor.pos : W.boss_idx >= 0 ? W.rob[W.boss_idx].pos : W.pl.pos;
        for (int k = 0; k < 16; k++) {
            V2 at = v2mad(c, v2fromang(PI / 2 + k * TAU / 16), 420);
            if (!point_in_rock(at) && los(at, c)) { W.pl.pos = at; break; }
        }
        g_cam.pos = W.pl.pos;
    }
    if (opt_destroy) {
        W.pl.keys = 7; /* a real pilot holds every key by the time the core falls */
        if (W.reactor.exists) {
            W.reactor.phase = W.reactor.nphases - 1; /* past the phase breaks, which would stop the hit */
            reactor_damage(1e9f);
        }
        else if (W.boss_idx >= 0) robot_damage(&W.rob[W.boss_idx], 1e9f, v2(1, 0), true);
        opt_destroy = opt_autorun;
    }
    if (opt_spawn) {
        /* robots by their map letters, in a ring around the ship */
        static const char *letters = "dlthsgvucaPkBq";
        static const int types[] = {RB_DRONE, RB_LIFTER, RB_TURRET, RB_HULK, RB_SPIDER, RB_GOPHER, RB_DRILLER, RB_SUPERHULK, RB_CLOAKER,
                                    RB_WASP, RB_PULSAR, RB_LANCER, RB_BOMBER, RB_CARRIER};
        int n = (int)strlen(opt_spawn);
        for (int i = 0; i < n; i++) {
            const char *c = strchr(letters, opt_spawn[i]);
            if (!c) continue;
            V2 at = v2mad(W.pl.pos, v2fromang(TAU * i / n), 330);
            if (point_in_rock(at)) at = v2mad(W.pl.pos, v2fromang(TAU * i / n), 160);
            Robot *r = robot_spawn(types[c - letters], at, false);
            if (!r) continue;
            G.robots_total++;
            if (opt_elite) { r->elite = true; r->maxhp = r->hp = r->maxhp * 1.8f; r->radius *= 1.15f; }
        }
    }
    if (opt_automap) automap_open();
    if (opt_reveal) memset(L.explored, 1, sizeof(L.explored));
}

static void install_debug_mods(void) {
    if (!opt_mods) return;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", opt_mods);
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ","))
        for (int i = 0; i < MOD_COUNT; i++)
            if (!strcmp(tok, MODS[i].key)) R.mods[i] = (uint8_t)MODS[i].max_rank;
    run_roll_draft();
    run_roll_shop();
}

static void enter_state(AppState s) {
    AppState prev = state;
    if (opt_frames > 0)
        SDL_Log("state %d -> %d (frame %d) score=%d lives=%d saved=%d sector=%d salvage=%d done=%d", (int)prev, (int)s, frame_count, G.score, G.lives,
                G.hostages_saved, R.layer, R.salvage, R.sectors_done);
    state = s;
    state_t = 0;
    menu_sel = 0;
    idle_t = 0;
    ui_reset();
    switch (s) {
    case S_INTRO:
        bg_init();
        music_play(SONG_MENU);
        break;
    case S_TITLE:
        game_shutdown_level();
        /* an aborted mine rolls back to the briefing before it */
        if (prev == S_PAUSE && has_campaign) campaign_restore();
        /* the intro hands its background and logo straight over */
        logo_landed = prev == S_INTRO && fade < 0.5f;
        if (prev != S_INTRO) bg_init();
        music_play(SONG_MENU);
        break;
    case S_SETUP:
        setup_enter();
        break;
    case S_STORY:
        music_play(SONG_BRIEF);
        break;
    case S_MAP:
        bg_init();
        R.phase = PH_MAP;
        map_sel = run_reachable(R.layer, R.path[R.layer]) ? R.path[R.layer] : run_first_reachable();
        map_shown = -1;
        chart_enter(map_sel);
        campaign_save();
        music_play(SONG_BRIEF);
        break;
    case S_BRIEFING:
        game_shutdown_level();
        if (!run_reachable(R.layer, R.path[R.layer])) R.path[R.layer] = run_first_reachable();
        R.phase = PH_BRIEF;
        run_prepare_sector();
        holo_setup();
        campaign_save();
        bg_init();
        music_play(SONG_BRIEF);
        break;
    case S_PLAY:
        if (prev != S_PAUSE) {
            g_fx_collide = true;
            bg_ready = false;
            game_start_level(retry_level || G.level_score_start < 0);
            retry_level = false;
            tally_unlock_from = R.nunlocked;
            apply_debug_start();
        }
        game_audio_pause(false);
        SDL_HideCursor();
        break;
    case S_PAUSE:
        game_audio_pause(true);
        SDL_ShowCursor();
        break;
    case S_TALLY:
        tally_t = 0;
        tally_line = 0;
        game_shutdown_level();
        /* the escaped ship, its bonuses and the banked cargo carry over: the run resumes in the hangar */
        tally_final = run_final_sector();
        if (!tally_final) {
            run_advance();
            campaign_save();
        }
        music_play(SONG_BRIEF);
        SDL_ShowCursor();
        break;
    case S_GAMEOVER:
        game_shutdown_level();
        run_finish(false);
        campaign_end();
        music_play(SONG_GAMEOVER);
        SDL_ShowCursor();
        bg_init();
        break;
    case S_VICTORY:
        game_shutdown_level();
        profile_complete(CH_WIN);
        if (run_heat() >= 3) profile_complete(CH_HOTSTREAK);
        run_finish(true);
        campaign_end();
        music_play(SONG_VICTORY);
        bg_init();
        SDL_ShowCursor();
        break;
    case S_SUMMARY:
        summary_saved = false;
        summary_path[0] = 0;
        if (!bg_ready) bg_init();
        break;
    case S_NAME:
        name_buf[0] = 0;
        SDL_StartTextInput(g_window);
        break;
    case S_SCORES:
        if (prev != S_NAME) new_score_rank = -1;
        SDL_StopTextInput(g_window);
        if (prev == S_SUMMARY || prev == S_NAME) music_play(SONG_MENU);
        break;
    case S_OPTIONS:
    case S_HELP:
        help_page = 0;
        break;
    case S_HANGAR:
        bg_init();
        R.phase = PH_HANGAR;
        hangar_enter();
        campaign_save();
        music_play(SONG_BRIEF);
        break;
    case S_RECORD:
        break;
    }
    if (s != S_PLAY) SDL_ShowCursor();
}

/* ------------------------------------------------------------ intro */
static void intro_update(float dt) {
    cine_intro_update(state_t);
    if (state_t > 22.4f) bg_update_ex(dt, false);
    if ((ui.confirm || ui.click || ui.back) && state_t > 0.3f) goto_state(S_TITLE);
    else if (state_t >= INTRO_LEN) set_state_now(S_TITLE);
}

static void intro_draw(void) {
    if (state_t > 22.4f) bg_draw(clampf((state_t - 22.4f) / 2.2f, 0, 1));
    cine_intro_draw(state_t);
    if (state_t > 1.0f && state_t < 21.2f) r_text("PRESS FIRE TO SKIP", g_virt_w * 0.5f, 48, 9, rgba(0.45f, 0.65f, 1, 0.5f), AL_CENTER);
}

/* ------------------------------------------------------------ title */
/* CONTINUE is only listed while a run is in progress */
static const char *TITLE_ITEMS[8] = {"CONTINUE", "NEW RUN", "DAILY CHALLENGE", "PILOT RECORD", "HOW TO PLAY", "HIGH SCORES", "OPTIONS", "QUIT"};
static int title_first(void) { return has_campaign ? 0 : 1; }
static bool setup_daily = false;

static void continue_run(void) {
    switch (R.phase) {
    case PH_MAP: goto_state(S_MAP); break;
    case PH_BRIEF: goto_state(S_BRIEFING); break;
    default: goto_state(S_HANGAR); break;
    }
}

static void title_update(float dt) {
    bg_update(dt);
    int first = title_first();
    int r = menu_update(8 - first, 368, 36, 19, menu_widths);
    switch (r < 0 ? -1 : r + first) {
    case 0: continue_run(); break;
    case 1: setup_daily = false; goto_state(S_SETUP); break;
    case 2: setup_daily = true; goto_state(S_SETUP); break;
    case 3: goto_state(S_RECORD); break;
    case 4: return_state = S_TITLE; goto_state(S_HELP); break;
    case 5: goto_state(S_SCORES); break;
    case 6: return_state = S_TITLE; goto_state(S_OPTIONS); break;
    case 7: running = false; break;
    }
    if (ui.back && state_t > 0.3f) running = false;
    /* attract mode */
    if (idle_t > 75 && opt_frames < 0 && !opt_autorun) goto_state(S_INTRO);
}

static void title_draw(void) {
    bg_draw(1.0f);
    float cx = g_virt_w * 0.5f;
    draw_logo(96, logo_landed ? 1.0f : clampf(state_t * 1.5f, 0, 1));
    float tk = logo_landed ? 1 : intro_k(0.2f);
    r_push(v2(0, 8 * (1 - tk)), v2(0, 0), 1, 1, tk);
    r_text_glow("GREED HAS MASS", cx, 284, 15, col_a(powerup_color(PU_SALVAGE), 0.95f), AL_CENTER);
    r_text("LOOT THE MINE.  BLOW THE REACTOR.  ESCAPE - IF YOU CAN STILL FLY.", cx, 310, 10, rgba(0.6f, 0.75f, 1, 0.75f), AL_CENTER);
    r_pop();
    int first = title_first();
    menu_draw(TITLE_ITEMS + first, 8 - first, 368, 36, 19, menu_widths);
    r_push(v2(0, 0), v2(0, 0), 1, 1, intro_k(0.1f));
    if (has_campaign) {
        r_textf(cx, 338, 10, rgba(0.55f, 0.7f, 0.95f, 0.75f), AL_CENTER, "%s IN PROGRESS  -  SECTOR %d OF %d  -  %s  -  HEAT %d  -  SCORE %d",
                R.daily ? "DAILY RUN" : "RUN", R.layer + 1, R.nlayers, run_ship()->name, run_heat(),
                G.score);
        char sb[24];
        snprintf(sb, sizeof(sb), "%d", R.salvage);
        draw_salvage(sb, g_virt_w - 30, 24, 14, powerup_color(PU_SALVAGE), AL_RIGHT);
    }
    r_pop();
    int date = 0;
    daily_seed(&date);
    int di = 2 - first;
    float dy = 368 + di * 36;
    float dk = intro_k(0.25f + di * 0.05f);
    r_push(v2(-12 * (1 - dk), 0), v2(0, 0), 1, 1, dk);
    if (g_prof.daily_date == date && g_prof.daily_best > 0)
        r_textf(cx + menu_widths[di] * 0.5f + 44, dy + 5, 9, col_a(C_YELLOW, 0.8f), AL_LEFT, "TODAY'S BEST %d", g_prof.daily_best);
    else r_textf(cx + menu_widths[di] * 0.5f + 44, dy + 5, 9, col_a(C_YELLOW, 0.6f), AL_LEFT, "%04d-%02d-%02d", date / 10000, date / 100 % 100, date % 100);
    r_pop();
    r_push(v2(0, 0), v2(0, 0), 1, 1, intro_k(0.5f));
    r_text("A TOP-DOWN VECTOR MINE SHOOTER", cx, VIRT_H - 56, 9, rgba(0.45f, 0.55f, 0.8f, 0.6f), AL_CENTER);
    hint(g_gamepad_connected ? "D-PAD SELECT   A CONFIRM   B BACK" : "ARROWS / MOUSE SELECT   ENTER CONFIRM   ESC QUIT");
    r_pop();
}

/* ------------------------------------------------------------ run setup */
enum { SR_SHIP, SR_SKILL, SR_PROTOCOLS, SR_SEED, SR_LAUNCH, SR_ROWS };
static int setup_row = SR_LAUNCH;
static int setup_ship = SHIP_WRAITH, setup_diff = 2;
static uint32_t setup_heat = 0;
static int setup_chip = 0;
static char setup_seed[12] = "";
static bool setup_editing = false;

static const char *SKILL_DESC[3] = {
    "FORGIVING ROBOTS AND 50 SECONDS TO ESCAPE.",
    "A GENTLE CHALLENGE. ROBOTS RARELY AIM AHEAD.",
    "THE INTENDED EXPERIENCE. ROBOTS LEAD SHOTS.",
};

/* the day's challenge: the same seed, ship and protocols for every pilot */
static void daily_config(uint32_t seed, int *ship, int *diff, uint32_t *heat) {
    Rng r = rng_make(seed, 0xDA1u);
    static const int pool[6] = {TP_FUSE, TP_ARMORED, TP_SWARM, TP_HEAVY, TP_FRAGILE, TP_SCARCITY};
    int a = rng_int(&r, 6), b = (a + 1 + rng_int(&r, 5)) % 6;
    *ship = SHIP_WRAITH;
    *diff = 2;
    *heat = (1u << pool[a]) | (1u << pool[b]);
}

static void setup_enter(void) {
    setup_row = SR_LAUNCH;
    setup_editing = false;
    if (!ship_unlocked(setup_ship)) setup_ship = SHIP_WRAITH;
    if (!protocols_unlocked()) setup_heat = 0;
}

static void setup_values(int *ship, int *diff, uint32_t *heat) {
    *ship = setup_ship;
    *diff = setup_diff;
    *heat = setup_heat;
    if (setup_daily) daily_config(daily_seed(NULL), ship, diff, heat);
}

static void setup_launch(void) {
    ui_press(ui_id(UA_ITEM, SR_LAUNCH));
    int ship, diff;
    uint32_t heat, seed;
    setup_values(&ship, &diff, &heat);
    if (setup_daily) seed = daily_seed(NULL);
    else if (!seed_parse(setup_seed, &seed)) seed = hash32((uint32_t)SDL_GetTicksNS() ^ rng_next());
    run_new(ship, diff, heat, seed, setup_daily);
    install_debug_mods();
    if (opt_salvage > 0) R.salvage += opt_salvage;
    if (setup_daily) {
        int date = 0;
        daily_seed(&date);
        if (g_prof.daily_date != date) { g_prof.daily_date = date; g_prof.daily_best = 0; g_prof.daily_tries = 0; }
        g_prof.daily_tries++;
        profile_save();
    }
    retry_level = false;
    campaign_save();
    snd_play(SND_TELEPORT, 0.6f, 0.9f);
    goto_state(g_prof.seen_story ? S_HANGAR : S_STORY);
}

static void setup_cycle_ship(int dir) {
    for (int k = 1; k <= SHIP_COUNT; k++) {
        int s = ((setup_ship + dir * k) % SHIP_COUNT + SHIP_COUNT) % SHIP_COUNT;
        if (ship_unlocked(s)) { setup_ship = s; return; }
    }
}

static int setup_dir = 1; /* the way the last value turned: it slides in from that side */

static void setup_change(int row, int dir) {
    setup_dir = dir;
    int ship = setup_ship;
    switch (row) {
    case SR_SHIP: setup_cycle_ship(dir); break;
    case SR_SKILL: setup_diff = (setup_diff + dir + 3) % 3; break;
    case SR_PROTOCOLS: if (protocols_unlocked()) setup_chip = (setup_chip + dir + TP_COUNT) % TP_COUNT; break;
    case SR_SEED: if (dir < 0) setup_seed[0] = 0; break;
    }
    if ((row == SR_SHIP && setup_ship == ship) || (row == SR_PROTOCOLS && !protocols_unlocked())) {
        /* nothing else to pick yet: the value shakes its head */
        ui_press(ui_id(UA_DENY, row));
        snd_play(SND_NOAMMO, 0.5f, 1.0f);
        return;
    }
    if (row == SR_SHIP) ui_press(ui_id(UA_SHIP, 0));
    if (row != SR_PROTOCOLS) ui_press(ui_id(UA_VALUE, row));
    snd_play(SND_MENU_MOVE, 0.5f, dir > 0 ? 1.1f : 0.9f);
}

static float setup_row_y(int r) { return 160 + r * 70; }

static void setup_update(float dt) {
    bg_update(dt);
    if (setup_editing) {
        if (ui.confirm) {
            uint32_t v;
            if (setup_seed[0] && !seed_parse(setup_seed, &v)) setup_seed[0] = 0;
            setup_editing = false;
            SDL_StopTextInput(g_window);
            snd_play(SND_MENU_SEL, 0.6f, 1.0f);
        } else if (ui.back) {
            setup_editing = false;
            SDL_StopTextInput(g_window);
        }
        return;
    }
    if (ui.back) { goto_state(S_TITLE); return; }
    if (setup_daily) {
        setup_row = SR_LAUNCH;
        if (ui.confirm || ui.click) setup_launch();
        return;
    }
    if (ui.up) { setup_row = (setup_row + SR_ROWS - 1) % SR_ROWS; snd_play(SND_MENU_MOVE, 0.5f, 1.0f); }
    if (ui.down) { setup_row = (setup_row + 1) % SR_ROWS; snd_play(SND_MENU_MOVE, 0.5f, 1.0f); }
    float x0 = g_virt_w * 0.5f - 560;
    int hover = -1;
    for (int r = 0; r < SR_ROWS; r++)
        if (ui.mouse.x > x0 && ui.mouse.x < x0 + 600 && fabsf(ui.mouse.y - setup_row_y(r) - 12) < 26) hover = r;
    if (hover >= 0) ui_point = true;
    if (hover >= 0 && ui.mouse_moved && hover != setup_row) { setup_row = hover; snd_play(SND_MENU_MOVE, 0.4f, 1.0f); }
    if (ui.left) setup_change(setup_row, -1);
    if (ui.right) setup_change(setup_row, 1);
    if (ui.confirm || (ui.click && hover >= 0)) {
        snd_play(SND_MENU_SEL, 0.6f, 1.0f);
        switch (setup_row) {
        case SR_PROTOCOLS:
            if (protocols_unlocked()) {
                setup_heat ^= 1u << setup_chip;
                ui_press(ui_id(UA_CHIP, setup_chip));
            }
            break;
        case SR_SEED:
            setup_editing = true;
            SDL_StartTextInput(g_window);
            break;
        case SR_LAUNCH: setup_launch(); break;
        default: setup_change(setup_row, 1); break;
        }
    }
}

static void setup_text_input(const char *text) {
    for (const char *c = text; *c; c++) {
        char ch = *c;
        if (ch >= 'a' && ch <= 'f') ch -= 32;
        size_t l = strlen(setup_seed);
        if (l < 9 && ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || ch == '-')) {
            setup_seed[l] = ch;
            setup_seed[l + 1] = 0;
        }
    }
}

static void setup_draw(void) {
    bg_draw(0.55f);
    float VW = g_virt_w, cx = VW * 0.5f;
    int ship, diff;
    uint32_t heat;
    setup_values(&ship, &diff, &heat);
    int hsum = 0;
    for (int i = 0; i < TP_COUNT; i++)
        if ((heat >> i) & 1) hsum += PROTOCOLS[i].heat;
    float tk = intro_k(0);
    r_push(v2(0, -12 * (1 - tk)), v2(0, 0), 1, 1, tk);
    r_text_glow(setup_daily ? "DAILY CHALLENGE" : "NEW RUN", cx, 44, 36, rgba(0.4f, 0.9f, 1, 1), AL_CENTER);
    if (setup_daily) {
        int date = 0;
        daily_seed(&date);
        r_textf(cx, 96, 11, col_a(C_YELLOW, 0.85f), AL_CENTER, "%04d-%02d-%02d  -  THE SAME MINES, SHIP AND PROTOCOLS FOR EVERY PILOT TODAY", date / 10000,
                date / 100 % 100, date % 100);
    } else r_text("PICK YOUR SHIP AND YOUR ODDS", cx, 96, 11, rgba(0.6f, 0.75f, 1, 0.8f), AL_CENTER);
    r_pop();

    float x0 = cx - 560, xv = x0 + 230;
    static const char *labels[SR_ROWS] = {"SHIP", "SKILL", "THREAT PROTOCOLS", "SEED", ""};
    char val[96];
    /* the highlight springs after the selected row */
    float hy = ui_spring(ui_id(UA_CURSOR, 0), setup_row_y(setup_row), 380, 26);
    float hb = ui_ease(ui_id(UA_CURSOR, 1), setup_row != SR_LAUNCH ? 1.0f : 0.0f, 14) * intro_k(0.1f);
    if (hb > 0.01f) {
        r_add_rect(x0, hy - 10, 600, 44, rgba(0.2f, 0.5f, 1, 0.08f * hb));
        r_add_hband(x0 - 60, hy + 34, 720, 1.5f, col_a(C_CYAN, 0.4f * hb));
        V2 tri[3] = {v2(x0 - 16, hy), v2(x0 - 4, hy + 10), v2(x0 - 16, hy + 20)};
        r_polyline(tri, 3, true, 3, col_a(C_MAGENTA, hb));
    }
    for (int r = 0; r < SR_ROWS; r++) {
        float y = setup_row_y(r);
        bool sel = r == setup_row;
        float k = intro_k(0.08f + r * 0.05f);
        float h = ui_ease(ui_id(UA_HOVER, r), sel ? 1.0f : 0.0f, 16);
        Col lc = col_lerp(rgba(0.45f, 0.6f, 0.85f, 0.85f), C_WHITE, h);
        r_push(v2(-26 * (1 - k), 0), v2(0, 0), 1, 1, k);
        if (r == SR_LAUNCH) {
            const char *t = setup_daily ? "LAUNCH TODAY'S RUN" : "LAUNCH";
            float p = ui_pressed(ui_id(UA_ITEM, SR_LAUNCH));
            float sz = 24 * (1 + 0.06f * h + 0.12f * p);
            Col c = col_white(col_lerp(col_a(C_CYAN, 0.6f), rgba(0.6f, 0.95f, 1, 1), h), p);
            r_text_glowk(t, x0 + 20, y - (sz - 24) * 0.5f, sz, c, AL_LEFT, 0.4f + 0.6f * h);
            if (h > 0.01f) {
                float w = text_width(t, sz), nudge = 4 * (0.5f + 0.5f * sinf(g_time * 6)) + 14 * p;
                V2 tri[3] = {v2(x0 + 30 + w + nudge, y), v2(x0 + 44 + w + nudge, y + 12), v2(x0 + 30 + w + nudge, y + 24)};
                r_polyline(tri, 3, true, 4, col_a(col_white(C_MAGENTA, p), h));
            }
            if (has_campaign) r_text("A NEW RUN ENDS THE ONE IN PROGRESS", x0 + 20, y + 36, 9, col_a(C_ORANGE, 0.9f), AL_LEFT);
            r_pop();
            continue;
        }
        r_text(labels[r], x0 + 12 + 6 * h, y + 4, 12, lc, AL_LEFT);
        Col vc = setup_daily ? rgba(0.7f, 0.75f, 0.85f, 0.8f) : col_lerp(rgba(0.8f, 0.9f, 1, 0.9f), rgba(0.6f, 0.95f, 1, 1), h);
        switch (r) {
        case SR_SHIP: snprintf(val, sizeof(val), "%s", SHIPS[ship].name); break;
        case SR_SKILL: snprintf(val, sizeof(val), "%s", DIFF_NAMES[diff]); break;
        case SR_PROTOCOLS:
            if (!protocols_unlocked() && !setup_daily) snprintf(val, sizeof(val), "LOCKED");
            else snprintf(val, sizeof(val), "HEAT %d", hsum);
            break;
        case SR_SEED:
            if (setup_daily) seed_format(daily_seed(NULL), val, sizeof(val));
            else if (setup_editing) snprintf(val, sizeof(val), "%s%s", setup_seed, fmodf(g_time * 2, 1) < 0.5f ? "_" : " ");
            else snprintf(val, sizeof(val), "%s", setup_seed[0] ? setup_seed : "RANDOM");
            break;
        }
        /* a changed value slides in from the side it turned to */
        float vp = ui_pressed(ui_id(UA_VALUE, r));
        r_text(val, xv + setup_dir * 24 * vp + ui_shake(ui_id(UA_DENY, r)), y, 18, col_a(col_white(vc, 0.5f * vp), 1 - 0.5f * vp), AL_LEFT);
        if (!setup_daily && r != SR_SEED && r != SR_PROTOCOLS && h > 0.01f) {
            float w = text_width(val, 18);
            Col ac = col_a(C_MAGENTA, 0.8f * h);
            r_text("<", xv - 22 - (setup_dir < 0 ? 10 * vp : 0), y, 18, setup_dir < 0 ? col_white(ac, vp) : ac, AL_LEFT);
            r_text(">", xv + w + 10 + (setup_dir > 0 ? 10 * vp : 0), y, 18, setup_dir > 0 ? col_white(ac, vp) : ac, AL_LEFT);
        }
        /* a line of detail under the value */
        const char *sub = NULL;
        char subbuf[96];
        switch (r) {
        case SR_SHIP: {
            int un = 0;
            for (int i = 0; i < SHIP_COUNT; i++) un += ship_unlocked(i);
            snprintf(subbuf, sizeof(subbuf), "%s  -  %d OF %d SHIPS UNLOCKED", SHIPS[ship].role, un, SHIP_COUNT);
            sub = subbuf;
        } break;
        case SR_SKILL: sub = SKILL_DESC[diff]; break;
        case SR_PROTOCOLS: sub = protocols_unlocked() || setup_daily ? "PER HEAT: +10% SCORE, +5% SALVAGE" : "UNLOCK: REACH THE FOURTH SECTOR OF A RUN"; break;
        case SR_SEED: sub = setup_editing ? "TYPE A HEX SEED, ENTER TO CONFIRM" : "SHARE IT: THE SAME MINES AND DRAFTS"; break;
        }
        if (sub) r_text(sub, xv, y + 26, 8.5f, col_a(vc, 0.6f), AL_LEFT);
        r_pop();
    }

    /* right panel: the ship, or the protocol list; it powers on, and the two cross-fade */
    float px = cx + 120, pw = VW * 0.5f - 150, py = 140, ph = 440;
    if (pw > 520) pw = 520;
    bool proto = setup_row == SR_PROTOCOLS && (protocols_unlocked() || setup_daily);
    float pm = ui_ease(ui_id(UA_CURSOR, 2), proto ? 1.0f : 0.0f, 12);
    float pk = intro_k(0.25f);
    r_push(v2(0, 0), v2(px + pw * 0.5f, py + ph * 0.5f), 1, 0.03f + 0.97f * pk, pk);
    r_panel(px, py, pw, ph, col_lerp(SHIPS[ship].col, C_ORANGE, setup_row == SR_PROTOCOLS ? 1 : pm), 0.72f);
    if (proto) {
        r_push(v2(0, 0), v2(0, 0), 1, 1, pm);
        r_text("THREAT PROTOCOLS", px + 24, py + 20, 14, C_ORANGE, AL_LEFT);
        r_textf(px + pw - 24, py + 20, 14, C_ORANGE, AL_RIGHT, "HEAT %d", hsum);
        float cyp = ui_spring(ui_id(UA_CHIP, 100), py + 58 + setup_chip * 38, 380, 26);
        for (int i = 0; i < TP_COUNT; i++) {
            float y = py + 58 + i * 38;
            bool on = (heat >> i) & 1, cur = i == setup_chip && !setup_daily;
            float p = ui_pressed(ui_id(UA_CHIP, i));
            float fk = ui_ease(ui_id(UA_CHIP, 50 + i), on ? 1.0f : 0.0f, 18);
            float ch = ui_ease(ui_id(UA_CHIP, 150 + i), cur ? 1.0f : 0.0f, 16);
            Col c = col_lerp(rgba(0.6f, 0.65f, 0.8f, 0.7f), C_ORANGE, fk);
            /* a toggled box pops and flares */
            float g = 3 * p;
            r_frame(px + 24 - g, y - g, 16 + 2 * g, 16 + 2 * g, 2.5f, col_a(col_white(c, p), 0.9f));
            if (fk > 0.02f) {
                float fs = 8 * ease_out_back(fk);
                r_fill_rect(px + 32 - fs * 0.5f, y + 8 - fs * 0.5f, fs, fs, c);
            }
            if (p > 0) r_glow(v2(px + 32, y + 8), 34, col_a(C_ORANGE, 0.5f * p));
            r_textf(px + 52 + 5 * ch, y, 11, col_lerp(c, C_WHITE, ch), AL_LEFT, "%s  +%d", PROTOCOLS[i].name, PROTOCOLS[i].heat);
            r_text(PROTOCOLS[i].desc, px + 52 + 5 * ch, y + 16, 7.5f, col_a(c, 0.75f), AL_LEFT);
        }
        if (!setup_daily) r_polyline((V2[]){v2(px + 12, cyp + 2), v2(px + 18, cyp + 8), v2(px + 12, cyp + 14)}, 3, true, 2.5f, C_MAGENTA);
        if (!setup_daily) r_text("LEFT / RIGHT PICK   ENTER TOGGLE", px + pw * 0.5f, py + ph - 26, 9, col_a(C_WHITE, 0.6f), AL_CENTER);
        r_pop();
    } else {
        const ShipDef *sd = &SHIPS[ship];
        r_push(v2(0, 0), v2(0, 0), 1, 1, 1 - pm);
        /* the ship on a holo turntable, drawing itself on when it changes */
        float rv = minf(1 - ui_pressed(ui_id(UA_SHIP, 0)), intro_k(0.35f));
        V2 sc = v2(px + pw * 0.5f, py + 100);
        r_glow(sc, 90, col_a(sd->col, 0.2f));
        holo_ship(sc, 48, g_time * 0.8f, sd->col, rv);
        r_text_reveal(sd->name, px + pw * 0.5f, py + 172, 22, sd->col, AL_CENTER, clampf(rv * 1.4f, 0, 1), true);
        r_text(sd->role, px + pw * 0.5f, py + 204, 10, col_a(sd->col, 0.75f), AL_CENTER);
        r_text(sd->desc, px + pw * 0.5f, py + 228, 9.5f, rgba(0.85f, 0.9f, 1, 0.9f), AL_CENTER);
        static const char *stat[5] = {"SHIELD", "ENERGY", "THRUST", "BURNER", "CARGO MASS"};
        float v[5] = {sd->shield / 140, sd->energy / 140, sd->accel / 1.25f, sd->burner / 1.5f, sd->cargo / 1.4f};
        for (int i = 0; i < 5; i++) {
            float y = py + 272 + i * 23;
            /* the bars grow into the new ship's stats, one after another */
            float bv = ui_ease(ui_id(UA_BAR, i), clampf(v[i], 0, 1) * intro_k(0.4f + i * 0.05f), 9 - i);
            r_text(stat[i], px + 30, y, 9, col_a(C_WHITE, 0.7f), AL_LEFT);
            r_add_rect(px + 150, y + 2, (pw - 190), 6, col_a(sd->col, 0.12f));
            r_add_rect(px + 150, y + 2, (pw - 190) * bv, 6, col_a(i == 4 ? C_ORANGE : sd->col, 0.85f));
            r_glow(v2(px + 150 + (pw - 190) * bv, y + 5), 9, col_a(i == 4 ? C_ORANGE : sd->col, 0.5f));
        }
        char sm[64];
        if (sd->start_mod >= 0) snprintf(sm, sizeof(sm), "STARTS WITH %s  -  %d SLOTS", MODS[sd->start_mod].name, sd->slots);
        else snprintf(sm, sizeof(sm), "NO STARTING MODULE  -  %d SLOTS", sd->slots);
        r_text(sm, px + pw * 0.5f, py + ph - 30, 9, col_a(sd->col, 0.85f), AL_CENTER);
        r_pop();
    }
    r_pop();
    r_push(v2(0, 0), v2(0, 0), 1, 1, intro_k(0.5f));
    hint(setup_daily ? "ENTER LAUNCH   ESC BACK" : "UP / DOWN SELECT   LEFT / RIGHT CHANGE   ENTER CONFIRM   ESC BACK");
    r_pop();
}

/* ------------------------------------------------------------ story */
static void story_update(float dt) {
    bg_update(dt);
    if (ui.confirm || ui.click) {
        if (!typewriter_done(STORY, state_t, 55)) state_t = 1000;
        else {
            g_prof.seen_story = true;
            profile_save();
            goto_state(S_HANGAR);
        }
    }
    if (ui.back) {
        g_prof.seen_story = true;
        profile_save();
        goto_state(S_HANGAR);
    }
}

static void story_draw(void) {
    bg_draw(0.5f);
    power_on(g_virt_w * 0.5f, 360, 0);
    r_panel(g_virt_w * 0.5f - 420, 100, 840, 520, rgba(0.3f, 0.8f, 1, 1), 0.75f);
    r_text_reveal("INCOMING TRANSMISSION", g_virt_w * 0.5f, 126, 22, rgba(0.4f, 0.9f, 1, 1), AL_CENTER, clampf((state_t - 0.2f) / 0.6f, 0, 1), true);
    typewriter(STORY, g_virt_w * 0.5f - 380, 185, 14, rgba(0.85f, 0.92f, 1, 1), state_t, 55);
    r_pop();
    if (typewriter_done(STORY, state_t, 55) && fmodf(g_time * 2, 1) < 0.7f) hint("PRESS FIRE TO CONTINUE");
}

/* a rotating selection ring, shared by the chart and the hangar */
static void sel_ring(V2 c, float r) {
    float sp = 0.5f + 0.5f * sinf(g_time * 6);
    for (int k = 0; k < 4; k++) {
        float a0 = -g_time * 0.8f + k * TAU / 4;
        r_arc(c, r + sp * 2, a0, a0 + 0.8f, 4, C_MAGENTA, 8);
    }
}

/* ------------------------------------------------------------ sector chart */
/* the 3D table is chart.c; the header and the panel of the selected sector are drawn here */

static void map_update(float dt) {
    chart_update(dt, map_sel, ui.mouse, g_in.mouse_active);
    float lk = chart_launch_k();
    if (lk >= 0) {
        /* the ship is flying: the briefing fades in as it dives into the mine */
        if (lk >= 0.84f) goto_state(S_BRIEFING);
        return;
    }
    int n = R.nnodes[R.layer];
    if (ui.up || ui.left) {
        for (int k = 1; k <= n; k++) {
            int i = (map_sel - k + n * 4) % n;
            if (run_reachable(R.layer, i)) { map_sel = i; snd_play(SND_MENU_MOVE, 0.5f, 1.0f); break; }
        }
    }
    if (ui.down || ui.right) {
        for (int k = 1; k <= n; k++) {
            int i = (map_sel + k) % n;
            if (run_reachable(R.layer, i)) { map_sel = i; snd_play(SND_MENU_MOVE, 0.5f, 1.0f); break; }
        }
    }
    int hover = chart_pick(ui.mouse);
    if (hover >= 0) ui_point = true;
    if (hover >= 0 && ui.mouse_moved && hover != map_sel) { map_sel = hover; snd_play(SND_MENU_MOVE, 0.4f, 1.0f); }
    if (ui.confirm || (ui.click && hover >= 0)) {
        if (ui.click && hover >= 0) map_sel = hover;
        R.path[R.layer] = map_sel;
        snd_play(SND_TELEPORT, 0.6f, 0.9f);
        chart_launch(map_sel);
    }
    if (ui.back) goto_state(S_HANGAR);
}

static void map_draw(void) {
    float VW = g_virt_w, cx = VW * 0.5f;
    chart_draw(map_sel, ui.mouse, g_in.mouse_active);
    /* the flat UI clears away for the flight */
    float out = smooth01(maxf(0, chart_launch_k()) / 0.4f);
    float in = intro_k(0.15f);
    r_push(v2(0, -14 * (1 - in) - 40 * out), v2(0, 0), 1, 1, in * (1 - out));
    r_text_glow("SECTOR CHART", cx, 22, 30, rgba(0.4f, 0.9f, 1, 1), AL_CENTER);
    char sd[24];
    seed_format(R.seed, sd, sizeof(sd));
    r_textf(cx, 62, 10, rgba(0.6f, 0.7f, 0.9f, 0.8f), AL_CENTER, "%sSEED %s  -  %s  -  HEAT %d  -  SHIPS %d  -  SCORE %d", R.daily ? "DAILY  -  " : "", sd,
            run_ship()->name, run_heat(), G.lives, G.score);
    char buf[48];
    snprintf(buf, sizeof(buf), "%d", R.salvage);
    draw_salvage(buf, VW - 36, 24, 20, powerup_color(PU_SALVAGE), AL_RIGHT);
    r_pop();

    /* the selected sector: the panel powers on, and its lines re-reveal when the selection moves */
    const SectorNode *nd = run_node(R.layer, map_sel);
    if (map_sel != map_shown) {
        map_shown = map_sel;
        map_shown_t = g_time;
    }
    float st = g_time - map_shown_t;
    float pw = minf(1100, VW - 60), x0 = cx - pw * 0.5f, x1 = x0 + pw - 24, y0 = 520;
    Col zc = nd->reward == SR_BOSS ? C_MAGENTA : zone_color(nd->zone);
    float pk = intro_k(0.45f);
    r_push(v2(0, 90 * out), v2(cx, y0 + 70), 1, 0.04f + 0.96f * pk, pk * (1 - out));
    r_panel(x0, y0, pw, 140, zc, 0.8f);
    r_text_reveal(nd->name, x0 + 24, y0 + 14, 18, zc, AL_LEFT, clampf(st / 0.35f, 0, 1), true);
    float la = smooth01((st - 0.08f) / 0.25f);
    r_push(v2(12 * (1 - la), 0), v2(0, 0), 1, 1, la);
    if (nd->reward == SR_BOSS)
        r_textf(x0 + 24, y0 + 42, 8, col_a(zc, 0.8f), AL_LEFT, "SECTOR %d OF %d  -  THE FOUNDRY ARENA  -  3 ACCESS KEYS", R.layer + 1, R.nlayers);
    else
        r_textf(x0 + 24, y0 + 42, 8, col_a(zc, 0.8f), AL_LEFT, "SECTOR %d OF %d  -  ABOUT %d CHAMBERS  -  %d ACCESS KEY%s", R.layer + 1, R.nlayers, nd->rooms, nd->keys,
                nd->keys == 1 ? "" : "S");
    r_textf(x0 + 24, y0 + 64, 11, rgba(0.9f, 0.95f, 1, 0.95f), AL_LEFT, "BONUS: %s", reward_name(nd->reward));
    r_text(reward_desc(nd->reward), x0 + 24, y0 + 84, 9, rgba(0.8f, 0.85f, 1, 0.85f), AL_LEFT);
    if (nd->hazard) {
        r_textf(x0 + 24, y0 + 104, 11, C_ORANGE, AL_LEFT, "HAZARD: %s", hazard_name(nd->hazard));
        r_text(hazard_desc(nd->hazard), x0 + 24 + text_width("HAZARD: ", 11) + text_width(hazard_name(nd->hazard), 11) + 16, y0 + 106, 9,
               col_a(C_ORANGE, 0.85f), AL_LEFT);
    } else r_text("NO HAZARD", x0 + 24, y0 + 104, 11, col_a(C_GREEN, 0.7f), AL_LEFT);
    r_pop();
    r_textf(x1, y0 + 14, 12, col_white(zc, 0.3f * (0.5f + 0.5f * sinf(g_time * 6))), AL_RIGHT, "%s  FLY TO THIS SECTOR", g_gamepad_connected ? "A" : "ENTER");
    r_pop();
    r_push(v2(0, 0), v2(0, 0), 1, 1, intro_k(0.6f) * (1 - out));
    hint(g_gamepad_connected ? "D-PAD PICK A SECTOR   A LAUNCH   B BACK TO THE HANGAR" : "ARROWS / MOUSE PICK A SECTOR   ENTER LAUNCH   ESC BACK TO THE HANGAR");
    r_pop();
}

/* ------------------------------------------------------------ briefing */
static void briefing_update(float dt) {
    bg_update(dt);
    holo_update(dt);
    if (ui.left || ui.right) {
        holo_step(ui.right ? 1 : -1);
        snd_play(SND_MENU_MOVE, 0.5f, 1.0f);
    }
    const char *txt = run_level_def()->briefing;
    if (ui.confirm || ui.click) {
        if (!typewriter_done(txt, state_t - 0.6f, 70)) state_t = 1000;
        else goto_state(S_PLAY);
    }
    if (ui.back) goto_state(retry_level ? S_PLAY : S_MAP);
}

static void briefing_draw(void) {
    bg_draw(0.4f);
    const LevelDef *d = run_level_def();
    float VW = g_virt_w;
    float cx = VW * 0.5f;
    slide_in(0, 0, -12);
    r_textf(cx, 40, 13, col_a(d->accent, 0.8f), AL_CENTER, retry_level ? "SECTOR %d OF %d  -  SECOND ATTEMPT" : "SECTOR %d OF %d", R.layer + 1, R.nlayers);
    r_text_reveal(d->name, cx, 64, 40, d->wall, AL_CENTER, clampf(state_t / 0.6f, 0, 1), true);
    r_text(d->subtitle, cx, 122, 12, col_a(d->accent, 0.8f), AL_CENTER);
    r_pop();
    /* the holo-sim acts out the objectives beside the written orders */
    const float TW = 660, GAP = 20, Y0 = 160, PH = 350;
    float hw = clampf(VW - 60 - TW - GAP, 300, 450);
    float x0 = cx - (hw + GAP + TW) * 0.5f, px = x0 + hw + GAP;
    power_on(x0 + hw * 0.5f, Y0 + PH * 0.5f, 0.1f);
    holo_draw(x0, Y0, hw, PH);
    r_pop();
    power_on(px + TW * 0.5f, Y0 + PH * 0.5f, 0.18f);
    r_panel(px, Y0, TW, PH, d->wall, 0.7f);
    const float TS = 12.5f, TX = px + 27, TY = Y0 + 26;
    int focus = holo_focus_line(d->briefing), typed = (int)((state_t - 0.6f) * 70);
    if (focus >= 0) {
        int at = 0;
        for (int l = 0; l < focus && d->briefing[at]; at++)
            if (d->briefing[at] == '\n') l++;
        if (typed > at) {
            float ly = TY + focus * TS * 1.6f;
            float pulse = 0.5f + 0.5f * sinf(g_time * 4);
            r_add_rect(TX - 14, ly - 5, TW - 26, TS + 10, col_a(d->accent, 0.07f + 0.03f * pulse));
            V2 tri[3] = {v2(TX - 22, ly), v2(TX - 15, ly + TS * 0.5f), v2(TX - 22, ly + TS)};
            r_polyline(tri, 3, true, 2.2f, col_a(d->accent, 0.9f));
        }
    }
    typewriter(d->briefing, TX, TY, TS, rgba(0.88f, 0.93f, 1, 1), state_t - 0.6f, 70);
    int hz = run_hazard();
    if (hz) r_textf(px + TW * 0.5f, Y0 + PH - 26, 9, col_a(C_ORANGE, 0.9f), AL_CENTER, "%s: %s", hazard_name(hz), hazard_desc(hz));
    r_pop();
    slide_in(0.35f, 0, 10);
    r_text("KNOWN THREATS", cx, 526, 12, col_a(d->accent, 0.8f), AL_CENTER);
    r_pop();
    int n = d->nthreats;
    for (int i = 0; i < n; i++) {
        /* the threats pop onto their turntables one by one */
        float x = cx + (i - (n - 1) * 0.5f) * 210;
        float k = ease_out_back(clampf((state_t - 0.4f - i * 0.08f) / 0.4f, 0, 1));
        if (k <= 0.01f) continue;
        r_push(v2(0, 0), v2(x, 580), k, k, clampf(k, 0, 1));
        holo_threat(d->threats[i], v2(x, 580), d->threats[i] == RB_BOSS ? 30 : 23, g_time * 0.8f + i * 1.3f);
        r_text(RDEF[d->threats[i]].name, x, 622, 10, col_a(RDEF[d->threats[i]].col, 0.9f), AL_CENTER);
        r_pop();
    }
    if (fmodf(g_time * 2, 1) < 0.7f)
        hint(typewriter_done(d->briefing, state_t - 0.6f, 70) ? "PRESS FIRE TO LAUNCH   LEFT / RIGHT HOLO-SIM   ESC BACK TO THE CHART" : "PRESS FIRE TO SKIP");
}

/* ------------------------------------------------------------ play */
static void bot_input(void) {
    Player *p = &W.pl;
    int best = -1;
    float bd = 700;
    for (int i = 0; i < MAX_ROBOTS; i++) {
        Robot *r = &W.rob[i];
        if (!r->active) continue;
        float d = v2dist(r->pos, p->pos);
        if (d < bd && los(p->pos, r->pos)) { bd = d; best = i; }
    }
    static float wander_t = 0;
    static V2 wander = {1, 0};
    wander_t -= 1.0f / 60;
    if (wander_t <= 0) { wander_t = 2; wander = v2fromang(frand() * TAU); }
    g_in.fire1 = false;
    g_in.fire2 = false;
    if (opt_arena && !G.reactor_dead && (W.reactor.exists || W.boss_idx >= 0)) {
        /* the arena test: circle the core (or the Overseer) and shoot it, pylons first */
        V2 c = W.reactor.exists ? W.reactor.pos : W.rob[W.boss_idx].pos;
        V2 target = c;
        float pd = 1e9f;
        for (int i = 0; i < MAX_ROBOTS; i++)
            if (W.rob[i].active && W.rob[i].type == RB_PYLON && v2dist(W.rob[i].pos, p->pos) < pd) { pd = v2dist(W.rob[i].pos, p->pos); target = W.rob[i].pos; }
        V2 to = v2sub(c, p->pos);
        float d = v2len(to);
        V2 n = v2norm(to);
        g_in.move = v2add(v2perp(n), v2scale(n, clampf((d - 380) / 200, -1, 1)));
        /* the bolts inherit some of the ship's speed: aim off to cancel it */
        g_in.aim_world = v2mad(target, p->vel, -0.35f * v2dist(target, p->pos) / 1000.0f);
        g_in.fire1 = los(p->pos, target);
        g_in.fire2 = frand() < 0.004f;
        g_in.burner = frand() < 0.3f;
        g_in.mouse_active = true;
        g_in.use_stick = false;
        return;
    }
    if (best >= 0) {
        Robot *r = &W.rob[best];
        g_in.aim_world = r->pos;
        g_in.fire1 = true;
        g_in.fire2 = frand() < 0.01f;
        V2 d = v2norm(v2sub(r->pos, p->pos));
        g_in.move = bd > 260 ? d : v2perp(d);
        g_in.burner = bd > 260 && p->burner > 0.5f;
        if (p->cargo > 120 && bd < 260 && frand() < 0.01f) g_in.jettison = true;
    } else {
        /* no robot in sight: head for the reactor, or the exit once it blows */
        V2 fd = G.reactor_dead ? flow_dir(L.exitflow, p->pos, true) : v2(0, 0);
        if (!G.reactor_dead && W.reactor.exists && !W.reactor.dead && los(p->pos, W.reactor.pos)) {
            g_in.aim_world = W.reactor.pos;
            g_in.fire1 = true;
        }
        g_in.move = v2len2(fd) > 0.01f ? fd : wander;
        if (v2len2(fd) < 0.01f) g_in.aim_world = v2mad(p->pos, wander, 200);
        g_in.burner = v2len2(fd) > 0.01f;
    }
    if (p->swap_pu >= 0 && frand() < 0.02f) g_in.swap = true;
    g_in.mouse_active = true;
    g_in.use_stick = false;
}

static bool mouse_drag = false; /* the left button is held: it turns the automap */

static void read_play_input(void) {
    const bool *ks = SDL_GetKeyboardState(NULL);
    V2 mv = v2(0, 0);
    if (ks[SDL_SCANCODE_W] || ks[SDL_SCANCODE_UP]) mv.y -= 1;
    if (ks[SDL_SCANCODE_S] || ks[SDL_SCANCODE_DOWN]) mv.y += 1;
    if (ks[SDL_SCANCODE_A] || ks[SDL_SCANCODE_LEFT]) mv.x -= 1;
    if (ks[SDL_SCANCODE_D] || ks[SDL_SCANCODE_RIGHT]) mv.x += 1;
    float mx, my;
    SDL_MouseButtonFlags mb = SDL_GetMouseState(&mx, &my);
    bool f1 = (mb & SDL_BUTTON_LMASK) != 0 || ks[SDL_SCANCODE_LCTRL] || ks[SDL_SCANCODE_RCTRL];
    bool f2 = (mb & SDL_BUTTON_RMASK) != 0 || ks[SDL_SCANCODE_SPACE];
    bool burn = ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT];
    g_in.turn = (float)(ks[SDL_SCANCODE_E] - ks[SDL_SCANCODE_Q]);
    mouse_drag = (mb & SDL_BUTTON_LMASK) != 0;
    V2 aim = v2(0, 0);
    if (pad) {
        float lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
        float ly = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
        float rx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f;
        float ry = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f;
        V2 l = v2(lx, ly), r = v2(rx, ry);
        if (v2len(l) > 0.18f) { mv = v2add(mv, l); g_in.use_stick = true; }
        if (v2len(r) > 0.25f) { aim = r; g_in.use_stick = true; }
        if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 9000) { f1 = true; g_in.use_stick = true; }
        if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 9000) { f2 = true; g_in.use_stick = true; }
        if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH)) burn = true;
    }
    static bool f2_was = false;
    if (f2 && !f2_was) g_in.fire2_pressed = true;
    f2_was = f2;
    g_in.move = mv;
    g_in.stick_aim = aim;
    g_in.fire1 = f1;
    g_in.fire2 = f2;
    g_in.burner = burn;
    g_in.aim_world = v2w(ui.mouse);
    if (opt_bot) bot_input();
}

static void play_update(float dt) {
    read_play_input();
    if (G.result == GR_NONE && !G.automap) R.time += dt;
    if (G.automap) automap_update(dt, ui.mouse, mouse_drag);
    game_update(dt); /* does nothing once a result is set; game_start_level clears it */
    if (G.result == GR_LEVEL_DONE) goto_state(S_TALLY);
    else if (G.result == GR_ESCAPE_FAIL) {
        if (!retry_level) {
            /* the failed attempt's points don't count */
            G.score = G.level_score_start;
            G.next_life = (G.score / 50000 + 1) * 50000;
        }
        retry_level = true;
        goto_state(S_BRIEFING);
    } else if (G.result == GR_GAME_OVER) goto_state(S_GAMEOVER);
}

static void play_draw(void) {
    if (G.automap) {
        automap_draw();
        return;
    }
    game_draw();
    game_hud_draw();
}

static void pause_update(float dt) {
    (void)dt;
    int r = menu_update(4, 300, 50, 22, menu_widths);
    switch (r) {
    case 0: goto_state(S_PLAY); break;
    case 1: return_state = S_PAUSE; goto_state(S_HELP); break;
    case 2: return_state = S_PAUSE; goto_state(S_OPTIONS); break;
    case 3: game_shutdown_level(); goto_state(S_TITLE); break;
    }
    if (ui.back && state_t > 0.1f) goto_state(S_PLAY);
}

static void pause_draw(void) {
    game_draw();
    game_hud_draw();
    r_fill_rect(0, 0, g_virt_w, VIRT_H, rgba(0, 0.01f, 0.04f, 0.72f));
    slide_in(0, 0, -14);
    r_text_glow("PAUSED", g_virt_w * 0.5f, 170, 48, rgba(0.4f, 0.9f, 1, 1), AL_CENTER);
    r_pop();
    char sd[24];
    seed_format(R.seed, sd, sizeof(sd));
    r_textf(g_virt_w * 0.5f, 236, 10, rgba(0.6f, 0.7f, 0.9f, 0.8f), AL_CENTER, "SECTOR %d OF %d  -  SEED %s  -  HEAT %d", R.layer + 1, R.nlayers, sd, run_heat());
    static const char *items[4] = {"RESUME", "HOW TO PLAY", "OPTIONS", "ABORT MISSION"};
    menu_draw(items, 4, 300, 50, 22, menu_widths);
    if (menu_sel == 3)
        r_text("PROGRESS IN THIS MINE IS LOST - CONTINUE RESUMES THE RUN BEFORE IT", g_virt_w * 0.5f, 520, 11, rgba(0.7f, 0.8f, 1, 0.8f), AL_CENTER);
    hint("ESC RESUME");
}

/* ------------------------------------------------------------ tally */
#define TALLY_LINES 10
static void tally_update(float dt) {
    tally_t += dt;
    int want = (int)(tally_t / 0.3f);
    if (want > TALLY_LINES) want = TALLY_LINES;
    if (want > tally_line) {
        tally_line = want;
        bool close = tally_line == 4 && G.escape_margin < 5;
        snd_play(tally_line == TALLY_LINES ? SND_POWERUP : close ? SND_EXTRALIFE : SND_TALLY, 0.6f, close ? 1.3f : 1.0f);
    }
    if (ui.confirm || ui.click) {
        if (tally_line < TALLY_LINES) tally_t = 100;
        else goto_state(tally_final ? S_VICTORY : S_HANGAR);
    }
}

static void tally_draw(void) {
    float cx = g_virt_w * 0.5f;
    const LevelDef *d = run_level_def();
    grid_draw(col_mul(d->grid, 0.6f), 0.6f);
    power_on(cx, 370, 0);
    r_panel(cx - 330, 80, 660, 580, d->wall, 0.8f);
    r_textf(cx, 100, 13, col_a(d->accent, 0.8f), AL_CENTER, "SECTOR %d OF %d", G.level + 1, R.nlayers);
    r_text_glow(d->name, cx, 124, 30, d->wall, AL_CENTER);
    r_text("MINE DESTROYED", cx, 170, 16, C_GREEN, AL_CENTER);
    char v[TALLY_LINES][48];
    const char *labels[TALLY_LINES] = {"ROBOTS DESTROYED", "HOSTAGES RESCUED", "MISSION TIME", "ESCAPE MARGIN", "SHIELD AND ENERGY BONUS",
                                       "HOSTAGE BONUS", "FULL RESCUE BONUS", "SKILL AND HEAT BONUS", "TOTAL SCORE", "CARGO BANKED"};
    /* line i shows up as tally_t passes (i + 1) * 0.3 */
    float kk[TALLY_LINES];
    for (int i = 0; i < TALLY_LINES; i++) kk[i] = ease_out_cubic((tally_t - (i + 1) * 0.3f) / 0.5f);
#define COUNT(i, x) ((int)lroundf((x) * kk[i]))
    snprintf(v[0], 48, "%d", COUNT(0, G.robots_killed));
    snprintf(v[1], 48, "%d / %d", COUNT(1, G.hostages_saved), G.hostages_total);
    snprintf(v[2], 48, "%d:%02d", (int)G.level_time / 60, (int)G.level_time % 60);
    snprintf(v[3], 48, "%.1f S", G.escape_margin);
    snprintf(v[4], 48, "%d", COUNT(4, G.bonus_shield));
    snprintf(v[5], 48, "%d", COUNT(5, G.bonus_hostage));
    snprintf(v[6], 48, "%d", COUNT(6, G.bonus_full));
    snprintf(v[7], 48, "%d", COUNT(7, G.bonus_skill));
    /* the total rolls up from the score before this mine's bonuses */
    int before = G.score - G.bonus_shield - G.bonus_hostage - G.bonus_full - G.bonus_skill;
    snprintf(v[8], 48, "%d", before + COUNT(8, G.score - before));
    if (G.cargo_bonus > 0) snprintf(v[9], 48, "%d +%d", COUNT(9, G.cargo_banked), COUNT(9, G.cargo_bonus));
    else snprintf(v[9], 48, "%d", COUNT(9, G.cargo_banked));
#undef COUNT
    for (int i = 0; i < tally_line && i < TALLY_LINES; i++) {
        float y = 224 + i * 38 + (i >= 8 ? 14 : 0) + (i == 9 ? 4 : 0);
        r_push(v2(-18 * (1 - kk[i]), 0), v2(0, 0), 1, 1, clampf(kk[i] * 1.5f, 0, 1));
        Col c = i == 8 ? C_WHITE : rgba(0.75f, 0.85f, 1, 1);
        float size = i == 8 ? 20 : 15;
        if (i == 8) r_line(v2(cx - 280, y - 13), v2(cx + 280, y - 13), 3, col_a(d->wall, 0.6f));
        if (i == 9) c = powerup_color(PU_SALVAGE);
        if (i == 3 && G.escape_margin < 5) {
            /* the moment worth a screenshot */
            c = fmodf(g_time * 4, 1) < 0.6f ? C_YELLOW : C_WHITE;
            size = 18;
            r_text_glow(G.escape_margin < 1 ? "BY A HAIR!" : "CLOSE CALL!", cx, y - 1, 13, C_ORANGE, AL_CENTER);
        }
        r_text(labels[i], cx - 280, y, i == 3 ? 15 : size, col_a(c, 0.85f), AL_LEFT);
        if (i == 6 && G.bonus_full > 0) c = C_GREEN;
        if (i == 9) draw_salvage(v[i], cx + 280, y, size, c, AL_RIGHT);
        else r_text(v[i], cx + 280, y, size, c, AL_RIGHT);
        r_pop();
    }
    if (tally_line >= TALLY_LINES) {
        if (G.cargo_bonus > 0) r_text("ADRENALINE BANK: +50% FOR A LATE ESCAPE", cx, 606, 9, col_a(powerup_color(PU_SALVAGE), 0.9f), AL_CENTER);
        for (int k = tally_unlock_from, row = 0; k < R.nunlocked && row < 2; k++, row++) {
            const ChallengeDef *ch = &CHALLENGES[R.unlocked[k]];
            r_textf(cx, 624 + row * 15, 8, col_a(C_YELLOW, 0.9f), AL_CENTER, "%s: %s", ch->name, ch->unlocks);
        }
        if (fmodf(g_time * 2, 1) < 0.7f) hint(tally_final ? "PRESS FIRE TO CONTINUE" : "PRESS FIRE TO REFIT YOUR SHIP IN THE HANGAR");
    }
    r_pop();
}

/* ------------------------------------------------------------ end screens */
static void after_game(void) {
    new_score_rank = score_rank(G.score);
    if (new_score_rank >= 0) goto_state(S_NAME);
    else goto_state(S_SCORES);
}

static void gameover_update(float dt) {
    bg_update(dt);
    if ((ui.confirm || ui.click || ui.back) && state_t > 1.0f) goto_state(S_SUMMARY);
}

static void gameover_draw(void) {
    bg_draw(0.5f);
    float cx = g_virt_w * 0.5f;
    float a = clampf(state_t, 0, 1);
    /* the title slams down from a little too big */
    float gs = lerpf(1.35f, 1, ease_out_cubic(state_t / 0.7f));
    r_push(v2(0, 0), v2(cx, 256), gs, gs, 1);
    r_text_glow("GAME OVER", cx, 220, 72, col_a(C_RED, a), AL_CENTER);
    r_pop();
    r_textf(cx, 340, 18, col_a(C_WHITE, a), AL_CENTER, "FINAL SCORE  %d", G.score);
    r_textf(cx, 380, 13, col_a(rgba(0.7f, 0.8f, 1, 1), a), AL_CENTER, "LOST IN SECTOR %d - %s", R.layer + 1, run_level_def()->name);
    if (R.killed_by[0]) r_textf(cx, 410, 11, col_a(rgba(1, 0.6f, 0.55f, 1), a), AL_CENTER, "KILLED BY %s", R.killed_by);
    if (state_t > 1 && fmodf(g_time * 2, 1) < 0.7f) hint("PRESS FIRE FOR THE RUN REPORT");
}

/* the ending plays as a cinematic, then settles on the final card */
static void victory_update(float dt) {
    cine_ending_update(state_t);
    if (ui.confirm || ui.click) {
        if (state_t < ENDING_LEN) state_t = ENDING_LEN;
        else if (state_t > ENDING_LEN + 1.2f) goto_state(S_SUMMARY);
    }
}

static void victory_draw(void) {
    float cx = g_virt_w * 0.5f;
    if (state_t < ENDING_LEN) {
        cine_ending_draw(state_t);
        if (state_t > 1.0f) r_text("PRESS FIRE TO SKIP", cx, 48, 9, rgba(0.45f, 0.65f, 1, 0.5f), AL_CENTER);
        return;
    }
    float t = state_t - ENDING_LEN;
    cine_backdrop(t, 0.3f * clampf(t / 1.5f, 0, 1));
    Col gc = rgba(0.4f, 1, 0.6f, 1);
    r_text_reveal("MISSION ACCOMPLISHED", cx, 170, 46, gc, AL_CENTER, clampf((t - 0.2f) / 1.4f, 0, 1), true);
    float a = clampf((t - 1.3f) / 0.6f, 0, 1);
    float w = text_width("MISSION ACCOMPLISHED", 46) * 0.5f;
    float k = clampf((t - 1.2f) / 0.5f, 0, 1);
    if (k > 0) r_line(v2(cx - w * k, 250), v2(cx + w * k, 250), 3, col_a(gc, 0.6f));
    r_textf(cx, 300, 26, col_a(C_WHITE, a), AL_CENTER, "FINAL SCORE  %d", G.score);
    r_textf(cx, 350, 12, col_a(rgba(0.7f, 0.85f, 1, 0.9f), a), AL_CENTER, "%s  -  HEAT %d   -   SHIPS REMAINING: %d", DIFF_NAMES[R.base_diff], run_heat(),
            G.lives);
    r_textf(cx, 380, 11, col_a(rgba(0.6f, 0.75f, 1, 0.8f), a), AL_CENTER, "%s  -  %d SECTORS  -  %d:%02d", run_ship()->name, R.sectors_done,
            (int)R.time / 60, (int)R.time % 60);
    r_text("THANK YOU FOR PLAYING BALLAST", cx, 430, 12, col_a(C_MAGENTA, 0.8f * a), AL_CENTER);
    if (t > 1.2f && fmodf(g_time * 2, 1) < 0.7f) hint("PRESS FIRE FOR THE RUN REPORT");
}

/* ------------------------------------------------------------ run report */
static void summary_update(float dt) {
    bg_update_ex(dt, false);
    /* once the fade is over, the report is saved as an image to share */
    if (!summary_saved && state_t > 1.0f && fade <= 0.001f) {
        summary_saved = true;
        if (campaign_io) {
            char name[96];
#if SDL_VERSION_ATLEAST(3, 4, 0)
            const char *ext = "png";
#else
            const char *ext = "bmp";
#endif
            snprintf(name, sizeof(name), "ballast_run_%08X_%u.%s", R.seed, (unsigned)(SDL_GetTicks() / 1000), ext);
            path_join(summary_path, sizeof(summary_path), name);
            g_screenshot_path = summary_path;
        }
    }
    if ((ui.confirm || ui.click || ui.back) && state_t > 1.2f) after_game();
}

static void summary_stat(float x, float y, const char *label, const char *value, Col c) {
    r_text(label, x, y, 9, col_a(c, 0.7f), AL_LEFT);
    r_text(value, x, y + 14, 16, c, AL_LEFT);
}

static void summary_draw(void) {
    bg_draw(0.3f);
    float VW = g_virt_w, cx = VW * 0.5f;
    float pw = minf(1180, VW - 40), x0 = cx - pw * 0.5f, y0 = 40, ph = 600;
    const ShipDef *sd = run_ship();
    Col rc = run_won ? C_GREEN : C_RED;
    power_on(cx, y0 + ph * 0.5f, 0);
    r_panel(x0, y0, pw, ph, rc, 0.85f);
    r_text_glow("RUN REPORT", x0 + 30, y0 + 22, 30, rgba(0.4f, 0.9f, 1, 1), AL_LEFT);
    char buf[128];
    if (run_won) snprintf(buf, sizeof(buf), "THE OVERSEER IS DESTROYED");
    else snprintf(buf, sizeof(buf), "LOST IN SECTOR %d OF %d - %s", R.layer + 1, R.nlayers, run_level_def()->name);
    r_text_glow(buf, x0 + 30, y0 + 66, 15, rc, AL_LEFT);
    char sd_txt[24];
    seed_format(R.seed, sd_txt, sizeof(sd_txt));
    r_textf(x0 + pw - 30, y0 + 24, 12, col_a(C_WHITE, 0.85f), AL_RIGHT, "%sSEED %s", R.daily ? "DAILY  -  " : "", sd_txt);
    r_textf(x0 + pw - 30, y0 + 44, 10, col_a(C_ORANGE, 0.85f), AL_RIGHT, "%s  -  HEAT %d", DIFF_NAMES[R.base_diff], run_heat());
    r_textf(x0 + pw - 30, y0 + 64, 20, C_WHITE, AL_RIGHT, "%d", G.score);

    /* the numbers */
    float cy = y0 + 118;
    Col sc = rgba(0.8f, 0.9f, 1, 1);
    float colw = (pw - 60) / 4;
    char v[32];
    snprintf(v, sizeof(v), "%d / %d", R.sectors_done, R.nlayers);
    summary_stat(x0 + 30, cy, "SECTORS CLEARED", v, sc);
    snprintf(v, sizeof(v), "%d:%02d", (int)R.time / 60, (int)R.time % 60);
    summary_stat(x0 + 30 + colw, cy, "RUN TIME", v, sc);
    snprintf(v, sizeof(v), "%d", R.kills);
    summary_stat(x0 + 30 + colw * 2, cy, "ROBOTS DESTROYED", v, sc);
    snprintf(v, sizeof(v), "%d", R.rescued);
    summary_stat(x0 + 30 + colw * 3, cy, "MINERS RESCUED", v, sc);
    cy += 58;
    snprintf(v, sizeof(v), "%d", R.salvage_total);
    summary_stat(x0 + 30, cy, "SALVAGE BANKED", v, powerup_color(PU_SALVAGE));
    snprintf(v, sizeof(v), "%d", R.deaths);
    summary_stat(x0 + 30 + colw, cy, "SHIPS LOST", v, sc);
    if (R.best_escape >= 0) snprintf(v, sizeof(v), "%.1f S", R.best_escape);
    else snprintf(v, sizeof(v), "-");
    summary_stat(x0 + 30 + colw * 2, cy, "CLOSEST ESCAPE", v, R.best_escape >= 0 && R.best_escape < 3 ? C_YELLOW : sc);
    snprintf(v, sizeof(v), "X%d", R.best_chain > 0 ? R.best_chain : 1);
    summary_stat(x0 + 30 + colw * 3, cy, "BIGGEST CHAIN", v, R.best_chain >= 8 ? C_YELLOW : sc);
    cy += 58;
    snprintf(v, sizeof(v), "%d", R.best_haul);
    summary_stat(x0 + 30, cy, "HEAVIEST HAUL", v, powerup_color(PU_SALVAGE));
    snprintf(v, sizeof(v), "%d KILL%s", R.best_bomb, R.best_bomb == 1 ? "" : "S");
    summary_stat(x0 + 30 + colw, cy, "BEST CARGO BOMB", v, R.best_bomb >= 3 ? C_YELLOW : sc);
    summary_stat(x0 + 30 + colw * 2, cy, run_won ? "FINAL BLOW" : "KILLED BY", run_won ? "THE OVERSEER FELL" : R.killed_by[0] ? R.killed_by : "-",
                 run_won ? C_GREEN : rgba(1, 0.6f, 0.55f, 1));

    /* the build */
    cy += 76;
    r_line(v2(x0 + 30, cy - 14), v2(x0 + pw - 30, cy - 14), 2, col_a(rc, 0.35f));
    game_draw_ship_icon(v2(x0 + 70, cy + 44), -PI / 2, 18, sd->col);
    r_text(sd->name, x0 + 70, cy + 88, 12, sd->col, AL_CENTER);
    r_text("THE BUILD", x0 + 140, cy, 10, col_a(C_WHITE, 0.7f), AL_LEFT);
    int k = 0;
    for (int i = 0; i < MOD_COUNT; i++) {
        if (!R.mods[i]) continue;
        float bx = x0 + 160 + (k % 4) * ((pw - 200) / 4), by = cy + 38 + (k / 4) * 56;
        mod_draw_badge(i, v2(bx, by), 18, 1, true);
        r_text(MODS[i].name, bx + 28, by - 10, 10, mod_color(MODS[i].cat), AL_LEFT);
        r_textf(bx + 28, by + 6, 8, col_a(mod_color(MODS[i].cat), 0.7f), AL_LEFT, "%s%s", MOD_TAG_NAMES[MODS[i].tag], R.mods[i] > 1 ? (R.mods[i] > 2 ? "  -  RANK 3" : "  -  RANK 2") : "");
        k++;
    }
    if (!k) r_text("NO MODULES", x0 + 160, cy + 36, 12, col_a(C_GREY, 0.8f), AL_LEFT);

    /* unlocks */
    float uy = y0 + ph - 60;
    if (R.nunlocked > 0) {
        /* two columns of what this run unlocked, as many as fit */
        r_text("NEW UNLOCKS", x0 + 30, uy - 44, 9, col_a(C_YELLOW, 0.8f), AL_LEFT);
        for (int i = 0; i < R.nunlocked && i < 6; i++) {
            float ux = x0 + 30 + (i % 2) * (pw - 60) * 0.5f, yy = uy - 26 + (i / 2) * 16;
            const char *t = CHALLENGES[R.unlocked[i]].unlocks;
            float us = minf(9, ((pw - 60) * 0.5f - 20) / maxf(1, text_width(t, 1)));
            r_text(t, ux, yy, us, C_YELLOW, AL_LEFT);
        }
    }
    r_pop();
    if (summary_path[0] && summary_saved && state_t > 1.3f)
        r_textf(cx, y0 + ph + 14, 9, col_a(C_WHITE, 0.6f), AL_CENTER, "REPORT SAVED TO %s", summary_path);
    if (state_t > 1.2f && fmodf(g_time * 2, 1) < 0.7f) hint("PRESS FIRE TO CONTINUE");
}

/* ------------------------------------------------------------ names and scores */
static void name_update(float dt) {
    bg_update(dt);
    if (ui.confirm && state_t > 0.3f) {
        score_insert(new_score_rank, name_buf);
        SDL_StopTextInput(g_window);
        goto_state(S_SCORES);
    }
}

static void name_draw(void) {
    bg_draw(0.5f);
    float cx = g_virt_w * 0.5f;
    float nk = ease_out_back(clampf(state_t / 0.5f, 0, 1));
    r_push(v2(0, 0), v2(cx, 202), nk, nk, clampf(nk, 0, 1));
    r_text_glow("NEW HIGH SCORE!", cx, 180, 44, C_YELLOW, AL_CENTER);
    r_pop();
    r_textf(cx, 260, 18, C_WHITE, AL_CENTER, "%d  -  RANK %d", G.score, new_score_rank + 1);
    r_text("ENTER YOUR NAME", cx, 330, 14, rgba(0.7f, 0.85f, 1, 0.9f), AL_CENTER);
    char buf[32];
    snprintf(buf, sizeof(buf), "%s%s", name_buf, fmodf(g_time * 2, 1) < 0.5f ? "_" : " ");
    power_on(cx, 405, 0.2f);
    r_panel(cx - 220, 370, 440, 70, C_CYAN, 0.7f);
    r_text_glow(buf, cx, 390, 30, C_CYAN, AL_CENTER);
    r_pop();
    hint("TYPE YOUR NAME AND PRESS ENTER");
}

static void scores_update(float dt) {
    bg_update(dt);
    if (ui.confirm || ui.back || ui.click) goto_state(S_TITLE);
}

static void scores_draw(void) {
    bg_draw(0.6f);
    float cx = g_virt_w * 0.5f;
    slide_in(0, 0, -14);
    r_text_glow("HIGH SCORES", cx, 70, 40, rgba(0.4f, 0.9f, 1, 1), AL_CENTER);
    r_pop();
    power_on(cx, 380, 0.08f);
    r_panel(cx - 430, 140, 860, 480, rgba(0.3f, 0.8f, 1, 1), 0.7f);
    Col hc = rgba(0.6f, 0.7f, 0.9f, 0.8f);
    r_text("RANK", cx - 390, 165, 11, hc, AL_LEFT);
    r_text("PILOT", cx - 300, 165, 11, hc, AL_LEFT);
    r_text("SHIP", cx - 20, 165, 11, hc, AL_CENTER);
    r_text("SECTOR", cx + 90, 165, 11, hc, AL_CENTER);
    r_text("SKILL", cx + 200, 165, 11, hc, AL_CENTER);
    r_text("SCORE", cx + 390, 165, 11, hc, AL_RIGHT);
    for (int i = 0; i < nscores; i++) {
        float y = 205 + i * 40;
        bool hi = i == new_score_rank;
        slide_in(0.25f + i * 0.04f, -30, 0);
        Col c = hi ? (fmodf(g_time * 3, 1) < 0.6f ? C_YELLOW : C_WHITE) : (i == 0 ? rgba(1, 0.85f, 0.4f, 1) : rgba(0.8f, 0.9f, 1, 1));
        r_textf(cx - 390, y, 15, c, AL_LEFT, "%d", i + 1);
        r_text(scores[i].name, cx - 300, y, 15, c, AL_LEFT);
        r_text(SHIPS[scores[i].ship].name, cx - 20, y + 2, 10, col_a(c, 0.8f), AL_CENTER);
        r_textf(cx + 90, y, 15, c, AL_CENTER, "%d%s", scores[i].level, scores[i].daily ? " D" : "");
        if (scores[i].heat > 0) r_textf(cx + 200, y + 2, 10, col_a(c, 0.8f), AL_CENTER, "%s +%d", DIFF_NAMES[scores[i].diff], scores[i].heat);
        else r_text(DIFF_NAMES[scores[i].diff], cx + 200, y + 2, 10, col_a(c, 0.8f), AL_CENTER);
        r_textf(cx + 390, y, 15, c, AL_RIGHT, "%d", scores[i].score);
        r_pop();
    }
    r_pop();
    hint("D = DAILY CHALLENGE   PRESS FIRE TO RETURN");
}

/* ------------------------------------------------------------ pilot record */
static void record_update(float dt) {
    bg_update(dt);
    if (ui.confirm || ui.back || ui.click) goto_state(S_TITLE);
}

static void record_draw(void) {
    bg_draw(0.4f);
    float VW = g_virt_w, cx = VW * 0.5f;
    slide_in(0, 0, -14);
    r_text_glow("PILOT RECORD", cx, 30, 36, rgba(0.4f, 0.9f, 1, 1), AL_CENTER);
    int done = 0;
    for (int i = 0; i < CH_COUNT; i++) done += profile_done(i);
    r_textf(cx, 80, 11, rgba(0.6f, 0.7f, 0.9f, 0.85f), AL_CENTER,
            "RUNS %d   -   WINS %d   -   BEST SECTOR %d   -   BEST HEAT WON %s   -   BEST SCORE %d   -   SALVAGE BANKED %d", g_prof.runs, g_prof.wins,
            g_prof.best_sector, g_prof.best_heat >= 0 ? (char[8]){(char)('0' + clampi(g_prof.best_heat, 0, 9)), 0} : "-", g_prof.best_score,
            g_prof.lifetime_salvage);
    r_pop();
    float pw = minf(1180, VW - 40), x0 = cx - pw * 0.5f;
    power_on(cx, 318, 0.08f);
    r_panel(x0, 108, pw, 420, rgba(0.3f, 0.8f, 1, 1), 0.72f);
    r_textf(x0 + 24, 124, 12, C_YELLOW, AL_LEFT, "CHALLENGES  %d / %d", done, CH_COUNT);
    for (int i = 0; i < CH_COUNT; i++) {
        float x = x0 + 24 + (i % 2) * (pw * 0.5f), y = 156 + (i / 2) * 60;
        bool ok = profile_done(i);
        Col c = ok ? C_YELLOW : rgba(0.6f, 0.65f, 0.8f, 0.8f);
        slide_in(0.25f + i * 0.035f, (i % 2 ? 24 : -24), 0);
        r_frame(x, y + 2, 16, 16, 2.5f, c);
        if (ok) r_polyline((V2[]){v2(x + 3, y + 10), v2(x + 7, y + 15), v2(x + 14, y + 4)}, 3, false, 3, C_YELLOW);
        r_text(CHALLENGES[i].name, x + 28, y, 12, ok ? C_WHITE : c, AL_LEFT);
        r_text(CHALLENGES[i].desc, x + 28, y + 18, 9, col_a(c, 0.85f), AL_LEFT);
        r_textf(x + 28, y + 32, 8, col_a(ok ? C_GREEN : C_ORANGE, 0.8f), AL_LEFT, "%s %s", ok ? "UNLOCKED" : "UNLOCKS", CHALLENGES[i].unlocks);
        r_pop();
    }
    r_pop();
    /* the hangar */
    power_on(cx, 601, 0.2f);
    r_panel(x0, 542, pw, 118, rgba(0.3f, 0.8f, 1, 1), 0.72f);
    for (int s = 0; s < SHIP_COUNT; s++) {
        float x = x0 + pw * (s + 0.5f) / SHIP_COUNT;
        bool ok = ship_unlocked(s);
        Col c = ok ? SHIPS[s].col : rgba(0.4f, 0.45f, 0.55f, 0.6f);
        /* the ships you fly turn on holo turntables, the locked ones stay flat and grey */
        if (ok) holo_ship(v2(x, 576), 25, g_time * 0.7f + s * 1.6f, c, intro_k(0.3f + s * 0.08f));
        else game_draw_ship_icon(v2(x, 580), -PI / 2, 14, c);
        r_text(SHIPS[s].name, x, 606, 12, c, AL_CENTER);
        if (ok) r_text(SHIPS[s].role, x, 626, 8, col_a(c, 0.7f), AL_CENTER);
        else r_textf(x, 626, 8, col_a(C_ORANGE, 0.8f), AL_CENTER, "LOCKED - %s", CHALLENGES[ship_challenge(s)].name);
    }
    r_pop();
    int mods = 0;
    for (int i = 0; i < MOD_COUNT; i++) mods += mod_unlocked(i);
    r_textf(cx, 668, 9, rgba(0.6f, 0.7f, 0.9f, 0.8f), AL_CENTER, "%d OF %d MODULES IN THE DRAFT POOL   -   THREAT PROTOCOLS %s", mods, MOD_COUNT,
            protocols_unlocked() ? "UNLOCKED" : "LOCKED");
    hint("PRESS FIRE TO RETURN");
}

/* ------------------------------------------------------------ options */
#define NOPT 8
static void option_change(int i, int dir) {
    switch (i) {
    case 0: g_cfg.music_vol = clampf(g_cfg.music_vol + dir * 0.1f, 0, 1); break;
    case 1: g_cfg.sfx_vol = clampf(g_cfg.sfx_vol + dir * 0.1f, 0, 1); snd_play(SND_PICKUP, 0.6f, 1.0f); break;
    case 2:
        g_cfg.fullscreen = !g_cfg.fullscreen;
        SDL_SetWindowFullscreen(g_window, g_cfg.fullscreen);
        break;
    case 3: g_cfg.shake = (g_cfg.shake + dir + 3) % 3; break;
    case 4: g_cfg.move_mode = 1 - g_cfg.move_mode; break;
    case 5: g_cfg.bloom = !g_cfg.bloom; break;
    case 6: g_cfg.autoswitch = !g_cfg.autoswitch; break;
    }
    audio_set_volumes(g_cfg.music_vol, g_cfg.sfx_vol);
    config_save();
}

static void options_items(char items[NOPT][64]) {
    static const char *shake[3] = {"OFF", "LOW", "FULL"};
    snprintf(items[0], 64, "MUSIC VOLUME   < %d >", (int)(g_cfg.music_vol * 10 + 0.5f));
    snprintf(items[1], 64, "SOUND VOLUME   < %d >", (int)(g_cfg.sfx_vol * 10 + 0.5f));
    snprintf(items[2], 64, "FULLSCREEN   %s", g_cfg.fullscreen ? "ON" : "OFF");
    snprintf(items[3], 64, "SCREEN SHAKE   %s", shake[g_cfg.shake]);
    snprintf(items[4], 64, "CONTROLS   %s", g_cfg.move_mode ? "SHIP RELATIVE" : "TWIN STICK");
    snprintf(items[5], 64, "BLOOM GLOW   %s", g_cfg.bloom ? "ON" : "OFF");
    snprintf(items[6], 64, "AUTO-SELECT NEW WEAPONS   %s", g_cfg.autoswitch ? "ON" : "OFF");
    snprintf(items[7], 64, "BACK");
}

static void options_update(float dt) {
    if (return_state != S_PAUSE) bg_update(dt);
    int r = menu_update(NOPT, 190, 50, 18, menu_widths);
    if (ui.left && menu_sel < NOPT - 1) { option_change(menu_sel, -1); snd_play(SND_MENU_MOVE, 0.5f, 0.9f); ui_press(ui_id(UA_ITEM, menu_sel)); }
    if (ui.right && menu_sel < NOPT - 1) { option_change(menu_sel, 1); snd_play(SND_MENU_MOVE, 0.5f, 1.1f); ui_press(ui_id(UA_ITEM, menu_sel)); }
    if (r >= 0) {
        if (r == NOPT - 1) goto_state(return_state);
        else option_change(r, 1);
    }
    if (ui.back) goto_state(return_state);
}

static void options_draw(void) {
    if (return_state == S_PAUSE) {
        game_draw();
        r_fill_rect(0, 0, g_virt_w, VIRT_H, rgba(0, 0.01f, 0.04f, 0.8f));
    } else bg_draw(0.6f);
    slide_in(0, 0, -14);
    r_text_glow("OPTIONS", g_virt_w * 0.5f, 90, 40, rgba(0.4f, 0.9f, 1, 1), AL_CENTER);
    r_pop();
    static char items[NOPT][64];
    options_items(items);
    const char *ptrs[NOPT];
    for (int i = 0; i < NOPT; i++) ptrs[i] = items[i];
    menu_draw(ptrs, NOPT, 190, 50, 18, menu_widths);
    static const char *desc[NOPT] = {
        "", "", "ALT+ENTER OR F11 ALSO TOGGLES FULLSCREEN", "",
        "TWIN STICK: WASD MOVES ON SCREEN AXES.  SHIP RELATIVE: W THRUSTS TOWARD THE CURSOR.",
        "DISABLE ON SLOW MACHINES", "SWITCH TO A BETTER WEAPON WHEN YOU PICK IT UP", ""};
    /* the line of help fades in for each item */
    static int desc_sel = -1;
    static float desc_t = 0;
    if (desc_sel != menu_sel) { desc_sel = menu_sel; desc_t = g_time; }
    float dk = smooth01((g_time - desc_t) / 0.25f);
    r_text(desc[menu_sel], g_virt_w * 0.5f, 600 + 6 * (1 - dk), 11, rgba(0.7f, 0.8f, 1, 0.8f * dk), AL_CENTER);
    hint("LEFT / RIGHT CHANGE   ESC BACK");
}

/* ------------------------------------------------------------ help */
#define HELP_PAGES 3
static int help_dir = 1; /* the way the pages last turned: the new one slides in from that side */

static void help_update(float dt) {
    if (return_state != S_PAUSE) bg_update(dt);
    if (ui.left || ui.up) {
        help_page = (help_page + HELP_PAGES - 1) % HELP_PAGES;
        help_dir = -1;
        ui_press(ui_id(UA_VALUE, 700));
        snd_play(SND_MENU_MOVE, 0.5f, 1.0f);
    }
    if (ui.right || ui.down || ui.confirm || ui.click) {
        if ((ui.confirm || ui.click) && help_page == HELP_PAGES - 1) { goto_state(return_state); return; }
        help_page = (help_page + 1) % HELP_PAGES;
        help_dir = 1;
        ui_press(ui_id(UA_VALUE, 700));
        snd_play(SND_MENU_MOVE, 0.5f, 1.0f);
    }
    if (ui.back) goto_state(return_state);
}

static void help_row(float x, float y, const char *key, const char *what, Col kc) {
    r_text(key, x, y, 12, kc, AL_RIGHT);
    r_text(what, x + 24, y, 12, rgba(0.85f, 0.9f, 1, 0.95f), AL_LEFT);
}

static void help_draw(void) {
    if (return_state == S_PAUSE) {
        game_draw();
        r_fill_rect(0, 0, g_virt_w, VIRT_H, rgba(0, 0.01f, 0.04f, 0.85f));
    } else bg_draw(0.4f);
    float cx = g_virt_w * 0.5f;
    static const char *titles[HELP_PAGES] = {"CONTROLS", "HOW TO PLAY", "POWERUPS"};
    float hp = ui_pressed(ui_id(UA_VALUE, 700));
    slide_in(0, 0, -14);
    r_push(v2(help_dir * 40 * hp, 0), v2(0, 0), 1, 1, 1 - 0.7f * hp);
    r_text_glow(titles[help_page], cx, 50, 36, rgba(0.4f, 0.9f, 1, 1), AL_CENTER);
    r_pop();
    r_textf(cx, 100, 11, rgba(0.6f, 0.7f, 0.9f, 0.8f), AL_CENTER, "PAGE %d OF %d", help_page + 1, HELP_PAGES);
    for (int i = 0; i < HELP_PAGES; i++) {
        float x = cx + text_width("PAGE 1 OF 3", 11) * 0.5f + 22 + i * 16, on = ui_ease(ui_id(UA_HOVER, 700 + i), i == help_page ? 1.0f : 0.0f, 12);
        r_circle(v2(x, 105.5f), 3 + 1.5f * on, 1.6f, col_a(C_CYAN, 0.35f + 0.65f * on), 12);
        if (on > 0.05f) r_glow(v2(x, 105.5f), 12, col_a(C_CYAN, 0.4f * on));
    }
    r_pop();
    power_on(cx, 390, 0.06f);
    r_panel(cx - 520, 125, 1040, 530, rgba(0.3f, 0.8f, 1, 1), 0.75f);
    /* the page's content slides in with it */
    r_push(v2(help_dir * 60 * hp, 0), v2(0, 0), 1, 1, 1 - 0.85f * hp);
    Col kc = C_CYAN, gc = C_MAGENTA;
    if (help_page == 0) {
        float x = cx - 240, y = 150;
        r_text("KEYBOARD + MOUSE", x - 110, y, 14, kc, AL_LEFT);
        y += 36;
        help_row(x, y, "WASD / ARROWS", "FLY", kc); y += 28;
        help_row(x, y, "MOUSE", "AIM", kc); y += 28;
        help_row(x, y, "LEFT BUTTON / CTRL", "FIRE PRIMARY WEAPON", kc); y += 28;
        help_row(x, y, "RIGHT BUTTON / SPACE", "FIRE SECONDARY WEAPON", kc); y += 28;
        help_row(x, y, "SHIFT", "AFTERBURNER", kc); y += 28;
        help_row(x, y, "1 / 2 / Q / WHEEL", "LASER OR SPECIAL WEAPON", kc); y += 28;
        help_row(x, y, "E", "SWAP FOR THE WEAPON UNDER YOU", kc); y += 28;
        help_row(x, y, "F", "JETTISON CARGO AS A BOMB", kc); y += 28;
        help_row(x, y, "TAB / M", "AUTOMAP", kc); y += 28;
        help_row(x, y, "ESC / P", "PAUSE", kc); y += 28;
        float x2 = cx + 290;
        y = 150;
        r_text("GAMEPAD", x2 - 110, y, 14, gc, AL_LEFT);
        y += 36;
        help_row(x2, y, "LEFT STICK", "FLY", gc); y += 28;
        help_row(x2, y, "RIGHT STICK", "AIM", gc); y += 28;
        help_row(x2, y, "RIGHT TRIGGER", "FIRE PRIMARY", gc); y += 28;
        help_row(x2, y, "LEFT TRIGGER", "FIRE SECONDARY", gc); y += 28;
        help_row(x2, y, "A / LB", "AFTERBURNER", gc); y += 28;
        help_row(x2, y, "Y", "LASER OR SPECIAL", gc); y += 28;
        help_row(x2, y, "RB", "SWAP WEAPON", gc); y += 28;
        help_row(x2, y, "X / B", "JETTISON CARGO", gc); y += 28;
        help_row(x2, y, "BACK", "AUTOMAP", gc); y += 28;
        help_row(x2, y, "START", "PAUSE", gc); y += 28;
    } else if (help_page == 1) {
        static const char *text =
            "EVERY MINE HAS A REACTOR CORE. FIND THE COLOURED ACCESS KEYS, BREAK THE\n"
            "REACTOR, THEN FOLLOW THE GREEN ARROWS TO THE EXIT BEFORE THE MINE BLOWS.\n"
            "THE CORE FIGHTS IN PHASES OF BULLET PATTERNS AND ROTATING LASERS - HIDE\n"
            "BEHIND THE PILLARS. SHIELD PYLONS MUST FALL BEFORE IT CAN BE HURT AGAIN.\n"
            "BULLETS THAT BRUSH PAST THE SHIP (GRAZES) RECHARGE ITS ENERGY.\n"
            "WATCH FOR TRAPS: PLASMA VENTS, LASER GATES, SWEEPERS, GRAVITY WELLS, MINES.\n"
            "\n"
            "GREED HAS MASS: SALVAGE AND RESCUED MINERS RIDE IN YOUR HOLD AND MAKE THE\n"
            "SHIP HEAVIER - SLOWER TO GET GOING, LONGER TO STOP, HARDER WHEN IT RAMS.\n"
            "THE HOLD IS ONLY BANKED AT THE EXIT. DIE AND IT SPILLS WHERE YOU FELL.\n"
            "F DUMPS HALF OF IT AS A BOMB THAT GROWS WITH THE SALVAGE PACKED INSIDE.\n"
            "VAULTS OPEN WHEN THE REACTOR BLOWS: THE FURTHER FROM THE EXIT, THE RICHER.\n"
            "\n"
            "A RUN IS FIVE SECTORS AND THE OVERSEER. BETWEEN SECTORS THE HANGAR OFFERS\n"
            "ONE FREE MODULE FROM A DRAFT OF THREE, AND A SHOP FOR YOUR BANKED SALVAGE.\n"
            "MODULES COMBINE - WATCH THEIR TAGS. PICK THE NEXT SECTOR ON THE CHART:\n"
            "EVERY SECTOR PAYS A BONUS, AND HAZARDS PAY EXTRA SALVAGE.\n"
            "\n"
            "ONE SPECIAL WEAPON AND ONE KIND OF MISSILE FIT NEXT TO THE LASER. E SWAPS\n"
            "THE ONE IN YOUR SLOT FOR THE ONE UNDER THE SHIP. ENERGY POWERS EVERY GUN.\n"
            "KILL ROBOTS QUICKLY FOR A CHAIN MULTIPLIER UP TO X8. CHALLENGES IN THE\n"
            "PILOT RECORD UNLOCK NEW SHIPS, MODULES AND SECTOR TYPES.";
        r_text(text, cx - 480, 150, 12.5f, rgba(0.88f, 0.93f, 1, 1), AL_LEFT);
    } else {
        static const int types[21] = {PU_SHIELD, PU_ENERGY, PU_LASER, PU_QUAD, PU_VULCAN, PU_SPREAD, PU_PLASMA, PU_FUSION, PU_CONC, PU_HOMING, PU_PROX,
                                      PU_SMART, PU_MEGA, PU_CLOAK, PU_INVULN, PU_LIFE, PU_SALVAGE, PU_TREASURE, PU_CRATE, PU_KEY_BLUE, -1};
        static const char *desc[21] = {"+SHIELD", "+ENERGY", "STRONGER LASERS (MAX 4)", "FOUR LASER BOLTS", "RAPID FIRE, CHEAP ON ENERGY",
                                       "THREE-WAY ENERGY FAN", "FAST ENERGY BOLTS", "HOLD TO CHARGE, PIERCES", "DUMB-FIRE MISSILES", "SEEK THEIR TARGET",
                                       "DROPPED BEHIND YOU", "BURSTS INTO SEEKERS", "HUGE BLAST RADIUS", "30S INVISIBILITY", "30S INVULNERABLE",
                                       "ONE MORE SHIP", "CARGO: BANK IT AT THE EXIT", "A VAULT'S HOARD", "A FREE MODULE", "OPENS MATCHING DOORS", ""};
        for (int i = 0; i < 20; i++) {
            int col = i / 7, row = i % 7;
            float x = cx - 440 + col * 330, y = 160 + row * 66;
            draw_powerup_icon(types[i], v2(x, y + 8), 1.2f, g_time, 1);
            r_text(powerup_name(types[i]), x + 34, y - 2, 12, powerup_color(types[i]), AL_LEFT);
            r_text(desc[i], x + 34, y + 18, 10, rgba(0.8f, 0.85f, 1, 0.85f), AL_LEFT);
        }
    }
    r_pop();
    r_pop();
    hint("LEFT / RIGHT CHANGE PAGE   ESC BACK");
}

/* ------------------------------------------------------------ hangar */
/*
 * Between sectors: one free module from the draft, a shop for banked salvage
 * and the ship's module slots. A new module needs a free slot; with all slots
 * full the hangar asks which installed module to scrap for it.
 */
enum { HR_DRAFT, HR_SHOP, HR_SHIP, HR_ROWS };
enum { SHOP_MOD0, SHOP_MOD1, SHOP_REROLL, SHOP_REPAIR, SHOP_MISSILES, SHOP_SHIP, SHOP_COUNT };
static int hrow = HR_DRAFT, hcol = 0;
static int hrep_id = -1, hrep_src = -1; /* a module waiting for a slot, and where it came from */
static float hscrap_t = 0;              /* > 0 while waiting for the scrap confirmation */
static char hnote[112];
static float hnote_t = 0;
static Col hnote_col;
static float hflash = 0;
static int hdet_key = -1;               /* what the details panel shows, and since when: it re-reveals on a change */
static float hdet_t = 0;
#define SCRAP_REFUND 30
#define SKIP_SALVAGE 25

static void hangar_note(const char *text, Col c) {
    snprintf(hnote, sizeof(hnote), "%s", text);
    hnote_col = c;
    hnote_t = 2.8f;
}

/* something refused: the note, the buzz, and the item shakes its head */
static void hangar_deny(const char *text, Col c) {
    if (text) hangar_note(text, c);
    snd_play(SND_NOAMMO, 0.5f, 1.0f);
    ui_press(ui_id(UA_DENY, hrow * 16 + hcol));
}

static int ship_mods(int *ids) {
    int n = 0;
    for (int i = 0; i < MOD_COUNT; i++)
        if (R.mods[i] > 0) ids[n++] = i;
    return n;
}

static int hrow_len(int row) {
    switch (row) {
    case HR_DRAFT: return R.ndraft + 1;
    case HR_SHOP: return SHOP_COUNT;
    default: return mod_slots() + 1;
    }
}

static int shop_price(int item) {
    switch (item) {
    case SHOP_MOD0:
    case SHOP_MOD1: return R.shop[item] >= 0 ? run_module_price(R.shop[item]) : 0;
    case SHOP_REROLL: return run_price(20 + 15 * R.rerolls);
    case SHOP_REPAIR: return run_price(35 + 10 * R.repairs);
    case SHOP_MISSILES: return run_price(30);
    default: return run_price(150 + 75 * R.ships_bought);
    }
}

static SDL_FRect hitem_rect(int row, int col) {
    float VW = g_virt_w, cx = VW * 0.5f;
    SDL_FRect r = {0, 0, 0, 0};
    if (row == HR_DRAFT) {
        int n = R.ndraft;
        float gap = 22, sw = 120, w = minf(236, (VW - 80 - sw - n * gap) / maxf(1, (float)n));
        float total = n * w + n * gap + sw;
        float x = cx - total * 0.5f + col * (w + gap);
        r = (SDL_FRect){x, 108, col < n ? w : sw, 172};
    } else if (row == HR_SHOP) {
        float gap = 12, w = minf(176, (VW - 60 - (SHOP_COUNT - 1) * gap) / SHOP_COUNT);
        float total = SHOP_COUNT * w + (SHOP_COUNT - 1) * gap;
        r = (SDL_FRect){cx - total * 0.5f + col * (w + gap), 318, w, 86};
    } else {
        int n = mod_slots();
        if (col < n) r = (SDL_FRect){230 + col * 66 - 27, 452, 54, 54};
        else r = (SDL_FRect){VW - 290, 450, 250, 58};
    }
    return r;
}

static bool in_rect(V2 p, SDL_FRect r) { return p.x >= r.x && p.x <= r.x + r.w && p.y >= r.y && p.y <= r.y + r.h; }

static void hangar_enter(void) {
    hscrap_t = 0;
    hnote_t = 0;
    hrep_id = -1;
    hrow = R.draft_taken ? HR_SHIP : HR_DRAFT;
    hcol = R.draft_taken ? mod_slots() : 0;
}

static void hangar_celebrate(SDL_FRect r, Col c) {
    V2 w = v2w(v2(r.x + r.w * 0.5f, r.y + r.h * 0.5f));
    fx_ring(w, 20, 140, c, 0.5f, 6);
    fx_burst(w, 24, c, 260, 0.5f, 3);
    grid_impulse(w, 160, 260);
    hflash = 1;
    ui_press(ui_id(UA_HIT, hrow * 16 + hcol));
}

/* the module is on board; pay for it if it came from the shop */
static void hangar_commit(int id, int src) {
    char buf[112];
    if (src >= 10) {
        R.salvage -= shop_price(src - 10);
        R.shop_sold[src - 10] = true;
    } else R.draft_taken = true;
    if (id == MOD_QUAD) W.pl.quad = true;
    if (id == MOD_CALIBRATE) W.pl.laser_level = clampi(W.pl.laser_level, mod_base_laser(), 4);
    snprintf(buf, sizeof(buf), R.mods[id] > 1 ? "%s - RANK %d" : "%s INSTALLED", MODS[id].name, R.mods[id]);
    hangar_note(buf, mod_color(MODS[id].cat));
    int ids[MOD_COUNT], n = ship_mods(ids);
    for (int k = 0; k < n; k++)
        if (ids[k] == id) ui_press(ui_id(UA_HIT, HR_SHIP * 16 + k));
    snd_play(MODS[id].rare || R.mods[id] == MODS[id].max_rank ? SND_EXTRALIFE : SND_POWERUP, 0.7f, 1.0f);
    campaign_save();
}

static void hangar_offer(int id, int src) {
    if (R.mods[id] >= MODS[id].max_rank) {
        /* the other offer already maxed it */
        hangar_deny("THAT MODULE IS ALREADY AT ITS HIGHEST RANK", C_GREY);
        return;
    }
    if (src >= 10 && R.salvage < shop_price(src - 10)) {
        hangar_deny("NOT ENOUGH SALVAGE", C_RED);
        return;
    }
    if (R.mods[id] == 0 && mod_installed() >= mod_slots()) {
        hrep_id = id;
        hrep_src = src;
        hrow = HR_SHIP;
        hcol = 0;
        hangar_note("ALL SLOTS ARE FULL - PICK A MODULE TO SCRAP FOR IT", C_ORANGE);
        snd_play(SND_MENU_SEL, 0.6f, 0.8f);
        return;
    }
    mod_add(id);
    hangar_celebrate(src >= 10 ? hitem_rect(HR_SHOP, src - 10) : hitem_rect(HR_DRAFT, src), mod_color(MODS[id].cat));
    hangar_commit(id, src);
}

static void hangar_service(int item) {
    Player *p = &W.pl;
    int price = shop_price(item);
    char buf[96];
    if (item == SHOP_REROLL && R.draft_taken) { hangar_deny("YOU HAVE ALREADY TAKEN THIS DRAFT'S MODULE", C_GREY); return; }
    if (item == SHOP_REPAIR && p->shield >= 200) { hangar_deny("YOUR SHIELD IS AT MAXIMUM", C_GREY); return; }
    int ms = p->secondary >= 0 ? p->secondary : SW_CONCUSSION;
    if (item == SHOP_MISSILES && p->secondary >= 0 && p->missiles >= game_missile_cap(ms)) {
        hangar_deny("YOUR MISSILE RACKS ARE FULL", C_GREY);
        return;
    }
    if (R.salvage < price) { hangar_deny("NOT ENOUGH SALVAGE", C_RED); return; }
    R.salvage -= price;
    switch (item) {
    case SHOP_REROLL:
        R.rerolls++;
        run_roll_draft();
        hangar_note("NEW DRAFT OFFERS", C_CYAN);
        break;
    case SHOP_REPAIR:
        p->shield = minf(200, p->shield + 50);
        R.repairs++;
        snprintf(buf, sizeof(buf), "HULL REPAIRED - SHIELD %d", (int)p->shield);
        hangar_note(buf, C_BLUE);
        break;
    case SHOP_MISSILES:
        if (p->secondary < 0 || p->missiles == 0) p->secondary = ms;
        p->missiles = clampi(p->missiles + 4, 0, game_missile_cap(ms));
        snprintf(buf, sizeof(buf), "%s MISSILES RESTOCKED - %d IN THE RACKS", SECONDARY_NAMES[ms], p->missiles);
        hangar_note(buf, rgba(1, 0.7f, 0.4f, 1));
        break;
    default:
        G.lives++;
        R.ships_bought++;
        hangar_note("A SPARE SHIP IS READY", C_GREEN);
        break;
    }
    snd_play(SND_POWERUP, 0.6f, 1.1f);
    hangar_celebrate(hitem_rect(HR_SHOP, item), C_CYAN);
    campaign_save();
}

static void hangar_confirm(void) {
    int ids[MOD_COUNT], n = ship_mods(ids);
    if (hrep_id >= 0) {
        /* scrap the selected module for the waiting one */
        if (hrow != HR_SHIP || hcol >= n) return;
        int old = ids[hcol];
        mod_remove(old);
        R.salvage += SCRAP_REFUND;
        mod_add(hrep_id);
        hangar_celebrate(hitem_rect(HR_SHIP, hcol), mod_color(MODS[hrep_id].cat));
        hangar_commit(hrep_id, hrep_src);
        hrep_id = -1;
        return;
    }
    switch (hrow) {
    case HR_DRAFT:
        if (R.draft_taken) { hangar_deny("DRAFT TAKEN - SEE THE SHOP OR CHART YOUR COURSE", C_GREY); break; }
        if (hcol >= R.ndraft) {
            R.draft_taken = true;
            R.salvage += SKIP_SALVAGE;
            hangar_note("DRAFT SKIPPED: +25 SALVAGE", powerup_color(PU_SALVAGE));
            snd_play(SND_PICKUP, 0.6f, 1.0f);
            campaign_save();
        } else hangar_offer(R.draft[hcol], hcol);
        break;
    case HR_SHOP:
        if (hcol <= SHOP_MOD1) {
            if (R.shop[hcol] < 0 || R.shop_sold[hcol]) { hangar_deny(NULL, C_GREY); break; }
            hangar_offer(R.shop[hcol], 10 + hcol);
        } else hangar_service(hcol);
        break;
    default:
        if (hcol >= mod_slots()) {
            snd_play(SND_TELEPORT, 0.6f, 0.9f);
            ui_press(ui_id(UA_HIT, hrow * 16 + hcol));
            goto_state(S_MAP);
        }
        break;
    }
}

static void hangar_update(float dt) {
    bg_update_ex(dt, false);
    hscrap_t = maxf(0, hscrap_t - dt);
    hnote_t = maxf(0, hnote_t - dt);
    hflash = maxf(0, hflash - dt * 2);
    int ids[MOD_COUNT], nm = ship_mods(ids);
    if (hrep_id >= 0) {
        /* choosing a module to scrap */
        if (ui.left && nm > 0) { hcol = (hcol + nm - 1) % nm; snd_play(SND_MENU_MOVE, 0.5f, 1.0f); }
        if (ui.right && nm > 0) { hcol = (hcol + 1) % nm; snd_play(SND_MENU_MOVE, 0.5f, 1.0f); }
        for (int c = 0; c < nm; c++) {
            if (!in_rect(ui.mouse, hitem_rect(HR_SHIP, c))) continue;
            ui_point = true;
            if (ui.mouse_moved) hcol = c;
        }
        if (ui.confirm || (ui.click && in_rect(ui.mouse, hitem_rect(HR_SHIP, hcol)))) hangar_confirm();
        if (ui.back) {
            hrep_id = -1;
            hangar_note("CANCELLED", C_GREY);
        }
        return;
    }
    if (ui.up) { hrow = (hrow + HR_ROWS - 1) % HR_ROWS; snd_play(SND_MENU_MOVE, 0.5f, 1.0f); }
    if (ui.down) { hrow = (hrow + 1) % HR_ROWS; snd_play(SND_MENU_MOVE, 0.5f, 1.0f); }
    int len = hrow_len(hrow);
    if (hcol >= len) hcol = len - 1;
    if (ui.left) { hcol = (hcol + len - 1) % len; snd_play(SND_MENU_MOVE, 0.5f, 1.0f); }
    if (ui.right) { hcol = (hcol + 1) % len; snd_play(SND_MENU_MOVE, 0.5f, 1.0f); }
    int hr = -1, hc = -1;
    for (int r = 0; r < HR_ROWS; r++)
        for (int c = 0; c < hrow_len(r); c++)
            if (in_rect(ui.mouse, hitem_rect(r, c))) { hr = r; hc = c; }
    if (hr >= 0) ui_point = true;
    if (hr >= 0 && ui.mouse_moved && (hr != hrow || hc != hcol)) {
        hrow = hr;
        hcol = hc;
        snd_play(SND_MENU_MOVE, 0.4f, 1.0f);
    }
    if (ui.confirm || (ui.click && hr >= 0)) {
        if (ui.click) { hrow = hr; hcol = hc; }
        hangar_confirm();
    }
    if (ui.alt) {
        /* scrap the selected module for a little salvage */
        if (hrow != HR_SHIP || hcol >= nm) hangar_note("SELECT AN INSTALLED MODULE TO SCRAP IT", C_GREY);
        else if (hscrap_t <= 0) {
            hscrap_t = 3.0f;
            snd_play(SND_MENU_MOVE, 0.5f, 0.7f);
        } else {
            char buf[96];
            snprintf(buf, sizeof(buf), "%s SCRAPPED: +%d SALVAGE", MODS[ids[hcol]].name, SCRAP_REFUND);
            mod_remove(ids[hcol]);
            R.salvage += SCRAP_REFUND;
            hangar_note(buf, powerup_color(PU_SALVAGE));
            hscrap_t = 0;
            snd_play(SND_TELEPORT, 0.6f, 1.2f);
            campaign_save();
        }
    }
    /* the run is saved: CONTINUE comes back here */
    if (ui.back) goto_state(S_TITLE);
}

/* an item of the hangar lifts under the cursor, squashes and flares when used, shakes when refused;
 * it deals itself in when the hangar opens. Pair with r_pop. */
static float hangar_item_push(int row, int col, SDL_FRect r, bool sel, float delay, float lift_px) {
    int key = row * 16 + col;
    float in = intro_k(delay);
    float lift = ui_ease(ui_id(UA_LIFT, key), sel ? 1.0f : 0.0f, 14);
    float hit = ui_pressed(ui_id(UA_HIT, key));
    float s = 1 + 0.03f * lift - 0.06f * hit;
    r_push(v2(ui_shake(ui_id(UA_DENY, key)), -lift_px * lift + 36 * (1 - in)), v2(r.x + r.w * 0.5f, r.y + r.h * 0.5f), s, s, in);
    return lift;
}

/* the flare over an item that was just used */
static void hangar_item_flare(int row, int col, SDL_FRect r, Col c) {
    float hit = ui_pressed(ui_id(UA_HIT, row * 16 + col));
    if (hit <= 0) return;
    r_glow(v2(r.x + r.w * 0.5f, r.y + r.h * 0.5f), maxf(r.w, r.h) * 0.8f, col_a(c, 0.3f * hit));
    float g = 12 * (1 - hit);
    r_frame(r.x - g, r.y - g, r.w + 2 * g, r.h + 2 * g, 3, col_a(col_white(c, 0.5f), hit));
}

static void hangar_draft_card(int k) {
    SDL_FRect r = hitem_rect(HR_DRAFT, k);
    bool sel = hrow == HR_DRAFT && hcol == k && hrep_id < 0;
    float cx = r.x + r.w * 0.5f;
    float a = R.draft_taken ? 0.35f : 1.0f;
    float lift = hangar_item_push(HR_DRAFT, k, r, sel, 0.1f + k * 0.07f, 8);
    if (k >= R.ndraft) {
        Col c = col_a(powerup_color(PU_SALVAGE), a);
        r_panel(r.x, r.y, r.w, r.h, c, 0.7f + 0.2f * lift);
        if (lift > 0.01f) r_glow(v2(cx, r.y + r.h * 0.5f), r.w * 0.8f, col_a(c, 0.08f * lift));
        r_text("SKIP", cx, r.y + 58, 16, col_white(c, 0.3f * lift), AL_CENTER);
        draw_salvage("+25", cx, r.y + 92, 12, c, AL_CENTER);
        if (sel) sel_ring(v2(cx, r.y + r.h * 0.5f), 70);
        hangar_item_flare(HR_DRAFT, k, r, c);
        r_pop();
        return;
    }
    int id = R.draft[k];
    const ModDef *m = &MODS[id];
    Col c = col_a(mod_color(m->cat), a);
    r_panel(r.x, r.y, r.w, r.h, c, 0.72f + 0.18f * lift);
    if (lift > 0.01f) r_glow(v2(cx, r.y + r.h * 0.5f), r.w * 0.7f, col_a(c, 0.1f * lift));
    mod_draw_badge(id, v2(cx, r.y + 38), 22 * (1 + 0.12f * lift), a, R.mods[id] > 0);
    float ns = minf(11, (r.w - 16) / maxf(1, text_width(m->name, 1)));
    r_text(m->name, cx, r.y + 70, ns, col_lerp(c, col_white(c, 0.4f), lift), AL_CENTER);
    r_textf(cx, r.y + 90, 7.5f, col_a(c, 0.75f), AL_CENTER, "%s  -  %s%s", MOD_CAT_NAMES[m->cat], MOD_TAG_NAMES[m->tag], m->rare ? "  -  RARE" : "");
    char eff[64];
    int rank = R.mods[id];
    mod_effect_text(id, rank + 1, eff, sizeof(eff));
    float es = minf(8.5f, (r.w - 14) / maxf(1, text_width(eff, 1)));
    r_text(eff, cx, r.y + 112, es, col_a(C_WHITE, 0.85f * a), AL_CENTER);
    const char *st = rank > 0 ? NULL : mod_installed() >= mod_slots() ? "NEEDS A FREE SLOT" : "NEW";
    if (rank >= m->max_rank) r_text("MAXED", cx, r.y + 136, 9, col_a(C_GREY, a), AL_CENTER);
    else if (rank > 0) r_textf(cx, r.y + 136, 9, col_a(C_CYAN, a), AL_CENTER, "RANK %d > %d", rank, rank + 1);
    else r_text(st, cx, r.y + 136, 9, col_a(st[0] == 'N' && st[1] == 'E' && st[2] == 'W' ? C_GREEN : C_ORANGE, a), AL_CENTER);
    r_text(R.draft_taken ? "" : "FREE", cx, r.y + 154, 8, col_a(C_GREEN, 0.7f * a), AL_CENTER);
    if (sel) sel_ring(v2(cx, r.y + 38), 32);
    hangar_item_flare(HR_DRAFT, k, r, c);
    r_pop();
}

static void hangar_shop_tile(int item) {
    SDL_FRect r = hitem_rect(HR_SHOP, item);
    bool sel = hrow == HR_SHOP && hcol == item && hrep_id < 0;
    if (item <= SHOP_MOD1 && R.shop[item] < 0) return;
    float lift = hangar_item_push(HR_SHOP, item, r, sel, 0.3f + item * 0.04f, 4);
    int price = shop_price(item);
    bool can = R.salvage >= price;
    Col c = rgba(0.45f, 0.85f, 1, 1);
    char title[40], sub[48];
    bool sold = false;
    Player *p = &W.pl;
    if (item <= SHOP_MOD1) {
        int id = R.shop[item];
        sold = R.shop_sold[item];
        c = mod_color(MODS[id].cat);
        r_panel(r.x, r.y, r.w, r.h, col_a(c, sold ? 0.3f : 1), 0.72f + 0.15f * lift);
        mod_draw_badge(id, v2(r.x + 26, r.y + 30), 15, sold ? 0.3f : 1, R.mods[id] > 0);
        float ns = minf(9, (r.w - 56) / maxf(1, text_width(MODS[id].name, 1)));
        r_text(MODS[id].name, r.x + 48, r.y + 16, ns, col_a(c, sold ? 0.3f : 1), AL_LEFT);
        r_text(R.mods[id] > 0 ? "RANK UP" : MODS[id].rare ? "RARE MODULE" : "MODULE", r.x + 48, r.y + 34, 7.5f, col_a(c, sold ? 0.3f : 0.7f), AL_LEFT);
    } else {
        int ms = p->secondary >= 0 ? p->secondary : SW_CONCUSSION;
        switch (item) {
        case SHOP_REROLL: snprintf(title, sizeof(title), "REROLL DRAFT"); snprintf(sub, sizeof(sub), R.draft_taken ? "DRAFT TAKEN" : "THREE NEW OFFERS"); break;
        case SHOP_REPAIR: snprintf(title, sizeof(title), "HULL REPAIR"); snprintf(sub, sizeof(sub), "+50 SHIELD  (NOW %d)", (int)p->shield); break;
        case SHOP_MISSILES: snprintf(title, sizeof(title), "MISSILES"); snprintf(sub, sizeof(sub), "+4 %s", SECONDARY_NAMES[ms]); break;
        default: snprintf(title, sizeof(title), "SPARE SHIP"); snprintf(sub, sizeof(sub), "SHIPS: %d", G.lives); break;
        }
        r_panel(r.x, r.y, r.w, r.h, c, 0.72f + 0.15f * lift);
        r_text(title, r.x + 14, r.y + 14, 10, col_lerp(c, col_white(c, 0.5f), lift), AL_LEFT);
        r_text(sub, r.x + 14, r.y + 34, 7.5f, col_a(c, 0.75f), AL_LEFT);
    }
    char pb[24];
    if (sold) snprintf(pb, sizeof(pb), "SOLD");
    else snprintf(pb, sizeof(pb), "%d", price);
    if (sold) r_text(pb, r.x + r.w - 12, r.y + r.h - 24, 10, col_a(C_GREY, 0.7f), AL_RIGHT);
    else draw_salvage(pb, r.x + r.w - 12, r.y + r.h - 24, 11, can ? powerup_color(PU_SALVAGE) : col_a(C_RED, 0.8f), AL_RIGHT);
    if (lift > 0.01f) r_glow(v2(r.x + r.w * 0.5f, r.y + r.h * 0.5f), r.w * 0.6f, col_a(c, 0.07f * lift));
    hangar_item_flare(HR_SHOP, item, r, c);
    r_pop();
}

static void hangar_details(float x0, float y0, float pw) {
    float x1 = x0 + pw - 24;
    int ids[MOD_COUNT], nm = ship_mods(ids);
    int id = -1;
    const char *action = NULL;
    Col ac = rgba(0.7f, 0.75f, 0.85f, 0.9f);
    char actbuf[128];
    const char *ok = g_gamepad_connected ? "A" : "ENTER";
    if (hrep_id >= 0) {
        id = hrow == HR_SHIP && hcol < nm ? ids[hcol] : -1;
        snprintf(actbuf, sizeof(actbuf), "%s  SCRAP IT FOR %s  (+%d SALVAGE)   -   ESC CANCELS", ok, MODS[hrep_id].name, SCRAP_REFUND);
        action = actbuf;
        ac = C_ORANGE;
    } else if (hrow == HR_DRAFT && hcol < R.ndraft) {
        id = R.draft[hcol];
        snprintf(actbuf, sizeof(actbuf), R.draft_taken ? "DRAFT ALREADY TAKEN" : "%s  TAKE IT - FREE", ok);
        action = actbuf;
        ac = R.draft_taken ? ac : C_GREEN;
    } else if (hrow == HR_SHOP && hcol <= SHOP_MOD1 && R.shop[hcol] >= 0) {
        id = R.shop[hcol];
        if (R.shop_sold[hcol]) snprintf(actbuf, sizeof(actbuf), "SOLD");
        else snprintf(actbuf, sizeof(actbuf), "%s  BUY FOR %d SALVAGE", ok, shop_price(hcol));
        action = actbuf;
        ac = R.salvage >= shop_price(hcol) ? powerup_color(PU_SALVAGE) : C_RED;
    } else if (hrow == HR_SHIP && hcol < nm) {
        id = ids[hcol];
        action = hscrap_t > 0 ? (g_gamepad_connected ? "PRESS Y AGAIN TO SCRAP IT" : "PRESS R AGAIN TO SCRAP IT")
                              : (g_gamepad_connected ? "Y  SCRAP FOR 30 SALVAGE" : "R  SCRAP FOR 30 SALVAGE");
        ac = hscrap_t > 0 ? C_YELLOW : ac;
    }
    /* a new selection types its name on and slides its lines in */
    int key = (hrep_id >= 0) * 1000 + hrow * 16 + hcol;
    if (key != hdet_key) {
        hdet_key = key;
        hdet_t = g_time;
    }
    float st = g_time - hdet_t, tk = clampf(st / 0.3f, 0, 1), la = smooth01((st - 0.06f) / 0.25f);
    if (id >= 0) {
        const ModDef *m = &MODS[id];
        Col c = mod_color(m->cat);
        r_panel(x0, y0, pw, 124, c, 0.82f);
        float bp = ease_out_back(clampf(st / 0.3f, 0, 1));
        mod_draw_badge(id, v2(x0 + 44, y0 + 46), 24 * bp, 1, true);
        r_text_reveal(m->name, x0 + 84, y0 + 14, 17, c, AL_LEFT, tk, true);
        r_push(v2(10 * (1 - la), 0), v2(0, 0), 1, 1, la);
        r_textf(x0 + 84, y0 + 40, 8, col_a(c, 0.75f), AL_LEFT, "%s  -  %s%s", MOD_CAT_NAMES[m->cat], MOD_TAG_NAMES[m->tag], m->rare ? "  -  RARE MODULE" : "");
        r_text(m->desc, x0 + 84, y0 + 60, 10.5f, rgba(0.85f, 0.9f, 1, 0.95f), AL_LEFT);
        int rank = R.mods[id];
        char now[64], next[64], buf[96];
        r_textf(x1, y0 + 14, 11, col_a(C_WHITE, 0.85f), AL_RIGHT, "RANK %d / %d", rank, m->max_rank);
        if (rank > 0) {
            mod_effect_text(id, rank, now, sizeof(now));
            snprintf(buf, sizeof(buf), "NOW   %s", now);
            r_text(buf, x1, y0 + 36, 10, rgba(0.8f, 0.85f, 1, 0.85f), AL_RIGHT);
        }
        if (rank < m->max_rank) {
            mod_effect_text(id, rank + 1, next, sizeof(next));
            snprintf(buf, sizeof(buf), "NEXT  %s", next);
            r_text(buf, x1, y0 + (rank > 0 ? 54 : 36), 10, c, AL_RIGHT);
        }
        if (action) r_text(action, x1, y0 + 94, 12, ac, AL_RIGHT);
        r_pop();
        return;
    }
    /* everything else */
    const char *title = "", *desc = "";
    Col c = rgba(0.45f, 0.85f, 1, 1);
    char dbuf[160];
    if (hrow == HR_DRAFT) {
        title = "SKIP THE DRAFT";
        desc = "TAKE 25 SALVAGE INSTEAD OF A MODULE. SLOTS ARE LIMITED - SOMETIMES NOTHING IS WORTH ONE.";
        c = powerup_color(PU_SALVAGE);
        snprintf(actbuf, sizeof(actbuf), R.draft_taken ? "DRAFT ALREADY TAKEN" : "%s  SKIP", ok);
        action = actbuf;
    } else if (hrow == HR_SHOP) {
        static const char *T[SHOP_COUNT] = {"", "", "REROLL THE DRAFT", "HULL REPAIR", "MISSILE RESTOCK", "SPARE SHIP"};
        static const char *D[SHOP_COUNT] = {"", "", "REPLACE THE THREE DRAFT OFFERS WITH NEW ONES. THE PRICE GROWS WITH EVERY REROLL.",
                                            "THE SHIELD CARRIES OVER BETWEEN SECTORS. PATCH IT UP BY 50 (UP TO 200).",
                                            "FOUR MORE MISSILES OF THE KIND IN YOUR SECONDARY SLOT.", "ONE MORE SHIP FOR THIS RUN. THE PRICE GROWS WITH EVERY SHIP."};
        title = T[hcol];
        desc = D[hcol];
        snprintf(actbuf, sizeof(actbuf), "%s  BUY FOR %d SALVAGE", ok, shop_price(hcol));
        action = actbuf;
        ac = R.salvage >= shop_price(hcol) ? powerup_color(PU_SALVAGE) : C_RED;
    } else if (hcol < mod_slots()) {
        title = "EMPTY SLOT";
        desc = "TAKE A MODULE FROM THE DRAFT OR BUY ONE IN THE SHOP TO FILL IT.";
    } else {
        const SectorNode *nd = run_node(R.layer, run_first_reachable());
        title = "CHART COURSE";
        snprintf(dbuf, sizeof(dbuf), "ON TO SECTOR %d OF %d%s. PICK THE NEXT MINE ON THE SECTOR CHART.", R.layer + 1, R.nlayers,
                 nd && nd->reward == SR_BOSS ? " - THE OVERSEER'S FOUNDRY" : "");
        desc = dbuf;
        c = C_CYAN;
        snprintf(actbuf, sizeof(actbuf), "%s  OPEN THE SECTOR CHART", ok);
        action = actbuf;
        ac = col_white(C_CYAN, 0.3f * (0.5f + 0.5f * sinf(g_time * 6)));
    }
    r_panel(x0, y0, pw, 124, c, 0.82f);
    r_text_reveal(title, x0 + 24, y0 + 14, 17, c, AL_LEFT, tk, true);
    r_push(v2(10 * (1 - la), 0), v2(0, 0), 1, 1, la);
    r_text(desc, x0 + 24, y0 + 48, 10.5f, rgba(0.85f, 0.9f, 1, 0.95f), AL_LEFT);
    if (action) r_text(action, x1, y0 + 94, 12, ac, AL_RIGHT);
    r_pop();
}

static void hangar_draw(void) {
    bg_draw(0.35f);
    float VW = g_virt_w, cx = VW * 0.5f;
    Col salv = powerup_color(PU_SALVAGE);
    Col dim = rgba(0.6f, 0.7f, 0.9f, 0.8f);
    Player *p = &W.pl;

    /* header */
    float tk = intro_k(0);
    r_push(v2(0, -12 * (1 - tk)), v2(0, 0), 1, 1, tk);
    r_text_glow("R&D HANGAR", cx, 16, 28, rgba(0.4f, 0.9f, 1, 1), AL_CENTER);
    if (R.sectors_done == 0 && R.layer == 0) r_text("YOUR STARTING DRAFT: TAKE A MODULE, THEN CHART YOUR COURSE", cx, 54, 10, dim, AL_CENTER);
    else r_textf(cx, 54, 10, dim, AL_CENTER, "SECTOR %d CLEARED  -  REFIT BEFORE SECTOR %d OF %d", R.layer, R.layer + 1, R.nlayers);
    /* the salvage counter rolls to its new value and glows while it does */
    float shown = ui_ease(ui_id(UA_BAR, 900), (float)R.salvage, 7);
    float roll = clampf(fabsf(shown - R.salvage) / 12, 0, 1);
    char buf[128];
    snprintf(buf, sizeof(buf), "%d", (int)lroundf(shown));
    draw_salvage(buf, VW - 36, 18, 20 * (1 + 0.08f * roll), col_white(salv, 0.6f * roll), AL_RIGHT);
    r_text("BANKED SALVAGE", VW - 36, 46, 8, col_a(salv, 0.6f), AL_RIGHT);
    r_textf(36, 20, 11, dim, AL_LEFT, "SHIELD %d   -   SHIPS %d", (int)p->shield, G.lives);
    r_textf(36, 40, 8, col_a(dim, 0.75f), AL_LEFT, "LASER LV%d%s  -  %s  -  %s X%d", p->laser_level, p->quad ? " QUAD" : "",
            p->special >= 0 ? PRIMARY_NAMES[p->special] : "NO SPECIAL", p->secondary >= 0 ? SECONDARY_NAMES[p->secondary] : "NO MISSILES",
            p->missiles);
    r_pop();
    if (hnote_t > 0) {
        /* a note drops in, then fades */
        float a = clampf(hnote_t / 0.4f, 0, 1), d = ease_out_cubic((2.8f - hnote_t) / 0.2f);
        r_text(hnote, cx, 76 - 8 * (1 - d), 11, col_a(hnote_col, a * d), AL_CENTER);
    }

    /* the draft */
    r_push(v2(0, 0), v2(0, 0), 1, 1, intro_k(0.08f));
    r_textf(cx, 92, 9, col_a(C_WHITE, 0.7f), AL_CENTER, R.draft_rare ? "DERELICT SALVAGE - RARE MODULES - TAKE ONE FREE" : "DRAFT - TAKE ONE MODULE FREE");
    r_text("SUPPLY SHOP", cx, 300, 9, col_a(C_WHITE, 0.7f), AL_CENTER);
    r_pop();
    for (int k = 0; k <= R.ndraft; k++) hangar_draft_card(k);
    /* the shop, and the frame that glides between its tiles */
    for (int i = 0; i < SHOP_COUNT; i++) hangar_shop_tile(i);
    {
        bool on = hrow == HR_SHOP && hrep_id < 0;
        SDL_FRect r = hitem_rect(HR_SHOP, hcol < SHOP_COUNT ? hcol : 0);
        uint32_t fx = ui_id(UA_CURSOR, 10), fw = ui_id(UA_CURSOR, 11);
        if (!on) {
            ui_snap(fx, r.x);
            ui_snap(fw, r.w);
        }
        float x = ui_spring(fx, r.x, 420, 28), w = ui_spring(fw, r.w, 420, 28);
        float fa = ui_ease(ui_id(UA_CURSOR, 12), on ? 1.0f : 0.0f, 14) * intro_k(0.4f);
        float lift = 4 * ui_ease(ui_id(UA_LIFT, HR_SHOP * 16 + hcol), on ? 1.0f : 0.0f, 14);
        float dx = ui_shake(ui_id(UA_DENY, HR_SHOP * 16 + hcol));
        if (fa > 0.01f) r_frame(x - 4 + dx, r.y - 4 - lift, w + 8, r.h + 8, 3, col_a(C_MAGENTA, (0.6f + 0.4f * sinf(g_time * 6)) * fa));
    }

    /* the ship and its slots */
    const ShipDef *sd = run_ship();
    holo_ship(v2(66, 472), 24, g_time * 0.8f, sd->col, intro_k(0.45f));
    r_push(v2(0, 0), v2(0, 0), 1, 1, intro_k(0.45f));
    r_text(sd->name, 110, 458, 12, sd->col, AL_LEFT);
    r_textf(110, 476, 8, col_a(sd->col, 0.7f), AL_LEFT, "%d / %d SLOTS", mod_installed(), mod_slots());
    r_pop();
    int ids[MOD_COUNT], nm = ship_mods(ids);
    for (int k = 0; k < mod_slots(); k++) {
        SDL_FRect r = hitem_rect(HR_SHIP, k);
        V2 c = v2(r.x + r.w * 0.5f, r.y + r.h * 0.5f);
        bool sel = hrow == HR_SHIP && hcol == k;
        int key = HR_SHIP * 16 + k;
        /* the slots pop in one by one; an installed module pops out of its slot */
        float in = ease_out_back(clampf((state_t - 0.45f - k * 0.04f) / 0.35f, 0, 1));
        float hov = ui_ease(ui_id(UA_LIFT, key), sel ? 1.0f : 0.0f, 14);
        float hit = ui_pressed(ui_id(UA_HIT, key));
        float sc = in * (1 + 0.12f * hov + 0.35f * hit);
        if (sc <= 0.01f) continue;
        r_push(v2(ui_shake(ui_id(UA_DENY, key)), 0), c, sc, sc, clampf(in, 0, 1));
        if (k < nm) {
            Col mc = mod_color(MODS[ids[k]].cat);
            mod_draw_badge(ids[k], c, 22, 1, true);
            if (R.mods[ids[k]] > 1) r_textf(c.x + 16, c.y + 8, 8, C_WHITE, AL_LEFT, "%d", R.mods[ids[k]]);
            if (hrep_id >= 0) r_circle(c, 30, 2, col_a(C_ORANGE, 0.5f + 0.4f * sinf(g_time * 8)), 20);
            if (hit > 0) {
                r_glow(c, 60, col_a(mc, 0.5f * hit));
                r_circle(c, 24 + 26 * (1 - hit), 3, col_a(col_white(mc, 0.5f), hit), 24);
            }
        } else {
            for (int s = 0; s < 6; s++) {
                float a0 = s * TAU / 6 + g_time * (0.2f + 1.2f * hov);
                r_arc(c, 20, a0, a0 + 0.5f, 2.5f, rgba(0.5f, 0.6f, 0.8f, 0.4f + 0.3f * hov), 4);
            }
        }
        r_pop();
        if (sel) sel_ring(c, 30);
    }
    {
        /* chart course: it brightens under the cursor, the arrow pushes on, a press flares it */
        SDL_FRect r = hitem_rect(HR_SHIP, mod_slots());
        bool sel = hrow == HR_SHIP && hcol == mod_slots() && hrep_id < 0;
        int key = HR_SHIP * 16 + mod_slots();
        float h = hangar_item_push(HR_SHIP, mod_slots(), r, sel, 0.55f, 3);
        float hit = ui_pressed(ui_id(UA_HIT, key));
        Col c = col_white(col_lerp(C_CYAN, C_WHITE, h), hit);
        r_panel(r.x, r.y, r.w, r.h, C_CYAN, 0.6f + 0.3f * h);
        if (h > 0.01f) r_glow(v2(r.x + r.w * 0.5f, r.y + r.h * 0.5f), 130, col_a(C_CYAN, 0.12f * h));
        float nudge = h * (3 + 3 * sinf(g_time * 6)) + 10 * hit;
        r_text_glow("CHART COURSE", r.x + r.w * 0.5f - 12, r.y + 20, 15, c, AL_CENTER);
        float ax = r.x + r.w * 0.5f + text_width("CHART COURSE", 15) * 0.5f + 8 + nudge;
        r_polyline((V2[]){v2(ax, r.y + 21), v2(ax + 7, r.y + 27.5f), v2(ax, r.y + 34)}, 3, false, 3.5f, c);
        hangar_item_flare(HR_SHIP, mod_slots(), r, C_CYAN);
        r_pop();
    }
    if (hflash > 0) r_add_rect(0, 0, VW, VIRT_H, col_a(C_WHITE, hflash * 0.04f));

    float pw = minf(1100, VW - 60), dk = intro_k(0.5f);
    r_push(v2(0, 0), v2(cx, 544 + 62), 1, 0.03f + 0.97f * dk, dk);
    hangar_details(cx - pw * 0.5f, 544, pw);
    r_pop();
    char h[160];
    if (hrep_id >= 0) snprintf(h, sizeof(h), "LEFT / RIGHT PICK THE MODULE TO SCRAP   ENTER CONFIRM   ESC CANCEL");
    else if (g_gamepad_connected) snprintf(h, sizeof(h), "D-PAD SELECT   A TAKE / BUY   Y SCRAP MODULE   B MAIN MENU");
    else snprintf(h, sizeof(h), "ARROWS / MOUSE SELECT   ENTER TAKE / BUY   R SCRAP MODULE   ESC MAIN MENU");
    r_push(v2(0, 0), v2(0, 0), 1, 1, intro_k(0.6f));
    hint(h);
    r_pop();
}

/* ------------------------------------------------------------ events */
static void handle_play_key(SDL_Keycode k) {
    if (G.automap) {
        if (k == SDLK_TAB || k == SDLK_M || k == SDLK_ESCAPE) { G.automap = false; game_audio_pause(false); }
        return;
    }
    switch (k) {
    case SDLK_ESCAPE:
    case SDLK_P: goto_state(S_PAUSE); break;
    case SDLK_TAB:
    case SDLK_M:
        if (!W.pl.dead) {
            automap_open();
            game_audio_pause(true);
            snd_play(SND_MENU_SEL, 0.4f, 1.3f);
        }
        break;
    case SDLK_1: g_in.select = 0; break;
    case SDLK_2: g_in.select = 1; break;
    case SDLK_Q: g_in.cycle_p = 1; break;
    case SDLK_E: g_in.swap = true; break;
    case SDLK_F: g_in.jettison = true; break;
    default: break;
    }
}

static void handle_pad_button(int b) {
    if (state == S_PLAY) {
        if (G.automap) {
            if (b == SDL_GAMEPAD_BUTTON_BACK || b == SDL_GAMEPAD_BUTTON_EAST || b == SDL_GAMEPAD_BUTTON_START) { G.automap = false; game_audio_pause(false); }
            return;
        }
        switch (b) {
        case SDL_GAMEPAD_BUTTON_START: goto_state(S_PAUSE); break;
        case SDL_GAMEPAD_BUTTON_BACK:
            if (!W.pl.dead) { automap_open(); game_audio_pause(true); }
            break;
        case SDL_GAMEPAD_BUTTON_NORTH: g_in.cycle_p = 1; break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: g_in.swap = true; break;
        case SDL_GAMEPAD_BUTTON_WEST:
        case SDL_GAMEPAD_BUTTON_EAST: g_in.jettison = true; break;
        }
        return;
    }
    switch (b) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: ui.up = true; break;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: ui.down = true; break;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: ui.left = true; break;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: ui.right = true; break;
    case SDL_GAMEPAD_BUTTON_SOUTH:
    case SDL_GAMEPAD_BUTTON_START: ui.confirm = true; break;
    case SDL_GAMEPAD_BUTTON_EAST:
    case SDL_GAMEPAD_BUTTON_BACK: ui.back = true; break;
    case SDL_GAMEPAD_BUTTON_NORTH: ui.alt = true; break;
    }
}

static void process_events(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT: running = false; break;
        case SDL_EVENT_KEY_DOWN: {
            SDL_Keycode k = e.key.key;
            if ((k == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT)) || k == SDLK_F11) {
                g_cfg.fullscreen = !g_cfg.fullscreen;
                SDL_SetWindowFullscreen(g_window, g_cfg.fullscreen);
                config_save();
                break;
            }
            if (state == S_NAME) {
                if (k == SDLK_BACKSPACE) {
                    size_t l = strlen(name_buf);
                    if (l > 0) name_buf[l - 1] = 0;
                } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) ui.confirm = true;
                break;
            }
            if (state == S_SETUP && setup_editing) {
                if (k == SDLK_BACKSPACE) {
                    size_t l = strlen(setup_seed);
                    if (l > 0) setup_seed[l - 1] = 0;
                } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) ui.confirm = true;
                else if (k == SDLK_ESCAPE) ui.back = true;
                break;
            }
            if (state == S_PLAY) {
                if (!e.key.repeat) handle_play_key(k);
                break;
            }
            switch (k) {
            case SDLK_UP: case SDLK_W: ui.up = true; break;
            case SDLK_DOWN: case SDLK_S: ui.down = true; break;
            case SDLK_LEFT: case SDLK_A: ui.left = true; break;
            case SDLK_RIGHT: case SDLK_D: ui.right = true; break;
            case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: ui.confirm = true; break;
            case SDLK_ESCAPE: case SDLK_BACKSPACE: ui.back = true; break;
            case SDLK_R: ui.alt = true; break;
            default: break;
            }
        } break;
        case SDL_EVENT_TEXT_INPUT:
            if (state == S_NAME) {
                for (const char *c = e.text.text; *c; c++) {
                    size_t l = strlen(name_buf);
                    char ch = *c;
                    if (ch >= 'a' && ch <= 'z') ch -= 32;
                    if (l < 12 && ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '-' || ch == '.')) {
                        name_buf[l] = ch;
                        name_buf[l + 1] = 0;
                    }
                }
            } else if (state == S_SETUP && setup_editing) setup_text_input(e.text.text);
            break;
        case SDL_EVENT_MOUSE_MOTION: {
            float rx, ry;
            SDL_RenderCoordinatesFromWindow(g_ren, e.motion.x, e.motion.y, &rx, &ry);
            ui.mouse = v2(rx / g_scale, ry / g_scale);
            ui.mouse_moved = true;
            g_in.mouse_active = true;
            if (fabsf(e.motion.xrel) + fabsf(e.motion.yrel) > 1) g_in.use_stick = false;
        } break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) ui.click = true;
            if (state == S_PLAY) g_in.use_stick = false;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            ui.wheel += e.wheel.y;
            if (state == S_PLAY) {
                if (G.automap) G.automap_zoom = clampf(G.automap_zoom * (e.wheel.y > 0 ? 1.15f : 1 / 1.15f), 0.5f, 5.0f);
                else if (e.wheel.y != 0) g_in.cycle_p = 1;
            }
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!pad) {
                pad = SDL_OpenGamepad(e.gdevice.which);
                g_gamepad_connected = pad != NULL;
            }
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (pad && SDL_GetGamepadID(pad) == e.gdevice.which) {
                SDL_CloseGamepad(pad);
                pad = NULL;
                g_gamepad_connected = false;
                g_in.use_stick = false;
                if (state == S_PLAY) goto_state(S_PAUSE);
            }
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN: handle_pad_button(e.gbutton.button); break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            if (state == S_PLAY && !opt_bot && opt_frames < 0) goto_state(S_PAUSE);
            break;
        default: break;
        }
    }
    /* left stick menu navigation with repeat */
    if (pad && state != S_PLAY) {
        float ly = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
        float lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
        if (fabsf(ly) > 0.5f || fabsf(lx) > 0.5f) {
            if (stick_repeat <= 0) {
                if (ly < -0.5f) ui.up = true;
                if (ly > 0.5f) ui.down = true;
                if (lx < -0.5f) ui.left = true;
                if (lx > 0.5f) ui.right = true;
                stick_repeat = 0.22f;
            }
        } else stick_repeat = 0;
    }
}

/* ------------------------------------------------------------ main */
static void update(float dt) {
    g_time += dt;
    state_t += dt;
    stick_repeat -= dt;
    if (pending) {
        /* screen is fading out: freeze the old state and swallow input */
        memset(&ui.up, 0, sizeof(bool) * 8);
        ui.mouse_moved = false;
        return;
    }
    if (opt_autorun && state != S_PLAY && state_t > 0.6f) {
        /* press on through every screen; the hangar takes the first offer and charts the course */
        if (state == S_HANGAR && hrep_id < 0) { hrow = R.draft_taken ? HR_SHIP : HR_DRAFT; hcol = R.draft_taken ? mod_slots() : 0; }
        if (state == S_HANGAR && hrep_id >= 0) { hrow = HR_SHIP; hcol = 0; }
        if (state == S_TITLE && !has_campaign && frame_count > 60) running = false;
        if (state == S_TITLE) menu_sel = 0;
        ui.confirm = true;
    }
    ui_point = false;
    switch (state) {
    case S_TITLE: title_update(dt); break;
    case S_SETUP: setup_update(dt); break;
    case S_STORY: story_update(dt); break;
    case S_MAP: map_update(dt); break;
    case S_BRIEFING: briefing_update(dt); break;
    case S_PLAY: play_update(dt); break;
    case S_PAUSE: pause_update(dt); break;
    case S_TALLY: tally_update(dt); break;
    case S_GAMEOVER: gameover_update(dt); break;
    case S_VICTORY: victory_update(dt); break;
    case S_SUMMARY: summary_update(dt); break;
    case S_NAME: name_update(dt); break;
    case S_SCORES: scores_update(dt); break;
    case S_OPTIONS: options_update(dt); break;
    case S_HELP: help_update(dt); break;
    case S_HANGAR: hangar_update(dt); break;
    case S_RECORD: record_update(dt); break;
    case S_INTRO: intro_update(dt); break;
    }
    cursor_update();
    if (ui.click && state != S_MAP) ui_ripple(ui.mouse, C_CYAN, 0.6f);
    if (ui.up || ui.down || ui.left || ui.right || ui.confirm || ui.back || ui.click || ui.mouse_moved) idle_t = 0;
    else idle_t += dt;
    /* one-shot UI events are consumed by the first simulation step */
    memset(&ui.up, 0, sizeof(bool) * 8);
    ui.mouse_moved = false;
}

static void draw(void) {
    switch (state) {
    case S_TITLE: title_draw(); break;
    case S_SETUP: setup_draw(); break;
    case S_STORY: story_draw(); break;
    case S_MAP: map_draw(); break;
    case S_BRIEFING: briefing_draw(); break;
    case S_PLAY: play_draw(); break;
    case S_PAUSE: pause_draw(); break;
    case S_TALLY: tally_draw(); break;
    case S_GAMEOVER: gameover_draw(); break;
    case S_VICTORY: victory_draw(); break;
    case S_SUMMARY: summary_draw(); break;
    case S_NAME: name_draw(); break;
    case S_SCORES: scores_draw(); break;
    case S_OPTIONS: options_draw(); break;
    case S_HELP: help_draw(); break;
    case S_HANGAR: hangar_draw(); break;
    case S_RECORD: record_draw(); break;
    case S_INTRO: intro_draw(); break;
    }
    if (fade > 0.001f) r_fill_rect(0, 0, g_virt_w, VIRT_H, rgba(0, 0, 0, fade));
}

static AppState parse_state(const char *s) {
    static const struct { const char *name; AppState st; } T[] = {
        {"title", S_TITLE}, {"setup", S_SETUP}, {"difficulty", S_SETUP}, {"story", S_STORY}, {"map", S_MAP}, {"briefing", S_BRIEFING},
        {"tally", S_TALLY}, {"gameover", S_GAMEOVER}, {"victory", S_VICTORY}, {"summary", S_SUMMARY}, {"scores", S_SCORES},
        {"options", S_OPTIONS}, {"help", S_HELP}, {"name", S_NAME}, {"hangar", S_HANGAR}, {"record", S_RECORD}, {"intro", S_INTRO},
    };
    for (size_t i = 0; i < sizeof(T) / sizeof(T[0]); i++)
        if (!strcmp(s, T[i].name)) return T[i].st;
    return S_TITLE;
}

/* a test start deep in a run: walk the chart along the first links */
static void debug_goto_layer(int layer) {
    R.layer = clampi(layer, 0, R.nlayers - 1);
    for (int l = 0; l < R.layer; l++) {
        R.path[l] = 0;
        if (l > 0) {
            const SectorNode *prev = run_node(l - 1, R.path[l - 1]);
            for (int j = 0; j < R.nnodes[l]; j++)
                if ((prev->links >> j) & 1) { R.path[l] = j; break; }
        }
    }
    R.path[R.layer] = run_first_reachable();
    R.sectors_done = R.layer;
    G.level = R.layer;
    run_roll_draft();
    run_roll_shop();
}

int main(int argc, char **argv) {
    int start_level = -1;
    int start_diff = 2, start_ship = SHIP_WRAITH;
    uint32_t start_heat = 0, start_seed = 0;
    bool unlock_all = false, have_seed = false;
    AppState start_state = S_TITLE;
    bool state_given = false;
    int start_help_page = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--level") && i + 1 < argc) start_level = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--difficulty") && i + 1 < argc) start_diff = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) opt_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) opt_shot = argv[++i];
        else if (!strcmp(argv[i], "--state") && i + 1 < argc) {
            start_state = parse_state(argv[++i]);
            state_given = true;
        }
        else if (!strcmp(argv[i], "--page") && i + 1 < argc) start_help_page = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bot")) opt_bot = true;
        else if (!strcmp(argv[i], "--god")) opt_god = true;
        else if (!strcmp(argv[i], "--destroy")) opt_destroy = true;
        else if (!strcmp(argv[i], "--automap")) opt_automap = true;
        else if (!strcmp(argv[i], "--reveal")) opt_reveal = true;
        else if (!strcmp(argv[i], "--allweapons")) opt_allweapons = true;
        else if (!strcmp(argv[i], "--tp") && i + 2 < argc) { opt_tp_x = atoi(argv[++i]); opt_tp_y = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--arena")) opt_arena = true;
        else if (!strcmp(argv[i], "--spawn") && i + 1 < argc) opt_spawn = argv[++i];
        else if (!strcmp(argv[i], "--elite")) opt_elite = true;
        else if (!strcmp(argv[i], "--fullscreen")) g_cfg.fullscreen = true;
        else if (!strcmp(argv[i], "--salvage") && i + 1 < argc) opt_salvage = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cargo") && i + 1 < argc) opt_cargo = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mods") && i + 1 < argc) opt_mods = argv[++i];
        else if (!strcmp(argv[i], "--ship") && i + 1 < argc) start_ship = clampi(atoi(argv[++i]), 0, SHIP_COUNT - 1);
        else if (!strcmp(argv[i], "--heat") && i + 1 < argc) start_heat = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) have_seed = seed_parse(argv[++i], &start_seed);
        else if (!strcmp(argv[i], "--unlockall")) unlock_all = true;
        else if (!strcmp(argv[i], "--daily")) setup_daily = true;
        else if (!strcmp(argv[i], "--autorun")) opt_autorun = true;
        else if (!strcmp(argv[i], "--prefdir") && i + 1 < argc) opt_prefdir = argv[++i];
    }

    SDL_SetAppMetadata("Ballast", "2.0", "com.ballast.game");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    if (opt_prefdir) {
        size_t n = strlen(opt_prefdir) + 2;
        pref_path = SDL_malloc(n);
        snprintf(pref_path, n, "%s/", opt_prefdir);
    } else pref_path = SDL_GetPrefPath("Ballast", "Ballast");
    config_load();
    scores_load();
    if (!SDL_CreateWindowAndRenderer(GAME_TITLE, 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &g_window, &g_ren)) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(g_window, 800, 450);
    SDL_SetRenderVSync(g_ren, opt_frames > 0 ? 0 : 1); /* test runs step as fast as they can */
    if (g_cfg.fullscreen) SDL_SetWindowFullscreen(g_window, true);
    if (!render_init()) {
        SDL_Log("Renderer init failed: %s", SDL_GetError());
        return 1;
    }
    rng_seed(opt_frames > 0 ? 0x5EEDu : (uint32_t)SDL_GetTicks() ^ 0xA5A5F00Du); /* test runs replay exactly */
    game_init();
    audio_init();
    audio_set_volumes(g_cfg.music_vol, g_cfg.sfx_vol);
    /* test runs and debug starts leave the player's run and record alone */
    if ((opt_frames > 0 || start_level >= 0 || start_state != S_TITLE) && !opt_prefdir) campaign_io = false;
    {
        char pp[1024];
        path_join(pp, sizeof(pp), "profile.txt");
        profile_load(campaign_io ? pp : "");
    }
    if (unlock_all) g_prof.done = (1u << CH_COUNT) - 1;
    /* --difficulty 3 and 4 are the old ACE and INSANE: HOTSHOT with the veteran and elite protocols */
    if (start_diff > 2) start_heat |= 1u << TP_VETERAN;
    if (start_diff > 3) start_heat |= 1u << TP_ELITE;
    if (!have_seed) start_seed = hash32((uint32_t)SDL_GetTicksNS() ^ rng_next());
    run_new(start_ship, clampi(start_diff, 0, 2), start_heat, start_seed, false);
    install_debug_mods();
    if (start_level < 0) campaign_load();
    if (opt_salvage > 0) {
        R.salvage += opt_salvage;
        if (has_campaign) campaign_snapshot();
    }
    ui.mouse = v2(g_virt_w * 0.5f, VIRT_H * 0.5f - 120);
    {
        int n = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&n);
        if (ids && n > 0) {
            pad = SDL_OpenGamepad(ids[0]);
            g_gamepad_connected = pad != NULL;
        }
        SDL_free(ids);
    }

    if (start_level >= 0) debug_goto_layer(start_level);
    /* a normal launch opens on the intro; test runs go straight to their screen */
    if (!state_given && start_level < 0 && opt_frames < 0 && !opt_autorun) start_state = S_INTRO;
    if (start_level >= 0 && start_state == S_TITLE) {
        run_prepare_sector();
        state = S_BRIEFING;
        enter_state(S_PLAY);
        fade = 0;
    } else {
        if (start_state == S_TALLY || start_state == S_SUMMARY || start_state == S_GAMEOVER || start_state == S_VICTORY) {
            run_prepare_sector();
            G.robots_killed = 42; G.hostages_saved = 3; G.hostages_total = 4; G.level_time = 272;
            G.bonus_shield = 1630; G.bonus_hostage = 3000; G.bonus_skill = 3000; G.score = 48210;
            G.cargo_banked = 214; G.cargo_bonus = 107; G.escape_margin = 0.7f;
            R.sectors_done = R.layer; R.kills = 131; R.rescued = 9; R.salvage_total = 1240; R.deaths = 2; R.time = 1375;
            R.best_escape = 0.7f; R.best_chain = 6; R.best_haul = 412; R.best_bomb = 4;
            snprintf(R.killed_by, sizeof(R.killed_by), "SUPER HULK - HOMING MISSILE");
            tally_t = 100;
        }
        if (start_state == S_BRIEFING || start_state == S_PLAY) run_prepare_sector();
        enter_state(start_state);
        if (start_state == S_TALLY) tally_t = 100;
        if (start_state == S_HELP) help_page = start_help_page;
        if (opt_frames > 0) fade = 0;
    }

    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 last = SDL_GetPerformanceCounter();
    double work_total = 0, work_max = 0;
    double acc = 0;
    while (running) {
        Uint64 now = SDL_GetPerformanceCounter();
        double frame = (double)(now - last) / (double)freq;
        last = now;
        if (frame > 0.1) frame = 0.1;
        if (opt_frames > 0) frame = 1.0 / 60.0; /* deterministic stepping in test runs */
        render_check_resize();
        process_events();
        acc += frame;
        int steps = 0;
        while (acc >= SIM_DT && steps < 12) {
            update(SIM_DT);
            acc -= SIM_DT;
            steps++;
        }
        /* fades between states */
        if (pending) {
            fade = minf(1, fade + (float)frame * 4.0f);
            if (fade >= 1) set_state_now(pending_state);
        } else if (fade > 0) {
            fade = maxf(0, fade - (float)frame * 3.0f);
        }
        frame_count++;
        if (opt_frames > 0 && frame_count >= opt_frames) {
            if (opt_shot) g_screenshot_path = opt_shot;
            running = false;
        }
        Uint64 w0 = SDL_GetPerformanceCounter();
        render_begin();
        draw();
        render_end(g_cfg.bloom, 1.0f);
        double wt = (double)(SDL_GetPerformanceCounter() - w0) / (double)freq;
        if (frame_count > 10) { work_total += wt; if (wt > work_max) work_max = wt; }
        if (opt_frames > 0 && frame_count > 10 && wt > 0.1) SDL_Log("slow frame %d: %.1f ms", frame_count, wt * 1000);
    }
    if (opt_frames > 0 && frame_count > 10)
        SDL_Log("frames %d, avg draw+present %.2f ms, max %.2f ms", frame_count, work_total * 1000 / (frame_count - 10), work_max * 1000);
    if (opt_frames > 0) {
        char ml[512] = "";
        size_t l = 0;
        for (int i = 0; i < MOD_COUNT; i++)
            if (R.mods[i]) l += snprintf(ml + l, sizeof(ml) - l, " %s%d", MODS[i].key, R.mods[i]);
        SDL_Log("run: sector %d phase %d salvage %d lives %d score %d continue %d draft_taken %d mods%s", R.layer, R.phase, R.salvage, G.lives, G.score,
                has_campaign, R.draft_taken, ml);
        int eb = 0;
        for (int i = 0; i < MAX_PROJ; i++) eb += W.proj[i].active && IS_ENEMY_PROJ(W.proj[i].type);
        SDL_Log("mine: %dx%d tiles, reactor hp %.0f/%.0f phase %d/%d pylons %d, robots %d/%d, enemy bullets %d, grazes %d, traps %d, countdown %.1f",
                L.w, L.h, W.reactor.hp, W.reactor.maxhp, W.reactor.phase + 1, W.reactor.nphases, W.reactor.pylons, G.robots_killed, G.robots_total, eb,
                W.grazes, W.ntraps, G.countdown_max);
        if (W.boss_idx >= 0) SDL_Log("boss: hp %.0f/%.0f phase %d active %d", W.rob[W.boss_idx].hp, W.rob[W.boss_idx].maxhp, W.rob[W.boss_idx].phase + 1, W.rob[W.boss_idx].active);
    }
    config_save();
    game_shutdown_level();
    audio_shutdown();
    render_shutdown();
    grid_free();
    if (pad) SDL_CloseGamepad(pad);
    SDL_free(pref_path);
    SDL_DestroyRenderer(g_ren);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    return 0;
}
