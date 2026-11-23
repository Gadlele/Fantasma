/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Gadlele */
/* Fantasma 2.0: each channel its delay and reverb (firmware/src/chfx.c). BUS (Fantasma 1.x's buses) and CHAN; in CHAN a
 * track's DLY and REV sends reach its own delay and reverb only, the delays are four quarter-rate lanes of the bus's line
 * (each its own TIME), the reverbs are stereo, their tails end in silence and never run away (SIZE 127, SHIMMR 127, DAMP
 * 0, a minute); the AMBI layer sends to TO's; the pages edit the selected channel's; a project keeps it all, one without
 * it (Fantasma 1.x) loads as BUS. Built and run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#define CHK(c) do { if (!(c)) { printf("chfx: FAIL line %d: %s\n", __LINE__, #c); bad++; } } while (0)
static double run_rms(double s, double *side)
{
    double e = 0, d = 0;
    uint32_t b, i, n = 0;
    for (b = 0; b < (uint32_t)(44100.0 * s / CTL); b++) {
        int32_t o[2 * CTL];
        events_block(CTL);
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++, n++) {
            e += (double)o[2 * i] * o[2 * i];
            d += (double)(o[2 * i] - o[2 * i + 1]) * (o[2 * i] - o[2 * i + 1]);
        }
    }
    if (side)
        *side = sqrt(d / n);
    return sqrt(e / n);
}
static void chan_on(void)
{
    int16_t v[CF_N];
    chfx_defaults(v, 1);
    chfx_put(v);
    chfx_poll();
}

int main(int argc, char **argv)
{
    int bad = 0;
    uint32_t k;
    double sd;
    (void)argc; (void)argv;
    ui_power_on();
    song.master_q12 = 4096; kb_boot_hold = 0; song.playing = 0;
    CHK(!cf.mode[0] && !cf.mode[1] && !cf.drun);           /* the tests' power-on: BUS (main.c: CHAN) */
    {   /* main.c's power-on values: CHAN, each channel its own */
        int16_t v[CF_N];
        chfx_defaults(v, 1);
        CHK(v[NPART * CF_PER] == 1 && v[NPART * CF_PER + 1] == 1 && v[CF_SIZE] == CF_DEF[0][CF_SIZE] &&
            v[3 * CF_PER + CF_SHIM] == 0);
    }
    chan_on();
    CHK(cf.drun == 1u && cf.mode[1] == 1);
    for (k = 0; k < 4u; k++)                               /* only PAD plays, its REV and DLY up */
        trk[k].p[P_MUTE] = k != 1u;
    trk[1].p[P_REV] = 100; trk[1].p[P_DLY] = 60; trk[1].p[P_CHOR] = 0;
    run_rms(0.3, 0);                                       /* (the power-on's engine switches done) */
    trk_note_on(&trk[1], 60, 100);
    run_rms(1.0, 0);
    trk_note_off(&trk[1], 60);
    run_rms(0.3, 0);
    {   /* its own reverb and delay only: the others' lanes and lines still empty */
        int32_t e0 = 0, e1 = 0, ep = 0;
        uint32_t i;
        for (i = 0; i < CD_LANE; i++) {
            e0 |= dly_buf[i];                              /* DRONE's lane */
            e1 |= dly_buf[CD_LANE + i];                    /* PAD's */
        }
        for (i = 0; i < CR_LEN[0][0]; i++)
            ep |= cr[0].ln[0][i];
        CHK(!e0 && e1 && !ep);
        for (i = 0, ep = 0; i < CR_LEN[1][0]; i++)
            ep |= cr[1].ln[0][i];
        CHK(ep);
    }
    CHK(run_rms(2.0, &sd) > 100.0 && sd > 20.0);            /* a tail, stereo */
    for (k = 0; k < 10u; k++)                               /* .. that ends in silence */
        run_rms(6.0, 0);
    CHK(run_rms(1.0, 0) < 4.0);
    /* never runs away: SIZE 127, SHIMMR 127, DAMP 0, a loud chord, then a minute */
    cf.v[1][CF_SIZE] = 127; cf.v[1][CF_SHIM] = 127; cf.v[1][CF_DAMP] = 0;
    trk_note_on(&trk[1], 48, 127); trk_note_on(&trk[1], 55, 127); trk_note_on(&trk[1], 64, 127);
    run_rms(4.0, 0);
    trk_note_off(&trk[1], 48); trk_note_off(&trk[1], 55); trk_note_off(&trk[1], 64);
    {
        double peak = 0, r;
        for (k = 0; k < 20u; k++)
            if ((r = run_rms(3.0, 0)) > peak)
                peak = r;
        CHK(peak < 12000.0);
    }
    /* the pages: the selected channel's values, the modes the project's */
    {
        uint32_t pg;
        int16_t *vp;
        song.sel = 2;
        for (pg = 0; pg < NPAGES && !str_eq(PAGES[pg].title, "CH REVERB"); pg++)
            ;
        CHK(pg < NPAGES && PAGES[pg].scope == SC_CHFX && str_eq(page_desc(&PAGES[pg], 0, &vp)->label, "SIZE") &&
            vp == &cf.v[2][CF_SIZE] && str_eq(page_desc(&PAGES[pg], 3, &vp)->label, "REV") && vp == &cf.mode[1]);
        song.sel = 0;
    }
    /* the project keeps them; a FUN10 of Fantasma 1.x (no block): BUS */
    {
        project_t a, b;
        static project_store_t st;
        cf.v[2][CF_TIME] = 7; cf.v[3][CF_SHIM] = 0; cf.v[0][CF_DAMP] = 77;
        al.v[AL_TO] = 2;
        project_capture(&a);
        CHK(proj_pack(&st, &a) && proj_import(&b, &st, sizeof st) && !memcmp(b.chfx, a.chfx, sizeof a.chfx) &&
            b.chfx[2 * CF_PER + CF_TIME] == 7 && b.chfx[NPART * CF_PER] == 1 && b.ambi[AL_TO] == 2);
        {
            uint32_t sum;
            memset(st.raw + PROJ_F2_OFF, 0x10, FM6_PACKED);
            sum = proj_hash(st.raw, PROJ_STORE_SIZE - 4u);
            memcpy(st.raw + PROJ_STORE_SIZE - 4u, &sum, 4);
            CHK(proj_import(&b, &st, sizeof st) && !b.chfx[NPART * CF_PER] && !b.chfx[NPART * CF_PER + 1]);
        }
    }
    /* back to BUS: the line cleared, the bus delay runs again */
    cf.mode[0] = 0;
    chfx_poll();
    CHK(!cf.drun);
    {
        uint32_t i;
        int32_t e = 0;
        for (i = 0; i < DLY_LEN; i++)
            e |= dly_buf[i];
        CHK(!e);
    }
    if (bad) { printf("chfx: %d FAILED\n", bad); return 1; }
    printf("chfx: BUS and CHAN, each channel its delay lane and reverb, tails to silence, never running away, the pages, the project passed.\n");
    return 0;
}
