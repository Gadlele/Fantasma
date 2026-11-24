/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Gadlele */
/* Fantasma 2.0: Niagara Drift's drones as the DRONE channel's sound. drone.c, drone.h and fastmath.h are Niagara
 * Drift's (Evan Yerburgh, GPL-3.0, github.com/evanyerburgh/niagara-drift, firmware/src/dsp, v0.3.1), here unchanged but
 * for three lines: its effects (drone_fx.c) are left out (niagara_fx.c: the channel's own play instead), its block
 * (BLK) is Fantasma's, 32 frames, so its buffers take no more RAM than a block needs (the same sound: it is called a
 * block at a time either way), and its scope (DR_SCOPE_N, for its screen) is 4 points. This unit is built apart from felucca.c (tools/build.py), with the FPU as Niagara
 * Drift builds it; Fantasma reaches it through niagara.h only. The Presets are Niagara Drift's sixteen
 * (firmware/src/app/project.c, the same changes from the starting sound), its delay and reverb settings left out. */
#ifndef BLK
#define BLK 32u
#endif
#define DR_SCOPE_N 4                             /* (the drones' scope is Niagara Drift's screen's: none here) */
#include "drone.c"
#include "niagara.h"

/* each NK_* value's parameter in drone.c (NK_IIOFS: none, nia_pitch's) */
#define G1(p) DR_GP(0, p)
#define G2(p) DR_GP(1, p)
static const uint8_t NIA_ID[NK_N] = {
    G1(GP_WAVE), G1(GP_WIDTH), G1(GP_MIX), G1(GP_LEVEL),
    G1(GP_INTERVAL), G1(GP_DETUNE), G1(GP_PARTNER), G1(GP_SUBOCT),
    G1(GP_CUTOFF), G1(GP_RESO), G1(GP_MODE), G1(GP_DRIVE),
    G1(GP_NOISE), G1(GP_NCOLOR), P_SUBW_0,
    G2(GP_WAVE), G2(GP_WIDTH), G2(GP_MIX), G2(GP_LEVEL),
    G2(GP_INTERVAL), G2(GP_DETUNE), G2(GP_PARTNER), G2(GP_SUBOCT),
    G2(GP_CUTOFF), G2(GP_RESO), G2(GP_MODE), G2(GP_DRIVE),
    G2(GP_NOISE), G2(GP_NCOLOR), P_SUBW_1,
    P_PLINK_OFF, P_PLINK, P_SYNC, P_XMOD,
    P_XMOD_AMT, P_WIDTH, P_TUNING, P_II_OUT,
    P_DRIFT, P_CHAR, P_AGE, P_FEEDBACK,
    P_WANDER, P_WSCALE,
    P_LFO_RATE, P_LFO_W1, P_LFO_W2, P_MOVE,
    P_LFO_RMOD, P_LFO_RMOD_SRC, P_LFO_LOCK,
    G1(GP_OLFO), G1(GP_OLFO_DEPTH), G1(GP_FLFO), G1(GP_FLFO_DEPTH),
    G2(GP_OLFO), G2(GP_OLFO_DEPTH), G2(GP_FLFO), G2(GP_FLFO_DEPTH),
    G1(GP_OLFO_DEST), G1(GP_FLFO_DEST), G2(GP_OLFO_DEST), G2(GP_FLFO_DEST),
    P_PULSE, P_PULSE_RATE, P_PULSE_DEPTH, P_PULSE_SHAPE,
    P_PULSE_SWING, P_GLIDE, P_MORPH,
};
_Static_assert(DR_NPARAMS <= 255, "an id a byte");

static int32_t nia_ofs = 700, nia_c1 = -1;       /* drone II from drone I (cents); drone I's pitch now */
static int nia_link;

void nia_init(void)
{
    dr_init();
    dr_phones = 1;                               /* (Fantasma's own low cut and speaker EQ: Niagara's left out) */
    dr_set(P_FADE, 0);                           /* (the channel's ENV fades it: Niagara's PLAY fade at its shortest) */
    dr_set(P_DLY_MIX, 0);                        /* (its effects are left out: their mixes at 0 all the same) */
    dr_set(P_REV_MIX, 0);
    dr_set(P_CLK_SYNC, 0);
    dr_set(P_PLOCK, 0);
    dr_play(1);
}

static void nia_pitch_send(void)
{
    if (nia_c1 < -9000)
        return;
    dr_set(G1(GP_PITCH), nia_c1);
    if (!nia_link)                               /* (linked: drone.c takes drone I's plus Link ofs) */
        dr_set(G2(GP_PITCH), nia_c1 + nia_ofs);
}

void nia_set(uint32_t k, int32_t v)
{
    if (k >= NK_N)
        return;
    if (k == NK_IIOFS) {
        nia_ofs = v;
        dr_set(P_PLINK_OFF, v);
        nia_pitch_send();
        return;
    }
    if (k == NK_PLINK) {
        nia_link = v != 0;
        dr_set(P_PLINK, v);
        nia_pitch_send();
        return;
    }
    dr_set(NIA_ID[k], v);
}

void nia_pitch(int32_t c1)
{
    if (c1 == nia_c1)
        return;
    nia_c1 = c1;
    nia_pitch_send();
}

void nia_render(int32_t *lr, uint32_t n)
{
    uint32_t i;
    dr_render(lr, n, 3277u);                     /* (MASTER at 4/5: its own volume, not into its drive; n 0: the
                                                  * commands only) */
    for (i = 0; i < 2u * n; i++)                 /* 24 bits (8.3 M full) -> ~16 */
        lr[i] >>= 8;
}

int32_t nia_get(uint32_t k)                      /* what drone.c has for k (its commands taken) */
{
    return k == NK_IIOFS ? nia_ofs : k == NK_PLINK ? nia_link : base[NIA_ID[k % NK_N]];
}

void nia_range(uint32_t k, int16_t *lo, int16_t *hi, int16_t *def)
{
    const dr_param_t *d;
    if (k == NK_IIOFS) {                         /* (drone II from drone I: Link ofs's range) */
        *lo = -4800;
        *hi = 4800;
        *def = (int16_t)(PARAMS[G2(GP_PITCH)].def - PARAMS[G1(GP_PITCH)].def);
        return;
    }
    d = dr_param_info(NIA_ID[k % NK_N]);
    *lo = d->lo;
    *hi = d->hi;
    *def = d->def;
}

void nia_text(uint32_t k, int32_t v, char *buf)
{
    if (k == NK_IIOFS) {                         /* semitones, and cents when there are some: "+7", "-5.02" */
        int32_t a = v < 0 ? -v : v, s = a / 100, c = a % 100;
        char d[8], *p = buf;
        uint32_t n = 0;
        *p++ = v < 0 ? '-' : '+';
        do d[n++] = (char)('0' + s % 10), s /= 10; while (s);
        while (n)
            *p++ = d[--n];
        if (c) {
            *p++ = '.';
            *p++ = (char)('0' + c / 10);
            *p++ = (char)('0' + c % 10);
        }
        *p = 0;
        return;
    }
    dr_param_text(NIA_ID[k % NK_N], v, buf);
}

/* ---- the Presets: Niagara Drift's sixteen (firmware/src/app/project.c PRESET_CHANGES, v0.3.1), each a list of changes
 * from the starting sound, ended by -1 */
#define I_(p) DR_GP(0, p)
#define II_(p) DR_GP(1, p)
static const int16_t PRESET_CHANGES[][101] = {
    {-1},                                        /* NIAGARA: the starting sound */
    {I_(GP_WAVE), 40, I_(GP_INTERVAL), 13, I_(GP_DETUNE), 10, I_(GP_CUTOFF), 700, I_(GP_RESO), 300,
     I_(GP_OLFO_DEPTH), 71, I_(GP_OLFO_DEST), 1, II_(GP_PITCH), 5386, II_(GP_WAVE), 40, II_(GP_INTERVAL), 13,
     II_(GP_DETUNE), 10, II_(GP_CUTOFF), 700, II_(GP_RESO), 300, II_(GP_LEVEL), 80, II_(GP_OLFO), 1,
     II_(GP_OLFO_DEPTH), -71, II_(GP_OLFO_DEST), 1, P_LFO_RATE, 78, P_PULSE_DEPTH, 30, P_DLY_MIX, 20, P_REV_MIX, 50,
     P_REV_TYPE, 3, P_CHAR, 15, P_DLY_TYPE, 1, P_WSCALE, 1, P_PULSE, 0, -1},
    {I_(GP_INTERVAL), 9, I_(GP_MIX), 10, I_(GP_CUTOFF), 520, I_(GP_RESO), 450, I_(GP_MODE), 1, I_(GP_LEVEL), 100,
     I_(GP_OLFO_DEPTH), 45, I_(GP_OLFO_DEST), 1, I_(GP_FLFO_DEPTH), 18, II_(GP_WAVE), 67, II_(GP_INTERVAL), 13,
     II_(GP_MIX), 0, II_(GP_CUTOFF), 595, II_(GP_RESO), 450, II_(GP_MODE), 1, II_(GP_LEVEL), 100,
     II_(GP_OLFO_DEPTH), 55, II_(GP_OLFO_DEST), 1, II_(GP_FLFO_DEPTH), -18, P_LFO_RATE, 45, P_PULSE_RATE, 20,
     P_PULSE_DEPTH, 30, P_DLY_MIX, 15, P_REV_MIX, 55, P_MOVE, 60, P_REV_LEN, 85, P_REV_DAMP, 20, P_WSCALE, 5, -1},
    {I_(GP_PITCH), 3300, I_(GP_WAVE), 20, I_(GP_INTERVAL), 13, I_(GP_DETUNE), 0, I_(GP_MIX), 0, I_(GP_CUTOFF), 700,
     I_(GP_RESO), 350, I_(GP_LEVEL), 56, I_(GP_FLFO_DEPTH), 70, I_(GP_FLFO_DEST), 1, II_(GP_PITCH), 6669,
     II_(GP_WAVE), 0, II_(GP_INTERVAL), 13, II_(GP_DETUNE), 0, II_(GP_MIX), 0, II_(GP_CUTOFF), 680,
     II_(GP_RESO), 300, II_(GP_LEVEL), 40, II_(GP_FLFO_DEPTH), -60, II_(GP_FLFO_DEST), 1, P_LFO_RATE, 50,
     P_GLIDE, 16, P_PULSE, 0, P_DLY_MIX, 15, P_REV_MIX, 55, P_FEEDBACK, 25, P_WSCALE, 5, P_REV_LEN, 85, P_TUNING, 6,
     -1},
    {I_(GP_INTERVAL), 0, I_(GP_MIX), 45, I_(GP_SUBOCT), 3, I_(GP_CUTOFF), 650, I_(GP_RESO), 450, I_(GP_DRIVE), 70,
     I_(GP_LEVEL), 100, I_(GP_FLFO_DEPTH), 100, I_(GP_FLFO_DEST), 1, I_(GP_NOISE), 8, II_(GP_PITCH), 4700,
     II_(GP_WAVE), 100, II_(GP_WIDTH), 70, II_(GP_DETUNE), 30, II_(GP_CUTOFF), 610, II_(GP_RESO), 450,
     II_(GP_DRIVE), 60, II_(GP_LEVEL), 100, II_(GP_FLFO_DEPTH), -100, II_(GP_FLFO_DEST), 1, P_LFO_RATE, 61,
     P_DLY_TIME, 85, P_DLY_REPEATS, 45, P_DLY_MIX, 15, P_REV_MIX, 40, P_REV_TYPE, 1, P_FEEDBACK, 30, P_CHAR, 27,
     P_DLY_TYPE, 1, P_REV_LEN, 75, P_REV_DAMP, 80, P_PULSE_RATE, 0, P_PULSE_DEPTH, 60, P_PULSE_SHAPE, 0, -1},
    {I_(GP_WAVE), 20, I_(GP_INTERVAL), 1, I_(GP_DETUNE), 35, I_(GP_CUTOFF), 600, II_(GP_PITCH), 6881,
     II_(GP_WAVE), 27, II_(GP_INTERVAL), 8, II_(GP_DETUNE), 8, II_(GP_MIX), 20, II_(GP_CUTOFF), 615,
     II_(GP_RESO), 688, II_(GP_MODE), 1, II_(GP_DRIVE), 26, II_(GP_LEVEL), 62, II_(GP_OLFO_DEPTH), 10,
     II_(GP_NOISE), 10, II_(GP_NCOLOR), 18, P_XMOD, 1, P_LFO_RATE, 73, P_LFO_W1, 6, P_GLIDE, 37, P_PULSE, 0,
     P_DLY_TIME, 111, P_DLY_REPEATS, 68, P_DLY_MIX, 33, P_REV_MIX, 56, P_REV_TYPE, 3, P_LFO_RMOD, 33,
     P_LFO_RMOD_SRC, 1, P_MOVE, 66, P_WANDER, 1, P_WSCALE, 3, P_DLY_TYPE, 0, P_REV_LEN, 66, P_REV_DAMP, 39,
     P_DLY_DRIVE, 7, P_TUNING, 8, -1},
    {I_(GP_PITCH), -1900, I_(GP_MIX), 0, I_(GP_CUTOFF), 900, I_(GP_LEVEL), 64, I_(GP_PARTNER), 0,
     II_(GP_PITCH), 4500, II_(GP_WAVE), 67, II_(GP_CUTOFF), 620, II_(GP_RESO), 850, II_(GP_LEVEL), 88,
     P_LFO_RATE, 94, P_PULSE, 0, P_DLY_TIME, 45, P_DLY_MIX, 40, P_REV_TYPE, 1, P_DLY_TYPE, 0, P_REV_LEN, 55,
     P_DLY_DRIVE, 25, -1},
    {I_(GP_PITCH), 6200, I_(GP_WAVE), 0, I_(GP_INTERVAL), 9, I_(GP_DETUNE), 15, I_(GP_MIX), 0, I_(GP_CUTOFF), 950,
     I_(GP_RESO), 50, I_(GP_LEVEL), 65, I_(GP_OLFO_DEPTH), 65, I_(GP_OLFO_DEST), 1, II_(GP_PITCH), 7800,
     II_(GP_WAVE), 10, II_(GP_INTERVAL), 13, II_(GP_DETUNE), 15, II_(GP_MIX), 0, II_(GP_CUTOFF), 950,
     II_(GP_RESO), 50, II_(GP_LEVEL), 61, II_(GP_OLFO_DEPTH), 75, II_(GP_OLFO_DEST), 1, P_DRIFT, 40, P_PULSE_RATE, 8,
     P_PULSE_DEPTH, 30, P_PULSE_SHAPE, 0, P_DLY_TIME, 80, P_DLY_REPEATS, 60, P_REV_MIX, 65, P_MOVE, 55,
     P_DLY_TYPE, 1, P_REV_LEN, 92, P_REV_DAMP, 20, -1},
    {I_(GP_PITCH), 3800, I_(GP_CUTOFF), 740, I_(GP_RESO), 600, I_(GP_DRIVE), 75, I_(GP_LEVEL), 92,
     I_(GP_FLFO_DEPTH), 100, I_(GP_FLFO_DEST), 1, II_(GP_PITCH), 3900, II_(GP_CUTOFF), 780, II_(GP_RESO), 600,
     II_(GP_MODE), 1, II_(GP_DRIVE), 75, II_(GP_LEVEL), 92, P_LFO_RATE, 100, P_LFO_W1, 8, P_DLY_TIME, 20,
     P_DLY_REPEATS, 45, P_DLY_MIX, 40, P_REV_MIX, 50, P_REV_TYPE, 0, P_MOVE, 40, P_FEEDBACK, 85, P_CHAR, 21,
     P_REV_LEN, 40, P_PULSE, 0, -1},
    {I_(GP_WAVE), 45, I_(GP_WIDTH), 0, I_(GP_MIX), 35, I_(GP_CUTOFF), 600, I_(GP_RESO), 150, I_(GP_DRIVE), 10,
     I_(GP_LEVEL), 87, I_(GP_FLFO_DEPTH), 45, II_(GP_PITCH), 5702, II_(GP_WAVE), 65, II_(GP_DETUNE), 6,
     II_(GP_MIX), 0, II_(GP_CUTOFF), 690, II_(GP_RESO), 300, II_(GP_DRIVE), 25, II_(GP_LEVEL), 77,
     II_(GP_FLFO_DEPTH), -45, P_LFO_RATE, 30, P_PULSE_RATE, 18, P_PULSE_SHAPE, 0, P_DLY_TIME, 145, P_DLY_REPEATS, 55,
     P_DLY_MIX, 28, P_REV_MIX, 60, P_REV_TYPE, 4, P_DLY_LFO_DEPTH, 22, P_MOVE, 40, P_REV_LEN, 99, P_REV_DAMP, 35,
     P_SUBW_0, 1, -1},
    {I_(GP_PITCH), 3800, I_(GP_CUTOFF), 620, I_(GP_RESO), 600, I_(GP_DRIVE), 40, I_(GP_LEVEL), 86, I_(GP_PARTNER), 0,
     I_(GP_OLFO_DEPTH), 29, I_(GP_FLFO_DEPTH), 0, II_(GP_CUTOFF), 640, II_(GP_RESO), 400, II_(GP_LEVEL), 74,
     II_(GP_FLFO_DEPTH), 0, P_XMOD, 1, P_XMOD_AMT, 45, P_LFO_RATE, 112, P_LFO_W1, 9, P_LFO_W2, 0, P_PULSE_RATE, 10,
     P_PULSE_DEPTH, 45, P_PULSE_SHAPE, 0, P_DLY_TIME, 55, P_DLY_MIX, 30, P_REV_MIX, 25, P_FEEDBACK, 20,
     P_DLY_TYPE, 2, -1},
    {I_(GP_PITCH), 5100, I_(GP_WAVE), 85, I_(GP_WIDTH), 20, I_(GP_DETUNE), 80, I_(GP_MIX), 25, I_(GP_CUTOFF), 500,
     I_(GP_RESO), 100, I_(GP_DRIVE), 10, I_(GP_LEVEL), 84, I_(GP_FLFO_DEPTH), 20, II_(GP_PITCH), 3900,
     II_(GP_WAVE), 50, II_(GP_INTERVAL), 13, II_(GP_DETUNE), 6, II_(GP_MIX), 30, II_(GP_CUTOFF), 480,
     II_(GP_RESO), 60, II_(GP_DRIVE), 0, II_(GP_LEVEL), 84, II_(GP_FLFO_DEPTH), -15, P_LFO_RATE, 40, P_GLIDE, 20,
     P_DRIFT, 35, P_PULSE_RATE, 25, P_PULSE_DEPTH, 25, P_PULSE_SHAPE, 0, P_DLY_TIME, 120, P_DLY_REPEATS, 35,
     P_DLY_MIX, 10, P_REV_TYPE, 3, P_DLY_LFO_DEPTH, 0, P_CHAR, 20, P_WANDER, 35, P_AGE, 20, P_WSCALE, 4,
     P_REV_LEN, 75, P_REV_DAMP, 50, P_SUBW_0, 1, P_SUBW_1, 1, P_TUNING, 9, -1},
    {I_(GP_PITCH), 3800, I_(GP_CUTOFF), 600, I_(GP_LEVEL), 88, I_(GP_FLFO_DEPTH), 0, II_(GP_WAVE), 100,
     II_(GP_RESO), 720, II_(GP_DRIVE), 50, II_(GP_LEVEL), 100, II_(GP_PARTNER), 0, II_(GP_OLFO), 1,
     II_(GP_OLFO_DEPTH), -70, II_(GP_FLFO), 1, II_(GP_FLFO_DEPTH), 40, P_LFO_RATE, 113, P_LFO_W1, 10, P_GLIDE, 0,
     P_PULSE_RATE, 9, P_PULSE_DEPTH, 35, P_PULSE_SHAPE, 0, P_DLY_TIME, 40, P_DLY_REPEATS, 45, P_DLY_MIX, 30,
     P_REV_MIX, 20, P_WANDER, 10, P_WSCALE, 2, P_DLY_TYPE, 2, -1},
    {I_(GP_WAVE), 100, I_(GP_WIDTH), 35, I_(GP_CUTOFF), 720, I_(GP_LEVEL), 78, I_(GP_OLFO_DEPTH), 7,
     I_(GP_FLFO_DEPTH), 40, I_(GP_NOISE), 18, II_(GP_PITCH), 5300, II_(GP_WAVE), 67, II_(GP_CUTOFF), 700,
     II_(GP_LEVEL), 78, II_(GP_OLFO), 1, II_(GP_OLFO_DEPTH), 6, II_(GP_FLFO_DEPTH), -40, II_(GP_NOISE), 12,
     P_LFO_RATE, 106, P_DLY_REPEATS, 55, P_DLY_MIX, 35, P_REV_MIX, 25, P_REV_TYPE, 0, P_FEEDBACK, 15, P_CHAR, 67,
     P_AGE, 45, P_REV_LEN, 35, P_PULSE_RATE, 2, P_PULSE_DEPTH, 20, -1},
    {I_(GP_PITCH), 6429, I_(GP_WAVE), 0, I_(GP_INTERVAL), 9, I_(GP_DETUNE), 15, I_(GP_MIX), 33, I_(GP_CUTOFF), 300,
     I_(GP_RESO), 50, I_(GP_DRIVE), 21, I_(GP_LEVEL), 94, I_(GP_OLFO), 3, I_(GP_OLFO_DEPTH), 65, I_(GP_FLFO), 2,
     II_(GP_PITCH), 5469, II_(GP_WAVE), 10, II_(GP_INTERVAL), 13, II_(GP_DETUNE), 15, II_(GP_MIX), 39,
     II_(GP_CUTOFF), 380, II_(GP_RESO), 50, II_(GP_DRIVE), 38, II_(GP_LEVEL), 88, II_(GP_OLFO), 3,
     II_(GP_OLFO_DEPTH), 70, II_(GP_OLFO_DEST), 1, P_XMOD, 1, P_XMOD_AMT, 41, P_LFO_RATE, 50, P_GLIDE, 0,
     P_DRIFT, 60, P_PULSE_DEPTH, 21, P_PULSE_SHAPE, 76, P_DLY_TIME, 80, P_DLY_REPEATS, 60, P_REV_MIX, 45,
     P_DLY_LFO_DEPTH, 28, P_LFO_RMOD, -100, P_LFO_RMOD_SRC, 3, P_MOVE, 35, P_FEEDBACK, 64, P_WANDER, 8,
     P_DLY_TYPE, 1, P_REV_LEN, 92, P_REV_DAMP, 20, P_SUBW_0, 1, P_SUBW_1, 1, P_TUNING, 7, -1},
    {I_(GP_MIX), 0, I_(GP_CUTOFF), 1000, I_(GP_RESO), 0, I_(GP_DRIVE), 0, I_(GP_LEVEL), 80, I_(GP_PARTNER), 0,
     I_(GP_FLFO_DEPTH), 0, II_(GP_LEVEL), 100, II_(GP_PARTNER), 0, II_(GP_FLFO_DEPTH), 0, P_DRIFT, 0, P_PULSE, 0,
     P_DLY_MIX, 0, P_REV_MIX, 0, P_DLY_LFO_DEPTH, 0, P_II_OUT, 1, P_MOVE, 0, -1},
};
static const char *const NIA_PRESET_NAME[] = {"NIAGARA", "EMBERS", "VESPERS", "DREAMHOUSE", "ABYSS", "SEANCE", "SONAR",
                                              "SIBYL", "HOWL", "CISTERN", "PISTON", "ASHRAM", "TELEGRAPH", "TAPE",
                                              "FALLING", "BARE"};
#define NIA_NPRESETS (sizeof PRESET_CHANGES / sizeof PRESET_CHANGES[0])
_Static_assert(sizeof NIA_PRESET_NAME / sizeof NIA_PRESET_NAME[0] == NIA_NPRESETS, "a name for each Preset");

uint32_t nia_npresets(void) { return NIA_NPRESETS; }
const char *nia_preset_name(uint32_t p) { return NIA_PRESET_NAME[p % NIA_NPRESETS]; }

void nia_preset(uint32_t p, int16_t *v, int16_t *rev, int16_t *dly)
{
    int16_t a[DR_NPARAMS];
    const int16_t *c = PRESET_CHANGES[p % NIA_NPRESETS];
    uint32_t k;
    for (k = 0; k < DR_NPARAMS; k++)
        a[k] = PARAMS[k].def;
    for (k = 0; k < 100u && c[k] >= 0; k += 2u)
        a[c[k]] = c[k + 1u];
    for (k = 0; k < NK_N; k++)
        v[k] = a[NIA_ID[k]];
    v[NK_IIOFS] = a[P_PLINK] ? a[P_PLINK_OFF] : (int16_t)(a[G2(GP_PITCH)] - a[G1(GP_PITCH)]);
    *rev = a[P_REV_MIX];
    *dly = a[P_DLY_MIX];
}
