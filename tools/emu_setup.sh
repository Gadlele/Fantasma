#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Fantasma: everything the browser emulator (web/emu) needs, in one go, on a plain Linux box such as a GitHub Codespace:
# Python's Pillow and fontTools, Emscripten (in ~/emsdk), the generated tables (build/gen), then web/emu/build.sh.
#   sh tools/emu_setup.sh            then:   python3 -m http.server -d build/emu 8000
set -e
cd "$(dirname "$0")/.."
echo "== 1/4 Python packages"
pip3 install Pillow fonttools 2>/dev/null || pip3 install --user Pillow fonttools || pip3 install --break-system-packages Pillow fonttools
echo "== 2/4 Emscripten (a few minutes the first time)"
if ! command -v emcc >/dev/null 2>&1; then
    [ -d "$HOME/emsdk" ] || git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$HOME/emsdk"
    "$HOME/emsdk/emsdk" install latest
    "$HOME/emsdk/emsdk" activate latest
    # (emsdk_env.sh wants bash; construct_env prints the same settings for any shell)
    eval "$("$HOME/emsdk/emsdk" construct_env 2>/dev/null)"
fi
command -v emcc >/dev/null 2>&1 || { echo "emu_setup: Emscripten installed but emcc is not on the PATH; try: bash -c '. ~/emsdk/emsdk_env.sh && sh web/emu/build.sh'"; exit 1; }
echo "== 3/4 tables"
mkdir -p build/gen
python3 tools/gen_aa_font.py build/gen/ui_fonts.h --preset inter-tight
python3 tools/gen_aa_icons.py build/gen/ui_icons.h
python3 tools/gen_aa_keycaps.py build/gen/ui_keycaps.h
python3 tools/gen_ui_palettes.py build/gen/ui_palettes.h
python3 tools/gen_tables.py build/gen/felucca_tables.h
python3 tools/gen_fm6_patches.py build/gen/felucca_fm6.h
python3 tools/gen_samples.py build/gen/felucca_samples.h
echo "== 4/4 the emulator"
sh web/emu/build.sh
echo
echo "Done. Now run:   python3 -m http.server -d build/emu 8000"
