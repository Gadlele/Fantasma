/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Gadlele */
/* Fantasma 2.0: the DRUMS' generator (firmware/src/drum_gen.c): EUCLID and MUTATE on a DRUM track only; DENS writes the
 * kit live from KNOB 1 (OFF: the steps are the user's), LANE one lane alone, LEN each track's own; OCT+ DICE, OCT- back to
 * the pattern dialled; MUTATE moves the pattern on the track's loops while playing, ORBIT round the anchor; a kit change
 * keeps the settings; the ambient kits. Built and run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#define CHK(c) do { if (!(c)) { printf("drum_gen: FAIL line %d: %s\n", __LINE__, #c); bad++; } } while (0)
static uint32_t lane_hits(const track_t *t, uint32_t l, uint32_t from, uint32_t to)
{
    uint32_t i, n = 0;
    for (i = from; i < to; i++)
        n += (t->step[i].hit >> l) & 1u;
    return n;
}
static uint32_t all_hits(const track_t *t, uint32_t len)
{
    uint32_t l, n = 0;
    for (l = 0; l < NLANE; l++)
        n += lane_hits(t, l, 0, len);
    return n;
}
static uint32_t all_hits_of(const step_t *s, uint32_t len)
{
    uint32_t i, n = 0;
    for (i = 0; i < len; i++)
        n += (uint32_t)__builtin_popcount(s[i].hit);
    return n;
}
static int same_steps(const track_t *t, const step_t *s, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++)
        if (t->step[i].hit != s[i].hit)
            return 0;
    return 1;
}

int main(void)
{
    int bad = 0;
    uint32_t i, k, d;
    track_t *t = &trk[3];
    step_t before[NSTEP];
    ui_power_on();
    song.sel = 3;
    CHK(drum_track(t));
    go_title("EUCLID");
    CHK(str_eq(cur_page()->title, "EUCLID") && page_visible(ui.page));
    {   /* the labels and the defaults; not on another engine */
        int16_t *vp;
        CHK(str_eq(page_desc(cur_page(), 0, &vp)->label, "DENS") && vp == &t->p[DG_DENS] && !t->p[DG_DENS]);
        CHK(str_eq(page_desc(cur_page(), 1, &vp)->label, "LEN") && vp == &t->p[P_SLEN] && t->p[P_SLEN] == 16);
        song.sel = 0;
        CHK(!page_visible(ui.page));
        song.sel = 3;
    }
    /* DENS OFF: nothing written; KNOB 1 to 4: the kit, live */
    memcpy(before, t->step, sizeof before);
    turn(EN_K3, 3);                                       /* ROT with DENS OFF: the steps stay */
    CHK(same_steps(t, before, NSTEP));
    t->p[DG_ROT] = 0;
    turn(EN_K1, 4);
    CHK(t->p[DG_DENS] == 4 && (t->step[0].hit & 1u));     /* the kick on 1 */
    for (k = 0; k < NLANE; k++)
        CHK(lane_hits(t, k, 0, 16) == tl_per(DG_KIT[3][k], 16));
    CHK(lane_hits(t, DV_SNARE, 4, 5) == 1u);              /* the backbeat */
    for (d = 4; d < 8u; d++) {                            /* busier as DENS rises */
        uint32_t n0 = all_hits(t, 16);
        turn(EN_K1, 1);
        CHK(all_hits(t, 16) > n0);
    }
    memcpy(before, t->step, sizeof before);               /* (DENS 8) */
    hold(B_SAVE);                                         /* one undo a rewrite (SAVE held): DENS 7's steps */
    CHK(!same_steps(t, before, 16) && all_hits(t, 16) < all_hits_of(before, 16));
    /* LEN: each track its own, the steps past it untouched */
    t->p[DG_DENS] = 3;
    for (i = 0; i < NSTEP; i++)
        step_clear(&t->step[i]);
    t->step[40].hit = 1u << DV_TOM;
    t->step[40].time = ST_NOTE;
    turn(EN_K2, 8);                                       /* LEN 24 */
    CHK(t->p[P_SLEN] == 24 && trk[0].p[P_SLEN] == 16 && lane_hits(t, DV_KICK, 0, 24) == tl_per(2, 24) &&
        lane_hits(t, DV_TOM, 40, 41) == 1u && !lane_hits(t, DV_KICK, 24, 40));
    /* LANE: one lane alone, DENS its eighths of LEN */
    memcpy(before, t->step, sizeof before);
    turn(EN_K4, 1 + DV_HATC);                             /* HATCL */
    CHK(t->p[DG_LANE] == 1 + DV_HATC && lane_hits(t, DV_HATC, 0, 24) == (3u * 24u + 7u) / 8u && (t->step[0].hit & 1u));
    for (i = 0; i < 24u; i++)                             /* the other lanes as they were */
        CHK((t->step[i].hit & ~(1u << DV_HATC)) == (before[i].hit & ~(1u << DV_HATC)));
    t->p[DG_LANE] = 0;
    t->p[P_SLEN] = 16;
    turn(EN_K1, 1);                                       /* DENS 4, the kit again: the anchor */
    memcpy(before, t->step, sizeof before);
    /* OCT+ DICE, OCT- back */
    for (k = 0; k < 5u && same_steps(t, before, 16); k++)
        press(B_OCTUP);
    CHK(!same_steps(t, before, 16) && song.octave == 0);  /* (no octave on these pages) */
    press(B_OCTDN);
    CHK(same_steps(t, before, 16) && song.octave == 0);
    /* MUTATE: on the loops while playing, ORBIT round the anchor (never far), the kick on 1 stays */
    go_title("MUTATE");
    CHK(str_eq(cur_page()->title, "MUTATE") && page_visible(ui.page));
    t->p[DG_EVERY] = 1;                                   /* every loop */
    t->p[DG_AMT] = 50;
    {
        uint32_t changed = 0, n0 = all_hits(t, 16), far = 0;
        song.playing = 1;
        for (k = 0; k < 12u; k++) {
            dg_loops[3]++;                                /* (a loop of the track comes round) */
            dg_poll();
            changed += !same_steps(t, before, 16);
            far |= all_hits(t, 16) > n0 + 8u || all_hits(t, 16) + 8u < n0;
            CHK(t->step[0].hit & 1u);
        }
        song.playing = 0;
        CHK(changed >= 10u && !far);
        press(B_OCTDN);
        CHK(same_steps(t, before, 16));
        t->p[DG_EVERY] = 0;
        for (k = 0; k < 4u; k++) {                        /* EVERY OFF: no mutation */
            dg_loops[3]++;
            dg_poll();
        }
        CHK(same_steps(t, before, 16));
    }
    /* a kit change keeps the settings; another engine and back: the defaults */
    t->p[DG_DENS] = 5; t->p[DG_FEEL] = 90; t->p[DG_EVERY] = 3;
    apply_preset_to(t, 4);                                /* SOFT KIT */
    CHK(t->p[DG_DENS] == 5 && t->p[DG_FEEL] == 90 && t->p[DG_EVERY] == 3 && str_eq(ENGINES[ENGI_DRUM]->presets[4].name, "SOFT KIT"));
    set_engine_of(t, 0);
    set_engine_of(t, ENGI_DRUM);
    CHK(!t->p[DG_DENS] && !t->p[DG_EVERY] && t->p[DG_FEEL] == DG_P[6].def && t->p[DG_AMT] == DG_P[4].def);
    CHK(ENGINES[ENGI_DRUM]->npresets == 10u);             /* the four kits and the six ambient ones */
    if (bad) { printf("drum_gen: %d FAILED\n", bad); return 1; }
    printf("drum_gen: EUCLID live (DENS, LEN, ROT, LANE), DICE and back, MUTATE on the loops, the settings through a kit change passed.\n");
    return 0;
}
