/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* AMBI: an ambient noise generator (Fantasma), in LOFI's place. Two layers that play together, each with its own
 * controls, so one track can be a whole ambience (wind and rain, a sea, a fire, a record):
 *   BED     white noise tilted by COLR through pink (-3 dB/oct) to brown (-6 dB/oct), then a resonant filter (FREQ,
 *           RES: low-pass at 0, a narrow band-pass at 127). SWELL moves FREQ and the level in slow random swells,
 *           RATE how slowly (0: a swell every half minute, 127: every second or two): waves, gusts, breathing.
 *   CRKL    random pops, 1 to 2000 a second (CRKL; 0: none), not through the bed's colour or filter: each a short
 *           burst of noise with a random size, POP its length (0: a dry tick, 127: a soft thud), BAL the balance
 *           with the bed (64: both full; below it the pops give way, above it the bed). Dry ticks are vinyl, a few of them fire, many of them rain.
 * The bed's noise and filter are NOISE's (eng_noise.c); the pops are this engine's. The track's FLT moves FREQ, SHP
 * moves COLR. State: s[0] s[1] the filter, s[2] the random generator, s[3] the swell's target and countdown,
 * s[4..6] the pink poles, s[7] the brown pole; ph[0] the pop's envelope, ph[2] the swell. */
static void ambi_note_on(track_t *t, voice_t *v)
{
    uint32_t seed = (v->note + 1u) * 0x9E3779B9u ^ (uint32_t)v->fine * 0x85EBCA6Bu;   /* UNISON: each its own */
    if (t->p[P_REL] >= 127 && !t->p[P_ESYNC]) {          /* REL at the top (voice.c): the noise that goes on is crossfaded into this one */
        uint32_t i;
        for (i = 0; i < NVOICE; i++)
            if (&t->v[i] != v && t->v[i].active && t->v[i].stage == 3)
                t->v[i].stage = 5;                        /* (it fades out as this one rises: voice.c) */
    }
    v->s[0] = v->s[1] = 0;
    v->s[2] = (int32_t)(seed ? seed : 1u);
    v->s[3] = 0;                                      /* a new swell target at once */
    v->s[4] = v->s[5] = v->s[6] = v->s[7] = 0;
    v->ph[0] = 0;
    v->ph[1] = 0;
    v->ph[2] = 0;
}

static void ambi_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t i;
    int32_t swell = p[P_E3], rate = p[P_E6], bedf = p[P_E7] <= 64 ? 32767 : (127 - p[P_E7]) * 512, crkf = p[P_E7] >= 64 ? 32767 : p[P_E7] * 512, pe = (int32_t)v->ph[0];
    int32_t ic1 = v->s[0], ic2 = v->s[1], rng = v->s[2], b0 = v->s[4], b1 = v->s[5], b2 = v->s[6], br = v->s[7];
    int32_t drift = (int32_t)v->ph[2], d16 = 0, gain = 32767, gq, colr, cut, k, a1, a2, a3, g, bpg, mix, cf, thr = 0;
    int32_t dec = 31000 + p[P_E5] * 13;               /* a pop's decay per sample: 0.946 .. 0.9985 */
    if (swell) {                                      /* a new random target every 0.7 .. 24 s, followed with a lag */
        int32_t tg = v->s[3] >> 16, cnt = v->s[3] & 0xFFFF;
        if (--cnt <= 0) {
            uint32_t r = noise32(&rng);
            tg = (int32_t)(r >> 16) - 32768;
            cnt = (1024 + (int32_t)(r & 3071u)) * (1 + ((127 - rate) >> 4));
            if (cnt > 0xFFFF)
                cnt = 0xFFFF;
        }
        v->s[3] = (int32_t)((uint32_t)tg << 16 | (uint32_t)cnt);
        drift += ((tg << 4) - drift) >> 10;           /* Q19 */
        d16 = ((drift >> 11) * swell * swell) >> 14;  /* up to about +-16 semitones */
        gain = 32767 - ((((32767 - (drift >> 4)) >> 1) * swell) >> 8);   /* down to -6 dB as it falls */
    }
    colr = clamp((p[P_E0] << 8) + m->shape - (64 << 8), 0, 127 << 8);   /* COLR (+ SHP): white -> pink -> brown */
    cf = colr <= (64 << 8) ? colr << 1 : ((colr - (64 << 8)) * 520) >> 8;   /* the crossfade, Q15 */
    cut = clamp((p[P_E1] << 8) + m->cutoff + ((d16 * 4785) >> 8), 0, 127 << 8);   /* FREQ (+ FLT), the swell */
    g = SVF_G[cut >> 8];
    if (cut < (127 << 8))
        g += ((SVF_G[(cut >> 8) + 1] - g) * (cut & 255)) >> 8;
    k = 8192 - p[P_E2] * 7800 / 127;                  /* damping, Q12: 2.0 .. 0.1 */
    a1 = (int32_t)((4096u << 13) / (uint32_t)(4096 + ((g * (g + k)) >> 12)));
    a2 = (a1 * g) >> 12;
    a3 = (a2 * g) >> 12;
    bpg = (int32_t)isqrt32((uint32_t)k << 12);        /* band-pass x 1.5 sqrt(damping): its level holds with the Q */
    mix = p[P_E2] * 258;                              /* low-pass -> band-pass, Q15 */
    {   /* the bed's level: what the filter takes from white noise made up (less for pink and brown), the swell's */
        int32_t wf = 32767 - ((colr * 230) >> 8), mk = (((((127 << 8) - cut) >> 8) * 7) * wf) >> 15;
        mk += 214;
        if (mk > 1279)
            mk = 1279;
        gq = (4096 + ((mk & 255) << 4)) << (mk >> 8);
        gq = (gq * (gain >> 3)) >> 12;
        if (gq > 65535)
            gq = 65535;
    }
    if (p[P_E4]) {                                    /* pops: 1 .. 2000 a second */
        uint32_t e = (uint32_t)p[P_E4] * 22u, r = 1u << (e >> 8);   /* 2^(CRKL * 11 / 127), Q8 fraction */
        r += (r * (e & 255u)) >> 8;
        thr = (int32_t)((r * 97391u) >> 16);          /* per 2^16 of a sample: rate * 65536 / FS, Q16 */
    }
    for (i = 0; i < n; i++) {
        int32_t w, x, v1, v2, v3, y, c = 0;
        uint32_t r = noise32(&rng);
        w = (int32_t)(r >> 17) - 16384;
        x = w;
        if (colr) {                                   /* pink: three leaky poles (Kellet), brown: a leaky integrator */
            int32_t pk;
            b0 = b0 - ((b0 * 78) >> 15) + ((w * 3245) >> 15);   /* (x 32690 / 32768 as b0 - b0 * 78: b0 reaches
                                                                * +-70000, b0 * 32690 overflowed into clicks) */
            b1 = ((b1 * 31555) >> 15) + ((w * 9716) >> 15);
            b2 = ((b2 * 18678) >> 15) + ((w * 17247) >> 14);
            pk = (b0 + b1 + b2 + ((w * 6055) >> 15)) >> 2;
            if (colr <= (64 << 8)) {
                x = w + mulq15(pk - w, cf);
            } else {
                br = ((br * 32580) >> 15) + ((w * 3480) >> 15);
                x = pk + mulq15(br - pk, cf);
            }
        }
        v3 = x - ic2;                                 /* the SVF (eng_noise.c), with the band-pass */
        v1 = (a1 * ic1 + a2 * v3 + 4096) >> 13;
        v2 = ic2 + ((a2 * ic1 + a3 * v3 + 4096) >> 13);
        ic1 = clamp(2 * v1 - ic1, -150000, 150000);
        ic2 = clamp(2 * v2 - ic2, -150000, 150000);
        v1 = (v1 * bpg * 3) >> 13;
        y = v2 + (((v1 - v2) * (mix >> 3)) >> 12);
        y = soft_knee(((y >> 4) * gq) >> 8, 24000);
        if (thr) {                                    /* a pop: its size at random, then a burst of noise falling away */
            uint32_t r2 = noise32(&rng);
            if ((int32_t)(r2 & 0xFFFFu) < thr)
                pe = 12000 + (int32_t)((r2 >> 17) & 0x5FFFu);
            if (pe > 0) {
                c = mulq15(pe, (int32_t)(r2 >> 17) - 16384) << 1;
                pe = (int32_t)(((int64_t)pe * dec) >> 15);
            }
        }
        out[i] += voice_amp(soft_knee(mulq15(y, bedf) + mulq15(c, crkf), 28000), m, i);
    }
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = rng;
    v->s[4] = b0;
    v->s[5] = b1;
    v->s[6] = b2;
    v->s[7] = br;
    v->ph[0] = (uint32_t)pe;
    v->ph[2] = (uint32_t)drift;
}

/* {COLR, FREQ, RES, SWELL, CRKL, POP, RATE, BAL} */
static const preset_t AMBI_PRESETS[] = {
    /* a bed alone: SWELL moves it (CRKL 0) */
    {"SEA", {110, 62, 15, 110, 0, 0, 20, 0}, {70, 90, 120, 64}, 0, 0, FX(0, 0, 25, 85), PAT(0)},
    {"SURF", {70, 78, 30, 127, 0, 0, 45, 0}, {60, 90, 115, 64}, 0, 0, FX(0, 0, 30, 80), PAT(0)},
    {"STORM", {127, 45, 25, 127, 0, 0, 60, 0}, {90, 90, 127, 64}, 0, 0, FX(0, 0, 30, 95), PAT(0)},
    {"WIND", {64, 70, 100, 90, 0, 0, 55, 0}, {90, 90, 110, 64}, 0, 0, FX(0, 30, 20, 80), PAT(0)},
    {"STREAM", {40, 92, 70, 90, 0, 0, 90, 0}, {50, 90, 120, 64}, 0, 0, FX(0, 0, 20, 70), PAT(0)},
    {"HISS", {0, 110, 0, 0, 0, 0, 64, 0}, {20, 90, 127, 64}, 0, 0, FX(0, 0, 0, 30), PAT(0)},
    /* pops on a bed: the two layers together */
    {"FIRE", {115, 50, 12, 60, 55, 55, 40, 80}, {30, 90, 120, 64}, 0, 0, FX(0, 0, 10, 55), PAT(0)},
    {"VINYL", {20, 105, 10, 0, 50, 8, 64, 105}, {5, 90, 127, 64}, 0, 0, FX(0, 0, 0, 25), PAT(0)},
    {"RAIN", {45, 100, 20, 30, 105, 15, 60, 80}, {30, 90, 127, 64}, 0, 0, FX(0, 0, 20, 70), PAT(0)},
    {"STORM RAIN", {110, 70, 20, 100, 95, 25, 70, 72}, {50, 90, 127, 64}, 0, 0, FX(0, 0, 30, 90), PAT(0)},
    {"CAVE DRIP", {120, 40, 30, 40, 22, 90, 30, 100}, {40, 90, 127, 64}, 0, 0, FX(0, 0, 40, 95), PAT(0)},
    {"CRICKETS", {10, 118, 110, 20, 85, 3, 64, 90}, {40, 90, 127, 64}, 0, 0, FX(0, 0, 15, 60), PAT(0)},
};

static const engine_t ENG_AMBI = {
    .name = "AMBI",
    .page_title = {"BED", "CRKL"},
    .edit = {
        {"COLR", F_PCT, 0, 127, 64, 0, 0},
        {"FREQ", F_CUTOFF, 0, 127, 80, 0, 0},
        {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"SWELL", F_PCT, 0, 127, 50, 0, 0},
        {"CRKL", F_PCT, 0, 127, 0, 0, 0},
        {"POP", F_PCT, 0, 127, 40, 0, 0},
        {"RATE", F_PCT, 0, 127, 64, 0, 0},
        {"BAL", F_PCT, 0, 127, 70, 0, 0},
    },
    .presets = AMBI_PRESETS,
    .npresets = NELEM(AMBI_PRESETS),
    .note_on = ambi_note_on,
    .render = ambi_render,
    .knob = {P_E1, P_E3, P_E4, P_REL},
    .keep = 0xF3,                /* the filter, the colour poles */
};
