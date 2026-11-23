/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Gadlele, based on Fantasma 1.x's (Leo Kuroshita's Felucca tests) */
/* Fantasma 2.0's four channels (engines.c eng_allowed / part_preset_ok / ROLE_NAME / TRK_DEF, ui.c list_keep and
 * ambient_random): DRONE plays DRONE, PAD ANALOG (until Golana's), BELLS BELL, DRUMS DRUM, nothing else; DRONE's
 * list its 12 (and its FILTER page), PAD's ANALOG's pads and soft leads (21); PRESETS lists exactly those; the power-on
 * sounds; the ambient machine picks from them, holds a DRONE note at INF, switches PAD's progression, BELLS' bells and
 * the AMBI layer on. Built and run by tests/run_tests.sh (without FANTASMA_PAD_ALL, which the other UI tests use). */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#define CHK(c) do { if (!(c)) { printf("channels: FAIL line %d: %s\n", __LINE__, #c); bad++; } } while (0)

int main(void)
{
    int bad = 0;
    uint32_t e, k, n, ti, total, cnt = 0;
    ui_power_on();
    CHK(str_eq(ROLE_NAME[0], "DRONE") && str_eq(ROLE_NAME[1], "PAD") && str_eq(ROLE_NAME[2], "BELLS") &&
        str_eq(ROLE_NAME[3], "DRUMS"));
    for (e = 0; e < NENGINES; e++) {                          /* each channel its one engine */
        CHK(eng_allowed(0, e) == (e == ENGI_DRONE));
        CHK(eng_allowed(1, e) == (e == ENGI_GOLANA));
        CHK(eng_allowed(2, e) == (e == ENGI_CHIME));
        CHK(eng_allowed(3, e) == (e == ENGI_DRUM));
    }
    for (k = 0; k < ENGINES[ENGI_GOLANA]->npresets; k++)      /* PAD: every GOLANA sound, pads and leads */
        cnt += part_preset_ok(1, ENGI_GOLANA, k);
    CHK(cnt == 16u && str_eq(ENGINES[ENGI_GOLANA]->name, "GOLANA") && ENGINES[ENGI_GOLANA]->npresets == 16u);
    for (k = 0; k < ENGINES[ENGI_DRONE]->npresets; k++)       /* DRONE: every DRONE sound */
        CHK(part_preset_ok(0, ENGI_DRONE, k));
    CHK(str_eq(ENGINES[ENGI_DRONE]->name, "DRONE") && ENGINES[ENGI_DRONE]->npresets == 12u);
    {   /* DRONE's FILTER page: CUT RES KTR TYPE, on a DRONE track only; each sound sets them; CLEAN INIT the defaults */
        uint32_t pg, seen = 0;
        track_t *t = &trk[0];
        int16_t *vp;
        song.sel = 0;
        set_engine_of(t, ENGI_DRONE);
        apply_preset_to(t, 4);                                /* HOLLOW: a band */
        CHK(str_eq(track_desc(t, P_DCUT)->label, "CUT") && str_eq(track_desc(t, P_DRES)->label, "RES") &&
            str_eq(track_desc(t, P_DKTR)->label, "KTR") && track_desc(t, P_DCUT)->fmt == F_CUTOFF);
        CHK(t->p[P_DCUT] == DR_FILT[4][0] && t->p[P_DRES] == DR_FILT[4][1] && t->p[P_DKTR] == DR_FILT[4][2] &&
            t->p[P_FTYPE] == 1);
        for (pg = 0; pg < NPAGES; pg++)
            if (PAGES[pg].id[0] == P_DCUT && PAGES[pg].id[3] == P_FTYPE && page_visible(pg) &&
                page_desc(&PAGES[pg], 0, &vp) == &DR_FILT_P[0] && vp == &t->p[P_DCUT])
                seen++;
        CHK(seen == 1u);
        clean_engine_of(t);
        CHK(t->p[P_DCUT] == DR_FILT_P[0].def && t->p[P_DRES] == DR_FILT_P[1].def && t->p[P_DKTR] == DR_FILT_P[2].def &&
            !t->p[P_FTYPE]);
        set_engine_of(t, 0);                                  /* another engine: no DRONE FILTER page, its own labels */
        for (pg = 0, seen = 0; pg < NPAGES; pg++)
            seen += PAGES[pg].id[0] == P_DCUT && PAGES[pg].id[3] == P_FTYPE && page_visible(pg);
        CHK(!seen && !str_eq(track_desc(t, P_DCUT)->label, "CUT"));
        set_engine_of(t, ENGI_DRONE);
    }
    {   /* GOLANA's FILTER and TONE pages, on a GOLANA track only; each sound sets them (and its LFO, its voice);
         * CLEAN INIT the defaults; KTR keeps its sign through a project */
        uint32_t pg, seen = 0;
        track_t *t = &trk[1];
        int16_t *vp;
        song.sel = 1;
        set_engine_of(t, ENGI_GOLANA);
        apply_preset_to(t, 13);                               /* SUPERSAW: UNISON, wide */
        CHK(str_eq(track_desc(t, GO_FTYPE)->label, "TYPE") && str_eq(track_desc(t, GO_CUT)->label, "CUT") &&
            str_eq(track_desc(t, GO_DRIFT)->label, "DRIFT") && track_desc(t, GO_KTR)->min == -63);
        CHK(t->p[GO_CUT] == GO_EXTRA[13].f[1] && t->p[GO_SUBTYP] == GO_EXTRA[13].f[5] && t->p[P_VOICE] == V_UNISON &&
            t->p[P_SPRD] == 127);
        for (pg = 0; pg < NPAGES; pg++)
            if (page_visible(pg) && PAGES[pg].scope == SC_TRACK && PAGES[pg].graph == GR_NONE &&
                (page_desc(&PAGES[pg], 0, &vp) == &GO_P[0] || page_desc(&PAGES[pg], 0, &vp) == &GO_P[4]))
                seen++;
        CHK(seen == 2u);
        CHK(param_desc_of(ENGI_GOLANA, GO_KTR)->min == -63 && param_fit(param_desc_of(ENGI_GOLANA, GO_KTR), -20) == -20);
        clean_engine_of(t);
        CHK(t->p[GO_CUT] == GO_P[1].def && t->p[GO_OSC1] == 127 && t->p[GO_KTR] == GO_P[3].def);
        apply_preset_to(t, 0);
        song.sel = 0;
    }
    {   /* CHIME's TONE page, on a CHIME track only; each sound sets it; its GEN is BELL's generator */
        uint32_t pg, seen = 0;
        track_t *t = &trk[2];
        int16_t *vp;
        song.sel = 2;
        set_engine_of(t, ENGI_CHIME);
        apply_preset_to(t, 3);                                /* BOWL */
        CHK(str_eq(track_desc(t, CH_ENV)->label, "ENV") && str_eq(track_desc(t, CH_BREATH)->label, "BREATH") &&
            t->p[CH_BREATH] == CH_TONEV[3][2] && t->p[CH_TONE] == CH_TONEV[3][3]);
        for (pg = 0; pg < NPAGES; pg++)
            if (page_visible(pg) && PAGES[pg].scope == SC_TRACK && page_desc(&PAGES[pg], 0, &vp) == &CH_P[0])
                seen++;
        CHK(seen == 1u && home_id(t, 0) == P_E4 && home_id(t, 1) == P_E5);   /* HOME: DENS EVOLVE SCL REV, as BELL's */
        t->p[P_E4] = 30;
        CHK(gen_on(t));
        t->p[P_E4] = 0;
        CHK(!gen_on(t));
        clean_engine_of(t);
        CHK(t->p[CH_ENV] == CH_P[0].def && t->p[CH_TONE] == CH_P[3].def);
        apply_preset_to(t, 0);
        song.sel = 0;
    }
    CHK(TRK_DEF[0][0] == ENGI_DRONE && TRK_DEF[0][1] == 0u && TRK_DEF[1][0] == ENGI_GOLANA && TRK_DEF[1][1] == 0u &&
        TRK_DEF[2][0] == ENGI_CHIME && TRK_DEF[3][0] == ENGI_DRUM);   /* the power-on sounds (main.c; the tests' own differ) */
    for (ti = 0; ti < 4u; ti++)                               /* (each channel on its own engine, as the power-on) */
        if (!eng_allowed(ti, trk[ti].eng_req)) {
            set_engine_of(&trk[ti], TRK_DEF[ti][0]);
            apply_preset_to(&trk[ti], TRK_DEF[ti][1]);
            trk[ti].engine = trk[ti].eng_req;
        }
    for (ti = 0; ti < 4u; ti++) {                             /* PRESETS: each channel's own list, exactly */
        song.sel = (uint8_t)ti;
        CHK(preset_pos(&total) <= total);
        CHK(total == ENGINES[ti == 0u ? ENGI_DRONE : ti == 1u ? ENGI_GOLANA : ti == 2u ? ENGI_CHIME : ENGI_DRUM]->npresets);
        for (n = 0; n < total; n++) {
            uint32_t kk, ee = preset_at(n, &kk);
            CHK(eng_allowed(ti, ee) && part_preset_ok(ti, ee, kk));
        }
    }
    song.sel = 0;
    for (n = 0; n < 30u; n++) {                               /* the ambient machine */
        ambient_random();
        CHK(trk[0].eng_req == ENGI_DRONE && part_preset_ok(0, ENGI_DRONE, trk[0].preset) && trk[0].p[P_REL] == 127 && noise_inf(&trk[0]));
        CHK(trk[1].eng_req == ENGI_GOLANA && part_preset_ok(1, ENGI_GOLANA, trk[1].preset) && trk[1].p[P_VOICE] == V_POLY &&
            trk[1].p[P_PDEN] > 0);
        CHK(trk[2].eng_req == ENGI_CHIME && trk[2].p[P_E4] > 0);
        CHK(al.v[AL_ON] == 1 && al.v[AL_SCENE] < (int16_t)AL_NSCENE);
        CHK(trk[3].eng_req == ENGI_DRUM && trk[3].p[P_ROOT] == trk[0].p[P_ROOT] && trk[3].p[P_SCALE] == trk[0].p[P_SCALE]);
    }
    ambient_stop();
    CHK(!trk[1].p[P_PDEN] && !trk[2].p[P_E4] && !noise_inf(&trk[0]) && !al.v[AL_ON]);
    if (bad) { printf("channels: %d FAILED\n", bad); return 1; }
    printf("channels: DRONE PAD BELLS DRUMS, one engine each, their lists, the power-on sounds, the ambient machine passed.\n");
    return 0;
}
