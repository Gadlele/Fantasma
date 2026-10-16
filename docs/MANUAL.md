# Fantasma manual

Fantasma is a fork of [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (Hügelton Instruments). This
manual is Felucca's, with what Fantasma adds or changes marked **(Fantasma)**. Installing: [the web
installer](https://gadlele.github.io/Fantasma/install/). Building: [BUILDING.md](../BUILDING.md).

## Features

- **Thirteen engines** (below), each with its own factory presets; every factory sound has a category (BASS, LEAD,
  PAD, PLUCK, KEYS, DRUM, FX, OTHER) to browse by
- **Four tracks**, one synth part each with its own engine and sound (drums are the DRUM engine);
  8 voices shared between them. ALGORITHM selects the track on every page
- **Sequencer:** 64 steps per track with chords, ties, accent, slide, per-step chance, velocity and ratchets
  (a step played 2, 3 or 4 times in its length); a piano roll of the steps, where KNOB 3 sets a note's length; a drum grid (white keys =
  steps, black keys = lanes); **SEQ > DETAIL**: the cursor step's chance, ratchet, nudge and velocity on the four
  knobs, drawn as lanes over the steps; automation: recording of knob moves per step (an automation icon marks the cards it
  drives); live loop recording with overdub; step recording at the cursor (REC on SEQ > STEP while stopped), one
  chord at a time or note by note (MENU > CHORD ENTRY ADD); divisions listed by length, 4 bars to 1/32; loading a
  sound never touches your patterns
- **Timing:** live recording keeps when you played each note, as a per-step **NUDGE** (1/16 of a step, half a step
  early to 7/16 late); the track's **QUANTIZE** decides what plays: ON (the default) every step on the grid, as
  always, OFF your own timing. Nudges can also be set by hand
- **Parameter locks and the AUTOMATION list:** hold a step on SEQ > STEP (or the drum grid) and turn a knob to set
  that parameter for that step only; EDIT with the step held clears its locks. **SEQ > AUTOMATION** is one list of
  everything that happens on the track's steps: PLAY / CLEAR, QUANTIZE, each step's CHANCE, RATCH, NUDGE and VEL, its
  locks and automation events; change their step, parameter or value, turn a lock into automation or back, add or
  delete one. 128 records shared by the four tracks. SAVE held undoes every edit
- **SEQ TOOLS:** hold SEQ on any page for tools on the keys: CLEAR, REVERSE, SHIFT < / >, **RANDOM** (a new beat,
  or new notes in the scale on the same rhythm) and **COOK** (changes the pattern a little each press); on a DRUM
  track also BEAT and each lane's CLEAR, REVERSE, FILL and RANDOM; KNOB 1–4 are LEN, DIV, SWING and GATE. Each tool
  is one undo (SAVE held); OCT− puts the track back as the layer found it
- **Metronome and count-in:** CLICK (OFF, while recording, or always) with three levels, and a count-in
  of 1 or 2 bars before recording from stop; the click goes to the headphones and speaker only, never to
  USB audio or into a pattern
- **Songs** (GLO > SONG): up to 16 sections, each naming the project slot (A–D) **every track** plays, or "-" for a
  track that rests there, with its repeats; drawn as a timeline with a lane per track
- **Chord keys:** one finger plays an in-key chord (triads or sevenths of the scale, or fixed chord
  shapes), with voicings; on the keys, MIDI in, recording and the arpeggiator
- **Arpeggiator** with UP, DN, UPDN, RND, ORD (the notes in the order you pressed them), REPEAT and, for chords,
  DNUP, UP+8, CONV, DIVG, PINKY, THUMB, WALK and CHORD; a beat LED; 16 scales with a white-key mode, glide,
  MONO / LEGATO / UNISON
- **LFO:** RATE or **SYNC** to a note value (4 bars to 1/32, following the tempo or an external clock), **TRIG** NOTE
  or FREE, **POL** bipolar or unipolar (LFO > LFO 2)
- **Modulation matrix:** 4 slots per track, MIDI controllers as sources, **S&H** and **SLEW** (a random value per
  LFO cycle, held or gliding), and **DEPTH** as a destination, so the matrix can modulate the LFO itself
- **SPREAD** (EDIT > VOICE 3): the voices of a POLY or UNISON track alternate left and right of its PAN
- **ENV SYNC** (ENV > ENV DEST): the envelope's attack, decay and release as note values of the tempo
- **Effects:** distortion, an **INSERT** (drives, CRUSH, phaser, flanger, chorus, with a dry / wet MIX) and the
  SLICER per track; chorus, delay (in time with the tempo, dotted values included) and reverb sends (the reverb as
  **HALL**, ROOM or SPRING; HALL is a smooth stereo reverb; **AMBI** (Fantasma) is
  HALL with tails of 7 s at SIZE 0 up to about 90 s at 127, for pads and ambience); Fantasma's default on a fresh start, with SIZE 100 and DAMP 0; master limiter
- **FX layer:** hold FX for repeat, reverse, filter sweeps, tape stop, freeze, a harmonizer
  (OCT UP / OCT DN with shimmer), **FLANGER** and **PHASER** (in time with the tempo), **REV THROW** and **ECHO THROW**
  (every track's reverb or delay send full while held), and mutes on the black keys;
  any white key can hold any effect (hold the key in the layer and turn PRESETS); MENU > FX LATCH makes them toggle,
  so nothing has to stay held
- **MOMENTARY:** hold LFO and turn a knob on HOME or a sound / FX page; let go of LFO and the value jumps back
  (OCT+ while holding keeps it)
- **Quick layers:** hold FX, GLO, SCL, EDIT, REC or SEQ for shortcuts on the keys and knobs, or double-tap it to
  keep the layer open; one-step undo (SAVE held); REC on every page; OCT+ confirms, OCT- goes back; what can be
  pressed breathes softly instead of blinking
- **The ambient machine** (Fantasma): SEQ has a fourth page, **AMBIENT** (STEP, DETAIL, AUTOMATION, AMBIENT). **OCT+** draws the **Pad**,
**Echoes** and the **Noise** anew: one random ROOT and one soft SCALE (MAJ, MIN, DOR, MIX, PEN, MPEN, PHRY, LYD) for all three, an engine
and a sound at random for each among those they may use (never a kit), the Pad's progression (DENS, BARS, CHORDS, SEED) and Echoes' bells
(DENS, EVOLVE, RANGE, SEED) switched on, a slow ATK and a long REL on the Pad, reverb on both and delay on Echoes. The Noise gets an AMBI
sound with REL at its top and one note of the scale, which goes on for ever. The **Lead** gets the same ROOT and SCALE (only those: you can play
along on it); its sound, the BPM and the global reverb are yours: not touched. **Nothing starts the transport**: the generators have their own clock (below). Press again for another machine; **OCT-** is
**STOP**: the Pad's and Echoes' DENS to 0, the Noise's REL to 64 and its note let go (the transport and the Lead are not touched; HOME
leaves the page).
- **The generators' own clock** (Fantasma): the Pad's progression and Echoes' bells run **with the transport stopped** too, one step every
DIV at the project's BPM, from the moment DENS is above 0 and until it is back to 0 (what they hold then lets go). PLAY takes over as
before: the generators start again with it. With an external clock and the transport stopped they run at the BPM of the project.
- **The Pad's GEN page** (Fantasma): **EDIT > GEN** (after FILTER on ANALOG, before VOICE; not on a kit) holds the Pad's generator:
**DENS**, **BARS**, **CHORDS** (how many chords to choose among, 1 to 8: I IV V vi, then ii, iii, vii° and as eighth the bVII, each rarer than the last; default 4; BARS can be RND for a random 1 to 6 bars) and **SEED** (the same one is
the same progression). CHORD (SCL) keeps CHRD and VOIC.

**The Noise that never stops** (Fantasma): on the AMBI engine, **REL at its top (127) means infinite**, shown as **INF**: after the note off the
noise goes on, with the transport stopped too, until REL is turned down (it then fades with that release). A tap shorter than
the attack still ends in a drone at the SUS level (the envelope goes on rising), and a new note is crossed into: the old one fades out
as the new one rises, with no hole between. Nothing starts by itself at power-on (the Noise's steps are empty too): play a note once, or use the
ambient machine. All AMBI sounds have REL 64 (half), so they let go as before until you turn it up. The ambient machine (and any sound you pick on the Noise part) puts the Noise at INF and keeps it sounding: if the drone is ever lost it is struck again, so the Noise is always on, through preset changes too.

**Sounds per part** (Fantasma): browsing follows the part. **Echoes** offers the BELL sounds only, **Noise** the AMBI
  engine only (the older NOISE engine is no longer offered; ARCADE and METAL went with it), **Lead** everything else, and the **Pad** only the engines that make pads: ANALOG, PHASE, VOICE,
  WHEEL, PHYS and FM6 (no GRAIN, SAMPLE, TRIO or DRUM), of them 33 pad-like sounds, all polyphonic (the ambient machine picks
  from those too). This holds for the PRESETS list and its ENG knob and
  for the EDIT layer; a project, a user preset or MIDI can still put another sound on a part.
- **Warm ambient sounds** (Fantasma): soft, slow presets for the three tonal parts. Pad: ANALOG WARM PAD, TAPE STR, DUSK PAD,
  SUN PAD, HUSH (a drone), WHEEL AIR ORGAN, VOICE SOFT CHOIR and BREATH, GRAIN WARM CLOUD and SLOW FROST, PHASE WARM STR (attacks of
  2-6 s, long releases). Lead: ANALOG SOFT LEAD, FLUTE, GLASS LEAD, MELLOW SAW. Pad: new CHAPEL PAD and STRING ORGAN (WHEEL), OOH CHOIR, MORPH CHOIR and ANGELS (VOICE), SLOW STRINGS FM (FM6; MIDI
  program numbers of FM6 stay as they were: F1..F8, then the bank). Echoes: the 15 BELL sounds (wooden: THUMB, TOY, HEARTH, AMBER, COPPER, SOFT TOLL; soft: SILK, COTTON, PETAL, HALO, DAWN,
  PRAYER, CLOISTER; bright but gentle: DEW, WIND CHIME). Add a little REV (the AMBI type for the longest tails).
- **Presets:** factory presets, 32 user preset slots and 4 projects, named on the device (an FM6
  sound keeps its own patch in both); user presets get a category, and a project can get a random name; projects
  from every earlier version load. The PRESETS knob changes the sound from every page that is not a list.
  Turn the FM-1 off and on and the music is as you left it (MENU > RESTORE LAST, an autosave of its own,
  written only when stopped and idle)
- **Screen:** flat UI with Inter Tight and Fukiai icons, FLAT or LINE style, 10 palettes including
  grayscale, black and white, high contrast and NIGHT; type in sizes by role (what you read in the larger size,
  labels and hints in the smaller); MENU > LARGE for tall knob cards with larger labels and values; the menu in
  five tabs. The header shows the track and the **project slot of its pattern** ("1 A", "1 A*" once edited), the
  play state and the tempo. HOME shows the oscilloscope or, with MENU > HOME TRACKS, **the four tracks** with their
  steps playing (LEVELS: with the four tracks' levels on the knobs); the screen can go dark when the panel is left
  alone (MENU > SCREEN OFF); MENU > HELP shows a short hint when a page or a layer opens
- **LEDs:** idle buttons and keys glow dim so the panel can be found in the dark (MENU > LEDS: OFF,
  DIM LO, DIM HI or INV, the official firmware's look); PLAY turns green while playing; the keys show
  the notes the sequencer and MIDI IN play, and on SEQ > STEP (stopped) the notes of the step under the cursor;
  the drum grid marks the beats (steps 1, 5, 9, 13); MENU > SCALE LEDS shows the track's scale on the keys;
  a soft light sweeps over the keys at power-on, and with MENU > ANIM IDLE drifts over them while the FM-1 waits
- **USB:** class-compliant MIDI in and out, and a stereo audio input ("Felucca") at 44.1 or 48 kHz
  (the computer picks) that records the master output on the computer, no driver needed (at a fixed level with MENU > USB LEVEL FIXED;
  on macOS 13–15, set MENU > USB SERIAL to OFF so the audio input appears)
- **MIDI:** USB and TRS MIDI in; MENU > MIDI > MIDI IN: CH1-4 (channels 1–4 play tracks 1–4), CH5-8, CH9-12 or
  CH13-16 (another block of four, e.g. for two FM-1s on one cable), or SEL (every channel plays the selected track);
  the keys send on the track's channel; pitch bend, sustain, panic; clock from internal, USB or TRS. MIDI CCs set
  track parameters: 5 GLIDE, 7 LEVEL, 10 PAN, 71 resonance, 72 / 73 / 75 release / attack / decay, 74 brightness,
  91 / 93 / 94 the reverb, chorus and delay sends. **MIDI LEARN** (GLO + D4) sets up to 16 more CCs of your
  controller to any knob's parameter of any track, on the device; **Program Change** loads a track's sound by its
  number in PRESETS
- **Web:** editor for every parameter (with a 6-operator FM patch editor that also sends a whole 32-voice bank),
  step grid, mixer, preset library, sample upload and recording with trim, the MENU settings; full backup and restore
  (everything but the FM6 voice bank: keep its .syx file);
  return to the official firmware; Felucca itself running in the browser

## Controls

![FM-1 controls with Felucca](controls.jpg)

- Tap a page button for its page, again for the next; HOME returns home. Page buttons act when let go
- Hold FX, GLO, SCL, EDIT, REC or SEQ for its quick layer; hold SAVE to undo, HOME for the menu; hold LFO and turn a
  knob for a MOMENTARY change
- Double-tap a layer's button to keep its layer open without holding; tap it again to close
- On SEQ > STEP, hold a step and turn KNOB 1–4 to lock those parameters on that step (PRESETS: its NUDGE)
- On action pages and in dialogs, OCT+ does it and OCT− goes back; in the menu, OCT+ / OCT− change the value and HOME closes it
- Save a sound: stop, tap SAVE, pick a slot with KNOB 1, then OCT+ and OCT+ again (name it with the keys)

The pages by button:

| Button | Taps go round | Held |
|---|---|---|
| HOME | HOME, MIXER, CLOCK (BPM, swing, clock source) | the menu |
| SEQ | STEP (the piano roll, or the drum grid), DETAIL, AUTOMATION | SEQ TOOLS, on every page |
| GLO | SONG | the GLO layer (mutes, solos, levels, TAP tempo, D4: MIDI LEARN) |
| SAVE | USER, PRESETS, PHRASES (the pattern loader), PROJECT, TOOLS | UNDO |
| LFO | LFO, LFO 2, LFO DEST, MOD | MOMENTARY (with a knob) |
| EDIT | EDIT 1, EDIT 2, VOICE, VOICE 2, VOICE 3 (FM6 adds OPERATOR, OP ENV, OPERATOR 2; DRUM LANES; SLICE SLICES; ANALOG FILTER) | the EDIT layer (the engines; on DRUM the black keys mute lanes) |
| FX, SCL, ENV, ARP | their pages (FX: INSERT and INSERT 2 after the SLICER) | FX and SCL: their layers |
| REC | arms the track | the REC layer (CLEAR, CLICK, COUNT-IN, CLICK LEVEL) |

## Menu

Hold **HOME** for the menu, in five tabs: **DISPLAY** (COLOR, STYLE, LARGE, ANIM, LEDS, SCREEN OFF, SCOPE, HOME),
**CONTROL** (HOLD, KNOB ACCEL, FX LATCH, BPM LOCK, SCALE LEDS, STEP PREVIEW, CHORD ENTRY), **AUDIO** (SPEAKER EQ,
USB LEVEL, CLICK, CLICK LEVEL, COUNT-IN, TUNE), **MIDI** (MIDI IN, LEARN CLEAR) and **SYSTEM** (USB SERIAL,
RESTORE LAST, HELP, HARDWARE CALIBRATION, INFO, ABOUT).
ALGORITHM moves between the tabs and PRESETS through the rows of one; any of KNOB 1–4, or OCT+ / OCT−,
changes the value (OCT+ opens HARDWARE CALIBRATION, INFO and ABOUT); press HOME to close the menu. Every new
setting defaults to the earlier behaviour. The web editor's Settings tab reads and changes them too, saved the same
way as from the menu, in the same tabs.

- **COLOR:** the palette. GREY (grayscale, called MONO up to 1.0.1), GREEN, AMBER, ICE, VIOLET, ROSE,
  PAPER (light), HI-CON (high contrast), NIGHT (the 0.9 look: true black, green-tinted text) and MONO
  (black and white)
- **STYLE:** FLAT (filled cards) or LINE (areas divided by thin lines)
- **LARGE:** OFF or ON: on HOME and the value pages the four knob cards grow tall, marked K1–K4, with
  larger labels and values about twice the size; the graph below becomes a strip. Lists, the piano roll,
  the drum grid and the quick layers keep their layout with larger labels
- **ANIM:** ON, OFF or IDLE. OFF shows every change at once, without rolling digits, a gliding piano roll or
  the LED sweep at power-on. IDLE is ON plus a gentle LED animation after a minute without input: a soft light drifts
  over the keys and the buttons breathe; any key, button or knob ends it and does what it always does. It never runs
  while playing, with a track armed, in a layer, the menu or a dialog
- **LEDS:** OFF (no glow), DIM LO, DIM HI (default) or INV (the idle LEDs lit, the active ones dark).
  What can be pressed breathes up to about 60 % of a lit LED, about 30 % with DIM LO
- **SCREEN OFF:** NEVER (default), 5 MIN, 15 MIN, 30 MIN or 60 MIN: after that long with no button, key or knob
  touched, the screen goes dark; the sound, the sequencer, MIDI and USB go on. The backlight stays on (on the FM-1
  its line also enables the buttons and keys). The next button, key or knob only turns the screen back on
- **SCOPE:** OUT (default) or MIX: what HOME's oscilloscope shows. OUT the sound after MASTER (the picture shrinks
  with the volume), MIX the mix before MASTER, the same size at any volume
- **HOME:** SCOPE (default), TRACKS or LEVELS: under HOME's four cards, the oscilloscope or the four tracks, a row
  each with its sound, its steps playing, MUTE and a meter. With TRACKS only the look changes; with LEVELS KNOB 1–4
  set the LEVEL of T1–T4 (the engine's main parameters are on EDIT)
- **HOLD:** how long a layer's button is held before its map shows
- **KNOB ACCEL:** OFF (one step per click) or ON: a fast, steady turn of a wide value moves 2 to 4 steps
  per click, up to 8 on values of more than 64 steps, the FX and GLO layers' knobs included; lists never
  jump. ON or OFF, every click of a fast turn counts
- **FX LATCH:** ON, FX + an effect key turns the effect on until pressed again, the knob macros stay
  where you leave them, and FX + OCT− turns everything off
- **BPM LOCK:** ON, SELECT no longer changes the tempo; hold GLO and turn SELECT, tap F4 in the GLO
  layer or use HOME > CLOCK instead
- **SCALE LEDS:** OFF or ON: the keys show the selected track's scale all the time (the root a little
  brighter); not on DRUM or SLICE tracks
- **STEP PREVIEW:** OFF (default) or ON: on SEQ > STEP with the transport stopped, moving the cursor plays the step
  it lands on once (its notes or hits, through the track's sound)
- **CHORD ENTRY:** HOLD (default) or ADD: how step recording writes. HOLD: the keys held together become the step,
  letting go moves on. ADD: each key adds its note to the step, up to 4 (a key whose note is there takes it out),
  and KNOB 1 moves on, so a chord goes in one finger at a time
- **SPEAKER EQ:** FLAT, LOWCUT or BASS+, a tone setting for the built-in speaker (not a switch). It also
  shapes the headphone out and USB audio, so keep it on FLAT when recording. The firmware cannot turn
  the speaker off, but headphones in the jack do
- **USB LEVEL:** MASTER (the MASTER knob sets the USB audio level too) or FIXED (USB always at full
  level, MASTER sets only the speaker and headphones)
- **CLICK:** OFF (default), REC (while a track is armed) or ON: a click on every beat while playing, the
  bar's first beat higher. **CLICK LEVEL:** LOW, MID (default) or HIGH; MASTER sets it too
- **COUNT-IN:** OFF (default), 1 BAR or 2 BARS: with a track armed, PLAY counts in first (clicking even with
  CLICK OFF), and a note played in the last half beat lands on step 1. Not with an external clock
- **TUNE:** the global tuning, −50 to +50 cents. It is the project's, saved and loaded with it
- **MIDI IN:** CH1-4 (default), SEL, CH5-8, CH9-12 or CH13-16 (see MIDI above). The project's too
- **LEARN CLEAR:** how many CCs MIDI LEARN has set (NONE, 1 CC … 16 CC); OCT+ clears them all
- **USB SERIAL:** ON or OFF, applied when the menu closes (the FM-1 reconnects). OFF leaves out the
  serial console, a developer tool, so the FM-1 is a plain audio + MIDI device; this lets macOS 13–15
  see its USB audio input. MIDI, the editor and the installer work either way
- **RESTORE LAST:** ON (default) or OFF. ON, the FM-1 starts with the music you left (the header says
  RESTORED), kept in an autosave apart from the four projects; it is written when the music changed, the
  transport is stopped and nothing has been touched for 10 s, at most once a minute. OFF starts with the
  power-on sounds
- **HELP:** OFF (default) or ON: a short hint in the footer for about 2 s when a page opens, and under a quick
  layer's map while it shows
- **HARDWARE CALIBRATION**, **INFO** (the version, the USB link and the CPU load) and **ABOUT**

## Engines

In the order the device lists them:

- **ANALOG**: virtual analog; two oscillators (WAVE SYNC: the second hard-synced to the first; SUB: a square an
  octave down), noise, drive, a resonant filter: low-pass, band-pass or high-pass (EDIT > FILTER)
- **FM6**: classic 6-operator FM (Dexed-based): 32 algorithms, a full patch per track, edited in the
  web editor (which imports .syx files) or on the device: EDIT > OPERATOR (ratio or fixed frequency, fine, level),
  OP ENV (each stage's rate and level, drawn as the envelope) and OPERATOR 2 (mode, detune, velocity), one
  operator at a time on the algorithm chart; macros on top; SLOT picks a factory patch (F1–F8), the track's own (OWN) or a voice of the bank the web editor sent (B1–B32)
- **PHASE**: phase distortion (ported from CrispyZebra)
- **AMBI** (Fantasma, engine 3, where LOFI was; `FELUCCA_LOFI=1` builds LOFI instead): an ambient noise generator, a
  bed of white / pink / brown noise through a resonant filter with slow random swells (SWELL, RATE), and random pops
  (CRKL, POP, BAL): sea, wind, rain, fire, vinyl, crickets
- **SAMPLE**: multisampled instruments (PIANO, FLUTE, SAX) and 3 user sample slots
- **VOICE**: formant oscillator, sung vowels
- **TRIO**: 3 oscillators with ring modulation and sync, multimode filter
- **WHEEL**: tonewheel-style organ; drawbar registrations, percussion, key click, drive, rotary speaker
- **GRAIN**: granular textures from the built-in samples or a user slot
- **PHYS**: physical models: modal resonators, strings, struck membranes, sympathetic strings
- **NOISE**: noise from analog to digital: colours, crackle, shift-register and metallic tones
- **BELL** (Fantasma, engine 13, where SLICE was; `FELUCCA_SLICE=1` builds SLICE instead): a soft two-operator FM bell
  (RATIO, TONE, ENV, DTN). Every bell starts with DENS 0, so a key plays the preset; turn DENS up (Echoes: KNOB 1) and it plays itself: four loops of 13, 17, 23 and 29 steps that drift against
  each other, snapped to the track's ROOT and SCALE (EVOLVE, RANGE, SEED)
- **SLICE**: a drum break, a piano note (both built in) or your own sample cut into slices, one per key;
  set the slices by hand on the SLICES page
- **DRUM**: an 8-lane kit of Felucca's own drum voices on the General MIDI key map. KIT picks the
  standard kit or one of five virtual-analog (VA) kits in the manner of
  classic analog drum machines (the numbers are a hint), every lane its own voice: **80** (deep sine kick with a long decay, noisy snare,
  six-square hats), **10** (swept kick, white-noise snare, long-tailed claps, a cymbal), **66** (soft
  round kick, bright snare, noise hats, a conga), **55** (dropping kick, high metal hats, a metal bell)
  and **77** (swelling kick, multi-burst claps, claves, a cymbal). All of them are synthesized by
  Felucca, no samples. EDIT > LANES and LANES 2 set each lane's level

The DIGITAL engine of 0.9 has been replaced by FM6: projects and presets with DIGITAL sounds load
as FM6 sounds converted from them. The SAMPLE engine's PERC kit was removed in 1.0.2: sounds and
projects that used it load as the DRUM engine's kit, on the same key map. FM6's patch bank (the B
slots) was removed in 1.0.3: user presets keep their own FM6 patch, and presets that used a B slot get
that patch on the first start of 1.0.3. 1.4.1 brings B slots back as a voice bank sent from the web editor
(B1–B32): a voice picked from it becomes the track's own patch, so nothing saved depends on the bank. SLICE gained a second built-in sound in 1.0.4, PIANO (the SAMPLE
engine's middle C as it was then), next to BREAK. Since 1.0.4 a missing sample (an empty user slot, or a set missing
from the build) plays a plain sine at the note's pitch on SAMPLE, GRAIN and SLICE, and the screen says
NO SAMPLE once. DRUM's KIT variants HAND, CYM and H+CYM were retired in 1.0.5: sounds and projects that
used them play the 66, 10 and 77 kits. SAMPLE's PIANO was a lo-fi piano in 1.4 and 1.4.1 (the full one installed into a user slot from the web editor as
PIANO HD); 1.5 brings the full piano back.

**SLICER** (FX page, every track): a tempo-synced 16-step gate or stutter, with 16 patterns.

**Reverb** (FX > REVERB, TYPE): **HALL** (since 1.4, the default on a fresh start: eight delay lines feeding each
other with slowly drifting taps, stereo, SIZE the decay up to about 4.5 s, DAMP how fast the highs die), **ROOM** (the
default up to 1.1.5) and **SPRING**. A project plays the TYPE it was saved with.

## Scale keyboard

On the **SCL** page, set **QNT** to WHITE to play the selected scale using only the
white keys (SNAP keeps every key and rounds it down to the scale). C4 plays **ROOT**; consecutive white keys play consecutive scale notes
above and below it. Black keys are silent, including during live recording and
step entry. **TRN** transposes the resulting notes; the octave buttons shift them
by full octaves. Set QNT to OFF for the normal chromatic keyboard. QNT SEQ snaps the keys like SNAP and
also maps the sequenced notes onto the current ROOT / SCALE as they play (the steps are not changed; drum
kits are never quantized).

Available scales: chromatic (CHR), major (MAJ), natural minor (MIN), Dorian (DOR),
Mixolydian (MIX), major pentatonic (PEN), minor pentatonic (MPEN), harmonic minor
(HARM), Phrygian (PHRY), Lydian (LYD), Locrian (LOC), ascending melodic minor (MEL),
minor blues (BLUES), whole tone (WHOLE), half-whole diminished (DIMHW), and
whole-half diminished (DIMWH). Scales with other than seven notes continue across
the white keys without repeating notes; their roots need not fall on every C key.
Drum kits and incoming MIDI keep their own note mapping.

Press **SCL** again for the **CHORD** page: CHRD picks the chord keys (OFF, the scale's triads or
sevenths, or a fixed shape) and VOIC the voicing.

**Parts** (Fantasma): the four tracks are named for what they play, **PAD, LEAD, ECHOES, NOISE**: HOME's footer shows the
selected track's part, and the tracks' rows lead with it ("PAD / SOFT PAD"). HOME's four knobs follow the part: the **PAD**
(track 1) has its sound's first knob, **ATK** (the swell in), **REL** (the tail out) and **REV** (the space); **ECHOES**
(track 3, the BELL engine) has **DENS** and **EVOLVE** of the generator, then the **SCL** (the scale the bells are drawn from, on the track's ROOT, which is set on the SCL page: MAJ, MIN, DOR, PHRY, PEN ..; another scale starts the garden again from it) and the **REV** send (the delay is on its FX page); LEAD and NOISE keep
their engine's own four. At power-on: the PAD is FM6 PAD (CHRD OFF: OMNI is one turn of SCL > CHORD away), the LEAD ANALOG SAW LEAD, ECHOES the bells,
NOISE the AMBI sea, its steps empty (play a key, or use the ambient machine; REL at its top makes it endless).

**A colour for each part** (Fantasma): the screen's theme colour follows the selected track: the palette's own for the PAD, its hue
turned for the LEAD (warm), ECHOES (green) and NOISE (violet), whichever palette is chosen (the background, the text and the
accent stay). GREY and MONO have no hue and stay as they are.

**The garden** (Fantasma, after [AMB-1](https://github.com/charlesvestal/fm1-amb)): on **ECHOES** (the BELL engine with DENS above 0)
the keys do not play, each one turns its note on or off for the bells to be drawn from. The first key starts from the
track's ROOT / SCALE and turns that note off; the lit keys are the garden (always, whatever MENU > SCALE LEDS says). With every
note off the scale is the garden again. MIDI IN follows it too. With DENS 0 the keys play as ever. The garden is not saved.

**OMNI** (Fantasma, after [FoMni](https://github.com/charlesvestal/fm1-fomni)), the last CHRD value, on the Pad and the Lead (every engine but a drum kit): the
keys become an Omnichord. The 11 black keys are chord buttons (F C G Dm Am Em G7 E7 D7 Bb A7 in C, following the
track's ROOT), each sounding its chord, in the VOIC voicing, while held. The 16 white keys are the strum plate: one
note of the current chord each, root, third, fifth and again an octave up. OFF is the classic keyboard; MIDI IN
always plays single notes.

**The Pad plays itself** (Fantasma): SCL > CHORD has two more knobs, **DENS** and **BARS**. With DENS above 0 the Pad stops reading
its steps and plays chords by itself, the diatonic triads of its ROOT (**I, IV, V, vi** and, with CHORDS higher, ii, iii, vii° and bVII) and SCALE (in the VOIC voicing; in a
minor scale they are i, iv, v and VI), with I the most often and the same chord seldom twice. A chord is struck once and held for
BARS bars of 16 steps (at the track's DIV), so a slow ATK swells and a long REL carries it into the next one. DENS is the chance
that a bar group has its chord: 100 is a chord all the time, lower leaves silences where the reverb rings. DENS starts at 0, so
nothing changes until you turn it; the first chord after a start always sounds. The keys still play over it as ever.

OMNI also has FoMni's modifiers: **ENV held** makes the button's chord minor, **LFO held** adds the seventh (C7 from a major
button, Cm7 from a minor one), both together a minor seventh; ENV and LFO then open no page when let go (they do alone). The
white keys follow the chord as played. The CHRD value after OMNI, **OMNI H**, is OMNI with a **chord hold**: the chord of a
button rings on after the button is let go, until the next button (or back to OMNI) ends it; the white keys pluck over it.

## Layout

| Path | What |
| --- | --- |
| `firmware/` | firmware sources: `src/` app, `hal/` hardware layer, `loader/` update loader |
| `tools/` | build script, generators, package maker, installer and sample uploader |
| `assets/` | UI font, icon names, CC0 instrument samples |
| `web/` | the web installer, the earlier editor and the browser emulator (`web/emu/`); the new editor: [Felucca-WebApp](https://github.com/hugelton/Felucca-WebApp) |
| `tests/` | tests that run on the build machine |
| `LICENSES/` | licence texts of the bundled fonts, icons, ported DSP and SDK files |

## If the FM-1 does not start

**USB rescue** (after [dinghy](https://github.com/zvenson/dinghy), zvenson): hold **OCT-** alone while
powering on, or let two boots in a row die in their first 30 s, and the FM-1 starts the rescue screen
"FANTASMA USB RESCUE": no audio, no UI, no settings, only USB and the updater. It answers as the stock firmware
(FM-1_000), so the web installer or the FM-1 Firmware Hub can install any package. If the rescue itself crashes,
the next boot goes to the chip's UBOOT (below). A cold power-on clears the failed-boot count.

If an update is interrupted and the FM-1 stays black, check whether a computer sees it as a USB device named
**WL80UBOOT** (or a USB mass-storage device with ID 4C4A:8057). That is the chip's built-in boot mode, and
the FM-1 can be brought back:

- First try another USB data cable, and close every other app that uses MIDI, then run the web installer again.
- If it stays in boot mode, [FM-1 Transporter](https://github.com/kurogedelic/FM-1-transporter) reads and
  writes the FM-1's flash from a Mac through a Seeed XIAO RP2040 (three wires to the FM-1's USB lines). Back up
  the flash first, then write the official firmware (M-VAVE's FM-1.fwsc).
- Questions: [Issues](https://github.com/Gadlele/Fantasma/issues).

## Support

Bugs and ideas: [Issues](https://github.com/Gadlele/Fantasma/issues). Fantasma stands on Felucca: to support
Felucca's author, see [Felucca](https://github.com/hugelton/Felucca) ([sponsoring on GitHub](https://github.com/sponsors/hugelton),
[itch.io](https://hugelton.itch.io/felucca)).

## AI disclaimer

Fantasma, like Felucca, is developed with the assistance of AI coding agents. These tools are used for coding, testing, documentation, translation, and maintenance.
The instrument's design, features, sound design, and overall direction are determined by the maintainer or community.
No generative AI is used to create music, icons, or visual artwork for this project.
For more details, see [On AI-Assisted Development and Responsibility](https://github.com/hugelton/Felucca/discussions/166).

## Credits

- **Fantasma** by Gadlele: the ambient machine (after Charles Vestal's [AMB-1](https://github.com/charlesvestal/fm1-amb)), OMNI (after his [FoMni](https://github.com/charlesvestal/fm1-fomni)), the AMBI and BELL engines, the parts, the ECTO look; the USB rescue after zvenson's [dinghy](https://github.com/zvenson/dinghy)
- **[Hügelton Instruments](https://hugelton.com)** (Leo Kuroshita, [@kurogedelic](https://github.com/kurogedelic)):
  Felucca itself; the PHASE engine's waveforms (a C port of the oscillator of
  [CrispyZebra](https://github.com/hugelton/CrispyZebra), GPL-3.0); the DRUM voices and kits; the Hügelton Sample
  Pack (the drum samples, GPL-3.0-only, not CC0); the [Fukiai](https://github.com/hugelton/Fukiai) icon
  font ([MIT](../LICENSES/MIT-Fukiai.txt))
- Fonts: [Inter Tight](https://github.com/rsms/inter-tight) by The Inter Project Authors, [SIL OFL 1.1](../LICENSES/OFL-InterTight.txt);
  the browser emulator's labels: [DotGothic16](https://github.com/fontworks-fonts/DotGothic16) by The DotGothic16 Project Authors, [SIL OFL 1.1](../LICENSES/OFL-DotGothic16.txt)
- Samples: [Versilian Studios](https://versilian-studios.com/) [VSCO-2 Community Edition](https://github.com/sgossner/VSCO-2-CE) and [VCSL](https://github.com/sgossner/VCSL), CC0 1.0: the SAMPLE sets, also SLICE's PIANO and PIANO HD ([attribution](../assets/samples-cc0/ATTRIBUTION.txt))
- VOICE engine: after [klattsch](https://github.com/tgies/klattsch) by Tony Gies (MIT); formant data from Klatt (1980) and Hillenbrand et al. (1995)
- PHYS engine: models ported from [DaisySP](https://github.com/electro-smith/DaisySP) by Electrosmith and Emilie Gillet ([MIT](../LICENSES/MIT-DaisySP.txt)) and from Emilie Gillet's [eurorack](https://github.com/pichenettes/eurorack) code ([MIT](../LICENSES/MIT-Rings.txt))
- FM6 engine: msfa from [Dexed](https://github.com/asb2m10/dexed) by Google Inc. and Pascal Gauthier ([Apache-2.0](../LICENSES/Apache-2.0-msfa.txt))
- Browser emulator: after [X0X](https://github.com/charlesvestal/fm1-x0x) by [charlesvestal](https://github.com/charlesvestal) (GPL-3.0), a Felucca fork whose browser build showed the way
- Package format and boot files: [JieLi AC79 SDK](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK) ([Apache-2.0](../LICENSES/Apache-2.0.txt); three of its files are in every package, none in this tree)
- Contributions: [keremimo](https://github.com/keremimo) (white-key scales, #2), [ChanceTheMaker](https://github.com/ChanceTheMaker)
  (TRS MIDI, bend, sustain and clock, palettes, favourites, editor display settings: #8, #10, #11, #12),
  [andreahaku](https://github.com/andreahaku) (sample recording and trim, #29; SLICE manual slices and tests, #27, #22),
  [spinkham](https://github.com/spinkham) (the boot fix for 0.9 projects, #111),
  [zednaked](https://github.com/zednaked) (ratchets, #100),
  [jasonpersinger](https://github.com/jasonpersinger) (the ROOM reverb click fix, #121),
  [renebohne](https://github.com/renebohne) (MIDI Program Change, #179)

## Licence

Free software: [GPL-3.0-only](../LICENSE), the Hügelton Sample Pack included. The bundled fonts and the
ported DSP keep their own licences ([LICENSES/](../LICENSES/)); details in [LICENSING.md](../LICENSING.md).

M-VAVE and FM-1 are trademarks of their respective owners. Felucca is not affiliated with or endorsed by them.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments

## Android app

`android/` is a small Android app (a WebView around the browser emulator in `web/emu/`, so the firmware plays on a tablet, no FM-1 needed;
Web MIDI is not available in a WebView). The APK is built by the **Android APK** GitHub Action (Actions tab > Android APK > Run workflow;
download `fantasma-debug-apk` from the run), or locally with `sh tools/emu_setup.sh && sh android/build_apk.sh` (Android SDK, JDK 17, Gradle).
Install the debug APK by hand (allow installs from your browser/files app). Not tested on a device yet.
