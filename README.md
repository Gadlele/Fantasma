<p align="center">
  <img src="https://raw.githubusercontent.com/Gadlele/Fantasma/gh-pages/img/splash.gif" width="240" alt="Fantasma's power-on animation">
</p>

<h1 align="center">Fantasma</h1>

<p align="center">
  <b>A ghostly, ambient synth firmware for the M-VAVE FM-1</b><br>
  drones · bells · rain · Omnichord chords · in a neon skin
</p>

<p align="center">
  <a href="https://gadlele.github.io/Fantasma/install/"><b>Install</b></a> ·
  <a href="https://gadlele.github.io/Fantasma/try/"><b>Try it in your browser</b></a> ·
  <a href="https://gadlele.github.io/Fantasma/">Website</a> ·
  <a href="https://gadlele.github.io/Fantasma/firmware/Fantasma-1.1.fwsc">Download 1.1</a>
</p>

<p align="center">
  <img alt="version 1.1" src="https://img.shields.io/badge/version-1.1-3af3ff?style=flat-square&labelColor=000408">
  <img alt="FM-1" src="https://img.shields.io/badge/for-M--VAVE%20FM--1-ff3dd6?style=flat-square&labelColor=000408">
  <img alt="GPL-3.0" src="https://img.shields.io/badge/license-GPL--3.0-efffff?style=flat-square&labelColor=000408">
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/Gadlele/Fantasma/gh-pages/img/device.png" width="820" alt="The FM-1 running Fantasma">
</p>

**Fantasma** is custom firmware for the M-VAVE FM-1. It keeps everything
[Felucca](https://github.com/hugelton/Felucca) does (thirteen engines, a 64-step sequencer, songs, effects) and adds an
instrument for slow, warm, drifting music: a Pad that writes its own chords, bells that grow like a garden, an endless
noise of sea, rain or vinyl, and an Omnichord on the keys. In a neon skin, with a ghost.

## What's inside

| | |
| --- | --- |
| **The ambient machine** | SEQ › AMBIENT: OCT+ picks the sounds, a scale and a key, switches the Pad's progression and the bells on, and sets long envelopes and reverb. It keeps playing with the transport stopped. |
| **AMBI, the noise engine** | Sea, surf, storm, wind, stream, fire, vinyl, rain, cave drip, crickets. REL at its top means **INF**: a drone that never stops. |
| **BELL and the garden** | Soft FM bells that play themselves: four loops of 13, 17, 23 and 29 steps drifting apart; the keys choose the notes. |
| **A Pad that writes chords** | DENS, BARS, CHORDS, SEED: a progression of its own in the key, never the same twice. |
| **OMNI** | The black keys are chord buttons, the white keys strum. ENV makes it minor, LFO adds the seventh, OMNI H holds the chord. |
| **AMBI reverb** | Tails from 7 s up to about 90 s. |
| **Four parts** | PAD, LEAD, ECHOES and NOISE, each with its own colour and warm sounds made for it. |
| **The neon look** | The ECTO palette, an animated ghost at power-on, and one on the AMBIENT page. |
| **Tape sounds (1.1)** | NOSTALGIA, HAZY LEAD, TAPE EP, FADED CHOIR, MUSIC BOX and VHS HISS, with a slow wow on the pitch, like a worn cassette. [Hear every sound](https://gadlele.github.io/Fantasma/#sounds). |

## On the screen

<p align="center">
  <img src="https://raw.githubusercontent.com/Gadlele/Fantasma/gh-pages/img/seqpage4.png" width="200" alt="AMBIENT">
  <img src="https://raw.githubusercontent.com/Gadlele/Fantasma/gh-pages/img/home_ambient.png" width="200" alt="HOME">
  <img src="https://raw.githubusercontent.com/Gadlele/Fantasma/gh-pages/img/mixer.png" width="200" alt="MIXER">
  <img src="https://raw.githubusercontent.com/Gadlele/Fantasma/gh-pages/img/scl.png" width="200" alt="SCL">
  <br>
  <img src="https://raw.githubusercontent.com/Gadlele/Fantasma/gh-pages/img/edit.png" width="200" alt="EDIT">
  <img src="https://raw.githubusercontent.com/Gadlele/Fantasma/gh-pages/img/lfo.png" width="200" alt="LFO">
  <img src="https://raw.githubusercontent.com/Gadlele/Fantasma/gh-pages/img/seq.png" width="200" alt="SEQ">
  <img src="https://raw.githubusercontent.com/Gadlele/Fantasma/gh-pages/img/fx.png" width="200" alt="FX">
</p>

## Install

1. Open the **[web installer](https://gadlele.github.io/Fantasma/install/)** in Chrome or Edge on a computer.
2. Connect the FM-1 with a USB **data** cable, straight into the computer.
3. Follow the five steps: the checklist, a backup, the build, connect, install. About two minutes.

From a terminal instead: `python3 tools/fm1_install.py Fantasma-1.1.fwsc` (needs `pip install mido python-rtmidi`).

**If something goes wrong:** switch the FM-1 on while holding **OCT−**. It starts **FANTASMA USB RESCUE**, with
only USB, ready for a new install (it also starts by itself after two failed starts). The firmware never writes the
FM-1's boot code, and the installer can put the official M-VAVE firmware back at any time.

## The code

| | |
| --- | --- |
| `main` | the latest release, now **1.1** |
| `next` | the work on the next version |
| [Releases](https://github.com/Gadlele/Fantasma/releases) | every version: its notes and its package |
| [`gh-pages`](https://github.com/Gadlele/Fantasma/tree/gh-pages) | the website: project page, installer, emulator, package |

- [The manual](docs/MANUAL.md): every page, engine and control
- [What changed](CHANGELOG.md), version by version · [How a version is released](RELEASING.md)
- Building: [BUILDING.md](BUILDING.md) (`./build.sh --release 1.1`); the tests: `sh tests/run_tests.sh`

## Credits

Fantasma is by **Gadlele**. It is a fork of [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita
(@kurogedelic), Hügelton Instruments. The ambient machine follows Charles Vestal's
[AMB-1](https://github.com/charlesvestal/fm1-amb), OMNI follows his [FoMni](https://github.com/charlesvestal/fm1-fomni),
and the USB rescue follows zvenson's [dinghy](https://github.com/zvenson/dinghy). See
[LICENSING.md](LICENSING.md) for every bundled work and its licence.

Fantasma is free software under the [GPL-3.0](LICENSE), with no warranty. M-VAVE and FM-1 are trademarks of their
respective owners; Fantasma is independent and not affiliated with, endorsed by or supported by them.
