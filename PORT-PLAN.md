# POLY5 on the M-VAVE FM-1: the port plan

Goal: turn [poly5.html](poly5.html), the 5-channel Web Audio synth, into firmware for the FM-1, built on
[Dinghy](dinghy/) (zvenson/dinghy v0.5, GPL-3.0, cloned at commit ea1b6bf).

## Why Dinghy

- It's fully open and builds on macOS, using Docker for JieLi's Linux clang. M-VAVE's Official Open needs Windows and a closed audio engine.
- Poly5 is its own subtractive engine, not FM. We'd throw away Felucca's or M-VAVE's DX7 engine anyway.
- The platform layer is already done: updates, OCT− USB rescue, USB/TRS MIDI with clock, USB audio, panel, screen and flash storage.
- **Host mode** runs the same C file on the Mac and writes the sound to a WAV and the screen to PNGs. Most of the port can be done and heard without touching the FM-1.

## What the hardware gives us (from Dinghy's docs/API.md)

| | FM-1 |
| --- | --- |
| CPU | JieLi AC791N, 240 MHz, **no FPU, no 64-bit divide**: everything in fixed point (Q15, 32-bit phase) |
| Audio | 44.1 kHz, `app_audio()` fills 256 stereo frames every 5.8 ms; aim for under 2.9 ms |
| RAM | 96 KB globals, plus a 336 KB `.pool` for big buffers (delay lines, reverb) |
| Code | 568 KB, run straight from flash (const tables are free) |
| Keys | 27 (F3–G5), with an LED each |
| Buttons | OCT−, OCT+, FX, SEL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC, each with an LED |
| Encoders | SELECT, ALGORITHM, KNOB1–4, PRESETS, plus the MASTER pot (volume) |
| Screen | 240×240 colour (replaces the OLED strip) |
| Storage | settings 4 KB, 8 blobs × 4 KB, 8 slots × 16 KB, each CRC-checked |

Open question: the pitch and mod wheels. Dinghy's FM-1 API exposes only the MASTER and battery ADC channels. The wheels would need finding in `hal/fm1_adc.h`, or come later.

## Feature mapping

| Poly5 | On the FM-1 |
| --- | --- |
| 5 channels × 8 voices (40 Web Audio voices) | 5 channels sharing a **pool of ~12 voices**, sized by measured CPU (≈5,400 cycles/frame in total, before USB and FX) |
| osc: saw / square / tri / sine, + PWM, sub, noise | polyBLEP saw and square, a parabolic sine (Dinghy has one), triangle, a sub one octave down, an LFSR noise source |
| biquad lowpass + env→cutoff, reso | per-voice state-variable filter (Chamberlin or TPT, Q15), coefficients updated every 16 frames from a table |
| attack / release VCA, glide | linear-in-dB AR envelope per voice, updated every 16 frames; glide on the phase increment |
| tape delay (wow), DIGI, SHIM | mono delay line in the pool (~1.5 s = 130 KB of int16); tape adds a slow LFO on the read head and an LP in the loop |
| convolution reverb | small Schroeder/FDN reverb (≈4 combs + 2 allpasses, ~40 KB), since convolution is too expensive |
| saturation, compressor, DJ filter | tanh table; a peak-follower compressor; one master SVF that sweeps LP↔HP |
| round robin / unison / poly | EDIT cycles POLY → RR → UNISON; LED shows the mode |
| arp (UP, DOWN, UP-DN, RAND, AS-PLAYED; rates; gate) | ARP toggles; internal clock, or MIDI clock when it arrives |
| motion recording (takes per param) | REC records the knobs you turn, PLAY loops them; the points are stored in a 16 KB slot per pattern |
| presets (INIT, ROUND ROBIN, …), randomize | PRESETS browses, SAVE stores, packed into the blobs (a preset is ~120 B, so dozens fit in one blob) |
| MIDI ch per channel | kept: channel N plays on MIDI ch N, and the keys play the selected channel |

### Panel layout (fourth redesign, 2026-11-18)

Round robin is what makes POLY5 what it is: each note goes to the next synth. It is the mode at power-on. The sound should lean percussive: hard attacks, a filter snap on every note. Graphics: Teenage Engineering-style line art in the five synths' colours.

**MAIN: the five shapes**

| Control | Does |
| --- | --- |
| keys | play, by the mode |
| **ARP** | the play mode: ROUND ROBIN → RANDOM (each note jumps to a random synth, never the same twice) → UNISON → POLY |
| **SEQ** | the sequencer's page (below) |
| **PLAY** | starts and stops the sequence (flashes on the beat) |
| **REC** | tapped while stopped: step entry; tapped while playing: live record; held 1.2 s: clear the sequence |
| **GLO** | **RANDOM sounds**: five new sounds |
| SELECT | a cursor on the five synths: the one under it grows and shows its state. It goes away by itself after 1.5 s. In POLY, the synth it lands on is the one the keys play (and stays so) |
| **ALGORITHM** | While the delay has the screen: the delay time (40 ms to 1.46 s). While the reverb has it: the room size. While the cursor is showing: that synth **ON**, **OFF** (it takes its turn in the cycle but is silent: a rest) or **SKIP** (not in the cycle). In UNISON, OFF and SKIP both silence it |
| KNOB1–4 | performance knobs for everything: **DJ filter** (left lowpass, centre open, right highpass), its **resonance**, **delay**, **reverb**. The one you turn takes the screen for a moment: a big number and a full-screen animation: the spectrum as a 3D landscape of ridges in the five colours, cut where the filter cuts; the delay's repeats as solid pillars with a pulse running along them at the delay time; the reverb as a hall you fly into, wider and deeper with more |
| OCT− / OCT+ | the keys' octave |
| PRESETS | patches: the eight factory ones (below), then yours (U01–U32) |
| **SAVE** | the save page (below) |

**A SYNTH'S PAGE (press FX / SEL / ENV / LFO / EDIT = synth 1–5; it stays open; the same button again goes back to MAIN)**

| Control | Does |
| --- | --- |
| keys | play this synth only |
| **ALGORITHM** | its **NOISE** |
| SELECT | its wave |
| KNOB1–4 | cutoff, reso, attack, release; **HOME** switches to the other row: env→filter, volume, delay, reverb |
| **GLO** | a new random sound for this synth only |
| PLAY | ON → OFF (a rest in the cycle) → SKIP (not in the cycle) |
| OCT− / OCT+ | its octave |

**Factory patches (redesigned twice on 2026-11-19; names are the user's lines)**

The user's direction: lofi, Boards of Canada, Bonobo, Max Cooper; each patch clearly different from the others; a wide spread of FM; noise used on purpose. They are described in `dinghy/tools/gen_poly5_patches.py` (named fields, defaults for the rest), which writes `firmware/gen/poly5_patches.h`. Run it after a change.

| # | Name | Play mode | Synths | Master effects |
| --- | --- | --- | --- | --- |
| F01 | KNUCKLES OF CHALK | Round robin | melodic, for chords (fourth version, 2026-11-19, the user's wish): five keyboards that hold their notes and blend: FM electric piano, soft detuned saw, hollow square, warm triangle, FM bell an octave up; a little noise on four filters | heavy tape wow and flutter, worn; chorus, room, quarter-note delay, compressor |
| F02 | LADDER OF WAX | Random | melodic (2026-11-19): an old polysynth: detuned saw, FM, square with a slow LFO, triangle, a breathy sine with noise in it; they swell in and hold | chorus, badly drifting worn tape, room, delay |
| F03 | CLIMBING THE ECHO | Random | melodic (2026-11-19): plucked and struck strings that ring out long: harp, nylon guitar, kalimba, a breathy bandpass pluck with noise, music box | long dotted-eighth delay, light flanger, fluttering tape, compressor |
| F04 | THAT NEVER CLIMBS BACK | Unison | detuned pad, slow FM colour, a wash of hiss swept over four bars, sub | chorus, huge reverb, drifting worn tape |
| F05 | GRAVITY'S LAWYER | Unison | melodic (2026-11-19): one thick keyboard of all five: FM, detuned saw, square, sub, a hissy triangle on top; speaks at once, then holds | soft drive, bit reduction, wobbling fluttering tape, compressor |
| F06 | BILLING MY FALLS | Random | cassette drum kit: soft kick, FM rim, noise snare, noise hat, low tom | soft drive, bit reduction, tape that drops out, compressor |
| F07 | TAUGHT ME TO ORBIT | Round robin | swells: sine, wind (noise through a resonant filter swept in time), slowly opening FM; synths 2 and 5 SILENT | slow phaser, biggest reverb, tape |
| F08 | UNGRASPABLE WALLS | Poly | keys: worn FM electric piano; MIDI channels 2-5: sub bass, kalimba, wobbling lead, soft hat | chorus, room, soft drive, tape, compressor |

Levels in the simulation (a played line): about -17 to -20 dB RMS each, peaks under -2 dB, nothing clipped. The first redesign of the day (drier, two harsh industrial patches) was installed on the device before this one; this one is built, not installed.

- **Arpeggiator (2026-11-19):** ARP tapped steps the style: OFF, UP, DOWN, UP-DOWN, RANDOM, AS PLAYED, CLIMB (up, then again an octave higher), CHORD (the last four notes together every step). ARP held half a second steps the play mode (it was the tap before). Held notes (keys and MIDI, up to 12) play one a step at the sequencer's tempo, step length and gate; every note goes through the play mode, so an arpeggio in round robin or random also walks the five synths. With a sustain pedal the chord stays held. The style is saved with a patch (`preset_t.arp`, one of the spare bytes; `arp=` in the patch generator); the header shows `ARP UP / RR`; the ARP light is on and blinks each step. Not arpeggiated: the sequencer's own notes; keys on the sequencer page (they edit steps) and while REC is on. Test: `tests/scripts/poly5-arp.txt`.
- **Audio load, second pass (2026-11-19):** the master effects' 64-bit multiplies (delay, reverb combs, tape, compressor, master gain) are 32-bit now, and the DJ filter is skipped while wide open. Sound unchanged to 0.01 dB in the simulation. Measured before this on the device, F04 in unison: 56 % of a block worst with 12 voices. Not yet re-measured.
- **What the user likes (2026-11-19):** F01's fourth version (melodic, held notes, for chords, heavy tape wow and flutter, a little noise on the filters) "sounds great": keep it. F02, F03 and F05 follow it, each with one voice that has noise in it.
- **Home grid slowed (2026-11-19):** the lines across drift a cell in 1.9 s (it was 0.64 s, too busy), in eighths of a cell so it glides; the stars twinkle a quarter as often. The streak is as the user set it.
- **Pops and overload on held chords (2026-11-19, found in the simulation with clip counters):** (1) DRIVE, even at 12-28, multiplied a summed chord 2-3 times into the clipper: flat tops. Drive is off now on the chord patches (F01, F05, F08); it stays on the drum kit. (2) The chorus / flanger line (16-bit) overflowed on chords: it now holds a quarter of the level; the tape holds half. (3) F04 and F05 (unison) used five voices a note, so a three-note chord wanted 15 of 12: two of their synths are SKIP now (three layers: a four-note chord is exactly 12); they can be switched on again by hand. After this: no hard clipping on 4-7 note chords in any patch; the soft ceiling still works on loud chords. Installed 2026-11-19; the user confirms the pops and overload are gone. Device load on F02 with 12 sustained notes: 49 % of a block worst.
- **Loading animation (2026-11-19):** `intro()`, 1.3 s at switch-on, after the patch is loaded: the five shapes spring up one after the other out of the grid, spinning, full of light that drains, each with its letter of POLY5 (large, in its colour) under it; the letters fade as the home screen takes over with the same shapes in place. The keys and MIDI play during it. Test: `tests/scripts/poly5-intro.txt`.
**Sixteen more factory patches, F09-F24 (2026-11-19)**, named after the user's second set of lines; the user asked for Porter Robinson's Nurture as the main manner, with a couple after Tycho and Boards of Canada. Each has a sequence. The sequences of all 24 are in the generator now (`seq=`; it writes `firmware/gen/poly5_seqs.h`, the `FSEQ[]` table). Settings `rev` is 3: the device starts on F01 once after this update (your patches are U01 on, after F24; their saved contents do not move).

| # | Name | Mode | Manner | What it is |
| --- | --- | --- | --- | --- |
| F09 | CORONER'S TONGUE | Round robin | Nurture | bright detuned saw plucks, pumping compressor |
| F10 | WRAPPED IN TIN | Random | Nurture | toy piano and music boxes, thin, a little crushed |
| F11 | GRIEF GOES VIRAL | Round robin | Nurture | soft felt piano with breath in it, long room |
| F12 | BEFORE IT BEGINS | Unison (3 layers) | Nurture | wide saw pad rising slowly, phaser |
| F13 | LANTERNS OF STATIC | Round robin | Boards of Canada | every voice has static in it; static moves the filters; worn tape |
| F14 | HUNG FROM THE PEWS | Random | - | small church organ in a fast chorus, a hall |
| F15 | PULPIT'S A MIRROR | Round robin | Tycho | warm singing lead and plucks in a dotted-eighth delay |
| F16 | LAUGHING AT YOU | Random | Nurture | chopped-voice chirps: resonant bandpass plucks that fall onto the note |
| F17 | BALLOT OF RUMORS | Round robin | Tycho | plucked bass and guitar figures, filters in time, flanger |
| F18 | SIGNED IN MILK | Round robin | Nurture | lullaby: round sines, triangles, soft bells, thick chorus |
| F19 | VELVET THE MOTIVE | Unison (3 layers) | Boards of Canada | brassy detuned lead whose filter opens after the note; heavy wow |
| F20 | GOSPEL THE GUILT | Random | - | electric pianos and a small choir of slow bandpassed saws |
| F21 | FINGERTIPS COUNTING | Round robin, arp UP | Nurture | crisp short plucks made for the arpeggiator |
| F22 | DOORS MADE OF HUM | Round robin | - | low drones that rise slowly and beat against each other |
| F23 | KNOCKING IN LATIN | Random, arp RANDOM | - | wood: marimba, kalimba, wood block, log drum, thumb piano |
| F24 | ANSWERED IN DRUM | Random | Nurture | crunchy drum kit: kick, clap, hat, tom, rim; crushed and driven |

Simulation, all 24: no hard clipping on a held five-note chord; played lines at about -17 to -22 dB. Not yet heard on the device.

- **POLY5 in a browser (2026-11-19):** the firmware compiled to WebAssembly (dinghy/web/build.sh, 422 KB): web/web.c stands in for the hardware as tests/host.c does, and poly5.c steps its startup animation and its main loop one pass a call when DINGHY_WEB is set (the FM-1's image is byte-identical with and without the change). It runs in an AudioWorklet at 44.1 kHz (about 5 ms latency), 170 times faster than real time on a laptop; the screen and lights go to the page 30 times a second. The page's Play it section is a playable FM-1: mouse and touch on knobs, buttons and keys, the computer's keys (A = C4), MIDI in (Web MIDI). Saved patches stay in the browser (localStorage). Checked: silence at -inf, a chord at -19 dB RMS.
- **POLY5's own identity and safety on both FM-1s (2026-11-19):** the package identity is FM-1_945 (it was Dinghy's FM-1_990; 945 was free in thefirmwarehub.com/catalog.json, read in full: 57 FM-1_9NN numbers are taken, 950 among them). It is fixed for every FM-1 build (`POLY5_ID` in tools/build.py); the version stays on the screen. The update loader's screen says POLY5 in amber. The identity is only the installers' handshake: nothing is stored under it, so patches survive the change. Both FM-1 flash chips are accepted (Puya P25Q80 0x856014, older units; UC25HQ80 0xB36014, newer), anything else refused before erasing: a new test, tests/fm1_jedec_test.c. All of Dinghy's platform tests pass with POLY5 (installer, update loader, M-UPGRADE entry, flash maps, unknown flash). Not yet tried on a unit with the other chip.
- **FAIL fixed (2026-11-19, the user: "it sounds like a noise floor"):** it was. FAIL added white hiss to every sample whenever LOFI was on (F01 sat at -58 dBFS of hiss in silence, F06 at -44), while its dropouts came about once a minute. Now FAIL is dropouts only: about every 6 s at 10, more than once a second at 100 (17 in 10.5 s on a held chord), and the sound crackles while one lasts (grit multiplied into the signal, so silence stays silent: every patch measures digital silence when nothing plays). F01 and F02 have FAIL 0. The LOFI picture throws sparks only where the tape breaks. Levels of all 24 sequences unchanged, nothing clipped.
- **Version number (2026-11-19):** `POLY5_VER` in `poly5.c` (now "1.0") shows as `v1.0` on the bottom line of the screen during the startup animation; raise it with every release.
- **Sequences, fixed and with more chords (2026-11-19):** F09-F24 never loaded their sequences: `sq_from_patch()` still took only the first 8 as factory (`i < 8`), so they got an empty one, and your patches read their saved sequence from the wrong slot (`i - 8`, not `i - 24`). Both now use `NFACTORY`. Six more sequences have chords (the user's wish): F02 (C, A minor, G swells, each answered by a note), F03 (a fifth struck, then two notes), F05 (E minor and D hammered: 3 notes x 3 layers = 9 voices), F09 (chord stabs and climbing plucks), F13 (D minor, B flat), F18 (F, D minor lullaby). With F04, F08, F11, F12, F14, F20 and F22 that is 13 of 24 with chords; F01 is kept as the user likes it. Simulation, each patch's sequence for 12 s: -13 to -25 dB RMS, peaks under -2.6 dB, nothing clipped. Built and installed on the FM-1 2026-11-19, with the new LOFI tape picture; not yet heard there.
- **Sustain pedal (2026-11-19):** MIDI CC 64. Notes from MIDI let go while the pedal is down ring on until it comes up (test: `tests/scripts/poly5-pedal.txt`). The FM-1's own keys and the sequencer are not held by it. MIDI velocity already scales each note's level.

- **DRIVE** (master distortion, Poly5's saturation, added 2026-11-18): up to 9 times the gain into a soft clipper. Set per patch only for now (F05: 70, F06: 55); not on a knob yet.

**Wider sounds (2026-11-18)**

- **Six waves** (SELECT on a synth's page), each with its own shape: saw (ramp), square (cube), triangle (pyramid), sine (globe), **FM** (diamond: a sine bent by a second sine; the filter-envelope amount is how hard it is bent on the hit), **METAL** (hourglass: two squares ringing against each other).
- **Third knob row** (HOME steps through three rows): **TONE** (FM: ten ratios from clean to clangy; METAL: the interval), **DROP** (the pitch starts up to two octaves high and falls in about 25 ms: punch, tom, kick, zap), **DECAY** (0 holds at 70% as before; otherwise the note dies by itself in 30 ms to 3 s and frees its voice), **DETUNE** (a second copy of saw, square, triangle or sine up to 42 cents off).
- **GLO (random sounds)** rolls the new waves and controls too.
- **Patch format 2** (17 bytes a synth); patches saved in format 1 are converted at power-on.
- **Delay takeover** now follows the keys: the lead cube jumps on each note and the echoes jump in turn, one delay time apart.

**SEQUENCER (2026-11-18; the pattern of OberVave, SLOOP and M-VAVE's own: SEQ its page, PLAY, REC stopped = step entry, REC playing = live, REC held = clear)**

- **What is POLY5's own:** the sequence keeps pitches only (up to 4 notes a step, up to 32 steps). Each step goes through the play mode like a key press, so the round robin hands the pattern round the synths, RANDOM scatters it, and a SILENT synth makes a rest. Changing patch or play mode while it runs keeps the notes and changes who plays them.
- **Its page (tidied 2026-11-18):** four dials at the top like a synth's page (tempo, step, swing, length), no instruction lines, one readout under the bars (the step and its notes). The steps as glass bars as high as their note, each in the colour of the synth that played it last, flaring and throwing sparks when it sounds. A short pattern gets wider bars.
- **Knobs on its page:** KNOB1 tempo (40–240), KNOB2 step length (1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32), KNOB3 gate (5% to 400% of a step: above 100% notes ring into the next steps), KNOB4 length; SELECT a step; ALGORITHM that step's note (below the lowest: no note). HOME leaves; the sequence keeps running on every page.
- **Step entry:** a note or chord, let go, and the cursor moves on; SELECT skips a step to leave a rest.
- **Editing a step without REC, also while it plays:** on the sequencer's page the notes of the step under the cursor light on the keyboard; a key switches its note in that step on or off (up to four). ALGORITHM moves the whole step up or down.
- **MIDI clock:** when a clock comes in (USB or TRS) it sets the tempo, and START / STOP run the sequence.
- **Factory start:** settings now carry a revision; this build's first power-on starts on F01 with octave 0 and an empty sequence (user patches are kept).
- **A sequence per patch (2026-11-18):** each factory patch has one (below); your patches save theirs with SAVE (blobs 1 and 2). Loading a patch loads its sequence, except while a sequence is playing: then the notes go on and only the sounds change. Sequence edits count as unsaved changes (`*`).
- **Defaults the user likes:** lengths 6, 9 or 3; step 1/8 or 1/4; tempo 118. An empty sequence starts at 6 steps of 1/8 at 118.

| Patch | Key | Steps | Step | Gate | What it plays |
| --- | --- | --- | --- | --- | --- |
| KNUCKLES OF CHALK | A minor | 6 | 1/8 | 60% | triad up, octave, the fourth, the seventh (six notes, so each pass shifts by one synth) |
| LADDER OF WAX | C major | 9 | 1/8 | 60% | up through the chord and the sixth, a breath, home by the second |
| CLIMBING THE ECHO | D minor | 3 | 1/4 | 50% | root, fifth, third; the delay fills in |
| THAT NEVER CLIMBS BACK | F major 7 / A minor 7 | 6 | 1/4 | 280% | two held four-note chords |
| GRAVITY'S LAWYER | E minor | 6 | 1/8 | 50% | root hammered, the third, the tritone |
| BILLING MY FALLS | C minor | 9 | 1/8 | 50% | a drum figure on C, E flat and G |
| TAUGHT ME TO ORBIT | G major | 5 | 1/4 | 150% | five notes for five synths: each keeps its own note; the two silent synths are two fixed rests |
| UNGRASPABLE WALLS | B minor, G, D | 6 | 1/4 | 80% | three chords, each answered by a top note |
- **Synth states:** ON, **SILENT** (takes its turn, no sound; was called OFF) and SKIP. TAUGHT ME TO ORBIT has synths 2 and 5 SILENT.
- **Delay takeover (third version):** each note you play appears as its synth's glass shape at the left, and its repeats appear to the right one delay time apart, smaller and fainter, as many as the feedback makes audible. Takeovers now stay 2.5 s (delay 4 s).

**MAIN's pages (2026-11-19; the user's design). ALGORITHM turns them; HOME goes back to PLAY. Each has four dials under the four knobs and the effect's picture.**

| Page | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| PLAY | filter | resonance | delay | reverb (the quick knobs: each takes the screen for a moment) |
| SATURATION | drive | type (soft, hard, fold) | volume | on/off |
| FILTER | sweep (lowpass, open, highpass) | resonance | | on/off |
| MODULATION | type (off, chorus, flanger, phaser) | speed | depth | mix |
| DELAY | amount | time | repeats | on/off |
| REVERB | amount | size | damp | on/off |
| BIT REDUCTION | bits | rate | mix | on/off |
| LOFI (tape, after pedals like Shallow Water and Generation Loss) | wow (slow random pitch drift, about 60 cents at 80) | flutter (9 Hz tremble) | age (duller and thinner) | fail (dropouts, crackling while they last) |
| MASTERING | comp | rel | gain | on/off (the limiter stays on) |

- **One page knob (2026-11-21, the user's wish):** ALGORITHM turns the pages everywhere: MAIN's and a synth's. On a synth's page PRESETS is that synth's noise (they were the other way round; older notes below may still say so).
- **The page knobs go round and round** (ALGORITHM on MAIN and on a synth's page): past the last page comes the first.
- **LOFI's picture, second version (2026-11-19, the user's wish, after the OP-1's tape):** a tape loop in line art: two reels with the tape wound on them (amber, duller with AGE), a hub with three teeth and a mark on each so you see it turn, slowly (about 0.2 turns a second, it was 0.6; WOW makes it unsteady), a loop sign between them, the tape round four guide rollers and under the head, shaking with FLUTTER; FAIL breaks it, throws hiss, and the head's light goes red on a dropout.
- **A switched-off effect** shows OFF on its page; the delay and the reverb then take nothing in and ring out.
- **PLAY's background (2026-11-19, chosen from rendered options, then tuned by the user):** a floor and a ceiling of faint white grid lines meeting at nearly one point behind the shapes, drifting towards you, a little brighter while synths sound, about twenty stars, and one strip of light every 5 seconds running slowly (2.8 s) down a line to the vanishing point. No glow at the horizon (tried, removed: it distracted).
- **Page marks** are at the bottom centre of the screen, on MAIN's pages and on a synth's.
- **On PLAY** ALGORITHM still sets ON / SILENT / SKIP while SELECT's cursor is showing; otherwise it turns the pages.
- **One picture an effect:** the filter (ridges), the delay (each note's shape and its repeats) and the reverb (the hall) look the same on MAIN's page and on a synth's page; the synth's is in its colour and follows its own settings. Saturation and modulation share theirs too.
- **New in the engine:** drive types, a phaser, lofi (fewer bits, a lower rate), a compressor with make-up gain before the limiter. A stolen voice now fades over about 4 ms (was 0.4 ms: a click).
- **Patch format 5:** a preset is 171 bytes (8 to spare for later settings); format 4 is converted at power-on.
- **Audio load measured on the FM-1 (2026-11-19, F04, a detuned UNISON pad):** nothing playing 0.55 ms a buffer (9% of the 5.8 ms), 5 voices 1.7 ms, all 12 voices 3.2 ms worst (55%): about 0.18 ms a voice. Dinghy's rule is to stay under half a buffer, since USB audio and the key scan need the rest; the user heard clicks over USB, worst in UNISON. The voice's inner loops were then rewritten in 32-bit maths (filter in Q13 on a quarter-scale signal, pink noise in Q10, the saw and square's edge correction only at the edges); the sound is unchanged to 0.1 dB. To be measured again.
- **Measured on the device (diagnosis):** the audio interrupt's worst and average time a buffer are reported with CC 119 = 2 (channel 16, CC 40 and 41, in 50 us; a buffer lasts 5805 us).

**A synth's ten pages (2026-11-18; the user's list). PRESETS turns the pages; each has up to four dials under the FM-1's knobs and its own picture. HOME leaves; ALGORITHM is the synth's noise level on every page; SELECT its wave; GLO a new sound; PLAY its state.**

| Page | Dials | Picture |
| --- | --- | --- |
| WAVE | wave, octave, tone, detune | the wave running (its second copy when detuned), the glass shape |
| AMP ENVELOPE | attack, decay, sustain, release | the envelope, filled |
| FILTER ENVELOPE | depth, speed, pitch (the fall at the note's start) | the filter where it rests, where the envelope throws it, falling back when you play |
| FILTER | cutoff, reso, type (low, band, high) | the curve, filled, with partials dancing where it lets them through |
| LFO | shape (sine, triangle, saw, square), speed, depth, sync | its wave going by, a marker at now |
| NOISE | level, filter (noise on the cutoff), speed | sparks for the level, a jagged line for the cutoff's steps |
| SATURATION | drive (this synth's own), volume | a clean wave and what the drive makes of it |
| MODULATION | type (off, chorus, flanger), speed, depth, mix | three voices weaving |
| DELAY | send, time, repeats | the synth's shape and its repeats |
| REVERB | send, size, damp | the room |

- **New in the engine for this:** LFO, envelope and noise act on the cutoff together; LFO shapes and sync to the sequencer tempo (4 bars to 1/32, restarted when the sequence starts); amp sustain (with DECAY on, the level it falls to and holds); filter types; per-synth saturation; a stereo chorus / flanger on everything.
- **Shared settings:** the modulation effect, the delay's time and repeats and the reverb's size and damping are the patch's, the same on every synth's page.
- **Patch format 4:** a synth is 25 bytes; the 32 user patches are kept in two halves (blobs 0 and 3). Formats 1 and 2 (all 32 in blob 0) are converted at power-on and both halves written at once. Format 3 was never installed.
- **Gone:** the three HOME knob rows, ARP on a synth's page.

**SAVE PAGE (the same pattern as OberVave's MEMORY and SLOOP's SAVE layer)**

| Control | Does |
| --- | --- |
| KNOB1 or PRESETS | the slot, U01–U32 |
| KNOB2 or SELECT | what SAVE does: SAVE, LOAD or ERASE |
| KNOB3, KNOB4 | the name's two words (16 × 16 words, e.g. GLASS SPARK) |
| **SAVE** | does it. So SAVE, SAVE keeps what you hear: into the patch's own slot, or the first free one. Overwriting another used slot, or erasing, asks for SAVE once more |
| HOME | back |

- **Nothing saves itself.** What you play is a working copy; a `*` after the patch name (and a dim SAVE light) means unsaved changes.
- **Kept across power-off:** the 32 user patches (one flash blob, own magic `PLY5`), and the patch and keyboard octave you were on.
- **Bottom of the main screen:** the patch, e.g. `U03  FUZZY CUBE *`.
- **Header:** POLY5 with one letter per synth colour; the play mode in white.
- **Amber bar:** under every synth that holds or still sounds a note.

- **Master effects (in the engine since 2026-11-18):** a delay (Poly5's time and feedback, 5.5 kHz lowpass in the loop), a stereo reverb (4 combs, 2 allpasses a side), the DJ filter last, and a soft ceiling above 60% so unison stays clean. Each synth's own delay and reverb sends (row 2 of its page) now work; the main-screen delay and reverb knobs add everything on top, with more repeats and a bigger room as they rise.

- **Main screen:** POLY5 small at top left, the play mode small at top right, and five rotating 3D shapes in a row: tall, of tinted glass, each filling with bright colour from the bottom with its synth's level and draining as the note fades (chosen 2026-11-18 over plain wireframe, taller wireframe and plain glass). The shape is the wave (cube = square, pyramid = triangle, ramp = saw, globe = sine); the colour and the number under it are the channel. A shape swells, spins faster and throws particles in its colour when its synth plays, with white particles for its noise. An amber bar under a number marks the synth that plays next. RANDOM sounds makes the shapes spin and shuffle.
- **Synth page:** four dials at the top, under the FM-1's physical knobs, with the value inside each (HOME flips to the second row). Below: the synth's shape, big, with particles; and its sound as a picture: the envelope filled in the synth's colour, the filter curve across it, the noise as specks.
- **Lights:** a synth's button is bright while it sounds, blinks while its page is open, and is dim while it is in the cycle.
- **Loudness:** noise 100 is as loud as the oscillator; sine and triangle are boosted to sound as loud as saw and square; RANDOM sounds avoids very low cutoffs and low-octave sines, which were nearly inaudible.
- **Attack:** the minimum is 1 ms (Poly5: 3 ms). RANDOM sounds gives an instant attack three times out of four.
- **Diagnosis over MIDI:** CC 119 value 1 clears, value 2 reports each synth's output peak (pitch bend on channel N) and patch (CC 20–27).
- **Still to place:** ARP, REC (motion), SAVE and the master FX (later phases).

## Status (2026-11-18)

- **Phase 0, software side, done:**
  - Host mode runs. `tests/run_host.sh` needed a macOS fix (`-Dsection(x)=used`, Darwin only).
  - The JieLi toolchain is in `~/.jieli` and the AC79 SDK in `~/fw-AC79_AIoT_SDK`.
  - `./build.sh` produces `dinghy/build/dinghy.fwsc`, and `tests/run_tests.sh` passes.
- **Connected FM-1:** identity `FM-1_920`, running, USB port "ChoralRoot FM-1", so a Felucca-family firmware is on it now. Its flash chip type is not known yet.
- **Dinghy installed:** written with `tools/fm1_install.py` on 2026-11-18. The FM-1 now reports `FM-1_990` (Dinghy), running. macOS still shows the old "ChoralRoot FM-1" port name.
- **Phase 0, device side, still to do:** test the OCT− rescue, then restore stock V16 or ChoralRoot to close the round trip.
- **Phase 1, voice, done in host mode** (`dinghy/firmware/src/poly5.c`, tables from `tools/gen_poly5_tables.py`):
  - The engine: polyBLEP saw and square, triangle and sine, Poly5's pink noise, a TPT lowpass matching the Web Audio biquad (within about 1–2 dB per partial), the filter envelope, and the attack / 80 ms decay to 70% / release VCA.
  - 12 voices shared by the 5 channels, with POLY, RR and UNISON routing. The INIT and ROUND ROBIN factory presets are in.
  - Run it with `APP=poly5 tests/run_host.sh tests/scripts/poly5.txt`. `APP=poly5 ./build.sh` passes the chip checks: image 61 KB, RAM 14.7 KB.
  - Estimated from the disassembly: about 45 instructions per voice per sample, about 80 with noise. That's well under budget; measure on the device in phase 5.
  - Poly5's `glide`, `pwm` and `subVol` are never used by its sound engine, so they're left out for now.
  - UNISON with a 3-note chord asks for 15 voices (12 exist) and clips. That's as loud as Poly5 is before its master saturation and compressor (phase 3).

## Phases

Each phase ends with something you can hear: in host mode first, then on the device from phase 5 on.

0. **Setup and a safe round trip.**
   - Start Docker, run `tools/get_toolchain.sh`, and clone the AC79 SDK tag.
   - Run `tests/run_host.sh`, then `./build.sh` the unmodified Dinghy.
   - On the FM-1: check which flash chip you have, install stock V16 with M-UPGRADE, and confirm the OCT− rescue works.
   - Flash unmodified Dinghy, then go back to stock through the hub.
1. **Voice in fixed point.** Oscillators, noise, sub, SVF, envelopes and glide in a new `poly5.c` replacing `dinghy.c`. Render the same notes as Poly5's INIT and ROUND ROBIN presets and compare by ear.
2. **Channels and modes.** The 5 channels, the shared voice pool, POLY/RR/UNISON, the arp with internal and MIDI clock, and MIDI per channel.
3. **Master FX.** Delay (tape, digital, shimmer), reverb, saturation, compressor, DJ filter. Time the audio code with `fm1_ticks()` in host mode and set the voice count from that.
4. **Panel and screen.** Pages, encoders and LEDs, plus a 240×240 screen drawn with only what changed: channel strip, param bars, mode.
5. **On the device.** Run `./build.sh`, measure the real audio-ISR time, tune the voice count and flash.
6. **Presets.** Port the factory presets and add save and browse in the blobs.
7. **Motion recording.** Record, play and clear, and store the points in slots.

## Risks

- **Bricking:** the flash is single-bank, so always keep the OCT− rescue path and `FELUCCA_OTA=1`. Never touch `platform.c`, the loader or the update code.
- **CPU:** 40 voices won't fit. 12 voices plus FX is the working guess until phase 3 has measurements.
- **Compiler:** keep `-Os`. Dinghy found that `-O2` breaks USB-MIDI on the device.
- **Licence:** the firmware will be GPL-3.0, so whoever gets the `.fwsc` gets its source.

- **Screen brightness (asked 2026-11-21):** the FM-1's backlight is one pin (PA2, active low): on or off, no dimming, and it is fully on. No firmware setting can make it brighter; only brighter colours in the drawing can.

- **LENGTH on KNOB 2 of PLAY (2026-11-21, the user's wish; it was the master filter's resonance):** `len_off`, -100 .. +100, added to every synth's release (and to its decay, if it has one) when a note starts or is let go; each synth stops at its own end. Not part of a patch: a patch loads at 0; SAVE writes the lengths as heard and puts the knob back to 0. Getting back to 12 o'clock: the knob never passes 0 in one turn and rests there 0.4 s; the picture (`g_length`) shows five tails with a tick where the patch has each, and a scale whose middle is a target that turns amber and closes; the home screen says LENGTH +N in its last line while it is off the patch. The resonance stays on the FILTER page (KNOB 2 there); every factory patch now starts with it at 30 (a DJ mixer's edge) instead of 0. Test: `tests/scripts/poly5-length.txt`.
- **Read a patch off the device (2026-11-21):** MIDI CC 119 = 3 sends the working patch and its sequence byte for byte (pitch bends on channel 15); `tools/poly5_read.py` prints it as a generator entry. F01's synth 2 is the user's own edit, read from the device's short report: square, an octave down, noise 35.
- **USB name POLY5 (2026-11-21):** `USB_PRODUCT_STR` in poly5.c (usb.c used "Felucca"). The update loader keeps "Felucca Update" (the installers look for it). **Update screen:** `LDR_COLOURS` (build.py `SCREEN_COLOURS`): the name letter by letter in the five synth colours.
- **Clicks in USB audio recordings: the cause (2026-11-21):** platform.c hands the 256 rendered frames to the USB ring only after `app_audio` returns, and the ring kept just 80..128 frames (1.8..2.9 ms) in hand when a render started. POLY5's render takes up to ~2.9 ms with twelve voices (1.4 ms with four notes): the ring ran empty in heavy renders, the last frame was repeated: a click. Not the speaker output: only the recording. Fix: `UA_SLOW_RENDER` (usb.c): ring 1024, low point kept at 264..312 frames (6 ms), 4 ms more delay on USB audio. CC 119 = 2 now also reports USB audio: CC 42 underruns, 43 dropped blocks, 44 missed packets, 45 the low point / 4, 46 bits (recording, 48 kHz, flowing). To confirm on the device with a recorder open.
- **USB audio clicks: confirmed fixed on the device (2026-11-21):** the user recorded chords into Ableton at 44.1 kHz: no clicks. The device's counters for that recording: the ring ran empty 0 times, 0 blocks dropped, the low point at 280 frames, renders up to 2.95 ms; 6 packets missed (an IN token with nothing queued: to watch; they were there before the count started, likely as the stream starts).
- **Naming a patch (2026-11-21, the user's wish: quick, random too, as an Elektron):** the save page shows the name large. GLO: a random two-word name. KNOB 3, 4: the two words. The 27 keys type (A on the lowest .. Z, the top key a space; silent meanwhile; the page draws the keys with their letters). OCT-: the last letter away; OCT+: all of it. ALGORITHM: the cursor; SELECT: the letter under it (A-Z, 0-9, space). A name that came whole is "fresh" (on amber): the first letter typed replaces it. SELECT no longer picks SAVE / LOAD / ERASE (KNOB 2 does); the slot list shows three rows. Test: `tests/scripts/poly5-name.txt`. Installed; not yet tried on the device by me.

- **Release 1.0 prepared (2026-11-21):** `APP=poly5 ./build.sh --release 1.0` -> `build/dinghy-1.0.fwsc` (copied as `poly5-1.0.fwsc`), identity FM-1_945, version 1.0 on the screen. Notes: `RELEASE-1.0.md`. To be listed in the FM-1 Firmware Hub: a GitHub release with the .fwsc, a screenshot, and the hub's "Submit your firmware" form (thefirmwarehub.com/submit/).
- **The USB name must contain "FM-1" (found while checking the release):** the installers (and the hub's, after them) only probe MIDI ports whose name matches a list (`fm1_install.py` PORT_RE: fm-1, felucca, sloop, ...). "POLY5" alone is not in it: on a computer that had never seen the unit (no cached port name) it could not have been found to be updated or put back to stock. Now "POLY5 FM-1"; `tests/usb_name_test.py` guards it. On the author's Mac the MIDI port had kept the cached name "ChoralRoot FM-1", which hid this.
- **What differs from upstream Dinghy (ea1b6bf) in the code that can harm a unit:** nothing in hal/, the loader's core, ota.c, recovery.c, bootguard.h, the flash code, fm1pkg_make.py or fm1_install.py. Changed: ldr_screen.c (the name's colours), usb.c (the deep audio ring, the product name), platform.c (the browser build's include), build.py (names).
