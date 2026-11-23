/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Gadlele */
/* Fantasma 2.0: the DRUMS' generator, after JIANT's EUCLID and MUTATE (github.com/juanjiant-bit/JIANT, its README,
 * V10.16; no code of it). Two pages on a DRUM track, after LANES 2:
 *   EUCLID  DENS (0 OFF: the steps are yours; 1..8 a whole kit written as euclidean rhythms, sparse to busy), LEN (the
 *           track's own, 1..64: each channel its length), ROT (the pattern turned by steps), LANE (ALL: the kit; KICK ..
 *           BELL: that lane alone, DENS its hits, 1..8 eighths of LEN). Turning one rewrites the steps 1..LEN at once
 *   MUTATE  EVERY (OFF, 1 2 4 8 16 loops of the track), AMOUNT (how much a mutation moves: a few hits shifted, added or
 *           dropped), MODE (ORBIT: each mutation from the pattern dialled, it never strays far; DRIFT: from the last,
 *           it walks on), FEEL (accents on the beats, softer steps between them: it breathes)
 * On either page OCT+ is DICE (a new pattern at random, the kit's way: the dialled one is kept) and OCT- goes back to
 * the pattern dialled (the anchor). Every rewrite is one undo (SAVE held), as SEQ TOOLS'. The values are track
 * parameters a DRUM sound has no other use for (DIGITAL's retired OP 4 envelope and two OP levels, core.h DG_*); a
 * kit change keeps them (ui.c apply_preset_to). The anchor is not saved (gone at power-off: the steps are) */
/* (the pages' descriptors, DG_P and dg_desc: eng_drum.c, with the engine) */

/* the kit at DENS d: each lane's hits a 16-step bar (made for LEN; tl_per) and where its first falls */
static const uint8_t DG_KIT[8][NLANE] = {
    /* KICK SNARE CLAP HATCL HATOP TOM RIM BELL */
    {1, 0, 0, 0, 0, 0, 0, 0},
    {2, 1, 0, 0, 0, 0, 0, 0},
    {2, 1, 0, 4, 0, 0, 0, 0},
    {3, 2, 0, 4, 0, 0, 1, 0},
    {3, 2, 1, 6, 1, 0, 1, 0},
    {4, 2, 1, 8, 1, 1, 2, 1},
    {5, 3, 1, 10, 2, 1, 3, 1},
    {6, 4, 2, 12, 2, 2, 4, 2},
};
static const uint8_t DG_OFF16[NLANE] = {0, 4, 12, 2, 6, 10, 3, 7};   /* the snare on the backbeat, the hats off it .. */

static struct {
    step_t step[NSTEP];          /* the anchor: the pattern dialled (or the hand-made one DICE started from) */
    uint8_t trk;                 /* its track + 1, 0 = none */
    uint8_t seen[NTRK];          /* the loops counted when the last mutation was made */
} dg;

static int dg_page(const page_t *pg)              /* EUCLID or MUTATE */
{
    return pg->graph == GR_NONE && ((pg->id[0] == DG_DENS && pg->id[1] == P_SLEN) || (pg->id[0] == DG_EVERY && pg->id[1] == DG_AMT));
}
static uint32_t dg_len(const track_t *t) { return (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP); }

/* FEEL: steps on the beat as they are, an accent on the first; between them softer as FEEL rises */
static void dg_feel(track_t *t)
{
    uint32_t len = dg_len(t), i, f = (uint32_t)clamp(t->p[DG_FEEL], 0, 127);
    for (i = 0; i < len; i++) {
        step_t *s = &t->step[i];
        if (!s->hit)
            continue;
        s->vel = (uint8_t)(i % 4u == 0u ? 100u : i % 2u == 0u ? 100u - f / 4u : 100u - f / 2u);
        if (i == 0u && f > 20u && (s->hit & 1u))
            s->acc |= 1u;                                /* (the kick on 1) */
    }
}

/* the euclid as dialled: DENS's kit (LANE ALL) or the lane's hits, turned by ROT; then FEEL; it becomes the anchor */
static void dg_write(track_t *t)
{
    uint32_t len = dg_len(t), d = (uint32_t)clamp(t->p[DG_DENS], 0, 8), ln = (uint32_t)clamp(t->p[DG_LANE], 0, 8);
    uint32_t rot = (uint32_t)clamp(t->p[DG_ROT], 0, 63), l;
    if (!d)
        return;
    tl_undo_take(t);
    if (!ln) {
        for (l = 0; l < NLANE; l++) {
            tl_lane_clear(t, len, l);
            tl_euclid(t, len, l, tl_per(DG_KIT[d - 1u][l], len), (DG_OFF16[l] * len / 16u + rot) % len, 0);
        }
    } else {
        l = ln - 1u;
        tl_lane_clear(t, len, l);
        tl_euclid(t, len, l, (d * len + 7u) / 8u, (DG_OFF16[l] * len / 16u + rot) % len, 0);
    }
    dg_feel(t);
    memcpy(dg.step, t->step, sizeof dg.step);
    dg.trk = (uint8_t)(trk_index(t) + 1u);
}

/* a mutation of the steps 1..LEN: AMOUNT % of the hits move a step, and as many hats or rims come or go; the kick on
 * step 1 stays */
static void dg_mutate(track_t *t)
{
    uint32_t len = dg_len(t), amt = (uint32_t)clamp(t->p[DG_AMT], 0, 127), n = 0, i, l, k, moves;
    if (dg.trk != trk_index(t) + 1u) {                   /* (no anchor yet: the steps as they are) */
        memcpy(dg.step, t->step, sizeof dg.step);
        dg.trk = (uint8_t)(trk_index(t) + 1u);
    }
    if (!t->p[DG_MODE])                                  /* ORBIT: from the anchor */
        for (i = 0; i < len; i++)
            t->step[i] = dg.step[i];
    for (i = 0; i < len; i++)
        n += (uint32_t)__builtin_popcount(t->step[i].hit);
    moves = (n * amt + 126u) / 127u;
    for (k = 0; k < moves; k++) {
        i = tl_rnd(len);
        l = tl_rnd(NLANE);
        if (!((t->step[i].hit >> l) & 1u) || (!i && !l))
            continue;
        grid_hit(t, i, l, 0);                            /* a hit moves a step either way */
        grid_hit(t, (i + (tl_rnd(2) ? 1u : len - 1u)) % len, l, 1);
    }
    for (k = 0; k < (moves + 1u) / 2u; k++) {            /* hats and rims come and go */
        l = tl_rnd(3) == 0u ? DV_RIM : DV_HATC;
        grid_hit(t, tl_rnd(len), l, 2);
    }
    dg_feel(t);
}

/* DICE (OCT+): a new pattern at random, the drum machine way (SEQ TOOLS' RANDOM, every lane), one undo; the pattern
 * dialled stays the anchor */
static void dg_dice(track_t *t)
{
    uint32_t len = dg_len(t), l;
    if (dg.trk != trk_index(t) + 1u) {
        memcpy(dg.step, t->step, sizeof dg.step);
        dg.trk = (uint8_t)(trk_index(t) + 1u);
    }
    tl_undo_take(t);
    for (l = 0; l < NLANE; l++)
        tl_lane_rnd(t, len, l, 1);
    dg_feel(t);
    ui_message("DICE");
}
/* OCT-: back to the anchor */
static void dg_anchor(track_t *t)
{
    if (dg.trk != trk_index(t) + 1u) {
        ui_message("NOTHING TO GO BACK TO");
        return;
    }
    tl_undo_take(t);
    memcpy(t->step, dg.step, sizeof dg.step);
    ui_message("BACK TO THE PATTERN");
}

/* the UI pass (ui_input.c): a DRUM track whose EVERY is on mutates once its loops have come round EVERY times */
static void dg_poll(void)
{
    uint32_t ti;
    for (ti = 0; ti < NTRK; ti++) {
        track_t *t = &trk[ti];
        uint32_t ev = DG_EVERY_N[clamp(t->p[DG_EVERY], 0, 5)], now = dg_loops[ti];
        if (!ev || !drum_track(t) || !song.playing) {
            dg.seen[ti] = (uint8_t)now;
            continue;
        }
        if ((uint8_t)(now - dg.seen[ti]) >= ev) {
            dg.seen[ti] = (uint8_t)now;
            dg_mutate(t);
        }
    }
}
