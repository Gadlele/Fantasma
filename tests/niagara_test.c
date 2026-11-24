/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Gadlele */
/* Fantasma 2.0: NIAGARA, Niagara Drift's drones on the DRONE channel (firmware/src/eng_niagara.c, niagara/): the values'
 * ranges and defaults are drone.c's; the power-on DRONE is NIAGARA; a key plays it (stereo: drone I left, II right), the
 * ENV lets it go; all sixteen Presets sound; its pages show on a NIAGARA track only, the ones it has no use for not; a
 * value turned reaches drone.c; a project and a user preset keep all 70 values; a project without the block: the
 * defaults. Built and run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#define CHK(c) do { if (!(c)) { printf("niagara: FAIL line %d: %s\n", __LINE__, #c); bad++; } } while (0)
static void nblocks(double sec, double *rms, double *side)
{
    uint32_t b, i, n = 0;
    double e = 0, d = 0;
    for (b = 0; b < (uint32_t)(44100.0 * sec / CTL); b++) {
        int32_t o[2 * CTL];
        if ((b & 15u) == 0u)
            nia_poll();                              /* (the UI pass) */
        events_block(CTL);
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++, n++) {
            e += (double)o[2 * i] * o[2 * i];
            d += (double)(o[2 * i] - o[2 * i + 1]) * (o[2 * i] - o[2 * i + 1]);
        }
    }
    if (rms) *rms = sqrt(e / n);
    if (side) *side = sqrt(d / n);
}
static void niagara_on(void)
{
    uint32_t k;
    ui_power_on();
    song.master_q12 = 4096; kb_boot_hold = 0;
    for (k = 1; k < NTRK; k++)
        trk[k].p[P_MUTE] = 1;
    song.sel = 0;
    set_engine_of(&trk[0], ENGI_NIAGARA);
    apply_preset_to(&trk[0], 0);
    nblocks(0.2, 0, 0);                               /* (the engine's switch done) */
}

int main(void)
{
    int bad = 0;
    uint32_t k, pg;
    double r, s;
    int16_t lo, hi, def, *vp;
    /* the values: drone.c's ranges and defaults; the engine's eight its EDIT values */
    for (k = 0; k < NK_N; k++) {
        nia_range(k, &lo, &hi, &def);
        CHK(NIA_P[k].min == lo && NIA_P[k].max == hi && NIA_P[k].def == def);
        if (k < 8u)
            CHK(ENG_NIAGARA.edit[k].min == lo && ENG_NIAGARA.edit[k].max == hi && ENG_NIAGARA.edit[k].def == def);
    }
    CHK(TRK_DEF[0][0] == ENGI_NIAGARA && eng_allowed(0, ENGI_NIAGARA) && !eng_allowed(1, ENGI_NIAGARA) &&
        str_eq(ENGINES[ENGI_NIAGARA]->name, "NIAGARA") && ENG_NIAGARA.npresets == nia_npresets());
    for (k = 0; k < ENG_NIAGARA.npresets; k++)
        CHK(str_eq(ENG_NIAGARA.presets[k].name, nia_preset_name(k)));
    /* a key plays it, in stereo; let go, the ENV ends it */
    niagara_on();
    nblocks(0.3, &r, 0);
    CHK(r == 0.0);
    trk_note_on(&trk[0], 50, 100);
    nblocks(2.0, &r, &s);
    CHK(r > 1000.0 && s > 500.0);
#if FELUCCA_CPU2
    part_cost[0] = 1600; part_cost[1] = part_cost[2] = part_cost[3] = 300;   /* (the split's pick: part 1 alone) */
    CHK(!(mix_split() & 1u) && mix_split());          /* NIAGARA never handed to the second core (fx.c); the rest are */
#endif
    trk_note_off(&trk[0], 50);
    nblocks(12.0, 0, 0);
    nblocks(0.5, &r, 0);
    CHK(r < 5.0);
    /* every Preset sounds */
    for (k = 0; k < ENG_NIAGARA.npresets; k++) {
        apply_preset_to(&trk[0], k);
        nblocks(0.2, 0, 0);
        trk_note_on(&trk[0], 50, 100);
        nblocks(1.5, &r, 0);
        CHK(r > 500.0);
        trk_note_off(&trk[0], 50);
        nblocks(10.0, 0, 0);
    }
    /* the pages: NIAGARA's on a NIAGARA track, the track's LFO / ENV DEST / VOICE pages not; on another, the other way */
    niagara_on();
    for (pg = 0; pg < NPAGES; pg++) {
        if (PAGES[pg].scope == SC_NIA)
            CHK(page_visible(pg));
        if (PAGES[pg].graph == GR_LFO || str_eq(PAGES[pg].title, "ENV DEST") || str_eq(PAGES[pg].title, "VOICE"))
            CHK(!page_visible(pg));
    }
    CHK(page_first(FAM_EDIT) == page_first(FAM_EDIT) && str_eq(ENG_NIAGARA.page_title[0], "DRONE I"));
    go_title("FILTER I");
    CHK(str_eq(cur_page()->title, "FILTER I") && page_desc(cur_page(), 0, &vp) && vp == &nia_v[NK_CUT1]);
    turn(EN_K1, 10);
    nia_poll();
    nblocks(0.01, 0, 0);                              /* (a block: drone.c takes the command) */
    CHK(nia_v[NK_CUT1] != NIA_P[NK_CUT1].def && nia_get(NK_CUT1) == nia_v[NK_CUT1]);
    set_engine_of(&trk[0], 0);
    for (pg = 0; pg < NPAGES; pg++)
        if (PAGES[pg].scope == SC_NIA)
            CHK(!page_visible(pg));
    /* a project keeps all 70; one without the block: the defaults */
    niagara_on();
    apply_preset_to(&trk[0], 5);
    trk[0].p[P_E0] = 44;
    nia_v[NK_CUT2] = 777; nia_v[NK_IIOFS] = -1234; nia_v[NK_PSHAPE] = -99; nia_v[NK_LRATE] = 150;
    {
        project_t a, b;
        static project_store_t st;
        int16_t keep[NK_N];
        memcpy(keep, nia_v, sizeof keep);
        project_capture(&a);
        CHK(proj_pack(&st, &a) && proj_import(&b, &st, sizeof st) && !memcmp(b.nia, keep, sizeof keep) &&
            b.t[0].engine == ENGI_NIAGARA && b.t[0].p[P_E0] == 44);
        nia_defaults();
        project_restore_runtime(&b);
        CHK(!memcmp(nia_v, keep, sizeof keep));
        st.raw[PROJ_FM6_OFF] = 0;                    /* (no block) */
        {
            uint32_t sum = proj_hash(st.raw, PROJ_STORE_SIZE - 4u);
            memcpy(st.raw + PROJ_STORE_SIZE - 4u, &sum, 4);
        }
        CHK(proj_import(&b, &st, sizeof st) && b.nia[NK_CUT2] == NIA_P[NK_CUT2].def);
    }
    /* a user preset keeps them */
    {
        int16_t keep[NK_N];
        memcpy(keep, nia_v, sizeof keep);
        CHK(up_store(9, "NIA") != 2 && up_used(9));
        nia_defaults();
        up_load(9);
        CHK(!memcmp(nia_v + 8, keep + 8, sizeof keep - 16u) && trk[0].eng_req == ENGI_NIAGARA);
    }
    if (bad) { printf("niagara: %d FAILED\n", bad); return 1; }
    printf("niagara: drone.c's ranges, the power-on DRONE, a key in stereo, the sixteen Presets, the pages, a value to "
           "drone.c, a project and a user preset passed.\n");
    return 0;
}
