# Changelog

Fantasma's versions, newest first. The numbers: **X.Y** adds something (saved music still opens), **X.Y.Z** only
fixes, a new **X** may change how things are saved.

## 1.1 — 2026-10-16

Tape sounds, in the manner of Boards of Canada:

- **WOW**: a preset can now set the LFO for a slow, free wobble of the pitch, a worn tape that slows down and
  speeds up again, and its SPREAD and GLIDE. The presets before 1.1 do not use them and sound as they did.
- **Six new sounds** with it, at the end of their engine's list (the numbers of the others do not change):
  **NOSTALGIA** (ANALOG, a detuned pad; on the Pad too), **HAZY LEAD** (ANALOG, a mono triangle with a slow delay),
  **TAPE EP** (FM6, the tine piano with a lot of chorus), **FADED CHOIR** (VOICE; on the Pad too), **MUSIC BOX**
  (BELL, a toy bell drifting in pitch) and **VHS HISS** (AMBI, the hiss and crackle of a running tape).
- **The website plays every sound**: the Sounds section plays each of the 129 factory sounds in a small piece,
  rendered by the firmware itself.

## 1.0 — 2026-10-16

The first release. Fantasma is [Felucca](https://github.com/hugelton/Felucca) 1.5.1 with:

- **The ambient machine** (SEQ › AMBIENT): OCT+ draws a whole scene at random, the Pad's progression and Echoes'
  bells switched on; the generators play with the transport stopped.
- **AMBI**, an ambient noise engine (engine 3, in LOFI's place): sea, surf, storm, wind, stream, fire, vinyl, rain,
  cave drip, crickets; REL at its top means INF, a drone that never stops.
- **BELL**, soft two-operator FM bells with a generator (engine 13, in SLICE's place), and the garden.
- **The Pad's chord progression**: DENS, BARS, CHORDS, SEED.
- **OMNI** and **OMNI H**: an Omnichord on the keys.
- **The AMBI reverb**: tails up to about 90 s.
- **Four parts**: PAD, LEAD, ECHOES and NOISE, with a colour and warm sounds each.
- **The ECTO look**, an animated power-on ghost.
- **USB rescue**: OCT− held at power-on, or after two failed starts.
- Projects and user presets saved by Felucca open (their LOFI / SLICE sounds become the default AMBI / BELL ones).
