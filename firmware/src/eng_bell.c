/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* BELL: a soft two-operator FM bell (Fantasma). A sine carrier phase-modulated by a sine whose pitch is a fixed
 * RATIO of the note's; the modulator's index (TONE) is what makes it a bell: a high one is a metallic clang, a low
 * one a round, nearly pure tone, and ENV lets the index follow the envelope, so a strike is bright and the tone
 * mellows as it rings out. DTN adds a second carrier a few cents off (the same modulator), which beats slowly
 * against the first: the shimmer of a real bell. The amplitude is the voice's ADSR: a bell is a long DECAY with
 * SUSTAIN 0.
 * GENERATIVE: with DENS above 0 the track stops reading its steps and plays four loops of unequal length (13, 17, 23
 * and 29 steps: seq.c gen_step) that drift against each other and repeat only every 148,000 steps. DENS is how many of
 * their slots sound, EVOLVE how fast the loops change as they come round, RANGE the octaves they roam, SEED which set
 * of loops: the same SEED is the same bells. The notes are snapped to the track's ROOT and SCALE.
 * The idea of this bell (an inharmonic ratio, an index that falls with the note) is AMB-1's, Charles Vestal,
 * GPL-3.0 (github.com/charlesvestal/fm1-amb); this is a fixed-point rewrite for Felucca's voices. */
static const char *const N_BL_RANGE[] = {"1 OCT", "2 OCT", "3 OCT", "4 OCT"};
static const char *const N_BL_RATIO[] = {"1", "1.5", "2", "2.76", "3", "3.5", "4.77"};
static const uint16_t BL_RATIO_Q8[7] = {256, 384, 512, 707, 768, 896, 1221};   /* x256 */

static void bell_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = 0;                /* carrier, modulator, second carrier: every strike starts the same */
    v->ph[1] = 0;
    v->ph[2] = 0;
}

static void bell_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t ratio = (uint32_t)p[P_E0] % 7u, i, k;
    int32_t det = p[P_E3], tone;
    uint32_t inc = m->inc, minc = (inc >> 8) * BL_RATIO_Q8[ratio], inc2 = det ? cents_inc(m->pitch16, det, m->fine) : 0;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    /* the index: TONE, the matrix's CUT / SHP, and ENV of the envelope on top (as PHASE's DCW) */
    tone = (p[P_E1] << 8) + m->cutoff + mulq15(m->envq15, p[P_E2] * 256) + (m->shape - (64 << 8));
    tone = clamp(tone, 0, 127 << 8);
    /* tone / 16 = the index in 1/256 radian (7.9 rad at the top); a phase offset of s (Q15) * index / 2pi turns is
     * s * (index / 256) * 81.5 in 1 / 2^32 turn: unsigned, so it wraps as a phase does */
    k = (uint32_t)(tone >> 4) * 81u;
    for (i = 0; i < n; i++) {
        uint32_t pm = (uint32_t)osc_sine(ph1) * k;
        int32_t s = osc_sine(ph0 + pm);
        if (det) {
            s = (s + osc_sine(ph2 + pm)) >> 1;
            ph2 += inc2;
        }
        ph0 += inc;
        ph1 += minc;
        out[i] += voice_amp(s, m, i) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
}

static const preset_t BELL_PRESETS[] = {
    /* name, {RATIO, TONE, ENV, DTN, DENS, EVOLVE, RANGE, SEED}, {A D S R}, fenv, mono (DENS starts at 0: the keys play the bell; EVOLVE RANGE SEED are what it will play once DENS is up) */
    /* wooden */
    {"THUMB", {2, 28, 85, 0, 0, 0, 0, 0}, {8, 75, 0, 65}, 0, 0, FX(0, 0, 30, 50), PAT(3)},
    {"TOY", {2, 18, 70, 0, 0, 0, 0, 0}, {10, 80, 0, 70}, 0, 0, FX(0, 0, 30, 50), PAT(3)},
    {"HEARTH", {2, 26, 65, 5, 0, 0, 0, 0}, {25, 105, 0, 105}, 0, 0, FX(0, 0, 30, 50), PAT(3)},
    {"AMBER", {4, 22, 55, 6, 0, 0, 0, 0}, {30, 100, 0, 100}, 0, 0, FX(0, 0, 30, 50), PAT(3)},
    {"COPPER", {4, 30, 60, 7, 0, 0, 0, 0}, {22, 100, 0, 100}, 0, 0, FX(0, 0, 30, 50), PAT(3)},
    {"SOFT TOLL", {2, 34, 75, 4, 0, 0, 0, 0}, {22, 108, 0, 110}, 0, 0, FX(0, 0, 30, 50), PAT(3)},
    /* soft */
    {"SILK", {0, 10, 40, 4, 0, 20, 1, 26}, {35, 105, 0, 100}, 0, 0, FX(0, 25, 45, 85), PAT(7)},
    {"COTTON", {2, 12, 50, 3, 0, 20, 1, 59}, {40, 100, 0, 100}, 0, 0, FX(0, 25, 45, 85), PAT(7)},
    {"PETAL", {2, 18, 45, 5, 0, 20, 1, 34}, {30, 90, 0, 95}, 0, 0, FX(0, 25, 45, 85), PAT(7)},
    {"HALO", {0, 16, 60, 8, 0, 20, 1, 115}, {50, 110, 0, 110}, 0, 0, FX(0, 25, 45, 85), PAT(8)},
    {"DAWN", {1, 14, 50, 6, 0, 20, 1, 42}, {45, 105, 0, 105}, 0, 0, FX(0, 25, 45, 85), PAT(7)},
    {"PRAYER", {2, 24, 70, 6, 0, 20, 1, 43}, {20, 110, 0, 115}, 0, 0, FX(0, 25, 45, 85), PAT(8)},
    {"CLOISTER", {1, 32, 70, 6, 0, 20, 1, 50}, {28, 115, 0, 118}, 0, 0, FX(0, 25, 45, 85), PAT(8)},
    /* bright, but not too much */
    {"DEW", {3, 16, 45, 10, 0, 20, 1, 99}, {30, 92, 0, 100}, 0, 0, FX(0, 30, 35, 70), PAT(7)},
    {"WIND CHIME", {5, 24, 70, 10, 0, 20, 1, 58}, {15, 100, 0, 105}, 0, 0, FX(0, 30, 35, 70), PAT(7)},
};

static const engine_t ENG_BELL = {
    .name = "BELL",
    .page_title = {"BELL", "GEN"},
    .edit = {
        {"RATIO", F_ENUM, 0, 6, 4, N_BL_RATIO, 0},
        {"TONE", F_PCT, 0, 127, 40, 0, 0},
        {"ENV", F_PCT, 0, 127, 60, 0, 0},
        {"DTN", F_INT, 0, 50, 6, 0, "ct"},
        {"DENS", F_INT, 0, 100, 0, 0, "%"},
        {"EVOLVE", F_INT, 0, 100, 20, 0, "%"},
        {"RANGE", F_ENUM, 0, 3, 1, N_BL_RANGE, 0},
        {"SEED", F_INT, 0, 127, 0, 0, 0},
    },
    .presets = BELL_PRESETS,
    .npresets = NELEM(BELL_PRESETS),
    .note_on = bell_note_on,
    .render = bell_render,
    .knob = {P_E1, P_E4, P_E5, P_REL},
};
