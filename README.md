# POLY5

Five synths in one, for the M-VAVE FM-1. Every note you play goes to the next synth in turn (round robin), or to a random one, or to all of them, or to one. It's lofi and melodic, with heavy tape wow and flutter, noise used on purpose, 24 factory patches each with its own sequence, an arpeggiator, a step sequencer, and eight master effects.

POLY5 started as a Web Audio synth ([poly5.html](poly5.html)) and runs as firmware on the FM-1 by way of [Dinghy](dinghy/) (zvenson/dinghy v0.5, GPL-3.0).

## The webpage

[index.html](index.html) is POLY5's page: what it is, random first, every control, the patches, the videos and how to install. Its pictures are the firmware's own screens, made by [site/make_images.py](site/make_images.py). Served from this repository's root (GitHub Pages: branch `main`, folder `/`).

## Play it in a browser

The page's **Play it** section runs the real firmware: `dinghy/web/build.sh` compiles `poly5.c` to WebAssembly (`site/play/poly5.wasm`, with `dinghy/web/web.c` in place of the hardware), and it runs in the browser's audio thread (`site/play/poly5-worklet.js`) behind a playable FM-1 (`site/play/poly5-player.js`). The device build is unchanged by it, byte for byte. Needs LLVM's clang and `wasm-ld` (macOS: `brew install llvm lld`).

## Video manual

Eleven short chapters in [manual/videos](manual/videos), or all of them in one: [poly5-manual-full.mp4](manual/videos/poly5-manual-full.mp4).

| # | Chapter |
| --- | --- |
| 01 | Meet the five synths |
| 02 | **Random: the key to everything** (random synths, random sounds, random arpeggios) |
| 03 | Play modes: round robin, random, unison, poly |
| 04 | Patches and random sounds |
| 05 | Performance knobs: filter, length, delay, reverb |
| 06 | On, silent, skip |
| 07 | A synth's page |
| 08 | The effects pages |
| 09 | The sequencer |
| 10 | The arpeggiator |
| 11 | Saving your patches |

The videos are made from the firmware itself: Dinghy's host mode renders the screen and the sound, and [manual/manual.py](manual/manual.py) adds the panel picture and the captions. To change a chapter, edit [manual/chapters.py](manual/chapters.py), then run `python3 manual/manual.py` (add a chapter number to make only that one).

## Installing

The easy way: [install it from your browser](https://questron5000.github.io/poly5/install/) (Chrome or Edge). Or as a file: [poly5-1.2.fwsc](https://github.com/questron5000/poly5/releases/download/v1.2/poly5-1.2.fwsc) ([release notes](https://github.com/questron5000/poly5/releases/latest)). Connect the FM-1 by USB, then:

```sh
curl -LO https://raw.githubusercontent.com/questron5000/poly5/main/dinghy/tools/fm1_install.py
pip3 install mido python-rtmidi
python3 fm1_install.py poly5-1.2.fwsc --yes
```

### Your saves and other firmwares

POLY5 keeps your patches, their sequences and its settings in the FM-1's storage, the same part of the flash other firmwares use for theirs. POLY5 does not read another firmware's saves, and another firmware does not read POLY5's. Switching firmware can erase the saves of the one you leave: if your patches matter, do not count on finding them after a visit to another firmware.

### What it was tested on

One FM-1. Installed over Dinghy, and over itself many times. Not yet done on that unit: an install straight from the official V15, leaving POLY5 for another firmware, and an update cut off halfway. Both flash chips (Puya P25Q80, UC25HQ80) are accepted by the app and by the update loader; the newer chip is covered by a test, not by a second unit.

## Building

```sh
cd dinghy
APP=poly5 ./build.sh                                          # -> build/dinghy.fwsc
python3 tools/fm1_install.py build/dinghy.fwsc --port FM-1 --yes
APP=poly5 tests/run_host.sh tests/scripts/poly5.txt           # on your computer: sound as WAV, screens as PNG
```

POLY5's package identity is **FM-1_945** (free in the FM-1 Firmware Hub's catalogue on 2026-11-19); installers and the hub tell it apart from Dinghy (FM-1_99N) by it. The update screen says POLY5.

Both FM-1 flash chips are supported, the Puya P25Q80 of older units and the UC25HQ80 of newer ones; the update loader refuses any other chip before it erases anything. Hold **OCT-** while switching on for the USB rescue. `dinghy/tests/run_tests.sh` runs the platform's tests (installer, update loader, flash maps, both chips).

`./build.sh` needs Docker, the JieLi toolchain (`tools/get_toolchain.sh`) and the AC79 SDK. After a change to the patches, run `python3 tools/gen_poly5_patches.py`.

What the panel does, the factory patches, and the state of the port are all in [PORT-PLAN.md](PORT-PLAN.md).

## Licence

GPL-3.0, as Dinghy: whoever gets the firmware gets its source. See [dinghy/LICENSE](dinghy/LICENSE).
