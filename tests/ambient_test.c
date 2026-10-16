/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* AMBI (src/fx.c: REVERB TYPE 3), the long-tail reverb that shares HALL's lines, on the host through hostsim.c.
 *   build/host/ambient_test
 * 1. RT60 (Schroeder integral, -5 .. -35 dB, x2) of the decay after a noise burst at SIZE 0, 64 and 127 (DAMP 0): it rises
 *    with SIZE, and is well above HALL's 9 s at the top.
 * 2. the corners: SIZE 127 with DAMP 0 and 127, 10 s of full-scale noise or square waves, then silence: the
 *    output stays bounded (no line clips) and the tail ends in exact silence (no limit cycle, no offset).
 * 3. level: on a held noise, AMBI's steady level at SIZE 127 within 6 dB of HALL's at SIZE 127.
 * 4. a model change HALL <-> AMBI while the bus rings: the new one starts from silence. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void check(const char *what, int ok)
{
    printf("ambient: %-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static uint32_t xs = 0x1234567u;
static int32_t noise(int32_t amp)
{
    xs ^= xs << 13;
    xs ^= xs >> 17;
    xs ^= xs << 5;
    return (int32_t)(((int64_t)(int32_t)xs * amp) >> 31);
}

static void reset(int32_t type, int32_t size, int32_t damp)
{
    host_tracks_init();
    song.g[G_RTYPE] = (int16_t)type;
    song.g[G_RSIZE] = (int16_t)size;
    song.g[G_RDAMP] = (int16_t)damp;
    rev_clear();
    fx.rtype = (uint8_t)type;
}
/* one block of the model through rev_hall: send in, mid out (the side is not looked at here) */
static void block(const int32_t *in, int32_t *out)
{
    uint32_t i;
    for (i = 0; i < CTL; i++)
        out[i] = 0;
    rev_hall(in, out, CTL);
}

#define WIN 4410u                                       /* an energy window: 0.1 s */
#define MAXWIN 12000u                                   /* up to 1200 s */
static double en[MAXWIN];
/* the energy per window of the response to a 0.3 s noise burst (an impulse is too small for the 16-bit lines:
 * the tail would sit in the rounding), for up to secs seconds: returns the windows used */
static uint32_t impulse(double secs, int32_t *peak)
{
    static int32_t in[CTL], out[CTL];
    uint32_t nb = (uint32_t)(secs * FS) / CTL, b, i, w = 0;
    int32_t pk = 0;
    double acc = 0;
    for (b = 0; b < MAXWIN; b++)
        en[b] = 0;
    for (b = 0; b < nb; b++) {
        for (i = 0; i < CTL; i++)
            in[i] = b * CTL < FS * 3u / 10u ? noise(1 << 19) : 0;   /* 0.3 s of full-scale noise, then silence */
        block(in, out);
        for (i = 0; i < CTL; i++) {
            int32_t v = abs(out[i]);
            pk = v > pk ? v : pk;
            acc += (double)out[i] * out[i];
            if (++w == WIN) {
                en[(b * CTL + i) / WIN] = acc;
                acc = 0;
                w = 0;
            }
        }
    }
    *peak = pk;
    return (uint32_t)(nb * CTL / WIN);
}
static double rt60_of(uint32_t nw)                       /* Schroeder over the windows after the burst: -5 .. -35 dB, x2 */
{
    uint32_t i0 = 3u;                                   /* (the burst's 0.3 s) */
    static double rem[MAXWIN];
    double s = 0, t5 = -1, t35 = -1;
    uint32_t i;
    for (i = nw; i-- > 0;) {
        s += en[i];
        rem[i] = s;
    }
    for (i = i0; i < nw; i++) {
        double db = 10 * log10(rem[i] / rem[i0] + 1e-30);
        if (t5 < 0 && db <= -5)
            t5 = (double)i * WIN / FS;
        if (t35 < 0 && db <= -35) {
            t35 = (double)i * WIN / FS;
            break;
        }
    }
    return t35 < 0 ? 9999 : (t35 - t5) * 2.0;
}

static void test_rt60(void)
{
    static const int32_t SZ[3] = {0, 64, 127};
    double r[3], hall127;
    char what[200];
    int32_t pk;
    uint32_t k, nw;
    for (k = 0; k < 3u; k++) {
        reset(3, SZ[k], 0);
        nw = impulse(k == 2u ? 900 : 300, &pk);
        r[k] = rt60_of(nw);
    }
    reset(2, 127, 0);
    nw = impulse(60, &pk);
    hall127 = rt60_of(nw);
    snprintf(what, sizeof what, "AMBI RT60 (DAMP 0): %.1f s (SIZE 0), %.1f (64), %.1f (127); HALL at 127: %.1f s", r[0], r[1], r[2], hall127);
    check(what, r[0] < r[1] && r[1] < r[2] && r[2] > 3.0 * hall127 && r[0] > 4.0);
}

/* full-scale sends for 10 s, then silence until the output is exactly 0 for 20 s; returns the seconds of tail
 * (or -1: never within limit) and the peak out */
static double burst_run(int kind, int32_t size, int32_t damp, int32_t *peak)
{
    static int32_t in[CTL], out[CTL];
    uint32_t b, i, zero = 0, n = 0, limit = 1500u * FS / CTL;
    int32_t pk = 0;
    reset(3, size, damp);
    for (b = 0; b < limit; b++) {
        for (i = 0; i < CTL; i++, n++)
            in[i] = b < 10u * FS / CTL ? (kind == 0 ? noise(1 << 19) : (n / (kind == 1 ? 7u : 53u)) & 1u ? 1 << 19 : -(1 << 19)) : 0;
        block(in, out);
        for (i = 0; i < CTL; i++) {
            int32_t v = abs(out[i]);
            pk = v > pk ? v : pk;
            zero = out[i] ? 0 : zero + 1;
        }
        if (b > 10u * FS / CTL && zero > 20u * FS) {
            *peak = pk;
            return (double)(b * CTL) / FS - 20.0 - 10.0;
        }
    }
    *peak = pk;
    return -1;
}
static void test_corners(void)
{
    char what[200];
    uint32_t kind, d;
    for (d = 0; d < 2u; d++)
        for (kind = 0; kind < 3u; kind++) {
            int32_t pk;
            double t = burst_run((int)kind, 127, d ? 127 : 0, &pk);
            snprintf(what, sizeof what, "AMBI SIZE 127 DAMP %d, %s: peak %d, silent %.0f s after the burst", d ? 127 : 0,
                     kind == 0 ? "noise" : kind == 1 ? "square /7" : "square /53", (int)pk, t);
            check(what, t >= 0 && pk < (1 << 20));
        }
}

static double held_rms(int32_t type)
{
    static int32_t in[CTL], out[CTL];
    uint32_t b, i;
    double s = 0;
    uint32_t cnt = 0;
    reset(type, 127, 60);
    for (b = 0; b < 240u * FS / CTL; b++) {
        for (i = 0; i < CTL; i++)
            in[i] = noise(1 << 18);                     /* a loud, held send */
        block(in, out);
        if (b > 200u * FS / CTL)
            for (i = 0; i < CTL; i++, cnt++)
                s += (double)out[i] * out[i];
    }
    return sqrt(s / cnt);
}
static void test_level(void)
{
    double a = held_rms(3), h = held_rms(2);
    char what[200];
    snprintf(what, sizeof what, "held noise at SIZE 127 (DAMP 60): AMBI RMS %.0f, HALL %.0f (%.1f dB)", a, h, 20 * log10(a / h));
    check(what, a < 2.0 * h && a > 0.5 * h);
}

static void test_switch(void)
{
    static int32_t in[CTL], out[CTL];
    uint32_t b, i;
    int32_t pk = 0;
    reset(2, 100, 60);
    for (b = 0; b < 3u * FS / CTL; b++) {
        for (i = 0; i < CTL; i++)
            in[i] = noise(1 << 18);
        block(in, out);
    }
    rev_clear();
    fx.rtype = 3;                                       /* (as rev_switch) */
    for (i = 0; i < CTL; i++)
        in[i] = 0;
    for (b = 0; b < FS / CTL; b++) {
        block(in, out);
        for (i = 0; i < CTL; i++)
            pk = abs(out[i]) > pk ? abs(out[i]) : pk;
    }
    check("HALL -> AMBI: the new model starts from silence (nothing in the first second)", pk == 0);
}

int main(void)
{
    test_rt60();
    test_corners();
    test_level();
    test_switch();
    printf(bad ? "ambient: %d FAILED\n" : "ambient: all ok\n", bad);
    return bad != 0;
}
