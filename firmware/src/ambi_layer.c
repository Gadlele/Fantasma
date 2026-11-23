/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Gadlele */
/* Fantasma 2.0: AMBI, the ambience under the four channels. The AMBI engine's noise (eng_ambi.c ambi_dsp) on a voice
 * of its own, outside the parts: no steps, no keys, none of the shared eight voices; into the mix (centred) and the
 * reverb's send (fx.c mix_block, after the parts). HOME's pages after CLOCK:
 *   AMBI     SCENE (an AMBI sound: SEA .. VHS HISS, then the layer's own five), TONE (darker / brighter: the filter
 *            and the noise's colour together), MOTION (how much and how fast it swells; 0: the scene's own),
 *            INTENS (a scene with crackles: how many; a bed: how heavy; 0: the scene's own)
 *   AMBI 2   AMBI (OFF / ON: it fades in and out over about a second), LEVEL, REVERB
 * Saved with the project (project.c: the FANTASMA 2 block), not as a track's parameters. The values are the editor's
 * and the knobs' (al.v); the render reads them once a block */
enum { AL_SCENE, AL_TONE, AL_MOTION, AL_INTENS, AL_ON, AL_LEVEL, AL_REV, AL_N };

/* the layer's own scenes after the engine's (COLR FREQ RES SWELL CRKL POP RATE BAL, as AMBI_PRESETS) */
static const int8_t AL_EXTRA[][8] = {
    {127, 22, 35, 127, 0, 0, 25, 0},     /* THUNDER: brown, deep, a slow roll that swells and falls away */
    {80, 74, 45, 110, 0, 0, 100, 0},     /* BREATH: pink, a band that rises and falls a few seconds apart */
    {30, 112, 100, 50, 60, 4, 80, 70},   /* NIGHT: a little wind, crickets far off */
    {120, 42, 10, 30, 18, 70, 30, 105},  /* EMBERS: a fire burnt down, few soft pops */
    {35, 98, 8, 10, 12, 20, 64, 40},     /* TAPE END: the hiss of a tape run out, now and then a crackle */
};
#define AL_NSCENE (NELEM(AMBI_PRESETS) + NELEM(AL_EXTRA))
static const char *const N_ALSCENE[] = {"SEA", "SURF", "STORM", "WIND", "STREAM", "HISS", "FIRE", "VINYL", "RAIN",
                                        "STORM RAIN", "CAVE DRIP", "CRICKETS", "VHS HISS",
                                        "THUNDER", "BREATH", "NIGHT", "EMBERS", "TAPE END", 0};
_Static_assert(NELEM(N_ALSCENE) == AL_NSCENE + 1u, "a name a scene");
static const char *const N_ALON[] = {"OFF", "ON", 0};
static const param_desc_t AL_P[AL_N] = {
    {"SCENE", F_ENUM, 0, (int16_t)(AL_NSCENE - 1u), 0, N_ALSCENE, 0},
    {"TONE", F_BIPCT, -64, 63, 0, 0, 0},
    {"MOTION", F_BIPCT, -64, 63, 0, 0, 0},
    {"INTENS", F_BIPCT, -64, 63, 0, 0, 0},
    {"AMBI", F_ENUM, 0, 1, 0, N_ALON, 0},
    {"LEVEL", F_PCT, 0, 127, 90, 0, 0},
    {"REVERB", F_PCT, 0, 127, 70, 0, 0},
};

static struct {
    int16_t v[AL_N];             /* the values (AL_P) */
    voice_t vc;                  /* the layer's voice (its filter, noise and swell states) */
    int32_t amp;                 /* the level now, Q15 (0: silent and not running) */
} al;

static void al_defaults(int16_t *v)
{
    uint32_t i;
    for (i = 0; i < AL_N; i++)
        v[i] = AL_P[i].def;
}
static void al_init(void)
{
    memset(&al, 0, sizeof al);
    al_defaults(al.v);
    al.vc.s[2] = 0x2468ACE1;                     /* (the noise's generator: never 0) */
}

/* the scene's eight values as the knobs move them */
static void al_values(int16_t *e)
{
    uint32_t sc = (uint32_t)clamp(al.v[AL_SCENE], 0, (int32_t)AL_NSCENE - 1), i;
    int32_t mo = al.v[AL_MOTION], in = al.v[AL_INTENS];
    for (i = 0; i < 8u; i++)
        e[i] = sc < NELEM(AMBI_PRESETS) ? AMBI_PRESETS[sc].e[i] : AL_EXTRA[sc - NELEM(AMBI_PRESETS)][i];
    e[3] = (int16_t)clamp(mo >= 0 ? e[3] + (127 - e[3]) * mo / 63 : e[3] * (64 + mo) / 64, 0, 127);   /* SWELL */
    e[6] = (int16_t)clamp(e[6] + mo / 2, 0, 127);                                                     /* RATE */
    if (e[4])                                       /* crackles: their density */
        e[4] = (int16_t)clamp(e[4] + in, 1, 127);
    else                                            /* a bed: heavier, brighter and fuller as it goes up */
        e[1] = (int16_t)clamp(e[1] + in / 2, 0, 127);
}

/* the layer's block into b (n samples, Q15 as a voice's, b zeroed by the caller): 0 when silent */
static uint32_t al_render(int32_t *b, uint32_t n)
{
    int32_t target = al.v[AL_ON] ? clamp(al.v[AL_LEVEL], 0, 127) * 258 : 0, a0 = al.amp, t = al.v[AL_TONE];
    int16_t e[8];
    vmod_t m;
    if (!a0 && !target)
        return 0;
    al.amp += target > a0 ? (target - a0 > 24 ? 24 : target - a0) : -(a0 - target > 24 ? 24 : a0 - target);   /* ~1 s */
    al_values(e);
    memset(&m, 0, sizeof m);
    m.amp0 = a0;
    m.amp1 = al.amp;
    m.cutoff = t * 128;                             /* TONE: up to +-32 steps of FREQ .. */
    m.shape = (64 << 8) - t * 128;                  /* .. the colour the other way (darker: browner) */
    ambi_dsp(e, &al.vc, b, n, &m);
    return 1;
}
