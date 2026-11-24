# Changelog

Fantasma's versions, newest first. The numbers: **X.Y** adds something (saved music still opens), **X.Y.Z** only
fixes, a new **X** may change how things are saved.

## 2.0 — in progress

- **The pages, arranged by what they touch**: **GLO** holds everything global, in a round: SONG, CLOCK (BPM, swing,
  clock), AMBI, AMBI 2, **FX MODE** (DLY and REV: CHAN or BUS), the bus DLY, REVERB and CHORUS. **FX** holds the
  selected channel's effects only: the sends, SLICER, INSERT, INSERT 2, CH DELAY, CH REVERB. **EDIT** the channel's
  sound, as before; **HOME** goes HOME › MIXER.
- **The channels' reverbs, cleaner**: the rasp is gone. SHIMMER's octave was made of 23 ms slices joined with a
  corner, a 43 Hz roughness on everything it fed (at the DRONE's SHIMMR 20 the reverb's tail was 40 % noise between
  the notes); now 35 ms slices with soft joins (about 5 %). The two swaying lines move smoothly instead of a sample at
  a time (no tiny ticks). Four times the level inside the reverb (the same out): a dying tail fades smoothly instead
  of turning grainy and stopping short. SHIMMER gives way as the reverb fills: at a big SIZE it no longer piles up
  until the lines saturate.
- **NIAGARA on DRONE**: [Niagara Drift](https://github.com/evanyerburgh/niagara-drift)'s drones (Evan Yerburgh, GPL-3.0),
  their own code, as the DRONE channel's engine, played with the keys and the channel's ENV: two drones, partner, sub,
  noise, their filters with drive, the ULTRA LFO and its taps, the PULSE, Drift, Character, Age, Feedback, Wander,
  pitch link, sync, cross-mod, in stereo; Niagara Drift's 16 Presets as its sounds; its values on Fantasma's pages
  (EDIT, LFO, ENV) and saved with the project and a user preset. Niagara Drift's own effects are left out (no room):
  the channel's play instead. The first DRONE engine stays on the channel's list.
- **FILT ENV**: ENV's second page, the filter's own envelope (ATK DEC SUS REL): a filter that opens slowly on a note
  that starts at once, or the other way round. At =ENV (KNOB 1 all the way down, every sound's start) it follows ENV,
  so every sound, user preset and project sounds as before; any of its knobs starts it from ENV's values. Saved in the
  sound and the project; a project now holds 116 knob movements (122 before).
- No more "HOLD [..] QUICK" after a tap of a layer's button.
- **A CPU counter, always on screen**: at the right of the tabs, **C1** the first core's load and **C2** the second's
  (MENU › SYSTEM › 2ND CORE; `--` when it is off), refreshed twice a second, in the accent from 85 %.
- **SELECT steps the tabs** of the page shown, by default (MENU › CONTROL › SELECT PAGE): the tempo is GLO › CLOCK,
  GLO + SELECT or TAP. SELECT BPM gives the knob the tempo back.
- **Felucca 1.8.3 underneath** (Fantasma 1.x was Felucca 1.5.1): the second core (MENU › SYSTEM › 2ND CORE, beta,
  off by default), AFTERTOUCH from the keys' pressure, a custom palette, MIDI IN per track, step conditions,
  recording while playing and Felucca's other fixes since 1.5.1. Fantasma's sounds, AMBI, BELL, the generators,
  the palettes and the tempo from 30 BPM stay.
- Projects of Fantasma 1.x load; a project now keeps up to 122 knob movements (128 before: the six last of a
  full one are let go), to make room for the new parameters.
- **Four channels, one engine each**: **DRONE** (part 1: the DRONE engine, below; REL at its top is INF, a note
  that never stops), **PAD** (part 2: the GOLANA engine, below, with the chord progression),
  **BELLS** (part 3: the CHIME engine, below, and the bells generator) and **DRUMS** (part 4: DRUM).
- **AMBI is no longer a part**: HOME › AMBI and AMBI 2 switch an ambience on under the four channels (SCENE, TONE,
  MOTION, INTENS, LEVEL, REVERB), with five new scenes: THUNDER, BREATH, NIGHT, EMBERS, TAPE END. Saved with the
  project.
- **The DRONE engine**, DRONE's own (after [Niagara Drift](https://github.com/evanyerburgh/niagara-drift)'s drones,
  rewritten for Fantasma): a main oscillator that blends SINE > TRI > SAW > SQUARE (WAVE, the square's WIDTH), a
  partner at an INTERVAL (tempered or just) with its DETUNE, a SUB an octave to four below or a subharmonic (1/3, 1/5),
  MIX between them, a soft NOISE and DRIFT: the drone never quite still; and a FILTER page of its own (CUT, RES,
  KTR, TYPE LP / BP / HP). 12 sounds (DRONE, FIFTHS, OCTAVES, DEEP,
  HOLLOW, GLOW, TIDE, RUST, VESSEL, HUM, WIND HUM, SHADOW), LEGATO with a glide: a new key bends the drone there.
- **The GOLANA engine**, PAD's own: [Golana](https://github.com/Gadlele/Golana)'s engine made smaller for a channel.
  OSC 1 (SAW, PULSE, TRI, SINE, MORPH with CTRL1), OSC 2 (SAW, SQR, TRI, MIX; RING, SYNC; SEMI, TUNE), the SUB, the
  NOISE, the five filters (LPF24, LPF12, BPF, HPF, MS20) with CUT, RESO and KTR, DRIFT; its LFOs, filter EG,
  patch, arpeggiator and effects are the track's (CTRL2 is the LFO's SHP). Pages EDIT 1 (OSC), EDIT 2 (OSC 2), FILTER,
  TONE. 16 sounds: 10 pads (SOFT PAD, PHASE PAD, SYNC PAD, OLD STRINGS, AGED PAD, DARK UNISON, POLY CHORUS, WORN TAPE,
  80S BRASS, GLASS PAD) and 6 leads (GLIDE LEAD, WARM LEAD, DRIFT LEAD, SUPERSAW, MORPH LEAD, BREATH LEAD).
- **The CHIME engine**, BELLS' own: bells, glass and metal, after the LIVEN Ambient 0's Atmos layer. Two sines, a
  carrier and a modulator at a RATIO (1 .. 7.1, harmonic or metallic), as RING (ring modulation), FM (with FDBK) or
  both (R+FM), INDEX for how much; TONE page ENV (bright at the strike), DTN (a shimmer), BREATH (each voice swells
  slowly) and TONE (a soft low-pass); GEN is BELL's generator, as it was. 14 sounds: GLASS, CRYSTAL, ICE, BOWL,
  TEMPLE, GAMELAN, TUBULAR, SPARKLE, RAINDROP, STARLIGHT, DARK METAL, SHIMMER, TINE, RING MOD. BELL stays in the
  tree and its sounds still load, but BELLS plays CHIME.
- **The DRUMS' generator**, after [JIANT](https://github.com/juanjiant-bit/JIANT)'s EUCLID and MUTATE: two pages on a
  DRUM track. **EUCLID**: DENS (OFF, 1..8: a whole kit as euclidean rhythms, live), LEN (each channel its own, 1..64),
  ROT, LANE (ALL or one lane alone). **MUTATE**: EVERY (OFF, 1..16 loops), AMOUNT, MODE (ORBIT round the pattern
  dialled, DRIFT walking on), FEEL (accents on the beats, softer steps between). On both, OCT+ is **DICE** (a new
  pattern at random) and OCT- goes back to the pattern dialled; every rewrite one undo. A kit change keeps them.
- **Six ambient kits** for DRUM: SOFT KIT, DUST KIT, TAPE KIT, DUB KIT, WOOD KIT, CLICK KIT.
- **Each channel its own delay and reverb** (FX › CH DELAY, CH REVERB; the power-on's, **CHAN**): every track's DLY
  send goes to its own delay (1.5 s, dark as a tape's, a slow wow; TIME, FDBK, TONE) and its REV send to its own
  stereo reverb (SIZE from 0.3 s to a minute, 127 holds; DAMP; SHIMMR, the tail an octave up, on DRONE, PAD and
  BELLS; DRUMS a room). **BUS** keeps one delay and one reverb for all, as in 1.x: every project before 2.0 loads so
  and sounds as it did. The AMBI layer sends to the reverb its TO names (AMBI 2). DRIVE and MOD per channel are FX's
  DIST and the INSERT (SOFT HARD FOLD FUZZ CRUSH, PHASR FLANG CHOR), as before.
- **SEQ › DICE**, a page of its own: **WHO** (all four channels, one, or a few: DRN+PAD, PAD+BEL, NO DRUMS),
  **WHAT** (ALL, SOUND, NOTES, FX), **AMOUNT**, **KEY** (KEEP, or NEW: one new key for all four); DICE 2's **EVOLVE**
  rolls by itself every 1..16 bars. OCT+ rolls; OCT- is **BEFORE / NOW**, back and forth. SOUND keeps the generators,
  the sends and the key; NOTES moves the drone's note, the progression's and the bells' seeds, the drums' EUCLID; FX
  the sends, the channel's delay and reverb and now and then its INSERT.
- The ambient machine plays the four channels: a DRONE note at INF, PAD's progression, the bells, and the
  ambience on; OCT- STOP lets them all go.

## 1.1 — 2026-10-16

Tape sounds, in the manner of Boards of Canada:

- **WOW**: a preset can now set the LFO for a slow, free wobble of the pitch, a worn tape that slows down and
  speeds up again, and its SPREAD and GLIDE. The presets before 1.1 do not use them and sound as they did.
- **Six new sounds** with it, at the end of their engine's list (the numbers of the others do not change):
  **NOSTALGIA** (ANALOG, a detuned pad; on the Pad too), **HAZY LEAD** (ANALOG, a mono triangle with a slow delay),
  **TAPE EP** (FM6, the tine piano with a lot of chorus), **FADED CHOIR** (VOICE; on the Pad too), **MUSIC BOX**
  (BELL, a toy bell drifting in pitch) and **VHS HISS** (AMBI, the hiss and crackle of a running tape).
- **Sounds on the website**: eight short pieces, played by the firmware itself on the four tracks.

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
