/*
 * Menu animation for the immediate-mode screens. A screen asks for a value by
 * id every frame it draws - a hover fading in, a cursor springing after the
 * selection, a press flashing and dying away - and the value keeps its state
 * here between frames, stepped by the real time since it was last asked for.
 * A new screen forgets them all, so it starts at rest.
 */
#include "common.h"

#define UI_SLOTS 512
#define PRESS_LEN 0.45f

typedef struct {
    uint32_t id;
    bool used, live;  /* live: v holds a value */
    float v, vel;
    float t;          /* g_time when v was last stepped */
    float press;      /* g_time of the last press, < 0 none */
} Slot;
static Slot slots[UI_SLOTS];

static Slot *slot(uint32_t id) {
    uint32_t h = hash32(id) & (UI_SLOTS - 1);
    for (int k = 0; k < UI_SLOTS; k++) {
        Slot *s = &slots[(h + k) & (UI_SLOTS - 1)];
        if (!s->used) {
            memset(s, 0, sizeof(*s));
            s->used = true;
            s->id = id;
            s->press = -1;
            return s;
        }
        if (s->id == id) return s;
    }
    return &slots[h]; /* full: two values share a slot, the worst is a twitch */
}

static float step_dt(Slot *s) {
    float dt = clampf(g_time - s->t, 0, 0.1f);
    s->t = g_time;
    return dt;
}

void ui_reset(void) { memset(slots, 0, sizeof(slots)); }

float ui_ease(uint32_t id, float target, float rate) {
    Slot *s = slot(id);
    float dt = step_dt(s);
    if (!s->live) {
        s->live = true;
        s->v = target;
        return target;
    }
    s->v = lerpf(s->v, target, damp_factor(rate, dt));
    return s->v;
}

float ui_spring(uint32_t id, float target, float k, float damp) {
    Slot *s = slot(id);
    float dt = step_dt(s);
    if (!s->live) {
        s->live = true;
        s->v = target;
        s->vel = 0;
        return target;
    }
    /* small semi-implicit steps keep stiff springs stable at any frame rate */
    int n = (int)ceilf(dt * 240);
    float h = n > 0 ? dt / n : 0;
    for (int i = 0; i < n; i++) {
        s->vel += ((target - s->v) * k - s->vel * damp) * h;
        s->v += s->vel * h;
    }
    return s->v;
}

void ui_snap(uint32_t id, float v) {
    Slot *s = slot(id);
    s->live = true;
    s->v = v;
    s->vel = 0;
    s->t = g_time;
}

void ui_press(uint32_t id) { slot(id)->press = g_time; }

float ui_pressed(uint32_t id) {
    Slot *s = slot(id);
    if (s->press < 0) return 0;
    float a = (g_time - s->press) / PRESS_LEN;
    return a >= 1 || a < 0 ? 0 : (1 - a) * (1 - a);
}

float ui_shake(uint32_t id) {
    float p = ui_pressed(id);
    return p > 0 ? sinf((1 - sqrtf(p)) * TAU * 3.5f) * 9 * sqrtf(p) : 0;
}
