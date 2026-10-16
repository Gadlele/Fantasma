/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The Pad's chord progression (Fantasma; seq.c prog_step): with DENS above 0 track 1 plays chords of its ROOT / SCALE by
 * itself instead of its steps (EVOLVE chords to choose between: 4 = I IV V vi, up to 8 with ii iii vii and a bVII; BARS 0 =
 * RND). Checks: DENS 0 plays the steps as ever; the chords are the diatonic triads of the
 * scale (major and minor), only on I IV V vi, I the most often; each is struck once and held for BARS x 16 steps; DENS
 * thins the bars; the same chord is seldom repeated. Built and run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#define NSTEPS 2000u
#define CHK(c) do { if (!(c)) { printf("prog: FAIL line %d: %s\n", __LINE__, #c); bad++; } } while (0)

static uint8_t at_step[NSTEPS], cn[NSTEPS][4];       /* per step: notes started on it (count, the notes) */
static uint32_t g_ev = 4, g_seed;
static uint32_t play(uint32_t dens, uint32_t bars, uint32_t scale, uint32_t root)
{
    track_t *t;
    uint32_t b, steps = 0, tot = 0;
    ui_power_on();
    t = &trk[0];
    track_defaults_steps(t);
    t->engine = t->eng_req = 0;
    t->p[P_VOICE] = V_POLY; t->p[P_VOIC] = VC_CLOSE; t->p[P_CHRD] = 0;
    t->p[P_SLEN] = 16; t->p[P_SGATE] = 90; t->p[P_SSWING] = 0;
    t->p[P_PDEN] = (int16_t)dens; t->p[P_PLEN] = (int16_t)bars; t->p[P_PEV] = (int16_t)g_ev; t->p[P_PSEED] = (int16_t)g_seed;
    t->p[P_SCALE] = (int16_t)scale; t->p[P_ROOT] = (int16_t)root; t->p[P_TRANS] = 0;
    song.octave = 0;
    memset(at_step, 0, sizeof at_step); memset(cn, 0, sizeof cn);
    seq_start();
    for (b = 0; steps < NSTEPS && b < 40000000u; b++) {
        uint32_t v0 = vage, before = prog_c[0];
        seq_tick(t, CTL);
        if (prog_c[0] != before && steps < NSTEPS) steps++;
        if (v0 != vage && steps && steps <= NSTEPS) {
            uint32_t i;
            at_step[steps - 1] = (uint8_t)t->seq_n;
            for (i = 0; i < t->seq_n && i < 4u; i++) cn[steps - 1][i] = t->seq_notes[i];
            tot += t->seq_n;
        }
    }
    return tot;
}
static uint32_t chords(void) { uint32_t i, n = 0; for (i = 0; i < NSTEPS; i++) n += at_step[i] != 0; return n; }
static uint32_t pcset(uint32_t i) { uint32_t k, m = 0; for (k = 0; k < at_step[i]; k++) m |= 1u << (cn[i][k] % 12u); return m; }

int main(void)
{
    int bad = 0;
    uint32_t i, n, deg[4] = {0, 0, 0, 0}, rep = 0, prev = 99, last = 0;
    /* the diatonic triads as pitch-class sets: C major I IV V vi, D minor i iv v VI */
    static const uint32_t MAJ[4] = {1u | 1u << 4 | 1u << 7, 1u << 5 | 1u << 9 | 1u, 1u << 7 | 1u << 11 | 1u << 2, 1u << 9 | 1u | 1u << 4};
    static const uint32_t MIN[4] = {1u << 2 | 1u << 5 | 1u << 9, 1u << 7 | 1u << 10 | 1u << 2, 1u << 9 | 1u << 0 | 1u << 4, 1u << 10 | 1u << 2 | 1u << 5};
    n = play(0, 1, 1, 0);
    CHK(n == 0);                                         /* DENS 0: the empty steps, nothing */
    play(100, 1, 1, 0);
    CHK(chords() >= NSTEPS / 16u - 1u);                  /* a chord every bar */
    for (i = 0; i < NSTEPS; i++) {
        if (!at_step[i]) continue;
        CHK(i % 16u == 0 && at_step[i] == 3u);           /* struck on a bar, a triad */
        {
            uint32_t s = pcset(i), d;
            for (d = 0; d < 4u && s != MAJ[d]; d++) ;
            CHK(d < 4u);                                 /* one of I IV V vi of C major */
            if (d < 4u) { deg[d]++; if (d == prev) rep++; prev = d; }
        }
    }
    CHK(deg[0] > deg[1] && deg[0] > deg[2] && deg[1] > deg[3] && deg[3] > 5u);   /* I most, all four heard, vi among the least */
    printf("prog: chords %u I %u IV %u V %u vi %u repeats %u\n", chords(), deg[0], deg[1], deg[2], deg[3], rep);
    CHK(rep < chords() / 5u);                            /* seldom the same chord twice */
    play(100, 2, 1, 0);
    for (i = 0; i < NSTEPS; i++) if (at_step[i]) { CHK(i % 32u == 0); last = i; }
    CHK(chords() >= NSTEPS / 32u - 1u && last > 0);      /* BARS 2: a chord every 32 steps */
    play(50, 1, 1, 0);
    n = chords();
    CHK(n > NSTEPS / 16u / 4u && n < NSTEPS / 16u * 3u / 4u);   /* DENS 50: about half the bars */
    play(100, 1, 2, 2);                                  /* D natural minor */
    for (i = 0; i < NSTEPS; i++) {
        uint32_t s, d;
        if (!at_step[i]) continue;
        s = pcset(i);
        for (d = 0; d < 4u && s != MIN[d]; d++) ;
        CHK(d < 4u);
    }
    {   /* EVOLVE 8: ii, iii, vii and a bVII too (the rarer the later); 1: the I only; BARS RND: lengths of 1 to 6 bars; SEED:
         * the same one the same chords */
        static const uint32_t IIIII[2] = {1u << 2 | 1u << 5 | 1u << 9, 1u << 4 | 1u << 7 | 1u << 11};   /* C major: ii, iii */
        static const uint32_t VII = 1u << 11 | 1u << 2 | 1u << 5, BVII = 1u << 10 | 1u << 2 | 1u << 5;
        uint32_t other = 0, nvii = 0, nbvii = 0, nI = 0, tot = 0, k, a[40], b[40], c[40], na = 0, same = 1, diff = 0, sd;
        g_ev = 8;
        for (sd = 1; sd <= 6u; sd++) {
            g_seed = sd;
            play(100, 1, 1, 0);
            for (i = 0; i < NSTEPS; i++) {
                if (!at_step[i]) continue;
                tot++;
                if (pcset(i) == IIIII[0] || pcset(i) == IIIII[1]) other++;
                if (pcset(i) == VII) nvii++;
                if (pcset(i) == BVII) nbvii++;
                if (pcset(i) == MAJ[0]) nI++;
            }
        }
        CHK(other > 20u && nvii > 0u && nbvii > 0u && nI > other);   /* all eight heard, I the most */
        CHK(other + nvii + nbvii + nI < tot + 1u && tot > 600u);
        printf("prog: EVOLVE 8: of %u chords I %u, ii/iii %u, vii %u, bVII %u\n", tot, nI, other, nvii, nbvii);
        g_ev = 1; g_seed = 5;
        play(100, 1, 1, 0);
        for (i = 0; i < NSTEPS; i++) if (at_step[i]) CHK(pcset(i) == MAJ[0]);   /* one chord to choose from: the I */
        g_ev = 4; g_seed = 5;                            /* BARS RND: the lengths vary */
        {
            uint32_t len[8] = {0}, prev2 = 0xFFFFu, kinds = 0, bars;
            play(100, 0, 1, 0);
            for (i = 0; i < NSTEPS; i++) {
                if (!at_step[i]) continue;
                if (prev2 != 0xFFFFu) { bars = (i - prev2) / 16u; CHK(i % 16u == 0 && bars >= 1u && bars <= 6u); if (bars < 8u) len[bars]++; }
                prev2 = i;
            }
            for (k = 0; k < 8u; k++) kinds += len[k] > 0u;
            CHK(kinds >= 3u);
        }
        g_ev = 8; g_seed = 5;
        play(100, 2, 1, 0);
        for (i = 0; i < NSTEPS; i++) if (at_step[i] && na < 40u) a[na++] = pcset(i);
        play(100, 2, 1, 0);                              /* the same SEED again: the same progression */
        for (i = 0, k = 0; i < NSTEPS && k < 40u; i++) if (at_step[i]) b[k++] = pcset(i);
        for (i = 0; i < k && i < na; i++) same &= a[i] == b[i];
        CHK(same && k >= 20u);
        g_seed = 77;
        play(100, 2, 1, 0);
        for (i = 0, k = 0; i < NSTEPS && k < 40u; i++) if (at_step[i]) c[k++] = pcset(i);
        for (i = 0; i < k && i < na; i++) diff |= a[i] != c[i];
        CHK(diff);                                       /* another SEED: another progression */
        g_ev = 4; g_seed = 0;
    }
    {   /* the own clock: the transport stopped, DENS above 0 and the blocks go by: chords come, a stop (DENS 0) lets go */
        track_t *t;
        uint32_t b, struck = 0, v0;
        ui_power_on();
        t = &trk[0];
        t->engine = t->eng_req = 0;
        t->p[P_VOICE] = V_POLY; t->p[P_PDEN] = 100; t->p[P_PLEN] = 1; t->p[P_PEV] = 4;
        song.g[G_CLOCK] = 0; song.g[G_BPM] = 120;
        song.playing = 0;
        v0 = vage;
        for (b = 0; b < 44100u * 10u / CTL; b++) {       /* 10 s: 5 bars of 2 s */
            uint32_t before = vage;
            events_block(CTL);
            if (vage != before) struck++;
        }
        CHK(!song.playing && vage != v0 && struck >= 3u);
        t->p[P_PDEN] = 0;
        events_block(CTL);
        CHK(t->seq_n == 0u);                             /* what it held was let go */
        v0 = vage;
        for (b = 0; b < 44100u * 4u / CTL; b++) events_block(CTL);
        CHK(vage == v0);                                 /* and nothing new comes */
        printf("prog: own clock, transport stopped: %u strikes in 10 s\n", struck);
    }
    if (bad) { printf("PROG TEST FAILED\n"); return 1; }
    printf("progression test passed\n");
    return 0;
}
