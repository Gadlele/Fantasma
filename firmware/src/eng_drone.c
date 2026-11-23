/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Gadlele */
/* DRONE (Fantasma 2.0, DRONE's engine, part 1): a drone that breathes and drifts, and that you play. Each voice is
 *   a main oscillator that blends SINE > TRI > SAW > SQUARE (WAVE; the square's pulse WIDTH from 50 % to 5 %),
 *   a partner, the same wave at an INTERVAL from it (tempered or just: a just fifth or third does not beat; DETUNE
 *     adds the beating on purpose, up to 30 cents),
 *   a SUB, a sine an octave to four below, or a subharmonic (a third or a fifth of the pitch, as the
 *     Subharmonicon's), crossfaded with the two oscillators at equal power (MIX: 0 the oscillators, 127 the sub),
 *   a soft pink NOISE,
 * into a filter (its own page, FILTER: TYPE LP / BP / HP, CUT, RES and KTR, how far it follows the note; ENV, the LFO,
 * the matrix's CUT and the key's pressure move it as well), and DRIFT: the voice is never quite still. Every fifth of a second the main and the
 * partner pick a new pitch to glide to (up to 12 cents), and every half a minute or so the voice wanders slowly in
 * brightness, level and wave. The matrix's SHP (and the LFO's SHP) move WAVE.
 * Its presets start LEGATO with a glide: a new key bends the drone there; on track 1 REL at its top is INF (seq.c
 * noise_keep): it never stops.
 * The design (the blend of the four waves, the partner at a tempered or just interval with its detune, the sub's
 * octaves and subharmonics and the equal-power OSC / SUB mix, the two speeds of drift) is Niagara Drift's, Evan
 * Yerburgh, GPL-3.0 (github.com/evanyerburgh/niagara-drift, firmware/src/dsp/drone.c, v0.3.1); this is a fixed-point
 * rewrite for Felucca's voices, with no code of its float one. */
static const char *const N_DR_IV[] = {"-OCT", "-5TH", "-5TH J", "-4TH", "-4TH J", "UNISON", "+m3", "+m3 J", "+M3",
                                      "+M3 J", "+4TH", "+4TH J", "+5TH", "+5TH J", "+OCT", "+OCT+5", "+OCT+5 J"};
/* the partner's ratio to the main oscillator, Q16: tempered 2^(n/12), just the pure ratio (2/3, 3/4, 6/5, 5/4, 4/3,
 * 3/2, 3) */
static const uint32_t DR_IV_Q16[17] = {32768, 43740, 43691, 49096, 49152, 65536, 77936, 78643, 82570,
                                       81920, 87427, 87381, 98193, 98304, 131072, 196386, 196608};
static const char *const N_DR_SUB[] = {"-1 OCT", "-2 OCT", "1/3", "1/5", "-3 OCT", "-4 OCT"};

/* the FILTER page's values (Fantasma 2.0): the engine's eight are full, so CUT RES KTR are three track parameters no
 * DRONE sound otherwise has, DIGITAL's OP 1 ATK DEC SUS (retired: read only when a DIGITAL sound converts,
 * fm4_convert.c; core.h P_DCUT ..), and TYPE ANALOG's (P_FTYPE). Their labels and defaults on a DRONE track
 * (params.c track_desc); each sound sets them (dr_filter_load). CUT is the corner at C4, KTR how far it follows the
 * note (127: all the way, 0: not at all) */
static const param_desc_t DR_FILT_P[3] = {
    {"CUT", F_CUTOFF, 0, 127, 87, 0, 0},
    {"RES", F_PCT, 0, 127, 22, 0, 0},
    {"KTR", F_PCT, 0, 127, 127, 0, 0},
};

/* the state: s[0], s[1] the filter, s[2] the noise generator, s[3] the pink noise, s[4] / s[5] the main's and the
 * partner's drift and s[6] the slow wander (Q15, each now and its target: high and low 16 bits), s[7] the wander's
 * target. ph[0] main, ph[1] partner, ph[2] sub */
#define DR_HI(x) ((int32_t)(x) >> 16)
#define DR_LO(x) ((int32_t)(int16_t)((uint32_t)(x) & 0xFFFFu))
#define DR_PACK(now, tgt) ((int32_t)(((uint32_t)(now) << 16) | ((uint32_t)(tgt) & 0xFFFFu)))

static void drone_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[1] = v->ph[0];                         /* the partner starts in phase (UNISON: one oscillator, not none) */
    v->s[0] = v->s[1] = 0;
    v->s[3] = 0;
    if (!v->s[2])
        v->s[2] = 0x2F6E2B1 + (int32_t)v->age * 7919;
}

/* one of the four waves at phase ph: 0 sine, 1 triangle, 2 saw, 3 square of pulse width pw */
static inline __attribute__((always_inline)) int32_t drone_wave(uint32_t w, uint32_t ph, uint32_t inc, uint32_t pw)
{
    switch (w) {
    case 0: return osc_sine(ph);
    case 1: return osc_tri(ph);
    case 2: return osc_saw(ph, inc);
    default: return osc_pulse(ph, inc, pw);       /* (half scale at 50 %: as loud as the saw) */
    }
}

/* a drift value (Q15) a block nearer its target; a new target now and then (one block in 2^sh on average) */
static inline int32_t drone_walk(int32_t st, int32_t *ns, uint32_t sh, uint32_t glide)
{
    int32_t now = DR_HI(st), tgt = DR_LO(st);
    if (!(noise32(ns) >> (32u - sh)))
        tgt = (int32_t)(noise32(ns) >> 17) - 16384;   /* -16384..16383: half scale */
    now += (tgt - now) >> glide;
    if (now == DR_HI(st) && now != tgt)               /* (the last steps: one at a time) */
        now += tgt > now ? 1 : -1;
    return DR_PACK(now, tgt);
}

static void drone_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int32_t drift = p[P_E7], ns = v->s[2], pk = v->s[3], ic1 = v->s[0], ic2 = v->s[1];
    int32_t d0, d1, wd, wq, pos, fq, mix, og, sg, nz, lvl, cut;
    uint32_t inc0, inc1, inc2, ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2], seg, pw, i, sub, ft;
    int32_t fk;
    uint64_t pinc;
    tsvf_t flt;
    /* DRIFT: the main and the partner each a new pitch a fifth of a second apart (2^8 blocks), gliding ~0.4 s; the
     * wander a new place every ~25 s (2^15 blocks), ~6 s to get there */
    v->s[4] = drone_walk(v->s[4], &ns, 8u, 9u);
    v->s[5] = drone_walk(v->s[5], &ns, 8u, 9u);
    v->s[6] = drone_walk(v->s[6], &ns, 15u, 13u);
    d0 = DR_HI(v->s[4]) * drift >> 16;            /* 1/4096 of the pitch: +-13 cents at DRIFT 127 (2.37 a cent) */
    d1 = DR_HI(v->s[5]) * drift >> 16;
    wd = DR_HI(v->s[6]) * drift >> 7;             /* the wander, -16384..16383 at DRIFT 127 */
    /* the pitches: main, partner (its INTERVAL, DETUNE up to 30 cents: 72 / 4096), sub */
    inc0 = m->inc + (uint32_t)((int32_t)(m->inc >> 12) * d0);
    pinc = (uint64_t)(m->inc + (uint32_t)((int32_t)(m->inc >> 12) * (d1 + p[P_E3] * 72 / 127))) *
           DR_IV_Q16[(uint32_t)p[P_E2] % 17u] >> 16;
    inc1 = pinc > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)pinc;
    sub = (uint32_t)p[P_E4] % 6u;
    inc2 = sub == 0u ? inc0 >> 1 : sub == 1u ? inc0 >> 2 : sub == 2u ? inc0 / 3u : sub == 3u ? inc0 / 5u :
           sub == 4u ? inc0 >> 3 : inc0 >> 4;
    /* WAVE (and the matrix's SHP, and the wander): 0..127 << 8 over the three blends */
    wq = clamp((p[P_E0] << 8) + (m->shape - (64 << 8)) + (wd >> 4), 0, 127 << 8);
    pos = wq * 3 / 127;                           /* 0..768: the blend's place, 1/256 */
    seg = (uint32_t)pos >> 8;
    fq = (pos & 255) << 7;                        /* the blend toward the next wave, Q15 */
    if (seg >= 3u) {
        seg = 2u;
        fq = 32768;
    }
    pw = 0x80000000u - (uint32_t)p[P_E1] * (0x73333333u / 127u);   /* WIDTH: 50 % .. 5 % */
    /* MIX at equal power: the oscillators cos, the sub sin of its quarter turn; the main and the partner 0.62 each */
    mix = p[P_E5];
    og = osc_sine((uint32_t)mix * (0x40000000u / 127u) + 0x40000000u) * 20316 >> 15;
    sg = osc_sine((uint32_t)mix * (0x40000000u / 127u)) * 29491 >> 15;
    nz = p[P_E6] * 140;                           /* NOISE: up to ~0.55 */
    lvl = 32767 - (wd > 0 ? wd : -wd) / 2;        /* the wander's level: down to -2.5 dB */
    /* the filter: CUT at C4, KTR of the note's distance from it (14 steps an octave: 30 Hz .. 16 kHz over 127; at
     * 127 a step 6/7 of a semitone), the routings and the wander on it */
    cut = (p[P_DCUT] << 8) + p[P_DKTR] * (m->pitch16 - 60 * 16) * 56 / (3 * 127) + m->cutoff + (wd >> 3);
    ft = (uint32_t)p[P_FTYPE] % 3u;
    fk = ANALOG_K(p[P_DRES]);
    tsvf_coef(&flt, clamp(cut, 0, 127 << 8), p[P_DRES]);
    for (i = 0; i < n; i++) {
        int32_t a = drone_wave(seg, ph0, inc0, pw), b = drone_wave(seg, ph1, inc1, pw), s, y, ya;
        if (fq) {
            a += ((drone_wave(seg + 1u, ph0, inc0, pw) - a) * fq) >> 15;
            b += ((drone_wave(seg + 1u, ph1, inc1, pw) - b) * fq) >> 15;
        }
        s = mulq15(a + b, og) + mulq15(osc_sine(ph2), sg);
        if (nz) {
            pk += (((int32_t)(noise32(&ns) >> 17) - 16384) - pk) >> 3;   /* white through a pole: soft, pink-ish */
            s += mulq15(pk, nz);
        }
        ph0 += inc0;
        ph1 += inc1;
        ph2 += inc2;
        y = ft ? analog_tap(&flt, fk, s >> 1, &ic1, &ic2, ft) : tsvf_lp(&flt, s >> 1, &ic1, &ic2);
        ya = y < 0 ? -y : y;                      /* the soft knee (as ANALOG's) */
        if (ya > 16000) {
            ya = 16000 + (softclip((ya - 16000) * 2) >> 1);
            y = y < 0 ? -ya : ya;
        }
        out[i] += voice_amp(mulq15(y, lvl), m, i) * 5 >> 1;   /* (as loud as ANALOG's DRONE) */
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = ns;
    v->s[3] = pk;
}

static const preset_t DRONE_PRESETS[] = {
    /* name, {WAVE, WIDTH, INTERVAL, DETUNE, SUB, MIX, NOISE, DRIFT}, {A D S R}, fenv, mono (1: LEGATO, with a glide) */
    {"DRONE", {60, 20, 5, 20, 0, 35, 4, 50}, {100, 100, 127, 112}, 0, 1, FX(0, 40, 20, 90), PAT(14), GLD(40)},
    {"FIFTHS", {45, 0, 13, 10, 1, 30, 0, 45}, {100, 100, 127, 112}, 0, 1, FX(0, 30, 20, 90), PAT(14), GLD(40)},
    {"OCTAVES", {30, 0, 14, 14, 0, 40, 0, 40}, {95, 100, 127, 110}, 0, 1, FX(0, 40, 20, 85), PAT(14), GLD(40)},
    {"DEEP", {20, 0, 0, 8, 1, 60, 6, 35}, {105, 100, 127, 114}, 0, 1, FX(0, 20, 10, 85), PAT(14), GLD(50)},
    {"HOLLOW", {110, 70, 12, 18, 0, 25, 0, 50}, {100, 100, 127, 112}, 0, 1, FX(0, 50, 25, 90), PAT(14), GLD(40)},
    {"GLOW", {10, 0, 9, 6, 0, 30, 0, 40}, {110, 100, 127, 116}, 0, 1, FX(0, 40, 25, 95), PAT(14), GLD(40)},
    {"TIDE", {64, 30, 5, 40, 0, 30, 25, 100}, {110, 100, 127, 116}, 0, 1, FX(0, 60, 20, 95), PAT(14), GLD(60)},
    {"RUST", {85, 40, 3, 25, 2, 35, 12, 70}, {95, 100, 127, 110}, 0, 1, FX(10, 30, 25, 85), PAT(14), GLD(40)},
    {"VESSEL", {50, 0, 16, 12, 3, 35, 0, 55}, {105, 100, 127, 114}, 0, 1, FX(0, 40, 20, 95), PAT(14), GLD(40)},
    {"HUM", {5, 0, 5, 4, 1, 50, 2, 25}, {90, 100, 127, 110}, 0, 1, FX(0, 20, 10, 80), PAT(14), GLD(30)},
    {"WIND HUM", {40, 0, 1, 30, 0, 30, 60, 90}, {110, 100, 127, 116}, 0, 1, FX(0, 50, 20, 95), PAT(14), GLD(60)},
    {"SHADOW", {75, 20, 7, 15, 1, 45, 5, 60}, {105, 100, 127, 114}, 0, 1, FX(0, 40, 20, 90), PAT(14), GLD(40)},
};

/* each sound's FILTER: CUT RES KTR TYPE (0 LP, 1 BP, 2 HP), in DRONE_PRESETS' order */
static const uint8_t DR_FILT[][4] = {
    {87, 22, 127, 0},            /* DRONE */
    {84, 22, 127, 0},            /* FIFTHS */
    {90, 18, 127, 0},            /* OCTAVES */
    {74, 30, 110, 0},            /* DEEP */
    {70, 55, 110, 1},            /* HOLLOW: a band, a little resonant */
    {92, 15, 127, 0},            /* GLOW */
    {86, 35, 120, 0},            /* TIDE */
    {82, 70, 127, 0},            /* RUST: resonant */
    {88, 22, 127, 0},            /* VESSEL */
    {70, 20, 100, 0},            /* HUM: dark */
    {80, 40, 127, 0},            /* WIND HUM */
    {78, 45, 120, 0},            /* SHADOW */
};
_Static_assert(NELEM(DR_FILT) == NELEM(DRONE_PRESETS), "a FILTER a sound");
/* the FILTER values of DRONE sound k on track t (apply_preset_to); k past the sounds: the defaults (CLEAN INIT) */
static void dr_filter_load(track_t *t, uint32_t k)
{
    uint32_t i;
    for (i = 0; i < 3u; i++)
        t->p[P_DCUT + i] = k < NELEM(DR_FILT) ? DR_FILT[k][i] : DR_FILT_P[i].def;
    t->p[P_FTYPE] = k < NELEM(DR_FILT) ? DR_FILT[k][3] : 0;
}

static const engine_t ENG_DRONE = {
    .name = "DRONE",
    .page_title = {"OSC", "MIX"},
    .edit = {
        {"WAVE", F_PCT, 0, 127, 60, 0, 0},
        {"WIDTH", F_PCT, 0, 127, 20, 0, 0},
        {"INTVL", F_ENUM, 0, 16, 5, N_DR_IV, 0},
        {"DETUNE", F_PCT, 0, 127, 20, 0, 0},
        {"SUB", F_ENUM, 0, 5, 0, N_DR_SUB, 0},
        {"MIX", F_PCT, 0, 127, 35, 0, 0},
        {"NOISE", F_PCT, 0, 127, 4, 0, 0},
        {"DRIFT", F_PCT, 0, 127, 50, 0, 0},
    },
    .presets = DRONE_PRESETS,
    .npresets = NELEM(DRONE_PRESETS),
    .note_on = drone_note_on,
    .render = drone_render,
    .knob = {P_E0, P_E2, P_E5, P_E7},           /* WAVE INTVL MIX DRIFT */
    .poly = 4,
    .keep = 0xF3,                                /* the filter, the drifts */
};
