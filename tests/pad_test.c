/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Gadlele, based on Fantasma 1.x's (Leo Kuroshita's Felucca tests) */
/* Fantasma 2.0's four channels (engines.c eng_allowed / part_preset_ok / ROLE_NAME / TRK_DEF, ui.c list_keep and
 * ambient_random): DRONE and PAD play ANALOG (until their own engines come), BELLS BELL, DRUMS DRUM, nothing else; DRONE's
 * list the long, still sounds (7), PAD's the pads and the soft leads (21); PRESETS lists exactly those; the power-on
 * sounds; the ambient machine picks from them, holds a DRONE note at INF, switches PAD's progression, BELLS' bells and
 * the AMBI layer on. Built and run by tests/run_tests.sh (without FANTASMA_PAD_ALL, which the other UI tests use). */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#define CHK(c) do { if (!(c)) { printf("channels: FAIL line %d: %s\n", __LINE__, #c); bad++; } } while (0)

int main(void)
{
    int bad = 0;
    uint32_t e, k, n, ti, total, cnt[2] = {0, 0};
    static const uint32_t WANT[2] = {7u, 21u};
    ui_power_on();
    CHK(str_eq(ROLE_NAME[0], "DRONE") && str_eq(ROLE_NAME[1], "PAD") && str_eq(ROLE_NAME[2], "BELLS") &&
        str_eq(ROLE_NAME[3], "DRUMS"));
    for (e = 0; e < NENGINES; e++) {                          /* each channel its one engine */
        CHK(eng_allowed(0, e) == (e == 0u));
        CHK(eng_allowed(1, e) == (e == 0u));
        CHK(eng_allowed(2, e) == (e == ENGI_BELL));
        CHK(eng_allowed(3, e) == (e == ENGI_DRUM));
    }
    for (ti = 0; ti < 2u; ti++)
        for (k = 0; k < ENGINES[0]->npresets; k++)
            cnt[ti] += part_preset_ok(ti, 0, k);
    CHK(cnt[0] == WANT[0] && cnt[1] == WANT[1]);
    CHK(part_preset_ok(0, 0, 13) && part_preset_ok(1, 0, 18));   /* DRONE, WARM PAD */
    CHK(TRK_DEF[0][0] == 0u && TRK_DEF[0][1] == 13u && TRK_DEF[1][0] == 0u && TRK_DEF[1][1] == 18u &&
        TRK_DEF[2][0] == ENGI_BELL && TRK_DEF[3][0] == ENGI_DRUM);   /* the power-on sounds (main.c; the tests' own differ) */
    for (ti = 0; ti < 4u; ti++)                               /* (each channel on its own engine, as the power-on) */
        if (!eng_allowed(ti, trk[ti].eng_req)) {
            set_engine_of(&trk[ti], TRK_DEF[ti][0]);
            apply_preset_to(&trk[ti], TRK_DEF[ti][1]);
            trk[ti].engine = trk[ti].eng_req;
        }
    for (ti = 0; ti < 4u; ti++) {                             /* PRESETS: each channel's own list, exactly */
        song.sel = (uint8_t)ti;
        CHK(preset_pos(&total) <= total);
        CHK(total == (ti < 2u ? WANT[ti] : ENGINES[ti == 2u ? ENGI_BELL : ENGI_DRUM]->npresets));
        for (n = 0; n < total; n++) {
            uint32_t kk, ee = preset_at(n, &kk);
            CHK(eng_allowed(ti, ee) && part_preset_ok(ti, ee, kk));
        }
    }
    song.sel = 0;
    for (n = 0; n < 30u; n++) {                               /* the ambient machine */
        ambient_random();
        CHK(trk[0].eng_req == 0u && part_preset_ok(0, 0, trk[0].preset) && trk[0].p[P_REL] == 127 && noise_inf(&trk[0]));
        CHK(trk[1].eng_req == 0u && part_preset_ok(1, 0, trk[1].preset) && trk[1].p[P_VOICE] == V_POLY &&
            trk[1].p[P_PDEN] > 0);
        CHK(trk[2].eng_req == ENGI_BELL && trk[2].p[P_E4] > 0);
        CHK(al.v[AL_ON] == 1 && al.v[AL_SCENE] < (int16_t)AL_NSCENE);
        CHK(trk[3].eng_req == ENGI_DRUM && trk[3].p[P_ROOT] == trk[0].p[P_ROOT] && trk[3].p[P_SCALE] == trk[0].p[P_SCALE]);
    }
    ambient_stop();
    CHK(!trk[1].p[P_PDEN] && !trk[2].p[P_E4] && !noise_inf(&trk[0]) && !al.v[AL_ON]);
    if (bad) { printf("channels: %d FAILED\n", bad); return 1; }
    printf("channels: DRONE PAD BELLS DRUMS, one engine each, their lists, the power-on sounds, the ambient machine passed.\n");
    return 0;
}
