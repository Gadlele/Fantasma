/* SPDX-License-Identifier: GPL-3.0-only */
/* The drone engine, after the Stylophone CPM DS-2 (its manual). Two drones, always on (in the
 * code a drone is a "group": GP_*, DR_GP), each:
 *
 *   MAIN     an oscillator whose X-WAVE blends sine -> triangle -> saw -> square (after the DS-2's
 *            X-WAVE, with a sine before it), the square's pulse Width
 *   PARTNER  a second oscillator riding along at an interval from the main one (unison, fifth,
 *            octave...) with Detune and a slow independent drift; on or off (GP_PARTNER)
 *   SUB      a square one or two octaves under the main oscillator; OSC MIX crossfades main <-> sub
 *   NOISE    a white or pink noise (its Colour), at its own level beside the
 *            oscillators (as a synth's mixer)
 *
 * mixed into the drone's own filter (12 dB/oct state-variable, LP / BP / HP, resonance up to
 * self-oscillation, soft-saturating drive). A drone's pitch is a parameter in cents, free between notes
 * (2 Hz .. 1.6 kHz: the DS-2's range from 27 Hz, and under it the Lyra-8's clicks), and slides or glides when it changes. There are no envelopes:
 * dr_play() fades the whole drone in or out, dr_voice() one drone.
 *
 * The PULSE chops both drones in time, on its own clock: rate, depth, shape (swell .. gate), swing.
 * Then the EFFECTS, as the DS-2's: a dark, PT2399-style DELAY (LFO on its time, repeats up to runaway),
 * a DRIVE after it (the Lyra-8's: the drones and their repeats distorted together) and a stereo REVERB of
 * four types, each with a dry/wet mix (drone_fx.c).
 *
 * Between the two, as the DS-2's switches and its best-known patch: HARD SYNC (drone II's oscillator
 * restarts with drone I's), PITCH LINK (drone II follows drone I's pitch, at an offset) and CROSS-MOD
 * (drone I's sound moves drone II's cutoff, at audio rate: the DS-2's DRONE I OUT -> CUTOFF II cable).
 *
 * The ULTRA LFO (after the DS-2's): one Rate, 0.01 .. 70 Hz, two taps (Wave I, Wave II), each Off or one
 * of ten shapes (the DS-2's seven, random, and the Lyra-8's AND / OR), locked to the same clock. Each drone routes a tap to its oscillator (pitch or X-WAVE)
 * and a tap to its filter (cutoff or resonance), each with a bipolar depth (0: none, negative: inverted).
 *
 * Threads, as in FoMni's engine: dr_render() runs in the audio interrupt; everything else posts commands that
 * the render drains at the start of a block. The state marked (UI) is written by the render and only
 * read elsewhere. Float only, no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define DR_SR 44100.0f
#define DR_NGROUPS 2                           /* the drones */
#define DR_OUT(v, g) ((v) >> ((g) ^ 1) & 1)      /* P_II_OUT's value v: drone g (0, 1) is out */

/* a drone's parameters; drone g's copy of GP_x is parameter DR_GP(g, GP_x) */
enum {
    GP_PITCH,       /* cents over MIDI note 0: -2400 (2 Hz) .. 9135 (1.6 kHz); 2068 27 Hz, 4500 A2, 5700 A3 */
    GP_WAVE,        /* X-WAVE, 0..100: 0 sine, 33 triangle, 67 saw, 100 square, blended in between */
    GP_WIDTH,       /* the square's pulse width: 0 even (50 %) .. 100 narrow (5 %) */
    GP_INTERVAL,    /* the partner's interval: an index into DR_IV_SEMI (each tempered, then just, pure) */
    GP_DETUNE,      /* the partner's detune, 0..100 = 0..30 cents */
    GP_MIX,         /* OSC MIX, 0..100: 0 the oscillators alone .. 100 the sub alone (a crossfade) */
    GP_SUBOCT,      /* the sub: 1 an octave below, 2 two, 3 a third of the pitch, 4 a fifth (subharmonics), 5 three
                     * octaves, 6 four (v0.3: these two go under 20 Hz and click, the others stay over it) */
    GP_CUTOFF,      /* 0..1000 = 20 Hz .. 18 kHz, exponential (a knob's detent: 10; with SEL held: 1) */
    GP_RESO,        /* 0..1000; self-oscillates near the top (a detent: 10; with SEL held: 1) */
    GP_MODE,        /* LP, BP, HP */
    GP_DRIVE,       /* 0..100: into the filter, soft saturation */
    GP_LEVEL,       /* the drone's level */
    GP_PARTNER,     /* the partner oscillator on (1) / off (0): a switch */
    GP_OLFO,        /* the oscillator's LFO tap: DR_LFO_OFF, _I, _II, _BOTH (a switch) */
    GP_OLFO_DEPTH,  /* -100..100: up to +-24 semitones (pitch) or the whole X-WAVE range at 100; 0 none */
    GP_OLFO_DEST,   /* what the oscillator's LFO moves: DR_TO_PITCH, DR_TO_XWAVE (a switch) */
    GP_FLFO,        /* the filter's LFO tap: DR_LFO_OFF, _I, _II, _BOTH (a switch) */
    GP_FLFO_DEPTH,  /* -100..100: up to +-5 octaves of cutoff, or the whole resonance range at 100; 0 none */
    GP_FLFO_DEST,   /* what the filter's LFO moves: DR_TO_CUTOFF, DR_TO_RESO (a switch) */
    GP_NOISE,       /* NOISE, 0..100: its level, added to the oscillators (they stay as they are); 100 is level
                     * with a saw, 50 about -7 dB, 25 -12 */
    GP_NCOLOR,      /* the noise's COLOUR, 0..100: white .. pink (-3 dB/oct), a blend (v0.2; it was 0 / 1) */
    GP_N
};
#define DR_GP(g, p) ((g) * GP_N + (p))

/* global parameters, after the drones' */
enum {
    P_XMOD = DR_NGROUPS * GP_N,     /* CROSS-MOD: 0 off, 1 drone I moves drone II's cutoff, 2 (v0.3) cross-FM: each drone's
                                     * sound moves the other's pitch, 3 (v0.3) Snarl: the finished sound moves both
                                     * (DR_XMOD_*); X-mod amt the depth of each */
    P_XMOD_AMT,                     /* 0..100: how far drone I's sound moves drone II's cutoff */
    P_SYNC,                         /* drone II's oscillator hard-synced to drone I's (1) / free (0): a switch */
    P_PLINK,                        /* drone II's pitch linked to drone I's (1) / its own (0): a switch */
    P_PLINK_OFF,                    /* linked: drone II's pitch = drone I's + this, in cents (-2400..2400) */
    P_LFO_RATE,                     /* 0..152: a cycle in 100 - v s to v 90 (10 s), then 0.1 * 700^((v - 90) / 62) Hz to 70 Hz */
    P_LFO_W1,                       /* Wave I: 0 off, 1..10 (DR_LFO_W1_NAME) (a switch) */
    P_LFO_W2,                       /* Wave II: 0 off, 1..10 (DR_LFO_W2_NAME) (a switch) */
    P_GLIDE,                        /* 0..100 = 0 .. 10 s to reach a new pitch (quadratic); 0 by default: a pitch
                                     * knob answers at once */
    P_FADE,                         /* 0..100 = 0.05 .. 60 s (a cube: 1 s at 25), the PLAY fade and the drone fades */
    P_DRIFT,                        /* 0..100: DRIFT, the drones never quite still: pitch (+-8 cents), and slow
                                     * random wanders of each drone's cutoff, X-WAVE, level and detune */
    P_WIDTH,                        /* stereo: 0 mono .. 100 drone I left, drone II right */
    P_TUNE,                         /* A's pitch in tenths of a Hz from 440: -130..130 = 427.0 .. 453.0 Hz (v0.3, Evan
                                     * 2026-11-01; cents, -50..50, before PROJ_FORMAT 41) */
    P_PULSE,                        /* the PULSE on (1) / off (0): both drones chopped in time (a switch) */
    P_PULSE_RATE,                   /* 0..100 = 0.03 .. 16 pulses a second, exponential (a 33 s breath .. a buzz) */
    P_PULSE_DEPTH,                  /* 0..100: how far the level drops between pulses */
    P_PULSE_SHAPE,                  /* -100..100: 0 a smooth swell; up to 100 a ramp, rising slowly and falling
                                     * quickly (a user's ask, 2026-10-25); down to -100 firmer, a hard gate at the
                                     * bottom (the other way round before PROJ_FORMAT 38) */
    P_PULSE_SWING,                  /* 0..50: every other pulse longer (50: 3 to 1) */
    P_DLY_TIME,                     /* the DELAY: 70 ms * (450/70)^(v/100): 0..100 70 .. 450 ms as since 0.1, on to
                                     * DR_DLY_TMAX, 1.46 s (2026-10-25: the line at half rate) */
    P_DLY_REPEATS,                  /* 0..100: its feedback; past ~85 it runs away (held by a soft clip) */
    P_DLY_MIX,                      /* 0..100: dry .. wet */
    P_REV_MIX,                      /* the REVERB: 0..100, dry .. wet */
    P_REV_TYPE,                     /* Room, Hall, Cathedral, Chorus hall (DR_REV_NAME) */
    P_DLY_LFO,                      /* the delay time's LFO tap: DR_LFO_I, _II, _BOTH (a switch) */
    P_DLY_LFO_DEPTH,                /* -100..100: up to +-25 % of the time; 0 none */
    P_LFO_LOCK,                     /* the LFO restarts at each Pulse bar (two beats) while the pulse runs (1) /
                                     * runs free (0) (a switch) */
    P_LFO_RMOD,                     /* -100..100: the LFO's own wave moving its rate, up to +-3 octaves (linear); 0 none */
    P_LFO_RMOD_SRC,                 /* which wave moves it: DR_LFO_I, _II, _BOTH */
    P_MORPH,                        /* 0..100 = 0.02 .. 30 s (x^2.5: 1 s at 25): how long a switch (a button or key) takes to move
                                     * the sound to its new setting: OCT, filter mode, pulse, partner, sub, the
                                     * links, the LFO routes */
    P_SCENE_T,                      /* 0..100 = 0.1 s .. 5 min (x^4: 1 s at 23): a scene's morph (the white keys) */
    P_II_OUT,                       /* the drones faded out on their own, a sound's: bit 0 drone II (SEL + PLAY; P12
                                     * Clean: one oscillator), bit 1 drone I (0.3.1, HOME + PLAY; DR_OUT). The same
                                     * slot and format, so 0.3 reads a 0.3.1 save; was Tuning until 30, 0 since */
    P_SPEAKER,                      /* 0..100: a low cut for a small speaker, 0 off (headphones) .. 240 Hz */
    P_PLOCK,                        /* the pitches locked (1): the pitch knobs do nothing (a switch) */
    P_MOVE,                         /* MOVEMENT, 0..100 = every LFO depth x 0 .. x 2 (50: as set) */
    P_FEEDBACK,                     /* 0..100: the finished sound (after the effects) fed back into both drones'
                                     * filters through their drive: thicker, then howling, held by the clip */
    P_CHAR,                         /* CHARACTER, 0..100: clean .. vintage oscillators (softer edges, a bent
                                     * saw, even harmonics, pitch jitter, the drones a little apart, noise) */
    /* THE MACROS (v0.2): offsets on top of the sound (drone.c MACRO_MV), the sound's own values untouched. These
     * four were the set macros (Bloom, Weight, Tide, Tear) until 2026-10-18 */
    P_WANDER,                       /* WANDER, 0..100: drone II plays a slow random melody, jumping between the
                                     * notes of a scale on drone I (P_WSCALE), up to an octave away; how often:
                                     * 25-30 s apart at 1 .. 0.5-2 s at 100 (GLO's Glide glides it); 0 still (Pitch) */
    P_AGE,                          /* AGE, 0..100: the sound worn, like an old tape: darker, a slow wow, hiss,
                                     * more character; it wears in (and out) slowly (Life) */
    P_SHIFT_MOVE,                   /* Shift's move in cents, 0..1400 = a fifth down .. none (700) .. a fifth up (a
                                     * detent 10, with SEL held 1) */
    P_WSCALE,                       /* Wander's scale (DR_WSCALE_NAME), 0 minor pentatonic (Moves) */
    P_GD_0, P_GD_1, P_GD_2, P_GD_3,      /* unused since 2026-10-18 (each key's depth: now DR_GEST_DEPTH) */
    P_GR_0, P_GR_1, P_GR_2, P_GR_3,      /* unused since 2026-10-18 (each key's time: now DR_GEST_TIME) */
    P_GL_0, P_GL_1, P_GL_2, P_GL_3,      /* ... latched at, 0..100 (0: not latched) */
    /* THE EFFECTS (v0.2, ChoralRoot's design) */
    P_DLY_TYPE,                     /* the delay's type: Clean, Wide, Ping-pong, Tape, Grain, Self (DR_DLY_NAME) */
    P_REV_LEN,                      /* the reverb's LENGTH: 0..99 = 0.3 .. 40 s (its decay time), 100 Inf (a freeze) */
    P_REV_DAMP,                     /* 0..100: how dark its tail gets, open .. ~600 Hz (out at Inf) */
    P_PRESS_SENS,                   /* the keys' pressure sensitivity, 0..100 (50: as tuned; a room setting) */
    P_DLY_DRIVE,                    /* DRIVE after the delay (v0.3, the Lyra-8's): 0..100, the drones and their repeats
                                     * distorted together, before the reverb; the loudness held */
    P_DLY_ROUTE,                    /* what goes into the reverb (v0.3, DR_ROUTE_NAME): 0 both, the drones and their
                                     * echoes; 1 the echoes alone (the drones dry, the wash added); 2 the drones alone (the
                                     * reverb beside the delay, the echoes dry) */
    P_PULSE_DIV,                    /* the Swell's breath with a MIDI clock in (v0.3, step 3; DR_DIV_NAME): 0 32 bars .. 7 a
                                     * quarter note; without a clock P_PULSE_RATE as before */
    P_LFO_DIV,                      /* ... the LFO's cycle (DR_LDIV_NAME): 0 32 bars .. 9 a sixteenth (v0.3) */
    P_DLY_NOTE,                     /* ... the delay's time (DR_NOTE_NAME): 1/16 .. 1 bar (v0.3) */
    P_SCENE_BARS,                   /* ... the Scene time (DR_BARS_NAME): 1 .. 16 bars, landing on a bar line (a room
                                     * setting, v0.3) */
    P_GL_4, P_GL_5, P_GL_6, P_GL_7,      /* the pressure keys' gestures (G4-C5: Tick, Chord, Snarl, Stutter) latched at,
                                          * 0..100 (0: not latched; v0.3) */
    P_SUBW_0, P_SUBW_1,                  /* each drone's sub wave (DR_SUBW_NAME): Square (as before), Sine, Triangle, Saw,
                                          * each as loud (RMS) as the square (v0.3, Evan 2026-10-28) */
    P_TUNING,                       /* where a pitch knob lands when it stops (DR_TUNING_NAME, the UI's; v0.3, Evan
                                     * 2026-10-30; a sound's since 2026-11-08, ledger 3): 0 12-tone .. the 22 shrutis */
    P_CLK_SYNC,                     /* follow a MIDI clock (1, as since v0.3's step 3) or not (0: the clock ignored, its
                                     * tempo forgotten); a room setting (v0.3, Evan 2026-11-01, ledger item 5) */
    DR_NPARAMS
};

enum { DR_LP, DR_BP, DR_HP };
enum { DR_SINE, DR_TRI, DR_SAW, DR_SQUARE };          /* the X-WAVE's four waves, in its order */
enum { DR_LFO_OFF, DR_LFO_I, DR_LFO_II, DR_LFO_BOTH };   /* a route's wave; BOTH: I + II, summed */
enum { DR_TO_PITCH, DR_TO_XWAVE };
enum { DR_TO_CUTOFF, DR_TO_RESO };
#define DR_NGEST 8                             /* the gesture keys: D5-G5 (by time), G4-C5 (by pressure, v0.3) */
enum { DR_G_WEIGHT, DR_G_SHIFT, DR_G_CLEAN, DR_G_WASH, DR_G_TICK, DR_G_CHORD, DR_G_SNARL, DR_G_STUTTER,
       DR_NGTYPE };                       /* what each key plays, fixed: D5-G5, then G4-C5 */
#define DR_SHIFT_MID 700                       /* Shift's move: none (cents + 700) */
extern const char *const DR_GEST_NAME[DR_NGTYPE];
extern const uint8_t DR_GEST_DEPTH[DR_NGTYPE], DR_GEST_TIME[DR_NGTYPE];   /* each one's full depth and its time */
#define DR_NWSCALE 6                           /* Wander's scales */
extern const char *const DR_WSCALE_NAME[DR_NWSCALE];
void dr_gesture(int g, int amt);               /* gesture key g's amount now, 0..1000 (the UI swells it) */
float dr_wander(void);                         /* Wander's move of drone II now, semitones (the screen) */
float dr_wander_to(void);                      /* ... and where it's going (the screen) */
int dr_wander_degs(float *deg);                /* Wander's scale over drone I, on the Tuning, semitones; how many
                                                * (up to 8; the screen) */
float dr_tun_iv(float cur, int j);             /* a scale's note on the Tuning over drone I, semitones:
                                                * cur on 12-tone, j its pure size, cents (Wander, Chord; tests) */
float dr_tun_over(int g, float cur, int j);    /* ... over drone g */
float dr_partner_semi(int g, int iv);          /* drone g's partner interval iv as heard on the Tuning, semitones */
float dr_shift_semi(int g);                    /* Shift's move as heard over drone g on the Tuning, semitones */
int dr_shift_on(int t, int p1, int ct);        /* Shift's ct (cents) a note of Tuning t over drone I (p1) */
int dr_shift_step(int t, int p1, int ct, int n);   /* ... n of its notes on, within +-700 */
int dr_heard(int p);                           /* a value as heard: the sound's own plus the macros (tests) */
float dr_mac_pitch(int g);                     /* drone g's pitch move by the macros now, semitones (tests) */
float dr_gest_pitch(int g);                    /* ... by the gestures alone (Shift's; the screen) */
#define DR_LFO_NSHAPES 11                               /* each tap: off and ten shapes */
#define DR_LFO_RANDOM 8                                 /* the eighth: random (I: steps, II: a glide) */
#define DR_LFO_AND 9                                    /* the Lyra-8's: two squares, both up (AND) .. */
#define DR_LFO_OR 10                                    /* .. or added (OR), 2 or 4 steps (v0.3) */
extern const char *const DR_LFO_W1_NAME[DR_LFO_NSHAPES];
extern const char *const DR_LFO_W2_NAME[DR_LFO_NSHAPES];
#define DR_PITCH_LO (-2400)                    /* 2 Hz, in cents over MIDI note 0 (v0.3, the Lyra-8's: under ~20 Hz a
                                                * drone is a pulse of clicks; was 27 Hz) */
#define DR_PITCH_NOTE 2068                     /* 27 Hz (A0), the DS-2's bottom: under it the screen shows Hz */
#define DR_PITCH_HI 9135                       /* 1.6 kHz */
#define DR_NREV 5
extern const char *const DR_REV_NAME[DR_NREV];          /* "Room", "Hall", "Cathedral", "Chorus hall", "Shimmer" */
#define DR_DLY_TMAX 163                                   /* the delay's longest Time: 1455 ms */
#define DR_NDIV 8
extern const char *const DR_DIV_NAME[DR_NDIV];          /* "32 bars" .. "1/4" */
#define DR_NLDIV 10
extern const char *const DR_LDIV_NAME[DR_NLDIV];        /* "32 bars" .. "1/16" */
#define DR_NNOTE 11
extern const char *const DR_NOTE_NAME[DR_NNOTE];        /* "1/16" .. "1 bar" */
extern const float DR_NOTE_BEATS[DR_NNOTE];
#define DR_NBARS 5
extern const char *const DR_BARS_NAME[DR_NBARS];        /* "1 bar" .. "16 bars" */
extern const uint8_t DR_BARS_N[DR_NBARS];
/* the MIDI clock in (v0.3), set by the UI: samples a beat (0: none heard yet; kept when it stops), the beat it's at
 * (counted from the clock's pulses), a count bumped with each update, and a Start (the Swell back to its top) */
extern volatile float dr_ext_spb, dr_ext_beat;
extern volatile uint32_t dr_ext_seq, dr_ext_start;
#define DR_XMOD_CUT 1
#define DR_XMOD_FM 2
#define DR_XMOD_OUT 3                                     /* SNARL: the finished sound bends both drones */
#define DR_NROUTE 3
#define DR_NSUBW 4                             /* the sub's waves */
enum { DR_SUBW_SQUARE, DR_SUBW_SINE, DR_SUBW_TRI, DR_SUBW_SAW };
extern const char *const DR_SUBW_NAME[DR_NSUBW];
#define DR_NTUNING 11                          /* the pitch knobs' grids: 12-tone, quarter tones, 19-, 31-, 53-tone equal,
                                                * just intonation, La Monte Young's (The Well-Tuned Piano's), the
                                                * harmonics, the 22 shrutis, Lou Harrison's (his National Steel
                                                * guitar's), Harry Partch's 43 */
enum { DR_TUN_12, DR_TUN_24, DR_TUN_19, DR_TUN_31, DR_TUN_53, DR_TUN_JUST, DR_TUN_HARM, DR_TUN_LMY, DR_TUN_HARRISON,
       DR_TUN_SHRUTI, DR_TUN_PARTCH };                 /* the equal ones by size, then the rest simplest to richest
                                                        * (Evan, 2026-10-30; PROJ_FORMAT 40) */
extern const char *const DR_TUNING_NAME[DR_NTUNING];
#define DR_TUN_MOVES(t) ((t) == DR_TUN_JUST || (t) == DR_TUN_SHRUTI)   /* movable: built on drone I (the tonic,
                                                * do or Sa), not on A: pure from wherever drone I is */
int dr_tun_n(int t);                           /* the Tuning t's notes an octave */
int dr_tun_c(int t, int i);                    /* ... its note i, cents over its root */
int dr_tun_name(int t, int p1, int a, int b, char *out);   /* the interval a to b (cents) on Tuning t (drone I on p1) in
                                               * its own terms: "11\19", "7/4"; 0: none (12-tone, unison, off its notes) */
int dr_grid_k(int t, int v, int root);         /* the index of the note nearest v (cents), the grid on root */
int dr_grid_at(int t, int k, int root);        /* ... the note k, cents (0 the root, n its octave) */
extern const char *const DR_ROUTE_NAME[DR_NROUTE];      /* "Both", "Echoes", "Drones" */
#define DR_NDLY 6
#define DR_DLY_TAPE 3                                     /* the starting type */
extern const char *const DR_DLY_NAME[DR_NDLY];          /* "Clean", "Wide", "Ping-pong", "Tape", "Grain", "Self" */
#define DR_NINTERVALS 17
extern const float DR_IV_SEMI[DR_NINTERVALS];           /* semitones: -Oct, -5th, -5th just .. +Oct+5 just */
#define DR_IV_UNISON 5

typedef struct {
    const char *name;
    int16_t lo, hi, def;
} dr_param_t;
const dr_param_t *dr_param_info(int i);
void dr_param_text(int i, int v, char *buf);   /* the value as the screen shows it (12 bytes) */

/* commands (main loop -> render) */
void dr_init(void);
void dr_set(int param, int value);
void dr_play(int on);                          /* fade the whole drone in (1) / out (0) over P_FADE */
void dr_voice(int group, int on);              /* fade one drone in / out */
/* the drone's next pitch change slides there in a straight line over ms (1..30000), instead of the Glide:
 * a pitch knob sends each detent's change with the time until the next detent is expected, so a turn at
 * any speed is one continuous slide */
void dr_slide(int group, int ms);
/* the next change of parameter p glides there over ms (1..30000), as a knob's detent does; otherwise a
 * change glides over 25 ms */
void dr_pslide(int param, int ms);
void dr_panic(void);                           /* silence now, no fade */
void dr_duck(int on);                          /* the output dips out (20 ms) or back (30 ms): around a flash write */
void dr_lfo_reset(void);                       /* the LFO back to the start of its cycle (the DS-2's RESET) */
void dr_seed(uint32_t s);                      /* mix s into the random generators (LFO, Drift): each run its own */

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void dr_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* state for the screen (UI) */
extern volatile float dr_level[DR_NGROUPS];    /* each drone's output level, 0..~1 (peak, decaying) */
extern volatile float dr_fade;                 /* the PLAY fade, 0..1 */
extern volatile uint8_t dr_phones;             /* headphones in: the Speaker low cut stays out (the UI sets it) */
extern volatile float dr_duck_level;           /* the dip now, 1 (none) .. 0 (out) */
extern volatile float dr_tail;                 /* the effects' output, the last block's peak (0 resting): a save
                                                * (its erase cuts the audio for a few ms) dips first while it's heard */
extern volatile float dr_lfo[2];               /* the LFO taps now, -1..1 (the DS-2's rate lights) */
extern volatile float dr_pulse;                /* the pulse's gain now, 0..1 (1: open, or off) */
extern volatile float dr_lfo_ph;               /* the LFO's phase now, 0..1 (cycles) */
extern volatile float dr_dly_ms;              /* the delay's time now, with its LFO, ms (the host's probe) */
extern volatile float dr_cut[DR_NGROUPS];      /* each drone's cutoff now, gliding, with its LFO (knob units 0..100) */
extern volatile float dr_xw[DR_NGROUPS];       /* ... its X-WAVE now, with its LFO and Wander (knob units 0..100), and */
extern volatile float dr_reso[DR_NGROUPS];     /* its resonance (0..1000); both updated while the drones play */
extern volatile float dr_fb;                   /* FEEDBACK's share of the sound now, 0..1 (v0.3; was the loop's level) */
extern volatile float dr_snarl;                /* SNARL's bend now (how far it moves the drones' speed), 0..1 */
extern volatile float dr_pulse_ph;             /* the pulse's phase now, 0..2 (its two beats) */
float dr_lfo_wave(int tap, int shape, float ph);   /* a tap's shape (1..10; 0 off: flat) at ph, -1..1; for
                                                    * random, ph 0..1 the last cycle, 1..2 this one */
float dr_lfo_rmod_k(float rm, int src, int w1, int w2);  /* rate mod's speed scale (the screen's picture) */
float dr_pulse_wave(float ph);                 /* the pulse's gain at ph (0..2) as set now, 0..1 */
float dr_time_s(int p, int v);                 /* Fade, Morph, Scene time at v, seconds (v0.3: a power curve, 1 s
                                                * near a quarter of the dial; Evan, 2026-11-07) */
float dr_pulse_hz(int v);                      /* breaths a second at Rate v, or now (-1: the Rate, or the clock's) */
/* Age and Character at full (Evan, 2026-11-02: "stronger"; the pictures on Life read them too) */
#define AGE_CUT -250                    /* Age: the cutoff, of its 1000, about 2.5 octaves darker (was -70) */
#define AGE_NZ 20                       /* its hiss (was 15) */
#define AGE_WOW 25                      /* its wow, +-cents (was 14; the flutter +-4) */
#define AGE_MK 5.0f                     /* dB of level made up, so it darkens without going quieter */
#define CHR_K 4.0f                      /* Character: the highs rolled off from 8 kHz / 2^CHR_K, 500 Hz (was 1.2 kHz) */
#define CHR_ASYM 0.35f                  /* the saw's bend, lifting the even harmonics (was 0.25) */
#define CHR_JIT 0.8f                    /* the pitch jitter (was 0.4) */
float dr_age_now(float *ph);                   /* Age as heard now (its 20 s wear-in), 0..1; *ph its wow's phase, s */
/* each drone's sound, for the screen: DR_SCOPE_N points spanning about two cycles of its oscillator,
 * starting where a cycle starts (so the picture stands still), as it sounds (after its filter and level);
 * renewed about 30 times a second */
#ifndef DR_SCOPE_N                              /* (Fantasma 2.0: niagara.c, no screen of Niagara's: 4) */
#define DR_SCOPE_N 104
#endif
extern volatile float dr_scope[DR_NGROUPS][DR_SCOPE_N];
extern volatile uint32_t dr_scope_t0[DR_NGROUPS];   /* each capture's start, in samples of one clock for both */
extern volatile float dr_scope_ci[DR_NGROUPS];      /* ... its oscillator's cycles a sample then */
extern volatile uint16_t dr_scope_dec[DR_NGROUPS];  /* ... samples a point */
extern volatile float dr_pitch[DR_NGROUPS];    /* each drone's pitch now (MIDI note, fractional while sliding) */
