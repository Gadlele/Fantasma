#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Every factory sound as a short MP3 for the website's Sounds section (tools/render_sounds.c, then ffmpeg):
#   tools/render_sounds.sh OUT     ->  OUT/sounds.json, OUT/E_K.mp3      (after ./build.sh: build/gen)
# web/site/make_pages.py PACKAGE EMU OUT_SITE OUT  puts them on the site.
set -e
cd "$(dirname "$0")/.."
OUT=${1:?usage: tools/render_sounds.sh OUT}
mkdir -p "$OUT" build/host
${CC:-cc} -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/render_sounds tools/render_sounds.c -lm
build/host/render_sounds "$OUT"
for f in "$OUT"/*.wav; do
    ffmpeg -loglevel error -y -i "$f" -codec:a libmp3lame -b:a 96k "${f%.wav}.mp3"
    rm "$f"
done
echo "sounds: $OUT ($(ls "$OUT"/*.mp3 | wc -l) clips, $(du -sh "$OUT" | cut -f1))"
