#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Felucca-Engines' own test: the tables, the standalone host (tests/fe_host.c), its renders against
# tests/golden.txt. Run from anywhere:  sh tests/run.sh   (CC=... to pick the compiler; UPDATE=1 rewrites
# the golden file: only for a reviewed, intended change of the sound)
set -e
cd "$(dirname "$0")/.."
mkdir -p build
python3 tools/gen_engine_tables.py build/fe_tables.h
${CC:-cc} -O2 -std=c99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Ibuild \
    -o build/fe_host tests/fe_host.c
build/fe_host > build/renders.txt
if [ "${UPDATE:-0}" = 1 ]; then
    cp build/renders.txt tests/golden.txt
    echo "tests/golden.txt rewritten ($(wc -l < tests/golden.txt | tr -d ' ') renders)"
    exit 0
fi
if diff -u tests/golden.txt build/renders.txt; then
    echo "ok: $(wc -l < tests/golden.txt | tr -d ' ') renders as in tests/golden.txt"
else
    echo "FAIL: the renders above differ from tests/golden.txt"
    exit 1
fi

# ANALOG signed scaling: frozen audio hash, then the same sweep without UBSan exclusions.
${CC:-cc} -O2 -std=c99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Ibuild \
    -o build/analog_signed_test tests/analog_signed_test.c
build/analog_signed_test
SAN="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
SAN_OK=0
if [ "${SANITIZE:-1}" = 0 ]; then
    echo "skip: ANALOG strict ASan/UBSan (SANITIZE=0)"
else
    printf 'int main(void){return 0;}\n' > build/san_probe.c
    if ${CC:-cc} $SAN -o build/san_probe build/san_probe.c 2>/dev/null && build/san_probe; then
        SAN_OK=1
        ${CC:-cc} $SAN -std=c99 -Wno-unused-function -Wno-unused-parameter -Ibuild \
            -o build/analog_signed_strict tests/analog_signed_test.c
        build/analog_signed_strict
    else
        echo "skip: ANALOG strict ASan/UBSan (compiler/runtime unavailable)"
    fi
fi

# The sample rate is a build-time choice (tools/gen_engine_tables.py --fs). 44100 stays the default: its header
# is the one above, byte for byte, and dsp.c's 44.1 kHz rate constants are the generator's.
python3 tools/gen_engine_tables.py --check src/dsp.c
mkdir -p build/fs44100
python3 tools/gen_engine_tables.py --fs 44100 build/fs44100/fe_tables.h
cmp build/fs44100/fe_tables.h build/fe_tables.h
echo "ok: --fs 44100 writes the default header"

# Other rates (FS_CHECK, "48000 96000" by default): the tables at FS, then
#   tests/fs_check.c: the pitch of four engines, the filter cutoff and LOFI's TONE pole, measured at FS
#   tests/fe_host.c's 122 renders: all there, each peak within 1/4 .. 4 x its 44.1 kHz peak (tests/golden.txt;
#     zero where that is zero)
#   ASan/UBSan (when available), strict: no memory error and no undefined behaviour at all, the same
#     renders as without the sanitizers (at 44.1 kHz: tests/golden.txt)
san_strict() {   # $1: a build dir with its fe_tables.h: fe_host and fs_check with the sanitizers, stopping at the first error
    ${CC:-cc} $SAN -std=c99 -Wno-unused-function -Wno-unused-parameter -I"$1" -o "$1/fe_host_san" tests/fe_host.c
    ${CC:-cc} $SAN -std=c99 -Wno-unused-function -Wno-unused-parameter -I"$1" -o "$1/fs_check_san" \
        tests/fs_check.c -lm
    "$1/fe_host_san" > "$1/renders_san.txt"
    "$1/fs_check_san" > /dev/null
}
if [ "$SAN_OK" = 1 ]; then
    san_strict build/fs44100
    cmp tests/golden.txt build/fs44100/renders_san.txt
    echo "ok: strict ASan/UBSan at FS 44100: no memory error, no undefined behaviour, the 122 golden renders"
else
    echo "skip: strict ASan/UBSan at FS 44100 (SANITIZE=0 or unavailable)"
fi
for fs in ${FS_CHECK:-48000 96000}; do
    d=build/fs$fs
    mkdir -p $d
    python3 tools/gen_engine_tables.py --fs $fs $d/fe_tables.h
    ${CC:-cc} -O2 -std=c99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -I$d \
        -o $d/fs_check tests/fs_check.c -lm
    $d/fs_check
    ${CC:-cc} -O2 -std=c99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -I$d \
        -o $d/fe_host tests/fe_host.c
    $d/fe_host > $d/renders.txt
    python3 - tests/golden.txt $d/renders.txt $fs <<'PY'
import sys
gold = {l.split()[0]: int(l.split()[2]) for l in open(sys.argv[1])}
got = {l.split()[0]: int(l.split()[2]) for l in open(sys.argv[2])}
# (Fantasma: DRIPS, noise/10, drops a few random drops a second: at another rate they fall elsewhere in the render,
# its peak a matter of where; checked to render, not for its peak)
SPARSE = {"noise/10"}
bad = [k for k in gold if k not in got or (k not in SPARSE and not gold[k] / 4 <= got[k] <= gold[k] * 4) or
       (k not in SPARSE and (got[k] == 0) != (gold[k] == 0))]
for k in bad:
    print(f"FAIL: {k}: peak {got.get(k)} at FS {sys.argv[3]}, {gold[k]} at 44.1 kHz")
if bad or len(got) != len(gold):
    sys.exit(1)
r = [got[k] / gold[k] for k in gold if gold[k]]
print(f"ok: {len(got)} renders at FS {sys.argv[3]}, peaks {min(r):.2f} .. {max(r):.2f} x their 44.1 kHz peaks")
PY
    if [ "$SAN_OK" = 1 ]; then
        san_strict $d
        cmp $d/renders.txt $d/renders_san.txt
        echo "ok: strict ASan/UBSan at FS $fs: no memory error, no undefined behaviour, the same renders"
    else
        echo "skip: strict ASan/UBSan at FS $fs (SANITIZE=0 or unavailable)"
    fi
done
