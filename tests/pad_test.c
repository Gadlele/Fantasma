/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The Pad's own sounds (Fantasma; engines.c eng_allowed / pad_preset_ok, ui.c list_keep and ambient_random): the Pad offers
 * ANALOG PHASE VOICE WHEEL PHYS FM6 only (no GRAIN, SAMPLE, TRIO, DRUM), of them the pad-like factory sounds (33), all
 * polyphonic; the Lead keeps every engine; the PRESETS list of the Pad holds exactly those; the ambient machine only
 * picks from them. Built and run by tests/run_tests.sh (without FANTASMA_PAD_ALL, which the other UI tests use). */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#define CHK(c) do { if (!(c)) { printf("pad: FAIL line %d: %s\n", __LINE__, #c); bad++; } } while (0)

int main(void)
{
    int bad = 0;
    uint32_t e, k, n, total, cnt[NENGINES] = {0}, all = 0, seen;
    static const uint8_t OKENG[6] = {0, 2, 5, 7, 9, 12};
    static const uint8_t WANT[NENGINES] = {14, 0, 2, 0, 0, 7, 0, 5, 0, 2, 0, 0, 3, 0};
    ui_power_on();
    for (e = 0; e < NENGINES; e++) {
        uint32_t in = 0, j;
        for (j = 0; j < 6u; j++) in |= OKENG[j] == e;
        if (eng_ok(e) && e != ENGI_BELL && e != ENGI_AMBI && e != ENGI_NOISE) {
            CHK(eng_allowed(0, e) == (int)in);                /* the Pad: only its six */
            CHK(eng_allowed(1, e));                           /* the Lead: everything but bells and noises */
        }
        CHK(!eng_allowed(0, ENGI_BELL) && !eng_allowed(0, ENGI_AMBI));
        for (k = 0; k < ENGINES[e]->npresets; k++)
            if (pad_preset_ok(e, k)) {
                cnt[e]++;
                CHK(in);
                CHK(ENGINES[e]->presets[k].mono == 0);        /* every Pad sound is polyphonic */
            }
        CHK(cnt[e] == WANT[e]);
        all += cnt[e];
    }
    CHK(all == 33);
    CHK(!eng_allowed(0, 8) && !eng_allowed(0, ENGI_DRUM) && !eng_allowed(0, ENGI_SAMPLE));    /* GRAIN is gone from the Pad */
    CHK(eng_allowed(1, 8));                                   /* ...not from the Lead */
    song.sel = 0;
    CHK(preset_pos(&total) <= total && total == all);         /* the Pad's list: exactly these */
    for (n = 0; n < total; n++) {
        uint32_t kk, ee = preset_at(n, &kk);
        CHK(ee < NENGINES && pad_preset_ok(ee, kk));
    }
    for (n = 0, seen = 0; n < 60u; n++) {                     /* the ambient machine: the Pad only plays its own list */
        ambient_random();
        CHK(eng_allowed(0, trk[0].eng_req) && pad_preset_ok(trk[0].eng_req, trk[0].preset) && trk[0].p[P_VOICE] == V_POLY);
        seen |= 1u << trk[0].eng_req;
    }
    CHK(__builtin_popcount(seen) >= 4);                       /* ...and it varies */
    if (bad) { printf("pad: %d FAILED\n", bad); return 1; }
    printf("pad: the Pad's engines and sounds, the list, the ambient machine passed.\n");
    return 0;
}
