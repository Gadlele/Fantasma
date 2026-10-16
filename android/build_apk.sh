#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Fantasma for Android: the browser emulator inside a WebView app.
#   sh tools/emu_setup.sh        (once: builds build/emu; needs Emscripten)
#   sh android/build_apk.sh      (needs the Android SDK, JDK 17 and Gradle)  ->  android/app/build/outputs/apk/debug/app-debug.apk
set -e
cd "$(dirname "$0")/.."
[ -f build/emu/felucca.wasm ] || { echo "run tools/emu_setup.sh first"; exit 1; }
rm -rf android/app/src/main/assets/emu
mkdir -p android/app/src/main/assets/emu
cp -r build/emu/. android/app/src/main/assets/emu/
cd android
if [ -x ./gradlew ]; then ./gradlew assembleDebug; else gradle assembleDebug; fi
echo "apk: android/app/build/outputs/apk/debug/app-debug.apk"
