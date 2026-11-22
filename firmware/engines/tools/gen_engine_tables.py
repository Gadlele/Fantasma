#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Fixed-point lookup tables of the Felucca engines (no float on the target).

FS = 44100 Hz by default, control rate = every CTL (32) samples. The engines (src/) use FS, CTL, SINE,
PITCH_OCT / pitch_inc(), SVF_G and TANH_Q15; a host either includes the header this writes,
  gen_engine_tables.py [--fs RATE] fe_tables.h
or makes the same tables itself from the functions below (Felucca's own table generator imports them, so
its tables are these, line for line).

--fs picks the sample rate (FS_MIN .. FS_MAX). At 44100 the header is the tables alone (the engines' own
44.1 kHz constants are their defaults, src/dsp.c); at any other rate it also carries the engines' rate
constants (rate_consts(): FE_RATE_CONSTS and the FE_* below), the 44.1 kHz design moved to that rate.
--check src/dsp.c checks that dsp.c's defaults are rate_consts() at 44100.
"""
import argparse
import math
import re
import sys
from pathlib import Path

FS = 44100                       # the default rate (Felucca's); main() sets the one asked for
CTL = 32                         # samples a control tick (a block): kept at every rate
FS_REF = 44100                   # the rate the engines' constants were designed at
FS_MIN = 32000                   # below ~26.5 kHz the top note (13.2 kHz) passes Nyquist: PITCH_OCT breaks
FS_MAX = 192000


def arr(name, ctype, vals, per=12):
    out = [f"static const {ctype} {name}[{len(vals)}] = {{"]
    for i in range(0, len(vals), per):
        out.append("    " + ", ".join(str(v) for v in vals[i:i + per]) + ",")
    out.append("};")
    return out


def sine():
    # (1025 points: SINE[i + 1] needs no wrap)
    return arr("SINE", "int16_t", [int(round(32767 * math.sin(2 * math.pi * i / 1024))) for i in range(1025)])


def pitch():
    # phase increment for pitch in 1/16 semitone, MIDI 0..127
    inc = []
    for p in range(128 * 16):
        f = 440.0 * 2 ** ((p / 16 - 69) / 12)
        inc.append(min(int(f / FS * 2 ** 32), 0x7FFFFFFF))
    # stored as its top octave (1856..2047) shifted down: floor(floor(x) / 2^k) = floor(x / 2^k), so
    # pitch_inc(p) equals the 2048-entry table bit for bit (checked here); 7.4 KB less flash
    oct_ = inc[1856:2048]
    assert all(inc[q] == oct_[(q + 64) % 192] >> (10 - (q + 64) // 192) for q in range(2048))
    assert all((q + 64) * 21846 >> 22 == (q + 64) // 192 for q in range(2048))
    L = arr("PITCH_OCT", "uint32_t", oct_, 8)
    L += ["/* phase increment of pitch p in 1/16 semitone, 0..2047 (MIDI 0..127): 440 Hz * 2^((p / 16 - 69) / 12) */",
          "static inline uint32_t pitch_inc(uint32_t p)",
          "{",
          "    uint32_t i = p + 64u, o = (i * 21846u) >> 22;     /* i / 192: the octave above PITCH_OCT's - 10 */",
          "    return PITCH_OCT[i - o * 192u] >> (10u - o);",
          "}"]
    return L


def cutoff_hz():
    """the filter cutoffs of the parameter values 0..127: 30 Hz .. 16 kHz"""
    return [30.0 * ((16000 / 30) ** (v / 127)) for v in range(128)]


def svf_g():
    # trapezoidal (Simper) SVF: g = tan(pi fc / FS), Q12; stable at any cutoff/resonance
    return arr("SVF_G", "uint16_t", [int(4096 * math.tan(math.pi * min(f, 0.45 * FS) / FS)) for f in cutoff_hz()])


def tanh_q15():
    # soft clip curve for |x| in 0..2 (Q12 in, Q15 out): tanh
    return arr("TANH_Q15", "int16_t", [int(32767 * math.tanh(i / 256 * 2)) for i in range(257)])


def rate_consts(fs):
    """the engines' rate constants at fs: (name, value, what) of the scalars, then the LOFI TONE pole map.
    Each is its 44.1 kHz value (the literal in the engine, now dsp.c's default) moved to fs: a one-pole
    coefficient keeps its corner in Hz (pole p -> p^(FS_REF / fs)) and its DC gain, a per-block decay its
    time constant. At FS_REF every value is the literal itself."""
    r = FS_REF / fs

    def pole(p44):               # Q15 pole of a one-pole at 44.1 kHz -> the same corner at fs
        return int(round(32768 * (p44 / 32768) ** r))

    def gain(g44, p44, p):       # the input gain that keeps the section's DC gain g / (1 - p)
        return int(round(g44 * (32768 - p) / (32768 - p44)))

    out = []
    for name, p44, g44 in (("PINK0", 32690, 3245), ("PINK1", 31555, 9716), ("PINK2", 18678, 17247),
                           ("BROWN", 32580, 3480)):
        p = pole(p44)
        out.append((f"FE_NOISE_{name}_P", p, f"NOISE {name.lower()} pole, Q15"))
        out.append((f"FE_NOISE_{name}_G", gain(g44, p44, p), "its input gain"))
    for name, d44 in (("PDEC_SLOW", 65457), ("PDEC_FAST", 65219)):
        out.append((f"FE_WHEEL_{name}", min(65535, int(round(65536 * (d44 / 65536) ** r))),
                    "WHEEL percussion decay a block, Q16"))
    out.append(("FE_WHEEL_CLKDEC", int(round(32768 * 0.75 ** r)), "WHEEL key click decay a block, Q15"))
    return out


def pole_map(fs):
    # LOFI TONE: a one-pole coefficient a (Q15) at 44.1 kHz -> the one with the same corner at fs, at a = i * 256
    r = FS_REF / fs
    return arr("FE_POLE", "int16_t",
               [min(32767, int(round(32768 * (1 - (1 - min(i * 256, 32767) / 32768) ** r)))) for i in range(129)])


def rate_block(fs):
    L = ["/* the engines' rate constants at FS (src/dsp.c has their 44.1 kHz values) */", "#define FE_RATE_CONSTS 1"]
    L += [f"#define {n} {v}    /* {w} */" for n, v, w in rate_consts(fs)]
    return L + pole_map(fs)


def check_defaults(path):
    """dsp.c's 44.1 kHz defaults are rate_consts(FS_REF): the generator and the engines agree"""
    src = Path(path).read_text()
    bad = 0
    for n, v, _ in rate_consts(FS_REF):
        m = re.search(rf"#define {n} (-?\d+)", src)
        if not m or int(m.group(1)) != v:
            print(f"{path}: {n} is {m.group(1) if m else 'missing'}, rate_consts(44100) gives {v}")
            bad = 1
    if not bad:
        print(f"ok: {path}'s 44.1 kHz rate constants are rate_consts({FS_REF})")
    return bad


def main(path, fs=FS_REF):
    global FS
    if not FS_MIN <= fs <= FS_MAX:
        sys.exit(f"gen_engine_tables.py: --fs {fs}: supported {FS_MIN} .. {FS_MAX}")
    FS = fs
    L = ["/* generated by tools/gen_engine_tables.py (Felucca-Engines) */", "#pragma once", "#include <stdint.h>",
         f"#define FS {FS}", f"#define CTL {CTL}", ""]
    L += sine() + pitch() + svf_g() + tanh_q15()
    if FS != FS_REF:             # at 44.1 kHz the header stays the tables alone (as it always was)
        L += rate_block(FS)
    Path(path).write_text("\n".join(L) + "\n")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="the Felucca engines' tables as a C header")
    ap.add_argument("out", nargs="?", help="the header to write (fe_tables.h)")
    ap.add_argument("--fs", type=int, default=FS_REF, help=f"sample rate, {FS_MIN} .. {FS_MAX} (default {FS_REF})")
    ap.add_argument("--check", metavar="DSP_C", help="check dsp.c's 44.1 kHz rate constants against rate_consts()")
    a = ap.parse_args()
    if a.check:
        sys.exit(check_defaults(a.check))
    if not a.out:
        ap.error("the output header is required")
    main(a.out, a.fs)
