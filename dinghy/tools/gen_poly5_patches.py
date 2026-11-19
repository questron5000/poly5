#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Write firmware/gen/poly5_patches.h: POLY5's eight factory patches (FACTORY[]), from the descriptions below.
Run it again after a change: python3 tools/gen_poly5_patches.py

A synth is a dict; what it leaves out is at its default. The words: wave (SAW SQUARE TRI SINE FM METAL), on
(ON SILENT SKIP), filt (LP BP HP), lfo_shape (SINE TRI SAW SQUARE), lfo_sync (the LFO in note values of the tempo:
lfo_rate = one of SYNC[...])."""
from pathlib import Path

WAVE = {"SAW": "W_SAW", "SQUARE": "W_SQUARE", "TRI": "W_TRI", "SINE": "W_SINE", "FM": "W_FM", "METAL": "W_METAL"}
ON = {"SKIP": 0, "ON": 1, "SILENT": 2}
FILT = {"LP": 0, "BP": 1, "HP": 2}
SHAPE = {"SINE": 0, "TRI": 1, "SAW": 2, "SQUARE": 3}
MODE = {"POLY": "M_POLY", "RR": "M_RR", "UNISON": "M_UNISON", "RANDOM": "M_RANDOM"}
SYNC = {"4BAR": 5, "2BAR": 17, "1BAR": 28, "1/2": 40, "1/4": 50, "1/8": 62, "1/8T": 72, "1/16": 84, "1/32": 96}
MOD = {"OFF": 0, "CHORUS": 1, "FLANGER": 2, "PHASER": 3}
DRV = {"SOFT": 0, "HARD": 1, "FOLD": 2}

CH_DEF = dict(midi_ch=1, on="ON", wave="SAW", octave=0, noise=0, env_filt=0, cutoff=100, reso=2, attack=0, release=20,
              dly=0, rev=0, vol=80, tone=0, drop=0, decay=0, detune=0, env_rate=0, lfo_depth=0, lfo_rate=30,
              filt="LP", lfo_shape="SINE", lfo_sync=0, nz_depth=0, nz_rate=50, sat=0, sus=0)
FX_DEF = dict(mode="RR", mvol=80, dj=0, del_time=14, del_fb=35, del_all=0, rev_size=45, rev_damp=45, rev_all=0, drive=0,
              dj_reso=0, comp=0, mod="OFF", ch_rate=30, ch_depth=50, ch_mix=0, drv="SOFT", lofi_bits=0, lofi_rate=0,
              lofi_mix=0, cmp_spd=30, cmp_gain=0, tp_wow=0, tp_flut=0, tp_age=0, tp_fail=0, arp=0)

# delay times at 118 to the minute (0.04 s + 0.025 s each): 9 = an eighth, 14 = a dotted eighth, 19 = a quarter
# The set's character (the user's wish, 2026-11-19): lofi; Boards of Canada (detuned, wobbling, worn tape), Bonobo
# (mallets, kalimbas, dusty drums), Max Cooper (precise plucks, filters moving in time, long delays). Little noise.
PATCHES = [
    # KNUCKLES OF CHALK: round robin, melodic: five keyboards that hold their notes, close enough to blend, so a
    # chord is spread over all five. An electric piano, a soft detuned saw, a hollow square, a warm triangle, a
    # bell an octave up. A little noise moves the filters; the tape wobbles and flutters heavily
    dict(name="KNUCKLES OF CHALK", mode="RR", mvol=78,
         seq=dict(div="1/8", gate=12, steps=[57, 64, 60, 69, 62, 67]),   # A minor: the triad up, the octave, the fourth, the seventh
         del_time=19, del_fb=38, del_all=6, rev_size=68, rev_damp=50, rev_all=16,
         mod="CHORUS", ch_rate=20, ch_depth=40, ch_mix=35, comp=40, cmp_spd=45, cmp_gain=15,
         tp_wow=60, tp_flut=38, tp_age=40, tp_fail=3,
         ch=[
             dict(wave="FM", tone=0, env_filt=50, env_rate=40, cutoff=72, attack=1, decay=62, sus=45, release=50,
                  nz_depth=8, nz_rate=30, dly=15, rev=30, vol=82),
             dict(wave="SAW", detune=26, cutoff=46, reso=6, env_filt=35, env_rate=50, attack=3, decay=60, sus=55, release=55,
                  nz_depth=14, nz_rate=35, dly=20, rev=35, vol=66),
             dict(wave="SQUARE", detune=12, cutoff=44, reso=4, env_filt=30, env_rate=45, attack=2, decay=58, sus=50, release=50,
                  nz_depth=10, nz_rate=25, dly=20, rev=30, vol=60),
             dict(wave="TRI", detune=20, cutoff=64, env_filt=20, attack=2, decay=64, sus=60, release=55, lfo_depth=6,
                  lfo_rate=40, nz_depth=8, nz_rate=40, dly=15, rev=35, vol=84),
             dict(wave="FM", octave=1, tone=14, env_filt=45, env_rate=35, cutoff=78, attack=1, decay=56, sus=25, release=55,
                  dly=35, rev=45, vol=58),
         ]),
    # LADDER OF WAX: random, melodic. Boards of Canada: an old polysynth on a tape that drifts badly: five voices
    # that swell in a little and hold, brighter than KNUCKLES, the filters opening slowly
    dict(name="LADDER OF WAX", mode="RANDOM", mvol=80,
         seq=dict(div="1/8", gate=16, steps=[[60, 64, 67], 0, 72, 69, [57, 60, 64], 0, 67, [55, 59, 62], 0]),   # C, A minor, G: swells, each answered by a note; nine
         del_time=19, del_fb=45, del_all=8, rev_size=72, rev_damp=45, rev_all=18,
         mod="CHORUS", ch_rate=26, ch_depth=55, ch_mix=55, comp=35, cmp_spd=50, cmp_gain=14,
         tp_wow=52, tp_flut=20, tp_age=46, tp_fail=4,
         ch=[
             dict(wave="SAW", detune=34, cutoff=52, reso=10, env_filt=45, env_rate=70, attack=6, decay=66, sus=60, release=60,
                  nz_depth=8, nz_rate=30, dly=25, rev=35, vol=64),
             dict(wave="FM", tone=26, env_filt=40, env_rate=75, cutoff=70, attack=4, decay=64, sus=50, release=60,
                  dly=25, rev=35, vol=70),
             dict(wave="SQUARE", detune=20, cutoff=50, reso=8, attack=5, decay=60, sus=55, release=55, lfo_depth=14, lfo_rate=38,
                  lfo_shape="TRI", dly=30, rev=30, vol=56),
             dict(wave="TRI", octave=1, detune=24, cutoff=70, attack=8, decay=62, sus=55, release=60, nz_depth=10, nz_rate=35,
                  dly=35, rev=40, vol=66),
             dict(wave="SINE", octave=1, detune=18, noise=45, cutoff=72, reso=6, attack=10, decay=58, sus=45, release=60, nz_depth=12,
                  nz_rate=45, dly=40, rev=45, vol=52),
         ]),
    # CLIMBING THE ECHO: random, melodic. Bonobo: plucked and struck strings that ring out long (a harp, a nylon
    # guitar, a kalimba, a thumb piano, a music box) into a long dotted-eighth delay; the tape flutters
    dict(name="CLIMBING THE ECHO", mode="RANDOM", mvol=100,
         seq=dict(div="1/4", gate=10, steps=[[50, 57], 69, 65]),   # D minor, three quarters: a fifth struck, then two notes; the delay does the rest
         del_time=14, del_fb=62, del_all=20, rev_size=62, rev_damp=45, rev_all=12,
         mod="FLANGER", ch_rate=8, ch_depth=30, ch_mix=18, comp=45, cmp_spd=40, cmp_gain=22,
         tp_wow=34, tp_flut=30, tp_age=30,
         ch=[
             dict(wave="TRI", detune=10, cutoff=70, reso=4, env_filt=55, env_rate=40, decay=64, release=50,
                  nz_depth=6, nz_rate=30, dly=40, rev=25, vol=84),
             dict(wave="SAW", cutoff=44, reso=8, env_filt=75, env_rate=36, decay=60, release=45, nz_depth=10, nz_rate=40,
                  dly=45, rev=20, vol=74),
             dict(wave="FM", tone=20, env_filt=65, env_rate=26, cutoff=76, decay=58, release=45, dly=45, rev=25, vol=74),
             dict(wave="SINE", octave=1, detune=8, drop=4, noise=35, filt="BP", reso=10, env_filt=30, cutoff=76, decay=50, release=40,
                  sat=15, dly=50, rev=30, vol=74),
             dict(wave="FM", octave=1, tone=46, env_filt=50, env_rate=20, cutoff=84, decay=54, release=45, dly=60, rev=35, vol=54),
         ]),
    # THAT NEVER CLIMBS BACK: unison. Boards of Canada: the detuned pad from an old film reel: dull, wide, the pitch
    # swimming, the filters breathing over bars
    dict(name="THAT NEVER CLIMBS BACK", mode="UNISON", mvol=84,
         seq=dict(div="1/4", gate=56, steps=[[53, 60, 64, 69], 0, 0, [57, 60, 64, 67], 0, 0]),   # F major seven, A minor seven, held long
         del_time=29, del_fb=50, del_all=8, rev_size=92, rev_damp=45, rev_all=38,
         mod="CHORUS", ch_rate=16, ch_depth=60, ch_mix=60, comp=35, cmp_spd=70, cmp_gain=12,
         tp_wow=48, tp_flut=14, tp_age=46, tp_fail=5,
         ch=[

             dict(wave="SAW", detune=38, cutoff=42, reso=6, env_filt=12, attack=55, release=90, lfo_depth=22, lfo_rate=17,
                  lfo_sync=1, dly=20, rev=70, vol=60),
             dict(wave="TRI", on="SKIP", detune=20, cutoff=58, attack=70, release=100, dly=30, rev=80, vol=62),
             dict(wave="FM", octave=1, tone=35, env_filt=30, env_rate=90, cutoff=58, attack=65, release=100, lfo_depth=16,
                  lfo_rate=28, lfo_sync=1, lfo_shape="TRI", dly=40, rev=85, vol=40),
             dict(wave="SINE", octave=-2, noise=100, filt="BP", cutoff=66, reso=14, attack=85, release=100, lfo_depth=45,
                  lfo_rate=5, lfo_sync=1, dly=20, rev=80, vol=30),
             dict(wave="SINE", on="SKIP", octave=-1, cutoff=60, attack=60, release=100, rev=50, vol=62),
         ]),
    # GRAVITY'S LAWYER: unison, melodic. One thick keyboard made of all five (a chord sampled off an old record):
    # it speaks at once, then holds; crushed a little, soft drive, the tape wobbling and fluttering
    dict(name="GRAVITY'S LAWYER", mode="UNISON", mvol=78,
         seq=dict(div="1/8", gate=10, steps=[[52, 59, 64], 0, 52, 55, [50, 57, 62], 58]),   # E minor, D: chords hammered, the third, the tritone (3 notes x 3 layers = 9 voices)
         del_time=9, del_fb=30, del_all=4, rev_size=55, rev_damp=60, rev_all=12,
         lofi_bits=24, lofi_rate=34, lofi_mix=40, comp=45, cmp_spd=40, cmp_gain=4,
         tp_wow=46, tp_flut=32, tp_age=48, tp_fail=5,
         ch=[
             dict(wave="FM", tone=0, env_filt=60, env_rate=36, cutoff=72, decay=60, sus=45, release=45, vol=74),
             dict(wave="SAW", detune=30, cutoff=46, reso=8, env_filt=50, env_rate=40, attack=2, decay=58, sus=55, release=45,
                  nz_depth=10, nz_rate=30, vol=62),
             dict(wave="SQUARE", on="SKIP", detune=16, cutoff=42, reso=4, env_filt=35, env_rate=40, decay=56, sus=50, release=45, vol=50),
             dict(wave="SINE", on="SKIP", octave=-1, cutoff=60, decay=56, sus=60, release=40, sat=25, vol=72),
             dict(wave="TRI", octave=1, detune=14, noise=40, cutoff=66, env_filt=25, decay=50, sus=30, release=45, nz_depth=14,
                  nz_rate=50, rev=15, vol=50),
         ]),
    # BILLING MY FALLS: random. A lofi drum kit off a cassette: soft kick, rim, dusty snare (the one noisy sound),
    # closed hat, low tom; the tape drops out now and then
    dict(name="BILLING MY FALLS", mode="RANDOM", mvol=92,
         seq=dict(div="1/8", gate=10, steps=[48, 0, 48, 55, 0, 48, 51, 0, 55]),   # C minor, nine: a drum figure
         del_time=9, del_fb=25, del_all=0, rev_size=40, rev_damp=70, rev_all=6,
         drive=30, drv="SOFT", lofi_bits=22, lofi_rate=35, lofi_mix=35, comp=55, cmp_spd=18, cmp_gain=18,
         tp_wow=14, tp_flut=18, tp_age=45, tp_fail=14,
         ch=[

             dict(wave="SINE", octave=-2, cutoff=66, drop=70, decay=34, release=16, sat=35, vol=100),
             dict(wave="FM", octave=-1, tone=88, filt="BP", cutoff=72, reso=14, env_filt=90, env_rate=8, drop=15, decay=14, release=8, vol=84),
             dict(wave="TRI", octave=-1, noise=85, cutoff=72, reso=8, env_filt=40, env_rate=14, drop=25, decay=24, release=12, vol=76),
             dict(wave="SINE", octave=2, noise=100, filt="HP", cutoff=90, reso=4, decay=9, release=6, vol=34),
             dict(wave="SINE", octave=-1, cutoff=62, drop=40, decay=38, release=18, detune=10, sat=20, rev=10, vol=92),
         ]),
    # TAUGHT ME TO ORBIT: round robin. Max Cooper: slow swells whose filters turn at different note values, through a
    # slow phaser into the biggest room. Synths 2 and 5 are SILENT: their turns are rests
    dict(name="TAUGHT ME TO ORBIT", mode="RR", mvol=96,
         seq=dict(div="1/4", gate=30, steps=[55, 62, 67, 71, 74]),   # G major, five: each synth keeps its note
         del_time=29, del_fb=55, del_all=10, rev_size=100, rev_damp=30, rev_all=35,
         mod="PHASER", ch_rate=14, ch_depth=60, ch_mix=60, comp=30, cmp_spd=70, cmp_gain=10, tp_wow=28, tp_age=25,
         ch=[

             dict(wave="SINE", detune=20, cutoff=70, attack=40, release=100, lfo_depth=12, lfo_rate=28, lfo_sync=1,
                  dly=35, rev=85, vol=80),
             dict(wave="FM", on="SILENT", octave=1, tone=12, env_filt=12, cutoff=75, attack=55, release=100, dly=45, rev=90, vol=66),
             dict(wave="SINE", octave=-2, noise=100, filt="BP", cutoff=64, reso=22, attack=45, release=100,
                  lfo_depth=55, lfo_rate=40, lfo_sync=1, lfo_shape="TRI", dly=50, rev=85, vol=44),
             dict(wave="FM", tone=40, env_filt=60, env_rate=95, cutoff=60, attack=60, release=100, lfo_depth=18, lfo_rate=62,
                  lfo_sync=1, lfo_shape="SAW", dly=30, rev=90, vol=70),
             dict(wave="FM", on="SILENT", octave=2, env_filt=8, cutoff=90, attack=35, release=95, dly=60, rev=95, vol=54),
         ]),
    # UNGRASPABLE WALLS: poly. The keys: synth 1, a worn electric piano (Bonobo's chords) in chorus and tape; MIDI
    # channels 2-5: a round sub bass, a kalimba, a wobbling lead, a soft hat
    dict(name="UNGRASPABLE WALLS", mode="POLY", mvol=60,
         seq=dict(div="1/4", gate=16, steps=[[59, 62, 66], 71, [55, 59, 62], 67, [62, 66, 69], 74]),   # B minor, G, D
         del_time=14, del_fb=40, del_all=4, rev_size=62, rev_damp=50, rev_all=12,
         mod="CHORUS", ch_rate=30, ch_depth=45, ch_mix=45, comp=42, cmp_spd=40, cmp_gain=8,
         tp_wow=26, tp_flut=12, tp_age=34,
         ch=[
             dict(wave="FM", tone=0, env_filt=50, env_rate=35, cutoff=72, decay=58, sus=35, attack=1, release=40, detune=0,
                  sat=20, dly=15, rev=30, vol=84),
             dict(midi_ch=2, wave="SINE", octave=-1, cutoff=60, drop=6, decay=50, sus=60, release=22, sat=35, vol=90),
             dict(midi_ch=3, wave="FM", octave=1, tone=22, cutoff=82, env_filt=65, env_rate=16, decay=34, release=30, dly=35, rev=35, vol=72),
             dict(midi_ch=4, wave="SAW", octave=1, detune=30, cutoff=50, reso=10, env_filt=40, env_rate=40, attack=3, release=35,
                  lfo_depth=10, lfo_rate=48, lfo_shape="TRI", dly=30, rev=25, vol=66),
             dict(midi_ch=5, wave="METAL", octave=2, tone=70, noise=15, filt="HP", cutoff=84, reso=4, decay=10,
                  release=6, rev=10, vol=46),
         ]),
    # ---- sixteen more (2026-11-19), named after the user's lines. Most in the manner of Porter Robinson's Nurture
    # (bright, detuned, hopeful: soft pianos, music boxes, plucks, crunchy drums); two after Tycho (warm leads in
    # delay), some after Boards of Canada (detuned, wobbling, worn). No drive on anything played as chords.
    # CORONER'S TONGUE: Nurture: bright detuned saw plucks, five of them an octave apart in colour, pumping
    dict(name="CORONER'S TONGUE", mode="RR", mvol=100,
         seq=dict(div="1/8", gate=10, steps=[[60, 64, 67], 72, 76, [57, 60, 64], 72, 79]),   # C, A minor: chord stabs and climbing plucks
         del_time=14, del_fb=42, del_all=8, rev_size=60, rev_damp=40, rev_all=14,
         mod="CHORUS", ch_rate=34, ch_depth=45, ch_mix=45, comp=60, cmp_spd=18, cmp_gain=42, tp_wow=14, tp_age=10,
         lofi_bits=14, lofi_rate=12, lofi_mix=15,
         ch=[
             dict(wave="SAW", detune=40, cutoff=64, reso=8, env_filt=60, env_rate=30, decay=44, sus=20, release=35, dly=25, rev=25, vol=72),
             dict(wave="SAW", octave=1, detune=34, cutoff=70, reso=6, env_filt=50, env_rate=24, decay=40, sus=15, release=35, dly=30, rev=30, vol=58),
             dict(wave="SQUARE", detune=24, cutoff=60, reso=10, env_filt=65, env_rate=22, decay=40, sus=20, release=30, dly=25, rev=25, vol=60),
             dict(wave="FM", octave=1, tone=8, env_filt=60, env_rate=20, cutoff=84, decay=42, release=40, dly=35, rev=35, vol=66),
             dict(wave="SAW", detune=46, cutoff=58, reso=12, env_filt=70, env_rate=34, decay=46, sus=25, release=35, nz_depth=8,
                  nz_rate=40, dly=25, rev=25, vol=70),
         ]),
    # WRAPPED IN TIN: a toy piano and music boxes, thin and bright, a little crushed
    dict(name="WRAPPED IN TIN", mode="RANDOM", mvol=100,
         seq=dict(div="1/8", gate=8, steps=[72, 79, 76, 84, 0, 81, 79, 76, 74]),   # C major, high, nine
         del_time=9, del_fb=40, del_all=10, rev_size=50, rev_damp=30, rev_all=14,
         lofi_bits=30, lofi_rate=30, lofi_mix=30, comp=40, cmp_spd=30, cmp_gain=48, tp_wow=24, tp_flut=22, tp_age=20,
         ch=[
             dict(wave="FM", octave=1, tone=38, env_filt=70, env_rate=14, cutoff=86, decay=36, release=30, dly=30, rev=25, vol=70),
             dict(wave="FM", octave=2, tone=20, env_filt=50, env_rate=12, cutoff=90, decay=32, release=30, dly=35, rev=30, vol=56),
             dict(wave="METAL", octave=1, tone=30, filt="HP", cutoff=64, reso=6, env_filt=30, decay=30, release=26, dly=30, rev=30, vol=40),
             dict(wave="FM", octave=1, tone=62, env_filt=80, env_rate=10, cutoff=84, decay=28, release=26, dly=35, rev=25, vol=62),
             dict(wave="TRI", octave=2, filt="BP", cutoff=78, reso=14, drop=6, decay=30, release=26, dly=40, rev=30, vol=74),
         ]),
    # GRIEF GOES VIRAL: Nurture: a soft felt piano, close, with breath in it; long room
    dict(name="GRIEF GOES VIRAL", mode="RR", mvol=84,
         seq=dict(div="1/4", gate=30, steps=[[57, 64], 72, [53, 60], 69, [55, 62], 71]),   # A minor, F, G: two notes and an answer
         del_time=19, del_fb=30, del_all=4, rev_size=78, rev_damp=55, rev_all=22,
         comp=38, cmp_spd=50, cmp_gain=16, tp_wow=30, tp_flut=14, tp_age=38,
         ch=[
             dict(wave="FM", tone=0, env_filt=45, env_rate=30, cutoff=66, attack=1, decay=64, sus=20, release=55, rev=30, vol=84),
             dict(wave="FM", tone=4, env_filt=40, env_rate=36, cutoff=62, attack=1, decay=66, sus=20, release=55, noise=10, rev=30, vol=82),
             dict(wave="TRI", detune=6, cutoff=62, env_filt=40, env_rate=30, attack=1, decay=62, sus=15, release=55, rev=30, vol=86),
             dict(wave="FM", tone=0, env_filt=55, env_rate=24, cutoff=70, attack=1, decay=60, sus=20, release=55, nz_depth=6,
                  nz_rate=30, rev=30, vol=80),
             dict(wave="SINE", detune=4, noise=22, cutoff=58, env_filt=30, attack=2, decay=60, sus=15, release=55, rev=35, vol=84),
         ]),
    # BEFORE IT BEGINS: unison of three: a wide saw pad that rises slowly, through a phaser
    dict(name="BEFORE IT BEGINS", mode="UNISON", mvol=80,
         seq=dict(div="1/4", gate=50, steps=[[60, 64, 67, 71], 0, 0, [57, 60, 64, 67], 0, 0]),   # C major seven, A minor seven
         del_time=29, del_fb=45, del_all=8, rev_size=90, rev_damp=35, rev_all=34,
         mod="PHASER", ch_rate=10, ch_depth=55, ch_mix=45, comp=35, cmp_spd=70, cmp_gain=10, tp_wow=20, tp_age=15,
         ch=[
             dict(wave="SAW", detune=44, cutoff=50, reso=6, env_filt=40, env_rate=95, attack=45, release=85, lfo_depth=14,
                  lfo_rate=17, lfo_sync=1, dly=20, rev=60, vol=64),
             dict(wave="SAW", octave=1, detune=36, cutoff=46, reso=4, env_filt=45, env_rate=100, attack=60, release=90, dly=30, rev=70, vol=44),
             dict(wave="SQUARE", on="SKIP", detune=24, cutoff=44, attack=50, release=85, rev=60, vol=50),
             dict(wave="TRI", octave=-1, detune=10, cutoff=60, attack=40, release=85, rev=40, vol=70),
             dict(wave="FM", on="SKIP", octave=1, tone=20, cutoff=70, attack=55, release=90, rev=70, vol=44),
         ]),
    # LANTERNS OF STATIC: Boards of Canada: every voice has static in it, and static moves the filters; worn tape
    dict(name="LANTERNS OF STATIC", mode="RR", mvol=86,
         seq=dict(div="1/4", gate=24, steps=[[50, 57, 62], 69, 65, [46, 53, 58], 72, 64]),   # D minor, B flat: chords, the static between
         del_time=19, del_fb=48, del_all=10, rev_size=70, rev_damp=55, rev_all=18,
         mod="CHORUS", ch_rate=14, ch_depth=50, ch_mix=40, comp=40, cmp_spd=50, cmp_gain=16,
         tp_wow=50, tp_flut=24, tp_age=55, tp_fail=10,
         ch=[
             dict(wave="SAW", detune=28, noise=28, cutoff=48, reso=10, env_filt=30, attack=4, decay=60, sus=50, release=55,
                  nz_depth=24, nz_rate=45, dly=25, rev=30, vol=66),
             dict(wave="TRI", detune=18, noise=35, cutoff=60, reso=6, attack=6, decay=60, sus=55, release=55, nz_depth=18,
                  nz_rate=30, dly=25, rev=35, vol=76),
             dict(wave="SQUARE", detune=14, noise=24, filt="BP", cutoff=56, reso=14, attack=5, decay=58, sus=50, release=55,
                  nz_depth=30, nz_rate=60, dly=30, rev=30, vol=74),
             dict(wave="FM", tone=30, noise=20, env_filt=35, env_rate=60, cutoff=64, attack=4, decay=60, sus=40, release=55,
                  nz_depth=14, nz_rate=35, dly=30, rev=35, vol=70),
             dict(wave="SINE", octave=1, detune=20, noise=45, cutoff=64, reso=8, attack=8, decay=56, sus=45, release=55,
                  nz_depth=20, nz_rate=50, dly=35, rev=40, vol=56),
         ]),
    # HUNG FROM THE PEWS: a small church organ: stops of square, triangle and sine, turning in a fast chorus
    dict(name="HUNG FROM THE PEWS", mode="RANDOM", mvol=62,
         seq=dict(div="1/4", gate=19, steps=[[53, 60, 65], [55, 62, 67], [57, 60, 64]]),   # F, G, A minor
         del_time=14, del_fb=25, del_all=0, rev_size=95, rev_damp=40, rev_all=30,
         mod="CHORUS", ch_rate=62, ch_depth=28, ch_mix=45, comp=35, cmp_spd=60, cmp_gain=10, tp_wow=16, tp_age=24,
         ch=[
             dict(wave="SQUARE", cutoff=56, reso=2, attack=3, decay=50, sus=85, release=30, rev=30, vol=62),
             dict(wave="TRI", detune=8, cutoff=70, attack=3, decay=50, sus=90, release=30, rev=30, vol=84),
             dict(wave="SINE", octave=1, detune=6, cutoff=80, attack=4, decay=50, sus=85, release=30, rev=35, vol=62),
             dict(wave="SQUARE", detune=10, cutoff=48, reso=4, attack=3, decay=50, sus=85, release=30, noise=6, rev=30, vol=60),
             dict(wave="SINE", octave=-1, detune=4, cutoff=70, attack=3, decay=50, sus=90, release=30, sat=15, rev=25, vol=74),
         ]),
    # PULPIT'S A MIRROR: Tycho: a warm lead that sings in a dotted-eighth delay; guitar-like plucks beside it
    dict(name="PULPIT'S A MIRROR", mode="RR", mvol=96,
         seq=dict(div="1/8", gate=14, steps=[64, 71, 67, 76, 74, 71, 0, 69, 66]),   # E minor, nine
         del_time=14, del_fb=55, del_all=22, rev_size=65, rev_damp=45, rev_all=14,
         mod="CHORUS", ch_rate=24, ch_depth=40, ch_mix=40, comp=40, cmp_spd=40, cmp_gain=30, tp_wow=22, tp_flut=8, tp_age=22,
         ch=[
             dict(wave="SAW", detune=14, cutoff=52, reso=12, env_filt=50, env_rate=50, attack=2, decay=56, sus=50, release=40,
                  lfo_depth=6, lfo_rate=46, dly=40, rev=20, vol=70),
             dict(wave="TRI", detune=8, cutoff=66, reso=4, env_filt=60, env_rate=26, decay=46, release=36, dly=40, rev=25, vol=84),
             dict(wave="SQUARE", detune=10, cutoff=50, reso=8, env_filt=45, env_rate=40, attack=2, decay=54, sus=40, release=40,
                  dly=40, rev=20, vol=62),
             dict(wave="SAW", cutoff=46, reso=10, env_filt=75, env_rate=28, decay=44, release=34, nz_depth=6, nz_rate=30,
                  dly=45, rev=20, vol=74),
             dict(wave="SINE", octave=1, detune=10, cutoff=80, attack=3, decay=50, sus=35, release=40, dly=50, rev=30, vol=58),
         ]),
    # LAUGHING AT YOU: Nurture: little chopped voices: resonant bandpass chirps that fall onto their note
    dict(name="LAUGHING AT YOU", mode="RANDOM", mvol=100,
         seq=dict(div="1/8", gate=9, steps=[72, 76, 79, 0, 81, 79]),   # C major, high
         del_time=9, del_fb=45, del_all=12, rev_size=55, rev_damp=35, rev_all=14,
         mod="FLANGER", ch_rate=16, ch_depth=30, ch_mix=20, comp=45, cmp_spd=25, cmp_gain=44, tp_wow=18, tp_age=10,
         lofi_bits=16, lofi_rate=18, lofi_mix=20,
         ch=[
             dict(wave="SAW", octave=1, filt="BP", cutoff=70, reso=24, env_filt=40, env_rate=16, drop=14, decay=30, sus=15,
                  release=24, dly=30, rev=25, vol=84),
             dict(wave="SQUARE", octave=1, filt="BP", cutoff=76, reso=26, env_filt=30, env_rate=12, drop=22, decay=26, release=22,
                  dly=35, rev=25, vol=84),
             dict(wave="SAW", filt="BP", cutoff=64, reso=22, lfo_depth=30, lfo_rate=84, lfo_sync=1, lfo_shape="TRI", drop=10,
                  decay=34, sus=20, release=24, dly=30, rev=25, vol=84),
             dict(wave="FM", octave=1, tone=16, filt="BP", cutoff=74, reso=18, env_filt=50, env_rate=14, drop=18, decay=28,
                  release=22, dly=35, rev=25, vol=80),
             dict(wave="TRI", octave=2, noise=20, filt="BP", cutoff=82, reso=20, drop=30, decay=20, release=18, dly=45, rev=30, vol=74),
         ]),
    # BALLOT OF RUMORS: Tycho: plucked bass and guitar figures, the filters turning with the tempo
    dict(name="BALLOT OF RUMORS", mode="RR", mvol=100,
         seq=dict(div="1/8", gate=11, steps=[45, 57, 52, 60, 57, 64]),   # A minor, low to high
         del_time=14, del_fb=50, del_all=14, rev_size=58, rev_damp=50, rev_all=10,
         mod="FLANGER", ch_rate=6, ch_depth=40, ch_mix=22, comp=45, cmp_spd=30, cmp_gain=38, tp_wow=16, tp_age=20,
         ch=[
             dict(wave="SAW", octave=-1, cutoff=46, reso=12, env_filt=70, env_rate=24, decay=40, sus=20, release=26, sat=25, vol=84),
             dict(wave="SQUARE", cutoff=50, reso=14, env_filt=50, env_rate=20, decay=38, release=30, lfo_depth=24, lfo_rate=62,
                  lfo_sync=1, lfo_shape="TRI", dly=35, rev=15, vol=66),
             dict(wave="TRI", detune=8, cutoff=68, env_filt=55, env_rate=22, decay=42, release=32, dly=40, rev=20, vol=86),
             dict(wave="SAW", detune=16, cutoff=48, reso=10, env_filt=60, env_rate=30, decay=44, sus=15, release=32,
                  lfo_depth=20, lfo_rate=50, lfo_sync=1, lfo_shape="SAW", dly=40, rev=20, vol=68),
             dict(wave="FM", octave=1, tone=10, env_filt=55, env_rate=16, cutoff=82, decay=36, release=32, dly=50, rev=25, vol=60),
         ]),
    # SIGNED IN MILK: a lullaby: round sines and triangles, soft bells, thick chorus
    dict(name="SIGNED IN MILK", mode="RR", mvol=88,
         seq=dict(div="1/4", gate=22, steps=[[53, 57, 60], 72, 69, [50, 53, 57], 76, 72]),   # F, D minor: a lullaby, chord and tune
         del_time=19, del_fb=35, del_all=6, rev_size=75, rev_damp=55, rev_all=20,
         mod="CHORUS", ch_rate=22, ch_depth=65, ch_mix=65, comp=32, cmp_spd=55, cmp_gain=12, tp_wow=26, tp_flut=10, tp_age=30,
         ch=[
             dict(wave="SINE", detune=12, cutoff=72, attack=3, decay=58, sus=35, release=55, rev=30, vol=86),
             dict(wave="TRI", detune=10, cutoff=60, attack=4, decay=60, sus=40, release=55, rev=30, vol=84),
             dict(wave="FM", octave=1, tone=4, env_filt=30, env_rate=30, cutoff=74, attack=1, decay=54, release=55, dly=20, rev=35, vol=60),
             dict(wave="SINE", octave=1, detune=8, cutoff=76, attack=5, decay=56, sus=30, release=55, dly=20, rev=35, vol=62),
             dict(wave="TRI", octave=-1, detune=6, cutoff=62, attack=3, decay=60, sus=45, release=50, rev=25, vol=84),
         ]),
    # VELVET THE MOTIVE: Boards of Canada: unison of three: a brassy detuned lead whose filter opens after the note
    dict(name="VELVET THE MOTIVE", mode="UNISON", mvol=66,
         seq=dict(div="1/4", gate=17, steps=[57, 0, 64, 62, 0, 60, 59, 0, 55]),   # A minor, nine, a slow tune
         del_time=19, del_fb=45, del_all=12, rev_size=70, rev_damp=50, rev_all=16,
         mod="CHORUS", ch_rate=18, ch_depth=45, ch_mix=40, comp=38, cmp_spd=50, cmp_gain=10,
         tp_wow=55, tp_flut=18, tp_age=44, tp_fail=4,
         ch=[
             dict(wave="SAW", detune=36, cutoff=40, reso=10, env_filt=60, env_rate=85, attack=5, decay=64, sus=60, release=45,
                  nz_depth=8, nz_rate=30, vol=70),
             dict(wave="SQUARE", detune=22, cutoff=38, reso=6, env_filt=50, env_rate=90, attack=6, decay=64, sus=55, release=45, vol=56),
             dict(wave="SAW", on="SKIP", octave=1, detune=30, cutoff=40, env_filt=50, env_rate=90, attack=6, decay=60, sus=45, release=45, vol=40),
             dict(wave="TRI", octave=-1, cutoff=60, attack=4, decay=60, sus=60, release=40, vol=70),
             dict(wave="FM", on="SKIP", tone=20, cutoff=64, env_filt=40, env_rate=70, decay=60, sus=40, release=45, vol=56),
         ]),
    # GOSPEL THE GUILT: electric pianos and a small choir of slow bandpassed saws; a hall
    dict(name="GOSPEL THE GUILT", mode="RANDOM", mvol=80,
         seq=dict(div="1/4", gate=28, steps=[[55, 59, 62], 67, [52, 55, 59], 64, [48, 55, 60], 67]),   # G, E minor, C
         del_time=19, del_fb=30, del_all=4, rev_size=92, rev_damp=45, rev_all=26,
         mod="CHORUS", ch_rate=28, ch_depth=40, ch_mix=40, comp=38, cmp_spd=55, cmp_gain=14, tp_wow=24, tp_flut=10, tp_age=28,
         ch=[
             dict(wave="FM", tone=0, env_filt=55, env_rate=34, cutoff=72, attack=1, decay=60, sus=40, release=45, sat=15, rev=30, vol=82),
             dict(wave="SAW", detune=26, filt="BP", cutoff=60, reso=12, attack=22, decay=60, sus=70, release=60, lfo_depth=8,
                  lfo_rate=30, rev=45, vol=74),
             dict(wave="FM", tone=6, env_filt=45, env_rate=40, cutoff=70, attack=1, decay=62, sus=40, release=45, rev=30, vol=80),
             dict(wave="SAW", octave=1, detune=20, filt="BP", cutoff=66, reso=14, attack=28, decay=60, sus=65, release=60, noise=8,
                  rev=50, vol=62),
             dict(wave="TRI", detune=10, cutoff=64, env_filt=30, attack=2, decay=60, sus=50, release=45, rev=30, vol=84),
         ]),
    # FINGERTIPS COUNTING: made for the arpeggiator (it is on: UP): crisp, short, each synth another colour
    dict(name="FINGERTIPS COUNTING", mode="RR", mvol=100, arp=1,
         seq=dict(div="1/8", gate=8, steps=[60, 64, 67, 71, 72, 71, 67, 64, 62]),   # C major seven, nine
         del_time=14, del_fb=50, del_all=14, rev_size=55, rev_damp=40, rev_all=12,
         comp=50, cmp_spd=20, cmp_gain=48, tp_wow=12, tp_age=12, lofi_bits=12, lofi_rate=10, lofi_mix=12,
         ch=[
             dict(wave="SAW", detune=20, cutoff=56, reso=12, env_filt=70, env_rate=16, decay=28, release=20, dly=30, rev=20, vol=74),
             dict(wave="FM", octave=1, tone=10, env_filt=65, env_rate=12, cutoff=84, decay=26, release=20, dly=35, rev=25, vol=66),
             dict(wave="SQUARE", filt="BP", cutoff=64, reso=16, env_filt=50, env_rate=14, decay=26, release=18, dly=30, rev=20, vol=86),
             dict(wave="TRI", octave=1, detune=10, cutoff=76, env_filt=40, env_rate=14, decay=28, release=20, dly=35, rev=25, vol=80),
             dict(wave="FM", tone=44, env_filt=75, env_rate=12, cutoff=74, decay=24, release=18, dly=30, rev=20, vol=70),
         ]),
    # DOORS MADE OF HUM: drones: low hums that come up slowly and beat against each other
    dict(name="DOORS MADE OF HUM", mode="RR", mvol=72,
         seq=dict(div="1/4", gate=60, steps=[[38, 45], 0, 0, [41, 48], 0, 0]),   # D and A, F and C: fifths, held
         del_time=29, del_fb=50, del_all=8, rev_size=95, rev_damp=50, rev_all=30,
         mod="PHASER", ch_rate=6, ch_depth=50, ch_mix=40, comp=35, cmp_spd=80, cmp_gain=12, tp_wow=30, tp_age=30,
         ch=[
             dict(wave="SINE", octave=-1, detune=14, cutoff=64, attack=40, release=90, sat=25, rev=40, vol=88),
             dict(wave="TRI", octave=-1, detune=22, cutoff=54, attack=50, release=95, lfo_depth=16, lfo_rate=17, lfo_sync=1, rev=45, vol=84),
             dict(wave="SAW", octave=-1, detune=30, cutoff=36, reso=10, attack=55, release=95, lfo_depth=22, lfo_rate=5,
                  lfo_sync=1, rev=45, vol=64),
             dict(wave="SQUARE", detune=18, filt="BP", cutoff=46, reso=14, noise=12, attack=60, release=95, lfo_depth=26,
                  lfo_rate=28, lfo_sync=1, lfo_shape="TRI", rev=50, vol=62),
             dict(wave="FM", tone=50, env_filt=30, env_rate=100, cutoff=52, attack=45, release=95, rev=50, vol=60),
         ]),
    # KNOCKING IN LATIN: wood: marimba, kalimba, wood block, a log drum, a thumb piano; the arpeggiator jumps about
    dict(name="KNOCKING IN LATIN", mode="RANDOM", mvol=96, arp=4,
         seq=dict(div="1/8", gate=9, steps=[57, 60, 64, 0, 62, 69, 67, 0, 64]),   # A minor, nine
         del_time=9, del_fb=35, del_all=8, rev_size=45, rev_damp=60, rev_all=10,
         comp=45, cmp_spd=25, cmp_gain=24, tp_wow=16, tp_flut=12, tp_age=26,
         ch=[
             dict(wave="SINE", drop=10, env_filt=30, cutoff=74, decay=36, release=26, detune=6, sat=20, rev=20, vol=88),
             dict(wave="FM", octave=1, tone=22, env_filt=70, env_rate=14, cutoff=80, decay=30, release=22, dly=25, rev=20, vol=72),
             dict(wave="SQUARE", octave=1, filt="BP", cutoff=62, reso=20, env_filt=50, env_rate=8, drop=18, decay=14, release=10, rev=15, vol=90),
             dict(wave="SINE", octave=-1, drop=30, cutoff=66, decay=34, release=20, sat=30, rev=10, vol=92),
             dict(wave="FM", tone=34, env_filt=60, env_rate=18, cutoff=72, decay=34, release=24, dly=20, rev=20, vol=76),
         ]),
    # ANSWERED IN DRUM: Nurture's drums: a punchy kick, a crunchy clap, a tight hat, a tom, a rim; crushed and driven
    dict(name="ANSWERED IN DRUM", mode="RANDOM", mvol=84,
         seq=dict(div="1/8", gate=9, steps=[48, 0, 55, 48, 0, 52]),   # a six-step drum figure
         del_time=9, del_fb=25, del_all=0, rev_size=40, rev_damp=60, rev_all=8,
         drive=35, drv="SOFT", lofi_bits=28, lofi_rate=30, lofi_mix=40, comp=62, cmp_spd=14, cmp_gain=18, tp_age=14,
         ch=[
             dict(wave="SINE", octave=-2, drop=85, cutoff=70, decay=30, release=14, sat=45, vol=100),
             dict(wave="TRI", noise=100, filt="BP", cutoff=72, reso=10, env_filt=30, env_rate=10, decay=22, release=14,
                  nz_depth=30, nz_rate=95, rev=15, vol=64),
             dict(wave="SINE", octave=2, noise=100, filt="HP", cutoff=92, reso=4, decay=8, release=5, vol=34),
             dict(wave="SINE", octave=-1, drop=45, cutoff=64, decay=34, release=16, detune=8, sat=25, rev=10, vol=92),
             dict(wave="FM", tone=80, filt="BP", cutoff=74, reso=14, env_filt=90, env_rate=8, drop=12, decay=12, release=8, vol=84),
         ]),
]


def chan(d):
    c = dict(CH_DEF)
    bad = set(d) - set(c)
    assert not bad, bad
    c.update(d)
    bits = FILT[c["filt"]] | SHAPE[c["lfo_shape"]] << 2 | (0x80 if c["lfo_sync"] else 0)
    return ("{" + f".midi_ch={c['midi_ch']}, .enabled={ON[c['on']]}, .wave={WAVE[c['wave']]}, .octave={c['octave']}, "
            f".noise={c['noise']}, .env_filt={c['env_filt']}, .cutoff={c['cutoff']}, .reso={c['reso']}, "
            f".attack={c['attack']}, .release={c['release']}, .dly={c['dly']}, .rev={c['rev']}, .vol={c['vol']},\n"
            f"         .tone={c['tone']}, .drop={c['drop']}, .decay={c['decay']}, .detune={c['detune']}, "
            f".env_rate={c['env_rate']}, .lfo_depth={c['lfo_depth']}, .lfo_rate={c['lfo_rate']}, .lfo_bits={bits}, "
            f".nz_depth={c['nz_depth']}, .nz_rate={c['nz_rate']}, .sat={c['sat']}, .sus={c['sus']}" + "}")


out = ["/* SPDX-License-Identifier: GPL-3.0-only */",
       "/* Generated by tools/gen_poly5_patches.py: do not edit. */",
       "static const char *const FACTORY_NAME[] = {" + ", ".join('"%s"' % p["name"] for p in PATCHES) + "};",
       "static const preset_t FACTORY[] = {"]
for p in PATCHES:
    f = dict(FX_DEF)
    bad = set(p) - set(f) - {"name", "ch", "seq"}
    assert not bad, bad
    assert len(p["ch"]) == 5
    f.update({k: v for k, v in p.items() if k not in ("name", "ch", "seq")})
    out.append(f"    /* {p['name']} */")
    out.append("    {.name = \"\", .ch = {\n        " + ",\n        ".join(chan(c) for c in p["ch"]) + "},")
    out.append(f"     .mode={MODE[f['mode']]}, .mvol={f['mvol']}, .dj={f['dj'] & 255}, .del_time={f['del_time']}, "
               f".del_fb={f['del_fb']}, .del_all={f['del_all']}, .rev_size={f['rev_size']}, .rev_damp={f['rev_damp']}, "
               f".rev_all={f['rev_all']}, .drive={f['drive']}, .dj_reso={f['dj_reso']}, .comp={f['comp']},")
    out.append(f"     .ch_type={MOD[f['mod']]}, .ch_rate={f['ch_rate']}, .ch_depth={f['ch_depth']}, .ch_mix={f['ch_mix']}, "
               f".drv_type={DRV[f['drv']]}, .lofi_bits={f['lofi_bits']}, .lofi_rate={f['lofi_rate']}, "
               f".lofi_mix={f['lofi_mix']}, .cmp_spd={f['cmp_spd']}, .cmp_gain={f['cmp_gain']},")
    out.append(f"     .tp_wow={f['tp_wow']}, .tp_flut={f['tp_flut']}, .tp_age={f['tp_age']}, .tp_fail={f['tp_fail']}, .arp={f['arp']}" + "},")
out.append("};")
Path(__file__).resolve().parent.parent.joinpath("firmware/gen/poly5_patches.h").write_text("\n".join(out) + "\n")
print("firmware/gen/poly5_patches.h written")

# the patches' sequences (FSEQ): up to nine steps of up to four notes; div 1/4 or 1/8; gate in twentieths of a step
DIV = {"1/4": 0, "1/8": 1}
sq = ["/* SPDX-License-Identifier: GPL-3.0-only */", "/* Generated by tools/gen_poly5_patches.py: do not edit. */",
      "static const fseq_t FSEQ[] = {"]
for p in PATCHES:
    q = p["seq"]; st = [x if isinstance(x, list) else [x] for x in q["steps"]]
    assert 1 <= len(st) <= 9 and all(len(x) <= 4 for x in st), p["name"]
    sq.append(f"    {{{len(st)}, {DIV[q['div']]}, {q['gate']}, {q.get('bpm', 118)}, {{" + ", ".join("{" + ", ".join(map(str, x)) + "}" for x in st) + f"}}}},   /* {p['name']} */")
sq.append("};")
Path(__file__).resolve().parent.parent.joinpath("firmware/gen/poly5_seqs.h").write_text("\n".join(sq) + "\n")
print(len(PATCHES), "patches; firmware/gen/poly5_seqs.h written")
