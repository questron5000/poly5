# POLY5

Five synths in one, for the M-VAVE FM-1. Every note you play goes to the next synth in turn (round robin), or to a random one, or to all of them, or to one. It's lofi and melodic, with heavy tape wow and flutter, noise used on purpose, 24 factory patches each with its own sequence, an arpeggiator, a step sequencer, and eight master effects.

POLY5 started as a Web Audio synth ([poly5.html](poly5.html)) and runs as firmware on the FM-1 by way of [Dinghy](dinghy/) (zvenson/dinghy v0.5, GPL-3.0).

## The webpage

[index.html](index.html) is POLY5's page: what it is, random first, every control, the patches, the videos and how to install. Its pictures are the firmware's own screens, made by [site/make_images.py](site/make_images.py). Served from this repository's root (GitHub Pages: branch `main`, folder `/`).

## Video manual

Eleven short chapters in [manual/videos](manual/videos), or all of them in one: [poly5-manual-full.mp4](manual/videos/poly5-manual-full.mp4).

| # | Chapter |
| --- | --- |
| 01 | Meet the five synths |
| 02 | **Random: the key to everything** (random synths, random sounds, random arpeggios) |
| 03 | Play modes: round robin, random, unison, poly |
| 04 | Patches and random sounds |
| 05 | Performance knobs: filter, resonance, delay, reverb |
| 06 | On, silent, skip |
| 07 | A synth's page |
| 08 | The effects pages |
| 09 | The sequencer |
| 10 | The arpeggiator |
| 11 | Saving your patches |

The videos are made from the firmware itself: Dinghy's host mode renders the screen and the sound, and [manual/manual.py](manual/manual.py) adds the panel picture and the captions. To change a chapter, edit [manual/chapters.py](manual/chapters.py), then run `python3 manual/manual.py` (add a chapter number to make only that one).

## Building and installing

```sh
cd dinghy
APP=poly5 ./build.sh                                          # -> build/dinghy.fwsc
python3 tools/fm1_install.py build/dinghy.fwsc --port FM-1 --yes
APP=poly5 tests/run_host.sh tests/scripts/poly5.txt           # on your computer: sound as WAV, screens as PNG
```

`./build.sh` needs Docker, the JieLi toolchain (`tools/get_toolchain.sh`) and the AC79 SDK. After a change to the patches, run `python3 tools/gen_poly5_patches.py`.

What the panel does, the factory patches, and the state of the port are all in [PORT-PLAN.md](PORT-PLAN.md).

## Licence

GPL-3.0, as Dinghy: whoever gets the firmware gets its source. See [dinghy/LICENSE](dinghy/LICENSE).
