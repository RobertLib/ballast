/*
 * Music: a small house music sequencer and synthesizer running inside the
 * audio callback (see audio.c). Songs are written as text patterns and
 * parsed once at startup. Pads, chord stabs, arpeggios and bass lines are
 * derived from the current chord so every part stays in key; hooks and acid
 * lines are written out note by note against the chords.
 *
 * Pattern syntax: one character per 16th step, spaces are ignored and
 * patterns repeat to fill their section.
 *   chords  "Am9:32 Dm9:32"   one bar per chord unless ":steps" is given
 *   pad     h hold the chord (re-voiced when it changes)  x retrigger
 *           - tie  . release
 *   bass    r root  o octave  f fifth  t third  s seventh  u fifth below
 *           (upper case = accent)  - tie  . rest
 *   arp     0-9 chord tone counted up from the pad voicing  c whole chord
 *           r o f root, octave, fifth (upper case C R O F = accent)
 *           - tie  . rest
 *   pluck   like arp, but each note rings on under the next one (strings)
 *   lead    "a4:4 c5:2! r:2"  note (r = rest) and length in steps, ! accent,
 *           ~ slide into the next note; the length can be left out to
 *           repeat the previous one
 *   kick    x X(accent)
 *   snare   x X(accent) g(ghost) r(32nd roll) c(clap) b(snare + clap)
 *   hat     g(ghost) x X(accent) o(open)
 *   perc    h m l (toms) k j (high, low conga) r (rimshot)
 *   shaker  x X(accent)
 *   ride    x X(accent) c(crash)
 *   In every drum lane but the kick, ? plays a soft ghost of that lane half
 *   of the time, so grooves never repeat exactly.
 */
#include "common.h"
#include <stddef.h>

#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

#define RATE 44100
#define CTL 16 /* control-rate period in samples */
#define MAX_BARS 32
#define MAX_STEPS (MAX_BARS * 16)
#define MAX_PAT 128
#define MAX_CHORDS 32
#define NPAD 12
#define NARP 16
#define NPLK 12
#define NUNI 5      /* most stacked oscillators per voice */
#define KS_MAX 1024 /* string delay line, a power of two: notes down to 43 Hz */
#define NLANE 6 /* drum lanes: kick, snare, hat, perc, shaker, ride */
#define DELAY_MAX (RATE * 2)
#define CHORUS_MAX 1024
#define LEAD_HOLD -1
#define LEAD_OFF -2
#define FX_ACCENT 1
#define FX_SLIDE 2

#ifndef MUSIC_TAP /* test hook: observe the individual buses of the mix */
#define MUSIC_TAP(bus, l, r)
#endif

/* ------------------------------------------------------------ song format */
enum { OSC_SAW, OSC_PULSE, OSC_SAWSQ, OSC_TRI, OSC_STRING };

typedef struct {
    float a, d, s, r;                               /* amp envelope: seconds, sustain level */
    int osc, unison;                                /* waveform, 1-5 stacked oscillators */
    float detune, width, pw, sub;                   /* unison cents, stereo width, pulse width, sine sub-octave level */
    float cutoff, env_oct, fdecay, reso, keytrack;  /* lowpass: Hz, envelope depth in octaves, ... */
    bool steep;                                     /* 24 dB/octave instead of 12 */
    float drive;                                    /* saturation after the filter, 0 = clean */
    float lfo, sweep;                               /* slow cutoff wobble, response to section brightness (octaves) */
    float vox, vowel, vowel_env;                    /* formant filter gain (0 = off), vowel oo..ah, envelope opening it */
    float ring, pick;                               /* OSC_STRING: seconds to fall 60 dB, pick brightness 0..1 */
    float glide, vib;                               /* slide time, vibrato depth in semitones */
    float level, delay, reverb, chorus;             /* output level and effect sends */
} Patch;

typedef struct { const char *kick, *snare, *hat, *perc, *shaker, *ride; } Drums;

enum { SEC_CRASH = 1, SEC_RISER = 2, SEC_LIFT = 4 }; /* LIFT: highpass the mix over the riser bars */

typedef struct {
    int bars;
    const char *chords, *pad, *bass, *arp, *pluck, *lead;
    const Drums *drums, *fill; /* fill replaces the drums in the last bar */
    float bright0, bright1;    /* filter brightness over the section, 0 = both 1 */
    unsigned flags;
} Section;

typedef struct {
    float bpm, swing;
    const Section *sec;
    int nsec, loop; /* after the last section play again from sec[loop] */
    const Patch *pad, *bass, *arp, *pluck, *lead;
    int bass_lo, pad_lo, pad_hi, arp_shift, pluck_shift; /* registers as MIDI notes */
    float sidechain;                        /* how far the kick ducks everything tonal */
    float delay_steps, delay_fb, delay_mix;
    float rev_time, rev_damp, rev_mix; /* reverb decay in seconds, darkness 0..1, return level */
    float kick_hz, kick_decay, drums, hats;
    float gain;
} Song;

#define SECS(a) .sec = a, .nsec = (int)(sizeof(a) / sizeof(a[0]))

/* ------------------------------------------------------------ patches */
/* pads: five detuned saws (supersaw) under a slow filter and the chorus */
static const Patch PAD_DEEP = {.a = 0.6f, .d = 2.0f, .s = 0.85f, .r = 1.5f, .osc = OSC_SAW, .unison = 5, .detune = 14, .width = 0.9f,
                               .cutoff = 1700, .env_oct = 0.3f, .fdecay = 1.5f, .reso = 0.1f, .keytrack = 0.3f, .lfo = 0.3f,
                               .sweep = 1.5f, .level = 0.06f, .reverb = 0.5f, .chorus = 0.5f};
static const Patch PAD_DARK = {.a = 0.8f, .d = 2.0f, .s = 0.85f, .r = 1.5f, .osc = OSC_SAW, .unison = 5, .detune = 16, .width = 0.9f,
                               .cutoff = 1200, .env_oct = 0.4f, .fdecay = 1.5f, .reso = 0.15f, .keytrack = 0.3f, .lfo = 0.35f,
                               .sweep = 1.5f, .level = 0.06f, .reverb = 0.5f, .chorus = 0.5f};
static const Patch PAD_WARM = {.a = 0.7f, .d = 2.0f, .s = 0.85f, .r = 1.5f, .osc = OSC_SAW, .unison = 5, .detune = 12, .width = 1.0f,
                               .cutoff = 2000, .env_oct = 0.3f, .fdecay = 1.5f, .reso = 0.05f, .keytrack = 0.3f, .lfo = 0.25f,
                               .sweep = 1.5f, .level = 0.055f, .reverb = 0.55f, .chorus = 0.6f};
static const Patch PAD_VOX = {.a = 1.0f, .d = 2.0f, .s = 0.9f, .r = 2.0f, .osc = OSC_SAW, .unison = 3, .detune = 9, .width = 1.0f,
                              .cutoff = 3200, .keytrack = 0.2f, .lfo = 0.15f, .sweep = 1, .vox = 2.2f, .vowel = 0.25f,
                              .level = 0.1f, .reverb = 0.7f, .chorus = 0.6f};
static const Patch BASS_DEEP = {.a = 0.003f, .d = 0.35f, .s = 0.55f, .r = 0.07f, .osc = OSC_SAW, .unison = 1, .sub = 0.9f,
                                .cutoff = 140, .env_oct = 2.0f, .fdecay = 0.08f, .reso = 0.2f, .keytrack = 0.6f, .sweep = 2,
                                .level = 0.5f};
static const Patch BASS_ROLL = {.a = 0.001f, .d = 0.14f, .s = 0.3f, .r = 0.05f, .osc = OSC_SAW, .unison = 1, .sub = 0.75f,
                                .cutoff = 170, .env_oct = 2.4f, .fdecay = 0.06f, .reso = 0.35f, .keytrack = 0.5f, .sweep = 2,
                                .level = 0.42f};
static const Patch BASS_GRIT = {.a = 0.001f, .d = 0.2f, .s = 0.4f, .r = 0.05f, .osc = OSC_SAWSQ, .unison = 2, .detune = 8, .pw = 0.4f,
                                .sub = 0.75f, .cutoff = 190, .env_oct = 2.6f, .fdecay = 0.09f, .reso = 0.4f, .keytrack = 0.4f,
                                .drive = 1.5f, .sweep = 2, .level = 0.34f};
static const Patch BASS_SOFT = {.a = 0.03f, .d = 1.0f, .s = 0.85f, .r = 0.4f, .osc = OSC_SAW, .unison = 1, .sub = 0.9f,
                                .cutoff = 150, .env_oct = 1.0f, .fdecay = 0.4f, .reso = 0.1f, .keytrack = 0.4f, .sweep = 2,
                                .level = 0.36f};
/* warm analog-style poly chords: detuned saws, a soft filter swell on each hit, held under the chorus */
static const Patch KEYS_WARM = {.a = 0.006f, .d = 1.4f, .s = 0.4f, .r = 0.5f, .osc = OSC_SAW, .unison = 3, .detune = 8, .width = 0.7f,
                                .cutoff = 850, .env_oct = 1.3f, .fdecay = 0.4f, .reso = 0.12f, .keytrack = 0.4f, .sweep = 1,
                                .level = 0.088f, .delay = 0.25f, .reverb = 0.45f, .chorus = 0.8f};
static const Patch KEYS_STAB = {.a = 0.004f, .d = 0.7f, .s = 0.25f, .r = 0.35f, .osc = OSC_SAW, .unison = 3, .detune = 10, .width = 0.8f,
                                .cutoff = 1100, .env_oct = 1.6f, .fdecay = 0.22f, .reso = 0.15f, .keytrack = 0.4f, .sweep = 1,
                                .level = 0.11f, .delay = 0.3f, .reverb = 0.4f, .chorus = 0.7f};
static const Patch STAB_DUB = {.a = 0.002f, .d = 0.45f, .s = 0, .r = 0.25f, .osc = OSC_SAW, .unison = 2, .detune = 10, .width = 0.8f,
                               .cutoff = 900, .env_oct = 1.6f, .fdecay = 0.12f, .reso = 0.2f, .keytrack = 0.3f, .sweep = 1.5f,
                               .level = 0.2f, .delay = 0.6f, .reverb = 0.5f, .chorus = 0.4f};
static const Patch STAB_TECH = {.a = 0.001f, .d = 0.22f, .s = 0, .r = 0.12f, .osc = OSC_SAW, .unison = 2, .detune = 14, .width = 0.8f,
                                .cutoff = 800, .env_oct = 1.8f, .fdecay = 0.07f, .reso = 0.25f, .keytrack = 0.3f, .sweep = 1.5f,
                                .level = 0.18f, .delay = 0.45f, .reverb = 0.35f, .chorus = 0.3f};
static const Patch ARP_SEQ = {.a = 0.001f, .d = 0.25f, .s = 0, .r = 0.1f, .osc = OSC_PULSE, .unison = 2, .detune = 6, .width = 0.6f,
                              .pw = 0.3f, .cutoff = 700, .env_oct = 2.2f, .fdecay = 0.1f, .reso = 0.25f, .keytrack = 0.5f,
                              .sweep = 2, .level = 0.13f, .delay = 0.5f, .reverb = 0.3f, .chorus = 0.2f};
/* plucked strings (Karplus-Strong): harp, a dark dub pluck and a glassy bell-like one; the string sets the decay,
 * so the envelope only ends the note well after it has faded */
static const Patch PLUCK_HARP = {.a = 0.001f, .d = 7.5f, .s = 0, .r = 1.2f, .osc = OSC_STRING, .ring = 3.0f, .pick = 0.55f,
                                 .cutoff = 3500, .keytrack = 0.2f, .sweep = 0.5f, .level = 0.45f, .delay = 0.4f, .reverb = 0.5f,
                                 .chorus = 0.3f};
static const Patch PLUCK_DUB = {.a = 0.001f, .d = 5.0f, .s = 0, .r = 0.8f, .osc = OSC_STRING, .ring = 2.0f, .pick = 0.35f,
                                .cutoff = 1800, .keytrack = 0.3f, .sweep = 1.2f, .level = 0.55f, .delay = 0.6f, .reverb = 0.4f,
                                .chorus = 0.2f};
static const Patch PLUCK_BELL = {.a = 0.001f, .d = 5.5f, .s = 0, .r = 1.0f, .osc = OSC_STRING, .ring = 2.2f, .pick = 0.75f,
                                 .cutoff = 3000, .keytrack = 0.2f, .sweep = 1, .level = 0.5f, .delay = 0.5f, .reverb = 0.5f,
                                 .chorus = 0.2f};
static const Patch ACID = {.a = 0.002f, .d = 0.25f, .s = 0.2f, .r = 0.04f, .osc = OSC_SAW, .unison = 1, .cutoff = 230,
                           .env_oct = 3.2f, .fdecay = 0.14f, .reso = 0.72f, .keytrack = 0.4f, .steep = true, .drive = 1.8f,
                           .sweep = 3, .glide = 0.06f, .level = 0.2f, .delay = 0.2f, .reverb = 0.1f};
static const Patch LEAD_VOX = {.a = 0.03f, .d = 0.6f, .s = 0.75f, .r = 0.3f, .osc = OSC_SAW, .unison = 2, .detune = 6, .width = 0.3f,
                               .cutoff = 3500, .env_oct = 0.3f, .fdecay = 0.3f, .vox = 2.2f, .vowel = 0.5f, .vowel_env = 0.35f,
                               .glide = 0.09f, .vib = 0.25f, .level = 0.13f, .delay = 0.35f, .reverb = 0.45f, .chorus = 0.3f};
static const Patch LEAD_CHOP = {.a = 0.004f, .d = 0.25f, .s = 0.35f, .r = 0.12f, .osc = OSC_SAW, .unison = 2, .detune = 8,
                                .width = 0.3f, .cutoff = 4000, .fdecay = 0.2f, .vox = 2.2f, .vowel = 0.75f, .vowel_env = 0.25f,
                                .glide = 0.04f, .level = 0.2f, .delay = 0.45f, .reverb = 0.35f, .chorus = 0.2f};
static const Patch LEAD_STRING = {.a = 0.001f, .d = 3.0f, .s = 0, .r = 0.3f, .osc = OSC_STRING, .ring = 1.2f, .pick = 0.6f,
                                  .cutoff = 2600, .keytrack = 0.2f, .level = 0.75f, .delay = 0.5f, .reverb = 0.35f};

/* shared house grooves: four to the floor, clap on 2 and 4, open hat on the offbeat; ? adds ghost notes */
#define FLOOR "x... x... x... x..."
#define CLAP ".... c... .... c..."
#define OFFBEAT "..o. ..o. ..o. ..o."
#define HATS16 "gxog gxog gxog gxog"
#define HATS_LIVE "g?og gxo? g?og gxog"
#define SHAKE "xxXx xxXx xxXx xxXx"
#define SHAKE_LIVE "x?Xx x?Xx x?Xx xxX?"
#define RIDE "x.X. x.X. x.X. x.X."

/* ------------------------------------------------------------ MENU: A minor, deep house */
static const char MENU_A[] = "Am9:32 Dm9:32";
static const char MENU_B[] = "Fmaj9:32 Em7:32";
static const char MENU_C[] = "Dm9 Em7 Fmaj9 Em7 Dm9 Em7 Fmaj9:32";
static const char MENU_BASS[] = "r--. ..r. ...r ..o.  r--. ..r. ..s. u.r.";
static const char MENU_BASS_2[] = "r--. ..r. ...r ..o.  r--. ..r. ..s. u.r.  r--. ..r. ...r ..o.  r... .rr. o.s. f.u.";
static const char MENU_BASS_C[] = "r--. ..r. ...r ..o.";
static const char MENU_KEYS[] = "C--- --c- ---- ----  ---c ---- --c- ----";
static const char MENU_KEYS_2[] = "C--- --c- ---- ---c  ---- --c- --c- ----";
static const char MENU_KEYS_C[] = "C--- ---c ---- ----";
static const char MENU_PLUCK[] = "0..2 ..1. 3..2 ..0.  0..2 ..1. 4..3 ..2.";
static const char MENU_PLUCK_C[] = "0.1. 2.3. 4... 3.2.";
static const char MENU_HOOK_A[] = "e5:3 d5:3 c5:2~ a4:8  r:16  g4:3 a4:3 c5:2~ d5:8  r:16";
static const char MENU_HOOK_A2[] = "e5:3 d5:3 c5:2~ a4:8  r:16  g4:3 a4:3 c5:2~ e5:4 d5:4  r:8 c5:2 d5:2 c5:2 a4:2";
static const char MENU_HOOK_B[] = "a4:3 c5:3 e5:2~ g5:8  r:16  g5:3 e5:3 d5:2~ b4:8  r:16";
static const Drums MENU_INTRO = {NULL, NULL, NULL, NULL, SHAKE, NULL};
static const Drums MENU_KICK = {FLOOR, NULL, OFFBEAT, NULL, SHAKE, NULL};
static const Drums MENU_GROOVE = {FLOOR, CLAP, OFFBEAT, NULL, SHAKE_LIVE, NULL};
static const Drums MENU_FULL = {FLOOR, CLAP, "g.o? g.og g.o. g?o.", ".... ..k. .j.? k...", SHAKE_LIVE, NULL};
static const Drums MENU_PEAK = {FLOOR, CLAP, "g.o? g.og g.o. g?o.", ".... ..k. .j.? k..?", SHAKE_LIVE, RIDE};
static const Drums MENU_FILL = {FLOOR, ".... c... .... c..c", "g.o. g.og g.o. o...", ".... ..k. .j.k j.j.", SHAKE, NULL};
static const Drums MENU_BREAK = {NULL, NULL, NULL, ".... ..k. .j.? k...", "x.X? x.X. x.X? x.X.", NULL};
static const Drums MENU_BREAK_FILL = {NULL, ".... .... .... c...", NULL, ".... ..k. .j.. k...", SHAKE, NULL};
static const Drums MENU_BRIDGE = {FLOOR, ".... c... .... c..?", "..o. ..o. ..o. ..o?", "..r. .... ..r. ...?", SHAKE_LIVE, NULL};
static const Section MENU_SEC[] = {
    {.bars = 4, .chords = MENU_A, .pad = "h", .arp = MENU_KEYS, .drums = &MENU_INTRO, .bright0 = 0.35f, .bright1 = 0.55f},
    {.bars = 4, .chords = MENU_A, .pad = "h", .arp = MENU_KEYS, .bass = MENU_BASS, .drums = &MENU_KICK, .bright0 = 0.55f,
     .bright1 = 0.75f},
    {.bars = 8, .chords = MENU_A, .pad = "h", .arp = MENU_KEYS, .bass = MENU_BASS, .drums = &MENU_GROOVE, .fill = &MENU_FILL,
     .bright0 = 0.8f, .bright1 = 0.9f},
    {.bars = 8, .chords = MENU_A, .pad = "h", .arp = MENU_KEYS, .bass = MENU_BASS, .lead = MENU_HOOK_A, .drums = &MENU_FULL,
     .fill = &MENU_FILL, .bright0 = 0.9f, .bright1 = 1.0f},
    {.bars = 8, .chords = MENU_B, .pad = "h", .arp = MENU_KEYS_2, .bass = MENU_BASS, .pluck = MENU_PLUCK, .lead = MENU_HOOK_B,
     .drums = &MENU_FULL, .fill = &MENU_FILL},
    {.bars = 8, .chords = MENU_A, .pad = "h", .arp = MENU_KEYS_2, .bass = MENU_BASS_2, .pluck = MENU_PLUCK, .lead = MENU_HOOK_A2,
     .drums = &MENU_FULL, .fill = &MENU_FILL},
    {.bars = 8, .chords = MENU_A, .pad = "h", .arp = MENU_KEYS, .pluck = MENU_PLUCK, .lead = MENU_HOOK_A, .drums = &MENU_BREAK,
     .fill = &MENU_BREAK_FILL, .bright0 = 0.5f, .bright1 = 0.85f, .flags = SEC_RISER | SEC_LIFT},
    {.bars = 8, .chords = MENU_C, .pad = "h", .arp = MENU_KEYS_C, .bass = MENU_BASS_C, .pluck = MENU_PLUCK_C, .drums = &MENU_BRIDGE,
     .fill = &MENU_FILL, .flags = SEC_CRASH | SEC_LIFT},
    {.bars = 8, .chords = MENU_A, .pad = "h", .arp = MENU_KEYS, .bass = MENU_BASS_2, .pluck = MENU_PLUCK, .drums = &MENU_PEAK,
     .fill = &MENU_FILL, .flags = SEC_CRASH},
    {.bars = 8, .chords = MENU_B, .pad = "h", .arp = MENU_KEYS_2, .bass = MENU_BASS, .pluck = MENU_PLUCK, .lead = MENU_HOOK_B,
     .drums = &MENU_PEAK, .fill = &MENU_FILL},
};

/* ------------------------------------------------------------ BRIEF: E minor, late-night deep house */
static const char BRIEF_A[] = "Em9:32 Cmaj9:32";
static const char BRIEF_KEYS[] = "C--- ---- --c- ----  ---- ---- c--- --c-";
static const char BRIEF_KEYS_C[] = "C--- ---- --c- ----";
static const char BRIEF_BASS[] = "r--. .... ..r. ....  r--. .... ..s. u...";
static const char BRIEF_PLUCK[] = "0... 1... 2... 3.2.  0... 1... 3... 2...";
static const char BRIEF_PLUCK_2[] = "0..1 ..2. 3..2 ..1.";
static const Drums BRIEF_INTRO = {NULL, NULL, NULL, "...r .... .... r...", "x.x? x.x. x.x? x.x.", NULL};
static const Drums BRIEF_GROOVE = {FLOOR, NULL, OFFBEAT, ".... r... .... r...", SHAKE_LIVE, NULL};
static const Drums BRIEF_PERC = {FLOOR, NULL, OFFBEAT, ".... r..k .j.? r...", SHAKE_LIVE, NULL};
static const Section BRIEF_SEC[] = {
    {.bars = 8, .chords = BRIEF_A, .pad = "h", .arp = BRIEF_KEYS, .pluck = BRIEF_PLUCK, .bass = "r--- ---- ---- ----",
     .drums = &BRIEF_INTRO, .bright0 = 0.6f, .bright1 = 0.8f},
    {.bars = 8, .chords = BRIEF_A, .pad = "h", .arp = BRIEF_KEYS, .bass = BRIEF_BASS, .drums = &BRIEF_GROOVE, .bright0 = 0.8f,
     .bright1 = 0.85f},
    {.bars = 8, .chords = "Am9:32 Bm7:32", .pad = "h", .arp = BRIEF_KEYS, .bass = BRIEF_BASS, .pluck = BRIEF_PLUCK_2,
     .drums = &BRIEF_PERC, .bright0 = 0.85f, .bright1 = 0.85f},
    {.bars = 8, .chords = "Cmaj9 Bm7 Am9 Bm7", .pad = "h", .arp = BRIEF_KEYS_C, .bass = "r--- ---- ---- ----", .pluck = BRIEF_PLUCK,
     .drums = &BRIEF_INTRO, .bright0 = 0.85f, .bright1 = 0.75f},
    {.bars = 8, .chords = BRIEF_A, .pad = "h", .arp = BRIEF_KEYS, .bass = BRIEF_BASS, .pluck = BRIEF_PLUCK_2, .drums = &BRIEF_PERC,
     .bright0 = 0.8f, .bright1 = 0.85f},
};

/* ------------------------------------------------------------ L1 Tycho: D minor, deep tech groove */
static const char L1_BASS[] = "r..r ..r. r..r ..o.  r..r ..r. r..u .or.";
static const char L1_BASS_B[] = "r..r ..r. r..s ..u.";
static const char L1_BASS_C[] = "r..r ..r. r..r ..o.";
static const char L1_STAB[] = "...c ..c. .... ..c.  .... ..c. ...c ....";
static const char L1_STAB_B[] = "...c ..c. .... ..c.  ...c ..c. .c.. ....";
static const char L1_STAB_C[] = "...c .... .... ..c.  .... ..c. .... ....";
static const char L1_PLUCK[] = "0..0 ..2. .1.. 3...  0..0 ..2. .1.. 4.3.";
static const char L1_PLUCK_B[] = "0.0. 2..1 ..3. .2..";
static const char L1_CHOP[] = "a4:1! r:1 a4:1 r:2 c5:2~ d5:3 r:6  r:8 f5:2 e5:2 d5:4  a4:1! r:1 a4:1 r:2 c5:2~ d5:3 r:6  r:16";
static const char L1_CHOP_B[] = "a4:1! r:1 a4:1 r:2 c5:2~ d5:3 r:6  r:8 f5:2 e5:2 c5:4  "
                                "a4:1! r:1 c5:1 r:2 d5:2~ e5:3 r:6  r:4 f5:2 e5:2 d5:2 c5:2 a4:4";
static const Drums L1_INTRO = {FLOOR, NULL, HATS16, NULL, NULL, NULL};
static const Drums L1_BUILD = {FLOOR, NULL, HATS_LIVE, NULL, SHAKE, NULL};
static const Drums L1_GROOVE = {FLOOR, CLAP, HATS_LIVE, NULL, SHAKE_LIVE, NULL};
static const Drums L1_FULL = {FLOOR, CLAP, HATS_LIVE, "...r ..r. ...? .r..", SHAKE_LIVE, NULL};
static const Drums L1_CONGA = {FLOOR, CLAP, HATS_LIVE, "...r ..r. ..k. .rj?", SHAKE_LIVE, NULL};
static const Drums L1_PEAK = {FLOOR, CLAP, HATS_LIVE, "...r ..r. ..k? .rj.", SHAKE_LIVE, RIDE};
static const Drums L1_FILL = {"x... x... x... ....", ".... c... .... c..c", HATS16, "...r ..r. .k.k j.j.", SHAKE, NULL};
static const Drums L1_BREAK = {NULL, NULL, NULL, NULL, "x.X? x.X. x.X? x.X.", NULL};
static const Drums L1_BREAK_FILL = {NULL, ".... .... .... c...", NULL, NULL, SHAKE, NULL};
static const Drums L1_STRIP = {FLOOR, CLAP, HATS_LIVE, "...r ..r. ...? .r..", NULL, NULL};
static const Section L1_SEC[] = {
    {.bars = 4, .chords = "Dm9", .drums = &L1_INTRO, .bright0 = 0.4f, .bright1 = 0.5f},
    {.bars = 4, .chords = "Dm9", .bass = L1_BASS, .drums = &L1_BUILD, .bright0 = 0.45f, .bright1 = 0.65f},
    {.bars = 8, .chords = "Dm9", .bass = L1_BASS, .arp = L1_STAB, .drums = &L1_GROOVE, .fill = &L1_FILL, .bright0 = 0.55f,
     .bright1 = 0.75f},
    {.bars = 8, .chords = "Dm9", .bass = L1_BASS, .arp = L1_STAB, .drums = &L1_FULL, .fill = &L1_FILL, .bright0 = 0.75f,
     .bright1 = 0.9f, .flags = SEC_CRASH},
    {.bars = 8, .chords = "Dm9", .bass = L1_BASS, .arp = L1_STAB_B, .pluck = L1_PLUCK, .drums = &L1_CONGA, .fill = &L1_FILL,
     .bright0 = 0.9f, .bright1 = 1.0f},
    {.bars = 8, .chords = "Gm9:32 Bbmaj9:32", .pad = "h", .bass = L1_BASS_B, .arp = L1_STAB_B, .pluck = L1_PLUCK,
     .drums = &L1_CONGA, .fill = &L1_FILL},
    {.bars = 8, .chords = "Dm9:32 Bbmaj9:32", .pad = "h", .arp = L1_STAB, .pluck = L1_PLUCK, .lead = L1_CHOP, .drums = &L1_BREAK,
     .fill = &L1_BREAK_FILL, .bright0 = 0.35f, .bright1 = 0.9f, .flags = SEC_RISER | SEC_LIFT},
    {.bars = 8, .chords = "Dm9", .bass = L1_BASS, .arp = L1_STAB, .pluck = L1_PLUCK, .lead = L1_CHOP, .drums = &L1_PEAK,
     .fill = &L1_FILL, .flags = SEC_CRASH},
    {.bars = 8, .chords = "Dm9", .bass = L1_BASS, .arp = L1_STAB_C, .pluck = L1_PLUCK_B, .lead = L1_CHOP_B, .drums = &L1_PEAK,
     .fill = &L1_FILL},
    {.bars = 8, .chords = "Bbmaj9 Am7 Gm9 Am7", .pad = "h", .bass = L1_BASS_C, .arp = L1_STAB_C, .pluck = L1_PLUCK_B,
     .drums = &L1_FULL, .fill = &L1_FILL},
    {.bars = 8, .chords = "Dm9", .bass = L1_BASS, .pluck = L1_PLUCK_B, .drums = &L1_STRIP, .fill = &L1_FILL, .bright0 = 0.8f,
     .bright1 = 0.6f},
};

/* ------------------------------------------------------------ L2 Io: E minor, acid house */
static const char L2_ACID_A[] = "e2:1 r e3! r  e2 d3~ e3 r  e2 r g2 e2  a2! r e2 b2~  "
                                "e2 r e3! r  e2 d3~ e3 r  g2 r a2 r  b2~ d3 e3! r";
static const char L2_ACID_B[] = "e2:1 e2 e3! e2  r e2 f2~ e2  e3! r e2 d3  r e2 g3! e2  "
                                "e2 e2 e3! e2  r e2 f2~ e2  b2! r d3~ e3  r g2 a2! b2";
static const char L2_ACID_C[] = "e3:1 r e2 e3!~  g3 e3 r d3~  e3 r b2! r  e3~ g3 a3! r  "
                                "e3 r e2 e3!~  g3 e3 r d3~  b2! r a2 r  g2~ e2 b2! d3";
static const char L2_STAB[] = "...c ..c. .... ..c.  .... ..c. ...c ....";
static const char L2_PLUCK[] = "0..1 ..2. 3..2 ..1.  0..1 ..2. 4..3 ..2.";
static const Drums L2_INTRO = {FLOOR, NULL, OFFBEAT, NULL, NULL, NULL};
static const Drums L2_BUILD = {FLOOR, CLAP, "g.o. g.o? g.o. g.og", NULL, SHAKE_LIVE, NULL};
static const Drums L2_FULL = {FLOOR, CLAP, HATS_LIVE, "..r. .r.. ..r? ...r", SHAKE_LIVE, NULL};
static const Drums L2_CONGA = {FLOOR, CLAP, HATS_LIVE, "..r. .r.. ..rk .rj?", SHAKE_LIVE, NULL};
static const Drums L2_PEAK = {FLOOR, CLAP, HATS_LIVE, "..r. .r.. ..r? ...r", SHAKE_LIVE, RIDE};
static const Drums L2_FILL = {"x... x... x... ....", ".... c... .... c..c", HATS16, "..r. .r.. .k.k j.j.", SHAKE, NULL};
static const Drums L2_BREAK = {NULL, NULL, NULL, NULL, "x.X? x.X. x.X? x.X.", NULL};
static const Drums L2_BREAK_FILL = {NULL, ".... .... .... c...", NULL, NULL, SHAKE, NULL};
static const Section L2_SEC[] = {
    {.bars = 8, .chords = "Em7", .lead = L2_ACID_A, .drums = &L2_INTRO, .bright0 = 0.25f, .bright1 = 0.45f},
    {.bars = 8, .chords = "Em7", .lead = L2_ACID_A, .drums = &L2_BUILD, .fill = &L2_FILL, .bright0 = 0.45f, .bright1 = 0.6f},
    {.bars = 8, .chords = "Em9", .arp = L2_STAB, .lead = L2_ACID_B, .drums = &L2_FULL, .fill = &L2_FILL, .bright0 = 0.55f,
     .bright1 = 0.7f, .flags = SEC_CRASH},
    {.bars = 8, .chords = "Em9", .arp = L2_STAB, .lead = L2_ACID_B, .drums = &L2_CONGA, .fill = &L2_FILL, .bright0 = 0.7f,
     .bright1 = 0.85f},
    {.bars = 8, .chords = "Em9:32 Cmaj9:32", .pad = "h", .pluck = L2_PLUCK, .lead = L2_ACID_A, .drums = &L2_BREAK,
     .fill = &L2_BREAK_FILL, .bright0 = 0.25f, .bright1 = 0.6f, .flags = SEC_RISER | SEC_LIFT},
    {.bars = 8, .chords = "Em9", .pad = "h", .arp = L2_STAB, .lead = L2_ACID_B, .drums = &L2_PEAK, .fill = &L2_FILL,
     .bright0 = 0.7f, .bright1 = 0.9f, .flags = SEC_CRASH},
    {.bars = 8, .chords = "Em9", .pad = "h", .arp = L2_STAB, .pluck = L2_PLUCK, .lead = L2_ACID_C, .drums = &L2_PEAK,
     .fill = &L2_FILL, .bright0 = 0.9f, .bright1 = 1.0f},
    {.bars = 8, .chords = "Cmaj7:32 Am7:32", .pad = "h", .arp = L2_STAB, .lead = L2_ACID_A, .drums = &L2_PEAK, .fill = &L2_FILL,
     .bright0 = 0.8f, .bright1 = 0.6f},
    {.bars = 8, .chords = "Em7", .lead = L2_ACID_A, .drums = &L2_FULL, .fill = &L2_FILL, .bright0 = 0.5f, .bright1 = 0.35f},
};

/* ------------------------------------------------------------ L3 Ceres: C minor, dark tech house */
static const char L3_BASS[] = ".r.r .rr. .r.r .ro.  .r.r .rr. .r.. r.u.";
static const char L3_STAB[] = "..c. .... c... .c..  ..c. .... ..c. ....";
static const char L3_STAB_B[] = "..c. .... c... .c..  ..c. .c.. ...c ....";
static const char L3_PLUCK[] = "0.2. ..3. .1.. 2...  0.2. ..3. .4.. 3.1.";
static const char L3_RIFF[] = "g4:1 r:2 g4:1 r:2 bb4:1 r:1 c5:2 r:6  r:16";
static const char L3_RIFF_B[] = "g4:1 r:2 g4:1 r:2 bb4:1 r:1 c5:2 r:6  r:4 eb5:1 r:1 d5:1 r:1 c5:2 bb4:2 r:4";
static const char L3_PERC[] = "..r. .r.. ..r? r...";
static const Drums L3_INTRO = {FLOOR, NULL, HATS16, L3_PERC, NULL, NULL};
static const Drums L3_BUILD = {FLOOR, NULL, HATS_LIVE, L3_PERC, SHAKE, NULL};
static const Drums L3_GROOVE = {FLOOR, ".... b... .... b...", HATS_LIVE, L3_PERC, SHAKE_LIVE, NULL};
static const Drums L3_CONGA = {FLOOR, ".... b... .... b..?", HATS_LIVE, "..r. .r.. ..rk r.j?", SHAKE_LIVE, NULL};
static const Drums L3_PEAK = {FLOOR, ".... b... .... b...", HATS_LIVE, "..r. .r.. ..rk r.j.", SHAKE_LIVE, RIDE};
static const Drums L3_FILL = {"x... x... x... ....", ".... b... .... b..c", HATS16, "..r. .r.. h.m. l...", SHAKE, NULL};
static const Drums L3_BREAK = {NULL, NULL, OFFBEAT, NULL, SHAKE_LIVE, NULL};
static const Drums L3_BREAK_FILL = {NULL, ".... .... .... c...", OFFBEAT, ".... .... h.m. l.l.", SHAKE, NULL};
static const Drums L3_STRIP = {FLOOR, ".... b... .... b...", HATS_LIVE, L3_PERC, NULL, NULL};
static const Section L3_SEC[] = {
    {.bars = 4, .chords = "Cm9", .drums = &L3_INTRO, .bright0 = 0.4f, .bright1 = 0.5f},
    {.bars = 4, .chords = "Cm9", .bass = L3_BASS, .drums = &L3_BUILD, .bright0 = 0.45f, .bright1 = 0.65f},
    {.bars = 8, .chords = "Cm9", .bass = L3_BASS, .arp = L3_STAB, .drums = &L3_GROOVE, .fill = &L3_FILL, .bright0 = 0.55f,
     .bright1 = 0.75f},
    {.bars = 8, .chords = "Cm9", .bass = L3_BASS, .arp = L3_STAB, .drums = &L3_GROOVE, .fill = &L3_FILL, .bright0 = 0.75f,
     .bright1 = 0.9f, .flags = SEC_CRASH},
    {.bars = 8, .chords = "Cm9", .bass = L3_BASS, .arp = L3_STAB_B, .pluck = L3_PLUCK, .drums = &L3_CONGA, .fill = &L3_FILL,
     .bright0 = 0.9f, .bright1 = 1.0f},
    {.bars = 8, .chords = "Abmaj7:32 Bb6:32", .pad = "h", .bass = L3_BASS, .arp = L3_STAB, .pluck = L3_PLUCK, .lead = L3_RIFF,
     .drums = &L3_GROOVE, .fill = &L3_FILL},
    {.bars = 8, .chords = "Cm9:32 Abmaj7:32", .pad = "h", .arp = L3_STAB, .pluck = L3_PLUCK, .drums = &L3_BREAK,
     .fill = &L3_BREAK_FILL, .bright0 = 0.3f, .bright1 = 0.9f, .flags = SEC_RISER | SEC_LIFT},
    {.bars = 8, .chords = "Cm9", .bass = L3_BASS, .arp = L3_STAB, .lead = L3_RIFF, .drums = &L3_PEAK, .fill = &L3_FILL,
     .flags = SEC_CRASH},
    {.bars = 8, .chords = "Cm9", .bass = L3_BASS, .arp = L3_STAB_B, .pluck = L3_PLUCK, .lead = L3_RIFF_B, .drums = &L3_PEAK,
     .fill = &L3_FILL},
    {.bars = 8, .chords = "Fm9:32 Abmaj7:16 Bb6:16", .pad = "h", .bass = L3_BASS, .arp = L3_STAB_B, .pluck = L3_PLUCK,
     .drums = &L3_PEAK, .fill = &L3_FILL},
    {.bars = 8, .chords = "Cm9", .bass = L3_BASS, .drums = &L3_STRIP, .fill = &L3_FILL, .bright0 = 0.8f, .bright1 = 0.6f},
};

/* ------------------------------------------------------------ BOSS: F minor, acid techno-house */
static const char BOSS_BASS[] = "..Rr ..Rr ..Rr ..Ro";
static const char BOSS_STAB[] = "C..c ..c. .... ....  C..c ..c. ..c. .c..";
static const char BOSS_PLUCK[] = "0.1. 0.2. 0.1. 0.3.  0.1. 0.2. 0.3. 0.4.";
static const char BOSS_ACID[] = "f3:1 r f4! f3  r f3 gb3~ f3  c4! r f3 eb4  r f3 ab4! f3  "
                                "f3 r f4! f3  r f3 gb3~ f3  eb4! r c4~ bb3  r ab3 c4! f4  "
                                "f3 r f4! f3  r f3 gb3~ f3  db4! r f3 bb3  r f3 ab4! f3  "
                                "f3 r f4! f3  r f3 gb3~ f3  db4! r bb3~ ab3  r gb3 bb3! db4";
static const Drums BOSS_A = {FLOOR, ".... b... .... b...", HATS_LIVE, "..r. .r.. ..r? r...", NULL, NULL};
static const Drums BOSS_B = {FLOOR, ".... b... .... b..?", HATS_LIVE, "..r. .r.. ..r? r.r.", SHAKE_LIVE, RIDE};
static const Drums BOSS_FILL = {"x... x... x... ....", ".... b... .... b.g.", HATS16, ".... .... h.m. l.l.", NULL, NULL};
static const Drums BOSS_TOMS = {"x... .... x... ....", NULL, OFFBEAT, "l..l ..m. h..h ..m.", SHAKE, NULL};
static const Drums BOSS_TOMS_FILL = {"x... .... x... ....", ".... .... c... c...", OFFBEAT, "l..l ..m. h.h. m.l.", SHAKE, NULL};
static const Section BOSS_SEC[] = {
    {.bars = 8, .chords = "Fm7:32 Gbmaj7:32", .pad = "h", .bass = BOSS_BASS, .arp = BOSS_STAB, .drums = &BOSS_A,
     .fill = &BOSS_FILL, .bright0 = 0.75f, .bright1 = 1.0f, .flags = SEC_CRASH},
    {.bars = 8, .chords = "Fm7:32 Gbmaj7:32", .pad = "h", .bass = BOSS_BASS, .arp = BOSS_STAB, .lead = BOSS_ACID,
     .drums = &BOSS_A, .fill = &BOSS_FILL, .bright0 = 0.7f, .bright1 = 1.0f},
    {.bars = 8, .chords = "Dbmaj7:32 C7:32", .pad = "h", .bass = BOSS_BASS, .arp = BOSS_STAB, .pluck = BOSS_PLUCK,
     .drums = &BOSS_B, .fill = &BOSS_FILL, .flags = SEC_CRASH},
    {.bars = 8, .chords = "Fm7:32 Gbmaj7:32", .pad = "h", .bass = BOSS_BASS, .arp = BOSS_STAB, .pluck = BOSS_PLUCK,
     .lead = BOSS_ACID, .drums = &BOSS_B, .fill = &BOSS_FILL},
    {.bars = 4, .chords = "Fm7:32 Gbmaj7:32", .pad = "h", .bass = "r--- ---- r--- ----", .lead = BOSS_ACID, .drums = &BOSS_TOMS,
     .fill = &BOSS_TOMS_FILL, .bright0 = 0.3f, .bright1 = 0.9f, .flags = SEC_RISER | SEC_LIFT},
};

/* ------------------------------------------------------------ ESCAPE: G minor, driving melodic tech */
static const char ESC_BASS[] = "..r. .rr. ..r. .rr.";
static const char ESC_ARP[] = "0242 1353 0242 1364";
static const char ESC_ARP_B[] = "4202 5313 4202 5343";
static const char ESC_PLUCK[] = "0... ..3. .... 2...  0... ..1. .... 3...";
static const Drums ESC_A = {FLOOR, CLAP, HATS_LIVE, "..r. ..r. ..r? .r..", NULL, NULL};
static const Drums ESC_B = {FLOOR, ".... b... .... b..?", HATS_LIVE, "..r. ..r. ..r? .r..", SHAKE_LIVE, RIDE};
static const Drums ESC_FILL = {"x... x... x... ....", ".... c... .... c..c", HATS16, "..r. ..r. h.m. l...", SHAKE, NULL};
static const Drums ESC_BUILD = {FLOOR, NULL, OFFBEAT, NULL, "xxxx xxxx xxxx xxxx", NULL};
static const Drums ESC_BUILD_FILL = {FLOOR, ".... c... c... c.c.", OFFBEAT, NULL, SHAKE, NULL};
static const Section ESC_SEC[] = {
    {.bars = 8, .chords = "Gm9:32 Ebmaj9:32", .bass = ESC_BASS, .arp = ESC_ARP, .drums = &ESC_A, .fill = &ESC_FILL,
     .bright0 = 0.6f, .bright1 = 0.9f, .flags = SEC_CRASH},
    {.bars = 8, .chords = "Gm9:32 Ebmaj9:32", .pad = "h", .bass = ESC_BASS, .arp = ESC_ARP, .pluck = ESC_PLUCK, .drums = &ESC_A,
     .fill = &ESC_FILL, .bright0 = 0.9f, .bright1 = 1.0f},
    {.bars = 8, .chords = "Cm9:32 D7:32", .pad = "h", .bass = ESC_BASS, .arp = ESC_ARP, .pluck = ESC_PLUCK, .drums = &ESC_B,
     .fill = &ESC_FILL, .flags = SEC_CRASH},
    {.bars = 8, .chords = "Gm9:32 Ebmaj9:32", .pad = "h", .bass = ESC_BASS, .arp = ESC_ARP_B, .pluck = ESC_PLUCK, .drums = &ESC_B,
     .fill = &ESC_FILL},
    {.bars = 4, .chords = "Cm9:32 D7:32", .pad = "h", .arp = ESC_ARP, .drums = &ESC_BUILD, .fill = &ESC_BUILD_FILL,
     .bright0 = 0.35f, .bright1 = 1.0f, .flags = SEC_RISER | SEC_LIFT},
};

/* ------------------------------------------------------------ GAME OVER: A minor, dub chords */
static const char GAMEOVER_CHORDS[] = "Am9:32 Fmaj9:32 Dm9:32 Em7:32";
static const Section GAMEOVER_SEC[] = {
    {.bars = 8, .chords = GAMEOVER_CHORDS, .pad = "h", .arp = "C--- ---- ---- ----  ---- ---- c--- ----",
     .bass = "r--- ---- ---- ----", .bright0 = 0.7f, .bright1 = 0.6f},
    {.bars = 8, .chords = GAMEOVER_CHORDS, .pad = "h", .arp = "C--- ---- --c- ----  ---- c--- ---- ----",
     .pluck = "3... .... 2... ....  1... .... 0... ....", .bass = "r--- ---- ---- ----", .bright0 = 0.6f, .bright1 = 0.5f},
};

/* ------------------------------------------------------------ VICTORY: C major, warm keys house */
static const char VIC_CHORDS[] = "Fmaj9:32 Em7:32 Dm9:32 Cmaj9:32";
static const char VIC_KEYS[] = "C..c ..c. ..c. ....  ..c. ..c. c... ....";
static const char VIC_BASS[] = "r... ..r. ..o. r.s.  r... ..r. .u.. r.o.";
static const char VIC_PLUCK[] = "0.2. 1.3. 2.4. 3.1.  0.2. 1.3. 4.3. 2.1.";
static const char VIC_HOOK[] = "a4:2 c5:2 e5:3 d5:3 c5:2~ e5:4  r:16  g5:3 e5:3 d5:2~ b4:8  r:16";
static const char VIC_HOOK_2[] = "a4:2 c5:2 e5:3 d5:3 c5:2~ e5:4  r:16  g5:3 e5:3 d5:2~ b4:4 d5:4  r:8 e5:2 g5:2 e5:2 d5:2";
static const Drums VIC_INTRO = {FLOOR, NULL, OFFBEAT, NULL, SHAKE, NULL};
static const Drums VIC_MAIN = {FLOOR, CLAP, OFFBEAT, ".k.. j.k. .k.? j...", SHAKE_LIVE, NULL};
static const Drums VIC_PEAK = {FLOOR, CLAP, "g.o? g.og g.o. g?o.", ".k.. j.k. .k.? j..?", SHAKE_LIVE, RIDE};
static const Drums VIC_LIGHT = {FLOOR, NULL, OFFBEAT, ".k.. j.k. .k.? j...", SHAKE_LIVE, "x.X? x.X. x.X? x.X."};
static const Drums VIC_FILL = {FLOOR, ".... c... .... c..c", OFFBEAT, ".k.. j.k. .kjk j.j.", SHAKE, NULL};
static const Section VICTORY_SEC[] = {
    {.bars = 4, .chords = "Fmaj9:32 Em7:32", .pad = "h", .arp = VIC_KEYS, .drums = &VIC_INTRO, .bright0 = 0.5f, .bright1 = 0.9f,
     .flags = SEC_RISER},
    {.bars = 8, .chords = VIC_CHORDS, .pad = "h", .arp = VIC_KEYS, .bass = VIC_BASS, .drums = &VIC_MAIN, .fill = &VIC_FILL,
     .flags = SEC_CRASH},
    {.bars = 8, .chords = VIC_CHORDS, .pad = "h", .arp = VIC_KEYS, .bass = VIC_BASS, .lead = VIC_HOOK, .drums = &VIC_MAIN,
     .fill = &VIC_FILL},
    {.bars = 8, .chords = VIC_CHORDS, .pad = "h", .arp = VIC_KEYS, .bass = VIC_BASS, .pluck = VIC_PLUCK, .lead = VIC_HOOK_2,
     .drums = &VIC_PEAK, .fill = &VIC_FILL},
    {.bars = 8, .chords = "Dm9:32 Em7:32", .pad = "h", .arp = VIC_KEYS, .bass = VIC_BASS, .pluck = VIC_PLUCK, .drums = &VIC_LIGHT,
     .fill = &VIC_FILL, .flags = SEC_LIFT},
};

static const Song SONGS[SONG_COUNT] = {
    [SONG_MENU] = {.bpm = 120, .swing = 0.12f, SECS(MENU_SEC), .loop = 2, .pad = &PAD_DEEP, .bass = &BASS_DEEP, .arp = &KEYS_WARM,
                   .pluck = &PLUCK_HARP, .lead = &LEAD_VOX, .bass_lo = 40, .pad_lo = 55, .pad_hi = 74, .pluck_shift = 12,
                   .sidechain = 0.4f, .delay_steps = 3, .delay_fb = 0.45f, .delay_mix = 0.5f, .rev_time = 2.4f, .rev_damp = 0.45f,
                   .rev_mix = 0.9f, .kick_hz = 50, .kick_decay = 0.17f, .drums = 0.9f, .hats = 1.0f, .gain = 0.95f},
    [SONG_L1] = {.bpm = 122, .swing = 0.08f, SECS(L1_SEC), .loop = 2, .pad = &PAD_DEEP, .bass = &BASS_DEEP, .arp = &STAB_DUB,
                 .pluck = &PLUCK_DUB, .lead = &LEAD_CHOP, .bass_lo = 40, .pad_lo = 55, .pad_hi = 74, .pluck_shift = 12,
                 .sidechain = 0.5f, .delay_steps = 3, .delay_fb = 0.5f, .delay_mix = 0.5f, .rev_time = 2.2f, .rev_damp = 0.45f,
                 .rev_mix = 0.8f, .kick_hz = 50, .kick_decay = 0.16f, .drums = 1.0f, .hats = 0.7f, .gain = 1.0f},
    [SONG_L2] = {.bpm = 124, .swing = 0.06f, SECS(L2_SEC), .loop = 1, .pad = &PAD_DARK, .bass = &BASS_DEEP, .arp = &STAB_DUB,
                 .pluck = &PLUCK_BELL, .lead = &ACID, .bass_lo = 40, .pad_lo = 52, .pad_hi = 71, .pluck_shift = 12,
                 .sidechain = 0.45f, .delay_steps = 3, .delay_fb = 0.4f, .delay_mix = 0.4f, .rev_time = 1.6f, .rev_damp = 0.5f,
                 .rev_mix = 0.7f, .kick_hz = 48, .kick_decay = 0.15f, .drums = 1.0f, .hats = 0.7f, .gain = 1.0f},
    [SONG_L3] = {.bpm = 126, .swing = 0.05f, SECS(L3_SEC), .loop = 2, .pad = &PAD_VOX, .bass = &BASS_ROLL, .arp = &STAB_TECH,
                 .pluck = &PLUCK_BELL, .lead = &LEAD_STRING, .bass_lo = 40, .pad_lo = 55, .pad_hi = 74, .pluck_shift = 12,
                 .sidechain = 0.5f, .delay_steps = 3, .delay_fb = 0.45f, .delay_mix = 0.45f, .rev_time = 2.2f, .rev_damp = 0.45f,
                 .rev_mix = 0.8f, .kick_hz = 48, .kick_decay = 0.14f, .drums = 1.0f, .hats = 0.7f, .gain = 1.0f},
    [SONG_BOSS] = {.bpm = 128, SECS(BOSS_SEC), .loop = 0, .pad = &PAD_DARK, .bass = &BASS_GRIT, .arp = &STAB_TECH,
                   .pluck = &PLUCK_BELL, .lead = &ACID, .bass_lo = 40, .pad_lo = 53, .pad_hi = 72, .pluck_shift = 12,
                   .sidechain = 0.5f, .delay_steps = 3, .delay_fb = 0.35f, .delay_mix = 0.4f, .rev_time = 1.8f, .rev_damp = 0.5f,
                   .rev_mix = 0.75f, .kick_hz = 47, .kick_decay = 0.14f, .drums = 1.0f, .hats = 0.7f, .gain = 1.0f},
    [SONG_ESCAPE] = {.bpm = 130, SECS(ESC_SEC), .loop = 0, .pad = &PAD_DARK, .bass = &BASS_ROLL, .arp = &ARP_SEQ,
                     .pluck = &PLUCK_BELL, .lead = &LEAD_CHOP, .bass_lo = 40, .pad_lo = 53, .pad_hi = 72, .pluck_shift = 12,
                     .sidechain = 0.45f, .delay_steps = 3, .delay_fb = 0.35f, .delay_mix = 0.4f, .rev_time = 1.6f,
                     .rev_damp = 0.5f, .rev_mix = 0.7f, .kick_hz = 50, .kick_decay = 0.13f, .drums = 1.0f, .hats = 0.7f,
                     .gain = 1.0f},
    [SONG_BRIEF] = {.bpm = 112, .swing = 0.14f, SECS(BRIEF_SEC), .loop = 1, .pad = &PAD_WARM, .bass = &BASS_DEEP,
                    .arp = &KEYS_WARM, .pluck = &PLUCK_HARP, .lead = &LEAD_VOX, .bass_lo = 40, .pad_lo = 55, .pad_hi = 74,
                    .pluck_shift = 12, .sidechain = 0.3f, .delay_steps = 3, .delay_fb = 0.5f, .delay_mix = 0.5f, .rev_time = 3.0f,
                    .rev_damp = 0.35f, .rev_mix = 1.0f, .kick_hz = 50, .kick_decay = 0.15f, .drums = 0.6f, .hats = 0.8f,
                    .gain = 0.9f},
    [SONG_GAMEOVER] = {.bpm = 88, SECS(GAMEOVER_SEC), .loop = 0, .pad = &PAD_VOX, .bass = &BASS_SOFT, .arp = &KEYS_WARM,
                       .pluck = &PLUCK_HARP, .lead = &LEAD_VOX, .bass_lo = 40, .pad_lo = 55, .pad_hi = 74, .pluck_shift = 12,
                       .delay_steps = 3, .delay_fb = 0.55f, .delay_mix = 0.55f, .rev_time = 3.5f, .rev_damp = 0.35f,
                       .rev_mix = 0.9f, .kick_hz = 50, .kick_decay = 0.12f, .drums = 0.5f, .hats = 0.5f, .gain = 0.85f},
    [SONG_VICTORY] = {.bpm = 122, .swing = 0.1f, SECS(VICTORY_SEC), .loop = 1, .pad = &PAD_WARM, .bass = &BASS_DEEP,
                      .arp = &KEYS_STAB, .pluck = &PLUCK_HARP, .lead = &LEAD_VOX, .bass_lo = 40, .pad_lo = 55, .pad_hi = 74,
                      .pluck_shift = 12, .sidechain = 0.4f, .delay_steps = 3, .delay_fb = 0.4f, .delay_mix = 0.45f,
                      .rev_time = 2.2f, .rev_damp = 0.4f, .rev_mix = 0.85f, .kick_hz = 50, .kick_decay = 0.16f, .drums = 0.9f,
                      .hats = 1.0f, .gain = 1.0f},
};

/* ------------------------------------------------------------ parsed songs */
typedef struct { int root, bass, n; int8_t iv[4]; } Chord;
typedef struct { char s[MAX_PAT + 1]; int n; } Pat;
typedef struct {
    int steps, riser_at;
    float b0, b1;
    uint8_t chord[MAX_STEPS];
    int8_t lead[MAX_STEPS];
    uint8_t lead_fx[MAX_STEPS];
    Pat pad, bass, arp, pluck, drum[NLANE], fill[NLANE];
} SecData;
typedef struct {
    Chord chords[MAX_CHORDS];
    int nchords;
    SecData *sec;
} SongData;
static SongData SD[SONG_COUNT];

/* ------------------------------------------------------------ synth state */
typedef struct { int stage; float lvl; } Env; /* stage: 0 off, 1 attack, 2 decay/sustain, 3 release */
typedef struct { float ic1, ic2; } Svf;
typedef struct { float k, a1, a2, a3; } SvfCo;
typedef struct {
    const Patch *p;
    float att, dec, rel, fdec, glide; /* per-sample (glide: per control tick) coefficients */
    float bmul;                       /* cutoff multiplier from the section brightness */
} Inst;

typedef struct {
    bool active, gate, damped; /* damped: released quickly, a string muted by hand */
    int note;
    float pitch, target, vel, pan, age_t;
    float ph[NUNI], sph, mul[NUNI], wl[NUNI], wr[NUNI], det[NUNI], drift[NUNI], jit; /* det: cents */
    float dt, fenv, fg[3];
    Env env;
    Svf fl, fr, sl, sr, vl[3], vr[3]; /* lowpass, its second stage when steep, formants */
    SvfCo co, co2, vco[3];
    uint32_t age;
    float ks_del, ks_g; /* OSC_STRING: loop delay in samples, gain per pass */
    int ks_pos;
    float ks[KS_MAX]; /* keep last: voice_start clears only what comes before it */
} Voice;

typedef struct {
    float k_ph, k_amp, k_p1, k_p2, k_click, k_vel;
    int k_hold;
    Svf k_f;
    float s_ph1, s_ph2, s_body, s_snap, s_lp, s_vel, s_pitch;
    Svf s_f;
    float c_t, c_vel;
    Svf c_f, c_f2;
    float h_amp, h_dec, h_vel, h_lp, h_lp2, h_ph[6];
    Svf h_bp, h_hp;
    float d_amp, d_vel, d_lp, d_ph[6];
    Svf d_bp;
    float t_ph, t_ph2, t_amp, t_amp2, t_pitch, t_hz, t_pan, t_dec, t_bend, t_lvl, t_slap;
    Svf t_f;
    float m_ph1, m_ph2, m_amp;
    float z_amp, z_env, z_vel, z_lp;
    Svf z_f;
    float y_amp, y_lpl, y_lpr;
    Svf y_fl, y_fr;
    float r_t, r_len;
    bool r_on;
    Svf r_f;
    SvfCo r_co;
    float w_t, w_len, w_lpl, w_lpr; /* reverse cymbal swell into a drop */
    Svf w_fl, w_fr;
    float i_ph, i_amp, i_p; /* sub impact on the drop */
} Drum;

typedef struct { int wait; uint8_t kind; char arg; float vel; } Hit;
#define NHITS 32

/* fixed drum coefficients */
static struct {
    float k_p1, k_p2, k_click, s_body, s_snap, s_pitch, g_amp2, i_amp, i_p, h_closed, h_open, d_amp, t_amp, t_pitch, g_amp, g_slap, m_amp, z_amp, z_att,
        y_amp, air, damp;
    SvfCo kclick, snare, swell, hat_bp, hat_hp, ride, clap, slap, shaker, crash;
} DC;

static struct {
    int song, target;
    float fade;
    int sec, step, roll, ctl;
    double step_left, step_len;
    int chord, voicing[4], nv;
    float bright, bright_target, lfo_ph, lfo;
    int arp_side, plk_side;
    bool lead_slide, lift_on;
    Inst ipad, ibass, iarp, iplk, ilead;
    Voice pad[NPAD], arp[NARP], plk[NPLK], bass, lead;
    float lift_t, lift_len, lift_w, gap_w;
    bool gap_on;
    unsigned prev_flags, cur_flags;
    Hit hits[NHITS];
    int nhits;
    Svf hp_l, hp_r;
    SvfCo hp_co;
    Drum dr;
    float k_dec, sc, sc_hold, sc_rel;
    float *dl, *drr;
    int dlen, dpos;
    float d_lp, d_hp, r_hp;
    float *cb, ch_ph, ch_hp;
    int cpos;
    float dc_xl, dc_xr, dc_yl, dc_yr;
    float c_env, c_gain, c_target;
    uint32_t age;
} M;

static uint32_t rng = 0x2545F491u;
static inline float noise(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (float)(int32_t)rng * (1.0f / 2147483648.0f);
}

/* ------------------------------------------------------------ DSP helpers */
static inline float polyblep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
static inline float osc_saw(float ph, float dt) { return 2.0f * ph - 1.0f - polyblep(ph, dt); }
static inline float osc_pulse(float ph, float dt, float pw) {
    float v = ph < pw ? 1.0f : -1.0f;
    float t = ph - pw;
    if (t < 0) t += 1.0f;
    return v + polyblep(ph, dt) - polyblep(t, dt) - (2.0f * pw - 1.0f);
}
static inline float osc_tri(float ph) { return 4.0f * fabsf(ph - 0.5f) - 1.0f; }

static inline float sat(float x) {
    if (x > 3) return 1;
    if (x < -3) return -1;
    return x * (27 + x * x) / (27 + 9 * x * x);
}

/* topology-preserving state variable filter; k is 1/Q */
static void svf_setk(SvfCo *c, float fc, float k) {
    float g = tanf(PI * clampf(fc, 20.0f, 18000.0f) / RATE);
    c->k = k;
    c->a1 = 1.0f / (1.0f + g * (g + c->k));
    c->a2 = g * c->a1;
    c->a3 = g * c->a2;
}
static void svf_set(SvfCo *c, float fc, float reso) { svf_setk(c, fc, 2.0f - 1.96f * reso); }
static inline float svf_tick(Svf *s, const SvfCo *c, float x, float *bp) {
    float v3 = x - s->ic2;
    float v1 = c->a1 * s->ic1 + c->a2 * v3;
    float v2 = s->ic2 + c->a2 * s->ic1 + c->a3 * v3;
    s->ic1 = 2 * v1 - s->ic1;
    s->ic2 = 2 * v2 - s->ic2;
    *bp = v1;
    return v2;
}
static inline float svf_lp(Svf *s, const SvfCo *c, float x) {
    float bp;
    return svf_tick(s, c, x, &bp);
}
static inline float svf_hp(Svf *s, const SvfCo *c, float x) {
    float bp, lp = svf_tick(s, c, x, &bp);
    return x - c->k * bp - lp;
}
/* bandpass with unity gain at the centre */
static inline float svf_bp(Svf *s, const SvfCo *c, float x) {
    float bp;
    svf_tick(s, c, x, &bp);
    return bp * c->k;
}

/* per-sample coefficient that decays to 1% after `sec` seconds */
static float decay_coef(float sec) { return sec > 0 ? 1.0f - expf(-4.6f / (sec * RATE)) : 1.0f; }
static float decay_mul(float sec) { return 1.0f - decay_coef(sec); }
/* one-pole lowpass coefficient */
static float onepole(float hz) { return 1.0f - expf(-TAU * hz / RATE); }

/* ------------------------------------------------------------ reverb: feedback delay network */
/* sixteen delay lines mixed through a Householder matrix, each damped by a one-pole lowpass; the input is
 * diffused by four allpasses after a short pre-delay, and four lines are slowly modulated so the tail
 * never rings metallic */
#define NFDN 16
#define PRE_MAX 4096 /* pre-delay line, a power of two */
#define FDN_SLACK 32 /* room for the modulated taps */
static const int FDN_LEN[NFDN] = {887, 1009, 1123, 1249, 1361, 1471, 1579, 1693, 1801, 1913, 2017, 2129, 2237, 2341, 2447, 2551};
static const int DIFF_LEN[4] = {142, 107, 379, 277};
static struct {
    float *line[NFDN], *ap[4], *pre;
    int pos[NFDN], ap_pos[4], pre_pos, pre_len;
    float lp[NFDN], g[NFDN], damp, mod[4], mod_ph[4];
} RV;
static float *rev_mem;
static size_t rev_len;

static void reverb_init(void) {
    rev_len = PRE_MAX;
    for (int i = 0; i < NFDN; i++) rev_len += (size_t)(FDN_LEN[i] + FDN_SLACK);
    for (int i = 0; i < 4; i++) rev_len += (size_t)DIFF_LEN[i];
    rev_mem = (float *)calloc(rev_len, sizeof(float));
    float *p = rev_mem;
    RV.pre = p;
    p += PRE_MAX;
    for (int i = 0; i < NFDN; i++) {
        RV.line[i] = p;
        p += FDN_LEN[i] + FDN_SLACK;
    }
    for (int i = 0; i < 4; i++) {
        RV.ap[i] = p;
        p += DIFF_LEN[i];
    }
}

static void reverb_clear(void) {
    if (rev_mem) memset(rev_mem, 0, rev_len * sizeof(float));
    memset(RV.lp, 0, sizeof(RV.lp));
}

/* t60: seconds to fall 60 dB; damp 0..1 darkens the tail */
static void reverb_set(float t60, float damp) {
    for (int i = 0; i < NFDN; i++) RV.g[i] = powf(10.0f, -3.0f * (float)FDN_LEN[i] / (t60 * RATE));
    RV.damp = onepole(9000.0f * (1.0f - damp) + 800.0f);
    RV.pre_len = (int)(0.018f * RATE);
}

static void reverb_mod(void) { /* control rate */
    static const float HZ[4] = {0.63f, 0.87f, 1.13f, 0.51f};
    for (int i = 0; i < 4; i++) {
        if ((RV.mod_ph[i] += CTL * HZ[i] / RATE) >= 1) RV.mod_ph[i] -= 1;
        RV.mod[i] = 9.0f * sinf(TAU * RV.mod_ph[i]);
    }
}

static void reverb_tick(float in, float *out) {
    RV.pre[RV.pre_pos] = in;
    float x = RV.pre[(RV.pre_pos - RV.pre_len) & (PRE_MAX - 1)];
    RV.pre_pos = (RV.pre_pos + 1) & (PRE_MAX - 1);
    for (int k = 0; k < 4; k++) {
        float *buf = RV.ap[k];
        float d = buf[RV.ap_pos[k]], w = x + 0.6f * d;
        x = d - 0.6f * w;
        buf[RV.ap_pos[k]] = w;
        if (++RV.ap_pos[k] >= DIFF_LEN[k]) RV.ap_pos[k] = 0;
    }
    float v[NFDN], sum = 0;
    for (int i = 0; i < NFDN; i++) {
        int size = FDN_LEN[i] + FDN_SLACK;
        float rp = (float)RV.pos[i] - ((float)FDN_LEN[i] + (i < 4 ? RV.mod[i] : 0));
        if (rp < 0) rp += (float)size;
        int i0 = (int)rp, i1 = i0 + 1 < size ? i0 + 1 : 0;
        float fr = rp - (float)i0;
        float y = RV.line[i][i0] + (RV.line[i][i1] - RV.line[i][i0]) * fr;
        RV.lp[i] += RV.damp * (y - RV.lp[i]);
        v[i] = RV.lp[i];
        sum += v[i];
    }
    sum *= 2.0f / NFDN;
    for (int i = 0; i < NFDN; i++) {
        RV.line[i][RV.pos[i]] = (v[i] - sum) * RV.g[i] + (i & 1 ? -x : x);
        if (++RV.pos[i] >= FDN_LEN[i] + FDN_SLACK) RV.pos[i] = 0;
    }
    float l = 0, r = 0;
    for (int i = 0; i < NFDN; i += 4) {
        l += v[i] - v[i + 2];
        r += v[i + 1] - v[i + 3];
    }
    out[0] = l * 0.35f;
    out[1] = r * 0.35f;
}

/* ------------------------------------------------------------ voices */
/* formants of the vowels oo, oh and ah: centre frequencies and levels */
static const float VOWEL_F[3][3] = {{350, 800, 2700}, {500, 900, 2800}, {800, 1200, 2800}};
static const float VOWEL_G[3][3] = {{0.8f, 0.7f, 0.35f}, {0.8f, 0.8f, 0.4f}, {0.8f, 0.9f, 0.5f}};
static const float VOWEL_Q[3] = {4, 5, 6};

static void inst_init(Inst *in, const Patch *p) {
    in->p = p;
    in->att = 1.0f / (maxf(p->a, 0.0005f) * RATE);
    in->dec = decay_coef(p->d);
    in->rel = decay_coef(p->r);
    in->fdec = decay_mul(p->fdecay);
    in->glide = p->glide > 0 ? 1.0f - expf(-4.6f * CTL / (p->glide * RATE)) : 1.0f;
    in->bmul = 1;
}

static void voice_ctl(Voice *v, const Inst *in) {
    const Patch *p = in->p;
    v->pitch += (v->target - v->pitch) * in->glide;
    float pitch = v->pitch;
    if (p->vib > 0) {
        float depth = p->vib * clampf((v->age_t - 0.2f) * 2.5f, 0, 1);
        pitch += depth * sinf(TAU * 5.3f * v->age_t);
    }
    v->age_t += (float)CTL / RATE;
    if (p->vox > 0) { /* a singer never holds a pitch perfectly still */
        v->jit += (noise() - v->jit) * 0.02f;
        pitch += v->jit * 0.12f;
    }
    v->dt = 440.0f * exp2f((pitch - 69.0f) / 12.0f) / RATE;
    if (p->osc != OSC_STRING) { /* analog drift: every oscillator wanders by a cent or two */
        int n = p->unison < 1 ? 1 : p->unison > NUNI ? NUNI : p->unison;
        for (int u = 0; u < n; u++) {
            v->drift[u] += (noise() - v->drift[u]) * 0.0015f;
            v->mul[u] = exp2f((v->det[u] + v->drift[u] * 90.0f) / 1200.0f);
        }
    }
    if (p->osc == OSC_STRING) {
        float hz = v->dt * RATE;
        v->ks_del = clampf(RATE / hz - 0.5f, 2, KS_MAX - 4); /* the two-tap average adds half a sample */
        v->ks_g = powf(10.0f, -3.0f / (p->ring * hz));
    }
    float oct = p->env_oct * v->fenv * (0.5f + 0.5f * v->vel) + p->keytrack * (v->pitch - 60.0f) / 12.0f + p->lfo * M.lfo;
    float fc = p->cutoff * exp2f(oct) * in->bmul;
    svf_set(&v->co, fc, p->reso);
    if (p->steep) svf_setk(&v->co2, fc, 1.2f);
    if (p->vox > 0) {
        float w = clampf(p->vowel + p->vowel_env * v->fenv + p->lfo * M.lfo, 0, 1) * 2;
        int i = w >= 1 ? 1 : 0;
        float t = w - (float)i;
        for (int f = 0; f < 3; f++) {
            svf_setk(&v->vco[f], lerpf(VOWEL_F[i][f], VOWEL_F[i + 1][f], t), 1.0f / VOWEL_Q[f]);
            v->fg[f] = lerpf(VOWEL_G[i][f], VOWEL_G[i + 1][f], t);
        }
    }
}

/* excite a string with a burst of filtered noise; a brighter pick lets more highs through */
static void string_pluck(Voice *v, const Patch *p, int note, float vel) {
    int len = (int)(RATE / (440.0f * exp2f((note - 69.0f) / 12.0f))) + 3;
    if (len > KS_MAX - 1) len = KS_MAX - 1;
    float c = 0.05f + 0.9f * p->pick * (0.6f + 0.4f * vel), lp = 0, mean = 0, peak = 1e-6f;
    for (int k = 0; k < len; k++) {
        lp += c * (noise() - lp);
        v->ks[(unsigned)(v->ks_pos - len + k) & (KS_MAX - 1)] = lp;
        mean += lp;
    }
    mean /= (float)len;
    for (int k = 0; k < len; k++) {
        float *x = &v->ks[(unsigned)(v->ks_pos - len + k) & (KS_MAX - 1)];
        *x -= mean;
        peak = maxf(peak, fabsf(*x));
    }
    for (int k = 0; k < len; k++) v->ks[(unsigned)(v->ks_pos - len + k) & (KS_MAX - 1)] *= 0.7f / peak;
}

static void voice_start(Voice *v, const Inst *in, int note, float vel, float pan, bool legato) {
    const Patch *p = in->p;
    if (!v->active) {
        memset(v, 0, offsetof(Voice, ks));
        for (int u = 0; u < NUNI; u++) v->ph[u] = (noise() + 1) * 0.5f;
        v->pitch = (float)note;
    } else if (!legato) {
        v->pitch = (float)note;
    }
    v->active = v->gate = true;
    v->damped = false;
    v->note = note;
    v->target = (float)note;
    v->vel = vel;
    v->pan = pan;
    if (!legato) {
        v->age_t = 0;
        v->env.stage = 1;
        v->fenv = 1;
        if (p->osc == OSC_STRING) string_pluck(v, p, note, vel);
    }
    v->age = ++M.age;
    static const float SPREAD[NUNI][NUNI] = {{0}, {-1, 1}, {0, -1, 1}, {-1, -0.33f, 0.33f, 1}, {0, -1, -0.5f, 0.5f, 1}};
    int n = p->unison < 1 ? 1 : p->unison > NUNI ? NUNI : p->unison;
    float g = 1.0f / sqrtf((float)n);
    for (int u = 0; u < NUNI; u++) {
        float s = SPREAD[n - 1][u];
        v->det[u] = s * p->detune * (n == 2 ? 0.5f : 1.0f);
        v->mul[u] = exp2f(v->det[u] / 1200.0f);
        v->wl[u] = u < n ? (1 - s * p->width) * g : 0;
        v->wr[u] = u < n ? (1 + s * p->width) * g : 0;
    }
    voice_ctl(v, in);
}

static void voice_release(Voice *v) {
    if (!v->gate) return;
    v->gate = false;
    if (v->active) v->env.stage = 3;
}

static inline float formants(Svf *s, const Voice *v, float x) {
    float y = x * 0.12f; /* a little of the source keeps the body */
    for (int f = 0; f < 3; f++) y += svf_bp(&s[f], &v->vco[f], x) * v->fg[f];
    return y;
}

static void voice_render(Voice *v, const Inst *in, float *out_l, float *out_r) {
    const Patch *p = in->p;
    Env *e = &v->env;
    switch (e->stage) {
    case 1:
        if ((e->lvl += in->att) >= 1) { e->lvl = 1; e->stage = 2; }
        break;
    case 2:
        e->lvl += (p->s - e->lvl) * in->dec;
        if (p->s < 1e-3f && e->lvl < 3e-4f) e->stage = 0;
        break;
    case 3:
        e->lvl -= e->lvl * (v->damped ? DC.damp : in->rel);
        if (e->lvl < 3e-4f) e->stage = 0;
        break;
    }
    if (!e->stage) { v->active = v->gate = false; return; }
    v->fenv *= in->fdec;
    float dt = v->dt, l = 0, r = 0;
    int n = p->unison < 1 ? 1 : p->unison > NUNI ? NUNI : p->unison;
    if (p->osc == OSC_STRING) { /* Karplus-Strong: a delay loop that averages two neighbouring taps */
        float rp = (float)v->ks_pos - v->ks_del;
        if (rp < 0) rp += KS_MAX;
        int i0 = (int)rp, i1 = (i0 + 1) & (KS_MAX - 1), im = (i0 + KS_MAX - 1) & (KS_MAX - 1);
        float fr = rp - (float)i0;
        float a = v->ks[i0] + (v->ks[i1] - v->ks[i0]) * fr, b = v->ks[im] + (v->ks[i0] - v->ks[im]) * fr;
        l = r = (a + b) * 0.5f * v->ks_g;
        v->ks[v->ks_pos] = l;
        v->ks_pos = (v->ks_pos + 1) & (KS_MAX - 1);
        n = 1;
    } else {
        for (int u = 0; u < n; u++) {
            float d = dt * v->mul[u];
            float ph = v->ph[u] + d;
            if (ph >= 1) ph -= 1;
            v->ph[u] = ph;
            float x;
            switch (p->osc) {
            case OSC_SAW: x = osc_saw(ph, d); break;
            case OSC_PULSE: x = osc_pulse(ph, d, p->pw); break;
            case OSC_SAWSQ: x = 0.55f * osc_saw(ph, d) - 0.45f * osc_pulse(ph, d, p->pw); break;
            default: x = osc_tri(ph); break;
            }
            l += x * v->wl[u];
            r += x * v->wr[u];
        }
    }
    if (p->vox > 0) { /* breath */
        l += noise() * 0.12f;
        r += noise() * 0.12f;
    }
    if (p->sub > 0) {
        if ((v->sph += dt * 0.5f) >= 1) v->sph -= 1;
        float s = p->sub * sinf(TAU * v->sph);
        l += s;
        r += s;
    }
    bool stereo = n > 1 && p->width > 0;
    l = svf_lp(&v->fl, &v->co, l);
    if (stereo) r = svf_lp(&v->fr, &v->co, r);
    if (p->steep) {
        l = svf_lp(&v->sl, &v->co2, l);
        if (stereo) r = svf_lp(&v->sr, &v->co2, r);
    }
    if (p->drive > 0) {
        l = sat(l * p->drive);
        if (stereo) r = sat(r * p->drive);
    }
    if (p->vox > 0) {
        l = formants(v->vl, v, l) * p->vox;
        if (stereo) r = formants(v->vr, v, r) * p->vox;
    }
    if (!stereo) r = l;
    float g = e->lvl * v->vel * p->level;
    *out_l += l * g * (1 - v->pan);
    *out_r += r * g * (1 + v->pan);
}

static Voice *pool_alloc(Voice *pool, int n) {
    Voice *best = NULL;
    for (int i = 0; i < n; i++)
        if (!pool[i].active) return &pool[i];
    for (int i = 0; i < n; i++) /* the quietest released voice */
        if (!pool[i].gate && (!best || pool[i].env.lvl < best->env.lvl)) best = &pool[i];
    if (best) return best;
    for (int i = 0; i < n; i++)
        if (!best || pool[i].age < best->age) best = &pool[i];
    return best;
}

/* ------------------------------------------------------------ harmony */
static int place(int pc, int lo) { return lo + ((pc - lo) % 12 + 12) % 12; } /* lowest note >= lo */

static int chord_tone(const Chord *c, int a, int b, int fallback) {
    for (int i = 0; i < c->n; i++)
        if (c->iv[i] == a || c->iv[i] == b) return c->iv[i];
    return fallback;
}
static int fifth(const Chord *c) { return chord_tone(c, 7, 6, 7); }
static int third(const Chord *c) { return chord_tone(c, 3, 4, chord_tone(c, 5, 2, 0)); }
static int seventh(const Chord *c) { return chord_tone(c, 10, 11, 10); }

/* pick the voicing closest to the previous one (smooth voice leading): every
 * close-position inversion and, for four-note chords, its open drop-2 form */
static void voice_chord(const Song *S, const Chord *c) {
    int best[4] = {0};
    float bestc = 1e9f, center = (S->pad_lo + S->pad_hi) * 0.5f;
    for (int cand = 0; cand < c->n * (c->n == 4 ? 2 : 1); cand++) {
        int inv = cand % c->n, v[4], sum = 0;
        v[0] = place((c->root + c->iv[inv]) % 12, S->pad_lo);
        for (int j = 1; j < c->n; j++) v[j] = place((c->root + c->iv[(inv + j) % c->n]) % 12, v[j - 1] + 1);
        if (cand >= c->n) { /* drop the second voice from the top an octave */
            int t = v[2] - 12;
            v[2] = v[1];
            v[1] = v[0];
            v[0] = t;
            if (v[0] < S->pad_lo - 5) continue;
        }
        for (int j = 0; j < c->n; j++) sum += v[j];
        float mean = (float)sum / c->n, cost = 0;
        if (M.nv == c->n) {
            for (int j = 0; j < c->n; j++) cost += (float)abs(v[j] - M.voicing[j]);
        } else if (M.nv > 0) {
            int ps = 0;
            for (int j = 0; j < M.nv; j++) ps += M.voicing[j];
            cost = fabsf(mean - (float)ps / M.nv) * c->n;
        }
        cost += 0.35f * fabsf(mean - center);
        for (int j = 1; j < c->n; j++)
            if (v[j] - v[j - 1] == 1) cost += 12; /* avoid muddy semitone clusters (maj7 inversions) */
        if (v[c->n - 1] > S->pad_hi) cost += 50;
        if (cost < bestc) { bestc = cost; memcpy(best, v, sizeof(best)); }
    }
    memcpy(M.voicing, best, sizeof(best));
    M.nv = c->n;
}

static bool pad_gated(void) {
    for (int i = 0; i < NPAD; i++)
        if (M.pad[i].gate) return true;
    return false;
}

static void pad_release(void) {
    for (int i = 0; i < NPAD; i++) voice_release(&M.pad[i]);
}

/* sound the current voicing; tones already held keep ringing unless retriggered */
static void pad_set(bool retrigger) {
    bool keep[NPAD] = {false};
    int fresh[4], nf = 0;
    for (int j = 0; j < M.nv; j++) {
        int i = NPAD;
        if (!retrigger)
            for (i = 0; i < NPAD; i++)
                if (M.pad[i].gate && !keep[i] && M.pad[i].note == M.voicing[j]) break;
        if (i < NPAD) keep[i] = true;
        else fresh[nf++] = j;
    }
    for (int i = 0; i < NPAD; i++)
        if (!keep[i]) voice_release(&M.pad[i]);
    for (int k = 0; k < nf; k++) {
        int j = fresh[k];
        float pan = M.nv > 1 ? ((float)j / (M.nv - 1) - 0.5f) * 0.4f : 0;
        voice_start(pool_alloc(M.pad, NPAD), &M.ipad, M.voicing[j], 0.8f, pan, false);
    }
}

static int arp_note(const Song *S, const Chord *c, char k, int shift) {
    if (k >= '0' && k <= '9') {
        int i = k - '0';
        return M.voicing[i % M.nv] + 12 * (i / M.nv) + shift;
    }
    int r = place(c->root, S->pad_lo + shift);
    if (k == 'o') return r + 12;
    if (k == 'f') return place((c->root + fifth(c)) % 12, r + 1);
    return r;
}

static int bass_note(const Song *S, const Chord *c, char k) {
    int r = place(c->bass, S->bass_lo);
    if (k == 'o') return r + 12;
    if (k == 'f') return place((c->root + fifth(c)) % 12, r + 1);
    if (k == 'u') return place((c->root + fifth(c)) % 12, r - 12);
    if (k == 't') return place((c->root + third(c)) % 12, r + 1);
    if (k == 's') return place((c->root + seventh(c)) % 12, r + 1);
    return r;
}

/* ------------------------------------------------------------ drums */
static void kick(float vel) {
    Drum *d = &M.dr;
    d->k_amp = d->k_p1 = d->k_p2 = d->k_click = 1;
    d->k_hold = (int)(0.012f * RATE); /* full level for a moment before the decay: the punch */
    d->k_vel = vel;
    d->k_ph = 0;
    M.sc_hold = 1;
}
static void snare(float vel) {
    Drum *d = &M.dr;
    d->s_body = d->s_snap = d->s_pitch = 1;
    d->s_vel = vel;
    d->s_ph1 = d->s_ph2 = 0;
}
static void clap(float vel) { M.dr.c_t = 0; M.dr.c_vel = vel; }
static inline float human(float vel) { return vel * (0.92f + 0.08f * noise()); }
static void hat(float vel, bool open) {
    M.dr.h_amp = 1;
    M.dr.h_vel = human(vel);
    M.dr.h_dec = open ? DC.h_open : DC.h_closed;
}
static void ride(float vel) {
    M.dr.d_amp = 1;
    M.dr.d_vel = human(vel);
}
static void tom(char which) {
    Drum *d = &M.dr;
    d->t_hz = which == 'h' ? 196.0f : which == 'm' ? 147.0f : 110.0f;
    d->t_pan = which == 'h' ? -0.2f : which == 'm' ? 0.0f : 0.2f;
    d->t_amp = d->t_pitch = 1;
    d->t_amp2 = 0;
    d->t_dec = DC.t_amp;
    d->t_bend = 0.8f;
    d->t_lvl = 0.24f;
    d->t_slap = 0;
}
/* congas share the tom voice: higher, shorter, a second membrane mode and a slap on the attack */
static void conga(bool high) {
    Drum *d = &M.dr;
    d->t_hz = high ? 370.0f : 262.0f;
    d->t_pan = high ? -0.35f : 0.3f;
    d->t_amp = d->t_pitch = d->t_amp2 = 1;
    d->t_dec = DC.g_amp;
    d->t_bend = 0.12f;
    d->t_lvl = human(0.15f);
    d->t_slap = 1;
}
static void rim(void) {
    M.dr.m_amp = human(1);
    M.dr.m_ph1 = M.dr.m_ph2 = 0;
}
static void shaker(float vel) {
    M.dr.z_amp = 1;
    M.dr.z_vel = human(vel);
}
static void crash(void) { M.dr.y_amp = 1; }
static void riser(float secs) {
    M.dr.r_on = true;
    M.dr.r_t = 0;
    M.dr.r_len = secs;
}
static void swell(float secs) {
    M.dr.w_t = 0;
    M.dr.w_len = secs;
}
static void impact(void) {
    M.dr.i_amp = M.dr.i_p = 1;
    M.dr.i_ph = 0;
}

/* drum hits wait in a short queue so they can land a few milliseconds behind the grid */
enum { HIT_SNARE, HIT_CLAP, HIT_HAT, HIT_OPEN, HIT_TOM, HIT_CONGA, HIT_RIM, HIT_SHAKER, HIT_RIDE };
static void hit_fire(int kind, char arg, float vel) {
    switch (kind) {
    case HIT_SNARE: snare(vel); break;
    case HIT_CLAP: clap(vel); break;
    case HIT_HAT: hat(vel, false); break;
    case HIT_OPEN: hat(vel, true); break;
    case HIT_TOM: tom(arg); break;
    case HIT_CONGA: conga(arg == 'k'); break;
    case HIT_RIM: rim(); break;
    case HIT_SHAKER: shaker(vel); break;
    default: ride(vel); break;
    }
}
static void hit(int kind, char arg, float vel, float late_ms, float loose_ms) {
    int wait = (int)((late_ms + loose_ms * (noise() * 0.5f + 0.5f)) * (RATE / 1000.0f));
    if (M.nhits == NHITS) {
        hit_fire(kind, arg, vel);
        return;
    }
    M.hits[M.nhits++] = (Hit){wait, (uint8_t)kind, arg, vel};
}
static void hits_tick(void) {
    for (int i = 0; i < M.nhits;) {
        if (M.hits[i].wait-- > 0) {
            i++;
            continue;
        }
        hit_fire(M.hits[i].kind, M.hits[i].arg, M.hits[i].vel);
        M.hits[i] = M.hits[--M.nhits];
    }
}

/* transition effects, kept apart from the drums so the gap before a drop does not silence them */
static void fx_render(float *lr) {
    Drum *d = &M.dr;
    if (d->w_len > 0) { /* reverse cymbal: bright noise swelling up to the downbeat */
        float k = d->w_t / d->w_len, a = k * k * k * 0.1f;
        d->w_lpl += DC.air * (svf_bp(&d->w_fl, &DC.swell, noise()) - d->w_lpl);
        d->w_lpr += DC.air * (svf_bp(&d->w_fr, &DC.swell, noise()) - d->w_lpr);
        lr[0] += d->w_lpl * a;
        lr[1] += d->w_lpr * a;
        if ((d->w_t += 1.0f / RATE) >= d->w_len) d->w_len = 0;
    }
    if (d->i_amp > 1e-4f) { /* sub boom falling from 90 to 38 Hz */
        if ((d->i_ph += 38.0f * (1.0f + 1.4f * d->i_p) / RATE) >= 1) d->i_ph -= 1;
        float x = sinf(TAU * d->i_ph) * d->i_amp * 0.3f;
        lr[0] += x;
        lr[1] += x;
        d->i_amp *= DC.i_amp;
        d->i_p *= DC.i_p;
    }
}

/* adds kick to *kick_out, the other drums to lr[], and effect sends to *rev */
static void drums_render(const Song *S, float *kick_out, float *lr, float *rev) {
    Drum *d = &M.dr;
    float mono = 0, send = 0, pl = 0, pr = 0;
    if (d->k_amp > 1e-4f) { /* two-stage pitch drop: a fast snap into a slower body sweep */
        float f = S->kick_hz * (1.0f + 3.0f * d->k_p1 + 1.2f * d->k_p2);
        if ((d->k_ph += f / RATE) >= 1) d->k_ph -= 1;
        float x = sinf(TAU * d->k_ph) * d->k_amp + svf_bp(&d->k_f, &DC.kclick, noise()) * d->k_click * 0.5f;
        *kick_out += sat(x * 1.5f) * d->k_vel * 0.4f;
        if (d->k_hold > 0) d->k_hold--;
        else d->k_amp *= M.k_dec;
        d->k_p1 *= DC.k_p1;
        d->k_p2 *= DC.k_p2;
        d->k_click *= DC.k_click;
    }
    if (d->s_snap > 1e-4f) {
        float bend = 1.0f + 0.5f * d->s_pitch; /* the drum head drops in pitch as it settles */
        if ((d->s_ph1 += 185.0f * bend / RATE) >= 1) d->s_ph1 -= 1;
        if ((d->s_ph2 += 330.0f * bend / RATE) >= 1) d->s_ph2 -= 1;
        d->s_pitch *= DC.s_pitch;
        float body = (0.65f * sinf(TAU * d->s_ph1) + 0.35f * sinf(TAU * d->s_ph2)) * d->s_body;
        d->s_lp += DC.air * (svf_bp(&d->s_f, &DC.snare, noise()) - d->s_lp);
        float x = (body * 0.55f + d->s_lp * d->s_snap * 0.5f) * d->s_vel * 0.5f;
        mono += x;
        send += x * 0.25f;
        d->s_body *= DC.s_body;
        d->s_snap *= DC.s_snap;
    }
    if (d->c_t >= 0) {
        float t = d->c_t;
        float env = t < 0.032f ? expf(-fmodf(t, 0.0105f) * 320.0f) : expf(-(t - 0.032f) * 13.0f);
        float g = env * d->c_vel * 0.42f; /* two noise sources, partly crossed, for a wider clap */
        float xl = svf_bp(&d->c_f, &DC.clap, noise()) * g, xr = svf_bp(&d->c_f2, &DC.clap, noise()) * g;
        pl += xl * 0.8f + xr * 0.45f;
        pr += xr * 0.8f + xl * 0.45f;
        send += (xl + xr) * 0.25f;
        if ((d->c_t += 1.0f / RATE) > 0.45f) d->c_t = -1;
    }
    float hat_out = 0;
    if (d->h_amp > 1e-4f) { /* six detuned squares and a little noise, band-limited */
        static const float HF[6] = {349.0f, 517.5f, 628.3f, 888.6f, 918.0f, 1360.0f};
        float m = 0;
        for (int i = 0; i < 6; i++) {
            if ((d->h_ph[i] += HF[i] / RATE) >= 1) d->h_ph[i] -= 1;
            m += d->h_ph[i] < 0.5f ? 1.0f : -1.0f;
        }
        float x = svf_hp(&d->h_hp, &DC.hat_hp, svf_bp(&d->h_bp, &DC.hat_bp, m * 0.1f + noise() * 0.25f));
        d->h_lp += DC.air * (x - d->h_lp);
        d->h_lp2 += DC.air * (d->h_lp - d->h_lp2);
        hat_out = d->h_lp2 * d->h_amp * d->h_vel * 0.36f * S->hats;
        d->h_amp *= d->h_dec;
    }
    if (d->d_amp > 1e-4f) { /* ride: a lower square bank ringing through a narrow band */
        static const float RF[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
        float m = 0;
        for (int i = 0; i < 6; i++) {
            if ((d->d_ph[i] += RF[i] / RATE) >= 1) d->d_ph[i] -= 1;
            m += d->d_ph[i] < 0.5f ? 1.0f : -1.0f;
        }
        float x = svf_bp(&d->d_bp, &DC.ride, m * 0.12f + noise() * 0.08f);
        d->d_lp += DC.air * (x - d->d_lp);
        x = d->d_lp * d->d_amp * d->d_vel * 0.13f * S->hats;
        pl += x * 0.8f;
        pr += x * 1.2f;
        send += x * 0.3f;
        d->d_amp *= DC.d_amp;
    }
    if (d->t_amp > 1e-4f) {
        if ((d->t_ph += d->t_hz * (1.0f + d->t_bend * d->t_pitch) / RATE) >= 1) d->t_ph -= 1;
        float x = sinf(TAU * d->t_ph) * d->t_amp * d->t_lvl;
        if (d->t_amp2 > 1e-4f) {
            if ((d->t_ph2 += d->t_hz * 1.47f / RATE) >= 1) d->t_ph2 -= 1;
            x += sinf(TAU * d->t_ph2) * d->t_amp2 * d->t_lvl * 0.35f;
            d->t_amp2 *= DC.g_amp2;
        }
        if (d->t_slap > 1e-4f) {
            x += svf_bp(&d->t_f, &DC.slap, noise()) * d->t_slap * d->t_lvl * 0.6f;
            d->t_slap *= DC.g_slap;
        }
        pl += x * (1 - d->t_pan);
        pr += x * (1 + d->t_pan);
        send += x * 0.2f;
        d->t_amp *= d->t_dec;
        d->t_pitch *= DC.t_pitch;
    }
    if (d->m_amp > 1e-4f) { /* rimshot: two driven partials, a little left */
        if ((d->m_ph1 += 1720.0f / RATE) >= 1) d->m_ph1 -= 1;
        if ((d->m_ph2 += 480.0f / RATE) >= 1) d->m_ph2 -= 1;
        float x = sat((sinf(TAU * d->m_ph1) + 0.7f * sinf(TAU * d->m_ph2)) * d->m_amp * 2.5f) * 0.07f;
        pl += x * 1.25f;
        pr += x * 0.75f;
        send += x * 0.35f;
        d->m_amp *= DC.m_amp;
    }
    if (d->z_amp > 1e-4f) { /* shaker: soft-edged band of noise, a little right */
        d->z_env += (d->z_amp - d->z_env) * DC.z_att;
        float x = svf_bp(&d->z_f, &DC.shaker, noise());
        d->z_lp += DC.air * (x - d->z_lp);
        x = d->z_lp * d->z_env * d->z_vel * 0.08f * S->hats;
        pl += x * 0.7f;
        pr += x * 1.3f;
        d->z_amp *= DC.z_amp;
    }
    float yl = 0, yr = 0;
    if (d->y_amp > 1e-4f) {
        float a = d->y_amp * 0.07f;
        d->y_lpl += DC.air * (svf_bp(&d->y_fl, &DC.crash, noise()) - d->y_lpl);
        d->y_lpr += DC.air * (svf_bp(&d->y_fr, &DC.crash, noise()) - d->y_lpr);
        yl = d->y_lpl * a;
        yr = d->y_lpr * a;
        send += (yl + yr) * 0.25f;
        d->y_amp *= DC.y_amp;
    }
    if (d->r_on) {
        float k = d->r_t / d->r_len;
        float bp;
        svf_tick(&d->r_f, &d->r_co, noise(), &bp);
        float x = bp * k * k * clampf((d->r_len - d->r_t) * 100.0f, 0, 1) * 0.16f;
        mono += x * 0.5f;
        send += x;
        if ((d->r_t += 1.0f / RATE) >= d->r_len) d->r_on = false;
    }
    float g = S->drums;
    *kick_out *= g;
    lr[0] += (mono + hat_out * 0.85f + pl + yl) * g;
    lr[1] += (mono + hat_out * 1.15f + pr + yr) * g;
    *rev += send * g;
}

/* ------------------------------------------------------------ sequencer */
static inline char pat(const Pat *p, int st) { return p->n ? p->s[st % p->n] : 0; }
static inline bool maybe(float chance) { return noise() * 0.5f + 0.5f < chance; }

/* chord-relative lanes: stabs and arpeggios cut their previous notes, plucked lanes let them ring out */
static void chord_lane(const Song *S, const Chord *ch, char c, int st, Voice *pool, int n, const Inst *in, int shift, int *side,
                       bool ring) {
    bool note = c && ((c >= '0' && c <= '9') || strchr("crofCROF", c));
    if (note || (!ring && c != '-'))
        for (int i = 0; i < n; i++) voice_release(&pool[i]);
    if (!note) return;
    float vel = (c >= 'A' && c <= 'Z' ? 1.0f : (st & 3) == 0 ? 0.8f : 0.72f) * (0.94f + 0.06f * noise());
    c = (char)(c | 32);
    if (c == 'c') {
        for (int j = 0; j < M.nv; j++) {
            float pan = M.nv > 1 ? ((float)j / (M.nv - 1) - 0.5f) * 0.5f : 0;
            voice_start(pool_alloc(pool, n), in, M.voicing[j] + shift, vel * 0.8f, pan, false);
        }
    } else {
        *side = *side > 0 ? -1 : 1;
        voice_start(pool_alloc(pool, n), in, arp_note(S, ch, c, shift), vel, (ring ? 0.35f : 0.22f) * *side, false);
    }
}

static void seq_step(double dur) {
    const Song *S = &SONGS[M.song];
    const Section *sec = &S->sec[M.sec];
    const SecData *sd = &SD[M.song].sec[M.sec];
    int st = M.step;
    const Chord *ch = &SD[M.song].chords[sd->chord[st]];
    M.bright_target = lerpf(sd->b0, sd->b1, (float)st / sd->steps);

    bool changed = sd->chord[st] != M.chord;
    if (changed) {
        M.chord = sd->chord[st];
        voice_chord(S, ch);
        for (int i = 0; i < NPLK; i++) { /* ringing strings are muted when the harmony moves on */
            voice_release(&M.plk[i]);
            M.plk[i].damped = true;
        }
    }

    char c = pat(&sd->pad, st);
    if (c == 'x') pad_set(true);
    else if (c == 'h' || (c == '-' && changed && pad_gated())) pad_set(false);
    else if (c != '-') pad_release();

    c = pat(&sd->bass, st);
    if (c && strchr("rotfsuROTFSU", c))
        voice_start(&M.bass, &M.ibass, bass_note(S, ch, (char)(c | 32)), c < 'a' ? 1.0f : 0.78f, 0, false);
    else if (c != '-') voice_release(&M.bass);

    chord_lane(S, ch, pat(&sd->arp, st), st, M.arp, NARP, &M.iarp, S->arp_shift, &M.arp_side, false);
    chord_lane(S, ch, pat(&sd->pluck, st), st, M.plk, NPLK, &M.iplk, S->pluck_shift, &M.plk_side, true);

    int e = sd->lead[st];
    if (e >= 0) {
        uint8_t fx = sd->lead_fx[st];
        bool slide = M.lead_slide && M.lead.active && M.lead.gate;
        voice_start(&M.lead, &M.ilead, e, fx & FX_ACCENT ? 1.0f : 0.8f, 0, slide);
        M.lead_slide = fx & FX_SLIDE;
    } else if (e == LEAD_OFF) {
        voice_release(&M.lead);
        M.lead_slide = false;
    }

    /* the kick sits on the grid; the rest of the kit lays back a little and loosely (milliseconds) */
    const Pat *dp = sec->fill && st >= sd->steps - 16 ? sd->fill : sd->drum;
    c = pat(&dp[0], st);
    if (c == 'x' || c == 'X') kick(c == 'X' ? 1.0f : 0.88f);
    switch (pat(&dp[1], st)) {
    case 'x': hit(HIT_SNARE, 0, 0.8f, 2, 1); break;
    case 'X': hit(HIT_SNARE, 0, 1.0f, 2, 1); break;
    case 'g': hit(HIT_SNARE, 0, 0.3f, 2, 3); break;
    case 'r': snare(0.55f); M.roll = (int)(dur * 0.5); break;
    case 'c': hit(HIT_CLAP, 0, 0.9f, 4, 1); break;
    case 'b': hit(HIT_SNARE, 0, 0.4f, 2, 1); hit(HIT_CLAP, 0, 0.8f, 4, 1); break;
    case '?': if (maybe(0.35f)) hit(HIT_SNARE, 0, 0.22f, 2, 3); break;
    }
    c = pat(&dp[2], st);
    if (c == 'g' || (c == '?' && maybe(0.5f))) hit(HIT_HAT, 0, 0.2f, 1, 3);
    else if (c == 'x' || c == 'X') hit(HIT_HAT, 0, c == 'X' ? 0.85f : 0.5f, 1, 2);
    else if (c == 'o') hit(HIT_OPEN, 0, 0.7f, 1, 2);
    c = pat(&dp[3], st);
    if (c == 'h' || c == 'm' || c == 'l') hit(HIT_TOM, c, 1, 0, 2);
    else if (c == 'k' || c == 'j') hit(HIT_CONGA, c, 1, 2, 4);
    else if (c == 'r') hit(HIT_RIM, 0, 1, 1, 3);
    else if (c == '?' && maybe(0.4f)) hit(HIT_CONGA, noise() > 0 ? 'k' : 'j', 1, 2, 4);
    c = pat(&dp[4], st);
    if (c == 'x' || c == 'X') hit(HIT_SHAKER, 0, c == 'X' ? 1.0f : 0.55f, 1, 3);
    else if (c == '?' && maybe(0.5f)) hit(HIT_SHAKER, 0, 0.35f, 1, 3);
    c = pat(&dp[5], st);
    if (c == 'x' || c == 'X') hit(HIT_RIDE, 0, c == 'X' ? 1.0f : 0.6f, 1, 2);
    else if (c == 'c') crash();
    else if (c == '?' && maybe(0.5f)) hit(HIT_RIDE, 0, 0.4f, 1, 2);

    /* transitions: a reverse swell into every crash, a gap and a sub impact where a lift drops */
    const Section *next = &S->sec[M.sec + 1 < S->nsec ? M.sec + 1 : S->loop];
    if (st == 0) {
        M.prev_flags = M.cur_flags;
        M.cur_flags = sec->flags;
        M.lift_on = M.gap_on = false;
        if (sec->flags & SEC_CRASH) {
            crash();
            if (M.prev_flags & SEC_LIFT) impact();
        }
    }
    if ((next->flags & SEC_CRASH) && st == sd->steps - 4) swell((float)(4 * M.step_len / RATE));
    if ((sec->flags & SEC_LIFT) && (next->flags & SEC_CRASH) && st == sd->steps - 2) M.gap_on = true;
    if (st == sd->riser_at) {
        float secs = (float)((sd->steps - st) * M.step_len / RATE);
        if (sec->flags & SEC_RISER) riser(secs);
        if (sec->flags & SEC_LIFT) {
            M.lift_on = true;
            M.lift_t = 0;
            M.lift_len = secs;
            memset(&M.hp_l, 0, sizeof(M.hp_l));
            memset(&M.hp_r, 0, sizeof(M.hp_r));
        }
    }
}

static void control_tick(void) {
    M.bright += (M.bright_target - M.bright) * 0.05f;
    if ((M.lfo_ph += CTL * 0.09f / RATE) >= 1) M.lfo_ph -= 1;
    M.lfo = sinf(TAU * M.lfo_ph);
    Inst *insts[5] = {&M.ipad, &M.ibass, &M.iarp, &M.iplk, &M.ilead};
    for (int i = 0; i < 5; i++) insts[i]->bmul = exp2f((M.bright - 1.0f) * insts[i]->p->sweep);
    for (int i = 0; i < NPAD; i++)
        if (M.pad[i].active) voice_ctl(&M.pad[i], &M.ipad);
    for (int i = 0; i < NARP; i++)
        if (M.arp[i].active) voice_ctl(&M.arp[i], &M.iarp);
    for (int i = 0; i < NPLK; i++)
        if (M.plk[i].active) voice_ctl(&M.plk[i], &M.iplk);
    if (M.bass.active) voice_ctl(&M.bass, &M.ibass);
    if (M.lead.active) voice_ctl(&M.lead, &M.ilead);
    reverb_mod();
    if (M.dr.r_on) svf_set(&M.dr.r_co, 250.0f * exp2f(4.5f * M.dr.r_t / M.dr.r_len), 0.5f);
    if (M.lift_on) svf_set(&M.hp_co, 30.0f * exp2f(4.5f * minf(M.lift_t / M.lift_len, 1)), 0.25f);
    /* bus compressor: 2.5:1 above -10 dBFS */
    M.c_target = M.c_env > 0.32f ? powf(0.32f / M.c_env, 0.6f) : 1.0f;
}

static void song_start(int song) {
    M.song = song;
    if (song < 0) return;
    const Song *S = &SONGS[song];
    memset(M.pad, 0, sizeof(M.pad));
    memset(M.arp, 0, sizeof(M.arp));
    memset(M.plk, 0, sizeof(M.plk));
    memset(&M.bass, 0, sizeof(M.bass));
    memset(&M.lead, 0, sizeof(M.lead));
    memset(&M.dr, 0, sizeof(M.dr));
    M.dr.c_t = -1;
    M.sec = 0;
    M.step = -1;
    M.step_left = 0;
    M.step_len = RATE * 60.0 / S->bpm / 4.0;
    M.roll = 0;
    M.chord = -1;
    M.nv = 0;
    M.lead_slide = M.lift_on = M.gap_on = false;
    M.lift_w = 0;
    M.gap_w = 1;
    M.nhits = 0;
    M.prev_flags = M.cur_flags = 0;
    M.bright = M.bright_target = SD[song].sec[0].b0;
    inst_init(&M.ipad, S->pad);
    inst_init(&M.ibass, S->bass);
    inst_init(&M.iarp, S->arp);
    inst_init(&M.iplk, S->pluck);
    inst_init(&M.ilead, S->lead);
    M.k_dec = decay_mul(S->kick_decay * 4.6f);
    M.sc = M.sc_hold = 0;
    M.sc_rel = decay_mul(60.0f / S->bpm);
    M.dlen = (int)clampf((float)(S->delay_steps * M.step_len), 1, DELAY_MAX);
    M.dpos = 0;
    memset(M.dl, 0, sizeof(float) * DELAY_MAX);
    memset(M.drr, 0, sizeof(float) * DELAY_MAX);
    memset(M.cb, 0, sizeof(float) * CHORUS_MAX);
    M.cpos = 0;
    M.d_lp = M.d_hp = M.r_hp = M.ch_hp = 0;
    reverb_clear();
    reverb_set(S->rev_time, S->rev_damp);
    M.c_env = 0;
    M.c_gain = M.c_target = 1;
}

static inline float chorus_read(float delay) {
    float pos = (float)M.cpos - delay;
    if (pos < 0) pos += CHORUS_MAX;
    int i = (int)pos;
    float fr = pos - (float)i;
    if (i >= CHORUS_MAX) i -= CHORUS_MAX; /* pos + CHORUS_MAX can round up to the end */
    int j = i + 1 < CHORUS_MAX ? i + 1 : 0;
    return M.cb[i] + (M.cb[j] - M.cb[i]) * fr;
}

void music_render(float *out, int frames, float vol) {
#if defined(__SSE__) || defined(_M_X64)
    _mm_setcsr(_mm_getcsr() | 0x8040); /* flush denormals to zero */
#endif
    if (!M.dl || (M.song < 0 && M.target < 0)) return;
    for (int i = 0; i < frames; i++) {
        if (M.target != M.song) {
            M.fade -= 1.0f / (RATE * 0.6f);
            if (M.song < 0 || M.fade <= 0) {
                M.fade = 0;
                song_start(M.target);
            }
        } else if (M.fade < 1) {
            M.fade = minf(1, M.fade + 1.0f / (RATE * 0.02f));
        }
        if (M.song < 0) continue;
        const Song *S = &SONGS[M.song];

        if ((M.step_left -= 1.0) <= 0) {
            if (++M.step >= SD[M.song].sec[M.sec].steps) {
                M.step = 0;
                if (++M.sec >= S->nsec) M.sec = S->loop;
            }
            double dur = M.step_len * ((M.step & 1) ? 1.0 - S->swing : 1.0 + S->swing);
            seq_step(dur);
            M.step_left += dur;
        }
        if (M.roll > 0 && --M.roll == 0) snare(0.5f);
        hits_tick();
        if (--M.ctl <= 0) {
            M.ctl = CTL;
            control_tick();
        }

        float pad[2] = {0, 0}, arp[2] = {0, 0}, plk[2] = {0, 0}, bass[2] = {0, 0}, lead[2] = {0, 0}, drums[2] = {0, 0};
        float kick = 0, rev_in = 0, fx[2] = {0, 0};
        for (int v = 0; v < NPAD; v++)
            if (M.pad[v].active) voice_render(&M.pad[v], &M.ipad, &pad[0], &pad[1]);
        for (int v = 0; v < NARP; v++)
            if (M.arp[v].active) voice_render(&M.arp[v], &M.iarp, &arp[0], &arp[1]);
        for (int v = 0; v < NPLK; v++)
            if (M.plk[v].active) voice_render(&M.plk[v], &M.iplk, &plk[0], &plk[1]);
        if (M.bass.active) voice_render(&M.bass, &M.ibass, &bass[0], &bass[1]);
        if (M.lead.active) voice_render(&M.lead, &M.ilead, &lead[0], &lead[1]);
        drums_render(S, &kick, drums, &rev_in);
        fx_render(fx);

        /* sidechain: the kick ducks everything tonal, the lead only half as much */
        M.sc += (M.sc_hold - M.sc) * 0.02f;
        M.sc_hold *= M.sc_rel;
        float duck = 1.0f - S->sidechain * M.sc, lead_duck = 1.0f - 0.5f * S->sidechain * M.sc;
        for (int c = 0; c < 2; c++) {
            pad[c] *= duck;
            arp[c] *= duck;
            plk[c] *= duck;
            lead[c] *= lead_duck;
            bass[c] = sat(bass[c] * 1.4f * duck) * 0.72f;
        }

        /* chorus: two slowly swept taps, one per side */
        float cin = (pad[0] + pad[1]) * 0.5f * S->pad->chorus + (arp[0] + arp[1]) * 0.5f * S->arp->chorus +
                    (plk[0] + plk[1]) * 0.5f * S->pluck->chorus + (lead[0] + lead[1]) * 0.5f * S->lead->chorus;
        M.ch_hp += 0.035f * (cin - M.ch_hp);
        M.cb[M.cpos] = cin - M.ch_hp;
        if ((M.ch_ph += 0.4f / RATE) >= 1) M.ch_ph -= 1;
        float cs = sinf(TAU * M.ch_ph), cc = cosf(TAU * M.ch_ph);
        float cho[2] = {chorus_read((0.012f + 0.004f * cs) * RATE), chorus_read((0.012f + 0.004f * cc) * RATE)};
        if (++M.cpos >= CHORUS_MAX) M.cpos = 0;

        /* ping-pong delay on arp, pluck and lead, darker and thinner on each repeat */
        float din = (arp[0] + arp[1]) * 0.5f * S->arp->delay + (plk[0] + plk[1]) * 0.5f * S->pluck->delay +
                    (lead[0] + lead[1]) * 0.5f * S->lead->delay;
        float yl = M.dl[M.dpos], yr = M.drr[M.dpos];
        M.d_lp += 0.4f * (yr - M.d_lp);
        M.d_hp += 0.03f * (M.d_lp - M.d_hp);
        M.dl[M.dpos] = din + (M.d_lp - M.d_hp) * S->delay_fb;
        M.drr[M.dpos] = yl * S->delay_fb;
        if (++M.dpos >= M.dlen) M.dpos = 0;
        float dly[2] = {yl * S->delay_mix, yr * S->delay_mix};

        /* reverb */
        rev_in += (pad[0] + pad[1]) * S->pad->reverb + (arp[0] + arp[1]) * S->arp->reverb + (plk[0] + plk[1]) * S->pluck->reverb +
                  (lead[0] + lead[1]) * S->lead->reverb + (dly[0] + dly[1]) * 0.3f;
        M.r_hp += 0.04f * (rev_in - M.r_hp);
        float rev[2];
        reverb_tick((rev_in - M.r_hp) * 0.18f, rev);
        rev[0] *= S->rev_mix;
        rev[1] *= S->rev_mix;

        MUSIC_TAP(0, kick, kick);
        MUSIC_TAP(1, drums[0], drums[1]);
        MUSIC_TAP(2, bass[0], bass[1]);
        MUSIC_TAP(3, pad[0], pad[1]);
        MUSIC_TAP(4, arp[0], arp[1]);
        MUSIC_TAP(5, lead[0], lead[1]);
        MUSIC_TAP(6, dly[0], dly[1]);
        MUSIC_TAP(7, rev[0], rev[1]);
        MUSIC_TAP(8, cho[0], cho[1]);
        MUSIC_TAP(9, plk[0], plk[1]);
        MUSIC_TAP(10, fx[0], fx[1]);

        float ml = kick + drums[0] + bass[0] + pad[0] + arp[0] + plk[0] + lead[0] + dly[0] + rev[0] + cho[0];
        float mr = kick + drums[1] + bass[1] + pad[1] + arp[1] + plk[1] + lead[1] + dly[1] + rev[1] + cho[1];

        /* build-up highpass, faded in and out over a few milliseconds so the drop lands cleanly */
        M.lift_w += ((M.lift_on ? 1.0f : 0.0f) - M.lift_w) * 0.005f;
        if (M.lift_w > 1e-4f) {
            ml += (svf_hp(&M.hp_l, &M.hp_co, ml) - ml) * M.lift_w;
            mr += (svf_hp(&M.hp_r, &M.hp_co, mr) - mr) * M.lift_w;
            if (M.lift_on) M.lift_t += 1.0f / RATE;
        }
        M.gap_w += ((M.gap_on ? 0.0f : 1.0f) - M.gap_w) * 0.01f; /* the breath before a drop */
        ml = ml * M.gap_w + fx[0];
        mr = mr * M.gap_w + fx[1];

        /* master: DC blocker and a gentle bus compressor */
        M.dc_yl = ml - M.dc_xl + 0.9995f * M.dc_yl;
        M.dc_xl = ml;
        M.dc_yr = mr - M.dc_xr + 0.9995f * M.dc_yr;
        M.dc_xr = mr;
        ml = M.dc_yl;
        mr = M.dc_yr;
        float pk = maxf(fabsf(ml), fabsf(mr));
        M.c_env = pk > M.c_env ? pk : M.c_env + (pk - M.c_env) * 0.00015f;
        M.c_gain += (M.c_target - M.c_gain) * (M.c_target < M.c_gain ? 0.05f : 0.0005f);

        float g = M.fade * vol * S->gain * M.c_gain * 1.15f; /* with makeup gain */
        out[i * 2] += ml * g;
        out[i * 2 + 1] += mr * g;
    }
}

/* ------------------------------------------------------------ parsing */
static int parse_pc(const char **pp) {
    static const int8_t PCS[7] = {9, 11, 0, 2, 4, 5, 7};
    const char *p = *pp;
    int c = *p | 32;
    if (c < 'a' || c > 'g') return -1;
    int pc = PCS[c - 'a'];
    p++;
    if (*p == '#') { pc++; p++; }
    else if (*p == 'b') { pc--; p++; }
    *pp = p;
    return (pc + 12) % 12;
}

/* 9th chords are voiced rootless (3 5 7 9) the way keyboard players do; the bass has the root */
static const struct { const char *name; int n; int8_t iv[4]; } QUALITY[] = {
    {"", 3, {0, 4, 7}},        {"m", 3, {0, 3, 7}},     {"7", 4, {0, 4, 7, 10}}, {"m7", 4, {0, 3, 7, 10}},
    {"maj7", 4, {0, 4, 7, 11}}, {"sus2", 3, {0, 2, 7}}, {"sus4", 3, {0, 5, 7}},  {"add9", 4, {0, 2, 4, 7}},
    {"madd9", 4, {0, 2, 3, 7}}, {"6", 4, {0, 4, 7, 9}}, {"m6", 4, {0, 3, 7, 9}}, {"dim", 3, {0, 3, 6}},
    {"5", 2, {0, 7}},          {"m9", 4, {3, 7, 10, 14}}, {"maj9", 4, {4, 7, 11, 14}},
};

static int chord_id(SongData *sd, const Chord *c) {
    for (int i = 0; i < sd->nchords; i++) {
        const Chord *o = &sd->chords[i];
        if (o->root == c->root && o->bass == c->bass && o->n == c->n && !memcmp(o->iv, c->iv, sizeof(c->iv))) return i;
    }
    if (sd->nchords >= MAX_CHORDS) {
        SDL_Log("music: too many chords in one song");
        return 0;
    }
    sd->chords[sd->nchords] = *c;
    return sd->nchords++;
}

static void parse_chords(SongData *sd, SecData *out, const char *src) {
    uint8_t idx[64];
    int len[64], n = 0;
    const char *p = src ? src : "";
    while (n < 64) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *tok = p;
        Chord c = {0};
        c.root = parse_pc(&p);
        char q[8];
        int qn = 0, qi = -1;
        while (*p && *p != ' ' && *p != ':' && *p != '/') {
            if (qn < 7) q[qn++] = *p;
            p++;
        }
        q[qn] = 0;
        for (int k = 0; k < (int)(sizeof(QUALITY) / sizeof(QUALITY[0])); k++)
            if (!strcmp(QUALITY[k].name, q)) qi = k;
        if (c.root < 0 || qi < 0) {
            SDL_Log("music: bad chord '%.8s'", tok);
            c.root = 0;
            qi = 0;
        }
        c.n = QUALITY[qi].n;
        memcpy(c.iv, QUALITY[qi].iv, sizeof(c.iv));
        c.bass = c.root;
        if (*p == '/') {
            p++;
            int b = parse_pc(&p);
            if (b >= 0) c.bass = b;
        }
        int l = 16;
        if (*p == ':') {
            char *e;
            l = (int)strtol(p + 1, &e, 10);
            p = e;
        }
        while (*p && *p != ' ') p++;
        idx[n] = (uint8_t)chord_id(sd, &c);
        len[n++] = l > 0 ? l : 16;
    }
    if (!n) {
        idx[n] = (uint8_t)chord_id(sd, &(Chord){0, 0, 3, {0, 4, 7}});
        len[n++] = 16;
    }
    for (int s = 0, k = 0, j = 0; s < out->steps; s++) {
        out->chord[s] = idx[k];
        if (++j >= len[k]) { j = 0; k = (k + 1) % n; }
    }
}

static void parse_lead(SecData *out, const char *src) {
    static int8_t ev[MAX_STEPS];
    static uint8_t fx[MAX_STEPS];
    int n = 0, len = 4;
    for (const char *p = src; p && *p;) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *tok = p;
        int note = LEAD_OFF;
        uint8_t f = 0;
        if (*p == 'r') {
            p++;
        } else {
            int pc = parse_pc(&p);
            char *e;
            long oct = strtol(p, &e, 10);
            if (pc < 0 || e == p) {
                SDL_Log("music: bad note '%.6s'", tok);
                break;
            }
            p = e;
            note = (int)(12 * (oct + 1)) + pc;
        }
        while (*p && *p != ' ') {
            if (*p == ':') {
                char *e;
                len = (int)strtol(p + 1, &e, 10);
                p = e;
                continue;
            }
            if (*p == '!') f |= FX_ACCENT;
            else if (*p == '~') f |= FX_SLIDE;
            p++;
        }
        for (int j = 0; j < len && n < MAX_STEPS; j++, n++) {
            ev[n] = (int8_t)(j ? LEAD_HOLD : note);
            fx[n] = j ? 0 : f;
        }
    }
    if (n % 16) SDL_Log("music: melody length %d is not whole bars", n);
    for (int s = 0; s < out->steps; s++) {
        out->lead[s] = n ? ev[s % n] : (int8_t)(s ? LEAD_HOLD : LEAD_OFF);
        out->lead_fx[s] = n ? fx[s % n] : 0;
    }
}

static void compact(Pat *out, const char *src) {
    out->n = 0;
    for (const char *p = src; p && *p; p++) {
        if (*p == ' ') continue;
        if (out->n == MAX_PAT) {
            SDL_Log("music: pattern too long '%s'", src);
            break;
        }
        out->s[out->n++] = *p;
    }
    out->s[out->n] = 0;
}

static void compact_drums(Pat *out, const Drums *d) {
    compact(&out[0], d ? d->kick : NULL);
    compact(&out[1], d ? d->snare : NULL);
    compact(&out[2], d ? d->hat : NULL);
    compact(&out[3], d ? d->perc : NULL);
    compact(&out[4], d ? d->shaker : NULL);
    compact(&out[5], d ? d->ride : NULL);
}

static void parse_songs(void) {
    for (int s = 0; s < SONG_COUNT; s++) {
        const Song *S = &SONGS[s];
        SongData *sd = &SD[s];
        sd->sec = (SecData *)calloc((size_t)S->nsec, sizeof(SecData));
        for (int i = 0; i < S->nsec; i++) {
            const Section *sec = &S->sec[i];
            SecData *o = &sd->sec[i];
            int bars = sec->bars < 1 ? 1 : sec->bars > MAX_BARS ? MAX_BARS : sec->bars;
            o->steps = bars * 16;
            o->riser_at = o->steps - (bars >= 4 ? 32 : 16);
            o->b0 = sec->bright0;
            o->b1 = sec->bright1;
            if (o->b0 == 0 && o->b1 == 0) o->b0 = o->b1 = 1;
            parse_chords(sd, o, sec->chords);
            parse_lead(o, sec->lead);
            compact(&o->pad, sec->pad);
            compact(&o->bass, sec->bass);
            compact(&o->arp, sec->arp);
            compact(&o->pluck, sec->pluck);
            compact_drums(o->drum, sec->drums);
            compact_drums(o->fill, sec->fill);
        }
    }
}

/* ------------------------------------------------------------ interface */
void music_init(void) {
    memset(&M, 0, sizeof(M));
    M.song = M.target = -1;
    parse_songs();
    reverb_init();
    M.dl = (float *)calloc(DELAY_MAX, sizeof(float));
    M.drr = (float *)calloc(DELAY_MAX, sizeof(float));
    M.cb = (float *)calloc(CHORUS_MAX, sizeof(float));
    DC.k_p1 = decay_mul(0.025f);
    DC.k_p2 = decay_mul(0.15f);
    DC.k_click = decay_mul(0.005f);
    svf_setk(&DC.kclick, 3500, 1.2f);
    DC.s_body = decay_mul(0.2f);
    DC.s_snap = decay_mul(0.28f);
    DC.s_pitch = decay_mul(0.035f);
    DC.h_closed = decay_mul(0.07f);
    DC.h_open = decay_mul(0.45f);
    DC.d_amp = decay_mul(1.4f);
    DC.t_amp = decay_mul(0.7f);
    DC.t_pitch = decay_mul(0.2f);
    DC.g_amp = decay_mul(0.22f);
    DC.g_slap = decay_mul(0.012f);
    DC.g_amp2 = decay_mul(0.07f);
    DC.i_amp = decay_mul(1.4f);
    DC.i_p = decay_mul(0.25f);
    svf_setk(&DC.swell, 6000, 1.4f);
    DC.m_amp = decay_mul(0.035f);
    DC.z_amp = decay_mul(0.08f);
    DC.z_att = decay_coef(0.012f);
    DC.y_amp = decay_mul(2.4f);
    DC.air = onepole(10000);
    DC.damp = decay_coef(0.12f);
    svf_setk(&DC.snare, 3500, 1.4f);
    svf_set(&DC.hat_bp, 8500, 0.3f);
    svf_set(&DC.hat_hp, 6000, 0.1f);
    svf_setk(&DC.ride, 4800, 1.0f / 3);
    svf_set(&DC.clap, 1200, 0.5f);
    svf_setk(&DC.slap, 2200, 1.0f);
    svf_setk(&DC.shaker, 6500, 1.0f / 1.2f);
    svf_setk(&DC.crash, 5000, 1.25f);
}

void music_shutdown(void) {
    for (int s = 0; s < SONG_COUNT; s++) {
        free(SD[s].sec);
        SD[s].sec = NULL;
    }
    free(rev_mem);
    free(M.dl);
    free(M.drr);
    free(M.cb);
    rev_mem = NULL;
    M.dl = M.drr = M.cb = NULL;
}

void music_request(int song) { M.target = song >= 0 && song < SONG_COUNT ? song : -1; }
