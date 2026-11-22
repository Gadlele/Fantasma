/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Shared DSP building blocks for the Felucca engines (all fixed point).
 * Voice output convention: add sample * amp to out[], where a full-scale
 * oscillator at amp = 1.0 (Q15 32767) contributes VOICE_FS. */
#define VOICE_FS 24000           /* per-voice level: one voice peaks near -6 dBFS before the master */

/* ---------------------------------------------------------- the rate --- */
/* FS is a build-time choice: the tables header (tools/gen_engine_tables.py --fs RATE) defines it, 44100 by
 * default. The engines were designed at 44.1 kHz; what they time in samples or blocks is moved to FS here,
 * at compile time (no cost a sample). At 44100 every macro below is the constant it replaced: the output
 * is bit for bit what it always was. */
#if !defined(FS) || !defined(CTL)
#error "dsp.c: the tables header (FS, CTL, ...) comes first"
#endif
#if FS < 32000 || FS > 192000
#error "dsp.c: FS is supported from 32000 to 192000"
#endif
#if FS != 44100 && !defined(FE_RATE_CONSTS)
#error "dsp.c: FS is not 44100: make the tables header with tools/gen_engine_tables.py --fs FS (the rate constants)"
#endif
/* FE_OCT: octaves from 44.1 kHz to FS, rounded (0 at 32 .. 62.3 kHz, 1 to 124.7 kHz, 2 above): a one-pole
 * or a block-rate smoother written as a shift (x += (y - x) >> s) keeps its corner with s + FE_OCT */
#if FS < 62367
#define FE_OCT 0
#elif FS < 124734
#define FE_OCT 1
#else
#define FE_OCT 2
#endif
#if FS == 44100
#define FE_T(n) (n)              /* a count of samples (or blocks) at 44.1 kHz -> the same time at FS */
#define FE_HOLD(n) (n)           /* the same for a sample hold, 1 (none) stays 1 */
#else
#define FE_RATE_Q16 ((uint32_t)((65536ull * FS + 22050u) / 44100u))   /* FS / 44100, Q16 */
#define FE_T(n) ((int32_t)(((uint32_t)(n) * FE_RATE_Q16 + 32768u) >> 16))
#define FE_HOLD(n) ((n) <= 1 ? (n) : FE_T(n))
#endif
/* the per-sample / per-block constants that need more than a shift: their 44.1 kHz values (the generator
 * writes them for any other rate; `gen_engine_tables.py --check src/dsp.c` checks these against it) */
#ifndef FE_RATE_CONSTS
#define FE_NOISE_PINK0_P 32690   /* NOISE: the pink poles (Kellet), Q15, and their input gains */
#define FE_NOISE_PINK0_G 3245
#define FE_NOISE_PINK1_P 31555
#define FE_NOISE_PINK1_G 9716
#define FE_NOISE_PINK2_P 18678
#define FE_NOISE_PINK2_G 17247
#define FE_NOISE_BROWN_P 32580   /* the brown pole, its gain */
#define FE_NOISE_BROWN_G 3480
#define FE_WHEEL_PDEC_SLOW 65457 /* WHEEL percussion decay a block, Q16: tau 0.6 s */
#define FE_WHEEL_PDEC_FAST 65219 /* tau 0.15 s */
#define FE_WHEEL_CLKDEC 24576    /* WHEEL key click decay a block, Q15: tau ~2.5 ms */
#define fe_pole(a) (a)           /* a one-pole coefficient (Q15) designed at 44.1 kHz, at FS */
#else
static inline int32_t fe_pole(int32_t a)                /* a: 0..32767 */
{
    int32_t i = a >> 8;
    return FE_POLE[i] + (((FE_POLE[i + 1] - FE_POLE[i]) * (a & 255)) >> 8);
}
#endif

static inline int32_t mulq15(int32_t a, int32_t b) { return (a * b) >> 15; }
/* a * k / 65536 without 64-bit: a up to 2^31, k Q16 */
static inline int32_t mulq16(int32_t a, uint32_t k)
{
    return (int32_t)(((a >> 16) * (int32_t)k) + (int32_t)(((uint32_t)(a & 0xFFFF) * k) >> 16));
}
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
/* a * b and x << n wrapped to 32 bits in unsigned arithmetic (defined in C; a signed overflow or a left shift
 * of a negative value is not), back to int32_t modulo 2^32 (GCC, Clang): the bits a two's-complement multiply
 * or shift gives */
static inline int32_t mul_wrap(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }
static inline int32_t shl(int32_t x, uint32_t n) { return (int32_t)((uint32_t)x << n); }

/* sine, linearly interpolated between the 1024 table points (plain lookup: THD -55 dB) */
static inline int32_t sine_i(uint32_t ph)
{
    uint32_t i = ph >> 22;
    int32_t a = SINE[i], b = SINE[(i + 1u) & 1023u];
    return a + (((b - a) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}
static inline int32_t osc_sine(uint32_t ph) { return sine_i(ph); }

/* polyBLEP residual (Q15) around a wrap of a phase accumulator */
static inline int32_t blep(uint32_t ph, uint32_t inc)
{
    uint32_t d = inc >> 15;
    int32_t x;
    if (!d)
        return 0;
    if (ph < inc) {
        x = (int32_t)(ph / d);                       /* 0..32767 */
        return x + x - ((x * x) >> 15) - 32768;
    }
    if (ph > 0xFFFFFFFFu - inc) {
        x = -(int32_t)((0xFFFFFFFFu - ph) / d);       /* -32767..0 */
        return ((x * x) >> 15) + x + x + 32768;
    }
    return 0;
}

static inline int32_t osc_saw(uint32_t ph, uint32_t inc)
{
    return ((int32_t)(ph >> 16) - 32768) - blep(ph, inc);
}

static inline int32_t osc_pulse(uint32_t ph, uint32_t inc, uint32_t pw)
{
    return (osc_saw(ph, inc) - osc_saw(ph + pw, inc)) >> 1;
}

static inline int32_t osc_tri(uint32_t ph)
{
    int32_t s = (int32_t)(ph >> 15);                 /* 0..131071 */
    return s < 65536 ? s - 32768 : 98303 - s;
}

/* tanh(x / 32768) * 32767, linearly interpolated (a plain lookup gives
 * 256-unit steps, audible on quiet signals) */
static inline int32_t softclip(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = a >> 8, y;
    if (i >= 256)
        y = TANH_Q15[256];                           /* continuous with the table */
    else
        y = TANH_Q15[i] + (((TANH_Q15[i + 1] - TANH_Q15[i]) * (a & 255)) >> 8);
    return x < 0 ? -y : y;
}

/* xorshift32 */
static inline uint32_t noise32(int32_t *st)
{
    uint32_t s = (uint32_t)*st;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *st = (int32_t)s;
    return s;
}

/* Trapezoidal SVF (A. Simper), unconditionally stable. Coefficients per
 * block in Q13; signals stay within +-150000 so products fit in 32 bits. */
typedef struct { int32_t a1, a2, a3; } tsvf_t;
static inline void tsvf_coef_k(tsvf_t *c, int32_t cut, int32_t k)   /* cut: 0..127 << 8, k: damping Q12 */
{
    int32_t i, g, den;
    cut = clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    g = SVF_G[i];
    if (i < 127)                                       /* between table points: sweeps without 128 steps */
        g += ((SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    den = 4096 + ((g * (g + k)) >> 12);
    c->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    c->a2 = (c->a1 * g) >> 12;
    c->a3 = (c->a2 * g) >> 12;
}
static inline void tsvf_coef(tsvf_t *c, int32_t cut, int32_t reso)   /* reso 0..127: damping 2.0 .. ~0.15 */
{
    tsvf_coef_k(c, cut, 8192 - reso * 7600 / 127);
}

static inline int32_t tsvf_lp(const tsvf_t *c, int32_t in, int32_t *ic1, int32_t *ic2)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    return v2;
}

/* amplitude ramp over the block. Blocks are always CTL long, so x / CTL is a
 * shift (rounded towards zero, as a division); the compiler at -Os keeps
 * even constant divisions as a hardware divide, once per sample and voice */
#define CTL_LOG2 5
#if (1 << CTL_LOG2) != CTL
#error "CTL_LOG2 does not match CTL"
#endif
static inline int32_t amp_at(const vmod_t *m, uint32_t i)
{
    int32_t x = (m->amp1 - m->amp0) * (int32_t)i;
    return m->amp0 + ((x + ((x >> 31) & (CTL - 1))) >> CTL_LOG2);
}

/* a voice's sample s (Q15) at the amplitude ramp's sample i, scaled to the voice level (VOICE_FS);
 * engines whose signal peaks near half scale add it << 1 */
static inline int32_t voice_amp(int32_t s, const vmod_t *m, uint32_t i)
{
    return mulq15(mulq15(s, amp_at(m, i)), VOICE_FS);
}

/* linear up to k, then a soft knee: only the peaks above k saturate (|y| - k must stay < 2^30) */
static inline int32_t soft_knee(int32_t y, int32_t k)
{
    int32_t a = y < 0 ? -y : y;
    if (a <= k)
        return y;
    a = k + (softclip((a - k) * 2) >> 1);
    return y < 0 ? -a : a;
}

/* phase increment of pitch16 (1/16 semitone) plus ct cents: whole 1/16 semitones from the table,
 * the rest (and fine, in 1/4096) as a factor (1 ct = 2.367 / 4096) */
static inline uint32_t cents_inc(int32_t pitch16, int32_t ct, int32_t fine)
{
    int32_t d16 = ct * 16 / 100, rem = ct * 16 - d16 * 100;          /* rem: 1/1600 semitone */
    uint32_t inc = pitch_inc(clamp(pitch16 + d16, 0, 2047));
    return inc + (uint32_t)((int32_t)(inc >> 12) * (rem * 2367 / 16000 + fine));
}
