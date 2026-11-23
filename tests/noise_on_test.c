/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Gadlele, based on Fantasma 1.x's (Leo Kuroshita's Felucca tests) */
/* The DRONE that is always on (Fantasma 2.0; seq.c noise_keep, voice.c, ui.c apply_preset_to; in 1.x the Noise's): on DRONE
 * (part 1) with REL at its top (INF) the sound goes on with no key: it starts by itself, comes back after its voice is cut,
 * and loading another sound keeps it playing with REL still INF; below INF nothing starts by itself; the ambient
 * machine's STOP lets everything go. Built and run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#define CHK(c) do { if (!(c)) { printf("drone_on: FAIL line %d: %s\n", __LINE__, #c); bad++; } } while (0)
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
    static const uint8_t DRONES[] = {12, 13, 14, 15, 16, 17, 22};   /* (part_preset_ok's DRONE list) */
    uint32_t k;
    ui_power_on();
    song.playing = 0; song.master_q12 = 4096; kb_boot_hold = 0;
    t = &trk[0];
    t->engine = t->eng_req;
    CHK(t->p[P_REL] < 127);
    CHK(run(2) < 5.0 && !voices(t));                      /* below INF: nothing starts by itself */
    t->p[P_REL] = 127; t->p[P_ESYNC] = 0;                 /* INF: always on */
    CHK(run(3) > 300.0 && voices(t) == 1u);               /* it starts by itself */
    CHK(run(20) > 300.0 && voices(t) == 1u);              /* ..and goes on well past the note's 8 s gate */
    for (k = 0; k < sizeof DRONES; k++) {                 /* another sound: it plays on, REL stays INF */
        apply_preset_to(t, DRONES[k]);
        CHK(t->p[P_REL] == 127);
        CHK(run(3) > 100.0 && voices(t) >= 1u);
    }
    seq_release(t); panic_req = 1u << 0;                  /* its voice cut: it comes back */
    CHK(run(4) > 100.0);
    t->p[P_REL] = 100;                                    /* below INF: it lets go and stays off */
    seq_release(t);
    song.g[G_RTYPE] = 2; song.g[G_RSIZE] = 0; song.g[G_RDAMP] = 127;
    run(40);
    CHK(run(3) < 5.0 && !voices(t));
    CHK(noise_inf(t) == 0);
    ambient_random();                                     /* the machine on, then OCT- STOP: everything off */
    CHK(run(6) > 100.0 && trk[1].p[P_PDEN] > 0 && noise_inf(&trk[0]) && al.v[AL_ON]);
    ambient_stop();
    song.g[G_RTYPE] = 2; song.g[G_RSIZE] = 0; song.g[G_RDAMP] = 127;   /* (the machine's AMBI tail is a minute: measure the voices with a short one) */
    CHK(trk[1].p[P_PDEN] == 0 && trk[2].p[P_E4] == 0 && !noise_inf(&trk[0]) && !al.v[AL_ON]);
    run(45);
    CHK(!voices(&trk[0]) && !voices(&trk[1]) && !voices(&trk[2]));   /* DRONE stays off, PAD and the bells rang out */
    CHK(run(3) < 5.0);
    if (bad) { printf("drone_on: %d FAILED\n", bad); return 1; }
    printf("drone_on: always on at INF, through new sounds, back after a cut, off below INF, and the machine's STOP passed.\n");
    return 0;
}
