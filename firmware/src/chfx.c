/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Gadlele */
/* Fantasma 2.0: each channel its own delay and reverb (included by fx.c, before fx_buses).
 *
 * Two ways the sends go, each set for the whole project (FX > CH DELAY's DLY, CH REVERB's REV):
 *   BUS    one delay and one reverb for all four tracks, the FX pages DLY and REVERB (ROOM SPRING HALL AMBI), as in
 *          Fantasma 1.x: every project before 2.0 loads so and sounds as it did
 *   CHAN   (the power-on's) each track's DLY send goes to its own delay, its REV send to its own reverb:
 *          DELAY   the bus delay's line cut in four (dly_buf: 4 x 16384), each running at a quarter of the rate: still
 *                  1.49 s of time, its sound darker as a tape's (it stops near 5 kHz), a slow wow on the time, the
 *                  feedback low-passed (TONE) and saturated; CH DELAY: TIME (the note values of the bus delay's),
 *                  FDBK, TONE
 *          REVERB  four lines that feed each other (a Hadamard mix) at half the rate, two diffusers in front, two of
 *                  the lines swaying slowly (no ringing), each line damped (DAMP); SIZE from 0.3 s to a minute
 *                  (127: it holds); stereo. DRONE, PAD and BELLS have long lines and SHIMMER (the tail an octave up,
 *                  fed back in: it rises); DRUMS shorter ones (a room, at most ~10 s), no shimmer. CH REVERB: SIZE,
 *                  DAMP, SHIMMER
 *   The AMBI layer goes to the reverb of the channel its TO says (ambi_layer.c AL_TO).
 * DRIVE and MOD, each track's own already: FX > FX's DIST (the drive) and INSERT (SOFT HARD FOLD FUZZ CRUSH, PHASR FLANG
 * CHOR), two slots.
 * The values (cf.v, cf.mode) are the project's (project.c: the FANTASMA 2 block); a project without them: BUS, the
 * defaults. Changing a mode clears the delay's line in the UI pass (chfx_poll): the delay is silent for that block. */
enum { CF_TIME, CF_FDBK, CF_TONE, CF_SIZE, CF_DAMP, CF_SHIM, CF_PER };   /* a channel's values */
enum { CF_DMODE = CF_PER, CF_RMODE, CF_NID };                         /* the pages' ids: then the two modes */
#define CF_N (NPART * CF_PER + 2u)                                    /* what the project keeps */
static const char *const N_CF_MODE[] = {"BUS", "CHAN", 0};
static const param_desc_t CF_P[CF_NID] = {
    {"TIME", F_ENUM, 0, 12, 1, N_DTIME, 0},
    {"FDBK", F_PCT, 0, 120, 55, 0, 0},
    {"TONE", F_PCT, 0, 127, 60, 0, 0},
    {"SIZE", F_PCT, 0, 127, 95, 0, 0},
    {"DAMP", F_PCT, 0, 127, 50, 0, 0},
    {"SHIMMR", F_PCT, 0, 127, 0, 0, 0},
    {"DLY", F_ENUM, 0, 1, 0, N_CF_MODE, 0},
    {"REV", F_ENUM, 0, 1, 0, N_CF_MODE, 0},
};
/* the power-on's values, a channel each: DRONE a long dark echo and a big space, PAD a dotted quarter, BELLS an
 * eighth dotted with some shimmer, DRUMS a short room */
static const int8_t CF_DEF[NPART][CF_PER] = {
    {6, 50, 45, 105, 60, 20},
    {12, 55, 55, 100, 50, 15},
    {10, 60, 70, 100, 40, 35},
    {10, 35, 60, 55, 45, 0},
};
static struct {
    int16_t v[NPART][CF_PER];    /* each channel's (the pages, the project) */
    int16_t mode[2];             /* DLY, REV: 0 BUS, 1 CHAN */
    uint8_t drun;                /* the delay's mode running (chfx_poll clears the line before a change) */
} cf;
static void chfx_defaults(int16_t *v, int chan)   /* v: CF_N values, the project's order; chan: the power-on's */
{
    uint32_t c, k;
    for (c = 0; c < NPART; c++)
        for (k = 0; k < CF_PER; k++)
            v[c * CF_PER + k] = chan ? CF_DEF[c][k] : CF_P[k].def;
    v[NPART * CF_PER] = v[NPART * CF_PER + 1u] = (int16_t)(chan ? 1 : 0);
}
static void chfx_get(int16_t *v) { memcpy(v, cf.v, sizeof cf.v); v[NPART * CF_PER] = cf.mode[0]; v[NPART * CF_PER + 1u] = cf.mode[1]; }
static void chfx_put(const int16_t *v)
{
    memcpy(cf.v, v, sizeof cf.v);
    cf.mode[0] = v[NPART * CF_PER];
    cf.mode[1] = v[NPART * CF_PER + 1u];
}
static const param_desc_t *chfx_desc(uint32_t k) { return &CF_P[k < NPART * CF_PER ? k % CF_PER : CF_PER + (k - NPART * CF_PER) % 2u]; }
/* the pages' value id (CF_TIME .. CF_RMODE) of the selected channel, and its descriptor (params.c page_desc) */
static int16_t *chfx_val(uint32_t id) { return id < CF_PER ? &cf.v[song.sel % NPART][id] : &cf.mode[(id - CF_PER) & 1u]; }
static const param_desc_t *chfx_pdesc(uint32_t id) { return &CF_P[id < CF_NID ? id : 0]; }

/* ---------------------------------------------------------------- delays --- */
#define CD_LANE 16384u                   /* a channel's line, quarter-rate samples (1.486 s) */
_Static_assert(NPART * CD_LANE <= DLY_LEN, "the delays in the bus delay's line");
static struct {
    uint16_t w;                          /* the write index (quarter rate) */
    uint8_t ph;                          /* the sample within the quarter */
    int32_t acc;                         /* the quarter's input, summed */
    int32_t lp;                          /* the feedback's low-pass */
    int32_t prev, cur;                   /* the last two read (for the way back up) */
    uint32_t wow;                        /* the wow's phase */
} cd[NPART];
static int32_t chd[NPART][CTL], chv[NPART][CTL];   /* each part's DLY and REV sends this block (fx.c mix_part) */

static uint32_t chfx_dly_q(uint32_t t)           /* a TIME in quarter-rate samples */
{
    uint32_t s = t - 10u < 3u ? (beat_samples() * 3u) >> DIV_DOT[t - 10u] : div_samples(t);
    s >>= 2;
    return s < 8u ? 8u : s >= CD_LANE - 4u ? CD_LANE - 4u : s;
}
static __attribute__((noinline)) void chfx_delays(int32_t *wet, uint32_t n)
{
    uint32_t c, i;
    for (c = 0; c < NPART; c++) {
        int16_t *ln = dly_buf + c * CD_LANE;
        const int32_t *in = chd[c];
        uint32_t dq = chfx_dly_q((uint32_t)clamp(cf.v[c][CF_TIME], 0, 12));
        int32_t fb = clamp(cf.v[c][CF_FDBK], 0, 120) * 230, tone = 1500 + clamp(cf.v[c][CF_TONE], 0, 127) * 220;
        uint32_t wq;
        /* the wow: +-~6 quarter samples, ~0.3 Hz, each channel its own pace (Q8 of a quarter sample) */
        cd[c].wow += (uint32_t)(28000u + c * 3000u) * CTL;
        wq = (dq << 8) + (uint32_t)((osc_sine(cd[c].wow) * 1536) >> 15) + (8u << 8);
        for (i = 0; i < n; i++) {
            cd[c].acc += in[i];
            if (++cd[c].ph >= 4u) {
                uint32_t r = (uint32_t)cd[c].w - (wq >> 8), f = wq & 255u;
                int32_t a = ln[r & (CD_LANE - 1u)], b = ln[(r - 1u) & (CD_LANE - 1u)], x = a + (((b - a) * (int32_t)f) >> 8);
                cd[c].ph = 0;
                cd[c].lp += mulq15(x - cd[c].lp, tone);
                ln[cd[c].w & (CD_LANE - 1u)] =
                    (int16_t)clamp((cd[c].acc >> 3) + (softclip(mulq15(cd[c].lp, fb)) >> 0), -32767, 32767);
                cd[c].acc = 0;
                cd[c].w++;
                cd[c].prev = cd[c].cur;
                cd[c].cur = cd[c].lp;
            }
            wet[i] += (cd[c].prev + (((cd[c].cur - cd[c].prev) * (int32_t)cd[c].ph) >> 2)) * 7 >> 2;   /* (back to the rate) */
        }
    }
}

/* --------------------------------------------------------------- reverbs --- */
#define CR_HP 6                          /* the input's low cut: a pole at ~55 Hz (half rate) */
#define CR_APG 19661                     /* the diffusers' gain, Q15 (0.6) */
#define CR_SH 1024u                      /* SHIMMER's line (half rate) */
#define CR_SHW 512u                      /* .. its window: the two heads this far apart at most (a power of 2) */
static const uint16_t CR_LEN[NPART][4] = {{659, 787, 911, 1061}, {613, 743, 877, 1013}, {571, 701, 839, 977},
                                          {241, 307, 379, 443}};
static const uint16_t CR_AP[NPART][2] = {{113, 241}, {107, 229}, {101, 233}, {53, 97}};
#define CR_MEM (659 + 787 + 911 + 1061 + 613 + 743 + 877 + 1013 + 571 + 701 + 839 + 977 + 241 + 307 + 379 + 443 + \
                113 + 241 + 107 + 229 + 101 + 233 + 53 + 97 + 3 * CR_SH)
static int16_t cr_mem[CR_MEM] __attribute__((section(".pool")));
/* the decay a half-rate sample at SIZE s, Q30: RT60 0.3 s .. 60 s (127: 0.999999, it holds) */
static const uint32_t CR_DEC[128] = {
    1072621148, 1072667272, 1072711499, 1072753907, 1072794569, 1072833559, 1072870945, 1072906792, 1072941165,
    1072974123, 1073005724, 1073036026, 1073065080, 1073092939, 1073119651, 1073145264, 1073169823, 1073193371,
    1073215950, 1073237599, 1073258358, 1073278262, 1073297346, 1073315646, 1073333192, 1073350015, 1073366146,
    1073381614, 1073396444, 1073410664, 1073424299, 1073437372, 1073449907, 1073461926, 1073473450, 1073484500,
    1073495095, 1073505254, 1073514994, 1073524334, 1073533289, 1073541875, 1073550108, 1073558002, 1073565571,
    1073572828, 1073579786, 1073586458, 1073592856, 1073598990, 1073604871, 1073610510, 1073615917, 1073621101,
    1073626072, 1073630838, 1073635408, 1073639790, 1073643992, 1073648020, 1073651882, 1073655586, 1073659137,
    1073662542, 1073665806, 1073668937, 1073671938, 1073674816, 1073677575, 1073680220, 1073682757, 1073685189,
    1073687521, 1073689757, 1073691901, 1073693957, 1073695928, 1073697818, 1073699630, 1073701367, 1073703033,
    1073704631, 1073706162, 1073707631, 1073709039, 1073710389, 1073711683, 1073712924, 1073714114, 1073715255,
    1073716349, 1073717398, 1073718404, 1073719368, 1073720293, 1073721180, 1073722030, 1073722845, 1073723626,
    1073724376, 1073725094, 1073725783, 1073726444, 1073727077, 1073727684, 1073728267, 1073728825, 1073729360,
    1073729873, 1073730365, 1073730837, 1073731290, 1073731723, 1073732139, 1073732538, 1073732921, 1073733287,
    1073733639, 1073733976, 1073734299, 1073734609, 1073734906, 1073735191, 1073735464, 1073735726, 1073735977,
    1073736218, 1073740750};
typedef struct {
    int16_t *ln[4], *ap[2], *sh;         /* its lines, diffusers, SHIMMER's line (0: none) */
    uint16_t w[4], apw[2], shw;          /* their write indices */
    uint32_t shr;                        /* SHIMMER's read distance, Q8 (falls 1 a sample: the octave up) */
    int32_t g[4];                        /* each line's gain, Q15 */
    int32_t lp[4], hp, shy;              /* the lines' damping, the low cut (Q8), SHIMMER's last out */
    int32_t dc[4];                       /* each line's DC (Q12): the integers' rounding leaves none to build up */
    int32_t pm, ps;                      /* the last half-rate outputs (mid, side): the way back up */
    uint32_t mph;                        /* the swaying lines' phase */
    int16_t size;                        /* the SIZE the gains are for (-1: none yet) */
} cr_t;
static cr_t cr[NPART];
static int32_t rev_side_ch[CTL];         /* the channels' reverbs: left - right */

static void chfx_init(void)
{
    uint32_t c, k, o = 0;
    memset(cr, 0, sizeof cr);
    memset(cd, 0, sizeof cd);
    for (c = 0; c < NPART; c++) {
        for (k = 0; k < 4u; k++) {
            cr[c].ln[k] = cr_mem + o;
            o += CR_LEN[c][k];
        }
        for (k = 0; k < 2u; k++) {
            cr[c].ap[k] = cr_mem + o;
            o += CR_AP[c][k];
        }
        if (c < 3u) {
            cr[c].sh = cr_mem + o;
            o += CR_SH;
            cr[c].shr = CR_SHW << 8;
        }
        cr[c].size = -1;
    }
    memset(cr_mem, 0, sizeof cr_mem);
}
/* x^e, both Q30 */
static uint32_t cr_pow(uint32_t x, uint32_t e)
{
    uint64_t r = 1u << 30, b = x;
    while (e) {
        if (e & 1u)
            r = (r * b) >> 30;
        b = (b * b) >> 30;
        e >>= 1;
    }
    return (uint32_t)r;
}
static __attribute__((noinline)) void cr_gains(cr_t *r, uint32_t c, int32_t size)
{
    uint32_t k, s = (uint32_t)clamp(size, 0, 127);
    if (c == 3u)                                        /* DRUMS: a room, ~10 s at most */
        s = s * 86u / 127u;
    for (k = 0; k < 4u; k++)
        r->g[k] = (int32_t)(cr_pow(CR_DEC[s], CR_LEN[c][k]) >> 15);
    r->size = (int16_t)size;
}
/* a x b (Q15) rounded to the nearest: a tail fades evenly to the last bit (the lines' DC blockers take what rounding
 * would leave standing) */
static inline int32_t mulq15z(int32_t a, int32_t b) { return (a * b + 16384) >> 15; }
static __attribute__((noinline)) void chfx_reverb(uint32_t c, const int32_t *in, int32_t *wet, int32_t *side, uint32_t n)
{
    cr_t *r = &cr[c];
    uint32_t i, k;
    /* SHIMMER s (Q15, up to 0.39): the octave-up tail fed back in at s, the lines' own gains down by as much (1 - s),
     * so the loop's gain stays below 1 at any SIZE (it rises, it never runs away) */
    int32_t kd = 32767 - clamp(cf.v[c][CF_DAMP], 0, 127) * 120, shim = r->sh ? clamp(cf.v[c][CF_SHIM], 0, 127) * 100 : 0;
    int32_t gk = 32767 - (shim >> 3);
    if (r->size != cf.v[c][CF_SIZE])
        cr_gains(r, c, cf.v[c][CF_SIZE]);
    for (i = 0; i + 1u < n; i += 2u) {
        int32_t x = (in[i] + in[i + 1u]) >> 1, o[4], h[4], m, s, sw;   /* (loud in the lines: a long tail stays above
                                                                          * the rounding; out a quarter, below) */
        r->hp += ((x << 8) - r->hp) >> CR_HP;          /* (Q8, rounded: it settles at 0, no offset left in) */
        x -= (r->hp + 128) >> 8;
        if (shim)
            x += mulq15(r->shy, shim) >> 1;             /* (shy: the lines' mean; a quarter to each, with the sign) */
        for (k = 0; k < 2u; k++) {                      /* the diffusers */
            int16_t *a = r->ap[k];
            int32_t d = a[r->apw[k]], v = clamp(x - mulq15(d, CR_APG), -32767, 32767);
            a[r->apw[k]] = (int16_t)v;
            x = d + mulq15(v, CR_APG);
            if (++r->apw[k] >= CR_AP[c][k])
                r->apw[k] = 0;
        }
        r->mph += 0x1E000u;                              /* (~0.3 Hz at half rate) */
        sw = osc_sine(r->mph) >> 12;                     /* +-8 samples: lines 0 and 2 sway */
        for (k = 0; k < 4u; k++) {
            uint32_t len = CR_LEN[c][k], rd;
            int32_t v;
            if (k == 0u || k == 2u) {                    /* read a little short of the end, swaying */
                rd = r->w[k] + (uint32_t)(10 + (k ? -sw : sw));   /* (2 .. 18 ahead of the write: the oldest) */
                if (rd >= len)
                    rd -= len;
            } else {
                rd = r->w[k];
            }
            v = r->ln[k][rd];
            r->lp[k] += mulq15z(v - r->lp[k], kd);       /* DAMP */
            o[k] = r->lp[k];
        }
#define CR_H2(v) (((v) + (int32_t)((uint32_t)(v) >> 31)) >> 1)   /* v / 2 toward zero, without a divide */
        h[0] = CR_H2(o[0] + o[1] + o[2] + o[3]);         /* the Hadamard mix (orthonormal: / 2, toward zero) */
        h[1] = CR_H2(o[0] - o[1] + o[2] - o[3]);
        h[2] = CR_H2(o[0] + o[1] - o[2] - o[3]);
        h[3] = CR_H2(o[0] - o[1] - o[2] + o[3]);
        for (k = 0; k < 4u; k++) {
            int32_t y = mulq15z(mulq15z(h[k], r->g[k]), gk) + ((k & 1u) ? -x : x);
            r->dc[k] += ((y << 12) - r->dc[k]) >> 12;       /* (a pole at ~1 Hz; Q12: it settles below an LSB) */
            y -= (r->dc[k] + 2048) >> 12;
            r->ln[k][r->w[k]] = (int16_t)(y > 24000 || y < -24000 ? soft_knee(y, 24000) : y);   /* (soft: never stuck) */
            if (++r->w[k] >= CR_LEN[c][k])
                r->w[k] = 0;
        }
        m = (o[0] + o[1] + o[2] + o[3]) >> 1;            /* left o0 + o2, right o1 + o3 */
        s = (o[0] + o[2] - o[1] - o[3]) >> 1;
        if (r->sh) {                                     /* SHIMMER: the tail an octave up, two heads crossfaded */
            uint32_t d1 = r->shr >> 8, d2 = (d1 + CR_SHW / 2u) & (CR_SHW - 1u);
            int32_t g1 = (int32_t)(d1 < CR_SHW / 2u ? d1 : CR_SHW - d1), g2 = (int32_t)(CR_SHW / 2u) - g1;
            r->sh[r->shw & (CR_SH - 1u)] = (int16_t)clamp(m >> 2, -32767, 32767);   /* (the lines' mean) */
            r->shy = (r->sh[(r->shw - d1 - 2u) & (CR_SH - 1u)] * g1 + r->sh[(r->shw - d2 - 2u) & (CR_SH - 1u)] * g2) >> 8;
            _Static_assert(CR_SHW / 2u == 256u, "the crossfade's shift");
            r->shw++;
            r->shr = r->shr >= (2u << 8) ? r->shr - (1u << 8) : (CR_SHW - 1u) << 8;   /* the read nears the write by
                                                                                    * one a sample: twice the speed */
        }
        wet[i] += ((r->pm + m) * 5) >> 3;               /* back to the rate: between the two, then this one */
        side[i] += ((r->ps + s) * 5) >> 3;
        wet[i + 1u] += (m * 5) >> 2;
        side[i + 1u] += (s * 5) >> 2;
        r->pm = m;
        r->ps = s;
    }
}

/* the channels' buses for a block (fx_buses, CHAN): the delays, the reverbs; HALL's side mix carries the stereo */
static void chfx_buses(int32_t *wet, uint32_t n, int dly, int rev)
{
    uint32_t c, i;
    if (dly && cf.drun == 1u)
        chfx_delays(wet, n);
    if (rev) {
        for (i = 0; i < n; i++)
            rev_side_ch[i] = 0;
        for (c = 0; c < NPART; c++)
            chfx_reverb(c, chv[c], wet, rev_side_ch, n);
    }
}
/* (chfx_poll, the UI pass's: ui_input.c) */
