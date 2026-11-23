/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Gadlele */
/* Fantasma 2.0: SEQ > DICE, the dice of the whole thing, a page of its own (after AMBIENT; JIANT's dice in mind).
 *   DICE    WHO (ALL, DRONE, PAD, BELLS, DRUMS, DRN+PAD, PAD+BEL, NO DRUMS: the channels rolled), WHAT (ALL, SOUND,
 *           NOTES, FX), AMOUNT (how far a roll moves things), KEY (KEEP: the channels' ROOT and SCALE stay; NEW: one new
 *           key for all four, the drone retuned)
 *   DICE 2  EVOLVE (OFF, every 1 2 4 8 16 bars at the project's BPM: a roll by itself, KEY kept; with the transport
 *           stopped too: the generators play)
 * OCT+ rolls. OCT- is BEFORE / NOW: the four channels as they were before the last roll, and again as it left them
 * (each press the other). What a roll moves, on the channels WHO names:
 *   SOUND   AMOUNT % of the time another of the channel's sounds; then each of the engine's values (and its own pages':
 *           DRONE's FILTER, GOLANA's FILTER and TONE, CHIME's TONE) moved by up to AMOUNT % of its range (a name, as
 *           often). The generators' settings, the sends and the key stay
 *   NOTES   DRONE a new note of the scale (at INF); PAD's progression a new SEED, its EVOLVE and BARS moved; BELLS' a
 *           new SEED, EVOLVE and RANGE moved; DRUMS' EUCLID (DENS above 0) DENS and ROT moved and written, else SEQ
 *           TOOLS' RANDOM on every lane
 *   FX      the sends (CHO DLY REV) and the channel's delay and reverb (chfx.c) moved; now and then (AMOUNT / 2 %) the
 *           INSERT another modulation (PHASR FLANG CHOR) or none
 * Not saved with the project (the page's settings and BEFORE are gone at power-off: what they made is in the tracks) */
enum { DI_WHO, DI_WHAT, DI_AMT, DI_KEY, DI_EVOLVE, DI_N };
static const char *const N_DI_WHO[] = {"ALL", "DRONE", "PAD", "BELLS", "DRUMS", "DRN+PAD", "PAD+BEL", "NO DRUMS", 0};
static const uint8_t DI_MASK[8] = {0xF, 1, 2, 4, 8, 3, 6, 7};
static const char *const N_DI_WHAT[] = {"ALL", "SOUND", "NOTES", "FX", 0};
static const char *const N_DI_KEY[] = {"KEEP", "NEW", 0};
static const char *const N_DI_EV[] = {"OFF", "1 BAR", "2 BARS", "4 BARS", "8 BARS", "16 BARS", 0};
static const uint8_t DI_EV_BARS[6] = {0, 1, 2, 4, 8, 16};
static const param_desc_t DI_P[DI_N] = {
    {"WHO", F_ENUM, 0, 7, 0, N_DI_WHO, 0},
    {"WHAT", F_ENUM, 0, 3, 0, N_DI_WHAT, 0},
    {"AMOUNT", F_INT, 0, 100, 35, 0, "%"},
    {"KEY", F_ENUM, 0, 1, 0, N_DI_KEY, 0},
    {"EVOLVE", F_ENUM, 0, 5, 0, N_DI_EV, 0},
};
static int16_t di_v[] = {0, 0, 35, 0, 0};
_Static_assert(sizeof di_v / sizeof di_v[0] == DI_N, "a value each");
static struct {                                    /* BEFORE (or NOW, after OCT-): the four channels' other state */
    int16_t p[NTRK][P_COUNT];
    uint8_t preset[NTRK];
    step_t drum[NSTEP];                            /* DRUMS' steps (NOTES may write them) */
    int16_t cf[NPART][CF_PER];
    uint8_t valid, now;                            /* a roll was made; showing NOW (1) or BEFORE (0) */
    uint32_t ev_ms;                                /* EVOLVE: when the last roll was */
} di __attribute__((section(".pool")));
static int16_t *dice_val(uint32_t id) { return &di_v[id % DI_N]; }
static const param_desc_t *dice_pdesc(uint32_t id) { return &DI_P[id % DI_N]; }
static int dice_page(const page_t *pg) { return pg->scope == SC_DICE; }

static uint32_t di_rnd(uint32_t n) { return n ? rng() % n : 0u; }
/* value id of track t moved by up to amt % of its range (a name: another one, amt % of the time) */
static void di_nudge(track_t *t, uint32_t id, uint32_t amt)
{
    const param_desc_t *d = track_desc(t, id);
    int32_t span, v;
    if (!d || d->max <= d->min || !d->label || d->label[0] == '-')
        return;
    span = d->max - d->min;
    if (d->fmt == F_ENUM) {
        if (di_rnd(100) < amt)
            t->p[id] = (int16_t)(d->min + (int32_t)di_rnd((uint32_t)span + 1u));
        return;
    }
    v = span * (int32_t)amt / 100;
    v = v ? (int32_t)di_rnd((uint32_t)(2 * v + 1)) - v : 0;
    t->p[id] = (int16_t)clamp(t->p[id] + v, d->min, d->max);
}

static void di_take(void)                          /* BEFORE: the channels as they are now */
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        memcpy(di.p[i], trk[i].p, sizeof di.p[i]);
        di.preset[i] = trk[i].preset;
    }
    memcpy(di.drum, trk[3].step, sizeof di.drum);
    memcpy(di.cf, cf.v, sizeof di.cf);
    di.valid = 1;
    di.now = 1;
}
static void di_drone_tune(track_t *t)              /* DRONE at INF: its note again, on its ROOT and SCALE */
{
    if (!noise_inf(t))
        return;
    fm1_irq_off();
    seq_release(t);
    ambient_drone(t, (uint32_t)clamp(scale_snap(t, 36 + t->p[P_ROOT] + (int32_t)di_rnd(12)), 0, 127));
    fm1_irq_on();
}

static void di_sound(track_t *t, uint32_t ti, uint32_t amt)
{
    static const uint8_t KEEP[] = {P_PDEN, P_PLEN, P_PEV, P_PSEED, P_DIST, P_CHOR, P_DLY, P_REV, P_ITYPE, P_IA, P_IB,
                                   P_IC, P_IMIX};
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    int16_t keep[NELEM(KEEP)], gen[4];
    uint32_t i, k;
    if (e->npresets && di_rnd(100) < amt) {          /* another of its sounds */
        for (i = 0; i < NELEM(KEEP); i++)
            keep[i] = t->p[KEEP[i]];
        for (i = 0; i < 4u; i++)
            gen[i] = t->p[P_E4 + i];
        for (k = 0; k < 16u; k++) {
            uint32_t j = di_rnd(e->npresets);
            if (part_preset_ok(ti, eng_idx(t->eng_req), j) && j != t->preset) {
                apply_preset_to(t, j);
                break;
            }
        }
        for (i = 0; i < NELEM(KEEP); i++)
            t->p[KEEP[i]] = keep[i];
        if (ti == 2u)                                /* BELLS: the generator as it was */
            for (i = 0; i < 4u; i++)
                t->p[P_E4 + i] = gen[i];
    }
    for (i = 0; i < 8u; i++)                         /* its values */
        if (!(ti == 2u && i >= 4u) && !(ti == 3u && i == 0u))   /* (BELLS' GEN, DRUMS' KIT: kept) */
            di_nudge(t, P_E0 + i, amt / 2u);
    for (i = P_FM1_ATK; i <= P_FM3_REL; i++)         /* its own pages' (DRONE's FILTER, GOLANA's, CHIME's TONE) */
        if (ti < 3u)
            di_nudge(t, i, amt / 2u);
}
static void di_notes(track_t *t, uint32_t ti, uint32_t amt)
{
    if (ti == 0u) {
        di_drone_tune(t);
    } else if (ti == 1u) {
        t->p[P_PSEED] = (int16_t)di_rnd(128);
        di_nudge(t, P_PEV, amt);
        di_nudge(t, P_PLEN, amt);
    } else if (ti == 2u && gen_engine(eng_idx(t->eng_req))) {
        t->p[P_E7] = (int16_t)di_rnd(128);
        di_nudge(t, P_E5, amt);
        di_nudge(t, P_E6, amt);
    } else if (ti == 3u && drum_track(t)) {
        if (t->p[DG_DENS] > 0) {
            t->p[DG_DENS] = (int16_t)clamp(t->p[DG_DENS] + (int32_t)di_rnd(3) - 1, 1, 8);
            t->p[DG_ROT] = (int16_t)di_rnd((uint32_t)clamp(t->p[P_SLEN], 1, 16));
            dg_write(t);
        } else {
            uint32_t l, len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
            for (l = 0; l < NLANE; l++)
                tl_lane_rnd(t, len, l, 1);
        }
    }
}
static void di_fx(track_t *t, uint32_t ti, uint32_t amt)
{
    static const uint8_t MODS[4] = {0, 6, 7, 8};     /* OFF PHASR FLANG CHOR (N_ITYPE) */
    uint32_t k;
    di_nudge(t, P_CHOR, amt);
    di_nudge(t, P_DLY, amt);
    di_nudge(t, P_REV, amt);
    for (k = 0; k < CF_PER; k++)                     /* its delay and reverb */
        if (k != CF_SHIM || ti < 3u) {
            int32_t span = CF_P[k].max - CF_P[k].min, v = span * (int32_t)amt / 200;
            if (CF_P[k].fmt == F_ENUM) {
                if (di_rnd(200) < amt)
                    cf.v[ti][k] = (int16_t)di_rnd((uint32_t)span + 1u);
            } else if (v) {
                cf.v[ti][k] = (int16_t)clamp(cf.v[ti][k] + (int32_t)di_rnd((uint32_t)(2 * v + 1)) - v, CF_P[k].min, CF_P[k].max);
            }
        }
    if (di_rnd(200) < amt) {                         /* now and then another modulation, or none */
        t->p[P_ITYPE] = MODS[di_rnd(4)];
        t->p[P_IA] = (int16_t)(20 + di_rnd(60));
        t->p[P_IB] = (int16_t)(30 + di_rnd(70));
        t->p[P_IMIX] = (int16_t)(40 + di_rnd(60));
    }
}

/* a roll: WHO's channels, WHAT moved by AMOUNT; KEY NEW (key: 1) one new key for all four */
static void dice_roll(int key)
{
    static const uint8_t SCL[8] = {1, 2, 3, 4, 5, 6, 8, 9};   /* MAJ MIN DOR MIX PEN MPEN PHRY LYD */
    uint32_t mask = DI_MASK[clamp(di_v[DI_WHO], 0, 7)], what = (uint32_t)clamp(di_v[DI_WHAT], 0, 3);
    uint32_t amt = (uint32_t)clamp(di_v[DI_AMT], 0, 100), ti;
    if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
    di_take();
    if (key) {
        int16_t root = (int16_t)di_rnd(12), scale = (int16_t)SCL[di_rnd(8)];
        for (ti = 0; ti < NTRK; ti++) {
            trk[ti].p[P_ROOT] = root;
            trk[ti].p[P_SCALE] = scale;
        }
        garden[2] = 0;                                /* (BELLS' garden from the new scale) */
        di_drone_tune(&trk[0]);
    }
    for (ti = 0; ti < NTRK; ti++) {
        track_t *t = &trk[ti];
        if (!((mask >> ti) & 1u))
            continue;
        if (what == 0u || what == 1u)
            di_sound(t, ti, amt);
        if (what == 0u || what == 2u)
            di_notes(t, ti, amt);
        if (what == 0u || what == 3u)
            di_fx(t, ti, amt);
    }
    di.ev_ms = fm1_ms;
    ui.force = 1;
    ui_message("DICE ROLLED");
}
/* OCT-: BEFORE / NOW, the other each press */
static void dice_swap(void)
{
    uint32_t i, k;
    if (!di.valid) {
        ui_message("NOTHING ROLLED YET");
        return;
    }
    fm1_irq_off();
    for (i = 0; i < NTRK; i++) {
        uint8_t pr = trk[i].preset;
        for (k = 0; k < P_COUNT; k++) {
            int16_t x = trk[i].p[k];
            trk[i].p[k] = di.p[i][k];
            di.p[i][k] = x;
        }
        trk[i].preset = di.preset[i];
        di.preset[i] = pr;
    }
    for (k = 0; k < NSTEP; k++) {
        step_t s = trk[3].step[k];
        trk[3].step[k] = di.drum[k];
        di.drum[k] = s;
    }
    for (i = 0; i < NPART; i++)
        for (k = 0; k < CF_PER; k++) {
            int16_t x = cf.v[i][k];
            cf.v[i][k] = di.cf[i][k];
            di.cf[i][k] = x;
        }
    fm1_irq_on();
    di_drone_tune(&trk[0]);
    di.now ^= 1u;
    ui.force = 1;
    ui_message(di.now ? "DICE: NOW" : "DICE: BEFORE");
}
/* the UI pass: EVOLVE, a roll every so many bars (KEY kept) */
static void dice_poll(void)
{
    uint32_t bars = DI_EV_BARS[clamp(di_v[DI_EVOLVE], 0, 5)], bar_ms;
    if (!bars) {
        di.ev_ms = fm1_ms;
        return;
    }
    bar_ms = 240000u / (uint32_t)clamp(song.g[G_BPM], 20, 300);
    if (fm1_ms - di.ev_ms >= bars * bar_ms)
        dice_roll(0);
}
