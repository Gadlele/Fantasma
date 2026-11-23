/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Gadlele */
/* Fantasma 2.0: SEQ > DICE (firmware/src/dice.c): the pages and their values; OCT+ rolls WHO's channels only, WHAT
 * alone (SOUND keeps the generators and the sends, NOTES the sound, FX the sound); KEY NEW one key for all four; OCT-
 * BEFORE and NOW, exactly; EVOLVE rolls by itself every so many bars. Built and run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#define CHK(c) do { if (!(c)) { printf("dice: FAIL line %d: %s\n", __LINE__, #c); bad++; } } while (0)
static int same_p(const track_t *a, const int16_t *b) { return !memcmp(a->p, b, sizeof a->p); }

int main(void)
{
    int bad = 0;
    uint32_t k, i;
    int16_t p0[NTRK][P_COUNT], cf0[NPART][CF_PER];
    step_t d0[NSTEP];
    ui_power_on();
    {   /* the channels as on the device (TRK_DEF) */
        for (k = 0; k < NTRK; k++) {
            set_engine_of(&trk[k], TRK_DEF[k][0]);
            apply_preset_to(&trk[k], TRK_DEF[k][1]);
            trk[k].engine = trk[k].eng_req;
        }
        int16_t v[CF_N];
        chfx_defaults(v, 1);
        chfx_put(v);
        trk[1].p[P_PDEN] = 60; trk[1].p[P_PSEED] = 5;
        trk[2].p[P_E4] = 25; trk[3].p[DG_DENS] = 3;
        dg_write(&trk[3]);
    }
    go_title("DICE");
    CHK(str_eq(cur_page()->title, "DICE") && cur_page()->fam == FAM_SEQ && page_visible(ui.page));
    {
        int16_t *vp;
        CHK(str_eq(page_desc(cur_page(), 0, &vp)->label, "WHO") && vp == &di_v[DI_WHO] &&
            str_eq(page_desc(cur_page(), 3, &vp)->label, "KEY"));
    }
    for (k = 0; k < NTRK; k++)
        memcpy(p0[k], trk[k].p, sizeof p0[k]);
    memcpy(cf0, cf.v, sizeof cf0);
    memcpy(d0, trk[3].step, sizeof d0);
    /* WHO PAD, WHAT SOUND: only PAD's sound moves; its progression, sends, key stay */
    di_v[DI_WHO] = 2; di_v[DI_WHAT] = 1; di_v[DI_AMT] = 100; di_v[DI_KEY] = 0;
    press(B_OCTUP);
    CHK(same_p(&trk[0], p0[0]) && same_p(&trk[2], p0[2]) && same_p(&trk[3], p0[3]) && !same_p(&trk[1], p0[1]));
    CHK(trk[1].p[P_PDEN] == 60 && trk[1].p[P_PSEED] == 5 && trk[1].p[P_REV] == p0[1][P_REV] &&
        trk[1].p[P_ROOT] == p0[1][P_ROOT] && trk[1].eng_req == ENGI_GOLANA && !memcmp(cf.v, cf0, sizeof cf0));
    /* OCT-: BEFORE exactly, again: NOW */
    {
        int16_t now1[P_COUNT];
        memcpy(now1, trk[1].p, sizeof now1);
        press(B_OCTDN);
        CHK(same_p(&trk[1], p0[1]) && msg_is("DICE: BEFORE"));
        press(B_OCTDN);
        CHK(same_p(&trk[1], now1) && msg_is("DICE: NOW"));
        press(B_OCTDN);                                   /* (back to BEFORE for what follows) */
    }
    /* WHAT NOTES on ALL: the generators' seeds, the drums' steps; the sounds stay */
    di_v[DI_WHO] = 0; di_v[DI_WHAT] = 2; di_v[DI_AMT] = 60;
    for (k = 0; k < 4u && trk[1].p[P_PSEED] == 5 && trk[2].p[P_E7] == p0[2][P_E7]; k++)
        press(B_OCTUP);
    CHK(trk[1].p[P_PSEED] != 5 || trk[2].p[P_E7] != p0[2][P_E7]);
    CHK(trk[0].p[P_E0] == p0[0][P_E0] && trk[2].p[P_E0] == p0[2][P_E0] && trk[1].p[P_E1] == p0[1][P_E1]);   /* (sounds kept) */
    {
        int diff = 0;
        for (i = 0; i < 16u; i++)
            diff |= trk[3].step[i].hit != d0[i].hit;
        CHK(diff || trk[3].p[DG_ROT] != p0[3][DG_ROT] || trk[3].p[DG_DENS] != p0[3][DG_DENS]);
    }
    /* WHAT FX on BELLS: its sends or its delay / reverb move, nobody else's */
    memcpy(cf0, cf.v, sizeof cf0);
    for (k = 0; k < NTRK; k++)
        memcpy(p0[k], trk[k].p, sizeof p0[k]);
    di_v[DI_WHO] = 3; di_v[DI_WHAT] = 3; di_v[DI_AMT] = 100;
    press(B_OCTUP);
    CHK(same_p(&trk[0], p0[0]) && same_p(&trk[1], p0[1]) && same_p(&trk[3], p0[3]) &&
        !memcmp(cf.v[0], cf0[0], sizeof cf0[0]) && !memcmp(cf.v[3], cf0[3], sizeof cf0[3]));
    CHK(memcmp(cf.v[2], cf0[2], sizeof cf0[2]) || !same_p(&trk[2], p0[2]));
    CHK(trk[2].p[P_E0] == p0[2][P_E0] && trk[2].p[P_E7] == p0[2][P_E7]);   /* (the sound and the bells as they were) */
    /* KEY NEW: one key for all four */
    di_v[DI_KEY] = 1; di_v[DI_WHAT] = 3; di_v[DI_AMT] = 0;
    for (k = 0; k < 6u; k++) {
        press(B_OCTUP);
        for (i = 1; i < NTRK; i++)
            CHK(trk[i].p[P_ROOT] == trk[0].p[P_ROOT] && trk[i].p[P_SCALE] == trk[0].p[P_SCALE]);
    }
    /* EVOLVE: a roll by itself after its bars (at 120 BPM a bar is 2 s) */
    di_v[DI_KEY] = 0; di_v[DI_WHO] = 2; di_v[DI_WHAT] = 2; di_v[DI_AMT] = 50; di_v[DI_EVOLVE] = 1;
    song.g[G_BPM] = 120;
    dice_poll();
    k = (uint32_t)trk[1].p[P_PSEED];
    {
        uint32_t t0 = fm1_ms, rolled = 0, ev0 = di.ev_ms;
        while (fm1_ms - t0 < 1500u) { frame(); }
        CHK(di.ev_ms == ev0);                             /* (not before the bar) */
        for (i = 0; i < 6u && !rolled; i++) {
            uint32_t t1 = fm1_ms;
            while (fm1_ms - t1 < 500u) { frame(); }
            rolled = di.ev_ms != ev0;
        }
        CHK(rolled);
    }
    di_v[DI_EVOLVE] = 0;
    if (bad) { printf("dice: %d FAILED\n", bad); return 1; }
    printf("dice: WHO and WHAT, SOUND NOTES FX each alone, KEY NEW for all, BEFORE / NOW, EVOLVE passed.\n");
    return 0;
}
