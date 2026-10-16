/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The Noise that is always on (Fantasma; seq.c noise_keep, ui.c apply_preset_to): on the Noise part with the AMBI engine and
 * REL at its top (INF) the noise goes on with no key: it starts by itself, comes back after its voice is cut, and
 * loading another sound keeps it playing with REL still INF; below INF nothing starts by itself; the Pad's first chord after
 * a restart always sounds. Built and run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#define CHK(c) do { if (!(c)) { printf("noise_on: FAIL line %d: %s\n", __LINE__, #c); bad++; } } while (0)
static double run(uint32_t s)
{
    double e = 0;
    uint32_t n = 0, b, i;
    for (b = 0; b < 44100u * s / CTL; b++) {
        int32_t o[2 * CTL];
        events_block(CTL);
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++) { e += (double)o[i] * o[i]; n++; }
    }
    return __builtin_sqrt(e / n);
}
static uint32_t voices(const track_t *t) { uint32_t i, n = 0; for (i = 0; i < NVOICE; i++) n += t->v[i].active; return n; }

int main(void)
{
    int bad = 0;
    track_t *t;
    uint32_t k;
    ui_power_on();
    song.playing = 0; song.master_q12 = 4096; kb_boot_hold = 0;
    t = &trk[3];
    set_engine_of(t, ENGI_AMBI); t->engine = ENGI_AMBI;
    CHK(t->p[P_REL] < 127);
    CHK(run(2) < 5.0 && !voices(t));                      /* REL 64: nothing starts by itself */
    t->p[P_REL] = 127; t->p[P_ESYNC] = 0;                 /* INF: always on */
    CHK(run(3) > 500.0 && voices(t) == 1u);               /* it starts by itself */
    CHK(run(20) > 500.0 && voices(t) == 1u);              /* ..and goes on well past the note's 8 s gate */
    for (k = 0; k < 12u; k++) {                           /* another sound: it plays on, REL stays INF */
        apply_preset_to(t, k);
        CHK(t->p[P_REL] == 127);
        CHK(run(2) > 300.0 && voices(t) >= 1u);
    }
    seq_release(t); panic_req = 1u << 3;                  /* its voice cut: it comes back */
    CHK(run(3) > 300.0);
    t->p[P_REL] = 100;                                    /* below INF: it lets go and stays off */
    seq_release(t);
    run(25);
    CHK(run(3) < 5.0 && !voices(t));
    CHK(noise_inf(t) == 0);
    apply_preset_to(t, 2);                                /* (not INF: a preset's own REL as ever) */
    CHK(t->p[P_REL] == 64);
    ambient_random();                                     /* the machine on, then OCT- STOP: everything off */
    CHK(run(6) > 300.0 && trk[0].p[P_PDEN] > 0 && noise_inf(&trk[3]));
    ambient_stop();
    song.g[G_RTYPE] = 2; song.g[G_RSIZE] = 0; song.g[G_RDAMP] = 127;   /* (the machine's AMBI tail is a minute: measure the voices with a short one) */
    CHK(trk[0].p[P_PDEN] == 0 && trk[2].p[P_E4] == 0 && !noise_inf(&trk[3]));
    run(30);
    CHK(!voices(&trk[3]));                                /* (the Noise stays off, the Pad and bells rang out) */
    CHK(!voices(&trk[0]) && !voices(&trk[2]));
    CHK(run(3) < 5.0);
    if (bad) { printf("noise_on: %d FAILED\n", bad); return 1; }
    printf("noise_on: always on at INF, through new sounds, back after a cut, off below INF, and the machine's STOP passed.\n");
    return 0;
}
