/*
 * NEON DESCENT - entry point, application state machine, menus and screens.
 */
#include "game_internal.h"
#include <SDL3/SDL_main.h>

float g_time;
bool g_gamepad_connected;
bool g_fx_collide = true;

typedef enum {
    S_TITLE, S_DIFFICULTY, S_STORY, S_BRIEFING, S_PLAY, S_PAUSE, S_TALLY,
    S_GAMEOVER, S_VICTORY, S_NAME, S_SCORES, S_OPTIONS, S_HELP
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

/* debug / test options */
static int opt_frames = -1;
static const char *opt_shot = NULL;
static bool opt_reveal = false;
static bool opt_bot = false, opt_god = false, opt_destroy = false, opt_automap = false, opt_allweapons = false;
static int opt_tp_x = -1, opt_tp_y = -1;
static int frame_count = 0;

typedef struct {
    bool up, down, left, right, confirm, back;
    bool click;
    V2 mouse;
    bool mouse_moved;
    float wheel;
} UIInput;
static UIInput ui;

/* ------------------------------------------------------------ persistence */
typedef struct { char name[16]; int score; int level; int diff; } Score;
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
        while (nscores < 10) {
            Score s;
            memset(&s, 0, sizeof(s));
            if (fscanf(f, "%15s %d %d %d", s.name, &s.score, &s.level, &s.diff) != 4) break;
            for (char *c = s.name; *c; c++)
                if (*c == '_') *c = ' ';
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
            scores[i].level = 3 - i / 2;
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
        fprintf(f, "%s %d %d %d\n", n, scores[i].score, scores[i].level, scores[i].diff);
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
    snprintf(scores[rank].name, sizeof(scores[rank].name), "%s", name[0] ? name : "PILOT");
    scores[rank].score = G.score;
    scores[rank].level = G.level + 1;
    scores[rank].diff = G.difficulty;
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

static void bg_update(float dt) {
    if (!bg_ready) bg_init();
    bg_boom_t -= dt;
    if (bg_boom_t <= 0) {
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

static void bg_draw(float dim) {
    grid_draw(rgba(0.18f, 0.2f, 0.7f, 1), dim);
    for (int i = 0; i < 7; i++) {
        Flyer *f = &flyers[i];
        const RobotDef *d = &RDEF[f->type];
        r_shape(&RSHAPE[f->type], f->pos, f->ang, f->scale, 3.5f, col_a(d->col, 0.28f * dim));
    }
    fx_draw();
}

/* ------------------------------------------------------------ menus */
static int menu_update(int n, float y0, float spacing, float size, const float *widths) {
    if (ui.up) { menu_sel = (menu_sel - 1 + n) % n; snd_play(SND_MENU_MOVE, 0.5f, 1.0f); }
    if (ui.down) { menu_sel = (menu_sel + 1) % n; snd_play(SND_MENU_MOVE, 0.5f, 1.0f); }
    int hover = -1;
    for (int i = 0; i < n; i++) {
        float w = widths ? widths[i] : 300;
        float y = y0 + i * spacing;
        if (fabsf(ui.mouse.x - g_virt_w * 0.5f) < w * 0.5f + 20 && ui.mouse.y > y - 6 && ui.mouse.y < y + size + 6) hover = i;
    }
    if (hover >= 0 && ui.mouse_moved && hover != menu_sel) { menu_sel = hover; snd_play(SND_MENU_MOVE, 0.4f, 1.0f); }
    if (ui.confirm || (ui.click && hover >= 0)) {
        if (ui.click && hover >= 0) menu_sel = hover;
        snd_play(SND_MENU_SEL, 0.6f, 1.0f);
        return menu_sel;
    }
    return -1;
}

static void menu_draw(const char **items, int n, float y0, float spacing, float size, float *widths_out) {
    float cx = g_virt_w * 0.5f;
    for (int i = 0; i < n; i++) {
        float y = y0 + i * spacing;
        bool sel = i == menu_sel;
        float w = text_width(items[i], size);
        if (widths_out) widths_out[i] = w;
        Col c = sel ? rgba(1, 1, 1, 1) : rgba(0.45f, 0.6f, 0.85f, 0.85f);
        if (sel) {
            float pulse = 0.5f + 0.5f * sinf(g_time * 6);
            r_text_glow(items[i], cx, y, size, rgba(0.6f, 0.95f, 1, 1), AL_CENTER);
            float bx = w * 0.5f + 22 + pulse * 5;
            V2 l[3] = {v2(cx - bx + 8, y - 2), v2(cx - bx, y + size * 0.5f), v2(cx - bx + 8, y + size + 2)};
            V2 r[3] = {v2(cx + bx - 8, y - 2), v2(cx + bx, y + size * 0.5f), v2(cx + bx - 8, y + size + 2)};
            r_polyline(l, 3, false, 4, C_MAGENTA);
            r_polyline(r, 3, false, 4, C_MAGENTA);
        } else {
            r_text(items[i], cx, y, size, c, AL_CENTER);
        }
    }
}

static float menu_widths[16];

static void draw_logo(float y, float alpha) {
    float cx = g_virt_w * 0.5f;
    float t = g_time;
    Col c1 = col_a(rgba(0.3f, 0.9f, 1, 1), alpha);
    Col c2 = col_a(col_lerp(rgba(1, 0.3f, 0.85f, 1), rgba(0.7f, 0.4f, 1, 1), 0.5f + 0.5f * sinf(t * 0.8f)), alpha);
    r_text_glow("N E O N", cx, y, 30, c1, AL_CENTER);
    r_text_glow("DESCENT", cx, y + 50, 96, c2, AL_CENTER);
    float w = text_width("DESCENT", 96) * 0.5f + 30;
    r_line(v2(cx - w, y + 170), v2(cx + w, y + 170), 4, col_a(c1, 0.7f));
    r_glow(v2(cx - w + fmodf(t * 300, w * 2), y + 170), 22, col_a(C_WHITE, 0.35f * alpha));
}

static void hint(const char *s) { r_text(s, g_virt_w * 0.5f, VIRT_H - 36, 11, rgba(0.5f, 0.65f, 0.9f, 0.75f), AL_CENTER); }

/* ------------------------------------------------------------ planet */
static void draw_planet(V2 c, float R, float rot, Col col) {
    const float tilt = 0.38f;
    float ct = cosf(tilt), st = sinf(tilt);
    V2 pts[64];
    Col cols[64];
    r_glow(c, R * 1.8f, col_a(col, 0.12f));
    for (int lat = -60; lat <= 60; lat += 30) {
        float la = lat * PI / 180;
        int n = 48;
        for (int i = 0; i < n; i++) {
            float lo = TAU * i / n;
            float x = cosf(la) * cosf(lo + rot), y = sinf(la), z = cosf(la) * sinf(lo + rot);
            float y2 = y * ct - z * st, z2 = y * st + z * ct;
            pts[i] = v2(c.x + x * R, c.y + y2 * R);
            cols[i] = col_a(col, z2 > 0 ? 0.85f : 0.18f);
        }
        r_polyline_cols(pts, cols, n, true, 3);
    }
    for (int k = 0; k < 12; k++) {
        float lo = TAU * k / 12 + rot;
        int n = 25;
        for (int i = 0; i < n; i++) {
            float la = -PI / 2 + PI * i / (n - 1);
            float x = cosf(la) * cosf(lo), y = sinf(la), z = cosf(la) * sinf(lo);
            float y2 = y * ct - z * st, z2 = y * st + z * ct;
            pts[i] = v2(c.x + x * R, c.y + y2 * R);
            cols[i] = col_a(col, z2 > 0 ? 0.7f : 0.12f);
        }
        r_polyline_cols(pts, cols, n, false, 2.5f);
    }
    r_circle(c, R, 5, col, 64);
    /* orbiting ship marker */
    float oa = g_time * 0.8f;
    V2 sp = v2(c.x + cosf(oa) * R * 1.35f, c.y + sinf(oa) * R * 0.45f);
    if (sinf(oa) > -0.2f || cosf(oa) * cosf(oa) > 0.6f) game_draw_ship_icon(sp, oa + PI / 2, 7, C_WHITE);
}

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
    "You are a contract pilot flying the WRAITH-7 assault\n"
    "craft. Fly into the infected mines, destroy their\n"
    "reactors, rescue the trapped crews and get out before\n"
    "each mine tears itself apart.\n"
    "\n"
    "The pay is excellent. The survival rate is not.";

static const char *ENDING =
    "The Overseer is scrap. Across the solar system the\n"
    "infected robots fall silent one by one.\n"
    "\n"
    "The Consortium thanks you for your service - and for\n"
    "the miners you brought home alive.\n"
    "\n"
    "But deep-space listening posts have picked up a new\n"
    "signal from beyond the orbit of Neptune...";

/* ------------------------------------------------------------ enter state */
static float tally_t = 0;
static int tally_line = 0;

static void enter_state(AppState s) {
    AppState prev = state;
    if (opt_frames > 0) SDL_Log("state %d -> %d (frame %d) score=%d lives=%d saved=%d level=%d", (int)prev, (int)s, frame_count, G.score, G.lives, G.hostages_saved, G.level);
    state = s;
    state_t = 0;
    menu_sel = 0;
    switch (s) {
    case S_TITLE:
        game_shutdown_level();
        bg_init();
        music_play(SONG_MENU);
        break;
    case S_DIFFICULTY:
        menu_sel = 2;
        break;
    case S_STORY:
        music_play(SONG_BRIEF);
        break;
    case S_BRIEFING:
        game_shutdown_level();
        bg_init();
        music_play(SONG_BRIEF);
        break;
    case S_PLAY:
        if (prev == S_BRIEFING || prev == S_TITLE || prev == S_DIFFICULTY || prev == S_STORY) {
            g_fx_collide = true;
            bg_ready = false;
            game_start_level(G.level, retry_level || G.level_score_start < 0);
            retry_level = false;
            if (opt_god) W.pl.spawn_inv = 1e9f;
            if (opt_allweapons) {
                W.pl.owned = 0x1F;
                W.pl.vulcan_ammo = 5000;
                W.pl.laser_level = 4;
                W.pl.quad = true;
                for (int i = 0; i < SW_COUNT; i++) W.pl.missiles[i] = 5;
                W.pl.keys = 7;
            }
            if (opt_tp_x >= 0) {
                W.pl.pos = tile_center(opt_tp_x, opt_tp_y);
                g_cam.pos = W.pl.pos;
                opt_tp_x = -1;
            }
            if (opt_destroy) {
                if (W.reactor.exists) reactor_damage(1e9f);
                else if (W.boss_idx >= 0) robot_damage(&W.rob[W.boss_idx], 1e9f, v2(1, 0), true);
                opt_destroy = false;
            }
            if (opt_automap) G.automap = true;
            if (opt_reveal) memset(L.explored, 1, sizeof(L.explored));
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
        music_play(SONG_BRIEF);
        SDL_ShowCursor();
        break;
    case S_GAMEOVER:
        game_shutdown_level();
        music_play(SONG_GAMEOVER);
        SDL_ShowCursor();
        bg_init();
        break;
    case S_VICTORY:
        game_shutdown_level();
        music_play(SONG_VICTORY);
        bg_init();
        SDL_ShowCursor();
        break;
    case S_NAME:
        name_buf[0] = 0;
        SDL_StartTextInput(g_window);
        break;
    case S_SCORES:
        if (prev != S_NAME) new_score_rank = -1;
        SDL_StopTextInput(g_window);
        if (prev == S_GAMEOVER || prev == S_VICTORY || prev == S_NAME) music_play(SONG_MENU);
        break;
    case S_OPTIONS:
    case S_HELP:
        help_page = 0;
        break;
    }
    if (s != S_PLAY) SDL_ShowCursor();
}

/* ------------------------------------------------------------ screens */
static void title_update(float dt) {
    bg_update(dt);
    static const char *items[5] = {"NEW GAME", "HOW TO PLAY", "HIGH SCORES", "OPTIONS", "QUIT"};
    (void)items;
    int r = menu_update(5, 400, 46, 22, menu_widths);
    switch (r) {
    case 0: goto_state(S_DIFFICULTY); break;
    case 1: return_state = S_TITLE; goto_state(S_HELP); break;
    case 2: goto_state(S_SCORES); break;
    case 3: return_state = S_TITLE; goto_state(S_OPTIONS); break;
    case 4: running = false; break;
    }
    if (ui.back && state_t > 0.3f) running = false;
}

static void title_draw(void) {
    bg_draw(1.0f);
    draw_logo(120, clampf(state_t * 1.5f, 0, 1));
    r_text("INFILTRATE.  RESCUE.  DESTROY.  ESCAPE.", g_virt_w * 0.5f, 320, 13, rgba(0.6f, 0.75f, 1, 0.8f), AL_CENTER);
    static const char *items[5] = {"NEW GAME", "HOW TO PLAY", "HIGH SCORES", "OPTIONS", "QUIT"};
    menu_draw(items, 5, 400, 46, 22, menu_widths);
    r_text("A TOP-DOWN TRIBUTE TO DESCENT (1995) IN THE STYLE OF GEOMETRY WARS", g_virt_w * 0.5f, VIRT_H - 60, 9,
           rgba(0.45f, 0.55f, 0.8f, 0.6f), AL_CENTER);
    hint(g_gamepad_connected ? "D-PAD SELECT   A CONFIRM   B BACK" : "ARROWS / MOUSE SELECT   ENTER CONFIRM   ESC QUIT");
}

static const char *DIFF_DESC[5] = {
    "Forgiving robots and 50 seconds to escape. For your first descent.",
    "A gentle challenge. Robots rarely aim ahead of you.",
    "The intended experience. Robots lead their shots.",
    "Deadly accurate robots and only 35 seconds to escape.",
    "No mercy. Robots hit hard, fire fast and you get 30 seconds.",
};

static void difficulty_update(float dt) {
    bg_update(dt);
    int r = menu_update(5, 250, 50, 24, menu_widths);
    if (r >= 0) {
        game_new(r);
        G.level = 0;
        G.level_score_start = -1;
        goto_state(S_STORY);
    }
    if (ui.back) goto_state(S_TITLE);
}

static void difficulty_draw(void) {
    bg_draw(0.7f);
    r_text_glow("SELECT DIFFICULTY", g_virt_w * 0.5f, 130, 36, rgba(0.4f, 0.9f, 1, 1), AL_CENTER);
    menu_draw(DIFF_NAMES, 5, 250, 50, 24, menu_widths);
    r_text(DIFF_DESC[menu_sel], g_virt_w * 0.5f, 530, 13, rgba(0.8f, 0.85f, 1, 0.9f), AL_CENTER);
    hint("ENTER CONFIRM   ESC BACK");
}

static void story_update(float dt) {
    bg_update(dt);
    if (ui.confirm || ui.click) {
        if (!typewriter_done(STORY, state_t, 55)) state_t = 1000;
        else goto_state(S_BRIEFING);
    }
    if (ui.back) goto_state(S_BRIEFING);
}

static void story_draw(void) {
    bg_draw(0.5f);
    r_panel(g_virt_w * 0.5f - 420, 100, 840, 520, rgba(0.3f, 0.8f, 1, 1), 0.75f);
    r_text_glow("INCOMING TRANSMISSION", g_virt_w * 0.5f, 126, 22, rgba(0.4f, 0.9f, 1, 1), AL_CENTER);
    typewriter(STORY, g_virt_w * 0.5f - 380, 185, 14, rgba(0.85f, 0.92f, 1, 1), state_t, 55);
    if (typewriter_done(STORY, state_t, 55) && fmodf(g_time * 2, 1) < 0.7f) hint("PRESS FIRE TO CONTINUE");
}

static void briefing_update(float dt) {
    bg_update(dt);
    const char *txt = game_level_briefing(G.level);
    if (ui.confirm || ui.click) {
        if (!typewriter_done(txt, state_t - 0.6f, 70)) state_t = 1000;
        else goto_state(S_PLAY);
    }
    if (ui.back) goto_state(S_PLAY);
}

static void briefing_draw(void) {
    bg_draw(0.4f);
    const LevelDef *d = &LEVELS[G.level];
    float VW = g_virt_w;
    float cx = VW * 0.5f;
    r_textf(cx, 40, 13, col_a(d->accent, 0.8f), AL_CENTER, retry_level ? "MINE %d OF %d  -  SECOND ATTEMPT" : "MINE %d OF %d", G.level + 1, NUM_LEVELS);
    r_text_glow(d->name, cx, 64, 40, d->wall, AL_CENTER);
    r_text(d->subtitle, cx, 122, 12, col_a(d->accent, 0.8f), AL_CENTER);
    float px = cx - 360;
    draw_planet(v2(px, 330), 140, g_time * 0.35f, d->wall);
    r_panel(cx - 185, 170, 630, 330, d->wall, 0.7f);
    typewriter(d->briefing, cx - 158, 195, 12.5f, rgba(0.88f, 0.93f, 1, 1), state_t - 0.6f, 70);
    r_text("KNOWN THREATS", cx, 520, 12, col_a(d->accent, 0.8f), AL_CENTER);
    int types[6];
    int n = game_level_threats(G.level, types, 6);
    for (int i = 0; i < n; i++) {
        float x = cx + (i - (n - 1) * 0.5f) * 210;
        robot_draw_preview(types[i], v2(x, 580), g_time * 0.8f + i, types[i] == RB_BOSS ? 26 : 18, g_time);
        r_text(RDEF[types[i]].name, x, 618, 10, col_a(RDEF[types[i]].col, 0.9f), AL_CENTER);
    }
    if (fmodf(g_time * 2, 1) < 0.7f) hint(typewriter_done(d->briefing, state_t - 0.6f, 70) ? "PRESS FIRE TO LAUNCH" : "PRESS FIRE TO SKIP");
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
    if (best >= 0) {
        Robot *r = &W.rob[best];
        g_in.aim_world = r->pos;
        g_in.fire1 = true;
        g_in.fire2 = frand() < 0.01f;
        V2 d = v2norm(v2sub(r->pos, p->pos));
        g_in.move = bd > 260 ? d : v2perp(d);
    } else {
        g_in.move = wander;
        g_in.aim_world = v2mad(p->pos, wander, 200);
    }
    g_in.mouse_active = true;
    g_in.use_stick = false;
}

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
        game_automap_draw();
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
    r_text_glow("PAUSED", g_virt_w * 0.5f, 170, 48, rgba(0.4f, 0.9f, 1, 1), AL_CENTER);
    static const char *items[4] = {"RESUME", "HOW TO PLAY", "OPTIONS", "ABORT MISSION"};
    menu_draw(items, 4, 300, 50, 22, menu_widths);
    hint("ESC RESUME");
}

/* ------------------------------------------------------------ tally */
static void tally_update(float dt) {
    tally_t += dt;
    int lines = 9;
    int want = (int)(tally_t / 0.32f);
    if (want > lines) want = lines;
    if (want > tally_line) {
        tally_line = want;
        snd_play(tally_line == lines ? SND_POWERUP : SND_TALLY, 0.6f, 1.0f);
    }
    if (ui.confirm || ui.click) {
        if (tally_line < lines) { tally_t = 100; }
        else {
            if (G.level + 1 < NUM_LEVELS) {
                G.level++;
                goto_state(S_BRIEFING);
            } else goto_state(S_VICTORY);
        }
    }
}

static void tally_draw(void) {
    float cx = g_virt_w * 0.5f;
    const LevelDef *d = &LEVELS[G.level];
    grid_draw(col_mul(d->grid, 0.6f), 0.6f);
    r_panel(cx - 330, 90, 660, 560, d->wall, 0.8f);
    r_textf(cx, 110, 13, col_a(d->accent, 0.8f), AL_CENTER, "MINE %d OF %d", G.level + 1, NUM_LEVELS);
    r_text_glow(d->name, cx, 134, 30, d->wall, AL_CENTER);
    r_text("MINE DESTROYED", cx, 180, 16, C_GREEN, AL_CENTER);
    char v[9][48];
    const char *labels[9] = {"ROBOTS DESTROYED", "HOSTAGES RESCUED", "MISSION TIME", "SHIELD BONUS", "ENERGY BONUS",
                             "HOSTAGE BONUS", "FULL RESCUE BONUS", "SKILL BONUS", "TOTAL SCORE"};
    snprintf(v[0], 48, "%d", G.robots_killed);
    snprintf(v[1], 48, "%d / %d", G.hostages_saved, G.hostages_total);
    snprintf(v[2], 48, "%d:%02d", (int)G.level_time / 60, (int)G.level_time % 60);
    snprintf(v[3], 48, "%d", G.bonus_shield);
    snprintf(v[4], 48, "%d", G.bonus_energy);
    snprintf(v[5], 48, "%d", G.bonus_hostage);
    snprintf(v[6], 48, "%d", G.bonus_full);
    snprintf(v[7], 48, "%d", G.bonus_skill);
    snprintf(v[8], 48, "%d", G.score);
    for (int i = 0; i < tally_line && i < 9; i++) {
        float y = 240 + i * 40 + (i == 8 ? 16 : 0);
        Col c = i == 8 ? C_WHITE : rgba(0.75f, 0.85f, 1, 1);
        float size = i == 8 ? 20 : 15;
        if (i == 8) r_line(v2(cx - 280, y - 14), v2(cx + 280, y - 14), 3, col_a(d->wall, 0.6f));
        r_text(labels[i], cx - 280, y, size, col_a(c, 0.85f), AL_LEFT);
        if (i == 6 && G.bonus_full > 0) c = C_GREEN;
        r_text(v[i], cx + 280, y, size, c, AL_RIGHT);
    }
    if (tally_line >= 9 && fmodf(g_time * 2, 1) < 0.7f)
        hint(G.level + 1 < NUM_LEVELS ? "PRESS FIRE FOR THE NEXT MINE" : "PRESS FIRE TO CONTINUE");
}

/* ------------------------------------------------------------ end screens */
static void after_game(void) {
    new_score_rank = score_rank(G.score);
    if (new_score_rank >= 0) goto_state(S_NAME);
    else goto_state(S_SCORES);
}

static void gameover_update(float dt) {
    bg_update(dt);
    if ((ui.confirm || ui.click || ui.back) && state_t > 1.0f) after_game();
}

static void gameover_draw(void) {
    bg_draw(0.5f);
    float cx = g_virt_w * 0.5f;
    float a = clampf(state_t, 0, 1);
    r_text_glow("GAME OVER", cx, 220, 72, col_a(C_RED, a), AL_CENTER);
    r_textf(cx, 340, 18, col_a(C_WHITE, a), AL_CENTER, "FINAL SCORE  %d", G.score);
    r_textf(cx, 380, 13, col_a(rgba(0.7f, 0.8f, 1, 1), a), AL_CENTER, "REACHED MINE %d - %s", G.level + 1, LEVELS[G.level].name);
    r_textf(cx, 410, 11, col_a(rgba(0.6f, 0.7f, 0.9f, 1), a), AL_CENTER, "DIFFICULTY: %s", DIFF_NAMES[G.difficulty]);
    if (state_t > 1 && fmodf(g_time * 2, 1) < 0.7f) hint("PRESS FIRE TO CONTINUE");
}

static void victory_update(float dt) {
    bg_update(dt);
    if (frand() < dt * 3) {
        V2 at = v2add(g_cam.pos, v2(frandr(-600, 600), frandr(-320, 320)));
        static const Col cols[4] = {{1, 0.8f, 0.3f, 1}, {0.4f, 1, 0.6f, 1}, {1, 0.4f, 0.9f, 1}, {0.4f, 0.8f, 1, 1}};
        fx_explosion(at, 25 + frand() * 25, cols[irand(4)]);
        if (frand() < 0.5f) snd_play(SND_EXPL_S, 0.25f, frandr(1.2f, 1.6f));
    }
    if (ui.confirm || ui.click) {
        if (!typewriter_done(ENDING, state_t - 1.5f, 45)) state_t = 1000;
        else after_game();
    }
}

static void victory_draw(void) {
    bg_draw(0.8f);
    float cx = g_virt_w * 0.5f;
    float a = clampf(state_t, 0, 1);
    r_text_glow("MISSION ACCOMPLISHED", cx, 80, 46, col_a(rgba(0.4f, 1, 0.6f, 1), a), AL_CENTER);
    r_panel(cx - 400, 170, 800, 300, rgba(0.4f, 1, 0.6f, 1), 0.75f);
    typewriter(ENDING, cx - 360, 200, 14, rgba(0.88f, 0.95f, 1, 1), state_t - 1.5f, 45);
    r_textf(cx, 500, 22, C_WHITE, AL_CENTER, "FINAL SCORE  %d", G.score);
    r_textf(cx, 540, 12, rgba(0.7f, 0.85f, 1, 0.9f), AL_CENTER, "DIFFICULTY: %s   -   SHIPS REMAINING: %d", DIFF_NAMES[G.difficulty], G.lives);
    r_text("THANK YOU FOR PLAYING NEON DESCENT", cx, 600, 12, col_a(C_MAGENTA, 0.8f), AL_CENTER);
    if (typewriter_done(ENDING, state_t - 1.5f, 45) && fmodf(g_time * 2, 1) < 0.7f) hint("PRESS FIRE TO CONTINUE");
}

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
    r_text_glow("NEW HIGH SCORE!", cx, 180, 44, C_YELLOW, AL_CENTER);
    r_textf(cx, 260, 18, C_WHITE, AL_CENTER, "%d  -  RANK %d", G.score, new_score_rank + 1);
    r_text("ENTER YOUR NAME", cx, 330, 14, rgba(0.7f, 0.85f, 1, 0.9f), AL_CENTER);
    char buf[32];
    snprintf(buf, sizeof(buf), "%s%s", name_buf, fmodf(g_time * 2, 1) < 0.5f ? "_" : " ");
    r_panel(cx - 220, 370, 440, 70, C_CYAN, 0.7f);
    r_text_glow(buf, cx, 390, 30, C_CYAN, AL_CENTER);
    hint("TYPE YOUR NAME AND PRESS ENTER");
}

static void scores_update(float dt) {
    bg_update(dt);
    if (ui.confirm || ui.back || ui.click) goto_state(S_TITLE);
}

static void scores_draw(void) {
    bg_draw(0.6f);
    float cx = g_virt_w * 0.5f;
    r_text_glow("HIGH SCORES", cx, 70, 40, rgba(0.4f, 0.9f, 1, 1), AL_CENTER);
    r_panel(cx - 380, 140, 760, 480, rgba(0.3f, 0.8f, 1, 1), 0.7f);
    r_text("RANK", cx - 330, 165, 11, rgba(0.6f, 0.7f, 0.9f, 0.8f), AL_LEFT);
    r_text("PILOT", cx - 230, 165, 11, rgba(0.6f, 0.7f, 0.9f, 0.8f), AL_LEFT);
    r_text("MINE", cx + 90, 165, 11, rgba(0.6f, 0.7f, 0.9f, 0.8f), AL_CENTER);
    r_text("SKILL", cx + 190, 165, 11, rgba(0.6f, 0.7f, 0.9f, 0.8f), AL_CENTER);
    r_text("SCORE", cx + 330, 165, 11, rgba(0.6f, 0.7f, 0.9f, 0.8f), AL_RIGHT);
    for (int i = 0; i < nscores; i++) {
        float y = 205 + i * 40;
        bool hi = i == new_score_rank;
        Col c = hi ? (fmodf(g_time * 3, 1) < 0.6f ? C_YELLOW : C_WHITE) : (i == 0 ? rgba(1, 0.85f, 0.4f, 1) : rgba(0.8f, 0.9f, 1, 1));
        r_textf(cx - 330, y, 15, c, AL_LEFT, "%d", i + 1);
        r_text(scores[i].name, cx - 230, y, 15, c, AL_LEFT);
        r_textf(cx + 90, y, 15, c, AL_CENTER, "%d", scores[i].level);
        r_text(DIFF_NAMES[clampi(scores[i].diff, 0, 4)], cx + 190, y, 11, col_a(c, 0.8f), AL_CENTER);
        r_textf(cx + 330, y, 15, c, AL_RIGHT, "%d", scores[i].score);
    }
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
    if (ui.left && menu_sel < NOPT - 1) { option_change(menu_sel, -1); snd_play(SND_MENU_MOVE, 0.5f, 0.9f); }
    if (ui.right && menu_sel < NOPT - 1) { option_change(menu_sel, 1); snd_play(SND_MENU_MOVE, 0.5f, 1.1f); }
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
    r_text_glow("OPTIONS", g_virt_w * 0.5f, 90, 40, rgba(0.4f, 0.9f, 1, 1), AL_CENTER);
    static char items[NOPT][64];
    options_items(items);
    const char *ptrs[NOPT];
    for (int i = 0; i < NOPT; i++) ptrs[i] = items[i];
    menu_draw(ptrs, NOPT, 190, 50, 18, menu_widths);
    static const char *desc[NOPT] = {
        "", "", "ALT+ENTER OR F11 ALSO TOGGLES FULLSCREEN", "",
        "TWIN STICK: WASD MOVES ON SCREEN AXES.  SHIP RELATIVE: W THRUSTS TOWARD THE CURSOR.",
        "DISABLE ON SLOW MACHINES", "SWITCH TO A BETTER WEAPON WHEN YOU PICK IT UP", ""};
    r_text(desc[menu_sel], g_virt_w * 0.5f, 600, 11, rgba(0.7f, 0.8f, 1, 0.8f), AL_CENTER);
    hint("LEFT / RIGHT CHANGE   ESC BACK");
}

/* ------------------------------------------------------------ help */
static void help_update(float dt) {
    if (return_state != S_PAUSE) bg_update(dt);
    if (ui.left || ui.up) { help_page = (help_page + 2) % 3; snd_play(SND_MENU_MOVE, 0.5f, 1.0f); }
    if (ui.right || ui.down || ui.confirm || ui.click) {
        if ((ui.confirm || ui.click) && help_page == 2) { goto_state(return_state); return; }
        help_page = (help_page + 1) % 3;
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
    static const char *titles[3] = {"CONTROLS", "HOW TO PLAY", "POWERUPS"};
    r_text_glow(titles[help_page], cx, 50, 36, rgba(0.4f, 0.9f, 1, 1), AL_CENTER);
    r_textf(cx, 100, 11, rgba(0.6f, 0.7f, 0.9f, 0.8f), AL_CENTER, "PAGE %d OF 3", help_page + 1);
    r_panel(cx - 520, 125, 1040, 530, rgba(0.3f, 0.8f, 1, 1), 0.75f);
    Col kc = C_CYAN, gc = C_MAGENTA;
    if (help_page == 0) {
        float x = cx - 250, y = 150;
        r_text("KEYBOARD + MOUSE", x - 110, y, 14, kc, AL_LEFT);
        y += 36;
        help_row(x, y, "WASD / ARROWS", "FLY", kc); y += 28;
        help_row(x, y, "MOUSE", "AIM", kc); y += 28;
        help_row(x, y, "LEFT BUTTON / CTRL", "FIRE PRIMARY WEAPON", kc); y += 28;
        help_row(x, y, "RIGHT BUTTON / SPACE", "FIRE SECONDARY WEAPON", kc); y += 28;
        help_row(x, y, "SHIFT", "AFTERBURNER", kc); y += 28;
        help_row(x, y, "1 - 5 / Q / WHEEL", "SELECT PRIMARY WEAPON", kc); y += 28;
        help_row(x, y, "6 - 0 / E", "SELECT SECONDARY WEAPON", kc); y += 28;
        help_row(x, y, "F", "DROP PROXIMITY BOMB", kc); y += 28;
        help_row(x, y, "TAB / M", "AUTOMAP", kc); y += 28;
        help_row(x, y, "ESC / P", "PAUSE", kc); y += 28;
        float x2 = cx + 285;
        y = 150;
        r_text("GAMEPAD", x2 - 110, y, 14, gc, AL_LEFT);
        y += 36;
        help_row(x2, y, "LEFT STICK", "FLY", gc); y += 28;
        help_row(x2, y, "RIGHT STICK", "AIM", gc); y += 28;
        help_row(x2, y, "RIGHT TRIGGER", "FIRE PRIMARY", gc); y += 28;
        help_row(x2, y, "LEFT TRIGGER", "FIRE SECONDARY", gc); y += 28;
        help_row(x2, y, "A / LB", "AFTERBURNER", gc); y += 28;
        help_row(x2, y, "Y", "NEXT PRIMARY", gc); y += 28;
        help_row(x2, y, "RB", "NEXT SECONDARY", gc); y += 28;
        help_row(x2, y, "X / B", "DROP PROXIMITY BOMB", gc); y += 28;
        help_row(x2, y, "BACK", "AUTOMAP", gc); y += 28;
        help_row(x2, y, "START", "PAUSE", gc); y += 28;
    } else if (help_page == 1) {
        static const char *text =
            "EACH MINE HAS A REACTOR CORE. FIND THE COLOURED ACCESS KEYS TO OPEN\n"
            "LOCKED DOORS, THEN DESTROY THE REACTOR. ITS DEATH STARTS A SELF-DESTRUCT\n"
            "COUNTDOWN - FOLLOW THE GREEN ARROWS TO THE EXIT TUNNEL BEFORE IT BLOWS.\n"
            "\n"
            "RESCUE THE TRAPPED MINERS (SOS) BY FLYING INTO THEM. THEY ONLY COUNT\n"
            "WHEN YOU ESCAPE. IF YOUR SHIP IS DESTROYED, THE HOSTAGES ON BOARD DIE\n"
            "AND YOUR WEAPONS ARE LEFT BEHIND - FLY BACK AND COLLECT THEM.\n"
            "\n"
            "SHIELDS ARE YOUR LIFE. ENERGY POWERS YOUR GUNS AND IS REFILLED AT\n"
            "YELLOW ENERGY CENTERS. THE VULCAN CANNON USES AMMO INSTEAD.\n"
            "\n"
            "KILL ROBOTS QUICKLY TO BUILD A CHAIN MULTIPLIER UP TO X8.\n"
            "SHOOT DOWN ENEMY MISSILES AND MINES. LOOK FOR CRACKED WALLS -\n"
            "SECRET AREAS HIDE RARE EQUIPMENT. ROBOT GENERATORS (PINK PADS)\n"
            "KEEP PRODUCING ROBOTS - ESPECIALLY AFTER THE REACTOR GOES DOWN.";
        r_text(text, cx - 480, 150, 12.5f, rgba(0.88f, 0.93f, 1, 1), AL_LEFT);
    } else {
        static const int types[18] = {PU_SHIELD, PU_ENERGY, PU_LASER, PU_QUAD, PU_VULCAN, PU_VAMMO, PU_SPREAD, PU_PLASMA, PU_FUSION,
                                      PU_CONC, PU_HOMING, PU_PROX, PU_SMART, PU_MEGA, PU_CLOAK, PU_INVULN, PU_LIFE, PU_KEY_BLUE};
        static const char *desc[18] = {"+SHIELD", "+ENERGY", "STRONGER LASERS (MAX 4)", "FOUR LASER BOLTS", "RAPID FIRE, USES AMMO",
                                       "600 VULCAN ROUNDS", "THREE-WAY ENERGY FAN", "FAST ENERGY BOLTS", "HOLD TO CHARGE, PIERCES",
                                       "DUMB-FIRE MISSILES", "SEEK THEIR TARGET", "DROPPED BEHIND YOU", "BURSTS INTO SEEKERS",
                                       "HUGE BLAST RADIUS", "30S INVISIBILITY", "30S INVULNERABLE", "ONE MORE SHIP", "OPENS MATCHING DOORS"};
        for (int i = 0; i < 18; i++) {
            int col = i / 6, row = i % 6;
            float x = cx - 440 + col * 330, y = 175 + row * 74;
            draw_powerup_icon(types[i], v2(x, y + 8), 1.2f, g_time, 1);
            r_text(powerup_name(types[i]), x + 34, y - 2, 12, powerup_color(types[i]), AL_LEFT);
            r_text(desc[i], x + 34, y + 18, 10, rgba(0.8f, 0.85f, 1, 0.85f), AL_LEFT);
        }
    }
    hint("LEFT / RIGHT CHANGE PAGE   ESC BACK");
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
            G.automap = true;
            G.automap_zoom = 1;
            G.automap_pan = v2(0, 0);
            game_audio_pause(true);
            snd_play(SND_MENU_SEL, 0.4f, 1.3f);
        }
        break;
    case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4: case SDLK_5: g_in.select = (int)(k - SDLK_1); break;
    case SDLK_6: case SDLK_7: case SDLK_8: case SDLK_9: g_in.select = 5 + (int)(k - SDLK_6); break;
    case SDLK_0: g_in.select = 9; break;
    case SDLK_Q: g_in.cycle_p = 1; break;
    case SDLK_E: g_in.cycle_s = 1; break;
    case SDLK_F: g_in.drop_bomb = true; break;
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
            if (!W.pl.dead) { G.automap = true; G.automap_zoom = 1; G.automap_pan = v2(0, 0); game_audio_pause(true); }
            break;
        case SDL_GAMEPAD_BUTTON_NORTH: g_in.cycle_p = 1; break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: g_in.cycle_s = 1; break;
        case SDL_GAMEPAD_BUTTON_WEST:
        case SDL_GAMEPAD_BUTTON_EAST: g_in.drop_bomb = true; break;
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
            }
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
                else if (e.wheel.y != 0) g_in.cycle_p = e.wheel.y > 0 ? -1 : 1;
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
        memset(&ui.up, 0, sizeof(bool) * 7);
        ui.mouse_moved = false;
        return;
    }
    switch (state) {
    case S_TITLE: title_update(dt); break;
    case S_DIFFICULTY: difficulty_update(dt); break;
    case S_STORY: story_update(dt); break;
    case S_BRIEFING: briefing_update(dt); break;
    case S_PLAY: play_update(dt); break;
    case S_PAUSE: pause_update(dt); break;
    case S_TALLY: tally_update(dt); break;
    case S_GAMEOVER: gameover_update(dt); break;
    case S_VICTORY: victory_update(dt); break;
    case S_NAME: name_update(dt); break;
    case S_SCORES: scores_update(dt); break;
    case S_OPTIONS: options_update(dt); break;
    case S_HELP: help_update(dt); break;
    }
    /* one-shot UI events are consumed by the first simulation step */
    memset(&ui.up, 0, sizeof(bool) * 7);
    ui.mouse_moved = false;
}

static void draw(void) {
    switch (state) {
    case S_TITLE: title_draw(); break;
    case S_DIFFICULTY: difficulty_draw(); break;
    case S_STORY: story_draw(); break;
    case S_BRIEFING: briefing_draw(); break;
    case S_PLAY: play_draw(); break;
    case S_PAUSE: pause_draw(); break;
    case S_TALLY: tally_draw(); break;
    case S_GAMEOVER: gameover_draw(); break;
    case S_VICTORY: victory_draw(); break;
    case S_NAME: name_draw(); break;
    case S_SCORES: scores_draw(); break;
    case S_OPTIONS: options_draw(); break;
    case S_HELP: help_draw(); break;
    }
    if (fade > 0.001f) r_fill_rect(0, 0, g_virt_w, VIRT_H, rgba(0, 0, 0, fade));
}

static AppState parse_state(const char *s) {
    if (!strcmp(s, "title")) return S_TITLE;
    if (!strcmp(s, "difficulty")) return S_DIFFICULTY;
    if (!strcmp(s, "story")) return S_STORY;
    if (!strcmp(s, "briefing")) return S_BRIEFING;
    if (!strcmp(s, "tally")) return S_TALLY;
    if (!strcmp(s, "gameover")) return S_GAMEOVER;
    if (!strcmp(s, "victory")) return S_VICTORY;
    if (!strcmp(s, "scores")) return S_SCORES;
    if (!strcmp(s, "options")) return S_OPTIONS;
    if (!strcmp(s, "help")) return S_HELP;
    if (!strcmp(s, "name")) return S_NAME;
    return S_TITLE;
}

int main(int argc, char **argv) {
    int start_level = -1;
    int start_diff = 2;
    AppState start_state = S_TITLE;
    int start_help_page = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--level") && i + 1 < argc) start_level = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--difficulty") && i + 1 < argc) start_diff = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) opt_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) opt_shot = argv[++i];
        else if (!strcmp(argv[i], "--state") && i + 1 < argc) start_state = parse_state(argv[++i]);
        else if (!strcmp(argv[i], "--page") && i + 1 < argc) start_help_page = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bot")) opt_bot = true;
        else if (!strcmp(argv[i], "--god")) opt_god = true;
        else if (!strcmp(argv[i], "--destroy")) opt_destroy = true;
        else if (!strcmp(argv[i], "--automap")) opt_automap = true;
        else if (!strcmp(argv[i], "--reveal")) opt_reveal = true;
        else if (!strcmp(argv[i], "--allweapons")) opt_allweapons = true;
        else if (!strcmp(argv[i], "--tp") && i + 2 < argc) { opt_tp_x = atoi(argv[++i]); opt_tp_y = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--fullscreen")) g_cfg.fullscreen = true;
    }

    SDL_SetAppMetadata("Neon Descent", "1.0", "com.neondescent.game");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    pref_path = SDL_GetPrefPath("NeonDescent", "NeonDescent");
    config_load();
    scores_load();
    if (!SDL_CreateWindowAndRenderer(GAME_TITLE, 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &g_window, &g_ren)) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(g_window, 800, 450);
    SDL_SetRenderVSync(g_ren, 1);
    if (g_cfg.fullscreen) SDL_SetWindowFullscreen(g_window, true);
    if (!render_init()) {
        SDL_Log("Renderer init failed: %s", SDL_GetError());
        return 1;
    }
    rng_seed((uint32_t)SDL_GetTicks() ^ 0xA5A5F00Du);
    game_init();
    audio_init();
    audio_set_volumes(g_cfg.music_vol, g_cfg.sfx_vol);
    game_new(start_diff);
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

    if (start_level >= 0) {
        G.level = clampi(start_level, 0, NUM_LEVELS - 1);
        G.level_score_start = -1;
        state = S_BRIEFING;
        enter_state(S_PLAY);
        fade = 0;
    } else {
        enter_state(start_state);
        if (start_state == S_TALLY) {
            G.level = 0;
            G.robots_killed = 42; G.hostages_saved = 3; G.hostages_total = 4; G.level_time = 272;
            G.bonus_shield = 1150; G.bonus_energy = 480; G.bonus_hostage = 3000; G.bonus_skill = 3000; G.score = 48210;
            tally_t = 100;
        }
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
