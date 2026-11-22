# Felucca-Engines

Six synthesis engines of [Felucca](https://github.com/hugelton/Felucca), with the DSP helpers
and the tables they share, under the MIT licence. Plain C99, fixed point only (no float, no
allocation, no library calls), written for a small 32-bit microcontroller at 44.1 kHz; the sample
rate is a build-time choice (32 to 96 kHz, see [Sample rate](#sample-rate)).

| Engine | File | What it is |
| --- | --- | --- |
| ANALOG | `src/eng_analog.c` | Two band-limited oscillators (saw, square, triangle, sine, PWM; hard sync and a sub oscillator), noise, drive, a trapezoidal state-variable filter (low-, band- or high-pass) |
| LOFI | `src/eng_lofi.c` | A chip voice: pulse, triangle, saw, shift-register noise and a 32 x 4-bit wave memory (16 tables), bit depth, sample hold, pitch sweep, arpeggio, a stepped volume envelope |
| TRIO | `src/eng_trio.c` | Three oscillators with ring modulation, hard sync and combined (AND) waves into a 12 dB/oct multimode filter |
| WHEEL | `src/eng_wheel.c` | A tonewheel-style additive organ: nine drawbar partials, 16 registrations, percussion, key click, drive and a rotary effect |
| NOISE | `src/eng_noise.c` | White / pink / brown noise, random impulses and shift-register noise through a resonant filter, with drift and a bit crusher |
| PHASE | `src/eng_phase.c` | Phase distortion: eight waves (bent-phase cosines and windowed resonant sines), a second wave every other cycle, a detuned second line mixed or ring-modulated, a sub oscillator and a sine wavefolder |

Shared:

- `src/felucca_engine.h`: the engine interface (parameter descriptors, the voice, its per-block
  modulation, presets, the engine table entry) and what a host must provide.
- `src/dsp.c`: oscillators with polyBLEP, the state-variable filter, soft clipping, noise, the
  amplitude ramp (all `static inline`).
- `tools/gen_engine_tables.py`: the lookup tables (`SINE`, `PITCH_OCT` / `pitch_inc()`, `SVF_G`,
  `TANH_Q15`, `FS`, `CTL`) as a C header, for 44.1 kHz or the rate you give it.

## Using the engines

The engines are compiled as one translation unit with their host, in this order:

```c
#define NVOICE 8                    /* voices per part */
#define NPART 4                     /* parts */
#include "fe_tables.h"              /* python3 tools/gen_engine_tables.py fe_tables.h */
#include "felucca_engine.h"
enum { P_ATK, P_REL, P_FTYPE, P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7, P_COUNT };
typedef struct track { int16_t p[P_COUNT]; voice_t v[NVOICE]; } track_t;
static track_t trk[NPART];
#include "dsp.c"
#include "eng_analog.c"             /* ... the engines you want */
```

Each engine is an `engine_t` (`ENG_ANALOG`, `ENG_LOFI`, `ENG_TRIO`, `ENG_WHEEL`, `ENG_NOISE`, `ENG_PHASE`):
its eight parameters with their ranges and names, its presets, and the functions the host calls
per voice (`note_on`, `render`) and per block (`block`, `amp`, and `post`, an optional effect over the whole part after its voices). `render` adds one block (at most
`CTL` = 32 samples) of a voice to a buffer. The header documents the rest of the contract: the
host's track and parameter numbers, how a voice is started and retriggered, and the envelope
fields that LOFI and WHEEL read.

`tests/fe_host.c` is a complete, minimal host: it plays every preset through a short phrase.

## Sample rate

The rate is chosen when the tables are generated; the default is 44100 Hz (Felucca's):

```sh
python3 tools/gen_engine_tables.py fe_tables.h               # 44.1 kHz
python3 tools/gen_engine_tables.py --fs 48000 fe_tables.h    # 48 kHz (32000 .. 192000)
```

Build the engines with that header as above; nothing else changes. At another rate the header also
carries the engines' rate constants, and `src/dsp.c` moves what the engines time in samples or blocks
(sample holds, filter corners, decays, the LOFI arpeggio and vibrato, the WHEEL rotors, the NOISE drift)
to the new rate at compile time, so there is no cost per sample. `CTL` stays 32 samples. At 44100 the
header and the output are exactly what they were before the rate became a choice.

32 to 96 kHz is supported; up to 192 kHz builds and plays. The limits of the fixed point (the lowest
filter cutoffs get coarser at high rates, some corners move by up to half an octave between the octave
steps, white noise is quieter in the audible band at high rates) are listed in `src/felucca_engine.h`.

## Tests

```sh
sh tests/run.sh
```

builds the tables and the host, renders 122 phrases and compares a hash of each with
`tests/golden.txt`. A change of any engine's output fails. Felucca runs the same host with its own
generated tables, so the two cannot drift apart.

It also sweeps 1,890 ANALOG cases (all waves/filter taps, PWM shapes,
resonance/drive corners and amplitude ramps) against a frozen pre-fix audio
hash. When available, ASan/UBSan runs this sweep, and the 122 phrases and `tests/fs_check.c` at
every rate checked, without exclusions or recovery (the sanitized renders must be the same);
an unavailable sanitizer compiler/runtime is reported as a skip.

Then the other rates: it checks that `--fs 44100` writes the default header byte for byte, and at
48 and 96 kHz (`FS_CHECK="32000 88200"` for others) measures the pitch of four engines, the filter
cutoff and LOFI's tone filter (`tests/fs_check.c`), renders the 122 phrases (each peak near its
44.1 kHz one).

## In Felucca

Felucca includes this repository as a git submodule (`engines/`, next to its `src/`). Its
`engines.c` includes `dsp.c` and the six `eng_*.c` files from here, its `core.h` includes
`felucca_engine.h`, and its table generator imports `tools/gen_engine_tables.py`. The firmware
built from them is GPL-3.0-only as a whole; the files of this repository stay MIT.

Clone Felucca with `git clone --recurse-submodules` (or run `git submodule update --init` in a
clone): its complete source includes this repository.

## Credits

The state-variable filter follows Andrew Simper's trapezoidal design; the pink noise filter uses
Paul Kellet's coefficients (the "economy" version), in fixed point. polyBLEP and xorshift32 are
the well-known techniques.

## Licence

MIT, see [LICENSE](LICENSE). Copyright (c) 2026 Leo Kuroshita (@kurogedelic), Hügelton
Instruments.

Changes to these files are welcome as pull requests here, under the MIT licence.
