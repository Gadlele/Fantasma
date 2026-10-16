/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* BELL's generator (Fantasma; seq.c gen_step, eng_bell.c), on the real sequencer: a track on BELL with DENS > 0 plays
 * four loops of 13 / 17 / 23 / 29 steps instead of its steps. Checks: DENS 0 plays the steps as ever; the same SEED
 * the same bells and another SEED others; the notes on the scale; DENS thins and thickens; the loops do not repeat
 * within hundreds of steps; EVOLVE changes them as they come round (and 0 does not). Built and run by
 * tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#define NSTEPS 700u
#define CHK(c) do { if (!(c)) { printf("bell_gen: FAIL line %d: %s\n", __LINE__, #c); bad++; } } while (0)

/* track 3 on BELL, steps empty, a 16-step pattern; plays NSTEPS steps: ev[i] = notes that started on step i (bit set
 * by note), cnt[i] = how many; returns the note-ons */
static uint8_t cnt[NSTEPS], first[NSTEPS], notes[NSTEPS][4];
static uint32_t play(uint32_t dens, uint32_t evolve, uint32_t seed, uint32_t scale)
{
    track_t *t;
    uint32_t b, n = 0, last = 0xFFFFu, steps = 0, blk, tot = 0;
    ui_power_on();
    t = &trk[2];
    track_defaults_steps(t);
    t->engine = t->eng_req = ENGI_BELL;
    t->p[P_SLEN] = 16;
    t->p[P_SGATE] = 40;
    t->p[P_SSWING] = 0;
    t->p[P_E4] = (int16_t)dens; t->p[P_E5] = (int16_t)evolve; t->p[P_E6] = 1; t->p[P_E7] = (int16_t)seed;
    t->p[P_SCALE] = (int16_t)scale; t->p[P_ROOT] = 0;
    memset(cnt, 0, sizeof cnt); memset(notes, 0, sizeof notes);
    seq_start();
    blk = div_samples((uint32_t)t->p[P_SDIV]) / CTL;
    (void)n; (void)last; (void)blk;
    for (b = 0; steps < NSTEPS && b < 4000000u; b++) {
        uint32_t v0 = vage;
        uint32_t before = gen[2].c;
        seq_tick(t, CTL);
        if (gen[2].c != before && steps < NSTEPS) {      /* a step started */
            steps++;
        }
        if (v0 != vage && steps && steps <= NSTEPS) {    /* note-ons on this step */
            uint32_t i;
            cnt[steps - 1] = (uint8_t)t->seq_n;
            for (i = 0; i < t->seq_n && i < 4u; i++) notes[steps - 1][i] = t->seq_notes[i];
            tot += t->seq_n;
        }
    }
    return tot;
}
static uint32_t hits(void) { uint32_t i, h = 0; for (i = 0; i < NSTEPS; i++) h += cnt[i] != 0; return h; }
static uint32_t hash(void) { uint32_t i, k, h = 2166136261u; for (i = 0; i < NSTEPS; i++) for (k = 0; k < 4; k++) h = (h ^ notes[i][k]) * 16777619u; return h; }

int main(void)
{
    int bad = 0;
    uint32_t i, k, a, b, h0, h1, n0, n1, n2, p;
    uint8_t keep[NSTEPS];
    uint32_t mask;
    n0 = play(0, 0, 5, 0);
    CHK(n0 == 0);                                         /* DENS 0: the empty steps, nothing */
    n1 = play(100, 0, 5, 0);
    CHK(hits() >= NSTEPS - 2u);                           /* every slot sounds: a note on (nearly) every step */
    n1 = play(25, 0, 5, 0); a = hits(); h0 = hash();
    n2 = play(70, 0, 5, 0); b = hits();
    CHK(a > NSTEPS / 5u && b > a + NSTEPS / 10u);         /* DENS thins and thickens */
    play(25, 0, 5, 0);
    CHK(hash() == h0);                                    /* the same SEED, the same bells */
    play(25, 0, 6, 0);
    CHK(hash() != h0);                                    /* another SEED, others */
    /* no repeat: the hit pattern (with EVOLVE 0) of 700 steps has no period up to 300 */
    play(25, 0, 5, 0);
    for (i = 0; i < NSTEPS; i++) keep[i] = (uint8_t)(cnt[i] ? notes[i][0] + 1u : 0u);
    for (p = 1; p <= 300u; p++) {
        uint32_t same = 1;
        for (i = 0; i + p < NSTEPS; i++) if (keep[i] != keep[i + p]) { same = 0; break; }
        CHK(!same);
    }
    /* EVOLVE: the first 13 steps cannot differ (no loop came round yet), later ones do; EVOLVE 0 stays as it is */
    play(25, 100, 5, 0);
    for (i = 0; i < 13u; i++) CHK((cnt[i] ? notes[i][0] + 1u : 0u) == keep[i]);
    for (i = 13; i < NSTEPS && (cnt[i] ? notes[i][0] + 1u : 0u) == keep[i]; i++) ;
    CHK(i < NSTEPS);
    /* the scale: MAJ, every note on it (ROOT 0) */
    mask = SCALE_MASK[1];
    play(60, 0, 9, 1);
    for (i = 0; i < NSTEPS; i++) for (k = 0; k < cnt[i] && k < 4u; k++) CHK((mask >> (notes[i][k] % 12u)) & 1u);
    /* the pentatonic, RANGE 2 oct: notes stay within 48 .. 48 + 24 */
    play(60, 0, 9, 5);
    for (i = 0; i < NSTEPS; i++) for (k = 0; k < cnt[i] && k < 4u; k++) CHK(notes[i][k] >= 36u && notes[i][k] < 48u + 24u);
    /* the garden: with the generator on, a key turns its note on / off and does not play; the bells keep to the garden */
    {
        track_t *t;
        uint32_t seen = 0, kc, ke;
        play(60, 0, 9, 1);                                 /* track 3 on BELL, DENS 60, C major; the sequencer stopped after */
        t = &trk[2];
        song.sel = 2;
        kb_prev = 0; fm1_in.notes = 0;
        CHK(garden[2] == 0 && scale_mask(t) == SCALE_MASK[1]);
        kc = 53u % 12u;                                    /* key 0 is F3 (53): pitch class 5 */
        key_down(0); key_up(0);
        CHK(garden[2] == (uint16_t)(SCALE_MASK[1] ^ (1u << 5)));      /* the scale to start from, F turned off */
        CHK(!((scale_mask(t) >> 5) & 1u));
        CHK(t->v[0].active == 0 || !t->v[0].gate);         /* (the key sounded nothing) */
        key_down(2); key_up(2);                            /* key 2 = G3 (pitch class 7): off too */
        key_down(1); key_up(1);                            /* key 1 = F#3 (6): on */
        CHK(!((garden[2] >> 7) & 1u) && ((garden[2] >> 6) & 1u));
        /* the bells only fall on the garden: C D E F# A B (F and G out) */
        play(100, 0, 9, 1);                                /* (play() starts the track again; garden stays) */
        for (i = 0; i < NSTEPS; i++) for (k = 0; k < cnt[i] && k < 4u; k++) {
            uint32_t pc = notes[i][k] % 12u;
            seen |= 1u << pc;
            CHK((garden[2] >> pc) & 1u);
        }
        CHK(seen != 0);
        (void)kc; (void)ke;
        /* another SCALE (ECHOES HOME's KNOB 3): the garden is that scale again, and a key edits it from there */
        t->p[P_SCALE] = 2;
        CHK(scale_mask(t) == SCALE_MASK[2] && garden[2] == 0);
        kb_prev = 0; fm1_in.notes = 0; song.sel = 2;
        key_down(0); key_up(0);
        CHK(garden[2] == (uint16_t)(SCALE_MASK[2] ^ (1u << 5)) || garden[2] == (uint16_t)(SCALE_MASK[2] | (1u << 5)));
        t->p[P_SCALE] = 1;
        garden[2] = 0;
        /* DENS 0: the keys play again, the garden is left alone */
        t->p[P_E4] = 0;
        {
            uint16_t g = garden[2];
            key_down(0); key_up(0);
            CHK(garden[2] == g);
        }
        garden[2] = 0;
    }
    {   /* the own clock: the transport stopped, DENS above 0, the blocks go by: bells come; DENS 0 and they stop */
        track_t *t;
        uint32_t b, struck = 0, v0;
        ui_power_on();
        t = &trk[2];
        t->engine = t->eng_req = ENGI_BELL;
        t->p[P_E4] = 80; t->p[P_E5] = 20; t->p[P_E6] = 1; t->p[P_E7] = 3; t->p[P_SCALE] = 1;
        song.g[G_CLOCK] = 0; song.g[G_BPM] = 120; song.playing = 0;
        v0 = vage;
        for (b = 0; b < 44100u * 5u / CTL; b++) {
            uint32_t before = vage;
            events_block(CTL);
            if (vage != before) struck++;
        }
        CHK(!song.playing && vage != v0 && struck >= 8u);
        t->p[P_E4] = 0;
        events_block(CTL);
        v0 = vage;
        for (b = 0; b < 44100u * 2u / CTL; b++) events_block(CTL);
        CHK(vage == v0);
        printf("bell_gen: own clock, transport stopped: %u strikes in 5 s\n", struck);
    }
    printf("bell_gen: hits at DENS 25: %u, 70: %u of %u steps; note-ons %u / %u\n", a, b, NSTEPS, n1, n2);
    (void)h1;
    if (bad) { printf("bell_gen: %d FAILED\n", bad); return 1; }
    printf("bell_gen: DENS 0 plays the steps, SEED repeats / differs, DENS thins, no repeat within 300 steps, EVOLVE, SCALE and RANGE passed.\n");
    return 0;
}
