/* SPDX-License-Identifier: GPL-3.0-only */
/* The drone engine (drone.h). One compilation unit, -O2.
 *
 * Oscillators: naive saw and pulse corrected with polyBLEP (two-sample polynomial band-limited
 * steps), a naive triangle (its harmonics fall at 12 dB/oct, so its aliasing stays low). Filter: the
 * trapezoidal (TPT) state-variable filter after Andrew Simper's "SvfLinearTrapOptimised2" paper, with
 * a soft clip on its band-pass state so it can self-oscillate without running away. Everything that
 * needs an exp2 or a tan (pitch, glide, cutoff) is computed every CR samples, the control rate. */
#include "drone.h"
#include "fastmath.h"

/* THE HOT LOOPS IN RAM (0.3.1; the way of fm1-base's PLAT_RAM_CODE, Henri David's): the FM-1 runs its code from the
 * flash chip through a small cache, and a loop bigger than the cache keeps waiting for the flash. The drones' loop
 * (render_group) and the effects' (fx_run, drone_fx.c) are copied to RAM at power-on (section .app_ram, in the RAM
 * code region beside the flash driver: app.ld) and run there: the same code, the same samples, less time. Measured on
 * the FM-1 with a heavy sound (Thomas Loop's, 2026-11-17), the drones' loop alone: 83-88 % average and blocks to 94 %
 * became 74-78 % and 85 %. The build sets OM_RAM_CODE=1; OM_RAM_CODE=0 ./build.sh leaves both in the flash (to
 * compare); the host and the emulator have no such section. */
#ifndef OM_RAM_CODE
#define OM_RAM_CODE 0
#endif
#if OM_RAM_CODE
#define DR_RAM_CODE __attribute__((section(".app_ram"), noinline, flatten))   /* flatten: every helper inlined (a call
                                                                                 * from RAM doesn't reach the flash) */
#define DR_INL __attribute__((always_inline))   /* a helper flatten left out of line somewhere */
#define DR_FAR(fn) fn##_far                    /* a call from RAM to a function left in the flash: through a pointer
                                                * (once a block, never a sample) */
#define DR_FAR_FN(ret, fn, args) static ret (*volatile const fn##_far) args = fn;
#else
#define DR_RAM_CODE
#define DR_INL
#define DR_FAR(fn) fn
#define DR_FAR_FN(ret, fn, args)
#endif

#define BARRIER() __asm__ volatile("" ::: "memory")
#define CR 16u                                  /* control rate: one update every 16 samples */
#ifndef BLK                                     /* (Fantasma 2.0: niagara.c sets it to Fantasma's block, 32) */
#define BLK 256u
#endif

/* the partner's intervals, each tempered and then just (pure ratios: 2/3, 3/4, 6/5, 5/4, 4/3, 3/2, 3; a just one
 * doesn't beat, Detune still can), so Tuning is the Interval knob's (Evan, 2026-10-18); octaves and unison are both */
const float DR_IV_SEMI[DR_NINTERVALS] = {-12.0f, -7.0f, -7.01955f, -5.0f, -4.98045f, 0.0f, 3.0f, 3.15641f, 4.0f,
                                         3.86314f, 5.0f, 4.98045f, 7.0f, 7.01955f, 12.0f, 19.0f, 19.01955f};
static const char *const INTERVAL_NAME[DR_NINTERVALS] = {"-Oct", "-5th", "-5th just", "-4th", "-4th just", "Unison",
                                                         "+m3", "+m3 just", "+M3", "+M3 just", "+4th", "+4th just",
                                                         "+5th", "+5th just", "+Oct", "+Oct+5", "+Oct+5 just"};

/* ------------------------------------------------------------ parameters --- */
#define VOICE_PARAMS(pitch, wave, ival, mix, cut, level, olfo, flfo, fdep)                                           \
    {"Pitch", DR_PITCH_LO, DR_PITCH_HI, pitch}, {"X-Wave", 0, 100, wave}, {"Width", 0, 100, 30},               \
        {"Interval", 0, DR_NINTERVALS - 1, ival}, {"Detune", 0, 100, 12}, {"Osc/Sub", 0, 100, mix},            \
        {"Sub oct", 1, 6, 1}, {"Cutoff", 0, 1000, cut}, {"Reso", 0, 1000, 250}, {"Mode", DR_LP, DR_HP, DR_LP},    \
        {"Drive", 0, 100, 20}, {"Level", 0, 100, level}, {"Partner", 0, 1, 1},                                 \
        {"Osc LFO", DR_LFO_OFF, DR_LFO_BOTH, olfo}, {"Osc depth", -100, 100, 0},                                \
        {"Osc LFO to", DR_TO_PITCH, DR_TO_XWAVE, DR_TO_PITCH}, {"Filter LFO", DR_LFO_OFF, DR_LFO_BOTH, flfo},     \
        {"Filt depth", -100, 100, fdep}, {"Filt LFO to", DR_TO_CUTOFF, DR_TO_RESO, DR_TO_CUTOFF},           \
        {"Noise", 0, 100, 0}, {"Color", 0, 100, 100}

/* THE STARTING SOUND (2026-10-17, Evan: Lyra-like; measured against two Lyra-8 recordings: changes mostly slower
 * than one every two seconds, ~16 dB of slow loudness swell, ~1.6 octaves of brightness): each voice's LFO taps
 * start routed (I or II), the filters breathing deep and against each other (+85 drone I, -85 drone II) on a
 * slow LFO (a cycle in ~25 s; Wave II a random glide), the Swell on as a slow breath (one in ~35 s, 12 dB),
 * some delay with a gentle wobble, a little more reverb; the oscillators' depths start at 0. The cutoffs 1 kHz and
 * 1.2 kHz (Evan, 2026-11-08; 2.3 and 2.0 before, where half of each sweep went above what a D3 drone lets you hear),
 * the levels 84 and 68 for the same loudness */
static const dr_param_t PARAMS[DR_NPARAMS] = {
    VOICE_PARAMS(5000, 67, DR_IV_UNISON, 30, 575, 84, DR_LFO_I, DR_LFO_I, 85),     /* I: D3, saw, partner in unison */
    VOICE_PARAMS(5700, 33, DR_IV_UNISON + 9, 15, 600, 68, DR_LFO_II, DR_LFO_II, -85),  /* II: A3, triangle, partner an octave up */
    {"Cross-mod", 0, DR_XMOD_OUT, 0}, {"X-mod amt", 0, 100, 40}, {"Hard sync", 0, 1, 0}, {"Pitch link", 0, 1, 0},
    {"Link ofs", -2400, 2400, 700}, {"LFO rate", 0, 152, 74}, {"Wave I", 0, DR_LFO_NSHAPES - 1, 1},
    {"Wave II", 0, DR_LFO_NSHAPES - 1, 8}, {"Pitch glide", 0, 100, 7}, {"Fade", 0, 100, 29}, {"Drift", 0, 100, 30},
    {"Stereo", 0, 100, 60}, {"Tune", -130, 130, 0},
    {"Pulse", 0, 1, 1}, {"Rate", 0, 100, 6}, {"Depth", 0, 100, 40}, {"Shape", -100, 100, -25}, {"Swing", 0, 50, 0},
    {"Time", 0, DR_DLY_TMAX, 50}, {"Repeats", 0, 100, 50}, {"Dly mix", 0, 100, 25}, {"Rev mix", 0, 100, 35},
    {"Type", 0, DR_NREV - 1, 2}, {"Dly LFO", DR_LFO_I, DR_LFO_BOTH, DR_LFO_I}, {"Delay mod", -100, 100, 20},
    {"LFO lock", 0, 1, 0}, {"Speed mod", -100, 100, 0}, {"Source", DR_LFO_I, DR_LFO_BOTH, DR_LFO_II},
    {"Morph", 0, 100, 34}, {"Scene time", 0, 100, 40}, {"II out", 0, 3, 0}, {"Speaker", 0, 100, 0},
    {"Pitch lock", 0, 1, 1}, {"Movement", 0, 100, 50}, {"Feedback", 0, 100, 0}, {"Character", 0, 100, 0},
    {"Wander", 0, 100, 0}, {"Age", 0, 100, 0}, {"Shift move", 0, 2 * DR_SHIFT_MID, 2 * DR_SHIFT_MID}, {"Scale", 0, DR_NWSCALE - 1, 0},
    {"D5 depth", 0, 100, 70}, {"E5 depth", 0, 100, 100}, {"F5 depth", 0, 100, 100}, {"G5 depth", 0, 100, 70},
    {"D5 time", 0, 100, 30}, {"E5 time", 0, 100, 40}, {"F5 time", 0, 100, 35}, {"G5 time", 0, 100, 35},
    {"D5 latch", 0, 100, 0}, {"E5 latch", 0, 100, 0}, {"F5 latch", 0, 100, 0}, {"G5 latch", 0, 100, 0},
    {"Type", 0, DR_NDLY - 1, DR_DLY_TAPE}, {"Length", 0, 100, 60}, {"Damp", 0, 100, 40}, {"Pressure", 0, 100, 50},
    {"Drive", 0, 100, 0}, {"Into rev", 0, DR_NROUTE - 1, 0}, {"Division", 0, DR_NDIV - 1, 2},
    {"LFO div", 0, DR_NLDIV - 1, 3}, {"Dly note", 0, DR_NNOTE - 1, 5}, {"Scene bars", 0, DR_NBARS - 1, 2},
    {"G4 latch", 0, 100, 0}, {"A4 latch", 0, 100, 0}, {"B4 latch", 0, 100, 0}, {"C5 latch", 0, 100, 0},
    {"Sub wave", 0, DR_NSUBW - 1, DR_SUBW_SQUARE}, {"Sub wave", 0, DR_NSUBW - 1, DR_SUBW_SQUARE},
    {"Tuning", 0, DR_NTUNING - 1, DR_TUN_12},
    {"Sync", 0, 1, 1},
};
const char *const DR_REV_NAME[DR_NREV] = {"Room", "Hall", "Cathedral", "Chorus hall", "Shimmer"};
const char *const DR_DIV_NAME[DR_NDIV] = {"32 bars", "16 bars", "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4"};
static const float DIV_BEATS[DR_NDIV] = {128.0f, 64.0f, 32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f};   /* a breath, beats */
const char *const DR_LDIV_NAME[DR_NLDIV] = {"32 bars", "16 bars", "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4",
                                            "1/8", "1/16"};
static const float LDIV_BEATS[DR_NLDIV] = {128.0f, 64.0f, 32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f};   /* a cycle */
const char *const DR_NOTE_NAME[DR_NNOTE] = {"1/16", "1/8T", "1/8", "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2", "1/2D",
                                            "1 bar"};
const float DR_NOTE_BEATS[DR_NNOTE] = {0.25f, 1.0f / 3.0f, 0.5f, 2.0f / 3.0f, 0.75f, 1.0f, 4.0f / 3.0f, 1.5f, 2.0f, 3.0f, 4.0f};
const char *const DR_BARS_NAME[DR_NBARS] = {"1 bar", "2 bars", "4 bars", "8 bars", "16 bars"};
const uint8_t DR_BARS_N[DR_NBARS] = {1, 2, 4, 8, 16};
volatile float dr_ext_spb, dr_ext_beat;
volatile uint32_t dr_ext_seq, dr_ext_start;
/* the beat now, on a MIDI clock: the UI's last count, carried on by the samples since (ext_n, advanced once a block in
 * pulse_run; on at the last tempo when the clock stops). 256 beats round (the UI's count's) */
static uint32_t ext_seq, ext_n;
static float ext_beat;
static float ext_beats(void)
{
    if (dr_ext_seq != ext_seq) {                         /* a fresh count from the UI */
        ext_seq = dr_ext_seq;
        ext_beat = dr_ext_beat;
        ext_n = 0;
    } else if (ext_n > (uint32_t)DR_SR) {               /* a second on its own: folded in (the float stays fine) */
        ext_beat += (float)ext_n / dr_ext_spb;
        ext_beat -= ext_beat >= 256.0f ? 256.0f : 0.0f;
        ext_n = 0;
    }
    return ext_beat + (float)ext_n / dr_ext_spb;
}
const char *const DR_ROUTE_NAME[DR_NROUTE] = {"Both", "Echoes", "Drones"};
const char *const DR_SUBW_NAME[DR_NSUBW] = {"Square", "Sine", "Triangle", "Saw"};
const char *const DR_TUNING_NAME[DR_NTUNING] = {"12-tone", "Quarter", "19-tone", "31-tone", "53-tone", "Just", "Harmonics",
                                                "LMY", "Harrison", "Shruti", "Partch"};
/* THE TUNINGS' notes (the pitch knobs', Wander's and Chord's; Evan, 2026-10-30): equal ones by their size; the rest
 * as tables of cents over the root, in one octave. The fixed ones are built on A, A 440 one of their notes, as
 * Ableton's tuning files place them; Just and Shruti move with drone I (DR_TUN_MOVES): pure over it, from their 1/1.
 * La Monte Young, the firmware's main inspiration (Evan): his Well-Tuned Piano's tuning (LMY), and the harmonics, as
 * his Dream House chords are */
static const uint8_t TUN_EDO[DR_TUN_JUST] = {12, 24, 19, 31, 53};
static const int16_t TUN_JUST[12] = {0, 112, 204, 316, 386, 498, 590, 702, 814, 884, 1018, 1088};   /* 5-limit
                                                  * (Ptolemy's, Ableton's 12 Quintal): from 1/1, 16/15 9/8 6/5 5/4 4/3
                                                  * 45/32 3/2 8/5 5/3 9/5 15/8 */
static const int16_t TUN_LMY[12] = {0, 27, 63, 267, 294, 498, 525, 702, 729, 765, 969, 996};   /* The Well-Tuned
                                                  * Piano's, placed as Ableton's file (12 Tertial-Septimal LMY WTP,
                                                  * after Kyle Gann) places it (27/16 over a C at C-6): from A, 64/63
                                                  * 28/27 7/6 32/27 4/3 256/189 3/2 32/21 14/9 7/4 16/9 (3s and 7s) */
static const int16_t TUN_HARM[8] = {0, 204, 386, 551, 702, 841, 969, 1088};   /* the odd harmonics 1-15 of A, in
                                                  * one octave: 1 9 5 11 3 13 7 15 */
static const int16_t TUN_SHRUTI[22] = {0, 90, 112, 182, 204, 294, 316, 386, 408, 498, 520, 590, 610, 702, 792, 814,
                                       884, 906, 996, 1018, 1088, 1110};   /* the 22 shrutis from Sa (Sambamurthy /
                                                  * Danielou's, 5-limit, as Ableton's 22 Sruti: MaL+ 64/45) */
static const int16_t TUN_HARRISON[12] = {0, 63, 182, 267, 347, 498, 561, 702, 765, 884, 996, 1049};   /* Lou
                                                  * Harrison's National Steel guitar tuning (11-limit, after Bill
                                                  * Alves), as Ableton's file places it (his 9/8 over G): from A,
                                                  * 28/27 10/9 7/6 11/9 4/3 112/81 3/2 14/9 5/3 16/9 11/6 */
static const int16_t TUN_PARTCH[43] = {0, 27, 63, 90, 112, 143, 182, 214, 231, 267, 294, 316, 347, 379, 414, 445, 477,
                                       498, 525, 561, 579, 610, 649, 680, 702, 729, 765, 792, 814, 831, 845, 884, 912,
                                       943, 975, 996, 1018, 1049, 1081, 1108, 1147, 1161, 1178};   /* Harry Partch's 43
                                                  * (11-limit, otonal and utonal), from A, his 9/8 over G (his own G
                                                  * 392 is A 441: Tune +4) */
static const int16_t *const TUN_TAB[DR_NTUNING - DR_TUN_JUST] = {TUN_JUST, TUN_HARM, TUN_LMY, TUN_HARRISON, TUN_SHRUTI,
                                                                     TUN_PARTCH};
static const uint8_t TUN_N[DR_NTUNING - DR_TUN_JUST] = {12, 8, 12, 12, 22, 43};
static int tun_ok(int t) { return (unsigned)t < DR_NTUNING ? t : DR_TUN_12; }
int dr_tun_n(int t)
{
    t = tun_ok(t);
    return t < DR_TUN_JUST ? TUN_EDO[t] : TUN_N[t - DR_TUN_JUST];
}
int dr_tun_c(int t, int i)
{
    t = tun_ok(t);
    return t < DR_TUN_JUST ? i * 1200 / TUN_EDO[t] : TUN_TAB[t - DR_TUN_JUST][i];
}
int dr_grid_k(int t, int v, int root)
{
    int x = v - root, o, r, i, n, best = 0, d = 99999;
    t = tun_ok(t);
    if (t < DR_TUN_JUST)
        return (int)fm_floorf((float)x * (float)TUN_EDO[t] * (1.0f / 1200.0f) + 0.5f);
    n = TUN_N[t - DR_TUN_JUST];
    o = (int)fm_floorf((float)x / 1200.0f);
    r = x - o * 1200;
    for (i = 0; i <= n; i++) {                   /* (the octave's root again, at the top) */
        int c = i < n ? TUN_TAB[t - DR_TUN_JUST][i] : 1200, e = c > r ? c - r : r - c;
        if (e < d) {
            d = e;
            best = i;
        }
    }
    return o * n + best;
}
int dr_grid_at(int t, int k, int root)
{
    int n, o;
    t = tun_ok(t);
    if (t < DR_TUN_JUST)
        return root + (int)fm_floorf((float)k * (1200.0f / (float)TUN_EDO[t]) + 0.5f);
    n = TUN_N[t - DR_TUN_JUST];
    o = (int)fm_floorf((float)k / (float)n);
    return root + o * 1200 + TUN_TAB[t - DR_TUN_JUST][k - o * n];
}
const char *const DR_DLY_NAME[DR_NDLY] = {"Clean", "Wide", "Ping-pong", "Tape", "Grain", "Self"};
const char *const DR_WSCALE_NAME[DR_NWSCALE] = {"Minor pent", "Major pent", "5ths + 8ves", "Whole tone", "Dorian",
                                                "Just 1-8"};

/* the Ultra LFO's shapes, after the DS-2's names; how each moves is ours (the DS-2 doesn't say) */
const char *const DR_LFO_W1_NAME[DR_LFO_NSHAPES] = {"Off", "Triangle", "Square", "Saw", "Roundtooth",
                                                    "2x Tri", "2x Exp", "4x Pattern", "Random", "AND 3", "OR 3"};
const char *const DR_LFO_W2_NAME[DR_LFO_NSHAPES] = {"Off", "Tri 90", "Square 90", "Exp saw", "Log tri",
                                                    "2x Saw", "2x Pattern", "4x Exp", "Rnd glide", "AND 5", "OR 5"};

const dr_param_t *dr_param_info(int i) { return (i >= 0 && i < DR_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }

static void wander_gap(int v, float *lo, float *hi)   /* WANDER at v (1..100): its notes this far apart, s */
{
    float a = (float)(v < 1 ? 1 : v > 100 ? 100 : v - 1) * (1.0f / 99.0f);
    *lo = 25.0f * fm_exp2f(-5.643856f * a);      /* 25 s .. 0.5 s */
    *hi = 30.0f * fm_exp2f(-3.906891f * a);      /* 30 s .. 2 s */
}

static void copy_s(char *b, const char *s)
{
    int k = 0;
    while (s[k] && k < 11) {                       /* up to 11 characters ("4x Pattern", "Roundtooth") */
        b[k] = s[k];
        k++;
    }
    b[k] = 0;
}

static void itoa_s(int v, char *b)
{
    char t[8];
    int n = 0, neg = v < 0;
    if (neg)
        v = -v;
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 6);
    if (neg)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

/* AN INTERVAL IN THE TUNING'S OWN TERMS (ledger 3a, Evan 2026-11-08: "follow how composers notate it"): on the equal
 * ones its steps over the division, as Ableton's tuning files and the xenharmonic writers do ("11\19", 19-tone's
 * fifth); on the others its ratio, from each Tuning's own (TUN_R, the same order as the cents tables; from the 1/1,
 * or from A for the ones built on it: an interval's ratio is the same), between the two notes' nearest. 0 when it has
 * no such name: 12-tone (its names are the familiar ones), unison, a gap more than 10 cents from that interval, a
 * ratio too long to fit ("1323/1024") */
typedef struct { uint16_t n, d; } dr_ratio_t;
static const dr_ratio_t TUN_R_JUST[12] = {{1, 1}, {16, 15}, {9, 8}, {6, 5}, {5, 4}, {4, 3}, {45, 32}, {3, 2}, {8, 5},
                                          {5, 3}, {9, 5}, {15, 8}};
static const dr_ratio_t TUN_R_HARM[8] = {{1, 1}, {9, 8}, {5, 4}, {11, 8}, {3, 2}, {13, 8}, {7, 4}, {15, 8}};
static const dr_ratio_t TUN_R_LMY[12] = {{1, 1}, {64, 63}, {28, 27}, {7, 6}, {32, 27}, {4, 3}, {256, 189}, {3, 2},
                                         {32, 21}, {14, 9}, {7, 4}, {16, 9}};
static const dr_ratio_t TUN_R_HARRISON[12] = {{1, 1}, {28, 27}, {10, 9}, {7, 6}, {11, 9}, {4, 3}, {112, 81}, {3, 2},
                                              {14, 9}, {5, 3}, {16, 9}, {11, 6}};
static const dr_ratio_t TUN_R_SHRUTI[22] = {{1, 1}, {256, 243}, {16, 15}, {10, 9}, {9, 8}, {32, 27}, {6, 5}, {5, 4},
                                            {81, 64}, {4, 3}, {27, 20}, {45, 32}, {64, 45}, {3, 2}, {128, 81}, {8, 5},
                                            {5, 3}, {27, 16}, {16, 9}, {9, 5}, {15, 8}, {243, 128}};
static const dr_ratio_t TUN_R_PARTCH[43] = {
    {1, 1}, {64, 63}, {28, 27}, {256, 243}, {16, 15}, {88, 81}, {10, 9}, {112, 99}, {8, 7}, {7, 6}, {32, 27},
    {6, 5}, {11, 9}, {56, 45}, {80, 63}, {128, 99}, {320, 243}, {4, 3}, {256, 189}, {112, 81}, {88, 63}, {64, 45},
    {16, 11}, {40, 27}, {3, 2}, {32, 21}, {14, 9}, {128, 81}, {8, 5}, {160, 99}, {44, 27}, {5, 3}, {320, 189},
    {512, 297}, {1280, 729}, {16, 9}, {9, 5}, {11, 6}, {28, 15}, {256, 135}, {64, 33}, {88, 45}, {160, 81}};   /* from A
                                                  * (his 9/8 over G): Partch's 43 */
static const dr_ratio_t *const TUN_R[DR_NTUNING - DR_TUN_JUST] = {TUN_R_JUST, TUN_R_HARM, TUN_R_LMY, TUN_R_HARRISON,
                                                                   TUN_R_SHRUTI, TUN_R_PARTCH};
static uint32_t gcd_u(uint32_t a, uint32_t b)
{
    while (b) {
        const uint32_t r = a % b;
        a = b;
        b = r;
    }
    return a;
}
#define TUN_NAME_OFF(d) ((d) > 10 || (d) < -10)   /* the gap this many cents from the named interval: no name (under
                                                  * half the smallest step, 53-tone's 11.3c: never the wrong step) */
int dr_tun_name(int t, int p1, int a, int b, char *out)   /* the interval from a to b (cents), its size alone */
{
    const int r = DR_TUN_MOVES(t) ? p1 : 900;
    int ka, kb, k;
    t = tun_ok(t);
    if (t == DR_TUN_12 || a == b)
        return 0;
    ka = dr_grid_k(t, a, r);
    kb = dr_grid_k(t, b, r);
    {   /* named by the two notes' nearest on the Tuning, when the gap heard is within 10 cents of theirs (Evan on the
         * FM-1, 2026-11-08: exactly on the notes was too rare, a drone left on D3 read "m6"; and a name for an
         * interval the drones aren't playing would mislead): else the familiar name */
        const int named = dr_grid_at(t, kb, r) - dr_grid_at(t, ka, r), heard = b > a ? b - a : a - b;
        if (ka == kb || TUN_NAME_OFF((named < 0 ? -named : named) - heard))
            return 0;
    }
    if (kb < ka) {
        k = ka;
        ka = kb;
        kb = k;
    }
    if (t < DR_TUN_JUST) {                       /* "11\19" */
        char *q = out;
        itoa_s(kb - ka, q);
        while (*q)
            q++;
        *q++ = '\\';
        itoa_s(TUN_EDO[t], q);
        return 1;
    }
    {
        const int n = TUN_N[t - DR_TUN_JUST], oa = ka >= 0 ? ka / n : -((n - 1 - ka) / n), ob = kb >= 0 ? kb / n :
                                                                                         -((n - 1 - kb) / n);
        const dr_ratio_t *R = TUN_R[t - DR_TUN_JUST], ra = R[ka - oa * n], rb = R[kb - ob * n];
        uint32_t num = (uint32_t)rb.n * ra.d, den = (uint32_t)rb.d * ra.n, g;
        for (k = ob - oa; k > 0; k--)            /* (b at or above a: its octaves over a's) */
            num *= 2u;
        g = gcd_u(num, den);
        num /= g;
        den /= g;
        if (num > 9999u || den > 999u || (num > 999u && den > 99u))
            return 0;                            /* more than 7 characters: it won't fit, the familiar name instead */
        itoa_s((int)num, out);
        while (*out)
            out++;
        *out++ = '/';
        itoa_s((int)den, out);
        return 1;
    }
}

/* the LFO's rate in Hz, 0..152: a cycle in 100 s, then every second down to 10 s (Evan on the FM-1, 2026-11-07: each
 * second on the dial), then on in steps of the same size (10 s to 9 s, x 700^(1/62), ~11 %) to 70 Hz: 1 / (100 - v)
 * to v 90, 0.1 * 700^((v - 90) / 62) after */
static float lfo_hz_f(float v)
{
    return v < 90.0f ? 1.0f / (100.0f - v) : 0.1f * fm_exp2f((v - 90.0f) * 0.152438f);
}
static float lfo_hz(int v) { return lfo_hz_f((float)v); }

/* the pulse's rate: 0.03 * 533^(v/100), 0.03 .. 16 a second (slow: a breath, a shruti's bellows) */
static float pulse_hz(int v) { return 0.03f * fm_exp2f((float)v * 0.0905875f); }

static void hz_text(float hz, char *b);
/* the rate as the screen shows it: "0.23Hz", "4.1Hz", "35Hz"; under 0.1 Hz its period: "21s" */
static void secs_text(float t, char *b);
static void rate_text(int v, char *b)
{
    float hz = lfo_hz(v);
    if (hz < 0.1005f) {                          /* slow, to 10 s: its period ("21s", "1m40") */
        secs_text(1.0f / hz, b);
        return;
    }
    hz_text(hz, b);
}

static void hz_text(float hz, char *b)
{
    int x;
    if (hz < 1.0f) {                             /* two decimals */
        x = (int)(hz * 100.0f + 0.5f);
        *b++ = '0';
        *b++ = '.';
        *b++ = (char)('0' + x / 10);
        *b++ = (char)('0' + x % 10);
    } else if (hz < 10.0f) {                     /* one */
        x = (int)(hz * 10.0f + 0.5f);
        itoa_s(x / 10, b);
        while (*b)
            b++;
        *b++ = '.';
        *b++ = (char)('0' + x % 10);
    } else {
        itoa_s((int)(hz + 0.5f), b);
        while (*b)
            b++;
    }
    copy_s(b, "Hz");
}

/* a pitch in cents as the nearest note and the cents from it: "A2 +14" */
static void pitch_text(int c, char *b)
{
    static const char *const NN[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    int n = (c + 50) / 100, d = c - 100 * n, o = n / 12 - 1;
    const char *s;
    if (c < DR_PITCH_NOTE) {                     /* under the DS-2's range (and the note names' table): in Hz */
        hz_text(8.1757989f * fm_exp2f((float)c * (1.0f / 1200.0f)), b);
        return;
    }
    s = NN[n % 12];
    while (*s)
        *b++ = *s++;
    *b++ = (char)('0' + o);
    if (d) {
        *b++ = ' ';
        *b++ = d > 0 ? '+' : '-';
        itoa_s(d > 0 ? d : -d, b);
    } else {
        *b = 0;
    }
}

static void secs_text(float t, char *b)          /* a time, short: "0.62s", "4.2s", "25s", "2m30" */
{
    int c;
    if (t < 1.0f) {
        c = (int)(t * 100.0f + 0.5f);
        *b++ = '0';
        *b++ = '.';
        *b++ = (char)('0' + c / 10 % 10);
        *b++ = (char)('0' + c % 10);
        copy_s(b, "s");
    } else if (t < 9.95f) {
        c = (int)(t * 10.0f + 0.5f);
        *b++ = (char)('0' + c / 10);
        *b++ = '.';
        *b++ = (char)('0' + c % 10);
        copy_s(b, "s");
    } else if (t < 59.5f) {
        itoa_s((int)(t + 0.5f), b);
        while (*b)
            b++;
        copy_s(b, "s");
    } else {
        c = (int)(t + 0.5f);
        itoa_s(c / 60, b);
        while (*b)
            b++;
        *b++ = 'm';
        *b++ = (char)('0' + c % 60 / 10);
        *b++ = (char)('0' + c % 10);
        *b = 0;
    }
}

void dr_param_text(int i, int v, char *b)
{
    static const char *const MODE[3] = {"LP", "BP", "HP"};
    static const char *const WAVE[4] = {"Sine", "Triangle", "Saw", "Square"};
    static const char *const WAVE_S[4] = {"Sin", "Tri", "Saw", "Sqr"};
    static const char *const TAP[4] = {"Off", "I", "II", "I+II"};
    int gp = i < P_XMOD ? i % GP_N : -1;
    if (i == P_XMOD)
        copy_s(b, v == DR_XMOD_OUT ? "Snarl" : v == DR_XMOD_FM ? "Pitch" : v ? "Cutoff" : "Off");
    else if (gp == GP_PARTNER || i == P_SYNC || i == P_PLINK || i == P_PULSE || i == P_LFO_LOCK ||
        i == P_PLOCK)
        copy_s(b, v ? "On" : "Off");
    else if (gp == GP_MODE)
        copy_s(b, MODE[(unsigned)v < 3u ? v : 0]);
    else if (gp == GP_INTERVAL)
        copy_s(b, INTERVAL_NAME[(unsigned)v < DR_NINTERVALS ? v : DR_IV_UNISON]);
    else if (gp == GP_WAVE) {                    /* on a wave: its name; between two: "Tri/Saw" */
        int x = v * 3, i = (x + 2) / 100, at = x - 100 * i <= 2;   /* within 2 of a wave: it, as the engine */
        if (at) {
            copy_s(b, WAVE[i]);
        } else {
            copy_s(b, WAVE_S[i]);
            b[3] = '/';
            copy_s(b + 4, WAVE_S[i + 1]);
        }
    } else if (gp == GP_OLFO || gp == GP_FLFO)
        copy_s(b, TAP[(unsigned)v < 4u ? v : 0]);
    else if (gp == GP_FLFO_DEST)
        copy_s(b, v ? "Reso" : "Cutoff");
    else if (gp == GP_OLFO_DEST)
        copy_s(b, v ? "X-Wave" : "Pitch");
    else if (i == P_LFO_W1)
        copy_s(b, DR_LFO_W1_NAME[(unsigned)v < DR_LFO_NSHAPES ? v : 0]);
    else if (i == P_LFO_W2)
        copy_s(b, DR_LFO_W2_NAME[(unsigned)v < DR_LFO_NSHAPES ? v : 0]);
    else if (i == P_LFO_RATE)
        rate_text(v, b);
    else if (i == P_PULSE_DEPTH) {               /* how far it dips: "-15dB", or "silent" */
        if (v >= 100)
            copy_s(b, "silent");
        else {
            int db = (int)((float)v * (30.0f / 99.0f) + 0.5f);
            if (db)
                *b++ = '-';
            itoa_s(db, b);
            while (*b)
                b++;
            copy_s(b, "dB");
        }
    } else if (i == P_PULSE_RATE) {                /* beats a second; slow, a beat's length ("33s") */
        float hz = pulse_hz(v);
        if (hz < 0.1f)
            secs_text(1.0f / hz, b);
        else
            hz_text(hz, b);
    }
    else if (i == P_REV_TYPE)
        copy_s(b, DR_REV_NAME[(unsigned)v < DR_NREV ? v : 0]);
    else if (i == P_DLY_TYPE)
        copy_s(b, DR_DLY_NAME[(unsigned)v < DR_NDLY ? v : DR_DLY_TAPE]);
    else if (i == P_DLY_ROUTE)
        copy_s(b, DR_ROUTE_NAME[(unsigned)v < DR_NROUTE ? v : 0]);
    else if (i == P_SUBW_0 || i == P_SUBW_1)
        copy_s(b, DR_SUBW_NAME[(unsigned)v < DR_NSUBW ? v : 0]);
    else if (i == P_TUNING)
        copy_s(b, DR_TUNING_NAME[(unsigned)v < DR_NTUNING ? v : 0]);
    else if (i == P_CLK_SYNC)
        copy_s(b, v ? "On" : "Off");
    else if (i == P_TUNE) {                   /* A in Hz: "432Hz", "432.5Hz" */
        const int d = 4400 + v;
        itoa_s(d / 10, b);
        while (*b)
            b++;
        if (d % 10) {
            *b++ = '.';
            *b++ = (char)('0' + d % 10);
        }
        copy_s(b, "Hz");
    }
    else if (i == P_PULSE_DIV)
        copy_s(b, DR_DIV_NAME[(unsigned)v < DR_NDIV ? v : 2]);
    else if (i == P_LFO_DIV)
        copy_s(b, DR_LDIV_NAME[(unsigned)v < DR_NLDIV ? v : 3]);
    else if (i == P_DLY_NOTE)
        copy_s(b, DR_NOTE_NAME[(unsigned)v < DR_NNOTE ? v : 5]);
    else if (i == P_SCENE_BARS)
        copy_s(b, DR_BARS_NAME[(unsigned)v < DR_NBARS ? v : 2]);
    else if (i == P_PULSE_SHAPE) {               /* "gate", "pulse 50", "even", "ramp 40": named on both sides */
        if (v == 0) {
            copy_s(b, "even");
        } else if (v <= -100) {
            copy_s(b, "gate");
        } else if (v < 0) {
            copy_s(b, "pulse ");
            itoa_s(-v, b + 6);
        } else {
            copy_s(b, "ramp ");
            itoa_s(v, b + 5);
        }
    }
    else if (i == P_WSCALE)
        copy_s(b, DR_WSCALE_NAME[(unsigned)v < DR_NWSCALE ? v : 0]);
    else if (i == P_WANDER) {                    /* "off", "25-30s", "4.1-7.6s", "0.5-2.0s" */
        float lo, hi;
        char *q = b;
        if (v <= 0) {
            copy_s(b, "off");
        } else {
            int k;
            wander_gap(v, &lo, &hi);
            for (k = 0; k < 2; k++) {
                float x = k ? hi : lo;
                if (x >= 9.95f) {
                    itoa_s((int)(x + 0.5f), q);
                } else {
                    int t = (int)(x * 10.0f + 0.5f);
                    itoa_s(t / 10, q);
                    while (*q)
                        q++;
                    *q++ = '.';
                    *q++ = (char)('0' + t % 10);
                    *q = 0;
                }
                while (*q)
                    q++;
                *q++ = k ? 's' : '-';
                *q = 0;
            }
        }
    }
    else if (i == P_SHIFT_MOVE) {                /* "5th up", "+340c", "none" */
        int c = v - DR_SHIFT_MID;
        if (c == 700 || c == -700 || c == 500 || c == -500)
            copy_s(b, c == 700 ? "5th up" : c == -700 ? "5th down" : c == 500 ? "4th up" : "4th down");
        else if (c == 0)
            copy_s(b, "none");
        else {
            char *q = b;
            if (c > 0)
                *q++ = '+';
            itoa_s(c, q);
            while (*q)
                q++;
            *q++ = 'c';
            *q = 0;
        }
    }
    else if (i == P_REV_LEN) {                   /* "4.4s", "Inf" */
        if (v >= 100)
            copy_s(b, "Inf");
        else
            secs_text(0.3f * fm_exp2f((float)v * 0.070607f), b);
    }
    else if (i == P_DLY_LFO || i == P_LFO_RMOD_SRC)
        copy_s(b, TAP[(unsigned)v < 4u ? v : 1]);
    else if (i == P_MORPH || i == P_FADE || i == P_SCENE_T) {
        /* a time: "0.62s", "4.2s", "25s", "2m30" */
        float t = dr_time_s(i, v);
        secs_text(t, b);
    } else if (i == P_GLIDE) {                     /* "Off", "2.5s": 0 .. 10 s, quadratic (QA 2026-10-20: it showed "50") */
        if (!v)
            copy_s(b, "Off");
        else
            secs_text(10.0f * (float)v * (float)v * 0.0001f, b);
    } else if (i == P_DLY_TIME) {                  /* "177ms"; "1.46s" from a second */
        float ms = 70.0f * fm_exp2f((float)v * 0.026845f);
        if (ms >= 999.5f) {
            int c = (int)(ms * 0.1f + 0.5f);       /* hundredths of a second */
            itoa_s(c / 100, b);
            while (*b)
                b++;
            *b++ = '.';
            *b++ = (char)('0' + c / 10 % 10);
            *b++ = (char)('0' + c % 10);
            copy_s(b, "s");
        } else {
            itoa_s((int)(ms + 0.5f), b);
            while (*b)
                b++;
            copy_s(b, "ms");
        }
    }
    else if (gp == GP_NCOLOR) {                  /* "White", "Pink 40", "Pink" */
        if (v <= 0 || v >= 100)
            copy_s(b, v > 0 ? "Pink" : "White");
        else {
            copy_s(b, "Pink ");
            itoa_s(v, b + 5);
        }
    }
    else if (gp == GP_PITCH)
        pitch_text(v, b);
    else if (gp == GP_CUTOFF || gp == GP_RESO) {   /* 0..1000 shown 0..100: "70", "70.5" between */
        itoa_s(v / 10, b);
        if (v % 10) {
            while (*b)
                b++;
            *b++ = '.';
            *b++ = (char)('0' + v % 10);
            *b = 0;
        }
    }
    else if (i == P_MOVE) {                      /* "100%": the depths as set */
        itoa_s(v * 2, b);
        while (*b)
            b++;
        copy_s(b, "%");
    } else if (i == P_II_OUT)
        copy_s(b, v == 3 ? "Both out" : v == 2 ? "I out" : v ? "Out" : "In");
    else if (i == P_SPEAKER) {                   /* "off", "120Hz" */
        if (!v)
            copy_s(b, "Off");
        else {
            itoa_s((int)(40.0f * fm_exp2f((float)v * 0.0258496f) + 0.5f), b);
            while (*b)
                b++;
            copy_s(b, "Hz");
        }
    } else if (gp == GP_SUBOCT)
        copy_s(b, v == 6 ? "-4 oct" : v == 5 ? "-3 oct" : v == 4 ? "1/5" : v == 3 ? "1/3" : v == 2 ? "-2 oct" : "-1 oct");
    else {
        if ((i == P_PLINK_OFF || i == P_DLY_LFO_DEPTH || i == P_LFO_RMOD || gp == GP_OLFO_DEPTH || gp == GP_FLFO_DEPTH) && v > 0)
            *b++ = '+';
        itoa_s(v, b);
    }
}

/* -------------------------------------------------------------- commands --- */
enum { C_SET, C_PLAY, C_VOICE, C_PANIC, C_SLIDE, C_LFO_RESET, C_PSLIDE, C_SEED, C_DUCK, C_GEST };
/* a load (boot, project_apply) posts every value at once, before the render drains any: the queue holds
 * them all (at 64 the last values of a saved sound were dropped at start-up, the macros among them) */
#define QN 128u
typedef char qn_holds_a_load[DR_NPARAMS + 16 <= QN ? 1 : -1];
static uint32_t q[QN];
static volatile uint32_t q_w, q_r;
static void post(uint32_t c, int a, int b)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;                                   /* full: dropped (never: a load is DR_NPARAMS) */
    q[w % QN] = c << 24 | ((uint32_t)a & 0xFFu) << 16 | ((uint32_t)b & 0xFFFFu);
    BARRIER();
    q_w = w + 1;
}
void dr_set(int p, int v) { post(C_SET, p, v); }
void dr_play(int on) { post(C_PLAY, on, 0); }
void dr_voice(int g, int on) { post(C_VOICE, g, on); }
void dr_panic(void) { post(C_PANIC, 0, 0); }
void dr_duck(int on) { post(C_DUCK, on, 0); }
void dr_slide(int g, int ms) { post(C_SLIDE, g, ms); }
void dr_pslide(int param, int ms) { post(C_PSLIDE, param, ms); }
void dr_lfo_reset(void) { post(C_LFO_RESET, 0, 0); }
void dr_seed(uint32_t s) { post(C_SEED, (int)(s >> 16 & 0xFFu), (int)(s & 0xFFFFu)); }
void dr_gesture(int g, int amt) { post(C_GEST, g, amt); }

/* ----------------------------------------------------------------- state --- */
volatile float dr_level[DR_NGROUPS];
volatile float dr_fade;
volatile float dr_duck_level = 1.0f;
volatile float dr_tail;
static float duck = 1.0f, duck_to = 1.0f;     /* the dip around a save while playing (ui.c) */
volatile float dr_pitch[DR_NGROUPS];

/* NOISE's colour: white noise through six gentle shelves (a one-pole low-pass at 10, 40, 160, 640, 2560, 10240
 * Hz, two octaves apart, each with g of its input added back: y = lp + g (x - lp)). The highs of each shelf
 * are g as loud, so g = 1 is white, 1/2 drops 6 dB every two octaves (pink, -3 dB/oct); g = 2^(-2 c), c 0
 * white .. 1/2 pink, the slopes between only as the Colour switch crossfades. Then a low cut (~25 Hz) */
#define NZ_N 6
static const float NZ_A[NZ_N] = {0.0014237f, 0.0056828f, 0.0225383f, 0.0871508f, 0.3056216f, 0.7675204f};
#define NZ_HP 0.0035554f
/* ... and its loudness made up: halfway (in dB) between the same total power and the same power over
 * 100 Hz .. 10 kHz; log2 of the gain, a cubic in c fitted within 0.2 dB (1.2x at white). NZ_RMS: white's
 * level, near the oscillators' own (a saw and its partner: ~0.5); NZ_TOP: the noise at 100, as loud as they are (Evan,
 * 2026-10-18: noise set on its own, as a Moog's mixer; it was 6 dB under) */
static float nz_gain(float c) { return fm_exp2f(0.2726416f + c * (9.6516356f + c * (-2.2130652f - 1.0846857f * c))); }
#define NZ_RMS 0.45f
#define NZ_TOP 1.0f

typedef struct {
    float pitch;                                  /* MIDI note, fractional: where the glide is now */
    float ps1, ps2;                               /* the pitch smoothed twice: what sounds */
    float lofs, lslide;                           /* PITCH LINK: the offset (semitones), its slide a tick */
    uint8_t linked;                               /* PITCH LINK was on last tick */
    uint16_t sc_n, sc_dec, sc_cnt, sc_wait;       /* the scope: points taken, a point every sc_dec samples */
    uint16_t sc_arm;                              /* ... samples armed without a cycle starting */
    uint32_t sc_t0;                               /* ... the sample it started on (sc_clk) */
    float sc_ci;                                  /* ... the cycles a sample then */
    float slide;                                  /* semitones a control tick toward the pitch; 0: there */
    float st;                                     /* where the slide is: the Glide follows it */
    uint16_t slide_ms;                            /* dr_slide: the next pitch change's slide, ms */
    float ph, pph, sph;                           /* main, partner, sub phases (turns) */
    float drift[2], dtarget[2];                   /* main and partner drift, -1..1 */
    uint32_t dcount;
    float cut;                                    /* smoothed cutoff, octaves over 1 Hz */
    float ic1, ic2;                               /* filter states */
    float bc;                                     /* the bass makeup's low-pass state */
    float du, dF;                                 /* the DRIVE's antialiasing: its last input, sat_F of it */
    float gf, gtarget;                            /* the voice's fade, 0..1 */
    float lv;                                     /* the level (with the Crossfade), smoothed */
    float peak;
    float pl, pr;                                 /* the pan's gains where the last block ended (Stereo) */
    /* MORPH: where each switch's change has got to, moving to its setting over the Morph time */
    float wm[3];                                  /* the filter mode's weights: LP, BP, HP */
    float pg;                                     /* the partner's gain, 0..1 */
    float ig;                                     /* a gesture's interval for the partner coming or going: its gain */
    float ov;                                     /* ... or a gesture's own (semitones; CHORD), when ovon */
    int ovon;
    float ivs;                                    /* the partner's interval on the Tuning, gliding (dr_partner_semi) */
    int ivi;                                      /* ... the interval it's for (-1: none yet) */
    float sg, sdiv;                               /* the sub's swap: its gain (down, swap, up), the octave */
    uint8_t sw;                                   /* ... and its wave (P_SUBW_*, swapped as the octave is) */
    float sy;                                     /* hard sync's swap (drone II): its gain */
    uint8_t sync;                                 /* hard sync as it sounds now */
    float sync_next;                              /* the sync BLEP's second half, for the next sample */
    float jit;                                    /* CHARACTER: the pitch's fast jitter, -1..1 */
    float rlp, dcx, dcy;                          /* ... its rounding low-pass; its DC blocker's states */
    uint32_t ns;                                  /* ... its noise */
    uint32_t nn;                                  /* NOISE: its generator */
    float nz[NZ_N], nh;                           /* ... its colour's shelves, its low cut */
    float nc;                                     /* ... its colour, 0 white .. 1 pink (the switch, crossfading) */
    float fbh;                                    /* FEEDBACK: its low cut's state */
    float fbe, fsh, fsc, fse;                     /* SING: the loop's level (~3 Hz); SNARL: its lows' state (~10 Hz), the
                                                   * limited bend's mean, its level (~3 Hz) */
    float wn[4], wt[4];                           /* DRIFT's wanders (cutoff, X-WAVE, level, detune): now, aim */
    uint32_t wc[4];                               /* ... ticks until each picks a new aim */
    float xm;                                     /* cross-mod (drone II), 0..1 */
    float xs;                                     /* SNARL (v0.3, Cross-mod's fourth): how far in, 0..1 */
    float xp, xh, xc;                             /* CROSS-FM (v0.3): how far in, 0..1; the other's sound's low part;
                                                   * the limited sound's mean */
    uint8_t rt[2], rp[2];                         /* the LFO routes (osc, filter): the tap now, the one before */
    float rw[2];                                  /* ... from the one before to now, 0..1 */
    float dw[2];                                  /* their destinations: osc 0 pitch .. 1 X-WAVE, filter 0
                                                   * cutoff .. 1 resonance */
} grp_t;

static int16_t par[DR_NPARAMS];                   /* the values heard: the sound's own plus the macros' offsets */
static int16_t base[DR_NPARAMS];                  /* the sound's own values (dr_set) */
static float gam[DR_NGEST];                       /* the gestures' amounts now, 0..1 (dr_gesture) */
static uint8_t mac_dirty;                         /* an amount or a depth changed: the offsets again */
static int16_t moff[DR_NPARAMS];                  /* the macros' offset on each value now (MOFF_ON: a switch on) */
#define MOFF_ON 0x7FFF
/* TICK: the sub's own rate, a drone's (p): TICKD sinks it from its octave under 20 Hz into sparse beats (2^-7 of the
 * drone), TICKU speeds the beats up (to 2^-3): the press is the beat rate */
static float tick_d[DR_NGROUPS], tick_u[DR_NGROUPS];
/* CHORD: a partner's interval from Wander's scale (P_WSCALE), built on drone I, just: d 1 drone I's partner, d 2 drone
 * II's; spread open, one of CHORD_V's voicings each press (never the same twice): each partner's chord tone (0 the
 * root, 1 the third, 2 the top note, 3 the fifth) and how many octaves up */
static float chord_t[DR_NGROUPS];
static int chord_on[DR_NGROUPS];
static const uint8_t CHORD_V[5][4] = {{1, 1, 2, 1}, {1, 1, 2, 2}, {0, 1, 1, 2}, {2, 1, 1, 2}, {3, 1, 1, 2}};
static int chord_vi;
/* A scale's note on the Tuning (Evan, 2026-10-30): its interval over drone I, semitones; cur as on 12-tone, j its pure
 * size (cents). 12-tone and Quarter: as cur (Wander's semitones, Chord's pure intervals). The 12-note Tunings (Just,
 * LMY, Harrison): the same keys, the Tuning's note cur keys over drone I's. The others (19-, 31-, 53-tone, Shruti,
 * Partch, Harmonics): the Tuning's note nearest j over drone I's. From drone I's nearest note on a fixed grid; on a
 * movable one (Just, Shruti) from drone I itself, so pure over it */
static int grid_near_a(int t, int v) { return dr_grid_at(t, dr_grid_k(t, v, 900), 900); }   /* on A */
/* ... and over drone g (Partner and Shift follow the Tuning too, Evan 2026-11-07): from drone g's nearest note, on
 * the grid as Wander's (a movable Tuning still built on drone I) */
float dr_tun_over(int g, float cur, int j)
{
    int t = tun_ok(base[P_TUNING]), p1 = base[DR_GP(0, GP_PITCH)], root, k, g1;
    if (t == DR_TUN_12 || t == DR_TUN_24)
        return cur;
    root = DR_TUN_MOVES(t) ? p1 : 900;
    k = dr_grid_k(t, base[DR_GP(g & 1, GP_PITCH)], root);
    g1 = dr_grid_at(t, k, root);
    if (dr_tun_n(t) == 12)
        return (float)(dr_grid_at(t, k + (int)fm_floorf(cur + 0.5f), root) - g1) * 0.01f;
    return (float)(dr_grid_at(t, dr_grid_k(t, g1 + j, root), root) - g1) * 0.01f;
}
float dr_tun_iv(float cur, int j) { return dr_tun_over(0, cur, j); }
/* the partner's interval as heard, semitones: on 12-tone and Quarter as picked (tempered or just); on another Tuning
 * the Tuning's interval over drone g, from the interval's pure size (both "+5th" and "+5th just" its fifth) */
static const int16_t IV_J[DR_NINTERVALS] = {-1200, -702, -702, -498, -498, 0, 316, 316, 386, 386, 498, 498, 702, 702,
                                            1200, 1902, 1902};
float dr_partner_semi(int g, int iv)
{
    const int t = tun_ok(base[P_TUNING]);
    iv = (unsigned)iv < DR_NINTERVALS ? iv : DR_IV_UNISON;
    if (t == DR_TUN_12 || t == DR_TUN_24)
        return DR_IV_SEMI[iv];
    return dr_tun_over(g, fm_floorf(DR_IV_SEMI[iv] + 0.5f), IV_J[iv]);
}
DR_FAR_FN(float, dr_partner_semi, (int, int))
/* SHIFT along the Tuning (Evan, 2026-11-07): its knob steps from note to note of the Tuning over drone I (SEL: a cent,
 * off the notes); on a note the move is so many of the Tuning's notes, and each drone moves that many from its own
 * nearest note; off a note, its cents (an older sound's +700c on 19-tone stays 700c) */
static int shift_root(int t, int p1) { return DR_TUN_MOVES(t) ? p1 : 900; }
int dr_shift_on(int t, int p1, int ct)               /* 1: ct (cents) is a note of the Tuning over drone I (p1) */
{
    const int r = shift_root(t, p1), g1 = dr_grid_at(t, dr_grid_k(t, p1, r), r);
    return dr_grid_at(t, dr_grid_k(t, g1 + ct, r), r) == g1 + ct;
}
int dr_shift_step(int t, int p1, int ct, int n)      /* n notes on from ct (to the next note first), within +-700 */
{
    const int r = shift_root(t, p1), g1 = dr_grid_at(t, dr_grid_k(t, p1, r), r);
    int k = dr_grid_k(t, g1 + ct, r), at = dr_grid_at(t, k, r) - g1, v;
    if (n > 0 && at > ct)                            /* off a note: its nearest in the turn's way counts as one */
        n--;
    else if (n < 0 && at < ct)
        n++;
    v = dr_grid_at(t, k + n, r) - g1;
    while (v > 700)
        v = dr_grid_at(t, --k + n, r) - g1;
    while (v < -700)
        v = dr_grid_at(t, ++k + n, r) - g1;
    return v;
}
float dr_shift_semi(int g)
{
    const int t = tun_ok(base[P_TUNING]), p1 = base[DR_GP(0, GP_PITCH)], ct = base[P_SHIFT_MOVE] - DR_SHIFT_MID;
    int r, k1, kt, kg;
    if (t == DR_TUN_12 || t == DR_TUN_24 || !dr_shift_on(t, p1, ct))
        return (float)ct * 0.01f;
    r = shift_root(t, p1);
    k1 = dr_grid_k(t, p1, r);
    kt = dr_grid_k(t, dr_grid_at(t, k1, r) + ct, r);
    kg = dr_grid_k(t, base[DR_GP(g & 1, GP_PITCH)], r);
    return (float)(dr_grid_at(t, kg + kt - k1, r) - dr_grid_at(t, kg, r)) * 0.01f;
}
static const float CHORD_T[DR_NWSCALE][2] = {
    {3.15641f, 10.17596f}, {3.86314f, 8.84359f}, {7.01955f, 12.0f}, {4.0f, 8.0f}, {3.15641f, 8.84359f}, {3.86314f, 9.68826f}};
static float mac_p[DR_NGROUPS];                   /* each drone's pitch move, semitones: Shift's, Wander's, Age's */
static float mac_gp[DR_NGROUPS];                  /* ... the gestures' share (Shift's): the pitch card shows it */
/* FEEDBACK: the finished sound (mono, after the effects), kept for a fixed 256 samples (5.8 ms) and fed
 * back into the drones' filters: a loop of the same length whatever the block size */
#define FB_N 512u
#define DRV_REF 0.6f                              /* DRIVE's make-up: the size it keeps as loud */
#define FB_MAX 0.7f                               /* the loop's gain at 100: strong, not singing (0.9 sang up on a still
                                                   * sound, +5 dB: QA 2026-10-20) */
#define FB_CEIL 0.45f                             /* the most it adds to an oscillator's +-1 (what keeps it bounded) */
static float fb_ring[FB_N];
static uint32_t fb_w;
/* the loop's length, in key (QA 2026-10-20: a fixed 256 samples rang at ~172 Hz, an F over a D chord, and took
 * over): a whole number of drone I's periods, or a period over a whole number, within 256 .. 510 samples (a block
 * is read back, never the one being made), gliding as drone I's pitch moves; and the loudness it adds, measured
 * (the energy fed in against the oscillators'), taken off the output: never louder than without it */
static float fb_d = 300.0f, fb_c = 1.0f, fb_ein, fb_eosc, fb_esn;   /* (fb_esn: Snarl's bend, its power) */
static float spk[2][2];                          /* the speaker low cut's states, left and right */
volatile uint8_t dr_phones;
#define SPK_CUT 61.0f                            /* the speaker's low cut, fixed (2026-10-20: ~120 Hz; P_SPEAKER unused) */
static float spk_x, spk_f;                       /* ... how far it's in (0..1), its knob (the corner) */
/* the HEADPHONE low cut: 12 dB at 12 Hz, under hearing, before the clip (Evan, 2026-11-15: Tick's sub swinging
 * under 1 Hz at low pitches had pinned each side against the clip in turn, the drones squeezed out, one side
 * silent, 51-74 % of the time held lightly; with it, ~1 %). The speaker has its own, above; crossfaded the same way */
#define HP_CUT_HZ 12.0f
static float hpc[2][2], hpc_x;
/* every parameter as it sounds: gliding to its setting. A knob's detent glides over the time until the
 * next is due (dr_pslide, as a pitch's); anything else (a scene's morph, MIDI, a load) over 25 ms. The
 * engine reads the continuous ones from here, so a slow turn is a sweep, never a staircase */
static float pfs[DR_NPARAMS], pfstep[DR_NPARAMS];        /* the value now; its step a sample */
static uint16_t pslide_ms[DR_NPARAMS];
static grp_t grp[DR_NGROUPS];
static float fade, fade_target;
volatile float dr_lfo[2];
volatile float dr_pulse = 1.0f;
volatile float dr_lfo_ph, dr_pulse_ph;
volatile float dr_cut[DR_NGROUPS];
volatile float dr_xw[DR_NGROUPS], dr_reso[DR_NGROUPS];
volatile float dr_fb, dr_snarl;
volatile float dr_scope[DR_NGROUPS][DR_SCOPE_N];
volatile uint32_t dr_scope_t0[DR_NGROUPS];
volatile float dr_scope_ci[DR_NGROUPS];
volatile uint16_t dr_scope_dec[DR_NGROUPS];
static uint32_t sc_clk;                          /* samples rendered: the scopes' common clock */
static float sc_buf[DR_NGROUPS][DR_SCOPE_N];
static float pulse_ph, pulse_g = 1.0f;           /* the pulse: phase over two beats (0..2), its gain */
static float pulse_a;                            /* the pulse coming in / going out (Morph), 0..1 */
static uint8_t dly_rt, dly_rp;                   /* the delay LFO's route: now, the one before (Morph) */
static float dly_rw;
static float lfo_rj[2], lfo_rk;                  /* a restart's jump, eased out over ~120 ms (QA 2026-10-20: locked to
                                                  * the Swell, the filter jumped an octave every bar) */
static int lfo_rnew;                             /* ... a restart to ease, at the next tick */
static float lfo_ph, lfo_s[2];                  /* the LFO's phase (cycles) and its taps, slewed */
static uint32_t lfo_q;                          /* ... its phase in 32 bits (a float lost ~2 % at the slowest rate) */
static int lfo_wc[2], lfo_wp[2];                 /* each tap's shape now, and the one before (Morph) */
static float lfo_wx[2];                          /* ... how far from that one to this, 0..1 */
static void lfo_restart(void);
static float lfo_rnd[3];                         /* the random shapes' levels (below) */
static uint32_t lfo_seed;                        /* ... their generator (dr_init seeds it) */
static float rnd_next(void);
static void morph_snap(int g);
static float morph_s(void);
static uint32_t rng;                             /* DRIFT's and CHARACTER's generator (dr_init seeds it) */

static float rnd(void)                            /* -1..1 */
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (float)(int32_t)rng * (1.0f / 2147483648.0f);
}

static float fade_rate(void)                      /* per sample: 0.05 s * 1200^(v/100) for a full fade */
{
    float s = dr_time_s(P_FADE, par[P_FADE]);
    return 1.0f / (s * DR_SR);
}

static void fx_clear(void);                      /* drone_fx.c, included below */
static void macro_reset(void);                   /* the macros' moves at rest (below) */

void dr_init(void)
{
    int i, g;
    for (i = 0; i < DR_NGEST; i++)
        gam[i] = 0.0f;
    mac_p[0] = mac_p[1] = mac_gp[0] = mac_gp[1] = 0.0f;
    macro_reset();
    for (i = 0; i < DR_NPARAMS; i++) {
        par[i] = base[i] = PARAMS[i].def;
        moff[i] = 0;
        pfs[i] = (float)par[i];
        pslide_ms[i] = 0;
    }
    for (g = 0; g < DR_NGROUPS; g++) {
        grp_t *v = &grp[g];
        *v = (grp_t){0};
        v->pitch = v->ps1 = v->ps2 = v->st = (float)par[DR_GP(g, GP_PITCH)] * 0.01f;
        v->cut = 4.32192809f + (float)par[DR_GP(g, GP_CUTOFF)] * 0.00981378f;
        v->gf = v->gtarget = g < 2 && DR_OUT(par[P_II_OUT], g) ? 0.0f : 1.0f;   /* a drone out on its own: out */
        v->lv = (float)(par[DR_GP(g, GP_LEVEL)] * par[DR_GP(g, GP_LEVEL)]) * 0.0001f * 0.4f;
        v->ph = 0.25f * (float)g;                  /* not all edges together */
        v->pph = 0.5f + 0.13f * (float)g;
        v->nn = 0x9E3779B9u * (uint32_t)(g + 1);  /* each drone its own noise: two noises, not one twice */
        dr_pitch[g] = v->ps2;
        dr_level[g] = 0.0f;
        morph_snap(g);
    }
    fade = fade_target = 0.0f;
    dr_fade = 0.0f;
    rng = 0x12345678u;                           /* the random ones from the same start: every init sounds the same */
    lfo_seed = 0x9E3779B9u;
    lfo_ph = lfo_s[0] = lfo_s[1] = 0.0f;
    lfo_rk = 0.0f;
    lfo_rnew = 0;
    lfo_q = 0u;
    lfo_wc[0] = lfo_wp[0] = par[P_LFO_W1];
    lfo_wc[1] = lfo_wp[1] = par[P_LFO_W2];
    lfo_wx[0] = lfo_wx[1] = 1.0f;
    lfo_rnd[0] = rnd_next();
    lfo_rnd[1] = rnd_next();
    lfo_rnd[2] = rnd_next();
    pulse_ph = 0.0f;
    pulse_g = 1.0f;
    pulse_a = 0.0f;
    dly_rt = dly_rp = (uint8_t)par[P_DLY_LFO];
    dly_rw = 1.0f;
    dr_pulse = 1.0f;
    spk_x = dr_phones ? 0.0f : 1.0f;
    spk_f = SPK_CUT;
    hpc_x = dr_phones ? 1.0f : 0.0f;
    hpc[0][0] = hpc[0][1] = hpc[1][0] = hpc[1][1] = 0.0f;
    fx_clear();
    q_r = q_w;
}

/* THE MACROS (v0.2, option D; two performer reviews; staged 2026-10-18 after the macro research). The four gesture
 * keys (D5-G5) play Weight, Shift, Clean, Wash, always the same (Evan, 2026-10-18: you learn where they are), by the
 * UI's swell (gam) times the gesture's full depth (DR_GEST_DEPTH); while one is in, the LFOs ease off what it moves.
 * Two slow states on knobs: AGE (Life) and WANDER (Moves).
 * The sound's own values (base) stay as they are; what's heard is base + the sum, clamped to each value's range.
 * Each move comes in over its own part of the macro's way in (in0 .. in1 of the amount, eased) and goes over
 * its own part of the way out (out0 .. out1), so a macro's parts arrive in order and leave in their own order
 * (Strymon Deco's auto-flange: "the controls are smoothly returned"); a move's level glides (MOVE_S) so turning
 * back half way doesn't jump. Kinds: ADD an offset; SW a switch on while the move is in, OFF one off; ZERO the
 * value toward 0 (by d %, all the way when d is 0); TO the value up toward d (nothing if it's there already: a
 * sound with its own effects up isn't pushed past them); SHIFT a drone's pitch (Shift's move); BUMP an offset that
 * rises over in0 .. in1 and falls again over out0 .. out1, both ways (a dip while something else moves); PULSE an
 * offset breathing slowly (PULSE_HZ) while it's in; WOW a drone's pitch wavering slowly, d cents; TICKD / TICKU the
 * sub's own rate (p a drone); CHORD a
 * partner's own interval (p a drone, d 1 or 2), from Wander's scale (v0.3); DOWN as TO, down toward d (the Swell's
 * Shape toward its gate). */
enum { MK_ADD, MK_SW, MK_OFF, MK_ZERO, MK_TO, MK_SHIFT, MK_BUMP, MK_PULSE, MK_WOW, MK_TICKD, MK_TICKU, MK_CHORD, MK_DOWN };
typedef struct {
    uint8_t p, kind;                             /* the value (or, for a pitch move, the drone) and the kind */
    int16_t d;                                   /* the amount at full (MK_ADD; MK_TO: the value aimed at) */
    uint8_t in0, in1, out0, out1;                /* where it comes in and goes, in % of the macro's amount */
} move_t;
#define MV_N 16
#define PULSE_HZ 0.15f                           /* a PULSE move's breath: about 7 s */
#define MOVE_S 0.15f                             /* a move's level: the whole way in this long, at most (a
                                                  * turn back half way doesn't jump; Gesture time can be quick) */
#define MOFF_OFF 0x7FFE                          /* moff[]: a switch held off */
const char *const DR_GEST_NAME[DR_NGTYPE] = {"Weight", "Shift", "Clean", "Wash", "Tick", "Chord", "Snarl", "Stutter"};
const uint8_t DR_GEST_DEPTH[DR_NGTYPE] = {70, 100, 100, 70, 100, 100, 100, 100};   /* a tap, or a hold all the way in */
const uint8_t DR_GEST_TIME[DR_NGTYPE] = {30, 40, 35, 35, 55, 55, 55, 55};   /* 0..100 = 1 .. 30 s: 2.8 s, 3.9 s, 3.3 s, 3.3 s;
                                                                          * the pressure keys' 6.5 s (as tried on the FM-1) */
static const move_t MACRO_MV[DR_NGTYPE][MV_N] = {
    /* WEIGHT: a full, warm bass (Evan, 2026-10-18): the sub comes up through most of the swell (a little: the main
     * oscillator's fundamental is the low-mids a small speaker plays), the drones come up, the resonance and the
     * noise give way and the cutoffs ease down a little (warm, not dark), then drive and the vintage character
     * round it off. 2026-10-20 (review 3: it was 2.4 dB quieter on the speaker, the sub crossfading the low-mids
     * away): now +1 dB there, +2 on headphones. Let go: the brightness comes back first, the sub lingers */
    {{DR_GP(0, GP_MIX), MK_ADD, 15, 0, 70, 0, 60}, {DR_GP(1, GP_MIX), MK_ADD, 15, 0, 70, 0, 60},
     {DR_GP(0, GP_CUTOFF), MK_ADD, -10, 0, 50, 40, 100}, {DR_GP(1, GP_CUTOFF), MK_ADD, -10, 0, 50, 40, 100},
     {DR_GP(0, GP_RESO), MK_ADD, -40, 0, 50, 40, 100}, {DR_GP(1, GP_RESO), MK_ADD, -40, 0, 50, 40, 100},
     {DR_GP(0, GP_DRIVE), MK_ADD, 25, 20, 100, 20, 100}, {DR_GP(1, GP_DRIVE), MK_ADD, 25, 20, 100, 20, 100},
     {P_CHAR, MK_ADD, 17, 30, 100, 30, 100}, {DR_GP(0, GP_NOISE), MK_ADD, -20, 0, 50, 50, 100},
     {DR_GP(1, GP_NOISE), MK_ADD, -20, 0, 50, 50, 100}, {DR_GP(0, GP_LEVEL), MK_ADD, 40, 20, 100, 0, 60},
     {DR_GP(1, GP_LEVEL), MK_ADD, 40, 20, 100, 0, 60}},
    /* SHIFT: a new chord, a voice at a time (the Lyra-8 manual's chord changes): drone I glides there first, drone
     * II follows; let go, drone I comes back first. Its move: P_SHIFT_MOVE */
    {{0, MK_SHIFT, 0, 0, 60, 40, 100}, {1, MK_SHIFT, 0, 40, 100, 0, 60}},
    /* CLEAN (Evan's idea, 2026-10-18): back to a clean sound, the notes kept (no unison, no sync), each kind of dirt
     * leaving at its own pace from the start: the grit first (drive, character, feedback, noise, cross-mod, hard
     * sync), then the movement (Movement, Drift, the Swell's depth), then the effects (the delay's repeats, both
     * mixes, the reverb's length). Let go: the effects come back first, the grit last */
    {{DR_GP(0, GP_DRIVE), MK_ZERO, 0, 0, 25, 0, 25}, {DR_GP(1, GP_DRIVE), MK_ZERO, 0, 0, 25, 0, 25},
     {P_CHAR, MK_ZERO, 0, 0, 25, 0, 25}, {P_FEEDBACK, MK_ZERO, 0, 0, 25, 0, 25},
     {DR_GP(0, GP_NOISE), MK_ZERO, 0, 0, 25, 0, 25}, {DR_GP(1, GP_NOISE), MK_ZERO, 0, 0, 25, 0, 25},
     {P_XMOD, MK_OFF, 0, 10, 30, 10, 30}, {P_SYNC, MK_OFF, 0, 10, 30, 10, 30},
     {P_MOVE, MK_ZERO, 80, 10, 55, 10, 55}, {P_DRIFT, MK_ZERO, 70, 10, 55, 10, 55},
     {P_PULSE_DEPTH, MK_ZERO, 0, 10, 55, 10, 55}, {P_DLY_REPEATS, MK_ZERO, 70, 25, 70, 25, 70},
     {P_DLY_MIX, MK_ZERO, 0, 30, 100, 30, 100}, {P_REV_MIX, MK_ZERO, 75, 30, 100, 30, 100},
     {P_REV_LEN, MK_ZERO, 50, 30, 100, 30, 100}},
    /* WASH: the delay first, then the reverb opens (Length toward Inf), darker as it grows, the dry dips
     * last; each effect up toward its aim, not past what the sound has already (Evan, 2026-10-18: with the effects
     * up it went wild; the repeats stop at 80, under the delay's own howl at 87); at the top the dry breathes slowly in and out of the cloud (55 .. 70 %: its depth is 70); the filter LFOs ease to a third as it opens
     * (Evan, 2026-10-18: with Movement up, their sweeps piled up in the tails as surges). Let go: the dry comes
     * back first, the filter LFOs with it, the tails ring on (the reverb's own length) and go last */
    {{P_DLY_MIX, MK_TO, 70, 0, 40, 0, 30}, {P_DLY_REPEATS, MK_TO, 80, 0, 40, 0, 30},
     {P_REV_MIX, MK_TO, 80, 30, 80, 0, 30}, {P_REV_LEN, MK_TO, 100, 50, 100, 20, 60},
     {P_REV_DAMP, MK_ADD, 15, 50, 100, 20, 60}, {DR_GP(0, GP_LEVEL), MK_ADD, -15, 60, 100, 60, 100},
     {DR_GP(1, GP_LEVEL), MK_ADD, -15, 60, 100, 60, 100}, {DR_GP(0, GP_LEVEL), MK_PULSE, -14, 55, 70, 55, 70},
     {DR_GP(1, GP_LEVEL), MK_PULSE, -14, 55, 70, 55, 70},
     {DR_GP(0, GP_FLFO_DEPTH), MK_ZERO, 67, 20, 70, 0, 40}, {DR_GP(1, GP_FLFO_DEPTH), MK_ZERO, 67, 20, 70, 0, 40}},
    /* the pressure keys G4..C5 (v0.3), each the partner of the top key four to its right: Tick (the low end, as Weight),
     * Chord (pitch, as Shift), Snarl (dirt in; Clean takes it out), Stutter (time, as Wash). Pressed harder, further in */
    /* TICK (G4): the sub comes up and sinks from its hum through 20 Hz into sparse beats; then the press speeds the beats
     * up, louder, into a flutter with drive. Let go: the beats slow, then climb back up into the hum */
    {{DR_GP(0, GP_MIX), MK_ADD, 25, 0, 30, 0, 35}, {DR_GP(1, GP_MIX), MK_ADD, 25, 0, 30, 0, 35},
     {0, MK_TICKD, 1, 0, 40, 0, 40}, {1, MK_TICKD, 1, 0, 40, 0, 40},
     {0, MK_TICKU, 1, 40, 100, 40, 100}, {1, MK_TICKU, 1, 40, 100, 40, 100},
     {DR_GP(0, GP_MIX), MK_ADD, 25, 40, 100, 40, 100}, {DR_GP(1, GP_MIX), MK_ADD, 25, 40, 100, 40, 100},
     {DR_GP(0, GP_DRIVE), MK_ADD, 30, 60, 100, 60, 100}, {DR_GP(1, GP_DRIVE), MK_ADD, 30, 60, 100, 60, 100}},
    /* CHORD (A4): a chord builds a voice at a time: the partners on (a sound with them off has them come in), the
     * beating goes (their detune out), then drone I's partner moves to a chord tone of Wander's scale, then drone II's,
     * just, spread open (CHORD_V, one voicing a press); the filters ease open a little. Let go: drone II's voice goes
     * home first, then drone I's, the beating returns last */
    {{DR_GP(0, GP_PARTNER), MK_SW, 0, 0, 1, 0, 1}, {DR_GP(1, GP_PARTNER), MK_SW, 0, 0, 1, 0, 1},
     {DR_GP(0, GP_DETUNE), MK_ZERO, 0, 0, 30, 0, 30}, {DR_GP(1, GP_DETUNE), MK_ZERO, 0, 0, 30, 0, 30},
     {0, MK_CHORD, 1, 30, 31, 40, 41}, {1, MK_CHORD, 2, 60, 61, 70, 71},
     {DR_GP(0, GP_CUTOFF), MK_ADD, 40, 30, 100, 30, 100}, {DR_GP(1, GP_CUTOFF), MK_ADD, 40, 50, 100, 50, 100}},
    /* SNARL (B4): a slight unease first (drone II detuned), then Snarl comes in (its mode crossfades in at the sound's
     * own amount) and grows, the drive bites, Feedback sings at the top. Let go: Feedback and drive first, the
     * snarl after, the unease last */
    {{DR_GP(1, GP_DETUNE), MK_ADD, 20, 0, 40, 0, 50}, {P_XMOD, MK_SW, 0, 15, 16, 10, 11},
     {P_XMOD_AMT, MK_TO, 85, 20, 80, 15, 75}, {DR_GP(0, GP_DRIVE), MK_ADD, 30, 50, 100, 40, 100},
     {DR_GP(1, GP_DRIVE), MK_ADD, 30, 50, 100, 40, 100}, {P_FEEDBACK, MK_TO, 45, 70, 100, 60, 100}},
    /* STUTTER (C5): the Swell switches on and chops quickly from the first touch, gently; pressing on, the chop
     * deepens, then hardens into a gate, and at the top the rate's two moves add up to its fastest (16 a second, as
     * heard and kept on the FM-1, 2026-10-28). Let go: it softens first, then slows, then lets go */
    {{P_PULSE, MK_SW, 0, 0, 1, 0, 1}, {P_PULSE_RATE, MK_TO, 80, 0, 35, 0, 35}, {P_PULSE_DEPTH, MK_TO, 95, 10, 70, 10, 70},
     {P_PULSE_SHAPE, MK_DOWN, -85, 50, 100, 50, 100}, {P_PULSE_RATE, MK_TO, 90, 70, 100, 70, 100}},
};
/* AGE (Life), a state: as the old tape wears, darker, a little hiss, the pitch wavering slowly (wow) and a little
 * quickly (flutter); it wears in (and out) over AGE_S. Not Character (Evan, 2026-10-20: the two knobs overlapped) */
static float age_mk = 1.0f;                      /* that makeup now, as a gain, by Age as heard */
static const move_t AGE_MV[MV_N] = {
    {DR_GP(0, GP_CUTOFF), MK_ADD, AGE_CUT, 0, 100, 0, 100}, {DR_GP(1, GP_CUTOFF), MK_ADD, AGE_CUT, 0, 100, 0, 100},
    {DR_GP(0, GP_NOISE), MK_ADD, AGE_NZ, 0, 100, 0, 100}, {DR_GP(1, GP_NOISE), MK_ADD, AGE_NZ, 0, 100, 0, 100},
    {0, MK_WOW, AGE_WOW, 0, 100, 0, 100}, {1, MK_WOW, AGE_WOW, 0, 100, 0, 100},
};
#define AGE_S 20.0f                              /* Age wears in from none to full this long */
static float mv_lvl[DR_NGEST][MV_N];             /* each key's moves' levels now, 0..1 (gliding) */
static float mac_last[DR_NGEST];                 /* each key's amount last time ... */
static uint8_t mac_out;                          /* ... and which are on their way out (bit m: 8 gestures) */
static uint8_t mv_moving;                        /* a move's level still gliding: again next block */
static float mac_ph;                             /* the PULSE moves' phase, cycles */
static float age_l, age_ph;                      /* Age as heard now (gliding), 0..1; its wow's phase */
static float wand_st, wand_to;                   /* WANDER: drone II's move now and aimed at, semitones */
static uint32_t wand_n;                          /* samples to its next step */
static int wand_v;                               /* the Wander setting the countdown was drawn for */
static uint32_t mac_rng = 0x6D2B79F5u;           /* the macros' own random steps */
static float mac_rand(void)                      /* 0..1 */
{
    mac_rng ^= mac_rng << 13;
    mac_rng ^= mac_rng >> 17;
    mac_rng ^= mac_rng << 5;
    return (float)(mac_rng >> 8) * (1.0f / 16777216.0f);
}
static void macro_reset(void)
{
    int m, k;
    for (m = 0; m < DR_NGEST; m++) {
        mac_last[m] = 0.0f;
        for (k = 0; k < MV_N; k++)
            mv_lvl[m][k] = 0.0f;
    }
    mac_out = 0;
    mv_moving = 0;
    mac_ph = age_l = age_ph = 0.0f;
    age_mk = 1.0f;
    wand_st = wand_to = 0.0f;
    wand_n = 0;
    wand_v = 0;
}
static float mac_amt(int m)                      /* a key's amount now, 0..1 */
{
    return gam[m] * (float)DR_GEST_DEPTH[m] * 0.01f;
}
static float mv_ease(float a, int lo, int hi)    /* the amount a through the window lo .. hi %, eased */
{
    float x = lo >= hi ? (a > 0.0f ? 1.0f : 0.0f) : fm_clampf((a * 100.0f - (float)lo) / (float)(hi - lo), 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}
static int heard(int i)                          /* the sound's own value with the macros' offset, in range */
{
    int v = moff[i] == MOFF_ON ? PARAMS[i].hi : moff[i] == MOFF_OFF ? PARAMS[i].lo : base[i] + moff[i];
    if (i < P_XMOD && i % GP_N == GP_LEVEL && moff[i] && moff[i] != MOFF_ON && moff[i] != MOFF_OFF) {
        /* a level's moves are in the knob's units before its taper (v^2 / 100): the same dips and lifts in dB as
         * Evan approved (Weight +40, Wash -15 and its breath -14), at any level from 10; under 10 a lift fades out
         * with the level, so a drone turned to 0 stays silent, as under every other gesture (Evan, 2026-11-02:
         * Weight brought it back at about -28 dB) */
        const float m = moff[i] > 0 ? (float)moff[i] * fm_minf(1.0f, (float)base[i] * 0.1f) : (float)moff[i];
        float l = fm_clampf((float)(base[i] * base[i]) * 0.01f + m, 0.0f, 100.0f);
        v = (int)(10.0f * fm_sqrtf(l) + 0.5f);
    }
    return v < PARAMS[i].lo ? PARAMS[i].lo : v > PARAMS[i].hi ? PARAMS[i].hi : v;
}
/* WANDER, n samples on: a slow random melody for drone II (Evan, 2026-10-18): it jumps from note to note in no
 * order, the notes of a scale on drone I (P_WSCALE: minor or major pentatonic, fifths and octaves, whole tone,
 * Dorian, or just: harmonics 1-8 folded into an octave; in any octave) up to an octave from drone II's own pitch,
 * only now and then home. The knob is how often, at random times within a range (Evan, 2026-10-18): 25-30 s apart
 * at 1 .. 0.5-2 s at 100 (wander_gap). It doesn't glide by itself: GLO's Glide does that, as for a pitch knob */
#define WS_MAX 7
static const float WSCALE[DR_NWSCALE][WS_MAX] = {   /* semitones over drone I; -1 ends a shorter one */
    {0.0f, 3.0f, 5.0f, 7.0f, 10.0f, -1.0f}, {0.0f, 2.0f, 4.0f, 7.0f, 9.0f, -1.0f}, {0.0f, 7.0f, -1.0f},
    {0.0f, 2.0f, 4.0f, 6.0f, 8.0f, 10.0f, -1.0f}, {0.0f, 2.0f, 3.0f, 5.0f, 7.0f, 9.0f, 10.0f},
    {0.0f, 3.8631f, 7.0196f, 9.6883f, -1.0f},   /* harmonics 1-8: 1 (2, 4, 8), 5, 3 (6), 7 */
};
static const int16_t WSCALE_J[DR_NWSCALE][WS_MAX] = {   /* ... their pure sizes, cents (dr_tun_iv): 6/5 4/3 3/2 9/5; 9/8
                                                  * 5/4 3/2 5/3; 3/2; whole tones; 9/8 6/5 4/3 3/2 5/3 9/5; 5/4 3/2 7/4 */
    {0, 316, 498, 702, 1018}, {0, 204, 386, 702, 884}, {0, 702}, {0, 200, 400, 600, 800, 1000},
    {0, 204, 316, 498, 702, 884, 1018}, {0, 386, 702, 969},
};
static int wander_degs(float *deg)             /* the scale's notes over drone I, on the Tuning, semitones (up to 8) */
{
    const float *sc = WSCALE[(unsigned)base[P_WSCALE] < DR_NWSCALE ? base[P_WSCALE] : 0];
    int nd = 0, t = tun_ok(base[P_TUNING]), i, j;
    if (t == DR_TUN_HARM) {                      /* the harmonics: all of them, whatever the scale */
        int g1 = grid_near_a(t, base[DR_GP(0, GP_PITCH)]);
        for (i = 0; i < 8; i++)
            deg[nd++] = (float)(((900 + TUN_HARM[i] - g1) % 1200 + 1200) % 1200) * 0.01f;
    } else {
        const int16_t *sj = WSCALE_J[(unsigned)base[P_WSCALE] < DR_NWSCALE ? base[P_WSCALE] : 0];
        for (i = 0; i < WS_MAX && sc[i] >= 0.0f; i++) {
            float x = dr_tun_iv(sc[i], sj[i]);
            for (j = 0; j < nd && fm_fabsf(deg[j] - x) > 0.05f; j++)
                ;
            if (j == nd)                         /* (two notes the Tuning puts on one: once) */
                deg[nd++] = x;
        }
    }
    return nd;
}
int dr_wander_degs(float *deg) { return wander_degs(deg); }

static void wander_run(uint32_t n)
{
    float a = (float)base[P_WANDER] * 0.01f, gs;
    if (a > 0.0f && base[P_WANDER] > wand_v && wand_n > n) {   /* turned up: no waiting out the slower gap drawn
                                                                * before (QA 2026-10-20: 26 s from off to 100) */
        float lo, hi;
        wander_gap(base[P_WANDER], &lo, &hi);
        if ((float)wand_n > hi * DR_SR)
            wand_n = (uint32_t)((lo + (hi - lo) * mac_rand()) * DR_SR);
    }
    wand_v = base[P_WANDER];
    {   /* on a MIDI clock (v0.3): a note due waits for the next beat (the next bar, when they're slower than ~6 beats
         * apart), so the melody lands on the grid, its times still random */
        static float lb;
        float bn = dr_ext_spb > 0.0f ? ext_beats() : 0.0f, lo, hi, g;
        if (dr_ext_spb > 0.0f && a > 0.0f && wand_n <= n) {
            wander_gap(base[P_WANDER], &lo, &hi);
            g = 0.5f * (lo + hi) * DR_SR / dr_ext_spb >= 6.0f ? 4.0f : 1.0f;
            if (fm_floorf(bn / g) == fm_floorf(lb / g)) {
                lb = bn;
                wand_n = 0;                      /* due: held till the line */
                goto glide;
            }
        }
        lb = bn;
    }
    if (a <= 0.0f) {
        wand_to = 0.0f;
        wand_n = 0;
    } else if (wand_n <= n) {
        float reach = 12.0f, cand[40], lo, hi;
        int nc = 0, o, i;
        if (wand_to != 0.0f && mac_rand() < 0.12f) {
            wand_to = 0.0f;                      /* home, now and then */
        } else {
            float rel = grp[0].ps2 - grp[1].ps2, deg[8];   /* drone I's root, from drone II's own pitch; the
                                                            * scale's notes over it, on the Tuning */
            const int nd = wander_degs(deg);
            for (o = -2; o <= 2; o++)
                for (i = 0; i < nd; i++) {
                    float x = rel + deg[i] + 12.0f * (float)o;
                    if (fm_fabsf(x) <= reach && fm_fabsf(x - wand_to) > 0.5f && fm_fabsf(x) > 0.5f && nc < 40)
                        cand[nc++] = x;
                }
            if (nc)
                wand_to = cand[(int)(mac_rand() * (float)nc) % nc];
        }
        wander_gap(base[P_WANDER], &lo, &hi);
        wand_n = (uint32_t)((lo + (hi - lo) * mac_rand()) * DR_SR);
    } else {
        wand_n -= n;
    }
glide:
    gs = (float)par[P_GLIDE] * 0.01f;            /* GLO's Glide: 0..10 s, quadratic (0: a jump) */
    gs = 10.0f * gs * gs;
    if (gs <= 0.0f)
        wand_st = wand_to;
    else
        wand_st += (wand_to - wand_st) * (1.0f - fm_expf(-(float)n / (gs * 0.25f * DR_SR)));
    if (fm_fabsf(wand_st - wand_to) < 1e-4f)
        wand_st = wand_to;
}
static void mv_add(const move_t *v, float l, float *off, uint8_t *on, float *pm, int *flags)
{
    switch (v->kind) {
    case MK_SW: on[v->p] = 1; break;
    case MK_OFF: on[v->p] = 2; break;
    case MK_CHORD: {
        int sc = base[P_WSCALE] < 0 ? 0 : base[P_WSCALE] >= DR_NWSCALE ? DR_NWSCALE - 1 : base[P_WSCALE];
        const uint8_t *vc = CHORD_V[chord_vi] + (v->d == 1 ? 0 : 2);
        float ct = vc[0] == 3 ? 7.01955f : vc[0] ? CHORD_T[sc][vc[0] - 1] : 0.0f;   /* on the Tuning (dr_tun_iv) */
        float t = (vc[0] ? dr_tun_iv(ct, (int)(ct * 100.0f + 0.5f)) : 0.0f) + 12.0f * (float)vc[1];
        if (v->d != 1)                           /* drone II's: the note over drone I, from drone II */
            t -= (float)(base[DR_GP(1, GP_PITCH)] - base[DR_GP(0, GP_PITCH)]) * 0.01f;
        chord_t[v->p] = t;
        chord_on[v->p] = 1;
        break;
    }
    case MK_TICKD: tick_d[v->p] = fm_maxf(tick_d[v->p], l); break;
    case MK_TICKU: tick_u[v->p] = fm_maxf(tick_u[v->p], l); break;
    case MK_ZERO: off[v->p] -= (float)base[v->p] * l * (v->d ? (float)v->d * 0.01f : 1.0f); break;
    case MK_TO: off[v->p] += fm_maxf((float)(v->d - base[v->p]), 0.0f) * l; break;
    case MK_DOWN: off[v->p] += fm_minf((float)(v->d - base[v->p]), 0.0f) * l; break;
    case MK_SHIFT: pm[v->p] += dr_shift_semi(v->p) * l; break;
    case MK_PULSE: off[v->p] += (float)v->d * l * (0.5f - 0.5f * fm_cosf(6.2831853f * mac_ph)); *flags |= 2; break;
    case MK_WOW: {                               /* two slow sines a drone, apart (wow: +-AGE_WOW cents at full), and two
                                                  * quick ones (flutter, 5.7 and 8.9 Hz: +-4 cents) */
        float ph = 6.2831853f * age_ph, g = (float)v->p;
        pm[v->p] += (float)v->d * 0.01f * l * (0.6f * fm_sinf(ph * 0.21f + 2.0f * g) + 0.4f * fm_sinf(ph * (0.13f + 0.04f * g)));
        pm[v->p] += 0.04f * l * (0.7f * fm_sinf(ph * 5.7f + 1.3f * g) + 0.3f * fm_sinf(ph * 8.9f + 2.9f * g));
        *flags |= 8;
        break;
    }
    default: off[v->p] += (float)v->d * l; break;
    }
}
/* what a gesture's move calms: the LFO moving the same thing (Evan, 2026-10-18: with Movement up, the LFOs kept
 * moving what a gesture moves), as far as the move or the gesture is in. The oscillator LFO when it moves pitch and the move is a
 * pitch move; the filter LFO when it moves what the move does (cutoff or resonance); the delay LFO for the delay's time */
static void calm_note(const move_t *v, float l, float *co, float *cf, float *cd)
{
    int g = v->p / GP_N, gp = v->p % GP_N;
    if (v->kind == MK_SHIFT) {
        if (par[DR_GP(v->p, GP_OLFO_DEST)] == DR_TO_PITCH)
            co[v->p] = fm_maxf(co[v->p], l);
    } else if (v->p == P_DLY_TIME) {
        *cd = fm_maxf(*cd, l);
    } else if (v->p < P_XMOD && (gp == GP_CUTOFF || gp == GP_RESO) &&
               (par[DR_GP(g, GP_FLFO_DEST)] == DR_TO_RESO) == (gp == GP_RESO)) {
        cf[g] = fm_maxf(cf[g], l);
    }
}
static void macro_apply(uint32_t n)              /* the offsets again, n samples on; what changes glides 25 ms */
{
    float off[DR_NPARAMS], pm[DR_NGROUPS] = {0.0f, 0.0f}, step = (float)n / (MOVE_S * DR_SR);
    float co[DR_NGROUPS] = {0.0f, 0.0f}, cf[DR_NGROUPS] = {0.0f, 0.0f}, cd = 0.0f;   /* the LFOs calmed */
    uint8_t on[DR_NPARAMS];
    int m, k, i, flags = 0;
    for (i = 0; i < DR_NPARAMS; i++) {
        off[i] = 0.0f;
        on[i] = 0;
    }
    mv_moving = 0;
    tick_d[0] = tick_d[1] = tick_u[0] = tick_u[1] = 0.0f;
    chord_on[0] = chord_on[1] = 0;
    for (m = 0; m < DR_NGEST; m++) {
        const move_t *row = MACRO_MV[m];
        float a = mac_amt(m);
        if (m == DR_G_CHORD && a < 0.3f && mac_last[m] >= 0.3f)   /* CHORD: its voices gone, the next press a new voicing */
            chord_vi = (chord_vi + 1 + (int)(mac_rand() * 3.999f)) % 5;
        if (a > mac_last[m] + 1e-4f)
            mac_out &= (uint8_t)~(1u << m);
        else if (a < mac_last[m] - 1e-4f)
            mac_out |= (uint8_t)(1u << m);
        mac_last[m] = a;
        for (k = 0; k < MV_N && (row[k].d || row[k].kind != MK_ADD); k++) {
            const move_t *v = &row[k];
            float to = v->kind == MK_BUMP ? mv_ease(a, v->in0, v->in1) * (1.0f - mv_ease(a, v->out0, v->out1))
                     : mac_out >> m & 1u ? mv_ease(a, v->out0, v->out1) : mv_ease(a, v->in0, v->in1), *l = &mv_lvl[m][k];
            if (v->kind != MK_BUMP && (mac_out >> m & 1u))
                to = fm_minf(to, *l);            /* on the way out only down from where it got to (QA 2026-10-20: a
                                                  * gesture at its 70 % depth had some out windows fuller than the
                                                  * in windows reached: Wash's reverb went to Inf, Weight louder) */
            if (*l != to) {
                *l = *l < to ? fm_minf(*l + step, to) : fm_maxf(*l - step, to);
                mv_moving = 1;
            }
            if (*l > 0.0f) {
                mv_add(v, *l, off, on, pm, &flags);
                calm_note(v, fm_maxf(*l, gam[m]), co, cf, &cd);   /* all off with the gesture all the way in */
            }
        }
    }
    mac_gp[0] = pm[0];                           /* the gestures' pitch move (before Age's and Wander's) */
    mac_gp[1] = pm[1];
    for (i = 0; i < DR_NGROUPS; i++) {           /* the LFO depths toward 0, as far as calmed */
        off[DR_GP(i, GP_OLFO_DEPTH)] -= ((float)base[DR_GP(i, GP_OLFO_DEPTH)] + off[DR_GP(i, GP_OLFO_DEPTH)]) * co[i];
        off[DR_GP(i, GP_FLFO_DEPTH)] -= ((float)base[DR_GP(i, GP_FLFO_DEPTH)] + off[DR_GP(i, GP_FLFO_DEPTH)]) * cf[i];
    }
    off[P_DLY_LFO_DEPTH] -= ((float)base[P_DLY_LFO_DEPTH] + off[P_DLY_LFO_DEPTH]) * cd;
    {   /* AGE: toward its knob, slowly */
        float to = (float)base[P_AGE] * 0.01f, s = (float)n / (AGE_S * DR_SR);
        if (age_l != to) {
            age_l = age_l < to ? fm_minf(age_l + s, to) : fm_maxf(age_l - s, to);
            age_mk = fm_exp2f(AGE_MK * (1.0f / 6.0206f) * age_l);
            mv_moving = 1;
        }
        if (age_l > 0.0f)
            for (k = 0; k < MV_N && (AGE_MV[k].d || AGE_MV[k].kind != MK_ADD); k++)
                mv_add(&AGE_MV[k], age_l, off, on, pm, &flags);
    }
    if (base[P_WANDER] || wand_st != 0.0f) {     /* WANDER */
        wander_run(n);
        pm[1] += wand_st;
        mv_moving = 1;
    }
    mac_p[0] = pm[0];
    mac_p[1] = pm[1];
    if (flags & 2) {                             /* breathing: on, and on again next block */
        mac_ph += (float)n * (PULSE_HZ / DR_SR);
        mac_ph -= (float)(int)mac_ph;
        mv_moving = 1;
    } else {
        mac_ph = 0.0f;                           /* the next breath starts from the top (no dip at once) */
    }
    if (flags & 8) {                             /* Age's wow: on, and on again next block (it froze once Age had worn
                                                  * in, until 2026-10-20) */
        mv_moving = 1;
        age_ph += (float)n / DR_SR;
        if (age_ph > 1000.0f)
            age_ph -= 1000.0f;
    }
    for (i = 0; i < DR_NPARAMS; i++) {
        int v;
        moff[i] = on[i] == 1 ? (int16_t)MOFF_ON : on[i] == 2 ? (int16_t)MOFF_OFF
                                                 : (int16_t)(off[i] + (off[i] >= 0.0f ? 0.5f : -0.5f));
        v = heard(i);
        if (v != par[i]) {
            par[i] = (int16_t)v;
            pfstep[i] = fm_maxf(fm_fabsf((float)v - pfs[i]) * 1000.0f / (25.0f * DR_SR), 1e-7f);
        }
    }
    mac_dirty = 0;
}

float dr_wander(void) { return wand_st; }
float dr_wander_to(void) { return wand_to; }
int dr_heard(int p) { return p >= 0 && p < DR_NPARAMS ? par[p] : 0; }
float dr_mac_pitch(int g) { return g >= 0 && g < DR_NGROUPS ? mac_p[g] : 0.0f; }
float dr_gest_pitch(int g) { return g >= 0 && g < DR_NGROUPS ? mac_gp[g] : 0.0f; }
float dr_age_now(float *ph)
{
    *ph = age_ph;
    return age_l;
}

static void drain(void)
{
    while (q_r != q_w) {
        uint32_t c = q[q_r % QN];
        int a = (int)(c >> 16 & 0xFFu), b = (int)(int16_t)(c & 0xFFFFu);
        BARRIER();
        q_r++;
        switch (c >> 24) {
        case C_GEST:
            if (a < DR_NGEST) {
                gam[a] = fm_clampf((float)b * 0.001f, 0.0f, 1.0f);
                mac_dirty = 1;
            }
            break;
        case C_SET:
            if (a < DR_NPARAMS) {
                base[a] = (int16_t)(b < PARAMS[a].lo ? PARAMS[a].lo : b > PARAMS[a].hi ? PARAMS[a].hi : b);
                par[a] = (int16_t)heard(a);    /* with the macros' offset on it, if any */
                if (a == P_II_OUT) {           /* a drone out on its own: it fades as a voice does */
                    grp[0].gtarget = DR_OUT(base[a], 0) ? 0.0f : 1.0f;
                    grp[1].gtarget = DR_OUT(base[a], 1) ? 0.0f : 1.0f;
                }
                if (a >= P_WANDER && a <= P_GD_3)
                    mac_dirty = 1;             /* an amount or a depth: the offsets again */
                {   /* its glide: a step a sample, to arrive over the time asked for (or 25 ms) */
                    float ms = pslide_ms[a] ? (float)pslide_ms[a] : 25.0f;
                    pfstep[a] = fm_maxf(fm_fabsf((float)par[a] - pfs[a]) * 1000.0f / (ms * DR_SR), 1e-7f);
                    pslide_ms[a] = 0;
                }
                if (a < P_XMOD && a % GP_N == GP_PITCH) {   /* a pitch: a slide asked for (the Glide after it) */
                    grp_t *v = &grp[a / GP_N];
                    float d = fm_fabsf((float)par[a] * 0.01f - v->st);
                    v->slide = v->slide_ms ? fm_maxf(d * (float)CR * 1000.0f / ((float)v->slide_ms * DR_SR), 1e-5f) : 0.0f;
                    v->slide_ms = 0;
                } else if (a == P_PLINK_OFF) {  /* the link offset slides as drone II's pitch would */
                    grp_t *v = &grp[1];
                    float d = fm_fabsf((float)par[a] * 0.01f - v->lofs);
                    v->lslide = v->slide_ms ? fm_maxf(d * (float)CR * 1000.0f / ((float)v->slide_ms * DR_SR), 1e-5f) : 0.0f;
                    v->slide_ms = 0;
                }
            }
            break;
        case C_LFO_RESET:
            lfo_restart();
            break;
        case C_SEED: {                           /* the random ones from somewhere new (the UI, at the first PLAY) */
            uint32_t x = ((uint32_t)a << 16 | ((uint32_t)b & 0xFFFFu)) * 0x9E3779B1u;
            lfo_seed ^= x;
            rng ^= x * 0x85EBCA6Bu;
            if (!lfo_seed)
                lfo_seed = 1u;
            if (!rng)
                rng = 1u;
            break;
        }
        case C_PSLIDE:
            if (a < DR_NPARAMS)
                pslide_ms[a] = (uint16_t)(b < 1 ? 1 : b > 30000 ? 30000 : b);
            break;
        case C_SLIDE:
            if (a < DR_NGROUPS)
                grp[a].slide_ms = (uint16_t)(b < 1 ? 1 : b > 30000 ? 30000 : b);
            break;
        case C_PLAY:
            fade_target = a ? 1.0f : 0.0f;
            break;
        case C_DUCK:
            duck_to = a ? 0.0f : 1.0f;
            break;
        case C_VOICE:
            if (a < DR_NGROUPS)
                grp[a].gtarget = b ? 1.0f : 0.0f;
            break;
        case C_PANIC: {
            int g;
            fx_clear();                          /* silence now: the echoes and the tail too */
            fade = fade_target = 0.0f;
            for (g = 0; g < DR_NGROUPS; g++)
                grp[g].ic1 = grp[g].ic2 = grp[g].bc = 0.0f;
            for (g = 0; g < (int)FB_N; g++)      /* and nothing left to feed back */
                fb_ring[g] = 0.0f;
            fb_c = 1.0f;
            for (g = 0; g < 2; g++)              /* and the output filters' memories */
                spk[g][0] = spk[g][1] = hpc[g][0] = hpc[g][1] = 0.0f;
            break;
        }
        }
    }
}

/* ------------------------------------------------------------------ dsp --- */
/* polyBLEP: the correction for a step at phase 0 of a wave whose phase advances by dt a sample */
static inline float blep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

/* THE SUB's wave at phase ph (turns), dt its phase step (P_SUBW_*): each as loud (RMS) as the square, so a change of
 * wave changes its colour, not its level. The square and saw band-limited (their edges); the sine and triangle have
 * none to smooth */
static inline float sub_wave(int w, float ph, float dt)
{
    switch (w) {
    case DR_SUBW_SINE: return 1.41421356f * fm_sinf(6.28318531f * ph);
    case DR_SUBW_TRI: return 1.73205081f * (ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph);
    case DR_SUBW_SAW: return 1.73205081f * (2.0f * ph - 1.0f - blep(ph, dt));
    default: return (ph < 0.5f ? 1.0f : -1.0f) + blep(ph, dt) - blep(ph < 0.5f ? ph + 0.5f : ph - 0.5f, dt);
    }
}

/* one cycle of the wave at phase ph (turns), dt the phase step a sample; pw the square's high fraction.
 * The four are in phase (their fundamentals all rise from 0 at ph = 0: the triangle peaks at 1/4, the
 * saw falls), so an X-WAVE blend of two never cancels. The sine and triangle are lifted (OSC_*_K, below). */
/* the sine and the triangle lifted: alike in level they sounded ~7 dB under the saw on the FM-1's speaker (nothing
 * above 200 Hz, where it starts to work); met half way, ~4 dB (Evan, 2026-10-19), a little louder on headphones */
#define OSC_SINE_K 1.6f
#define OSC_TRI_K 1.7f
/* the square brought down 3 dB: it was the loudest wave on the speaker, 2.7-4.1 dB over the saw (Evan, 2026-10-30) */
#define OSC_SQ_K 0.70794578f
static inline DR_INL float osc(int wave, float ph, float dt, float pw)
{
    switch (wave) {
    case DR_SINE:
        return OSC_SINE_K * fm_sin_turns(ph);
    case DR_TRI: {
        float t = ph + 0.25f;
        return OSC_TRI_K * (1.0f - 4.0f * fm_fabsf((t >= 1.0f ? t - 1.0f : t) - 0.5f));
    }
    case DR_SAW:
        return 1.0f - 2.0f * ph + blep(ph, dt);
    default: {
        float t2 = ph + 1.0f - pw;
        if (t2 >= 1.0f)
            t2 -= 1.0f;
        return OSC_SQ_K * ((ph < pw ? 1.0f : -1.0f) + blep(ph, dt) - blep(t2, dt) + (1.0f - 2.0f * pw));   /* DC-free */
    }
    }
}

/* soft clip, tanh-like on [-3, 3] and +-1 beyond: x(27 + x^2) / (27 + 9x^2) */
static inline float sat(float x)
{
    float x2;
    x = fm_clampf(x, -3.0f, 3.0f);
    x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* sat()'s antiderivative, for the DRIVE's antialiasing (sat_aa): x^2/18 + 4/3 ln(9x^2 + 27) on [-3, 3]
 * (sat = x/9 + 24x/(9x^2 + 27)), and straight on past it (sat is +-1 there) */
#define SAT_F3 6.7428416f                         /* F(3) = 0.5 + 4/3 ln 108 */
static inline float sat_F(float x)
{
    float a = fm_fabsf(x);
    if (a >= 3.0f)
        return SAT_F3 + (a - 3.0f);
    return x * x * (1.0f / 18.0f) + 0.92419624f * fm_log2f(9.0f * x * x + 27.0f);   /* 4/3 ln2 */
}
/* the soft clip without most of its aliasing (first-order antiderivative antialiasing): the clip's mean over the step
 * from the last input to this one, (F(u) - F(u0)) / (u - u0), instead of its value at one point (Evan, 2026-10-30:
 * the drone drive at 100 folded tones back in at high notes); half a sample late, a touch softer at the very top */
static inline float sat_aa(float u, float *u0, float *f0)
{
    float f = sat_F(u), d = u - *u0, y;
    y = *f0 > 0.0f && fm_fabsf(d) > 1e-4f ? (f - *f0) / d : sat(0.5f * (u + *u0));   /* (F >= 4.39: 0 is unset) */
    *u0 = u;
    *f0 = f;
    return y;
}

static inline float advance(float ph, float dt)
{
    ph += dt;
    return ph >= 1.0f ? ph - 1.0f : ph;
}

static float note_inc(float n)                    /* phase increment (turns per sample) */
{
    float inc = 440.0f * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)) * (1.0f / DR_SR);
    return fm_minf(inc, 0.45f);
}

#define FRAMES_PER_DRIFT 8192u                    /* a new drift target every ~0.19 s */
static float mixl[BLK], mixr[BLK];

/* ------------------------------------------------------------ the LFO --- */
static float lfo_tick[2][BLK / CR + 1];          /* each tap's value at each control tick of the block */
/* what drone I leaves for drone II each block: its sound, its oscillator's restarts, its pitch a tick */
static float xbuf[BLK], ptick[BLK / CR + 1];
static float ybuf[BLK];                           /* drone II's sound, for drone I's cross-FM in the next block */
static float snq[BLK];                            /* SNARL's bend each sample, worked out for drone I, shared with II */
static uint8_t sbuf[BLK];
static float sfr[BLK];                           /* ... and where in its sample it restarted (0..1) */
static float pgain[BLK];                         /* the PULSE's gain each sample (pulse_run) */
/* a route's LFO at control tick t: Wave I, Wave II, or both summed (as two DS-2 waves switched to one
 * destination); none: 0 */
static inline float lfo_at(int tap, uint32_t t)
{
    return tap == DR_LFO_BOTH ? lfo_tick[0][t] + lfo_tick[1][t] : tap ? lfo_tick[tap - 1][t] : 0.0f;
}

static inline float fr(float x) { return x - fm_floorf(x); }
static inline float l_tri(float x) { return 1.0f - 4.0f * fm_fabsf(fr(x) - 0.5f); }   /* -1 at 0, 1 at 1/2 */
static inline float l_sq(float x) { return fr(x) < 0.5f ? 1.0f : -1.0f; }
static inline float l_saw(float x) { return 2.0f * fr(x) - 1.0f; }
/* 0..1 rising exponentially: (e^3f - 1) / (e^3 - 1) */
static inline float l_exp(float f) { return (fm_exp2f(4.32808512f * f) - 1.0f) * (1.0f / 19.0855369f); }
/* exponential rises and falls in turn, n of each a cycle */
static inline float l_expud(float x)
{
    float e = l_exp(fr(x));
    return ((int32_t)fm_floorf(x) & 1) ? 1.0f - 2.0f * e : 2.0f * e - 1.0f;
}

/* RANDOM: a new random level each cycle (the DS-2's "random" shapes are fixed patterns; this one isn't).
 * Wave I holds it through the cycle (sample and hold); Wave II glides to it over the cycle (a raised
 * cosine from the last one), so the two wander together. lfo_rnd: the cycle before last, the last, this */
static float rnd_next(void)                     /* xorshift32, -1..1 */
{
    lfo_seed ^= lfo_seed << 13;
    lfo_seed ^= lfo_seed >> 17;
    lfo_seed ^= lfo_seed << 5;
    return (float)(int32_t)lfo_seed * (1.0f / 2147483648.0f);
}
static float rnd_shape(int tap, int c, float f)  /* c 1: this cycle, 0: the last */
{
    if (tap == 0)
        return lfo_rnd[1 + c];
    return lfo_rnd[c] + (lfo_rnd[1 + c] - lfo_rnd[c]) * (0.5f - 0.5f * fm_cosf(FM_PI * f));
}

/* tap 0 (Wave I) or 1 (Wave II), shape 1..10, at phase ph (cycles). Every shape averages 0 over a cycle (the
 * lopsided ones shifted and scaled into -1..1), so picking one doesn't retune the drone: Roundtooth sat at
 * +0.61 of the depth (~1.4 semitones sharp at osc depth 30) */
/* AND / OR (the Lyra-8's Hyper LFO, v0.3): a square at the LFO's rate and a faster one inside it (3x on Wave I, 5x on
 * Wave II), multiplied (AND: up only while both are, a step between 2 levels) or added (OR, the slow one twice the
 * fast: 4 levels). Lyra-8 acid, on pitch. AND is up for 1/3 of a cycle (3x) or 3/10 (5x): the rest sits lower by as
 * much, so it averages 0 as the others do */
static float lfo_logic(int tap, int shape, float ph)
{
    const float k = tap ? 5.0f : 3.0f, lo = tap ? -0.3f / 0.7f : -0.5f;
    float a = l_sq(ph), b = l_sq(k * ph);
    if (shape == DR_LFO_AND)
        return a > 0.0f && b > 0.0f ? 1.0f : lo;
    return (2.0f * a + b) * (1.0f / 3.0f);
}

static float lfo_shape(int tap, int shape, float ph)
{
    if (shape == DR_LFO_RANDOM)
        return rnd_shape(tap, 1, fr(ph));
    if (shape >= DR_LFO_AND)
        return lfo_logic(tap, shape, ph);
    static const float PAT4[8] = {1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f};
    static const float PAT2[4] = {1.0f, -1.0f, 0.5f, -0.5f};
    float f = fr(ph);
    if (tap == 0)
        switch (shape) {
        case 1: return l_tri(ph);
        case 2: return l_sq(ph);
        case 3: return l_saw(ph);
        case 4:                                    /* a saw with a rounded rise: 1 - e^-5f, then down; centred */
            return (2.0f * (1.0f - fm_exp2f(-7.21347520f * f)) * (1.0f / 0.993262053f) - 1.613567f) * 0.619754f;
        case 5: return l_tri(2.0f * ph);
        case 6: return l_expud(4.0f * ph);         /* 2x: two rises and two falls a cycle */
        case 7: return PAT4[(int)(f * 8.0f) & 7];  /* a square at 4x, in a pattern */
        default: return 0.0f;
        }
    switch (shape) {
    case 1: return l_tri(ph + 0.25f);
    case 2: return l_sq(ph + 0.25f);
    case 3: return (2.0f * l_exp(f) - 0.561875f) * 0.695358f;   /* centred */
    case 4: {                                      /* a triangle bent logarithmically; centred */
        float t = 1.0f - 2.0f * fm_fabsf(f - 0.5f);
        return (0.5f * fm_log2f(1.0f + 15.0f * t) - 1.411986f) * 0.708250f;
    }
    case 5: return l_saw(2.0f * ph);
    case 6: return PAT2[(int)(f * 4.0f) & 3];      /* a square at 2x, in a pattern */
    case 7: return l_expud(8.0f * ph);             /* 4x */
    default: return 0.0f;
    }
}

float dr_lfo_wave(int tap, int shape, float ph)
{
    if (shape == DR_LFO_RANDOM)
        return rnd_shape(tap, ph >= 1.0f, fr(ph));
    return shape ? lfo_shape(tap, shape, ph) : 0.0f;
}

static float depth_amt(int d);
static float depth_amtf(float d) { float x = d * 0.01f; return x * fm_fabsf(x); }
/* the LFO's taps now, for its own rate: Wave I, II or both (summed, then clamped by the caller) */
static inline float lfo_at_now(int src)
{
    return src == DR_LFO_BOTH ? lfo_s[0] + lfo_s[1] : src == DR_LFO_II ? lfo_s[1] : lfo_s[0];
}

/* RATE MOD's scale: the mean over a cycle of the time each bit takes, 2^(-rm * wave), so the modded cycle
 * lasts as long as a plain one. pub: through dr_lfo_wave (the screen) rather than this cycle's shapes */
static float rmod_k(float rm, int src, const int *w, int pub)
{
    float k = 0.0f;
    int i;
    for (i = 0; i < 32; i++) {
        float ph = ((float)i + 0.5f) * (1.0f / 32.0f);
        float a = pub ? dr_lfo_wave(0, w[0], ph) : lfo_shape(0, w[0], ph);
        float b = pub ? dr_lfo_wave(1, w[1], ph) : lfo_shape(1, w[1], ph);
        float v = src == DR_LFO_BOTH ? fm_clampf(a + b, -1.0f, 1.0f) : src == DR_LFO_II ? b : a;
        k += fm_exp2f(-rm * v);
    }
    return k * (1.0f / 32.0f);
}
float dr_lfo_rmod_k(float rm, int src, int w1, int w2)
{
    const int w[2] = {w1, w2};
    return rm != 0.0f ? rmod_k(rm, src, w, 1) : 1.0f;
}

/* the LFO for the n frames ahead, one value a control tick; a light slew (~0.7 ms) so its squares
 * don't click */
static void lfo_run(uint32_t n)
{
    /* on a MIDI clock (v0.3): a cycle a division of the beat (P_LFO_DIV), steered toward the beat as the Swell is, more
     * gently (a two-hundredth a block): the rate mod's warp within a cycle is kept */
    const int ext = dr_ext_spb > 0.0f;
    const float lbeats = LDIV_BEATS[(unsigned)par[P_LFO_DIV] < DR_NLDIV ? par[P_LFO_DIV] : 3];
    const float step = ext ? (float)CR / (lbeats * dr_ext_spb) : lfo_hz_f(pfs[P_LFO_RATE]) * (float)CR * (1.0f / DR_SR);
    const int w[2] = {par[P_LFO_W1], par[P_LFO_W2]};
    const float mstep = (float)CR / (morph_s() * DR_SR);
    /* RATE MOD: the LFO's own wave (I, II or both) speeds it up and slows it down within its cycle, as the
     * DS-2 patched from a wave out to its rate in: up to +-3 octaves at depth +-100; scaled so a cycle still
     * takes as long as the rate says (it lingered where slowed: x1.9 at 100, x2.9 with both waves) */
    const float rm = pfs[P_LFO_RMOD] * 0.03f;           /* linear: a quarter turn is already heard */
    const int rsrc = par[P_LFO_RMOD_SRC];
    const float rk = rm != 0.0f ? rmod_k(rm, rsrc, lfo_wc, 0) : 1.0f;
    uint32_t t, nt = (n + CR - 1u) / CR;
    int i;
    for (i = 0; i < 2; i++)                      /* a new shape (PRESETS / ALGORITHM): from the old one, over the */
        if (w[i] != lfo_wc[i]) {                 /* Morph, as a switch (it jumped: 2.4 semitones at depth 30) */
            lfo_wp[i] = lfo_wx[i] < 0.5f ? lfo_wp[i] : lfo_wc[i];
            lfo_wc[i] = w[i];
            lfo_wx[i] = 0.0f;
        }
    for (t = 0; t < nt; t++) {
        float inc;
        uint32_t q0 = lfo_q;
        for (i = 0; i < 2; i++) {
            float a = lfo_shape(i, lfo_wc[i], lfo_ph);
            if (lfo_wx[i] < 1.0f) {
                float b = lfo_shape(i, lfo_wp[i], lfo_ph);
                a = b + (a - b) * lfo_wx[i];
                lfo_wx[i] = fm_minf(lfo_wx[i] + mstep, 1.0f);
            }
            if (lfo_rnew)                        /* a restart: what it jumped by, eased out */
                lfo_rj[i] = lfo_s[i] - a;
            if (lfo_rk > 0.0f)
                a += lfo_rj[i] * lfo_rk * lfo_rk * (3.0f - 2.0f * lfo_rk);
            lfo_s[i] += (a - lfo_s[i]) * 0.5f;
            lfo_tick[i][t] = lfo_s[i];
        }
        if (lfo_rnew)
            lfo_rnew = 0;
        else if (lfo_rk > 0.0f) {
            lfo_rk = fm_maxf(lfo_rk - (float)CR / (0.12f * DR_SR), 0.0f);
        }
        inc = rm != 0.0f ? step * rk * fm_exp2f(rm * fm_clampf(lfo_at_now(rsrc), -1.0f, 1.0f)) : step;
        lfo_q += (uint32_t)(int32_t)(fm_minf(inc, 0.99f) * 2147483648.0f) << 1;   /* turns, in 32 bits */
        if (lfo_q < q0) {
            lfo_rnd[0] = lfo_rnd[1];             /* a new cycle: a new random level */
            lfo_rnd[1] = lfo_rnd[2];
            lfo_rnd[2] = rnd_next();
        }
        lfo_ph = (float)(lfo_q >> 8) * (1.0f / 16777216.0f);
    }
    if (ext) {
        float x = ext_beats() / lbeats, e;
        x -= fm_floorf(x);
        e = x - lfo_ph;
        e -= e >= 0.5f ? 1.0f : e < -0.5f ? -1.0f : 0.0f;
        lfo_q += (uint32_t)(int32_t)(0.005f * e * 4294967296.0f);
        lfo_ph = (float)(lfo_q >> 8) * (1.0f / 16777216.0f);
    }
    dr_lfo[0] = lfo_s[0];
    dr_lfo[1] = lfo_s[1];
    dr_lfo_ph = lfo_ph;
}

/* the LFO back to the start of its cycle (RESET, or a new bar when locked to the pulse): a new cycle, so the
 * random shapes draw a new level (they froze, locked to a bar shorter than the LFO's cycle) */
static void lfo_restart(void)
{
    lfo_rnew = 1;
    lfo_rk = 1.0f;
    lfo_q = 0u;
    lfo_ph = 0.0f;
    lfo_rnd[0] = lfo_rnd[1];
    lfo_rnd[1] = lfo_rnd[2];
    lfo_rnd[2] = rnd_next();
}

/* an LFO depth, -100..100, to an amount: squared (fine near zero), the sign kept */
static float depth_amt(int d)
{
    float x = (float)d * 0.01f;
    return x * fm_fabsf(x);
}

#include "niagara_fx.c"                  /* (Fantasma 2.0: Niagara Drift's effects left out, the channel's own play instead; niagara_fx.c) */

/* the X-WAVE: wave wi blended with the next by wf (0..1) */
static inline float xosc(int wi, float wf, float ph, float dt, float pw)
{
    float o = osc(wi, ph, dt, pw);
    if (wf > 0.0f)
        o += (osc(wi + 1, ph, dt, pw) - o) * wf;
    return o;
}

/* ------------------------------------------------------------- morph --- */
/* MORPH: a switch (a button or key) doesn't jump: the sound moves to its new setting over the Morph time.
 * The filter mode crossfades, the partner and cross-mod fade, the pulse deepens or eases out, an LFO route
 * crossfades from its old wave to its new one, a destination from one to the other; the sub's octave and
 * hard sync, which can't blend, dip out and come back changed. Stopped, all of it is where it's set. */
static float morph_s(void) { return dr_time_s(P_MORPH, par[P_MORPH]); }    /* 30 s at 100 */
static inline float toward(float x, float t, float step)
{
    return x < t ? fm_minf(x + step, t) : fm_maxf(x - step, t);
}
/* the sub's frequency, a fraction of the main oscillator's: an octave or two down, or a subharmonic (a
 * third: an octave and a fifth down; a fifth: two octaves and a major third), as the Subharmonicon's */
static float sub_div(int s)
{
    return s == 6 ? 0.0625f : s == 5 ? 0.125f : s == 4 ? 0.2f : s == 3 ? 1.0f / 3.0f : s == 2 ? 0.25f : 0.5f;
}
static void morph_snap(int g)                    /* all of a drone's switches where they're set */
{
    grp_t *v = &grp[g];
    const int16_t *p = &par[DR_GP(g, 0)];
    int i;
    for (i = 0; i < 3; i++)
        v->wm[i] = p[GP_MODE] == i ? 1.0f : 0.0f;
    v->pg = p[GP_PARTNER] ? 1.0f : 0.0f;
    v->ovon = 0;
    v->ov = 0.0f;
    v->ig = 1.0f;
    v->ivi = -1;
    v->nc = (float)p[GP_NCOLOR] * 0.01f;
    v->sg = 1.0f;
    v->sdiv = sub_div(p[GP_SUBOCT]);
    v->sw = (uint8_t)par[P_SUBW_0 + g];
    v->sy = 1.0f;
    v->sync = (uint8_t)(g == 1 && par[P_SYNC]);
    v->xm = g == 1 && par[P_XMOD] == DR_XMOD_CUT ? 1.0f : 0.0f;
    v->xp = par[P_XMOD] == DR_XMOD_FM ? 1.0f : 0.0f;
    v->xs = par[P_XMOD] == DR_XMOD_OUT ? 1.0f : 0.0f;
    v->rt[0] = v->rp[0] = (uint8_t)p[GP_OLFO];
    v->rt[1] = v->rp[1] = (uint8_t)p[GP_FLFO];
    v->rw[0] = v->rw[1] = 1.0f;
    v->dw[0] = p[GP_OLFO_DEST] == DR_TO_XWAVE ? 1.0f : 0.0f;
    v->dw[1] = p[GP_FLFO_DEST] == DR_TO_RESO ? 1.0f : 0.0f;
}
static void morph_tick(int g, float step)        /* one control tick of the switches' moves */
{
    grp_t *v = &grp[g];
    const int16_t *p = &par[DR_GP(g, 0)];
    const float sdiv = sub_div(p[GP_SUBOCT]);
    const uint8_t sync = (uint8_t)(g == 1 && par[P_SYNC]);
    int i;
    for (i = 0; i < 3; i++)
        v->wm[i] = toward(v->wm[i], p[GP_MODE] == i ? 1.0f : 0.0f, step);
    v->pg = toward(v->pg, p[GP_PARTNER] ? 1.0f : 0.0f, step);
    v->nc = pfs[DR_GP(g, GP_NCOLOR)] * 0.01f;    /* the colour, a blend white .. pink, gliding as a knob does */
    if (v->ovon != chord_on[g] || (chord_on[g] && fm_fabsf(v->ov - chord_t[g]) > 1e-3f)) {
        v->ig -= 2.0f * step;                    /* a gesture's interval for the partner (CHORD, v0.3): down, swap, up */
        if (v->ig <= 0.0f) {
            v->ig = 0.0f;
            v->ovon = chord_on[g];
            v->ov = chord_t[g];
        }
    } else {
        v->ig = fm_minf(v->ig + 2.0f * step, 1.0f);   /* (its own interval switches as it always has) */
    }
    if (v->sdiv != sdiv || v->sw != (uint8_t)par[P_SUBW_0 + g]) {   /* the sub: down, then swap the octave or
                                                                  * the wave, then up */
        v->sg -= 2.0f * step;
        if (v->sg <= 0.0f) {
            v->sg = 0.0f;
            v->sdiv = sdiv;
            v->sw = (uint8_t)par[P_SUBW_0 + g];
        }
    } else {
        v->sg = fm_minf(v->sg + 2.0f * step, 1.0f);
    }
    if (v->sync != sync) {                       /* hard sync: the same, on drone II's sound */
        v->sy -= 2.0f * step;
        if (v->sy <= 0.0f) {
            v->sy = 0.0f;
            v->sync = sync;
        }
    } else {
        v->sy = fm_minf(v->sy + 2.0f * step, 1.0f);
    }
    v->xm = toward(v->xm, g == 1 && par[P_XMOD] == DR_XMOD_CUT ? 1.0f : 0.0f, step);
    v->xp = toward(v->xp, par[P_XMOD] == DR_XMOD_FM ? 1.0f : 0.0f, step);
    v->xs = toward(v->xs, par[P_XMOD] == DR_XMOD_OUT ? 1.0f : 0.0f, step);
    for (i = 0; i < 2; i++) {                    /* a route to a new wave: from the old one */
        uint8_t t = (uint8_t)p[i ? GP_FLFO : GP_OLFO];
        if (t != v->rt[i]) {
            v->rp[i] = v->rt[i];
            v->rt[i] = t;
            v->rw[i] = 0.0f;
        }
        v->rw[i] = fm_minf(v->rw[i] + step, 1.0f);
    }
    v->dw[0] = toward(v->dw[0], p[GP_OLFO_DEST] == DR_TO_XWAVE ? 1.0f : 0.0f, step);
    v->dw[1] = toward(v->dw[1], p[GP_FLFO_DEST] == DR_TO_RESO ? 1.0f : 0.0f, step);
}
/* a route's widest swing in its waves: twice for BOTH (I + II summed), during a switch's crossfade too */
static inline float route_n(const grp_t *v, int i)
{
    return v->rt[i] == DR_LFO_BOTH || v->rp[i] == DR_LFO_BOTH ? 2.0f : 1.0f;
}
/* An LFO on a setting with hard ends (X-WAVE, the resonance: 0..1): b the knob, o the LFO's move now, s its widest.
 * Centered on the knob while the swing fits; where it would run past an end it slides away from it (the knob near
 * an end: the sweep reaches the end and turns there), and a swing wider than the range fills it: never held at an
 * end for part of the cycle (Evan, 2026-11-08: a sweep should be heard all the way round). A divide a control tick */
static inline float lfo_fit(float b, float o, float s)
{
    if (s <= 0.5f)                               /* it fits the range: centered, or slid, unscaled (no divide) */
        return fm_clampf(fm_clampf(b, s, 1.0f - s) + o, 0.0f, 1.0f);
    return fm_clampf(0.5f + o * (0.5f / s), 0.0f, 1.0f);   /* wider than the range: fills it */
}
static inline float route_at(const grp_t *v, int i, uint32_t t)
{
    float b = lfo_at(v->rt[i], t);
    return v->rw[i] >= 1.0f ? b : lfo_at(v->rp[i], t) + (b - lfo_at(v->rp[i], t)) * v->rw[i];
}

static DR_RAM_CODE void render_group(int g, uint32_t n)
{
    grp_t *v = &grp[g];
    const int16_t *p = &par[DR_GP(g, 0)];
    const float *pv = &pfs[DR_GP(g, 0)];          /* the continuous settings, as they glide */
    const float pw = 0.5f - 0.45f * pv[GP_WIDTH] * 0.01f;
    /* OSC MIX: an equal-power crossfade from the oscillators (main + partner) to the sub */
    const float mix = pv[GP_MIX] * 0.01f;
    const float omix = fm_sqrtf(1.0f - mix), sub = 0.8f * fm_sqrtf(mix);
    /* NOISE: a level added to the oscillators, as a synth's mixer (they stay as they are): 100 is as loud as
     * they are, 50 -6 dB, 25 -12 (a straight level: heard all along the knob); its colour's shelves
     * (g) and the loudness made up for them. White from the generator is +-1, RMS 1/sqrt 3 */
    const float nmix = pv[GP_NOISE] * 0.01f;
    const float ncol = 0.5f * v->nc, nsh = fm_exp2f(-2.0f * ncol);
    const float namp = nmix * NZ_TOP * NZ_RMS * 1.7320508f * nz_gain(ncol) * (1.0f / 2147483648.0f);
    /* the LFO: how much each route moves (its waves and destinations crossfade on a switch: morph_tick) */
    /* ... each scaled by the MOVEMENT (OCT- / OCT+: every route's depth at once, their balance kept) */
    const float mv = pfs[P_MOVE] * 0.02f;
    const float oamt = depth_amtf(pv[GP_OLFO_DEPTH]) * mv, famt = depth_amtf(pv[GP_FLFO_DEPTH]) * mv;
    /* to the resonance the depth is linear (squared, as for the cutoff's 5 octaves, 50 swung the peak ~5 dB and
     * 25 was barely heard): 100 sweeps its whole range, 50 half (format 44; 50 was the whole range, past it held at
     * the ends); X-WAVE's (squared, as pitch's) the same: xwamt */
    const float ramt = pv[GP_FLFO_DEPTH] * 0.005f * mv, xwamt = 0.5f * oamt;
    const float mstep = (float)CR / (morph_s() * DR_SR);    /* a switch's move a control tick */
    /* CHARACTER (clean .. vintage): softer edges (a one-pole, its corner 8 kHz .. 500 Hz in even steps, so the
     * middle of the knob is heard: in a straight line it was ~6.4 kHz at 50, now ~3 kHz), a little x^2 (a bent
     * saw, even harmonics; its DC taken out), pitch jitter (about +-12 cents, ~20 Hz), the drones
     * apart (II up to 6 cents sharp), a whisper of noise (-44 dB) */
    const float chr = pfs[P_CHAR] * 0.01f, asym = CHR_ASYM * chr, nz = 0.006f * chr;
    const float rk = 1.0f - fm_exp2f(-9.06472f * 8000.0f * fm_exp2f(-CHR_K * chr) * (1.0f / DR_SR));
    const float mm = g == 1 ? 0.06f * chr : 0.0f;
    /* FEEDBACK: its gain into the filter, in a straight line, bounded by its ceiling (it ran away from ~50 at
     * 1.6 with none). Evan, on the FM-1: 0.85 / 0.35 too mild, 1.05 / 0.6 (singing) too strong: 0.9 / 0.45, a
     * strong texture no louder at 100 than without; the sound you hear turned down as it rises (in dr_render) */
    /* v0.3 (Evan, 2026-10-26: at 100 still faint): FEEDBACK at a steady strength whatever the sound's level (its own
     * level followed and made up), about as before up to ~70, then over the top it crosses into singing in key (the
     * loop's gain past 1, its cap rising), the loudness only partly held. SNARL (Cross-mod's fourth choice): the
     * finished sound bends both drones' pitch instead (FM, like cross-FM, from the output); X-mod amt its depth */
#ifndef SING_TOP
#define SING_TOP 4.0f
#endif
    const float fsa = pfs[P_FEEDBACK] * 0.01f;
    const float fbk = fsa <= 0.7f ? fsa * 0.9f : 0.63f + (fsa - 0.7f) * (1.0f / 0.3f) * (SING_TOP - 0.63f);
    const float fnk = v->xs * depth_amtf(pfs[P_XMOD_AMT]) * 9.8f;   /* SNARL's depth (x the way in) */
#ifndef SING_CEIL
#define SING_CEIL 1.6f
#endif
    const float fbc = fsa <= 0.7f ? FB_CEIL : FB_CEIL + (fsa - 0.7f) * (1.0f / 0.3f) * (SING_CEIL - FB_CEIL);
    const float xw0 = pv[GP_WAVE] * 0.01f;
    /* DRIVE: the soft clip's input gain, 0.5 (clean: a sine comes through with its 3rd harmonic near -40 dB)
     * to 16 (as before at 100); the make-up keeps a signal of DRV_REF's size as loud as it went in, so the
     * knob adds grit, not level (1/sqrt(gain) took 6 dB off from 20 to 100) */
    const float drv = 0.5f * fm_exp2f(pv[GP_DRIVE] * 0.05f), mk = DRV_REF / sat(DRV_REF * drv);
    /* the drive's antialiasing (sat_aa), blended in from 40 to 60: below 40 the clip barely folds anything back, and
     * the sound and its cost stay as they were (the starting sound's drive is 20) */
    const float aaw = fm_clampf((pv[GP_DRIVE] - 40.0f) * 0.05f, 0.0f, 1.0f);
    if (aaw > 0.0f && v->dF <= 0.0f)
        v->dF = sat_F(v->du);                     /* (coming in: from the last input, as it was) */
    else if (aaw <= 0.0f)
        v->dF = 0.0f;
    /* resonance: damping k from 2 (none) to just under 0 (self-oscillation, held by the soft clip on the
     * band-pass state); the input drops as it rises, so the resonance has room to sing. In low-pass, what
     * that takes from the bass is added back (the DS-2's "no bass loss"): the input through a one-pole an
     * octave under the cutoff, times (1 - rin), so the passband stays level and the peak is left alone */
    const float r0 = pv[GP_RESO] * 0.001f;
    float k = 2.0f - 2.1f * r0, rin = 1.0f - 0.6f * r0 * r0;    /* per tick, when the LFO moves the resonance */
    const float lvl = pv[GP_LEVEL] * pv[GP_LEVEL] * 0.0001f * 0.6f * age_mk;   /* an audio taper: squared (50 is -12 dB) */
    /* drone I left, drone II right, as far apart as Width says (as it glides: read raw, each detent clicked); a drone
     * alone (the other out, II OUT) moves to the middle as the other fades (Evan, 2026-11-10: Bare sat on the left) */
    const float width = pfs[P_WIDTH] * 0.01f * grp[1 - g].gf, pan = g ? 0.5f + 0.5f * width : 0.5f - 0.5f * width;
    const float gl1 = fm_sqrtf(1.0f - pan) * 1.41421356f, gr1 = fm_sqrtf(pan) * 1.41421356f;
    float pdl, pdr, gl, gr;
    if (v->pl == 0.0f && v->pr == 0.0f) {        /* the first block: where it's set (both never 0 otherwise) */
        v->pl = gl1;
        v->pr = gr1;
    }
    pdl = (gl1 - v->pl) / (float)(n ? n : 1u);
    pdr = (gr1 - v->pr) / (float)(n ? n : 1u);
    gl = v->pl;                                   /* in a straight line across the block */
    gr = v->pr;
    v->pl = gl1;
    v->pr = gr1;
    const float drift_c = (float)par[P_DRIFT] * 0.0008f;          /* semitones: 8 cents at 100 */
    /* DRIFT's wanders, at 100: cutoff +-0.5 octave, X-WAVE +-0.12, level +-2.4 dB, detune +-10 cents; each a
     * slow random walk around where it's set (a new aim every 10..40 s, glided to over ~8 s) */
    const float dw = pfs[P_DRIFT] * 0.01f;
    const int iv = (unsigned)p[GP_INTERVAL] < DR_NINTERVALS ? p[GP_INTERVAL] : DR_IV_UNISON;
    /* the partner's interval: tempered, or just (pure ratios, so a fifth or a third doesn't beat; Detune
     * still adds beating on purpose); on another Tuning, the Tuning's (dr_partner_semi) */
    float ival;
    {   /* on a Tuning the interval follows the drone's note (dr_partner_semi): when the drone's pitch moves under it, it
         * glides there (~20 ms) as the pitch does, no step; a new interval switches at once, as it always has (and on
         * 12-tone and Quarter it never moves) */
        const float it = DR_FAR(dr_partner_semi)(g, iv);
        if (v->ivi != iv || fm_fabsf(it - v->ivs) < 1e-4f)
            v->ivs = it;
        else
            v->ivs += (it - v->ivs) * fm_minf((float)n * (1.0f / (0.02f * DR_SR)), 1.0f);
        v->ivi = iv;
        ival = v->ovon ? v->ov : v->ivs;
    }
    const float detune = pv[GP_DETUNE] * 0.003f;            /* semitones: 30 cents at 100 */
    /* Unison with no Detune is one fat oscillator: the partner is pulled into phase with the main (~25 ms),
     * so it can't sit half a cycle out and cancel it (it did: -20 dB, depending on what came before), and
     * Drift's wanders don't beat them. Any Detune lets it go. */
    const int plock = !v->ovon && iv == DR_IV_UNISON && pv[GP_DETUNE] < 0.5f;
    const float tune = 12.0f * fm_log2f(1.0f + (float)par[P_TUNE] * (1.0f / 4400.0f)),   /* A in Hz, as semitones */
                target = (float)p[GP_PITCH] * 0.01f;
    /* the cutoff, 0..1000 = log2 20 .. log2 18000 */
    const float cut_t = 4.32192809f + pv[GP_CUTOFF] * 0.00981378f;
    /* the links, on drone II: hard sync, pitch link, cross-mod (12 octaves a unit of drone I's sound, at
     * the top: its sound is about +-0.3, so +-3.6 octaves) */
    const int link = g == 1 && par[P_PLINK];
    const float xamt = g == 1 ? depth_amtf(pfs[P_XMOD_AMT]) * 12.0f : 0.0f;
    const float famt_x = depth_amtf(pfs[P_XMOD_AMT]) * 10.0f;   /* CROSS-FM: the other's sound (~+-0.3) x 10 at the top */
    const float frate = fade_rate() * (float)CR;
    float glide_k;
    uint32_t j0;
    {
        float gs = (float)par[P_GLIDE] * 0.01f;                    /* 0..10 s, quadratic */
        gs = 10.0f * gs * gs;
        /* time constant gs/4 reaches 98 % in gs */
        glide_k = gs > 0.0f ? 1.0f - fm_expf(-(float)CR / (gs * 0.25f * DR_SR)) : 1.0f;
    }
    if (v->gf <= 0.0f && v->gtarget <= 0.0f) {
        int q;
        v->peak = 0.0f;
        morph_snap(g);                          /* silent: its switches go straight where they're set */
        for (q = 0; q < DR_SCOPE_N; q++)        /* silent: the screen shows it flat */
            dr_scope[g][q] = 0.0f;
        if (g == 0) {                           /* silent, but a linked drone II still follows its pitch */
            uint32_t t;
            v->pitch = v->st = target;
            for (t = 0; t <= n / CR; t++)
                ptick[t] = target;
        }
        return;
    }
    for (j0 = 0; j0 < n; j0 += CR) {
        float inc, pinc, sinc, lk, ilk, mlk, plk, slk, gc, a1, a2, a3, gstep, gend, lend, lstep, lv = v->lv, ph = v->ph, pph = v->pph,
              sph = v->sph, om, fmod, fl, pmod, xw, wf, og, sg, xd, xpd, xh = v->xh, xc = v->xc, wl, wb, wh, sy, r, fbn, fsn;
        int wi, sync, sw = v->sw;
        float ic1 = v->ic1, ic2 = v->ic2, bc = v->bc, gf = v->gf, peak = v->peak, bk;
        float z0 = v->nz[0], z1 = v->nz[1], z2 = v->nz[2], z3 = v->nz[3], z4 = v->nz[4], z5 = v->nz[5], nh = v->nh;
        uint32_t nn = v->nn;                    /* the noise's states, kept in registers through the tick */
        uint32_t j, je = j0 + CR < n ? j0 + CR : n;
        int i;
        /* control rate: glide, drift, pitch, cutoff, fade. A knob's detents slide (dr_slide: a straight line at
         * the rate asked for), and the Glide follows the slide, so it smooths knob turns, scenes and the motion
         * loop as well as MIDI notes (the slide replaced it: knob turns arrived at once at any Glide) */
        if (v->slide > 0.0f) {
            float d = target - v->st;
            if (fm_fabsf(d) <= v->slide) {
                v->st = target;
                v->slide = 0.0f;
            } else {
                v->st += d > 0.0f ? v->slide : -v->slide;
            }
        } else {
            v->st = target;
        }
        v->pitch += (v->st - v->pitch) * glide_k;
        if (fm_fabsf(v->st - v->pitch) < 1e-4f)
            v->pitch = v->st;
        if (g == 0) {
            ptick[j0 / CR] = v->pitch;
        } else if (link) {                      /* PITCH LINK: drone I's pitch now, plus the offset */
            if (!v->linked)                     /* just linked: from where it is, no jump */
                v->lofs = v->pitch - ptick[j0 / CR];
            if (v->lslide > 0.0f) {             /* a knob's detents: a straight line, as the pitch's */
                float d = (float)par[P_PLINK_OFF] * 0.01f - v->lofs;
                if (fm_fabsf(d) <= v->lslide) {
                    v->lofs += d;
                    v->lslide = 0.0f;
                } else {
                    v->lofs += d > 0.0f ? v->lslide : -v->lslide;
                }
            } else {
                v->lofs += ((float)par[P_PLINK_OFF] * 0.01f - v->lofs) * 0.03f;   /* ~12 ms */
            }
            v->pitch = ptick[j0 / CR] + v->lofs;
        }
        v->linked = (uint8_t)link;
        /* then two one-pole smoothers of 8 ms: round the corners where one slide meets the next, and
         * keep a jump (a MIDI note) from clicking. 1 - exp(-CR / (0.008 s * SR)) = 0.0443. */
        v->ps1 += (v->pitch - v->ps1) * 0.0443f;
        v->ps2 += (v->ps1 - v->ps2) * 0.0443f;
        v->dcount += CR;
        if (v->dcount >= FRAMES_PER_DRIFT) {
            v->dcount = 0;
            v->dtarget[0] = rnd();
            v->dtarget[1] = rnd();
        }
        for (i = 0; i < 2; i++)
            v->drift[i] += (v->dtarget[i] - v->drift[i]) * 0.004f;   /* ~0.5 s glide between targets */
        for (i = 0; i < 4; i++) {                /* DRIFT's wanders */
            if (v->wc[i] == 0) {
                v->wt[i] = rnd();
                v->wc[i] = (uint32_t)((25.0f + 15.0f * rnd()) * DR_SR / (float)CR);   /* 10..40 s */
            }
            v->wc[i]--;
            v->wn[i] += (v->wt[i] - v->wn[i]) * 4.5e-5f;   /* ~8 s (a tick is 0.36 ms) */
        }
        /* the switches, a tick nearer their settings (MORPH) */
        morph_tick(g, mstep);
        og = (0.9f - 0.28f * v->pg) * omix;      /* main alone 0.9, with its partner 0.62 each */
        sg = sub * v->sg;
        sw = v->sw;
        xd = xamt * v->xm;
        xpd = v->xp * famt_x;                    /* CROSS-FM's depth, this tick */
        fbn = fbk * (1.0f / fbc) * 0.25f / fm_maxf(v->fbe, 0.03125f);   /* FEEDBACK's make-up and gain, this tick (a
                                                                          * division a sample was 4 % of the FM-1) */
        fsn = fnk * 0.22f / fm_maxf(g ? grp[0].fse : v->fse, 0.0275f);  /* SNARL's, this tick */
        wl = v->wm[DR_LP];
        sy = v->sy;
        sync = v->sync;
        /* the LFO: the oscillator's (pitch, up to +-24 semitones, or X-WAVE) and the filter's (cutoff, up
         * to +-5 octaves, or the resonance, its whole range at depth 100), each its route's wave(s) */
        om = route_at(v, 0, j0 / CR) * oamt;
        fl = route_at(v, 1, j0 / CR);
        fmod = (1.0f - v->dw[1]) * fl * famt * 5.0f;
        r = lfo_fit(r0, v->dw[1] * fl * ramt, v->dw[1] * fm_fabsf(ramt) * route_n(v, 1));
        dr_reso[g] = r * 1000.0f;                /* as it sounds, with the LFO (the screen) */
        k = 2.0f - 2.1f * r;
        rin = 1.0f - 0.6f * r * r;
        /* band-pass and high-pass get half of that input loss back (~1/sqrt(rin): +4 dB at 100), as the low-pass
         * gets its bass back: at full resonance they were 6-7 dB under it */
        wb = 1.0f + r * r * (0.3f + 0.28f * r * r);
        wh = v->wm[DR_HP] * wb;
        wb *= v->wm[DR_BP];
        pmod = (1.0f - v->dw[0]) * 24.0f * om;
        xw = fm_clampf(lfo_fit(xw0, 0.5f * v->dw[0] * om, v->dw[0] * fm_fabsf(xwamt) * route_n(v, 0)) + dw * 0.12f * v->wn[1],
                       0.0f, 1.0f) * 3.0f;
        dr_xw[g] = xw * (100.0f / 3.0f);          /* as it sounds, with the LFO and Wander (the screen) */
        wi = (int)(xw + 0.025f);                 /* within 0.02 of a wave (2 of the knob's 300ths): that */
        wf = xw - (float)wi;                     /* wave alone, as dr_param_text names it (33 is the */
        if (wf < 0.025f)                         /* triangle, 67 the saw; and one wave costs half two) */
            wf = 0.0f;
        if (wi > 2) {
            wi = 2;
            wf = 1.0f;
        }
        if (chr > 0.0f)                          /* the jitter: noise, low-passed to ~20 Hz */
            v->jit += (rnd() - v->jit) * 0.045f;
        inc = note_inc(v->ps2 + tune + pmod + v->drift[0] * drift_c + chr * (CHR_JIT * v->jit + mm) + mac_p[g]);
        pinc = note_inc(v->ps2 + tune + pmod + mac_p[g] + ival + detune + dw * 0.1f * v->wn[3] + v->drift[1] * drift_c +
                        chr * (CHR_JIT * v->jit + mm));
        const int tick = tick_d[g] > 0.0f || tick_u[g] > 0.0f;
        float sd = v->sdiv;
        if (tick) {                              /* TICK (v0.3), log2: from the sub as heard down to -7, then up by 4 */
            float sh = v->sdiv, l2;
            while (inc * sh < 20.0f / DR_SR && v->sdiv > 0.1f)   /* (lifted above 20 Hz as below, so a touch doesn't
                                                                   * drop it an octave) */
                sh *= 2.0f;
            l2 = (1.0f - tick_d[g]) * fm_log2f(sh) - 7.0f * tick_d[g] + 4.0f * tick_u[g];
            sd = fm_expf(l2 * 0.69314718f);
        }
        sinc = inc * sd;
        while (sinc < 20.0f / DR_SR && sd > 0.1f && !tick)   /* no subsonic sub: under 20 Hz, an octave up (the -2 oct
                                                          * and 1/5 at the bottom of the range); -3 and -4 oct go
                                                          * down there and click (v0.3) */
            sinc *= 2.0f;
        /* LOW: under A1 (55 Hz) an oscillator loses ~3 dB of loudness an octave, its energy a click a cycle and those
         * fewer (2 Hz was 18.5 dB under D3, 27 Hz 7): made up by sqrt(55 / f), so its clicks sit as loud as A1 (Evan,
         * 2026-10-24: +3 dB at 27 Hz, +14 at 2 Hz), the main oscillator, the partner and the sub at -3 / -4 oct
         * (which click under a drone) each their own. The largest goes on after the filter (lk); before it, each
         * oscillator goes in at its make-up over that, and the rest (the humming subs, which stay over 20 Hz, the noise,
         * Character's, Feedback's) that much lower (ilk): nothing hits the filter's drive harder than before, and the rest
         * comes out as before */
        {
            float fhz = inc * DR_SR, phz = pinc * DR_SR, shz = sinc * DR_SR;
            float mk_ = fhz < 55.0f ? fm_sqrtf(55.0f / fm_maxf(fhz, 0.5f)) : 1.0f;
            float pk_ = phz < 55.0f ? fm_sqrtf(55.0f / fm_maxf(phz, 0.5f)) : 1.0f;
            float sk_ = sd < 0.1f && shz < 55.0f ? fm_sqrtf(55.0f / fm_maxf(shz, 0.5f)) : 1.0f;   /* -3, -4 oct */
            lk = v->pg > 0.0f ? fm_maxf(mk_, pk_) : mk_;
            if (sg > 0.0f)
                lk = fm_maxf(lk, sk_);
            ilk = 1.0f / lk;
            mlk = mk_ * ilk;
            plk = pk_ * ilk;
            slk = sk_ * ilk;
        }
        v->cut += (cut_t + dw * 0.5f * v->wn[0] - v->cut) * 0.25f;   /* the knob's glide is in pfs; this rounds
                                                                        * its blocks (and Drift wanders it) */

        /* the LFO on the cutoff after the smoothing, so a quick LFO isn't blurred; 10 Hz .. 0.45 SR */
        gc = fm_tanf(FM_PI * fm_clampf(fm_exp2f(v->cut + fmod), 10.0f, 0.45f * DR_SR) * (1.0f / DR_SR));
        dr_cut[g] = (v->cut + fmod - 4.32192809f) * (1.0f / 0.0981378f);   /* as it sounds, with the LFO */
        a1 = 1.0f / (1.0f + gc * (gc + k));
        a2 = gc * a1;
        a3 = gc * a2;
        bk = 0.5f * gc / (1.0f + 0.5f * gc);     /* the makeup's one-pole, an octave under the cutoff */
        gend = v->gtarget > gf ? fm_minf(gf + frate, v->gtarget) : fm_maxf(gf - frate, v->gtarget);
        gstep = (gend - gf) * (1.0f / (float)CR);
        lend = lv + (lvl * fm_exp2f(dw * 0.4f * v->wn[2]) - lv) * 0.06f;   /* ~6 ms: a Level turn never clicks */
        lstep = (lend - lv) * (1.0f / (float)CR);
        for (j = j0; j < je; j++) {
            float ci = inc, cp = pinc, cs = sinc, x, y, v1, v2, v3, a;
            if (fnk > 0.0f && g == 1) {         /* SNARL on drone II: drone I's bend, the same sound's */
                ci *= snq[j];
                cp *= snq[j];
                cs *= snq[j];
            } else if (fnk > 0.0f) {            /* SNARL: the finished sound, fb_d samples ago, bends both drones (FM), its
                                                 * lows under ~10 Hz out, soft-limited as cross-FM */
                float rp = (float)(fb_w + j + FB_N) - fb_d, fr, fv, q;
                uint32_t ri = (uint32_t)rp;
                fr = rp - (float)ri;
                fv = fb_ring[ri & (FB_N - 1u)] + (fb_ring[(ri + 1u) & (FB_N - 1u)] - fb_ring[ri & (FB_N - 1u)]) * fr;
                v->fsh += (fv - v->fsh) * 0.001424f;
                fv -= v->fsh;
                v->fse += (fm_fabsf(fv) - v->fse) * 0.000427f;   /* its level made up to P1's (0.22; x8 at most): as
                                                                  * strong on a quiet sound as on P1 (Evan, 2026-10-26:
                                                                  * subtle on P12, strong on P1): fsn, a tick's */
                q = fv * fsn;
                q = q / (1.0f + fm_fabsf(q));
                v->fsc += (q - v->fsc) * 0.001424f;   /* its mean out after the limit, as cross-FM's (Evan, 2026-10-26:
                                                       * at 100 the pitch sank) */
                q = fm_clampf(1.0f + 0.9f * (q - v->fsc), 0.05f, 1.95f);
                ci *= q;
                cp *= q;
                cs *= q;
                snq[j] = q;
                fb_esn += 2.0f * (q - 1.0f) * (q - 1.0f);
            }
            if (xpd > 0.0f) {                   /* CROSS-FM (v0.3, the Lyra-8's): the other drone's sound speeds this one's
                                                 * oscillators up and down (linear FM), soft-limited so none stops (x0.05 ..
                                                 * x1.95). The pitch stays centred: the lows (under ~10 Hz) out before the
                                                 * limit, and the limited sound's mean out after it (a sound with a sub isn't
                                                 * even: limited, it pulled drone II 31 cents flat at the top) */
                float m = g ? xbuf[j] : ybuf[j], q;
                xh += (m - xh) * 0.001424f;
                q = xpd * (m - xh);
                q = q / (1.0f + fm_fabsf(q));
                xc += (q - xc) * 0.001424f;
                q = fm_clampf(1.0f + 0.9f * (q - xc), 0.05f, 1.95f);
                ci *= q;
                cp *= q;
                cs *= q;
            }
            x = xosc(wi, wf, ph, ci, pw);
            if (sync) {                         /* band-limit the restart (a polyBLEP of its jump) */
                x += v->sync_next;
                v->sync_next = 0.0f;
                if (sbuf[j]) {
                    float f = sfr[j], at = fm_minf(ph + f * ci, 0.999999f);
                    float d = xosc(wi, wf, 1e-6f, 1e-9f, pw) - xosc(wi, wf, at, 1e-9f, pw);
                    /* the oscillator's own BLEP will smooth a saw's or square's natural edge at 0 next
                     * sample: only the rest of the jump is ours there */
                    float nat = 2.0f * ((wi == DR_SAW ? 1.0f - wf : wi + 1 == DR_SAW ? wf : 0.0f) +
                                        OSC_SQ_K * (wi == DR_SQUARE ? 1.0f - wf : wi + 1 == DR_SQUARE ? wf : 0.0f));
                    x += d * (1.0f - f) * (1.0f - f) * 0.5f;
                    v->sync_next = -(d - nat) * f * f * 0.5f;
                }
            }
            if (v->pg > 0.0f)                   /* the partner, as loud as it's faded in */
                x = (x * mlk + v->pg * v->ig * plk * xosc(wi, wf, pph, cp, pw)) * og;
            else
                x *= og * mlk;
            if (sg > 0.0f)
                x += sg * slk * sub_wave(sw, sph, cs);
            if (nmix > 0.0f) {                  /* NOISE, coloured, beside the oscillators */
                float w;
                nn = nn * 1664525u + 1013904223u;
                w = (float)(int32_t)nn;
                z0 += (w - z0) * NZ_A[0];
                w = z0 + nsh * (w - z0);
                z1 += (w - z1) * NZ_A[1];
                w = z1 + nsh * (w - z1);
                z2 += (w - z2) * NZ_A[2];
                w = z2 + nsh * (w - z2);
                z3 += (w - z3) * NZ_A[3];
                w = z3 + nsh * (w - z3);
                z4 += (w - z4) * NZ_A[4];
                w = z4 + nsh * (w - z4);
                z5 += (w - z5) * NZ_A[5];
                w = z5 + nsh * (w - z5);
                nh += (w - nh) * NZ_HP;
                x += namp * ilk * (w - nh);
            }
            if (chr > 0.0f) {                   /* CHARACTER */
                float y2;
                v->ns = v->ns * 1664525u + 1013904223u;
                x += nz * ilk * (float)(int32_t)v->ns * (1.0f / 2147483648.0f);
                x += asym * x * x;
                y2 = x - v->dcx + 0.9993f * v->dcy;   /* the DC the x^2 makes, taken out (~5 Hz) */
                v->dcx = x;
                v->dcy = y2;
                v->rlp += (y2 - v->rlp) * rk;
                x = v->rlp;
            }
            if (fbk > 0.0f) {                   /* FEEDBACK: the sound fb_d samples ago (drone I's period), lows cut */
                float rp = (float)(fb_w + j + FB_N) - fb_d, fr, fv, in;
                uint32_t ri;
                ri = (uint32_t)rp;
                fr = rp - (float)ri;
                fv = fb_ring[ri & (FB_N - 1u)] + (fb_ring[(ri + 1u) & (FB_N - 1u)] - fb_ring[ri & (FB_N - 1u)]) * fr;
                v->fbh += (fv - v->fbh) * 0.011f;        /* ~80 Hz */
                fv -= v->fbh;
                v->fbe += (fm_fabsf(fv) - v->fbe) * 0.000427f;   /* its level, ~3 Hz: made up to 0.25 (x8 at most), in fbn */
                in = fbc * fm_tanhf(fv * fbn);  /* capped (it can't take over below 70); over the top the cap rises, so a
                                                 * dry sound sings too */
                in *= ilk;
                fb_eosc += x * x;
                fb_ein += in * in;
                x += in;
            }
            if (aaw > 0.0f) {
                float u = x * drv, a = sat_aa(u, &v->du, &v->dF);
                x = (aaw < 1.0f ? sat(u) + (a - sat(u)) * aaw : a) * mk;
            } else {
                v->du = x * drv;
                x = sat(v->du) * mk;
            }
            {                                   /* the bass makeup (low-pass, as weighted) */
                float bv = (x - bc) * bk;
                float lo = bv + bc;
                bc = lo + bv;
                x *= rin;
                y = wl * (1.0f - rin) * lo;
            }
            if (xd != 0.0f) {                   /* CROSS-MOD: drone I's sound moves this cutoff, per sample */
                float gg = fm_clampf(gc * fm_exp2f(xd * xbuf[j]), 1e-4f, 6.0f);
                a1 = 1.0f / (1.0f + gg * (gg + k));
                a2 = gg * a1;
                a3 = gg * a2;
            }
            v3 = x - ic2;
            v1 = a1 * ic1 + a2 * v3;
            v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = sat(2.0f * v1 - ic1);
            ic2 = 2.0f * v2 - ic2;
            y += wl * v2 + wb * v1 + wh * (x - k * v1 - v2);   /* the mode, crossfading on a switch */
            gf += gstep;
            lv += lstep;
            a = gf * gf * lv * sy * lk;
            y *= a;
            gl += pdl;
            gr += pdr;
            mixl[j] += y * gl;
            mixr[j] += y * gr;
            peak = fm_maxf(peak * 0.99995f, fm_fabsf(y));
            if (v->sc_n < DR_SCOPE_N) {         /* the scope: a point every sc_dec samples, from a cycle's start */
                if (++v->sc_cnt >= v->sc_dec) {
                    v->sc_cnt = 0;
                    sc_buf[g][v->sc_n++] = y;
                    if (v->sc_n == DR_SCOPE_N) {
                        int q;
                        for (q = 0; q < DR_SCOPE_N; q++)
                            dr_scope[g][q] = sc_buf[g][q];
                        dr_scope_t0[g] = v->sc_t0;
                        dr_scope_ci[g] = v->sc_ci;
                        dr_scope_dec[g] = v->sc_dec;
                        v->sc_wait = 1500;      /* ~34 ms, then the next */
                    }
                }
            } else if (v->sc_wait) {
                v->sc_wait--;
                v->sc_arm = 0;
            } else if (ph + ci >= 1.0f || ++v->sc_arm >= 4410u) {   /* armed: start on the next cycle, or after
                                                * 0.1 s anyway (hard sync can restart drone II's oscillator before
                                                * its cycle ends: the picture froze) */
                float d = 2.0f / (ci * (float)DR_SCOPE_N);
                v->sc_arm = 0;
                v->sc_n = 0;
                v->sc_cnt = 0;
                v->sc_dec = (uint16_t)(d < 1.0f ? 1.0f : d > 4000.0f ? 4000.0f : d);
                v->sc_t0 = sc_clk + j + 1;      /* the next sample starts the cycle */
                v->sc_ci = ci;
            }
            if (g == 1)                         /* for drone I's cross-FM, next block */
                ybuf[j] = y;
            if (g == 0) {                       /* for drone II: this sound, and where this oscillator restarts */
                xbuf[j] = y;
                sbuf[j] = ph + ci >= 1.0f;
                sfr[j] = sbuf[j] ? (1.0f - ph) / ci : 0.0f;
            } else if (sync && sbuf[j]) {
                ph = -sfr[j] * ci;             /* HARD SYNC: restart with drone I, at the very moment */
            }
            ph = advance(ph, ci);
            pph = advance(pph, cp);
            if (plock) {
                float d = ph - pph;
                d -= d >= 0.5f ? 1.0f : d < -0.5f ? -1.0f : 0.0f;
                pph += 0.001f * d;
                pph += pph < 0.0f ? 1.0f : pph >= 1.0f ? -1.0f : 0.0f;
            }
            sph = advance(sph, cs);
        }
        v->ph = ph;
        v->pph = pph;
        v->sph = sph;
        v->xh = xh;
        v->xc = xc;
        v->ic1 = fm_flush(ic1);
        v->ic2 = fm_flush(ic2);
        v->bc = fm_flush(bc);
        v->nz[0] = z0;
        v->nz[1] = z1;
        v->nz[2] = z2;
        v->nz[3] = z3;
        v->nz[4] = z4;
        v->nz[5] = z5;
        v->nh = nh;
        v->nn = nn;
        v->gf = gend;
        v->lv = lend;
        v->peak = peak;
    }
}

/* the PULSE for the n frames ahead, into pgain: two beats a cycle, the first (1 + swing) long, the second
 * (1 - swing); each a breath, open its first half (a sine), which Shape firms up: steeper through the middle
 * and flat at the top and bottom, a gate only at 100 (and even then ~15 ms edges at the default rate, and a
 * 3 ms slew: no tick). It was a swell mixed with a square gate, so any Shape over 0 stepped twice a beat.
 * Depth: how far it dips, in dB (0 .. -30 dB at 99; 100: silence) */
static float pulse_depth(int v)
{
    return v >= 100 ? 1.0f : 1.0f - fm_exp2f((float)v * (-30.0f / 99.0f * 0.166096405f));
}
/* a breath's gain at ph (0..2 a bar, two breaths; swing b1 the first's length). SHAPE under 0 firms it up, toward a
 * gate; over 0 a ramp (Evan, 2026-10-30: turning right lengthens it, as it was turning left): the rise (trough to peak) takes up to 97 % of the breath, the fall the rest, but never under
 * 20 ms (hz: breaths a second): quicker, it clicked */
static float pulse_gain(float ph, float depth, float shape, float b1, float hz)
{
    float f = ph < b1 ? ph / b1 : (ph - b1) / (2.0f - b1), o;
    if (shape > 0.0f) {
        float r = fm_minf(0.5f + 0.47f * shape, 1.0f - 0.02f * hz), u = f + 0.25f;
        u -= fm_floorf(u);
        if (r > 0.5f)
            u = u < r ? 0.5f * u / r : 0.5f + 0.5f * (u - r) / (1.0f - r);
        f = u - 0.25f;
        shape = 0.0f;
    }
    o = fm_clampf(0.5f * fm_sinf(FM_TWO_PI * f) / (1.0f + 0.98f * shape) + 0.5f, 0.0f, 1.0f);
    return 1.0f - depth * (1.0f - o);
}
/* THE TIME KNOBS (Setup 1): a power of the dial over each one's range, so most of it is for slow changes, a drone's
 * (Evan, 2026-11-07: an exponential curve gave 40 % of it to under a second): Fade 0.05 .. 60 s (a cube), Morph 0.02
 * .. 30 s (x^2.5), Scene time 0.1 s .. 5 min (x^4), 1 s near 25 on each. Saved ones converted (PROJ_FORMAT 41) */
float dr_time_s(int p, int v)
{
    const float x = fm_clampf((float)v * 0.01f, 0.0f, 1.0f), x2 = x * x;
    if (p == P_FADE)
        return 0.05f + 59.95f * x2 * x;
    if (p == P_MORPH)
        return 0.02f + 29.98f * x2 * fm_sqrtf(x);
    return 0.1f + 299.9f * x2 * x2;
}
float dr_pulse_wave(float ph)
{
    ph -= 2.0f * fm_floorf(ph * 0.5f);
    return pulse_gain(ph, pulse_depth(par[P_PULSE_DEPTH]), (float)par[P_PULSE_SHAPE] * 0.01f,
                      1.0f + (float)par[P_PULSE_SWING] * 0.01f, pulse_hz(par[P_PULSE_RATE]));
}
float dr_pulse_hz(int v)                       /* the screen's: as pulse_run's rate */
{
    const float dbeats = DIV_BEATS[(unsigned)par[P_PULSE_DIV] < DR_NDIV ? par[P_PULSE_DIV] : 2];
    if (v >= 0)
        return pulse_hz(v);
    return dr_ext_spb > 0.0f ? DR_SR / (dbeats * dr_ext_spb) : pulse_hz(par[P_PULSE_RATE]);
}
/* THE SWELL ON A MIDI CLOCK (v0.3, step 3; Evan, 2026-10-26): a breath a division of the beat (P_PULSE_DIV), its speed the
 * clock's tempo, its place steered toward the clock's beat (a gentle phase lock, ~0.3 s): it can't drift from the
 * master however its pulses wobble. The beat the UI last counted, carried on by the samples since (and on at the last
 * tempo when the clock stops). A Start: back to the top, at once */
static float ext_target(float beats_a_breath)          /* where the clock says the Swell is, 0..2 (two breaths) */
{
    float x = ext_beats() / beats_a_breath;
    x -= 2.0f * fm_floorf(x * 0.5f);
    return x;
}
static void pulse_run(uint32_t n)
{
    const int on = par[P_PULSE], ext = dr_ext_spb > 0.0f;
    const float dbeats = DIV_BEATS[(unsigned)par[P_PULSE_DIV] < DR_NDIV ? par[P_PULSE_DIV] : 2];
    const float hz = ext ? DR_SR / (dbeats * dr_ext_spb) : pulse_hz(par[P_PULSE_RATE]), step = hz * (1.0f / DR_SR),
                depth = pulse_depth(par[P_PULSE_DEPTH]);
    const float ak = 4.0f / (morph_s() * DR_SR);      /* switched on or off: in or out over the Morph */
    /* ... in dB: its dips' depth in dB follows pa (the depth reached through the floor's power), not their
     * gain (which left the first half nearly silent and all the change at the end) */
    const float floorg = fm_maxf(1.0f - depth, 0.0316f), lf = fm_log2f(floorg), dn = depth / (1.0f - floorg + 1e-9f);
    float pa = pulse_a;
    const float shape = (float)par[P_PULSE_SHAPE] * 0.01f, b1 = 1.0f + (float)par[P_PULSE_SWING] * 0.01f;
    float ph = pulse_ph, g = pulse_g, pt = 1.0f;
    uint32_t j;
    if (ext) {                                   /* steered by the clock: at its place when it starts (or a Start), else
                                                  * a fiftieth of the way a block */
        float tg = ext_target(dbeats), e = tg - ph;
        e -= e >= 1.0f ? 2.0f : e < -1.0f ? -2.0f : 0.0f;
        if (dr_ext_start || pa <= 0.0f)
            ph = tg;
        else
            ph += 0.02f * e;
        ph -= ph >= 2.0f ? 2.0f : ph < 0.0f ? -2.0f : 0.0f;
        dr_ext_start = 0;
        ext_n += n;
    }
    for (j = 0; j < n; j++) {
        float t = 1.0f;
        /* in or out like a breath: most of the change first, easing into place (an exponential approach:
         * ~98 % of the way in the Morph time); a straight line hung on, then seemed to snap at its end */
        pa += ((on ? 1.0f : 0.0f) - pa) * ak;
        if (on ? pa > 0.997f : pa < 0.003f)
            pa = on ? 1.0f : 0.0f;
        if (pa > 0.0f) {
            if (!(j & 3u))                       /* its curve every 4th sample, held (the 3 ms glide below rounds it):
                                                  * a sine and a division a sample were 2 % of the FM-1 */
                pt = pulse_gain(ph, pa >= 1.0f ? depth : dn * (1.0f - fm_exp2f(lf * pa)), shape, b1, hz);
            t = pt;
            ph += step;
            if (ph >= 2.0f) {
                ph -= 2.0f;
                if (on && par[P_LFO_LOCK])             /* a new bar: the LFO, locked to the pulse, starts over */
                    lfo_restart();
            }
        }
        g += (t - g) * 0.00753f;                 /* 1 - exp(-1 / (3 ms * SR)) */
        pgain[j] = g;
    }
    pulse_ph = pa > 0.0f || ext ? ph : 0.0f;     /* off (and out): the next one starts on a beat (on a clock, where it is) */
    pulse_a = pa;
    dr_pulse_ph = pulse_ph;
    pulse_g = g;
    dr_pulse = g;
}

/* the parameters, n samples nearer their settings (n 0: at once) */
static void pf_run(uint32_t n)
{
    int i;
    for (i = 0; i < DR_NPARAMS; i++) {
        float t = (float)par[i], d = pfstep[i] * (float)n;
        pfs[i] = !n ? t : pfs[i] < t ? fm_minf(pfs[i] + d, t) : fm_maxf(pfs[i] - d, t);
    }
}

#define OUT_KNEE 0.7f
DR_FAR_FN(void, render_group, (int, uint32_t))   /* the flash calls it in RAM the same way */
DR_FAR_FN(void, fx_run, (float *, float *, uint32_t))
/* the output clip: straight up to OUT_KNEE, then a tanh into full scale (a tanh all the way put a 3rd harmonic
 * on everything, -18 dB on a sine at 4/5, and cancelled a triangle's own) */
static inline float outclip(float x)
{
    float a = fm_fabsf(x);
    if (a <= OUT_KNEE)
        return x;
    a = OUT_KNEE + (1.0f - OUT_KNEE) * fm_tanhf((a - OUT_KNEE) * (1.0f / (1.0f - OUT_KNEE)));
    return x < 0.0f ? -a : a;
}

/* CLEAN holds the loudness (Evan, 2026-10-18; the review: it took the Swell's dips and the LFO's dark sweeps out,
 * and came in 2-8 dB louder). The sound's slow loudness is followed (over about 30 s, longer than its breath) while Clean is out; while it's
 * in, the level eases down (over about 3 s) to match it, as far as Clean is in: down only, by 12 dB at most */
static float cl_ref = -1.0f, cl_now, cl_g = 1.0f;
static void clean_hold(float *l, float *r, uint32_t k)
{
    float ms = 0.0f, a = gam[DR_G_CLEAN], to, s;
    uint32_t j;
    for (j = 0; j < k; j++)
        ms += l[j] * l[j] + r[j] * r[j];
    ms /= (float)(2u * (k ? k : 1u));
    if (cl_ref < 0.0f)
        cl_ref = cl_now = ms;
    cl_now += (ms - cl_now) * fm_minf((float)k / (3.0f * DR_SR), 1.0f);
    if (a <= 0.0f)
        cl_ref += (ms - cl_ref) * fm_minf((float)k / (30.0f * DR_SR), 1.0f);
    to = cl_now > cl_ref && cl_now > 1e-9f ? fm_maxf(fm_sqrtf(cl_ref / cl_now), 0.25f) : 1.0f;
    to = 1.0f + (to - 1.0f) * a;
    s = fm_minf((float)k / (1.0f * DR_SR), 1.0f);
    cl_g += (to - cl_g) * s;
    if (cl_g < 0.9999f)
        for (j = 0; j < k; j++) {
            l[j] *= cl_g;
            r[j] *= cl_g;
        }
}

void dr_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    /* MASTER: a volume up to 4/5 of its turn (there as loud as the old full turn); past that it drives the
     * soft clip harder (up to 5x) at about the same loudness, as the DS-2's volume knobs push into overdrive: the
     * output comes down by the drive^0.72 (QA 2026-10-20: 1/(1 + 0.6 x) left the top fifth 3-8 dB louder; now the
     * six presets measured are within +-2.3 dB, 0 on average) */
    const float m = (float)gain_q12 * (1.0f / 4096.0f), over = fm_maxf((m - 0.8f) * 5.0f, 0.0f);
    const float gain = fm_minf(m, 0.8f) * 1.78f * (1.0f + 4.0f * over), mout = fm_exp2f(-0.72f * fm_log2f(1.0f + 4.0f * over));
    drain();
    if (mac_dirty || mv_moving)
        macro_apply(n);
    while (n) {
        uint32_t k = n > BLK ? BLK : n, j;
        int g;
        float frate = fade_rate();
        for (j = 0; j < k; j++)
            mixl[j] = mixr[j] = 0.0f;
        pf_run(k);
        lfo_run(k);                              /* runs while stopped too: the rate lights keep moving */
        for (j = 0; j < k; j++) {
            xbuf[j] = 0.0f;
            sbuf[j] = 0;
        }
        if (fade > 0.0f || fade_target > 0.0f) {
            for (g = 0; g < DR_NGROUPS; g++)
                DR_FAR(render_group)(g, k);
            sc_clk += k;
        } else {                                 /* stopped: the pitches go straight where they're set, */
            pulse_a = par[P_PULSE] ? 1.0f : 0.0f;    /* and the switches are where they're set */
            pf_run(0);
            dly_rt = dly_rp = (uint8_t)par[P_DLY_LFO];
            dly_rw = 1.0f;
            lfo_wx[0] = lfo_wx[1] = 1.0f;
            for (g = 0; g < DR_NGROUPS; g++) {   /* so the screen shows them, and PLAY starts there */
                grp_t *v = &grp[g];
                int q;
                morph_snap(g);
                for (q = 0; q < DR_SCOPE_N; q++) /* and the drones are drawn flat */
                    dr_scope[g][q] = 0.0f;
                float t = (float)par[DR_GP(g, GP_PITCH)] * 0.01f;
                if (g == 1 && par[P_PLINK]) {
                    v->lofs = (float)par[P_PLINK_OFF] * 0.01f;
                    t = grp[0].pitch + v->lofs;
                }
                v->pitch = v->ps1 = v->ps2 = v->st = t;
                v->slide = v->lslide = 0.0f;
            }
        }
        pulse_run(k);
        for (j = 0; j < k; j++) {                /* the PLAY fade and the pulse, then the effects */
            float a;
            fade = fade_target > fade ? fm_minf(fade + frate, fade_target) : fm_maxf(fade - frate, fade_target);
            a = fade * fade * pgain[j];
            mixl[j] *= a;
            mixr[j] *= a;
        }
        /* the effects, after the fade: the tails ring on when stopped. Stopped, and silent (below -120 dB)
         * for longer than the delay's line (743 ms: an echo still in it would have come out by then), they
         * rest until PLAY */
        if (fade > 0.0f || fade_target > 0.0f || fx_quiet < 160u) {
            float pk = 0.0f;
            DR_FAR(fx_run)(mixl, mixr, k);
            clean_hold(mixl, mixr, k);
            for (j = 0; j < k; j++)
                pk = fm_maxf(pk, fm_maxf(fm_fabsf(mixl[j]), fm_fabsf(mixr[j])));
            fx_quiet = pk < 1e-6f ? fx_quiet + 1u : 0u;
            dr_tail = pk;
        } else {                                 /* resting: the delay goes straight to its knobs */
            fx_rest();
            dr_tail = 0.0f;
        }
        {                                        /* FEEDBACK: this block's sound, for the drones 256 samples on */
            float pk = 0.0f;
            for (j = 0; j < k; j++) {
                float m = fm_clampf((mixl[j] + mixr[j]) * 0.5f, -2.0f, 2.0f);
                fb_ring[(fb_w + j) & (FB_N - 1u)] = m;
                pk = fm_maxf(pk, fm_fabsf(m));
            }
            fb_w = (fb_w + k) & (FB_N - 1u);
#ifndef SING_COMP
#define SING_COMP 0.25f
#endif
            {   /* the meter: Feedback's share of the sound; Snarl's bend (how far it moves the drones' speed, x3) */
                float m = pfs[P_FEEDBACK] > 0.0f && fb_eosc > 1e-9f ? fm_sqrtf(fb_ein / (fb_eosc + fb_ein)) : 0.0f;
                dr_snarl = fm_maxf(dr_snarl * 0.95f, fm_minf(3.0f * fm_sqrtf(fb_esn / (float)(2u * k)), 1.0f));
                dr_fb = fm_maxf(dr_fb * 0.95f, m);
                (void)pk;
                fb_esn = 0.0f;
            }
            {   /* what you hear, turned down by part of what Sing adds (smoothed, ~60 ms): its power's ratio^0.25, half
                 * the loudness it adds held back (it held all of it: at 100 it was only a change of tone) */
                float sa = fm_maxf(pfs[P_FEEDBACK], 0.0f) * 0.01f;
                float to = sa > 0.0f && fb_eosc > 1e-9f ? fm_exp2f(SING_COMP * fm_log2f(fb_eosc / (fb_eosc + fb_ein))) : 1.0f;
                /* ... and a trim with the knob (the resonance it rings up isn't in what it adds): 1 dB by 70, 5 at the top */
                to *= fm_exp2f(-0.166096f * (sa <= 0.7f ? sa * (1.0f / 0.7f) : 1.0f + (sa - 0.7f) * (4.0f / 0.3f)));
                float c0 = fb_c, dc;
                fb_c += (to - fb_c) * fm_minf((float)k / (0.06f * DR_SR), 1.0f);
                dc = (fb_c - c0) / (float)k;
                if (c0 < 1.0f || fb_c < 1.0f)
                    for (j = 0; j < k; j++) {
                        c0 += dc;
                        mixl[j] *= c0;
                        mixr[j] *= c0;
                    }
                fb_ein = fb_eosc = 0.0f;
            }
            {   /* the next block's loop length: drone I's period, a multiple or a fraction of it, 256 .. 510 */
                float per = DR_SR / (440.0f * fm_exp2f((grp[0].ps2 - 69.0f) * (1.0f / 12.0f))), d;
                d = per >= 256.0f ? per / (float)(int)(per * (1.0f / 256.0f)) : per * (float)(int)(256.0f / per + 0.999f);
                d = fm_clampf(d, 256.0f, 510.0f);
                fb_d += (d - fb_d) * fm_minf((float)k / (0.05f * DR_SR), 1.0f);
            }
        }
        if (!dr_phones || spk_x > 0.0f) {   /* the SPEAKER low cut: 12 dB, ~120 Hz (TPT state-variable), while the
                                             * speaker plays; never with headphones in */
            /* on and off it crossfades (~20 ms: switched at once, its phase shift made a step, a click); its
             * corner follows the knob as it glides, and stays where it was while it fades out */
            const float on = dr_phones ? 0.0f : 1.0f, xs = 1.0f / (0.02f * DR_SR);
            float gc, a1, a2, a3;
            gc = fm_tanf(FM_PI * 40.0f * fm_exp2f(spk_f * 0.0258496f) * (1.0f / DR_SR));
            a1 = 1.0f / (1.0f + gc * (gc + 1.41421356f));
            a2 = gc * a1;
            a3 = gc * a2;
            for (j = 0; j < k; j++) {
                int c;
                spk_x = toward(spk_x, on, xs);
                for (c = 0; c < 2; c++) {
                    float *m = c ? mixr : mixl, v3 = m[j] - spk[c][1];
                    float v1 = a1 * spk[c][0] + a2 * v3, v2 = spk[c][1] + a2 * spk[c][0] + a3 * v3;
                    spk[c][0] = 2.0f * v1 - spk[c][0];
                    spk[c][1] = 2.0f * v2 - spk[c][1];
                    m[j] = m[j] - spk_x * (1.41421356f * v1 + v2);
                }
            }
            if (spk_x <= 0.0f)                   /* off: its states cleared for the next time it's on */
                spk[0][0] = spk[0][1] = spk[1][0] = spk[1][1] = 0.0f;
        }
        if (dr_phones || hpc_x > 0.0f) {    /* the HEADPHONE low cut (HP_CUT_HZ), the speaker's form */
            const float on = dr_phones ? 1.0f : 0.0f, xs = 1.0f / (0.02f * DR_SR);
            const float gc = fm_tanf(FM_PI * HP_CUT_HZ * (1.0f / DR_SR));
            const float a1 = 1.0f / (1.0f + gc * (gc + 1.41421356f)), a2 = gc * a1, a3 = gc * a2;
            for (j = 0; j < k; j++) {
                int c;
                hpc_x = toward(hpc_x, on, xs);
                for (c = 0; c < 2; c++) {
                    float *m = c ? mixr : mixl, v3 = m[j] - hpc[c][1];
                    float v1 = a1 * hpc[c][0] + a2 * v3, v2 = hpc[c][1] + a2 * hpc[c][0] + a3 * v3;
                    hpc[c][0] = 2.0f * v1 - hpc[c][0];
                    hpc[c][1] = 2.0f * v2 - hpc[c][1];
                    m[j] = m[j] - hpc_x * (1.41421356f * v1 + v2);
                }
            }
            if (hpc_x <= 0.0f)
                hpc[0][0] = hpc[0][1] = hpc[1][0] = hpc[1][1] = 0.0f;
        }
        for (j = 0; j < k; j++) {                /* the MASTER (its top fifth drives into the clip), the clip; */
            float o;                             /* the dip after it (a straight line: out 20 ms, back 30 ms) */
            if (duck != duck_to)
                duck = duck_to < duck ? fm_maxf(duck - 1.0f / (0.020f * DR_SR), duck_to)
                                      : fm_minf(duck + 1.0f / (0.030f * DR_SR), duck_to);
            o = mout * 8300000.0f * duck;
            out[2 * j] = (int32_t)(outclip(mixl[j] * gain) * o);
            out[2 * j + 1] = (int32_t)(outclip(mixr[j] * gain) * o);
        }
        out += 2 * k;
        n -= k;
    }
    for (int g = 0; g < DR_NGROUPS; g++) {
        dr_level[g] = grp[g].peak;
        dr_pitch[g] = grp[g].ps2;
    }
    dr_fade = fade;
    dr_duck_level = duck;
}
