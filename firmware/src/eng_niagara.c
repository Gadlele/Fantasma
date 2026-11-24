/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Gadlele */
/* NIAGARA (Fantasma 2.0, DRONE's engine, part 1): Niagara Drift's drones (Evan Yerburgh, GPL-3.0,
 * github.com/evanyerburgh/niagara-drift), their own code (niagara/niagara.c, built apart with the FPU): two drones,
 * each a main oscillator blending sine > triangle > saw > square, a partner at an interval, a sub, a noise, its own
 * 12 dB filter with drive; the ULTRA LFO with its two waves and each drone's taps; the PULSE; Drift, Character, Age,
 * Feedback, Wander; pitch link, hard sync, cross-mod; stereo (drone I left, II right). Its effects (delay, drive,
 * reverb) are left out for the channel's own (INSERT, SLICER, CH DELAY, CH REVERB, the sends).
 * Played as a Fantasma channel: a key sets drone I's pitch (drone II at its interval from it, LINK), Niagara's own
 * glide takes it there; the channel's ENV is its amplitude (REL at its top on DRONE: INF, it never stops), so the
 * drones start and stop with the keys or the steps. MONO (LEGATO): one drone pair.
 * Its 70 values, in Niagara's own units, on Fantasma's pages: EDIT 1 / 2 (DRONE I, DRONE I 2) are the engine's eight
 * (P_E0..P_E7: NK_WAVE1 .. NK_SUB1, saved as any engine's); the other 62 (NK_CUT1 ..) on FILTER I, NOISE I, DRONE II
 * .. WANDER (EDIT), LFO .. LFO TO (LFO), PULSE (ENV), held in nia_v and saved with the project (project.c, the DRONE
 * track's FM6 place: the NIAGARA block) and a user preset (up_fm6.c). The UI pass sends what changed
 * (nia_poll: drone.c takes its commands from one side only), the key's pitch too. */
#include "niagara/niagara.h"

static const char *const N_NIA_ONOFF[] = {"OFF", "ON"};
static const char *const N_NIA_SUB[] = {"-", "-1 OCT", "-2 OCT", "1/3", "1/5", "-3 OCT", "-4 OCT"};   /* (1..6) */
static const char *const N_NIA_SUBW[] = {"SQR", "SINE", "TRI", "SAW"};
static const char *const N_NIA_MODE[] = {"LP", "BP", "HP"};
static const char *const N_NIA_XMOD[] = {"OFF", "CUT", "PITCH", "SNARL"};
static const char *const N_NIA_TUN[] = {"12TET", "1/4TON", "19TET", "31TET", "53TET", "JUST", "HARM", "LMY", "HARRSN",
                                        "SHRUTI", "PARTCH"};
static const char *const N_NIA_OUT[] = {"IN", "II OUT", "I OUT", "OUT"};
static const char *const N_NIA_WSC[] = {"MINPNT", "MAJPNT", "5+8", "WHOLE", "DORIAN", "JUST8"};
static const char *const N_NIA_W1[] = {"OFF", "TRI", "SQR", "SAW", "ROUND", "2xTRI", "2xEXP", "4xPAT", "RAND", "AND3",
                                       "OR3"};
static const char *const N_NIA_W2[] = {"OFF", "TRI90", "SQR90", "EXPSAW", "LOGTRI", "2xSAW", "2xPAT", "4xEXP", "RNDGL",
                                       "AND5", "OR5"};
static const char *const N_NIA_TAP[] = {"OFF", "I", "II", "I+II"};
static const char *const N_NIA_ODST[] = {"PITCH", "XWAVE"};
static const char *const N_NIA_FDST[] = {"CUT", "RES"};

/* the values' descriptors, NK_* order (labels Fantasma's; ranges and defaults drone.c's: tests/niagara_test.c checks
 * them against niagara.c's nia_range) */
#define NI(l, mn, mx, df) {l, F_INT, mn, mx, df, 0, 0}
#define NE(l, n, mn, mx, df) {l, F_ENUM, mn, mx, df, n, 0}
static const param_desc_t NIA_P[NK_N] = {
    NI("WAVE", 0, 100, 67), NI("WIDTH", 0, 100, 30), NI("MIX", 0, 100, 30), NI("LEVEL", 0, 100, 84),
    NE("INTVL", N_DR_IV, 0, 16, 5), NI("DETUN", 0, 100, 12), NE("PARTN", N_NIA_ONOFF, 0, 1, 1), NE("SUB", N_NIA_SUB, 1, 6, 1),
    NI("CUT", 0, 1000, 575), NI("RES", 0, 1000, 250), NE("MODE", N_NIA_MODE, 0, 2, 0), NI("DRIVE", 0, 100, 20),
    NI("NOISE", 0, 100, 0), NI("COLOR", 0, 100, 100), NE("SUBWV", N_NIA_SUBW, 0, 3, 0),
    NI("WAVE", 0, 100, 33), NI("WIDTH", 0, 100, 30), NI("MIX", 0, 100, 15), NI("LEVEL", 0, 100, 68),
    NE("INTVL", N_DR_IV, 0, 16, 14), NI("DETUN", 0, 100, 12), NE("PARTN", N_NIA_ONOFF, 0, 1, 1), NE("SUB", N_NIA_SUB, 1, 6, 1),
    NI("CUT", 0, 1000, 600), NI("RES", 0, 1000, 250), NE("MODE", N_NIA_MODE, 0, 2, 0), NI("DRIVE", 0, 100, 20),
    NI("NOISE", 0, 100, 0), NI("COLOR", 0, 100, 100), NE("SUBWV", N_NIA_SUBW, 0, 3, 0),
    NI("II", -4800, 4800, 700), NE("LINK", N_NIA_ONOFF, 0, 1, 0), NE("SYNC", N_NIA_ONOFF, 0, 1, 0), NE("XMOD", N_NIA_XMOD, 0, 3, 0),
    NI("XAMT", 0, 100, 40), NI("WIDE", 0, 100, 60), NE("TUNE", N_NIA_TUN, 0, 10, 0), NE("OUT", N_NIA_OUT, 0, 3, 0),
    NI("DRIFT", 0, 100, 30), NI("CHAR", 0, 100, 0), NI("AGE", 0, 100, 0), NI("FDBK", 0, 100, 0),
    NI("WANDR", 0, 100, 0), NE("SCALE", N_NIA_WSC, 0, 5, 0),
    NI("RATE", 0, 152, 74), NE("WAVE1", N_NIA_W1, 0, 10, 1), NE("WAVE2", N_NIA_W2, 0, 10, 8), NI("MOVE", 0, 100, 50),
    NI("SPEED", -100, 100, 0), NE("SRC", N_NIA_TAP, 1, 3, 2), NE("LOCK", N_NIA_ONOFF, 0, 1, 0),
    NE("OSC", N_NIA_TAP, 0, 3, 1), NI("DEPTH", -100, 100, 0), NE("FILT", N_NIA_TAP, 0, 3, 1), NI("DEPTH", -100, 100, 85),
    NE("OSC", N_NIA_TAP, 0, 3, 2), NI("DEPTH", -100, 100, 0), NE("FILT", N_NIA_TAP, 0, 3, 2), NI("DEPTH", -100, 100, -85),
    NE("OSC I", N_NIA_ODST, 0, 1, 0), NE("FLT I", N_NIA_FDST, 0, 1, 0), NE("OSC II", N_NIA_ODST, 0, 1, 0),
    NE("FLT II", N_NIA_FDST, 0, 1, 0),
    NE("PULSE", N_NIA_ONOFF, 0, 1, 1), NI("RATE", 0, 100, 6), NI("DEPTH", 0, 100, 40), NI("SHAPE", -100, 100, -25),
    NI("SWING", 0, 50, 0), NI("GLIDE", 0, 100, 7), NI("MORPH", 0, 100, 34),
};

static int16_t nia_v[NK_N];                      /* the values (NK_WAVE1 .. NK_SUB1: the track's P_E0..P_E7 instead) */
static uint8_t nia_fresh = 1;                    /* every value to send (a start, a load) */
static volatile uint8_t nia_note = 50, nia_note_sent;   /* the key's note (the audio's, nia_note_on), sent */
static int32_t nia_lr[2 * CTL];                  /* this block's drones, L R L R (nia_block) */
static int32_t nia_left[CTL];                    /* the voices' left (SPREAD: voice.c nia_spread) */
static uint8_t nia_on;                           /* drone.c started (nia_boot) */

static int nia_track(const track_t *t) { return t == &trk[0] && t->engine == ENGI_NIAGARA; }
static int16_t nia_val(const track_t *t, uint32_t k) { return k < 8u ? t->p[P_E0 + k] : nia_v[k]; }

/* every value at its default (Niagara Drift's starting sound) */
static void nia_defaults(void)
{
    uint32_t k;
    for (k = 0; k < NK_N; k++)
        nia_v[k] = NIA_P[k].def;
    nia_fresh = 1;
}
static void nia_boot(void)                       /* main.c felucca_init (and the tests' power-on) */
{
    nia_init();
    nia_on = 1;
    nia_defaults();
}

/* the UI pass: what changed since the last pass to drone.c (its one sender), and the key's pitch */
static void nia_poll(void)
{
    const track_t *t = &trk[0];
    uint32_t k;
    uint8_t note = nia_note;
    if (!nia_on || t->eng_req != ENGI_NIAGARA)
        return;
    for (k = 0; k < NK_N; k++) {
        int16_t v = nia_val(t, k);
        if (nia_fresh || v != nia_get(k))          /* (what drone.c holds: a block takes the commands) */
            nia_set(k, v);
    }
    nia_fresh = 0;
    if (note != nia_note_sent) {
        nia_note_sent = note;
        nia_pitch((int32_t)note * 100);
    }
}

/* Preset k's values: its eight into p, the rest into nia_v; its reverb and delay mixes as the channel's sends */
static void nia_preset_load(track_t *t, uint32_t k)
{
    int16_t v[NK_N], rev, dly;
    uint32_t i;
    nia_preset(k, v, &rev, &dly);
    for (i = 0; i < NK_N; i++) {
        if (i < 8u)
            t->p[P_E0 + i] = v[i];
        else
            nia_v[i] = v[i];
    }
    t->p[P_REV] = (int16_t)(rev * 127 / 100);
    t->p[P_DLY] = (int16_t)(dly * 127 / 100);
}

static void nia_note_on(track_t *t, voice_t *v)
{
    if (t == &trk[0])
        nia_note = v->note;
}

/* once a block: the drones, while a voice sounds (else drone.c only takes its commands: no time passes for it) */
static void nia_block(track_t *t)
{
    uint32_t i, any = 0;
    if (!nia_track(t))
        return;
    for (i = 0; i < CTL; i++)
        nia_left[i] = 0;
    for (i = 0; i < NVOICE; i++)
        any |= t->v[i].active;
    if (any && nia_on)
        nia_render(nia_lr, CTL);
    else if (nia_on)
        nia_render(nia_lr, 0);                   /* (the commands only) */
}

/* a voice: the block's drones through its ENV (the channel's amplitude) */
static void nia_render_voice(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    uint32_t i;
    (void)v;
    if (!nia_track(t))
        return;
    for (i = 0; i < n && i < CTL; i++) {
        int32_t l = voice_amp(clamp(nia_lr[2u * i], -32767, 32767), m, i) << 1;
        int32_t r = voice_amp(clamp(nia_lr[2u * i + 1u], -32767, 32767), m, i) << 1;
        out[i] += l + r;
        nia_left[i] += l;
    }
}

#if !FELUCCA_ADPCM
/* engine 17 without FELUCCA_ADPCM: reserved (ADPCM's), never selectable (engines.c eng_ok); silent */
static void adpcm_gone_note_on(track_t *t, voice_t *v) { (void)t; (void)v; }
static void adpcm_gone_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    (void)t; (void)v; (void)out; (void)n; (void)m;
}
static const engine_t ENG_ADPCM_GONE = {
    .name = "-",
    .page_title = {"-", "-"},
    .edit = {{"-", F_INT, 0, 127, 0, 0, 0}, {"-", F_INT, 0, 127, 0, 0, 0}, {"-", F_INT, 0, 127, 0, 0, 0},
             {"-", F_INT, 0, 127, 0, 0, 0}, {"-", F_INT, 0, 127, 0, 0, 0}, {"-", F_INT, 0, 127, 0, 0, 0},
             {"-", F_INT, 0, 127, 0, 0, 0}, {"-", F_INT, 0, 127, 0, 0, 0}},
    .note_on = adpcm_gone_note_on,
    .render = adpcm_gone_render,
    .knob = {P_E0, P_E1, P_E2, P_REL},
};
#endif

/* SPREAD (voice.c track_render): the part's left, the voices' sum being both sides */
static void nia_spread(int32_t *sb, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n && i < CTL; i++)
        sb[i] = nia_left[i];
}

static const preset_t NIA_PRESETS[] = {
    /* name, {the eight: nia_preset_load's}, {A D S R}, fenv, mono, FX, PAT, wow, SPREAD (all of it: the drones' stereo) */
#define NP(n) {.name = n, .env = {20, 70, 127, 110}, .mono = 1, .fx = {1, 1, 1, 1}, .sprd = 128}
    NP("NIAGARA"), NP("EMBERS"), NP("VESPERS"), NP("DREAMHOUSE"), NP("ABYSS"), NP("SEANCE"), NP("SONAR"), NP("SIBYL"),
    NP("HOWL"), NP("CISTERN"), NP("PISTON"), NP("ASHRAM"), NP("TELEGRAPH"), NP("TAPE"), NP("FALLING"), NP("BARE"),
#undef NP
};

static const engine_t ENG_NIAGARA = {
    .name = "NIAGARA",
    .page_title = {"DRONE I", "DRONE I 2"},
    .edit = {
        NI("WAVE", 0, 100, 67), NI("WIDTH", 0, 100, 30), NI("MIX", 0, 100, 30), NI("LEVEL", 0, 100, 84),
        NE("INTVL", N_DR_IV, 0, 16, 5), NI("DETUN", 0, 100, 12), NE("PARTN", N_NIA_ONOFF, 0, 1, 1),
        NE("SUB", N_NIA_SUB, 1, 6, 1),
    },
    .presets = NIA_PRESETS,
    .npresets = NELEM(NIA_PRESETS),
    .note_on = nia_note_on,
    .render = nia_render_voice,
    .block = nia_block,
    .knob = {P_E0, P_E2, P_E5, P_REV},
};
#undef NI
#undef NE

/* a card's value as Niagara Drift shows it, where its numbers mean more than a number: the filter in tenths, the times,
 * the colour (draw_columns, after param_format); X-WAVE its number, 0 SINE 33 TRI 67 SAW 100 SQR (names would change
 * the size of its type on LARGE's tall cards) */
static void nia_card(uint32_t k, int32_t v, char *val, const char **unit)
{
    char b[16];
    uint32_t i;
    *unit = "";
    switch (k) {
    case NK_CUT1:
    case NK_CUT2:
    case NK_RES1:
    case NK_RES2:
        fmt_int(val, v / 10);
        if (v % 10) {
            uint32_t l = str_len(val);
            val[l] = '.';
            val[l + 1u] = (char)('0' + v % 10);
            val[l + 2u] = 0;
        }
        return;
    case NK_COLOR1:
    case NK_COLOR2:
        if (v <= 0 || v >= 100) {
            str_cpy(val, v <= 0 ? "WHITE" : "PINK", 7);
            return;
        }
        break;
    case NK_WANDER:
        if (!v) {
            str_cpy(val, "OFF", 7);
            return;
        }
        break;
    case NK_IIOFS:
    case NK_LRATE:
    case NK_PRATE:
    case NK_PDEPTH:
    case NK_GLIDE:
    case NK_MORPH:
    case NK_MOVE:
        nia_text(k, v, b);
        for (i = 0; b[i] && i < 6u; i++)
            val[i] = (char)(b[i] >= 'a' && b[i] <= 'z' ? b[i] - 32 : b[i]);
        val[i] = 0;
        return;
    default:
        break;
    }
    fmt_int(val, v);
}

/* a card's value (ui_draw.c draw_columns, after param_format): NIAGARA's, where it is one of them */
static void nia_card_of(const int16_t *vp, const param_desc_t *d, char *val, const char **unit)
{
    uint32_t k;
    if (d->fmt == F_ENUM)
        return;
    if (vp >= nia_v && vp < nia_v + NK_N)
        k = (uint32_t)(vp - nia_v);
    else if (trk[0].eng_req == ENGI_NIAGARA && vp >= &trk[0].p[P_E0] && vp < &trk[0].p[P_E0 + 8])
        k = (uint32_t)(vp - &trk[0].p[P_E0]);
    else
        return;
    nia_card(k, *vp, val, unit);
}

/* ---- the NIAGARA block (project.c: track 1's FM6 place; up_fm6.c: a user preset's): 0xF2 0xA4 (bytes an FM6 patch
 * never holds), the version (1), the count NK_N, then values NK_CUT1 .. (the first eight are the track's P_E0..P_E7,
 * saved with it): those within -100..155 a byte (+ 100), the others (CUT, RES, II) two, little-endian */
#define NIA_BLOCK 128u
static int nia_wide(uint32_t k) { return NIA_P[k].min < -100 || NIA_P[k].max > 155; }
static void nia_defaults_to(int16_t *v)
{
    uint32_t k;
    for (k = 0; k < NK_N; k++)
        v[k] = NIA_P[k].def;
}
static void nia_pack(uint8_t *b, const int16_t *v)
{
    uint32_t k, pos = 4;
    for (k = 0; k < NIA_BLOCK; k++)
        b[k] = 0;
    b[0] = 0xF2u; b[1] = 0xA4u; b[2] = 1u; b[3] = NK_N;
    for (k = 8; k < NK_N; k++) {
        int32_t x = clamp(v[k], NIA_P[k].min, NIA_P[k].max);
        if (nia_wide(k)) {
            b[pos++] = (uint8_t)((uint32_t)x & 0xFFu);
            b[pos++] = (uint8_t)((uint32_t)x >> 8 & 0xFFu);
        } else {
            b[pos++] = (uint8_t)(x + 100);
        }
    }
}
static int nia_unpack(const uint8_t *b, int16_t *v)
{
    uint32_t k, pos = 4, n;
    nia_defaults_to(v);
    if (b[0] != 0xF2u || b[1] != 0xA4u)
        return 0;
    n = b[3] < NK_N ? b[3] : NK_N;
    for (k = 8; k < n; k++) {
        int32_t x;
        if (nia_wide(k)) {
            x = (int16_t)(b[pos] | b[pos + 1u] << 8);
            pos += 2u;
        } else {
            x = (int32_t)b[pos++] - 100;
        }
        v[k] = (int16_t)clamp(x, NIA_P[k].min, NIA_P[k].max);
    }
    return 1;
}
/* the block as 7-bit bytes (a user preset's FM6 place, up_fm6.c): its first 112 bytes, 8 to 7 bits into 128; back */
static void nia_to7(const uint8_t *b, uint8_t *pk)
{
    uint32_t i, acc = 0, n = 0, o = 0;
    for (i = 0; i < NIA_BLOCK; i++) {
        while (n < 7u) {
            acc |= (uint32_t)(o < 112u ? b[o] : 0u) << n;
            o++;
            n += 8u;
        }
        pk[i] = (uint8_t)(acc & 127u);
        acc >>= 7;
        n -= 7u;
    }
}
static int nia_from7(const uint8_t *pk, uint8_t *b)
{
    uint32_t i, acc = 0, n = 0, o = 0;
    for (i = 0; i < NIA_BLOCK; i++)
        b[i] = 0;
    for (i = 0; i < NIA_BLOCK && o < 112u; i++) {
        acc |= (uint32_t)(pk[i] & 127u) << n;
        n += 7u;
        while (n >= 8u && o < 112u) {
            b[o++] = (uint8_t)acc;
            acc >>= 8;
            n -= 8u;
        }
    }
    return b[0] == 0xF2u && b[1] == 0xA4u;
}
_Static_assert(4u + (NK_N - 8u) + 5u <= 112u, "the NIAGARA block in 112 bytes");
