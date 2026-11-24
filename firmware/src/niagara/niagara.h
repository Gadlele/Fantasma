/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Gadlele */
/* Fantasma 2.0: Niagara Drift's drones on the DRONE channel (niagara.c: its own unit, built with the FPU, as Niagara
 * Drift builds drone.c). Fantasma's side (eng_niagara.c) knows them by these NK_* values, in Niagara's own units, as the
 * pages show them; niagara.c maps each to drone.c's parameter. Two drones, as Niagara Drift's: I and II */
#pragma once
#include <stdint.h>

enum {
    NK_WAVE1, NK_WIDTH1, NK_MIX1, NK_LEVEL1,         /* EDIT 1 (DRONE I): X-WAVE, the square's width, OSC / SUB, level */
    NK_IV1, NK_DET1, NK_PART1, NK_SUB1,              /* EDIT 2: the partner's interval, its detune, on / off; the sub */
    NK_CUT1, NK_RES1, NK_MODE1, NK_DRV1,             /* FILTER I */
    NK_NOISE1, NK_COLOR1, NK_SUBW1,                  /* NOISE I: the noise, its colour; the sub's wave */
    NK_WAVE2, NK_WIDTH2, NK_MIX2, NK_LEVEL2,         /* DRONE II */
    NK_IV2, NK_DET2, NK_PART2, NK_SUB2,              /* DRONE II 2 */
    NK_CUT2, NK_RES2, NK_MODE2, NK_DRV2,             /* FILTER II */
    NK_NOISE2, NK_COLOR2, NK_SUBW2,                  /* NOISE II */
    NK_IIOFS, NK_PLINK, NK_SYNC, NK_XMOD,            /* LINK: drone II's pitch from drone I's (cents), linked, hard sync,
                                                      * cross-mod */
    NK_XAMT, NK_STEREO, NK_TUNING, NK_IIOUT,         /* LINK 2: cross-mod's amount, the stereo width, the Tuning, II out */
    NK_DRIFT, NK_CHAR, NK_AGE, NK_FDBK,              /* CHARACTER */
    NK_WANDER, NK_WSCALE,                            /* WANDER: drone II's slow melody, its scale */
    NK_LRATE, NK_W1, NK_W2, NK_MOVE,                 /* LFO: the ULTRA LFO's rate, Wave I, Wave II, Movement */
    NK_RMOD, NK_RSRC, NK_LOCK,                       /* LFO 2: its speed moved by a wave, which, the lock to the pulse */
    NK_OLFO1, NK_ODEP1, NK_FLFO1, NK_FDEP1,          /* LFO I: drone I's oscillator tap and depth, filter tap and depth */
    NK_OLFO2, NK_ODEP2, NK_FLFO2, NK_FDEP2,          /* LFO II */
    NK_ODST1, NK_FDST1, NK_ODST2, NK_FDST2,          /* LFO TO: what each tap moves (pitch / X-WAVE, cutoff / resonance) */
    NK_PULSE, NK_PRATE, NK_PDEPTH, NK_PSHAPE,        /* PULSE: on, rate, depth, shape */
    NK_PSWING, NK_GLIDE, NK_MORPH,                   /* PULSE 2: swing; the pitches' glide; a switch's morph */
    NK_N
};

void nia_init(void);                                 /* drone.c started, its own effects' mixes out, playing */
void nia_set(uint32_t k, int32_t v);                 /* value k (NK_*), Niagara's units */
void nia_pitch(int32_t c1);                          /* drone I's pitch, cents over MIDI note 0 (II: NK_IIOFS from it) */
void nia_render(int32_t *lr, uint32_t n);            /* n (<= 32) frames, L R L R .., full scale ~ +-32767 */
int32_t nia_get(uint32_t k);                         /* value k as drone.c has it (its commands taken) */
void nia_range(uint32_t k, int16_t *lo, int16_t *hi, int16_t *def);   /* k's range and default (drone.c's) */
void nia_text(uint32_t k, int32_t v, char *buf);     /* v as Niagara Drift shows it (12 bytes) */
uint32_t nia_npresets(void);
const char *nia_preset_name(uint32_t p);
void nia_preset(uint32_t p, int16_t *v, int16_t *rev, int16_t *dly);  /* Preset p's NK_N values, its reverb and delay
                                                      * mix (0..100: the channel's sends stand in for them) */
