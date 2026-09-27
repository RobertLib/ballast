/*
 * Audio: all sound effects are synthesized at startup, music is a small
 * real-time synthwave sequencer running inside the audio callback.
 */
#include "common.h"

#define RATE 44100

typedef struct { float *d; int n; } Sample;
static Sample S[SND_COUNT];
static float S_gain[SND_COUNT];

typedef struct {
    bool active;
    int snd;
    double pos;
    float rate;
    float vol, tvol;
    float pl, pr;
    bool loop;
    int handle;
    uint32_t age;
    bool paused;
} Voice;

#define NVOICES 48
static Voice voices[NVOICES];
static SDL_Mutex *mtx;
static SDL_AudioStream *stream;
static V2 listener;
static float vol_music = 0.7f, vol_sfx = 0.8f;
static int next_handle = 1;
static uint32_t age_counter;
static bool audio_ok = false;

/* ------------------------------------------------------------ oscillators */
static inline float fr(float ph) { return ph - floorf(ph); }
static inline float o_sq(float ph) { return fr(ph) < 0.5f ? 1.0f : -1.0f; }
static inline float o_pulse(float ph, float duty) { return fr(ph) < duty ? 1.0f : -1.0f; }
static inline float o_saw(float ph) { return 2.0f * fr(ph) - 1.0f; }
static inline float o_tri(float ph) { return 4.0f * fabsf(fr(ph) - 0.5f) - 1.0f; }
static inline float o_sin(float ph) { return sinf(TAU * ph); }
static uint32_t nstate = 12345;
static inline float o_noise(void) {
    nstate = nstate * 1664525u + 1013904223u;
    return ((nstate >> 9) * (1.0f / 4194304.0f)) - 1.0f;
}

static float *alloc_snd(int id, float dur, float gain) {
    int n = (int)(dur * RATE);
    S[id].d = (float *)calloc((size_t)n + 2, sizeof(float));
    S[id].n = n;
    S_gain[id] = gain;
    return S[id].d;
}

static void normalize(int id) {
    float peak = 0;
    for (int i = 0; i < S[id].n; i++) peak = maxf(peak, fabsf(S[id].d[i]));
    if (peak < 1e-5f) return;
    float k = S_gain[id] / peak;
    for (int i = 0; i < S[id].n; i++) S[id].d[i] *= k;
    /* tiny fade-out to avoid clicks */
    int f = S[id].n < 400 ? S[id].n / 4 : 200;
    for (int i = 0; i < f; i++) S[id].d[S[id].n - 1 - i] *= (float)i / f;
}

static void gen_sounds(void) {
    float *d;
    int n;
    float ph, ph2, lp, lp2, t;
#define LOOP(id, dur, gain)              \
    d = alloc_snd(id, dur, gain);        \
    n = S[id].n;                         \
    ph = ph2 = lp = lp2 = 0;             \
    for (int i = 0; i < n; i++, t = (float)i / RATE)
#define T ((float)i / RATE)

    (void)t;
    LOOP(SND_LASER, 0.18f, 0.55f) {
        float f = 300 + 1700 * expf(-T * 20);
        ph += f / RATE;
        float v = 0.6f * o_sq(ph) + 0.4f * o_saw(ph * 0.5f);
        lp += 0.35f * (v - lp);
        d[i] = lp * expf(-T * 18) * minf(1, T * 400);
    }
    normalize(SND_LASER);
    LOOP(SND_LASER2, 0.2f, 0.55f) {
        float f = 380 + 2200 * expf(-T * 16);
        ph += f / RATE;
        ph2 += f * 1.51f / RATE;
        float v = 0.5f * o_sq(ph) + 0.35f * o_saw(ph2);
        lp += 0.4f * (v - lp);
        d[i] = lp * expf(-T * 15) * minf(1, T * 400);
    }
    normalize(SND_LASER2);
    LOOP(SND_VULCAN, 0.1f, 0.5f) {
        float nz = o_noise();
        lp += 0.5f * (nz - lp);
        float f = 150 * expf(-T * 20) + 60;
        ph += f / RATE;
        d[i] = 0.7f * lp * expf(-T * 50) + 0.8f * o_sin(ph) * expf(-T * 35);
    }
    normalize(SND_VULCAN);
    LOOP(SND_SPREAD, 0.22f, 0.5f) {
        float f = 500 + 900 * expf(-T * 14);
        ph += f / RATE;
        d[i] = (o_tri(ph) + 0.3f * o_sq(ph * 2.02f)) * expf(-T * 12) * minf(1, T * 300);
    }
    normalize(SND_SPREAD);
    LOOP(SND_PLASMA, 0.2f, 0.5f) {
        float f = 520 + 600 * expf(-T * 22);
        ph += f / RATE;
        ph2 += f * 2.5f / RATE;
        d[i] = sinf(TAU * ph + 2.2f * o_sin(ph2) * expf(-T * 10)) * expf(-T * 14) * minf(1, T * 300);
    }
    normalize(SND_PLASMA);
    LOOP(SND_FUSION_CHARGE, 1.8f, 0.4f) {
        float k = T / 1.8f;
        float f = 90 + 520 * powf(k, 1.5f);
        ph += f / RATE;
        ph2 += f * 1.007f / RATE;
        float v = 0.5f * o_saw(ph) + 0.5f * o_saw(ph2);
        v *= 0.7f + 0.3f * sinf(TAU * T * (8 + 22 * T));
        lp += (0.12f + 0.25f * k) * (v - lp);
        d[i] = lp * minf(1, T * 8);
    }
    normalize(SND_FUSION_CHARGE);
    LOOP(SND_FUSION, 0.8f, 0.7f) {
        float f = 60 + 700 * expf(-T * 9);
        ph += f / RATE;
        float v = 0.6f * o_saw(ph) + 0.5f * o_noise() * expf(-T * 20);
        lp += 0.3f * (v - lp);
        d[i] = lp * expf(-T * 5) * minf(1, T * 500);
    }
    normalize(SND_FUSION);
    LOOP(SND_MISSILE, 0.6f, 0.5f) {
        float a = 0.05f + 0.3f * expf(-T * 6);
        lp += a * (o_noise() - lp);
        float f = 200 + 100 * expf(-T * 3);
        ph += f / RATE;
        d[i] = (lp + 0.15f * o_sq(ph)) * minf(1, T * 50) * expf(-T * 4);
    }
    normalize(SND_MISSILE);
    LOOP(SND_MEGA, 1.0f, 0.65f) {
        float a = 0.03f + 0.15f * expf(-T * 3);
        lp += a * (o_noise() - lp);
        ph += 90.0f / RATE;
        d[i] = (lp + 0.2f * o_saw(ph)) * minf(1, T * 30) * expf(-T * 2.5f);
    }
    normalize(SND_MEGA);
    LOOP(SND_PROX, 0.3f, 0.35f) {
        float v = 0;
        if (T < 0.08f) v = o_sin(T * 1400) * expf(-T * 30);
        else if (T > 0.14f && T < 0.24f) v = o_sin(T * 1100) * expf(-(T - 0.14f) * 30);
        d[i] = v;
    }
    normalize(SND_PROX);
    LOOP(SND_EXPL_S, 0.6f, 0.7f) {
        float a = 0.15f * expf(-T * 3) + 0.02f;
        lp += a * (o_noise() - lp);
        float f = 90 * expf(-T * 4) + 40;
        ph += f / RATE;
        d[i] = lp * 1.4f * expf(-T * 7) + 0.9f * o_sin(ph) * expf(-T * 9);
    }
    normalize(SND_EXPL_S);
    LOOP(SND_EXPL_M, 1.0f, 0.8f) {
        float a = 0.12f * expf(-T * 2.5f) + 0.015f;
        lp += a * (o_noise() - lp);
        float f = 70 * expf(-T * 3) + 32;
        ph += f / RATE;
        d[i] = lp * 1.6f * expf(-T * 4.5f) + 0.9f * o_sin(ph) * expf(-T * 5) + 0.3f * o_noise() * expf(-T * 40);
    }
    normalize(SND_EXPL_M);
    LOOP(SND_EXPL_L, 2.4f, 0.95f) {
        float a = 0.08f * expf(-T * 1.5f) + 0.01f;
        lp += a * (o_noise() - lp);
        lp2 += 0.02f * (lp - lp2);
        float f = 50 * expf(-T * 1.2f) + 28;
        ph += f / RATE;
        float rumble = 1.0f + 0.5f * sinf(T * 37);
        d[i] = lp * 1.8f * expf(-T * 2.0f) * rumble + lp2 * 3.0f * expf(-T * 1.2f) +
               o_sin(ph) * expf(-T * 2.2f) + 0.4f * o_noise() * expf(-T * 30);
    }
    normalize(SND_EXPL_L);
    LOOP(SND_HIT, 0.08f, 0.35f) {
        d[i] = (0.5f * o_sin(T * 2600) + 0.3f * o_sin(T * 3900) + 0.3f * o_noise()) * expf(-T * 60);
    }
    normalize(SND_HIT);
    LOOP(SND_WALL, 0.06f, 0.25f) {
        lp += 0.3f * (o_noise() - lp);
        d[i] = lp * expf(-T * 70);
    }
    normalize(SND_WALL);
    LOOP(SND_PLAYER_HIT, 0.32f, 0.7f) {
        float f = 110 * expf(-T * 3) + 50;
        ph += f / RATE;
        lp += 0.2f * (o_noise() - lp);
        d[i] = (0.5f * o_sq(ph) + 0.9f * lp) * expf(-T * 12);
    }
    normalize(SND_PLAYER_HIT);
    LOOP(SND_PICKUP, 0.2f, 0.45f) {
        float t0 = T < 0.07f ? 0 : 0.07f;
        float f = T < 0.07f ? 880 : 1320;
        ph += f / RATE;
        d[i] = (o_sin(ph) + 0.25f * o_sq(ph)) * expf(-(T - t0) * 22);
    }
    normalize(SND_PICKUP);
    LOOP(SND_POWERUP, 0.55f, 0.45f) {
        static const float notes[4] = {523, 659, 784, 1046};
        int k = (int)(T / 0.09f);
        if (k > 3) k = 3;
        ph += notes[k] / RATE;
        float tt = T - k * 0.09f;
        d[i] = (0.5f * o_pulse(ph, 0.25f) + 0.5f * o_sin(ph)) * expf(-tt * (k == 3 ? 6 : 18));
    }
    normalize(SND_POWERUP);
    LOOP(SND_KEY, 1.0f, 0.5f) {
        float v = 0;
        float fs[2] = {1046, 1568};
        float st[2] = {0, 0.18f};
        for (int k = 0; k < 2; k++) {
            float tt = T - st[k];
            if (tt < 0) continue;
            v += (sinf(TAU * fs[k] * tt) + 0.5f * sinf(TAU * fs[k] * 2.76f * tt) * expf(-tt * 8) +
                  0.25f * sinf(TAU * fs[k] * 5.4f * tt) * expf(-tt * 14)) * expf(-tt * 3.5f);
        }
        d[i] = v;
    }
    normalize(SND_KEY);
    LOOP(SND_HOSTAGE, 0.75f, 0.45f) {
        static const float notes[5] = {660, 880, 1100, 1320, 1760};
        int k = (int)(T / 0.1f);
        if (k > 4) k = 4;
        ph += notes[k] * (1 + 0.01f * sinf(TAU * T * 6)) / RATE;
        float tt = T - k * 0.1f;
        d[i] = o_tri(ph) * expf(-tt * (k == 4 ? 5 : 12));
    }
    normalize(SND_HOSTAGE);
    LOOP(SND_DOOR, 0.6f, 0.35f) {
        float env = sinf(PI * T / 0.6f);
        float a = 0.02f + 0.18f * env;
        float nz = o_noise();
        lp += a * (nz - lp);
        lp2 += 0.01f * (nz - lp2);
        ph += 110.0f / RATE;
        d[i] = ((lp - lp2) * 2.0f + 0.2f * o_sin(ph)) * env;
    }
    normalize(SND_DOOR);
    LOOP(SND_LOCKED, 0.3f, 0.35f) {
        float v = 0.5f * (o_sq(T * 110) + o_sq(T * 117));
        lp += 0.25f * (v - lp);
        bool gate = T < 0.12f || (T > 0.16f && T < 0.28f);
        d[i] = gate ? lp : 0;
    }
    normalize(SND_LOCKED);
    LOOP(SND_ALARM, 0.9f, 0.3f) {
        float f = T < 0.45f ? 880 : 660;
        ph += f / RATE;
        lp += 0.2f * (o_sq(ph) - lp);
        float tt = fmodf(T, 0.45f);
        d[i] = lp * minf(1, tt * 60) * minf(1, (0.45f - tt) * 60);
    }
    normalize(SND_ALARM);
    LOOP(SND_ENERGY, 0.1f, 0.25f) { d[i] = (o_sin(T * 900) + 0.4f * o_sin(T * 1350)) * expf(-T * 30); }
    normalize(SND_ENERGY);
    LOOP(SND_MENU_MOVE, 0.06f, 0.3f) { d[i] = (o_sin(T * 1700) + 0.3f * o_tri(T * 3400)) * expf(-T * 60); }
    normalize(SND_MENU_MOVE);
    LOOP(SND_MENU_SEL, 0.3f, 0.4f) {
        float f = 600 + 1200 * T / 0.3f;
        ph += f / RATE;
        d[i] = (o_sin(ph) + 0.3f * o_sq(ph)) * expf(-T * 8);
    }
    normalize(SND_MENU_SEL);
    LOOP(SND_EN_SHOT, 0.2f, 0.4f) {
        float f = 220 + 700 * expf(-T * 18);
        ph += f / RATE;
        float v = 0.5f * o_saw(ph) + 0.4f * o_sq(ph);
        lp += 0.3f * (v - lp);
        d[i] = lp * expf(-T * 14) * minf(1, T * 300);
    }
    normalize(SND_EN_SHOT);
    LOOP(SND_EN_MISSILE, 0.45f, 0.4f) {
        lp += 0.1f * (o_noise() - lp);
        ph += 170.0f / RATE;
        d[i] = (lp * 1.5f + 0.2f * o_sq(ph)) * minf(1, T * 40) * expf(-T * 5);
    }
    normalize(SND_EN_MISSILE);
    LOOP(SND_SCREECH, 0.5f, 0.35f) {
        float f = 1800 - 1400 * T / 0.5f + 300 * sinf(TAU * T * 18);
        ph += f / RATE;
        float v = 0.5f * o_saw(ph) + 0.4f * sinf(TAU * ph * 1.5f + o_noise() * 0.8f);
        lp += 0.4f * (v - lp);
        d[i] = lp * sqrtf(maxf(0, sinf(PI * T / 0.5f)));
    }
    normalize(SND_SCREECH);
    LOOP(SND_TALLY, 0.035f, 0.25f) { d[i] = o_sq(T * 1800) * expf(-T * 120); }
    normalize(SND_TALLY);
    LOOP(SND_EXTRALIFE, 1.1f, 0.5f) {
        static const float notes[7] = {523, 659, 784, 1046, 784, 1046, 1318};
        int k = (int)(T / 0.1f);
        if (k > 6) k = 6;
        ph += notes[k] / RATE;
        float tt = T - k * 0.1f;
        d[i] = (0.5f * o_pulse(ph, 0.3f) + 0.5f * o_tri(ph)) * expf(-tt * (k == 6 ? 4 : 14));
    }
    normalize(SND_EXTRALIFE);
    LOOP(SND_REACTOR_HIT, 0.3f, 0.45f) {
        d[i] = (o_sin(T * 180) + 0.6f * o_sin(T * 467) + 0.4f * o_sin(T * 1030) + 0.4f * o_noise() * expf(-T * 60)) *
               expf(-T * 14);
    }
    normalize(SND_REACTOR_HIT);
    LOOP(SND_BOSS, 1.6f, 0.7f) {
        float f = 55 + 10 * sinf(TAU * T * 3);
        ph += f / RATE;
        float v = 0.6f * o_saw(ph) + 0.4f * o_saw(ph * 1.5f + 0.3f * sinf(TAU * T * 7)) + 0.2f * o_noise();
        lp += 0.1f * (v - lp);
        d[i] = lp * minf(1, T * 4) * expf(-T * 1.2f);
    }
    normalize(SND_BOSS);
    LOOP(SND_TELEPORT, 0.7f, 0.4f) {
        float f = 300 + 1500 * T / 0.7f;
        ph += f / RATE;
        d[i] = o_sin(ph) * (0.6f + 0.4f * sinf(TAU * T * 40)) * sinf(PI * T / 0.7f);
    }
    normalize(SND_TELEPORT);
    LOOP(SND_MATCEN, 0.9f, 0.4f) {
        float f = 1400 - 1200 * T / 0.9f;
        ph += f / RATE;
        d[i] = o_tri(ph) * (0.5f + 0.5f * sinf(TAU * T * 25)) * sinf(PI * T / 0.9f);
    }
    normalize(SND_MATCEN);
    LOOP(SND_BEEP, 0.12f, 0.35f) {
        lp += 0.3f * (o_sq(T * 1250) - lp);
        d[i] = lp * minf(1, T * 200) * minf(1, (0.12f - T) * 200);
    }
    normalize(SND_BEEP);
    LOOP(SND_BREAK, 0.7f, 0.6f) {
        lp += 0.2f * (o_noise() - lp);
        float pop = (o_noise() > 0.995f) ? 1.0f : 0.0f;
        lp2 += 0.3f * (pop - lp2);
        ph += (80 * expf(-T * 3) + 30) / RATE;
        d[i] = (lp + lp2 * 3 + 0.5f * o_sin(ph)) * expf(-T * 5);
    }
    normalize(SND_BREAK);
    LOOP(SND_BURNER, 1.0f, 0.3f) {
        float nz = o_noise();
        lp += 0.08f * (nz - lp);
        lp2 += 0.01f * (nz - lp2);
        d[i] = lp + lp2 * 2;
    }
    normalize(SND_BURNER);
    LOOP(SND_CLOAK, 0.6f, 0.4f) {
        float f = 1600 - 1300 * T / 0.6f;
        ph += f / RATE;
        d[i] = o_sin(ph) * (0.6f + 0.4f * sinf(TAU * T * 30)) * expf(-T * 4) * minf(1, T * 100);
    }
    normalize(SND_CLOAK);
    LOOP(SND_NOAMMO, 0.1f, 0.3f) { d[i] = o_sq(T * 200) * expf(-T * 30); }
    normalize(SND_NOAMMO);
#undef LOOP
#undef T
}

/* ------------------------------------------------------------ music */
typedef struct {
    float bpm;
    int root;
    int nbars;
    int chords[8];
    int qual[8];
    uint16_t kick, snare, hat, ohat;
    int8_t bass[16];
    int8_t arp[16];
    int8_t lead[32];
    float bass_v, arp_v, pad_v, drum_v, lead_v;
    float bass_cut;
    float arp_decay;
    int arp_oct;
} Song;

#define R -1
#define H -2
static const Song SONGS[SONG_COUNT] = {
    /* MENU: A minor, slow and atmospheric */
    {92, 45, 8, {0, -4, 3, -2, 0, -4, 3, -2}, {0, 1, 1, 1, 0, 1, 1, 1},
     0x0101, 0x1000, 0x4444, 0x0000,
     {0, R, R, R, R, R, R, R, 0, R, R, R, R, R, 7, R},
     {0, R, 2, R, 3, R, 2, R, 1, R, 2, R, 4, R, 2, R},
     {R}, 0.8f, 0.45f, 0.55f, 0.5f, 0.0f, 0.04f, 7.0f, 0},
    /* L1: D minor, driving */
    {118, 38, 8, {0, -4, -2, -5, 0, -4, -2, 2}, {0, 1, 1, 0, 0, 1, 1, 1},
     0x1111, 0x1010, 0x4444, 0x0000,
     {0, R, 0, 12, 0, R, 7, 12, 0, R, 0, 12, 0, R, 10, 12},
     {0, 1, 2, 3, 4, 3, 2, 1, 0, 1, 2, 3, 5, 3, 2, 1},
     {R}, 0.9f, 0.32f, 0.35f, 0.8f, 0.0f, 0.06f, 10.0f, 0},
    /* L2: E minor, offbeat techno bass */
    {124, 40, 8, {0, -4, -2, -5, 0, -4, -2, -1}, {0, 1, 1, 0, 0, 1, 1, 1},
     0x1111, 0x1010, 0x5555, 0x4444,
     {R, R, 0, R, R, R, 0, R, R, R, 0, R, R, 12, 0, R},
     {0, 2, 3, 2, 1, 2, 4, 2, 0, 2, 3, 2, 5, 4, 3, 2},
     {R}, 1.0f, 0.3f, 0.3f, 0.8f, 0.0f, 0.07f, 12.0f, 0},
    /* L3: C minor, 16th bass */
    {130, 36, 8, {0, -4, 3, -2, 0, -4, 3, -1}, {0, 1, 1, 1, 0, 1, 1, 1},
     0x1111, 0x1010, 0xFFFF, 0x0000,
     {0, 0, 12, 0, 0, 12, 0, 0, 0, 0, 12, 0, 7, 0, 12, 10},
     {3, R, 2, R, 1, R, 2, R, 3, R, 4, R, 5, R, 4, R},
     {R}, 0.85f, 0.3f, 0.35f, 0.8f, 0.0f, 0.05f, 9.0f, 12},
    /* BOSS: F minor / harmonic */
    {140, 41, 4, {0, -4, -2, -5}, {0, 1, 1, 1},
     0x1111, 0x1010, 0xFFFF, 0x2222,
     {0, 0, 12, 0, 0, 0, 12, 0, 0, 0, 12, 0, 13, 0, 12, 11},
     {0, 3, 2, 1, 0, 3, 2, 1, 0, 3, 2, 1, 5, 4, 3, 2},
     {12, H, H, H, 15, H, 14, H, 12, H, H, H, 11, H, H, H, 12, H, H, H, 15, H, 17, H, 19, H, H, H, 17, H, 15, H},
     1.0f, 0.25f, 0.3f, 0.9f, 0.22f, 0.07f, 12.0f, 0},
    /* ESCAPE: G minor, fast */
    {164, 43, 4, {0, -4, -2, -5}, {0, 1, 1, 1},
     0x1111, 0x1010, 0xFFFF, 0x0000,
     {0, 12, 0, 12, 0, 12, 0, 12, 0, 12, 0, 12, 0, 12, 7, 12},
     {0, 1, 2, 3, 4, 5, 4, 3, 2, 1, 2, 3, 4, 5, 4, 3},
     {24, H, 22, H, 19, H, 22, H, 24, H, 26, H, 27, H, 26, H, 24, H, 22, H, 19, H, 17, H, 18, H, H, H, 19, H, H, H},
     0.9f, 0.3f, 0.2f, 0.9f, 0.2f, 0.09f, 16.0f, 0},
    /* BRIEF: G major, calm */
    {84, 43, 4, {0, -3, -7, -5}, {1, 0, 1, 1},
     0x0101, 0x0000, 0x0000, 0x4444,
     {0, R, R, R, R, R, R, R, 7, R, R, R, R, R, R, R},
     {0, R, R, 2, R, R, 3, R, R, 4, R, R, 2, R, R, R},
     {R}, 0.6f, 0.4f, 0.6f, 0.35f, 0.0f, 0.035f, 5.0f, 0},
    /* GAMEOVER */
    {70, 45, 4, {0, 5, 7, 0}, {0, 0, 1, 0},
     0x0001, 0x0000, 0x0000, 0x0000,
     {0, R, R, R, R, R, R, R, R, R, R, R, R, R, R, R},
     {0, R, R, R, 2, R, R, R, 3, R, R, R, 1, R, R, R},
     {R}, 0.6f, 0.35f, 0.7f, 0.3f, 0.0f, 0.03f, 3.0f, 0},
    /* VICTORY: C major, uplifting */
    {112, 48, 4, {0, 7, 9, 5}, {1, 1, 0, 1},
     0x1111, 0x1010, 0x4444, 0x0000,
     {0, R, 12, R, 0, R, 12, R, 0, R, 12, R, 7, R, 12, R},
     {0, 1, 2, 3, 4, 3, 2, 1, 0, 1, 2, 3, 5, 4, 3, 2},
     {12, H, H, H, 16, H, 19, H, 24, H, H, H, 23, H, 19, H, 21, H, H, H, 19, H, 16, H, 17, H, H, H, 16, H, 14, H},
     0.8f, 0.35f, 0.45f, 0.75f, 0.18f, 0.06f, 8.0f, 0},
};
#undef R
#undef H

static const int TONES[2][6] = {{0, 3, 7, 12, 15, 19}, {0, 4, 7, 12, 16, 19}};

typedef struct {
    int song, target;
    float gain;
    int step, bar;
    double step_left;
    /* bass */
    float b_freq, b_ph, b_sub, b_env, b_lp1, b_lp2;
    /* arp */
    float a_freq, a_ph, a_env, a_lp;
    /* pad */
    float p_freq[3], p_ph[3][2], p_env, p_lp_l, p_lp_r;
    /* lead */
    float l_freq, l_ph, l_env, l_vib;
    bool l_on;
    /* drums */
    float k_env, k_ph, k_penv;
    float s_env, s_ph, s_hp;
    float h_env, h_hp, h_len;
    float n_prev;
    /* delay */
    float *dl, *dr;
    int dlen, dpos;
} MusicState;
static MusicState M;

static inline float mtof(int n) { return 440.0f * powf(2.0f, (n - 69) / 12.0f); }

static void music_trigger(void) {
    const Song *s = &SONGS[M.song];
    int bar = M.bar % s->nbars;
    int chord = s->chords[bar], q = s->qual[bar];
    int st = M.step;
    if (s->kick & (1 << st)) { M.k_env = 1; M.k_ph = 0; M.k_penv = 1; }
    if (s->snare & (1 << st)) { M.s_env = 1; }
    if (s->hat & (1 << st)) { M.h_env = 0.7f; M.h_len = 60; }
    if (s->ohat & (1 << st)) { M.h_env = 0.6f; M.h_len = 12; }
    if (s->bass[st] >= 0 && s->bass_v > 0) {
        M.b_freq = mtof(s->root + chord + s->bass[st]);
        M.b_env = 1;
    }
    if (s->arp[st] >= 0 && s->arp_v > 0) {
        M.a_freq = mtof(s->root + 24 + s->arp_oct + chord + TONES[q][s->arp[st]]);
        M.a_env = 1;
    }
    if (st == 0 && s->pad_v > 0) {
        for (int k = 0; k < 3; k++) M.p_freq[k] = mtof(s->root + 12 + chord + TONES[q][k]);
    }
    if (s->lead_v > 0) {
        int lv = s->lead[(bar % 2) * 16 + st];
        if (lv >= 0) { M.l_freq = mtof(s->root + 12 + lv); M.l_env = 1; M.l_on = true; }
        else if (lv == -1) M.l_on = false;
    }
}

static void music_render(float *out, int frames) {
    if (M.song < 0 && M.target < 0) return;
    for (int i = 0; i < frames; i++) {
        /* crossfade handling */
        if (M.target != M.song) {
            M.gain -= 1.0f / (RATE * 0.7f);
            if (M.gain <= 0 || M.song < 0) {
                M.gain = 0;
                M.song = M.target;
                M.step = 15; M.bar = -1; M.step_left = 0;
                M.a_env = M.b_env = M.l_env = 0;
                M.l_on = false;
            }
        } else if (M.gain < 1) {
            M.gain = minf(1, M.gain + 1.0f / (RATE * 0.5f));
        }
        if (M.song < 0) continue;
        const Song *s = &SONGS[M.song];
        M.step_left -= 1.0;
        if (M.step_left <= 0) {
            M.step_left += RATE * 60.0 / s->bpm / 4.0;
            M.step = (M.step + 1) % 16;
            if (M.step == 0) M.bar = (M.bar + 1) % s->nbars;
            music_trigger();
        }
        float L = 0, Rr = 0;
        /* bass: saw + sub through 2-pole lowpass with envelope */
        M.b_ph += M.b_freq / RATE;
        M.b_sub += M.b_freq * 0.5f / RATE;
        float bv = 0.55f * o_saw(M.b_ph) + 0.5f * o_sin(M.b_sub);
        float cut = s->bass_cut + 0.25f * M.b_env;
        M.b_lp1 += cut * (bv - M.b_lp1);
        M.b_lp2 += cut * (M.b_lp1 - M.b_lp2);
        float bass = M.b_lp2 * M.b_env * s->bass_v;
        M.b_env *= 1.0f - 6.0f / RATE;
        L += bass; Rr += bass;
        /* arp: pulse wave */
        M.a_ph += M.a_freq / RATE;
        float av = 0.6f * o_pulse(M.a_ph, 0.3f) + 0.4f * o_tri(M.a_ph);
        M.a_lp += 0.3f * (av - M.a_lp);
        float arp = M.a_lp * M.a_env * s->arp_v;
        M.a_env *= 1.0f - s->arp_decay / RATE;
        /* pad: detuned saws */
        float pl = 0, pr = 0;
        if (s->pad_v > 0) {
            for (int k = 0; k < 3; k++) {
                M.p_ph[k][0] += M.p_freq[k] * 0.9965f / RATE;
                M.p_ph[k][1] += M.p_freq[k] * 1.0035f / RATE;
                pl += o_saw(M.p_ph[k][0]);
                pr += o_saw(M.p_ph[k][1]);
            }
            M.p_lp_l += 0.03f * (pl - M.p_lp_l);
            M.p_lp_r += 0.03f * (pr - M.p_lp_r);
            float pv = s->pad_v * 0.16f;
            L += M.p_lp_l * pv;
            Rr += M.p_lp_r * pv;
        }
        /* lead */
        if (s->lead_v > 0) {
            M.l_vib += 5.5f / RATE;
            float f = M.l_freq * (1.0f + 0.006f * o_sin(M.l_vib));
            M.l_ph += f / RATE;
            float target = M.l_on ? 1.0f : 0.0f;
            M.l_env += (target - M.l_env) * (M.l_on ? 0.002f : 0.0006f);
            float lv = (0.5f * o_saw(M.l_ph) + 0.5f * o_sq(M.l_ph * 0.5f + 0.25f)) * M.l_env * s->lead_v;
            L += lv * 0.8f; Rr += lv * 0.8f;
            arp += lv * 0.3f;
        }
        /* drums */
        float nz = o_noise();
        float dr = 0;
        if (M.k_env > 0.0005f) {
            float f = 45 + 120 * M.k_penv;
            M.k_ph += f / RATE;
            dr += o_sin(M.k_ph) * M.k_env * 1.1f;
            M.k_env *= 1.0f - 9.0f / RATE;
            M.k_penv *= 1.0f - 38.0f / RATE;
        }
        if (M.s_env > 0.0005f) {
            M.s_hp = nz - M.n_prev;
            M.s_ph += 185.0f / RATE;
            dr += (M.s_hp * 0.45f + o_sin(M.s_ph) * 0.35f) * M.s_env;
            M.s_env *= 1.0f - 16.0f / RATE;
        }
        float hat = 0;
        if (M.h_env > 0.0005f) {
            float hp = nz - M.n_prev;
            hat = hp * M.h_env * 0.22f;
            M.h_env *= 1.0f - M.h_len / RATE;
        }
        M.n_prev = nz;
        dr *= s->drum_v;
        hat *= s->drum_v;
        L += dr + hat * 0.7f;
        Rr += dr + hat;
        /* arp through ping-pong delay */
        int dp = M.dpos;
        float echo_l = M.dl[dp], echo_r = M.dr[dp];
        M.dl[dp] = arp + echo_r * 0.45f;
        M.dr[dp] = echo_l * 0.45f;
        M.dpos = (dp + 1) % M.dlen;
        L += arp + echo_l * 0.6f;
        Rr += arp * 0.8f + echo_r * 0.6f;
        float g = M.gain * vol_music * 0.42f;
        out[i * 2] += L * g;
        out[i * 2 + 1] += Rr * g;
    }
}

/* ------------------------------------------------------------ mixer */
static inline float soft_clip(float x) {
    if (x > 3) return 1;
    if (x < -3) return -1;
    return x * (27 + x * x) / (27 + 9 * x * x);
}

static void SDLCALL audio_cb(void *userdata, SDL_AudioStream *st, int additional, int total) {
    (void)userdata; (void)total;
    static float buf[4096 * 2];
    while (additional > 0) {
        int frames = additional / (int)(sizeof(float) * 2);
        if (frames <= 0) break;
        if (frames > 4096) frames = 4096;
        memset(buf, 0, sizeof(float) * 2 * frames);
        SDL_LockMutex(mtx);
        for (int v = 0; v < NVOICES; v++) {
            Voice *vo = &voices[v];
            if (!vo->active || vo->paused) continue;
            const Sample *s = &S[vo->snd];
            if (!s->d || s->n < 2) { vo->active = false; continue; }
            for (int i = 0; i < frames; i++) {
                int idx = (int)vo->pos;
                float fr = (float)(vo->pos - idx);
                float x = s->d[idx] * (1 - fr) + s->d[idx + 1] * fr;
                vo->vol += (vo->tvol - vo->vol) * 0.002f;
                float g = x * vo->vol * vol_sfx;
                buf[i * 2] += g * vo->pl;
                buf[i * 2 + 1] += g * vo->pr;
                vo->pos += vo->rate;
                if (vo->pos >= s->n - 1) {
                    if (vo->loop) vo->pos -= (s->n - 1);
                    else { vo->active = false; break; }
                }
            }
        }
        music_render(buf, frames);
        SDL_UnlockMutex(mtx);
        for (int i = 0; i < frames * 2; i++) buf[i] = soft_clip(buf[i]);
        SDL_PutAudioStreamData(st, buf, frames * (int)sizeof(float) * 2);
        additional -= frames * (int)sizeof(float) * 2;
    }
}

void audio_init(void) {
    gen_sounds();
    memset(&M, 0, sizeof(M));
    M.song = M.target = -1;
    M.dlen = (int)(RATE * 0.36f);
    M.dl = (float *)calloc((size_t)M.dlen, sizeof(float));
    M.dr = (float *)calloc((size_t)M.dlen, sizeof(float));
    mtx = SDL_CreateMutex();
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = RATE;
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_cb, NULL);
    if (!stream) {
        SDL_Log("Audio unavailable: %s", SDL_GetError());
        return;
    }
    SDL_ResumeAudioStreamDevice(stream);
    audio_ok = true;
}

void audio_shutdown(void) {
    if (stream) SDL_DestroyAudioStream(stream);
    stream = NULL;
    if (mtx) SDL_DestroyMutex(mtx);
    mtx = NULL;
    for (int i = 0; i < SND_COUNT; i++) free(S[i].d);
    free(M.dl);
    free(M.dr);
}

static Voice *alloc_voice(int id) {
    Voice *best = NULL;
    int same = 0;
    Voice *oldest_same = NULL;
    for (int v = 0; v < NVOICES; v++) {
        if (voices[v].active && voices[v].snd == id && !voices[v].loop) {
            same++;
            if (!oldest_same || voices[v].age < oldest_same->age) oldest_same = &voices[v];
        }
    }
    if (same >= 5 && oldest_same) return oldest_same;
    for (int v = 0; v < NVOICES; v++) {
        if (!voices[v].active) return &voices[v];
        if (!voices[v].loop && (!best || voices[v].age < best->age)) best = &voices[v];
    }
    return best;
}

static int start_voice(int id, float vol, float pitch, float pan, bool loop) {
    if (!audio_ok || id < 0 || id >= SND_COUNT || vol <= 0.001f) return 0;
    SDL_LockMutex(mtx);
    Voice *vo = alloc_voice(id);
    int h = 0;
    if (vo) {
        memset(vo, 0, sizeof(*vo));
        vo->active = true;
        vo->snd = id;
        vo->pos = 0;
        vo->rate = pitch;
        vo->vol = vo->tvol = vol;
        float a = (clampf(pan, -1, 1) + 1) * PI * 0.25f;
        vo->pl = cosf(a) * 1.41f;
        vo->pr = sinf(a) * 1.41f;
        vo->loop = loop;
        vo->age = ++age_counter;
        vo->handle = h = next_handle++;
    }
    SDL_UnlockMutex(mtx);
    return h;
}

void snd_play(int id, float vol, float pitch) { start_voice(id, vol, pitch, 0, false); }

void snd_play_at(int id, V2 pos, float vol, float pitch) {
    float d = v2dist(pos, listener);
    float att = 1.0f;
    if (d > 180) att = clampf(1.0f - (d - 180) / 1300.0f, 0, 1);
    att *= att;
    float pan = clampf((pos.x - listener.x) / 700.0f, -0.75f, 0.75f);
    start_voice(id, vol * att, pitch, pan, false);
}

int snd_loop(int id, float vol, float pitch) { return start_voice(id, vol, pitch, 0, true); }

void snd_loop_set(int handle, float vol, float pitch) {
    if (!audio_ok || !handle) return;
    SDL_LockMutex(mtx);
    for (int v = 0; v < NVOICES; v++) {
        if (voices[v].active && voices[v].handle == handle) {
            voices[v].tvol = vol;
            voices[v].rate = pitch;
        }
    }
    SDL_UnlockMutex(mtx);
}

void snd_stop(int handle) {
    if (!audio_ok || !handle) return;
    SDL_LockMutex(mtx);
    for (int v = 0; v < NVOICES; v++)
        if (voices[v].active && voices[v].handle == handle) voices[v].active = false;
    SDL_UnlockMutex(mtx);
}

void audio_pause_loops(bool paused) {
    if (!audio_ok) return;
    SDL_LockMutex(mtx);
    for (int v = 0; v < NVOICES; v++)
        if (voices[v].active && voices[v].loop) voices[v].paused = paused;
    SDL_UnlockMutex(mtx);
}

void audio_set_listener(V2 p) { listener = p; }

void music_play(int song) {
    if (!audio_ok) return;
    SDL_LockMutex(mtx);
    M.target = song;
    SDL_UnlockMutex(mtx);
}

void audio_set_volumes(float music, float sfx) {
    if (mtx) SDL_LockMutex(mtx);
    vol_music = clampf(music, 0, 1);
    vol_sfx = clampf(sfx, 0, 1);
    if (mtx) SDL_UnlockMutex(mtx);
}
