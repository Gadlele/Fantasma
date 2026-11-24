/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Gadlele */
/* Fantasma 2.0: ENV > FILT ENV, the filter's own envelope (voice.c fenv_tick). Its ATK at -1 (= ENV, the default and
 * every older sound) the filter follows the sound's envelope bit for bit as before; its own times and SUS shape the
 * filter alone; the page after ENV; a turn at = ENV starts from ENV's four; KNOB 1 down to = ENV follows again; ENV SYNC
 * names its times; a user preset and a project keep it, an older record (no FILT ENV) loads at = ENV. Built and run by
 * tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#define CHK(c) do { if (!(c)) { printf("fenv: FAIL line %d: %s\n", __LINE__, #c); bad++; } } while (0)

/* T1 alone (ANALOG), a note: n frames rendered into o (left), from a fresh start each time */
static void render(int32_t *o, uint32_t n, uint32_t off)
{
    uint32_t b, i, k = 0;
    for (b = 0; b < 44100u / 4u / CTL; b++) {               /* (the power-on's engine switches done) */
        int32_t x[2 * CTL];
        events_block(CTL);
        mix_block(x, CTL);
    }
    trk_note_on(&trk[0], 48, 100);
    for (b = 0; k < n; b++) {
        int32_t x[2 * CTL];
        if (b * CTL == off)
            trk_note_off(&trk[0], 48);
        events_block(CTL);
        mix_block(x, CTL);
        for (i = 0; i < CTL && k < n; i++)
            o[k++] = x[2 * i];
    }
}
static void fresh(void)
{
    uint32_t k;
    ui_power_on();
    song.master_q12 = 4096; kb_boot_hold = 0; song.playing = 0;
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_MUTE] = k != 0u;
    trk[0].p[P_REV] = trk[0].p[P_DLY] = trk[0].p[P_CHOR] = 0;
    trk[0].p[P_ED_FLT] = 50;                                /* the envelope opens the filter a lot */
    trk[0].p[P_ATK] = 0; trk[0].p[P_DEC] = 60; trk[0].p[P_SUS] = 100; trk[0].p[P_REL] = 50;
}
/* brightness: the first differences' energy over the signal's, frames a..b */
static double bright(const int32_t *o, uint32_t a, uint32_t b)
{
    double e = 1e-9, d = 0;
    uint32_t i;
    for (i = a + 1u; i < b; i++) {
        e += (double)o[i] * o[i];
        d += (double)(o[i] - o[i - 1]) * (o[i] - o[i - 1]);
    }
    return d / e;
}

#define N (44100u * 2u)
static int32_t a[N], b[N], c[N];

int main(void)
{
    int bad = 0;
    uint32_t i, pg = NPAGES, env = NPAGES;
    int16_t *vp;
    /* the page, its place, its default */
    ui_power_on();
    for (i = 0; i < NPAGES; i++) {
        if (str_eq(PAGES[i].title, "FILT ENV")) pg = i;
        if (str_eq(PAGES[i].title, "ENV")) env = i;
    }
    CHK(pg < NPAGES && PAGES[pg].fam == FAM_ENV && pg == env + 1u && PAGES[pg].graph == GR_ADSR &&
        PAGES[pg].id[0] == P_FATK && PAGES[pg].id[3] == P_FREL);
    for (i = 0; i < NTRK; i++)
        CHK(trk[i].p[P_FATK] == -1);                         /* every power-on / preset sound: = ENV */
    apply_preset_to(&trk[0], 2);
    CHK(trk[0].p[P_FATK] == -1);
    /* = ENV: the filter follows the sound's envelope (bit for bit as before: tests/regress.c's golden renders, every
     * one at = ENV); FILT ENV set to the same four, its own, sounds the same (the earlier render's tails aside) */
    fresh();
    render(a, N, 44100u);
    fresh();
    trk[0].p[P_FATK] = trk[0].p[P_ATK]; trk[0].p[P_FDEC] = trk[0].p[P_DEC];
    trk[0].p[P_FSUS] = trk[0].p[P_SUS]; trk[0].p[P_FREL] = trk[0].p[P_REL];
    render(b, N, 44100u);
    {
        double e = 0, d = 0;
        for (i = 0; i < N; i++) { e += (double)a[i] * a[i]; d += (double)(a[i] - b[i]) * (a[i] - b[i]); }
        CHK(d < e * 1e-4);
    }
    /* its own: a slow FILT ENV attack keeps the note's start dark while the sound starts at once; SUS 0: darker after */
    fresh();
    trk[0].p[P_FATK] = 110; trk[0].p[P_FDEC] = 60; trk[0].p[P_FSUS] = 100; trk[0].p[P_FREL] = 50;
    render(c, N, 44100u);
    CHK(bright(c, 400, 4410) < bright(a, 400, 4410) * 0.8);
    {
        double l0 = 0, l1 = 0;                               /* (the level the same: the sound's envelope its own) */
        for (i = 400; i < 4410u; i++) { l0 += fabs((double)a[i]); l1 += fabs((double)c[i]); }
        CHK(l1 > l0 * 0.3);
    }
    fresh();
    trk[0].p[P_FATK] = 0; trk[0].p[P_FDEC] = 40; trk[0].p[P_FSUS] = 0; trk[0].p[P_FREL] = 50;
    render(c, N, 44100u);
    CHK(bright(c, 30000, 40000) < bright(a, 30000, 40000) * 0.8);
    /* the page: = ENV on its cards; a turn starts from ENV's four; KNOB 1 down to = ENV follows again */
    ui_power_on();
    go_title("FILT ENV");
    CHK(str_eq(cur_page()->title, "FILT ENV") && page_desc(cur_page(), 0, &vp) && vp == &TSEL->p[P_FATK]);
    TSEL->p[P_ATK] = 20; TSEL->p[P_DEC] = 70; TSEL->p[P_SUS] = 80; TSEL->p[P_REL] = 60;
    turn(EN_K2, 5);                                          /* DEC, at = ENV */
    CHK(TSEL->p[P_FATK] == 20 && TSEL->p[P_FDEC] == 75 && TSEL->p[P_FSUS] == 80 && TSEL->p[P_FREL] == 60);
    turn(EN_K1, -100);
    CHK(TSEL->p[P_FATK] == -1);
    /* ENV SYNC: its times as note values too */
    TSEL->p[P_ESYNC] = 1;
    CHK(page_desc(cur_page(), 1, &vp)->names && page_desc(cur_page(), 2, &vp)->names == 0);
    TSEL->p[P_ESYNC] = 0;
    /* a user preset keeps it; an older record (np 118: no FILT ENV) loads at = ENV */
    TSEL->p[P_FATK] = 33; TSEL->p[P_FDEC] = 44; TSEL->p[P_FSUS] = 55; TSEL->p[P_FREL] = 66;
    CHK(up_store(7, "FENV") != 2 && up_used(7));
    TSEL->p[P_FATK] = -1;
    up_load(7);
    CHK(TSEL->p[P_FATK] == 33 && TSEL->p[P_FDEC] == 44 && TSEL->p[P_FSUS] == 55 && TSEL->p[P_FREL] == 66);
    {
        int16_t in[P_COUNT], out[P_COUNT], def[P_COUNT];
        for (i = 0; i < P_COUNT; i++) { def[i] = TP[i].def; in[i] = (int16_t)(i % 100u); }
        params_by_count(out, in, P_COUNT - 4u, def);        /* (the 8 engine values last) */
        CHK(out[P_FATK] == -1 && out[P_ATSNS] == in[P_ATSNS] && out[P_E0] == in[P_COUNT - 12u]);
    }
    /* a project keeps it */
    {
        project_t q, r;
        static project_store_t st;
        trk[2].p[P_FATK] = 90; trk[2].p[P_FSUS] = 3;
        project_capture(&q);
        CHK(proj_pack(&st, &q) && proj_import(&r, &st, sizeof st) && r.t[2].p[P_FATK] == 90 && r.t[2].p[P_FSUS] == 3 &&
            r.t[1].p[P_FATK] == -1);
    }
    if (bad) { printf("fenv: %d FAILED\n", bad); return 1; }
    printf("fenv: = ENV bit for bit as before, its own attack / sustain on the filter, the page, a user preset, a project passed.\n");
    return 0;
}
