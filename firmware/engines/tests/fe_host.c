/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca-Engines: a standalone host. Renders every preset of the six engines through a fixed phrase on a
 * minimal track / voice / envelope and prints one line per render: an FNV-1a 64 hash of the output and its
 * peak. tests/run.sh compares the lines with tests/golden.txt: a change of any engine's sound fails.
 *   cc -O2 -I build -o fe_host tests/fe_host.c          (build/fe_tables.h from tools/gen_engine_tables.py)
 * FE_TABLES_H="other.h" builds it with another host's tables (Felucca checks its own this way: the same
 * hashes, so its generated tables are these). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------- the host's part --- */
#define NVOICE 8
#define NPART 4
#ifdef FE_TABLES_H
#include FE_TABLES_H
#else
#include "fe_tables.h"
#endif
#include "../src/felucca_engine.h"

enum { P_ATK, P_REL, P_FTYPE, P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7, P_COUNT };
typedef struct track {
    int16_t p[P_COUNT];
    voice_t v[NVOICE];
} track_t;
static track_t trk[NPART];

#include "../src/dsp.c"
#include "../src/eng_analog.c"
#include "../src/eng_phase.c"
#include "../src/eng_lofi.c"
#include "../src/eng_trio.c"
#include "../src/eng_wheel.c"
#include "../src/eng_noise.c"

static const struct { const char *id; const engine_t *e; } ENGS[] = {
    {"analog", &ENG_ANALOG}, {"lofi", &ENG_LOFI}, {"trio", &ENG_TRIO}, {"wheel", &ENG_WHEEL}, {"noise", &ENG_NOISE},
    {"phase", &ENG_PHASE},
};

/* ------------------------------------------------- a minimal voice host --- */
static uint32_t vage;

static void voice_on(track_t *t, const engine_t *e, voice_t *v, uint32_t note, int32_t fine)
{
    int sounding = v->active && v->stage != 0;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t s[8];
    memcpy(s, v->s, sizeof s);
    v->note = (uint8_t)note;
    v->vel = 100;
    v->gate = 1;
    v->active = 1;
    v->stage = 1;
    v->age = ++vage;
    v->pitch16 = v->pitch_cur = (int32_t)note * 16;
    v->fine = fine;
    if (!sounding)
        v->env = v->env_out = 0;
    e->note_on(t, v);
    if (sounding) {                                     /* a retrigger keeps the phases and the keep slots */
        uint32_t k;
        v->ph[0] = ph0;
        v->ph[1] = ph1;
        v->ph[2] = ph2;
        for (k = 0; k < 8u; k++)
            if (k != 2u && k != 3u && (e->keep >> k & 1u))
                v->s[k] = s[k];
    }
}

/* a plain envelope, once per block: attack in 4 blocks, sustain 3/4, release by 1/16 a block */
static int32_t env_tick(voice_t *v)
{
    if (!v->active)
        return 0;
    switch (v->stage) {
    case 1:
        v->env += 1 << 22;
        if (v->env >= 1 << 24) {
            v->env = 1 << 24;
            v->stage = 2;
        }
        break;
    case 2:
        v->env -= (v->env - (3 << 22)) >> 3;
        break;
    case 3:
        v->env -= v->env >> 4;
        if (v->env < (1 << 24) / 1000) {
            v->env = 0;
            v->stage = 0;
            v->active = 0;
        }
        break;
    default:
        break;
    }
    return v->env >> 9;                                 /* Q15 */
}

#define NBLK 240

typedef struct { uint64_t h; int32_t peak; } result_t;

/* the phrase: a low note, a second voice an octave and a fifth up (detuned) over it, the first voice
 * retriggered on another note, a third note, all released, the tails; the cutoff and the shape move */
static result_t render(const engine_t *e, const preset_t *pr, uint32_t part, int32_t ftype)
{
    track_t *t = &trk[part];
    int32_t out[CTL];
    int32_t last[NVOICE] = {0};
    uint32_t b, i, k;
    result_t r = {14695981039346656037ull, 0};
    memset(t, 0, sizeof *t);
    memset(drw_t, 0, sizeof drw_t);                     /* WHEEL's own state: each render from power-on */
    memset(drw_v, 0, sizeof drw_v);
    vage = 0;
    for (k = 0; k < 8u; k++)
        t->p[P_E0 + k] = pr->e[k];
    t->p[P_ATK] = pr->env[0];
    t->p[P_REL] = pr->env[3];
    t->p[P_FTYPE] = (int16_t)ftype;
    for (b = 0; b < NBLK; b++) {
        if (b == 0)
            voice_on(t, e, &t->v[0], 43, 0);
        if (b == 30)
            voice_on(t, e, &t->v[1], 62, 23);
        if (b == 70)
            voice_on(t, e, &t->v[0], 50, 0);
        if (b == 100)
            voice_on(t, e, &t->v[2], 79, -17);
        if (b == 150)
            for (k = 0; k < NVOICE; k++)
                if (t->v[k].active) {
                    t->v[k].gate = 0;
                    t->v[k].stage = 3;
                }
        if (e->block)
            e->block(t);
        memset(out, 0, sizeof out);
        for (k = 0; k < NVOICE; k++) {
            voice_t *v = &t->v[k];
            vmod_t m;
            int32_t a;
            uint32_t base;
            if (!v->active)
                continue;
            a = env_tick(v);
            if (e->amp)
                a = e->amp(t, v, a);
            if (!v->active)
                continue;
            base = pitch_inc((uint32_t)clamp(v->pitch16, 0, 2047));
            memset(&m, 0, sizeof m);
            m.pitch16 = v->pitch16;
            m.inc = base + (uint32_t)((int32_t)(base >> 12) * v->fine);
            m.fine = v->fine;
            m.amp0 = last[k];
            m.amp1 = a;
            m.envq15 = a;
            m.cutoff = ((int32_t)(b % 96u) - 48) * 128; /* a slow triangle-ish sweep of +-24 << 8 */
            m.shape = (64 << 8) + ((int32_t)(b % 64u) - 32) * 256;
            last[k] = a;
            e->render(t, v, out, CTL, &m);
        }
        for (i = 0; i < CTL; i++) {
            uint32_t x = (uint32_t)out[i];
            int32_t a = out[i] < 0 ? -out[i] : out[i];
            for (k = 0; k < 4u; k++) {
                r.h ^= (x >> (8u * k)) & 0xFFu;
                r.h *= 1099511628211ull;
            }
            if (a > r.peak)
                r.peak = a;
        }
    }
    return r;
}

int main(void)
{
    uint32_t n, j, c = 0;
    for (n = 0; n < NELEM(ENGS); n++) {
        const engine_t *e = ENGS[n].e;
        for (j = 0; j < e->npresets; j++, c++) {
            result_t r = render(e, &e->presets[j], c % NPART, 0);
            printf("%s/%u %016llx %d %s\n", ENGS[n].id, j, (unsigned long long)r.h, r.peak, e->presets[j].name);
        }
    }
    for (j = 0; j < ENG_ANALOG.npresets; j++) {         /* ANALOG's filter types: band-pass, high-pass */
        int32_t ft;
        for (ft = 1; ft <= 2; ft++) {
            result_t r = render(&ENG_ANALOG, &ENG_ANALOG.presets[j], j % NPART, ft);
            printf("analog/%u/ftype%d %016llx %d %s\n", j, ft, (unsigned long long)r.h, r.peak,
                   ENG_ANALOG.presets[j].name);
        }
    }
    {   /* the modes the presets do not reach: every value of E0 (ANALOG WAVE incl. SYNC / SUB, LOFI CHIP,
         * TRIO WAVE, WHEEL REG, NOISE MODE) and LOFI's WAVE, on the engine's defaults */
        for (n = 0; n < NELEM(ENGS); n++) {
            const engine_t *e = ENGS[n].e;
            preset_t pr;
            int32_t v;
            memset(&pr, 0, sizeof pr);
            for (j = 0; j < 8u; j++)
                pr.e[j] = (int8_t)e->edit[j].def;
            pr.env[3] = 40;
            for (v = e->edit[0].min; v <= e->edit[0].max; v++) {
                result_t r;
                pr.e[0] = (int8_t)v;
                r = render(e, &pr, (uint32_t)v % NPART, 0);
                printf("%s/e0=%d %016llx %d\n", ENGS[n].id, v, (unsigned long long)r.h, r.peak);
            }
        }
        {
            const engine_t *e = &ENG_LOFI;
            preset_t pr;
            int32_t v;
            memset(&pr, 0, sizeof pr);
            for (j = 0; j < 8u; j++)
                pr.e[j] = (int8_t)e->edit[j].def;
            for (v = e->edit[1].min; v <= e->edit[1].max; v++) {
                result_t r;
                pr.e[1] = (int8_t)v;
                r = render(e, &pr, 0, 0);
                printf("lofi/wave=%d %016llx %d\n", v, (unsigned long long)r.h, r.peak);
            }
        }
    }
    return 0;
}
