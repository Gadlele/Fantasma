/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Gadlele, based on Felucca (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* GOLANA (Fantasma 2.0, PAD's engine, part 2): Golana's engine (github.com/Gadlele/Golana, eng_golana.c, 1.3), made
 * smaller for a channel of Fantasma. What it keeps:
 *   OSC 1   SAW (CTRL1: a second saw an octave up blended in, brighter), PULSE (CTRL1: the width, 50 % .. ~6 %),
 *           TRI (CTRL1: folds the wave), SINE (CTRL1: cross modulation, OSC 2 moves its phase), MORPH (CTRL1: from
 *           the saw to the square). Golana's CTRL2 (LFO 1 on CTRL1) is the track's: the LFO's SHP (LFO DEST) and the
 *           matrix's SHP move CTRL1
 *   OSC 2   SAW / SQR / TRI / MIX, MOD OFF / RING / SYNC / R+S, SEMI +-24, TUNE +-63 ct (UNISON: the other way from
 *           OSC 1, every voice two detuned oscillators)
 *   SUB     a square, a sine, a saw or a MIX an octave or two below OSC 1, in step with it
 *   FILTER  LPF24, LPF12, BPF, HPF, MS20, CUT, RESO, KTR (+-63: 63 the keys exactly); the filter EG is the track's
 *           ENV on FLT (ENV DEST), the LFO the track's (LFO DEST FLT)
 *   DRIFT   every note's two oscillators and its cutoff a little off, slowly wandering
 * What it leaves to Fantasma: the two LFOs, the PATCH, the filter EG, the arpeggiator, DIST and the effects (the
 * track's LFO, matrix, ENV, ARP, INSERT and sends do it), and its screens (Fantasma's pages).
 * Pages: EDIT 1 (OSC) WAVE CTRL1 WAVE2 MOD, EDIT 2 (OSC 2) SEMI TUNE OSC2 SUB; FILTER TYPE CUT RESO KTR and TONE OSC1
 * SUBTYP NOISE DRIFT, eight track parameters a GOLANA sound has no other use for (DIGITAL's retired OP 1 and OP 2
 * envelopes, core.h GO_*, as Golana itself keeps them there; params.c track_desc labels them). At most 4 voices. */
static const char *const N_GO1WAVE[] = {"SAW", "PULSE", "TRI", "SINE", "MORPH"};
static const char *const N_GO2WAVE[] = {"SAW", "SQR", "TRI", "MIX"};
static const char *const N_GO2MOD[] = {"OFF", "RING", "SYNC", "R+S"};
static const char *const N_GFTYPE[] = {"LPF24", "LPF12", "BPF", "HPF", "MS20"};
static const char *const N_GSUB[] = {"-1 SQR", "-2 SQR", "-1 SINE", "-2 SINE", "-1 SAW", "-2 SAW", "-1 MIX", "-2 MIX"};
enum { GO_SAW, GO_PULSE, GO_TRI, GO_SINE, GO_MORPH };
enum { GM_OFF, GM_RING, GM_SYNC, GM_RSYNC };

/* the FILTER and TONE pages (GO_FTYPE .. GO_DRIFT, in this order) */
static const param_desc_t GO_P[8] = {
    {"TYPE", F_ENUM, 0, 4, 0, N_GFTYPE, 0},
    {"CUT", F_CUTOFF, 0, 127, 100, 0, 0},
    {"RESO", F_PCT, 0, 127, 20, 0, 0},
    {"KTR", F_BIPCT, -63, 63, 32, 0, 0},
    {"OSC1", F_PCT, 0, 127, 127, 0, 0},
    {"SUBTYP", F_ENUM, 0, 7, 0, N_GSUB, 0},
    {"NOISE", F_PCT, 0, 127, 0, 0, 0},
    {"DRIFT", F_PCT, 0, 127, 0, 0, 0},
};

/* the voice state beyond voice_t (the part's engine memory, engines.c eng_mem) */
typedef struct {
    int32_t f1, f2, f3, f4;      /* the filter: stage 1 (LPF24's first), stage 2 */
    int32_t nst;                 /* noise */
    int32_t jl;                  /* OSC 2 sync: 1 + the step of a restart a sample ago (16 bits), 0 none */
    int32_t tx, tg;              /* TRI's fold: its last input and its antiderivative (ADAA) */
    int32_t sx1, sy1;            /* SYNC: the DC blocker (a restarted saw is off centre), its last in and out */
    uint8_t sc;                  /* SUB: OSC 1's cycles (its low 2 bits: the sub's phase above OSC 1's) */
    uint16_t dn;                 /* DRIFT: blocks until the wander's next target */
    int16_t d1, d2, dc;          /* DRIFT: this note's offsets, OSC 1, OSC 2 and the cutoff (Q15, -1..1) */
    int16_t dw, dt;              /* DRIFT: the slow wander and its target (Q15) */
} gov_t;
typedef struct {
    gov_t v[NVOICE];
    int32_t seed;
} gop_t;
ENG_MEM_FITS("GOLANA", sizeof(gop_t));
static gop_t *go_part(const track_t *t) { return (gop_t *)ENG_MEM((uint32_t)(t - trk) % NPART); }
static inline int32_t gop(const track_t *t, uint32_t id, int32_t lo, int32_t hi) { return clamp(t->p[id], lo, hi); }

/* TRI's fold without its aliasing (ADAA, first order): the triangle T(x) = a - |w|, w = x - a wrapped into [-2a, 2a)
 * (a = 1 << lg); the output is T's mean between the last input and this one, (G(x) - G(x')) / (x - x'), G its
 * antiderivative a w - w |w| / 2; inputs closer than 4 apart take T at their middle */
static inline int32_t go_tri_w(int32_t x, uint32_t lg)
{
    return (int32_t)(((uint32_t)x + (1u << lg)) & ((4u << lg) - 1u)) - (int32_t)(2u << lg);
}
static inline int32_t go_tri_int(int32_t x, uint32_t lg)
{
    int32_t w = go_tri_w(x, lg);
    return (w * ((2 << lg) - (w < 0 ? -w : w))) >> 1;
}
static inline int32_t go_fold_aa(int32_t x, int32_t *xp, int32_t *gp, uint32_t lg)
{
    int32_t g = go_tri_int(x, lg), d = x - *xp, y;
    if (d > 3 || d < -3) {
        y = (g - *gp) / d;
    } else {
        int32_t w = go_tri_w((x + *xp) >> 1, lg);
        y = (1 << lg) - (w < 0 ? -w : w);
    }
    *xp = x;
    *gp = g;
    return y;
}

static void go_note_on(track_t *t, voice_t *v)
{
    gop_t *g = go_part(t);
    gov_t *s = &g->v[(uint32_t)(v - t->v) % NVOICE];
    if (!g->seed)
        g->seed = 0x5EED1234;
    if (v->env == 0 && v->env_out == 0) {               /* a fresh note (a retrigger keeps its oscillators running) */
        s->f1 = s->f2 = s->f3 = s->f4 = 0;
        s->jl = 0;
        s->sx1 = s->sy1 = 0;
        v->ph[0] = noise32(&g->seed);                   /* free-running oscillators: each note from its own point */
        v->ph[1] = noise32(&g->seed);
        s->sc = (uint8_t)(v->ph[1] >> 30);
        s->tx = 0;
        s->tg = go_tri_int(0, 15);
        s->d1 = (int16_t)(noise32(&g->seed) >> 16);     /* DRIFT: each note its own offsets */
        s->d2 = (int16_t)(noise32(&g->seed) >> 16);
        s->dc = (int16_t)(noise32(&g->seed) >> 16);
    }
    if (!s->nst)
        s->nst = 0x2468ACE1 + (int32_t)v->age;
}

/* a saw and a square at once, half each (OSC 2's MIX, the SUB's) */
static inline int32_t go_mix(uint32_t ph, uint32_t inc) { return (osc_saw(ph, inc) >> 1) + osc_pulse(ph, inc, 0x80000000u); }
static inline int32_t go_osc2(uint32_t w, uint32_t ph, uint32_t inc)
{
    return w == 1u ? osc_pulse(ph, inc, 0x80000000u) : w == 2u ? osc_tri(ph) : w == 3u ? go_mix(ph, inc) : osc_saw(ph, inc);
}

/* DRIFT: the note's offsets and the wander (a new target every ~0.37 s, followed over about as long), in 1/4096 of
 * the pitch (up to +-12 ct each oscillator, +-5 ct the wander) -> *df1, *df2, and in cutoff units (the return) */
static __attribute__((noinline)) int32_t go_drift(gop_t *g, gov_t *s, int32_t drift, int32_t *df1, int32_t *df2)
{
    int32_t dw;
    if (!s->dn--) {
        s->dn = 511;
        s->dt = (int16_t)(noise32(&g->seed) >> 16);
    }
    s->dw = (int16_t)(s->dw + ((s->dt - s->dw) >> 9));
    dw = (s->dw * drift) >> 15;
    *df1 = (((s->d1 * drift) >> 15) * 28 >> 7) + (dw * 12 >> 7);
    *df2 = (((s->d2 * drift) >> 15) * 28 >> 7) + (dw * 12 >> 7);
    return ((s->dc * drift) >> 15) * 5 + dw;
}

static void go_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    gop_t *g = go_part(t);
    gov_t *s = &g->v[(uint32_t)(v - t->v) % NVOICE];
    uint32_t w1 = (uint32_t)gop(t, P_E0, 0, 4), w2 = (uint32_t)gop(t, P_E2, 0, 3);
    uint32_t md = (uint32_t)gop(t, P_E3, 0, 3), ft = (uint32_t)gop(t, GO_FTYPE, 0, 4), i;
    int32_t ctrl1, cut, reso, x1, x2, xn, xs, pitch2, k2;
    uint32_t inc1, inc2;
    tsvf_t f1c, f2c;
    int32_t ring = md == GM_RING || md == GM_RSYNC, sync = md == GM_SYNC || md == GM_RSYNC;
    int32_t drift = gop(t, GO_DRIFT, 0, 127), df1 = 0, df2 = 0, dcut = 0;
    uint32_t st = (uint32_t)gop(t, GO_SUBTYP, 0, 7), so = 1u + (st & 1u);   /* SUB: its type, its octaves down */
    if (drift)
        dcut = go_drift(g, s, drift, &df1, &df2);
    /* pitches (1/16 semitone): the voice's (glide, unison, tune, bend, the LFO and ENV on PIT in m) */
    pitch2 = m->pitch16 + gop(t, P_E4, -24, 24) * 16;
    inc1 = df1 ? cents_inc(m->pitch16, 0, m->fine + df1) : m->inc;
    inc2 = cents_inc(clamp(pitch2, 0, 2047), gop(t, P_E5, -63, 63),
                     (t->p[P_VOICE] == V_UNISON ? m->fine - 2 * v->fine : m->fine) + df2);
    if (inc1 > 0x40000000u)
        inc1 = 0x40000000u;
    if (inc2 > 0x40000000u)
        inc2 = 0x40000000u;
    /* CTRL1, moved by the track's SHP (Golana's CTRL2: the LFO; the matrix) */
    ctrl1 = clamp(gop(t, P_E1, 0, 127) + ((m->shape - (64 << 8)) >> 8), 0, 127);
    /* the mixer: levels Q15 */
    x1 = gop(t, GO_OSC1, 0, 127) * 258;
    x2 = gop(t, P_E6, 0, 127) * 258;
    xn = gop(t, GO_NOISE, 0, 127) * 258;
    xs = gop(t, P_E7, 0, 127) * 258;
    /* the filter: CUT, KTR (63: the keys exactly, ~18.7 per 1/16 semitone), the track's ENV / LFO / matrix, DRIFT */
    cut = (gop(t, GO_CUT, 0, 127) << 8) + ((gop(t, GO_KTR, -63, 63) * (m->pitch16 - 60 * 16) * 19) >> 6) + m->cutoff + dcut;
    reso = gop(t, GO_RESO, 0, 127);
    tsvf_coef(&f2c, cut, reso);
    k2 = 8192 - reso * 7600 / 127;
    if (ft == 0u)
        tsvf_coef_k(&f1c, cut, 6000);                   /* LPF24: the first stage without the peak */
    else if (ft == 4u)
        tsvf_coef_k(&f1c, cut, 8192 - reso * 8140 / 127);   /* MS20: up to a ringing, near self-oscillating peak */
    {
        uint32_t ph0 = v->ph[0], ph1 = v->ph[1], pw, sc = s->sc;
        int32_t a1 = s->f1, a2 = s->f2, b1 = s->f3, b2 = s->f4, nst = s->nst, jl = s->jl, tx = s->tx, tg = s->tg;
        int32_t sx1 = s->sx1, sy1 = s->sy1;
        int32_t xm = ctrl1 * 380;                       /* SINE: the cross modulation's depth */
        if (m->pitch16 > 60 * 16)                       /* (less high up: in tune, without its aliasing) */
            xm = xm * 192 / (192 + m->pitch16 - 60 * 16);
        int32_t tfold = 32768 + ctrl1 * 768;            /* TRI: the fold's gain, Q15 (1 .. 4) */
        int32_t oct = ctrl1 * 129;                      /* SAW: the octave-up saw's share */
        int32_t mrph = ctrl1 * 258;                     /* MORPH: the square's share, Q15 */
        pw = 0x80000000u + (uint32_t)ctrl1 * 0xF00000u; /* PULSE: 50 % .. ~6 % */
        for (i = 0; i < n; i++) {
            int32_t o1, o2, sx, y;
            if (sync && w2 == 0u) {                     /* OSC 2 synced: its restart rounded as ANALOG's */
                int32_t bl = blep(ph1, inc2);
                if (jl) {
                    bl = (bl * ((jl - 1) >> 1)) >> 15;
                    jl = 0;
                }
                o2 = (int32_t)(ph1 >> 16) - 32768 - bl;
                if (ph0 > 0xFFFFFFFFu - inc1)
                    o2 -= (blep(ph0, inc1) * (int32_t)(ph1 >> 17)) >> 15;
            } else {
                o2 = go_osc2(w2, ph1, inc2);
            }
            switch (w1) {
            case GO_PULSE:
                o1 = osc_pulse(ph0, inc1, pw);
                break;
            case GO_TRI:
                o1 = go_fold_aa((osc_tri(ph0) * (tfold >> 4)) >> 11, &tx, &tg, 15u);
                break;
            case GO_SINE:
                o1 = osc_sine(ph0 + (((uint32_t)o2 * (uint32_t)xm) << 1));
                break;
            case GO_MORPH: {
                int32_t sw = osc_saw(ph0, inc1);
                o1 = sw + mulq15((osc_pulse(ph0, inc1, 0x80000000u) << 1) - sw, mrph);
                break;
            }
            default:
                o1 = osc_saw(ph0, inc1);
                if (oct)
                    o1 = mulq15(o1, 32767 - (oct >> 1)) + mulq15(osc_saw(ph0 << 1, inc1 << 1), oct >> 1);
                break;
            }
            if (ring)
                o2 = mulq15(o1, o2);
            sx = mulq15(o1, x1) + mulq15(o2, x2);
            if (sync) {                                 /* SYNC: a restarted saw is off centre, its DC out (a pole */
                sy1 = sx - sx1 + sy1 - (sy1 >> 9);      /* at ~14 Hz) */
                sx1 = sx;
                sx = sy1;
            }
            if (xs) {                                   /* SUB: OSC 1's phase an octave or two down */
                uint32_t sp = sc << (32u - so) | ph0 >> so, si = inc1 >> so;
                int32_t u = st < 2u ? osc_pulse(sp, si, 0x80000000u) : st < 4u ? osc_sine(sp)
                          : st < 6u ? osc_saw(sp, si) : go_mix(sp, si);
                sx += mulq15(u, xs);
            }
            ph0 += inc1;
            ph1 += inc2;
            sc += ph0 < inc1;
            if (sync && ph0 < inc1) {                   /* OSC 1 wrapped: OSC 2 restarts with it */
                uint32_t f = ph0 / ((inc1 >> 16) | 1u);
                uint32_t at = ph1 - f * (inc2 >> 16);
                jl = (int32_t)(at >> 16) + 1;
                ph1 = f * (inc2 >> 16);
            }
            if (xn)
                sx += mulq15((int32_t)(noise32(&nst) >> 17) - 16384, xn);
            sx = (sx * 3) >> 3;                         /* one oscillator ~12 k into the filter, all three ~32 k */
            switch (ft) {
            case 0:
                y = tsvf_lp(&f1c, sx, &a1, &a2);
                y = tsvf_lp(&f2c, y, &b1, &b2);
                break;
            case 2:
                y = analog_tap(&f2c, k2, sx, &b1, &b2, 1u);
                break;
            case 3:
                y = analog_tap(&f2c, k2, sx, &b1, &b2, 2u);
                break;
            case 4: {                                   /* MS20: driven in, the resonating state saturated */
                int32_t in = softclip(clamp(sx * 2, -65535, 65535)) >> 1, v1, v2, v3 = in - b2;
                v1 = (f1c.a1 * b1 + f1c.a2 * v3) >> 13;
                v2 = b2 + ((f1c.a2 * b1 + f1c.a3 * v3) >> 13);
                b1 = softclip(clamp((2 * v1 - b1) >> 1, -65535, 65535)) << 1;
                b2 = clamp(2 * v2 - b2, -150000, 150000);
                y = clamp(v2, -65535, 65535);
                break;
            }
            default:
                y = tsvf_lp(&f2c, sx, &b1, &b2);
                break;
            }
            y = soft_knee(y, 16000) << 1;
            out[i] += voice_amp(clamp(y, -65535, 65535), m, i) << 1;
        }
        v->ph[0] = ph0;
        v->ph[1] = ph1;
        s->sc = (uint8_t)sc;
        s->f1 = a1;
        s->f2 = a2;
        s->f3 = b1;
        s->f4 = b2;
        s->nst = nst;
        s->jl = jl;
        s->tx = tx;
        s->tg = tg;
        s->sx1 = sx1;
        s->sy1 = sy1;
    }
}

/* Golana's sounds for a PAD channel (golana_factory.c), its pads and leads, and four new ones. Golana's filter EG
 * becomes the track's ENV on FLT (fenv, twice Golana's INT: the same depth), its LFOs the track's LFO (GO_EXTRA),
 * its MFX the chorus send. {WAVE CTRL1 WAVE2 MOD SEMI TUNE OSC2 SUB}, {A D S R}, fenv, mono (1: LEGATO) */
static const preset_t GOLANA_PRESETS[] = {
    /* pads */
    {"SOFT PAD", {GO_TRI, 12, 0, 0, 0, 10, 60, 0}, {92, 100, 120, 105}, 28, 0, FX(0, 60, 20, 80), PAT(5)},
    {"PHASE PAD", {GO_SAW, 0, 0, 0, 0, 7, 100, 0}, {88, 100, 118, 102}, 16, 0, FX(0, 70, 20, 75), PAT(5)},
    {"SYNC PAD", {GO_SAW, 0, 0, GM_SYNC, 5, 0, 127, 0}, {45, 90, 105, 85}, 0, 0, FX(0, 60, 20, 70), PAT(5)},
    {"OLD STRINGS", {GO_SAW, 0, 0, 0, 12, 4, 80, 0}, {86, 100, 120, 100}, 0, 0, FX(0, 80, 20, 80), PAT(5)},
    {"AGED PAD", {GO_SAW, 0, 0, 0, 0, 6, 100, 40}, {98, 100, 122, 108}, 28, 0, FX(0, 70, 20, 90), PAT(5)},
    {"DARK UNISON", {GO_SAW, 0, 0, 0, -12, 0, 70, 0}, {95, 100, 124, 105}, 24, 0, FX(0, 70, 20, 85), PAT(5)},
    {"POLY CHORUS", {GO_PULSE, 30, 0, 0, 0, 0, 0, 70}, {8, 90, 110, 80}, 44, 0, FX(0, 80, 20, 60), PAT(5)},
    {"WORN TAPE", {GO_TRI, 18, 0, 0, 12, 0, 22, 45}, {4, 104, 70, 92}, 32, 0, FX(0, 50, 35, 70), PAT(5)},
    {"80S BRASS", {GO_SAW, 0, 0, 0, 0, 9, 110, 0}, {50, 90, 112, 65}, 63, 0, FX(0, 50, 15, 50), PAT(6)},
    {"GLASS PAD", {GO_SINE, 24, 2, 0, 12, 3, 30, 0}, {90, 100, 120, 108}, 0, 0, FX(0, 60, 30, 90), PAT(5)},   /* new */
    /* leads */
    {"GLIDE LEAD", {GO_PULSE, 35, 0, 0, 7, 6, 70, 0}, {10, 70, 120, 55}, 36, 1, FX(0, 20, 50, 40), PAT(4), GLD(55)},
    {"WARM LEAD", {GO_TRI, 22, 2, 0, 12, 0, 35, 0}, {30, 70, 120, 70}, 0, 1, FX(0, 20, 35, 60), PAT(4), GLD(40)},
    {"DRIFT LEAD", {GO_SAW, 0, 1, 0, 7, 0, 55, 0}, {6, 85, 122, 60}, 40, 1, FX(0, 20, 45, 45), PAT(4), GLD(45)},
    {"SUPERSAW", {GO_SAW, 0, 0, 0, 0, 11, 120, 35}, {4, 90, 124, 72}, 16, 0, FX(0, 70, 40, 60), PAT(4), GLD(18)},
    {"MORPH LEAD", {GO_MORPH, 40, 2, 0, -12, 0, 30, 0}, {20, 80, 115, 70}, 20, 1, FX(0, 30, 45, 60), PAT(4), GLD(40)},   /* new */
    {"BREATH LEAD", {GO_TRI, 10, 2, 0, 12, 5, 25, 0}, {35, 80, 110, 75}, 12, 1, FX(0, 30, 45, 70), PAT(4), GLD(35)},   /* new */
};
/* each sound's FILTER and TONE pages, and what Golana's LFOs and voice did there on the track: {TYPE CUT RESO KTR OSC1
 * SUBTYP NOISE DRIFT}, VOICE (0 as the preset's mono says, else V_* + 1), DETUNE, LFO WAVE (0 SIN, 1 TRI) RATE, its SHP
 * (Golana's CTRL2),
 * PIT and FLT depths, SPREAD (+ 1, 0: the default) */
typedef struct {
    int8_t f[8];
    uint8_t voice, detune, lwave, lrate;
    int8_t lshp, lpit, lflt;
    uint8_t sprd;
} go_extra_t;
static const go_extra_t GO_EXTRA[] = {
    {{0, 58, 15, 25, 127, 0, 0, 20}, 0, 0, 1, 30, 0, 0, 16, 0},            /* SOFT PAD: LFO 1 TRI on the cutoff */
    {{0, 66, 18, 32, 127, 0, 0, 25}, 0, 0, 1, 20, 0, 0, 0, 0},             /* PHASE PAD */
    {{1, 100, 15, 32, 30, 0, 0, 10}, 0, 0, 1, 25, 0, 0, 0, 0},             /* SYNC PAD */
    {{1, 74, 8, 30, 127, 0, 0, 70}, 0, 0, 1, 20, 0, 0, 0, 0},              /* OLD STRINGS */
    {{0, 50, 20, 32, 127, 3, 18, 127}, 0, 0, 1, 20, 0, 0, 0, 0},           /* AGED PAD */
    {{0, 34, 22, 32, 127, 0, 0, 20}, V_UNISON + 1, 55, 1, 20, 0, 0, 0, 0}, /* DARK UNISON */
    {{0, 64, 15, 30, 127, 0, 0, 25}, 0, 0, 1, 42, 35, 0, 0, 0},            /* POLY CHORUS: PWM */
    {{1, 66, 10, 32, 127, 2, 0, 110}, 0, 0, 0, 32, 0, 3, 0, 0},            /* WORN TAPE: a slow wow */
    {{0, 48, 12, 40, 127, 0, 0, 15}, 0, 0, 1, 20, 0, 0, 0, 0},             /* 80S BRASS */
    {{1, 96, 10, 40, 127, 0, 0, 30}, 0, 0, 1, 18, 20, 0, 0, 0},            /* GLASS PAD: a slow shimmer of the cross mod */
    {{1, 82, 30, 32, 127, 0, 0, 10}, 0, 0, 1, 45, 40, 2, 0, 0},            /* GLIDE LEAD: PWM, a vibrato */
    {{1, 76, 12, 32, 127, 0, 0, 15}, 0, 0, 0, 88, 0, 2, 0, 0},             /* WARM LEAD: a vibrato */
    {{0, 76, 35, 32, 127, 0, 0, 55}, 0, 0, 1, 20, 0, 0, 0, 0},             /* DRIFT LEAD */
    {{1, 108, 10, 32, 127, 4, 0, 20}, V_UNISON + 1, 88, 1, 20, 0, 0, 0, 128},   /* SUPERSAW: spread wide */
    {{0, 80, 25, 32, 127, 0, 0, 25}, 0, 0, 1, 35, 30, 0, 0, 0},            /* MORPH LEAD: the LFO from saw to square */
    {{1, 84, 10, 40, 127, 0, 14, 35}, 0, 0, 0, 80, 0, 1, 0, 0},            /* BREATH LEAD: noise, a light vibrato */
};
_Static_assert(NELEM(GO_EXTRA) == NELEM(GOLANA_PRESETS), "a page of extras a sound");

/* the FILTER and TONE values and the rest of GOLANA sound k on track t (apply_preset_to, after the preset's own); k
 * past the sounds: the pages' defaults (CLEAN INIT) */
static void go_extra_load(track_t *t, uint32_t k)
{
    uint32_t i;
    const go_extra_t *x = k < NELEM(GO_EXTRA) ? &GO_EXTRA[k] : 0;
    for (i = 0; i < 8u; i++)
        t->p[GO_FTYPE + i] = x ? x->f[i] : GO_P[i].def;
    if (!x)
        return;
    if (x->voice)
        t->p[P_VOICE] = (int16_t)(x->voice - 1u);
    if (x->detune)
        t->p[P_DETUNE] = x->detune;
    t->p[P_LWAVE] = x->lwave;
    t->p[P_LRATE] = x->lrate;
    t->p[P_LD_SHP] = x->lshp;
    t->p[P_LD_PIT] = x->lpit;
    t->p[P_LD_FLT] = x->lflt;
    if (x->lshp || x->lpit || x->lflt)
        t->p[P_LTRIG] = 1;                              /* FREE: one LFO for every voice, as Golana's KEY OFF */
    if (x->sprd)
        t->p[P_SPRD] = (int16_t)(x->sprd - 1u);
}

static const engine_t ENG_GOLANA = {
    .name = "GOLANA",
    .page_title = {"OSC", "OSC 2"},
    .edit = {
        {"WAVE", F_ENUM, 0, 4, 0, N_GO1WAVE, 0},
        {"CTRL1", F_INT, 0, 127, 0, 0, 0},
        {"WAVE2", F_ENUM, 0, 3, 0, N_GO2WAVE, 0},
        {"MOD", F_ENUM, 0, 3, 0, N_GO2MOD, 0},
        {"SEMI", F_SEMI, -24, 24, 0, 0, 0},
        {"TUNE", F_INT, -63, 63, 0, 0, "ct"},
        {"OSC2", F_PCT, 0, 127, 0, 0, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = GOLANA_PRESETS,
    .npresets = NELEM(GOLANA_PRESETS),
    .note_on = go_note_on,
    .render = go_render,
    .knob = {GO_CUT, GO_RESO, P_E1, P_E6},           /* (HOME on PAD: CUT, then ATK REL REV: ui.c home_id) */
    .poly = 4,
    .mem = sizeof(gop_t),
};
