# POLY5 1.0 for the M-VAVE FM-1

Five synths in one. Every note you play goes to the next synth, or to a random one. Lofi and melodic, with worn tape,
wow and flutter, and noise used on purpose.

- 24 factory patches, each with its own sequence; 32 slots for yours
- Play modes: round robin, random, unison, poly; an arpeggiator with seven styles; a step sequencer
- Each synth has ten pages (wave, envelopes, filter, LFO, noise, saturation, modulation, delay, reverb)
- Eight master effects pages: saturation, filter, modulation, delay, reverb, bit reduction, lofi tape, mastering
- GLO rolls new sounds for all five synths, or for one on its page
- On the home screen the four knobs are filter, length, delay and reverb
- Naming a patch: a random name, two word knobs, or typed on the keys
- USB audio to your computer at 44.1 or 48 kHz, USB and TRS MIDI in, a sustain pedal (CC 64)

Manual, videos and a playable version in your browser: https://questron5000.github.io/poly5/

## Install

`poly5-1.0.fwsc` (below) is the package. Once POLY5 is listed there, the FM-1 Firmware Hub installs it in one click:
https://thefirmwarehub.com in Chrome or Edge, Connect FM-1, Install. From a terminal today: `python3 tools/fm1_install.py poly5-1.0.fwsc --yes`.

Package identity: FM-1_945. SHA-256 of `poly5-1.0.fwsc`:
`d7b1b63af283a95c32d02d5784187adfb401fb0607b4d854b8c3bdc4f4bf8936`

## Safe to try

POLY5 is built on Dinghy 0.5 (zvenson) and uses its updater, update loader, rescue and flash code unchanged.

- Both FM-1 flash chips are accepted: the Puya P25Q80 of older units and the UC25HQ80 of newer ones. Any other chip
  is refused before anything is erased.
- The loader checks the whole package before it writes and verifies every block; the part of the flash that starts
  the FM-1 is never touched.
- Hold OCT- while switching on for the USB rescue. Two crashes while starting lead there by themselves.
- Back to the official firmware at any time with the Firmware Hub.

Tested on one FM-1 (the author's). The older or newer flash chip is covered by a test of the chip check, not by a
second device. Installing firmware is at your own risk: keep the cable in until the FM-1 restarts by itself.

## Credits and licence

GPL-3.0. Built on Dinghy by zvenson, which stands on Felucca (Leo Kuroshita, Hügelton Instruments) and SLOOP (isod89).
POLY5 by B. Danos.
