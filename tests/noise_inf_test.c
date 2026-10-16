/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* AMBI with REL at its top (Fantasma; voice.c esync_coef): the noise never lets go after the note off. Checks: REL 127 keeps
 * the voice and its sound after the release, stopped or not; a second note replaces it (one voice, not a pile); turning REL
 * down lets it fade away; a REL below the top frees the voice as ever; every AMBI preset has REL 64. Built and run by
 * tests/run_tests.sh. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
#define CHK(c) do { if (!(c)) { printf("noise_inf: FAIL line %d: %s\n", __LINE__, #c); bad++; } } while (0)
static int32_t lastrms;
static void run(uint32_t ms)
{
    int32_t o[2 * CTL];
    uint32_t n = ms * (FS / 1000u) / CTL, i;
    double acc = 0;
    uint32_t cnt = 0;
    while (n--) {
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++) { acc += (double)o[i] * o[i]; cnt++; }
    }
    lastrms = cnt ? (int32_t)__builtin_sqrt(acc / cnt) : 0;
}
static uint32_t active(const track_t *t) { uint32_t i, n = 0; for (i = 0; i < NVOICE; i++) n += t->v[i].active; return n; }

int main(void)
{
    track_t *t;
    uint32_t i;
    host_tracks_init();
    t = &trk[3];
    host_preset_req(t, ENGI_AMBI, 0);                 /* SEA */
    t->engine = t->eng_req;
    t->p[P_VOICE] = V_POLY;
    run(100);
    for (i = 0; i < ENGINES[ENGI_AMBI]->npresets; i++)
        CHK(ENGINES[ENGI_AMBI]->presets[i].env[3] == 64);
    /* REL 64: the note off lets the voice go */
    t->p[P_REL] = 64;
    input_on(t, 60, 100); run(1500);
    CHK(active(t) == 1u);
    input_off(t, 60); run(30000);
    CHK(active(t) == 0u);
    /* REL 127: it stays */
    t->p[P_REL] = 127;
    input_on(t, 60, 100); run(1500);
    CHK(active(t) == 1u && lastrms > 100);
    input_off(t, 60); run(60000);
    CHK(active(t) == 1u && lastrms > 100);            /* a minute later it still sounds */
    /* a new note replaces it: the old one fades out as the new one rises (no hole between), then one voice is left */
    {
        int32_t steady, mn = 1 << 30, r;
        uint32_t k;
        run(2000); steady = lastrms;
        input_on(t, 62, 100);
        for (k = 0; k < 120; k++) { run(250); r = lastrms; if (r < mn) mn = r; }      /* 30 s: past SEA's ATK */
        input_off(t, 62); run(3000);
        CHK(active(t) == 1u && lastrms > 100);
        CHK(mn * 2 > steady);                         /* never below half of what it was */
    }
    /* (1.0) everything released (MIDI All Notes Off, a sound's undo: trk_all_off) while a note fades out for a new one: it
     * fades on, it does not come back for ever (one voice left, sounding) */
    input_on(t, 64, 100); run(20);
    {
        uint32_t i, fading = 0;
        for (i = 0; i < NVOICE; i++)
            fading += t->v[i].active && t->v[i].stage == 5u;
        CHK(fading == 1u);
    }
    trk_all_off(t);
    run(60000);
    CHK(active(t) == 1u && lastrms > 100);
    /* REL down: it fades away */
    t->p[P_REL] = 64;
    run(60000);
    CHK(active(t) == 0u);
    /* a tap shorter than the attack: STORM's ATK is slow; the note off at 0.3 s still ends in a drone at the sustain level */
    {
        int32_t held, tapped;
        host_tracks_init();
        t = &trk[3];
        host_preset_req(t, ENGI_AMBI, 2);              /* STORM: ATK 90, SUS 127 */
        t->engine = t->eng_req;
        t->p[P_VOICE] = V_POLY; t->p[P_REL] = 127; t->p[P_ESYNC] = 0;
        input_on(t, 60, 100); run(40000); held = lastrms;
        input_off(t, 60); run(100);
        t->p[P_REL] = 64; run(60000);
        CHK(active(t) == 0u);
        t->p[P_REL] = 127;
        input_on(t, 60, 100); run(300); input_off(t, 60); run(40000); tapped = lastrms;
        CHK(active(t) == 1u && tapped * 10 > held * 6);   /* at least 60 % of a held note's level */
        printf("noise_inf: STORM held %d, tapped for 0.3 s %d\n", held, tapped);
        /* REL turned up to INF while the voice is already dying: it comes back up to SUS and stays */
        t->p[P_REL] = 100;
        input_on(t, 64, 100); run(1500); input_off(t, 64); run(2000);
        t->p[P_REL] = 127; run(40000);
        CHK(active(t) >= 1u && lastrms * 10 > held * 5);
    }
    if (bad) { printf("NOISE INF TEST FAILED\n"); return 1; }
    printf("noise_inf: REL at the top holds AMBI's noise, a new note replaces it, REL down releases it, presets REL 64 passed.\n");
    return 0;
}
