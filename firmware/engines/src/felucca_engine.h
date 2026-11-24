/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca-Engines: the engine interface. The types an engine (eng_*.c) and its host share: parameter
 * descriptors, a voice, its per-block modulation, presets and the engine table entry.
 *
 * The engines are built as one translation unit with their host: the host includes, in this order,
 *   its own definitions (below), this header, the tables, dsp.c, then the eng_*.c files it wants.
 * Every function is static: nothing here is linked separately.
 *
 * What the host provides (before the first eng_*.c):
 *   NVOICE, NPART          macros: voices per part, parts (WHEEL keeps NPART x NVOICE voices of its own state)
 *   track_t                a typedef of `struct track`, with at least
 *                            int16_t p[];          the part's parameters, indexed by the P_* below
 *                            voice_t v[NVOICE];    its voices
 *   trk                    the array of the parts' track_t (WHEEL finds a part's number as t - trk)
 *   P_E0 .. P_E7           enum constants: the engine's eight parameters in t->p[] (EDIT 1 / EDIT 2)
 *   P_ATK, P_REL           the amplitude envelope's attack and release (an engine's HOME knobs may name them)
 *   P_FTYPE                ANALOG's filter type: 0 low-pass, 1 band-pass, 2 high-pass (a per-part parameter)
 *   the tables             FS, CTL (32), SINE[1025], PITCH_OCT and pitch_inc(), SVF_G[128], TANH_Q15[257]
 *                          (tools/gen_engine_tables.py writes them as fe_tables.h)
 *
 * The sample rate: FS comes from that header, a build-time choice (gen_engine_tables.py --fs RATE fe_tables.h);
 * 44100 by default (Felucca's). The engines were designed at 44.1 kHz; at another rate the tables are made for it
 * and what the engines time in samples or blocks (sample holds, one-pole corners, block-rate decays and
 * smoothers, the LOFI arpeggio / vibrato / sweep, the WHEEL rotors and fold point, the NOISE drift and impulse
 * rate) is moved to it at compile time (dsp.c, "the rate"; the constants that need more than integer arithmetic
 * are in the header the generator writes for a rate other than 44100). No cost a sample; at 44100 the output is
 * bit for bit the same as before the rate was a choice.
 *   Supported: 32000 .. 96000 (tests/run.sh checks pitch, cutoff and levels at 48 and 96 kHz; FS_CHECK= other
 *   rates); up to 192000 builds and plays, with the limits below; other rates are refused (below ~26.5 kHz the
 *   top note, 13.2 kHz, would pass Nyquist).
 *   CTL stays 32 samples at every rate (a block is shorter in time at higher rates; the engines' block-rate
 *   times are scaled to it). The host's own control-rate tables (envelopes, LFOs: not in these engines) are the
 *   host's to make at its rate.
 *   Limits of the fixed point: SVF_G is Q12, so the lowest cutoffs quantise more as FS rises (worst near 30 Hz:
 *   ~9 % at 44.1 / 48 kHz, ~18 % at 96 kHz, ~33 % at 192 kHz; above ~250 Hz under 2 % at 96 kHz); cutoffs above
 *   0.45 FS are held there (at 32 kHz the top of the 16 kHz range). Shifts that set a corner (FE_OCT) move in
 *   whole octaves, so those corners and times are within ~0.7x .. 1.4x (half an octave) of the 44.1 kHz design
 *   at rates between the octaves. White-noise sources keep their amplitude a sample, so their level in the
 *   audible band falls with the rate (~-3.4 dB at 96 kHz).
 *
 * What the engines expect of the host's voices (the host's voice allocator and envelope):
 *   - note_on is called as a voice starts (or is retriggered); ph[] and s[] are zero before a voice first
 *     sounds. A retrigger of a sounding voice runs note_on, then puts back ph[0..2] and each s[k] (k = 0, 1,
 *     4..7) whose bit k of engine_t.keep is set; s[2] and s[3] are as note_on left them.
 *   - render adds n samples (n <= CTL; blocks are CTL long) to out[]: add, never overwrite. out[] may alias
 *     the voice's own state.
 *   - vmod_t carries the block's pitch, amplitude ramp (amp0 -> amp1) and the modulated cutoff and shape.
 *   - env is the ADSR value in Q24 (1 << 24 = full), stage 0 off, 1 attack, 2 decay / sustain, 3 release,
 *     4 given up (fading out); active is 1 while the voice sounds, gate 1 while its key is held.
 *     LOFI's stepped envelope (its amp hook) reads and ends them; WHEEL reads gate and the other voices
 *     of the part.
 *   - age is a counter the host advances per voice start (a noise seed); note and fine as the host set them.
 *   - amp, desc, block: optional hooks (0 = none), see engine_t.
 * The layout of these types is Felucca's: a host keeps it as it is (the field order, the sizes). The file names
 * in the comments below (voice.c, engines.c, mod.c, fx.c, ...) are Felucca's, the host these engines came from. */
#ifndef FELUCCA_ENGINE_H
#define FELUCCA_ENGINE_H
#include <stdint.h>

#ifndef NVOICE
#error "felucca_engine.h: the host defines NVOICE (voices per part) first"
#endif
#ifndef NPART
#error "felucca_engine.h: the host defines NPART (parts) first"
#endif

#ifndef NELEM
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))
#endif

/* ------------------------------------------------------- parameters --- */
enum {
    F_INT, F_PCT, F_BIPCT, F_TIME, F_LFOHZ, F_CUTOFF, F_DB, F_SEMI, F_ENUM, F_BPM, F_NOTE,
    F_ONOFF, F_OCT, F_STEPS
};

typedef struct {
    const char *label;
    uint8_t fmt;
    int16_t min, max, def;
    const char *const *names;   /* F_ENUM */
    const char *unit;           /* F_INT / F_ENUM optional unit */
} param_desc_t;

/* ------------------------------------------------- voices, engines --- */
typedef struct {
    uint8_t note, vel, gate, active;
    uint8_t stage;               /* env: 0 off, 1 attack, 2 decay/sustain, 3 release, (Fantasma: 4 given up, 5 AMBI INF
                                  * crossfade out: voice.c) */
    uint8_t side;                /* SPREAD (fx.c mix_spread): 0 left of PAN, 1 right; set as the voice starts */
    int32_t env;                 /* Q24 */
    int32_t env_out;             /* last control-rate amplitude, Q15 */
    int32_t pitch16, pitch_cur;  /* 1/16 semitone, with glide */
    int32_t gstep;               /* glide TIME mode: 1/16 st per control tick, 0 = RATE mode */
    int32_t fine;                /* unison detune: phase increment * (1 + fine / 4096) */
    uint32_t ph[3];
    int32_t s[8];                /* engine state (filters, envs) */
    uint32_t age;
    uint8_t mvel;                /* mod.c: the note-on velocity (before UNISON scaling) and RAND of the note */
    int16_t mrnd;
    uint8_t fstage;              /* (Fantasma 2.0) FILT ENV: 1 attack, 2 decay / sustain (released: the gate's) */
    int32_t fenv;                /* .. its level, Q24 (voice.c fenv_tick) */
} voice_t;

typedef struct {                 /* per-voice control-rate modulation, computed in voice.c */
    uint32_t inc;                /* phase increment of the base pitch */
    int32_t pitch16;
    int32_t amp0, amp1;          /* Q15 ramp over the block */
    int32_t cutoff;              /* 0..127 << 8 */
    int32_t shape;               /* 0..127 << 8 */
    int32_t envq15;              /* env value (for engines that use it as a mod source) */
    int32_t fine;                /* the residual below 1/16 semitone in inc: unison detune, TUNE, bend (1/4096) */
} vmod_t;

typedef struct {
    const char *name;
    int8_t e[8];                 /* P_E0..P_E7 (signed: an interval below the note; every value fits) */
    uint8_t env[4];              /* ATK DEC SUS REL */
    int8_t fenv;                 /* ENV -> FILTER amount (-64..63) */
    uint8_t mono;                /* 1 = MONO (bass / lead), 0 = POLY */
    /* the rest of the patch; each value is stored + 1, 0 = the default */
    uint8_t fx[4];               /* DIST, CHORUS, DELAY, REVERB sends */
    uint8_t pat;                 /* suggested pattern (PATTERNS[pat - 1], 0 = none): only a hint. A sound load
                                  * never touches the steps; SEQ > PATTERNS loads a pattern (ui.c pat_load). The
                                  * arp is the track's too: a preset does not set it */
    uint8_t wow[3];              /* (Fantasma 1.1) the tape's wow: the LFO's WAVE, RATE and PIT depth, each + 1, set
                                  * FREE; 0 = the LFO stays at its defaults, as every preset before 1.1 */
    uint8_t sprd, glide;         /* (Fantasma 1.1) SPREAD (EDIT > VOICE 3) and GLIDE, each + 1; 0 = their defaults */
} preset_t;
#define FX(d, c, dl, r) .fx = {(d) + 1, (c) + 1, (dl) + 1, (r) + 1}
#define PAT(n) .pat = (n)
#define WOW(w, r, d) .wow = {(w) + 1, (r) + 1, (d) + 1}   /* WAVE (N_LWAVE: 0 SIN, 1 TRI), RATE 0..127, PIT 0..63 */
#define SPRD(n) .sprd = (n) + 1                   /* the voices across the stereo field, 0..127 */
#define GLD(n) .glide = (n) + 1                   /* the glide between notes, 0..127 */

struct track;
typedef struct {                 /* an engine (engines.c ENGINES[]; the eng_*.c files) */
    const char *name;            /* "ANALOG" (PRESETS, the editor) */
    const char *page_title[2];   /* EDIT 1 and EDIT 2 */
    param_desc_t edit[8];        /* P_E0..P_E7 */
    const preset_t *presets;
    uint8_t npresets;
    uint8_t knob[4];             /* HOME: the four parameters on KNOB 1..4 */
    uint8_t poly;                /* voice cap for POLY and UNISON, 0 = NVOICE */
    uint8_t sampled;             /* 1 = plays recorded material (a position, not a phase): voice.c keeps
                                  * no phases over a retrigger, spreads none for UNISON, renders at SUS 0 */
    uint8_t keep;                /* bit k: s[k] is kept when a sounding voice is retriggered (filters) */
    uint8_t oneshot;             /* 1 = hits (DRUM): always POLY, no glide; amp ends its voices (eng_drum.c) */
    void (*note_on)(struct track *t, voice_t *v);
    void (*render)(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m);
    /* optional (0 = none): the voice amplitude instead of the ADSR curve, once per control tick;
     * gets the ADSR value (Q15, env_tick already ran: it still gates the voice), returns Q15 */
    int32_t (*amp)(struct track *t, voice_t *v, int32_t adsr);
    /* optional: a mode-dependent descriptor of EDIT k (the same range and default as edit[k],
     * another label / value names), 0 = edit[k] */
    const param_desc_t *(*desc)(const struct track *t, uint32_t k);
    /* optional: once per block and part, before its voices (also with no voice sounding) */
    void (*block)(struct track *t);
    /* optional: the note of key k (0 = the lowest) when the engine maps the keys itself, else -1
     * (then the scale keyboard of seq.c kb_map) */
    int32_t (*keys)(const struct track *t, uint32_t k);
    /* 1 = the engine's own envelopes are the voice's amplitude (FM6): voice.c renders it at 1.0 (no ADSR, no
     * velocity; LFO -> AMP, the matrix's AMP and the fades still apply; ENV is 0) and the voice ends when
     * done() says so (once per control tick, before the render), not at the end of the ADSR's release */
    uint8_t ownenv;
    int (*done)(struct track *t, voice_t *v);
    /* bytes of the part's engine memory (engines.c eng_mem, at most ENG_MEM_PART) the engine keeps its state
     * in, 0 = none: a part plays one engine at a time, so they share one region; on a switch to the engine
     * (after the old one's fade) the region is zeroed when another engine had it (eng_mem_claim) */
    uint16_t mem;
    /* optional: once per block and part, after its voices, on their sum in out (n samples): an effect of the engine's
     * own over the whole part (an echo, a resonator), also with no voice sounding; returns 1 while it still sounds
     * (the host keeps mixing the part), 0 = silent */
    uint32_t (*post)(struct track *t, int32_t *out, uint32_t n);
} engine_t;

#endif /* FELUCCA_ENGINE_H */
