/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Gadlele */
/* CHIME (Fantasma 2.0, BELLS' engine, part 3): bells, glass and metal that float over the drone and the pad. Two
 * sines, the carrier at the note and a modulator at a RATIO of it (harmonic or not: 1.41, 2.76, 3.5, 5.4 .. are the
 * metal ones), in one of three MODEs:
 *   RING   the carrier multiplied by the modulator (INDEX: how much): two sidebands that are not the note, metallic
 *   FM     the modulator moves the carrier's phase (INDEX: its depth), the modulator fed back on itself (FDBK: from a
 *          pure ring to a rough, noisy edge)
 *   R+FM   both: the FM tone, then rung by the modulator
 * TONE page: ENV (the index follows the envelope: bright at the strike, mellowing as it rings), DTN (a second carrier a
 * few cents off: the slow shimmer of a real bell), BREATH (a slow swell of the index and the level, each voice at its
 * own pace: the bells breathe) and TONE (a gentle low-pass: 127 open). Those four are track parameters a CHIME sound has
 * no other use for (DIGITAL's retired OP 3 envelope, core.h CH_*; params.c track_desc labels them).
 * GEN (EDIT 2) is BELL's generator (seq.c gen_step): DENS, EVOLVE, RANGE, SEED in the same places, the same garden.
 * The idea, the LIVEN Ambient 0's Atmos layer (a ring modulation and a two-operator FM with feedback); the code is new. */
static const char *const N_CH_MODE[] = {"RING", "FM", "R+FM"};
static const char *const N_CH_RATIO[] = {"1", "1.41", "2", "2.76", "3.5", "4.2", "5.4", "7.1"};
static const uint16_t CH_RATIO_Q8[8] = {256, 361, 512, 707, 896, 1075, 1382, 1818};
static const char *const N_CH_RANGE[] = {"1 OCT", "2 OCT", "3 OCT", "4 OCT"};
enum { CM_RING, CM_FM, CM_RFM };

/* the TONE page (CH_ENV .. CH_TONE) */
static const param_desc_t CH_P[4] = {
    {"ENV", F_PCT, 0, 127, 60, 0, 0},
    {"DTN", F_INT, 0, 50, 5, 0, "ct"},
    {"BREATH", F_PCT, 0, 127, 20, 0, 0},
    {"TONE", F_PCT, 0, 127, 110, 0, 0},
};

/* state: ph[0] the carrier, ph[1] the modulator, ph[2] the second carrier (DTN); s[0], s[1] the modulator's last two
 * values (FDBK), s[2] BREATH's phase, s[3] TONE's low-pass */
static void chime_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = v->ph[1] = v->ph[2] = 0;          /* every strike starts the same */
    v->s[0] = v->s[1] = 0;
    v->s[3] = 0;
    v->s[2] = (int32_t)(v->age * 0x9E3779B9u);    /* BREATH: each voice from its own point */
}

static void chime_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t mode = (uint32_t)p[P_E0] % 3u, i, k, kfb;
    uint32_t inc = m->inc, minc = (inc >> 8) * CH_RATIO_Q8[(uint32_t)p[P_E1] & 7u];
    int32_t det = clamp(p[CH_DTN], 0, 50), br = clamp(p[CH_BREATH], 0, 127), idx, bw, lvl, a;
    uint32_t inc2 = det ? cents_inc(m->pitch16, det, m->fine) : 0;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t f0 = v->s[0], f1 = v->s[1], lp = v->s[3], ring;
    /* BREATH: one slow sine a voice (~0.12 .. 0.25 Hz), on the index and, a quarter as deep, the level */
    v->s[2] += (int32_t)((14000u + (v->age & 7u) * 2000u) * CTL);
    bw = osc_sine((uint32_t)v->s[2]) * br >> 7;  /* Q15 x depth */
    lvl = 32767 - ((bw > 0 ? bw : -bw) >> 2);
    /* the index: INDEX, ENV of the envelope, the matrix's CUT and SHP, BREATH */
    idx = (p[P_E2] << 8) + m->cutoff + mulq15(m->envq15, clamp(p[CH_ENV], 0, 127) * 256) + (m->shape - (64 << 8)) +
          (bw >> 3);
    idx = clamp(idx, 0, 127 << 8);
    k = (uint32_t)(idx >> 4) * (mode == CM_RING ? 0u : 64u);   /* FM: up to ~6 radians (BELL's 81: 7.9) */
    ring = mode == CM_FM ? 0 : mode == CM_RING ? idx : idx >> 1;   /* RING: how much, Q15 (R+FM: half as much) */
    kfb = (uint32_t)clamp(p[P_E3], 0, 127) * 245u;           /* FDBK: up to ~1.5 radians */
    a = (clamp(p[CH_TONE], 0, 127) + 1) << 8;                /* TONE: the low-pass's pole, Q15 (128 << 8: open) */
    for (i = 0; i < n; i++) {
        int32_t mo = osc_sine(ph1 + (uint32_t)((f0 + f1) >> 1) * kfb), s;
        uint32_t pm = (uint32_t)mo * k;
        f1 = f0;
        f0 = mo;
        s = osc_sine(ph0 + pm);
        if (det) {
            s = (s + osc_sine(ph2 + pm)) >> 1;
            ph2 += inc2;
        }
        if (ring)
            s += mulq15(mulq15(s, mo) - s, ring);            /* toward the ring product as RING / INDEX says */
        ph0 += inc;
        ph1 += minc;
        if (a < 32768) {
            lp += mulq15(s - lp, a);
            s = lp;
        }
        out[i] += voice_amp(mulq15(s, lvl), m, i) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[0] = f0;
    v->s[1] = f1;
    v->s[3] = lp;
}

static const preset_t CHIME_PRESETS[] = {
    /* name, {MODE, RATIO, INDEX, FDBK, DENS, EVOLVE, RANGE, SEED}, {A D S R}, fenv, mono (DENS 0: the keys play the bells;
     * EVOLVE RANGE SEED are what the generator will play once DENS is up) */
    {"GLASS", {CM_FM, 4, 30, 0, 0, 20, 1, 21}, {5, 100, 0, 100}, 0, 0, FX(0, 25, 40, 85), PAT(7)},
    {"CRYSTAL", {CM_FM, 6, 22, 10, 0, 20, 1, 64}, {3, 95, 0, 95}, 0, 0, FX(0, 25, 40, 85), PAT(7)},
    {"ICE", {CM_RING, 5, 60, 0, 0, 20, 1, 9}, {10, 100, 0, 105}, 0, 0, FX(0, 30, 40, 90), PAT(7)},
    {"BOWL", {CM_FM, 3, 18, 0, 0, 15, 0, 33}, {40, 115, 0, 118}, 0, 0, FX(0, 20, 25, 90), PAT(8)},
    {"TEMPLE", {CM_FM, 1, 35, 15, 0, 15, 1, 47}, {8, 110, 0, 112}, 0, 0, FX(0, 20, 30, 90), PAT(8)},
    {"GAMELAN", {CM_RFM, 4, 40, 20, 0, 25, 1, 12}, {2, 90, 0, 95}, 0, 0, FX(0, 20, 40, 75), PAT(7)},
    {"TUBULAR", {CM_FM, 3, 45, 0, 0, 15, 1, 90}, {2, 108, 0, 110}, 0, 0, FX(0, 15, 30, 85), PAT(8)},
    {"SPARKLE", {CM_RING, 7, 50, 0, 0, 30, 2, 77}, {2, 80, 0, 85}, 0, 0, FX(0, 30, 50, 80), PAT(7)},
    {"RAINDROP", {CM_FM, 2, 25, 0, 0, 30, 2, 5}, {1, 70, 0, 80}, 0, 0, FX(0, 20, 50, 80), PAT(7)},
    {"STARLIGHT", {CM_RFM, 6, 30, 10, 0, 20, 2, 101}, {20, 100, 0, 110}, 0, 0, FX(0, 40, 40, 95), PAT(7)},
    {"DARK METAL", {CM_RING, 1, 80, 0, 0, 15, 0, 55}, {15, 105, 0, 110}, 0, 0, FX(0, 20, 30, 85), PAT(8)},
    {"SHIMMER", {CM_FM, 2, 15, 30, 0, 20, 1, 38}, {30, 110, 0, 115}, 0, 0, FX(0, 50, 35, 95), PAT(7)},
    {"TINE", {CM_FM, 0, 40, 0, 0, 20, 1, 70}, {1, 85, 0, 85}, 0, 0, FX(0, 30, 35, 70), PAT(7)},
    {"RING MOD", {CM_RING, 3, 100, 0, 0, 20, 1, 3}, {5, 95, 0, 100}, 0, 0, FX(0, 20, 40, 80), PAT(7)},
};
/* each sound's TONE page: ENV DTN BREATH TONE */
static const uint8_t CH_TONEV[][4] = {
    {70, 4, 20, 100},            /* GLASS */
    {80, 6, 25, 110},            /* CRYSTAL */
    {0, 8, 30, 115},             /* ICE */
    {40, 3, 60, 80},             /* BOWL */
    {60, 5, 30, 85},             /* TEMPLE */
    {50, 10, 10, 95},            /* GAMELAN */
    {75, 3, 10, 90},             /* TUBULAR */
    {0, 12, 40, 120},            /* SPARKLE */
    {90, 2, 0, 100},             /* RAINDROP */
    {40, 9, 50, 110},            /* STARLIGHT */
    {20, 5, 35, 60},             /* DARK METAL */
    {30, 14, 70, 105},           /* SHIMMER */
    {100, 2, 10, 90},            /* TINE */
    {0, 0, 20, 100},             /* RING MOD */
};
_Static_assert(NELEM(CH_TONEV) == NELEM(CHIME_PRESETS), "a TONE a sound");
/* the TONE values of CHIME sound k on track t (apply_preset_to); k past the sounds: the defaults (CLEAN INIT) */
static void ch_tone_load(track_t *t, uint32_t k)
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        t->p[CH_ENV + i] = k < NELEM(CH_TONEV) ? CH_TONEV[k][i] : CH_P[i].def;
}

static const engine_t ENG_CHIME = {
    .name = "CHIME",
    .page_title = {"CHIME", "GEN"},
    .edit = {
        {"MODE", F_ENUM, 0, 2, CM_FM, N_CH_MODE, 0},
        {"RATIO", F_ENUM, 0, 7, 4, N_CH_RATIO, 0},
        {"INDEX", F_PCT, 0, 127, 30, 0, 0},
        {"FDBK", F_PCT, 0, 127, 0, 0, 0},
        {"DENS", F_INT, 0, 100, 0, 0, "%"},
        {"EVOLVE", F_INT, 0, 100, 20, 0, "%"},
        {"RANGE", F_ENUM, 0, 3, 1, N_CH_RANGE, 0},
        {"SEED", F_INT, 0, 127, 0, 0, 0},
    },
    .presets = CHIME_PRESETS,
    .npresets = NELEM(CHIME_PRESETS),
    .note_on = chime_note_on,
    .render = chime_render,
    .knob = {P_E2, P_E4, P_E5, P_REL},
};
