/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca-Engines at another sample rate (tests/run.sh builds it with the tables of tools/gen_engine_tables.py
 * --fs 48000 and 96000; it also passes at 44100). Checks, at FS:
 *   - pitch: a held note of ANALOG (sine), LOFI (triangle), TRIO (triangles) and WHEEL (the 8' bar alone) at
 *     220, 440 and 880 Hz, measured by its zero crossings over a second: within 0.3 %
 *   - the filter: dsp.c's SVF low-pass at RES 0 (damping 2: -6 dB at the cutoff) at cutoffs 40, 64, 90, 110,
 *     fed a sine at fc / 4, fc and 4 fc: the gains of the cutoff the parameter names (cutoff_hz())
 *   - LOFI's TONE pole moved to FS (dsp.c fe_pole): the same corner in Hz as at 44.1 kHz, within 2 %
 * Prints what it measured; exits 1 on a miss. Host-side float only (the engines have none). */
#include <math.h>
#include <stdlib.h>
#define main engines_host_main
#include "fe_host.c"
#undef main
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int fails;

/* a held note of engine e with parameters e0..e7, n samples into buf (from fe_host's voice and envelope) */
static void hold_note(const engine_t *e, const int8_t *ep, uint32_t note, int32_t *buf, uint32_t n)
{
    track_t *t = &trk[0];
    int32_t last = 0;
    uint32_t done = 0, k;
    memset(t, 0, sizeof *t);
    memset(drw_t, 0, sizeof drw_t);
    memset(drw_v, 0, sizeof drw_v);
    vage = 0;
    for (k = 0; k < 8u; k++)
        t->p[P_E0 + k] = ep[k];
    voice_on(t, e, &t->v[0], note, 0);
    while (done < n) {
        voice_t *v = &t->v[0];
        int32_t out[CTL] = {0}, a;
        vmod_t m;
        uint32_t base, i;
        if (e->block)
            e->block(t);
        a = env_tick(v);
        if (e->amp)
            a = e->amp(t, v, a);
        base = pitch_inc((uint32_t)clamp(v->pitch16, 0, 2047));
        memset(&m, 0, sizeof m);
        m.pitch16 = v->pitch16;
        m.inc = base;
        m.amp0 = last;
        m.amp1 = a;
        m.envq15 = a;
        m.shape = 64 << 8;
        last = a;
        e->render(t, v, out, CTL, &m);
        for (i = 0; i < CTL && done < n; i++)
            buf[done++] = out[i];
    }
}

/* frequency from the rising zero crossings of buf[from..n) (minus its mean), linearly interpolated */
static double zc_freq(const int32_t *buf, uint32_t from, uint32_t n)
{
    double mean = 0, t0 = -1, t1 = -1;
    uint32_t i, c = 0;
    for (i = from; i < n; i++)
        mean += buf[i];
    mean /= (double)(n - from);
    for (i = from + 1; i < n; i++) {
        double a = buf[i - 1] - mean, b = buf[i] - mean;
        if (a < 0 && b >= 0) {
            double t = (i - 1) + a / (a - b);
            if (t0 < 0)
                t0 = t;
            t1 = t;
            c++;
        }
    }
    return c > 1 ? (c - 1) * (double)FS / (t1 - t0) : 0;
}

static void check_pitch(void)
{
    static const struct { const char *name; const engine_t *e; int8_t p[8]; } V[] = {
        {"ANALOG SIN", &ENG_ANALOG, {3, 0, 0, 0, 127, 0, 0, 0}},
        {"LOFI TRI", &ENG_LOFI, {2, 1, 64, 0, 0, 0, 0, 127}},
        {"TRIO TRI3", &ENG_TRIO, {4, 0, 0, 0, 0, 127, 0, 64}},
        {"WHEEL 8'", &ENG_WHEEL, {0, 0, 0, 0, 0, 0, 0, 0}},
    };
    static const uint32_t notes[] = {57, 69, 81};
    uint32_t n = FS, j, k;
    int32_t *buf = malloc(n * sizeof *buf);
    for (j = 0; j < NELEM(V); j++)
        for (k = 0; k < NELEM(notes); k++) {
            double want = 440.0 * pow(2.0, ((double)notes[k] - 69) / 12), got, err;
            hold_note(V[j].e, V[j].p, notes[k], buf, n);
            got = zc_freq(buf, n / 10, n);
            err = got / want - 1;
            printf("  pitch %-10s note %u: %8.3f Hz (want %8.3f, %+.3f %%)\n", V[j].name, notes[k], got, want,
                   err * 100);
            if (fabs(err) > 0.003) {
                printf("FAIL: pitch off by more than 0.3 %%\n");
                fails++;
            }
        }
    free(buf);
}

/* the gain of dsp.c's low-pass (cutoff index ci, RES 0) for a sine at f Hz */
static double svf_gain(int32_t ci, double f)
{
    tsvf_t c;
    int32_t ic1 = 0, ic2 = 0;
    uint32_t i, n = FS / 2, from = FS / 4;
    double ein = 0, eout = 0;
    tsvf_coef(&c, ci << 8, 0);
    for (i = 0; i < n; i++) {
        double x = 8000 * sin(2 * M_PI * f * i / FS);
        int32_t y = tsvf_lp(&c, (int32_t)lrint(x), &ic1, &ic2);
        if (i >= from) {
            ein += x * x;
            eout += (double)y * y;
        }
    }
    return sqrt(eout / ein);
}

static void check_filter(void)
{
    static const int32_t cuts[] = {40, 64, 90, 110};
    uint32_t j;
    for (j = 0; j < NELEM(cuts); j++) {
        double fc = 30.0 * pow(16000.0 / 30.0, cuts[j] / 127.0), lo, at, hi;
        if (fc > 0.4 * FS)
            continue;
        lo = svf_gain(cuts[j], fc / 4);
        at = svf_gain(cuts[j], fc);
        hi = 4 * fc < 0.45 * FS ? svf_gain(cuts[j], 4 * fc) : 0;
        printf("  filter CUT %3d (%7.1f Hz): gain %.3f at fc / 4, %.3f at fc, %.3f at 4 fc (want 0.94, 0.50, 0.06)\n",
               cuts[j], fc, lo, at, hi);
        if (lo < 0.85 || at < 0.42 || at > 0.58 || hi > 0.12) {
            printf("FAIL: the cutoff is not where CUT %d puts it\n", cuts[j]);
            fails++;
        }
    }
}

static void check_pole(void)
{
    static const int32_t as[] = {3000, 8000, 20000, 30000};
    uint32_t j;
    for (j = 0; j < NELEM(as); j++) {
        double f44 = -44100.0 * log(1 - as[j] / 32768.0) / (2 * M_PI);
        double ffs = -(double)FS * log(1 - fe_pole(as[j]) / 32768.0) / (2 * M_PI);
        printf("  LOFI TONE pole %5d: %7.1f Hz at 44.1 kHz, %7.1f Hz at FS (coefficient %d)\n", as[j], f44, ffs,
               fe_pole(as[j]));
        if (fabs(ffs / f44 - 1) > 0.02) {
            printf("FAIL: the TONE corner moved\n");
            fails++;
        }
    }
}

int main(void)
{
    printf("FS %d:\n", FS);
    check_pitch();
    check_filter();
    check_pole();
    if (fails) {
        printf("FAIL: %d check(s) at FS %d\n", fails, FS);
        return 1;
    }
    printf("ok: pitch, filter and pole at FS %d\n", FS);
    return 0;
}
