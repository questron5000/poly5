#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read the patch that is sounding on an FM-1 running POLY5 (edited or not, saved or not), with its sequence, over
USB-MIDI, and print it as an entry for tools/gen_poly5_patches.py.
  python3 tools/poly5_read.py [--port NAME] [--name "PATCH NAME"]      (needs mido and python-rtmidi)
A text log of the simulation's MIDI out works too: python3 tools/poly5_read.py --log FILE"""
import argparse, re, sys, time

CH = ["midi_ch", "on", "wave", "octave", "noise", "env_filt", "cutoff", "reso", "attack", "release", "dly", "rev", "vol",
      "tone", "drop", "decay", "detune", "env_rate", "lfo_depth", "lfo_rate", "lfo_bits", "nz_depth", "nz_rate", "sat", "sus"]
FX = ["mode", "mvol", "dj", "del_time", "del_fb", "del_all", "rev_size", "rev_damp", "rev_all", "drive", "dj_reso", "comp",
      "mod", "ch_rate", "ch_depth", "ch_mix", "drv", "lofi_bits", "lofi_rate", "lofi_mix", "cmp_spd", "cmp_gain",
      "tp_wow", "tp_flut", "tp_age", "tp_fail", "fx_off", "arp"]
WAVE = ["SAW", "SQUARE", "TRI", "SINE", "FM", "METAL"]; ON = ["SKIP", "ON", "SILENT"]; FILT = ["LP", "BP", "HP", "LP"]
SHAPE = ["SINE", "TRI", "SAW", "SQUARE"]; MODE = ["POLY", "RR", "UNISON", "RANDOM"]; MOD = ["OFF", "CHORUS", "FLANGER", "PHASER"]
DRV = ["SOFT", "HARD", "FOLD"]
CH_DEF = dict(midi_ch=1, on="ON", wave="SAW", octave=0, noise=0, env_filt=0, cutoff=100, reso=2, attack=0, release=20,
              dly=0, rev=0, vol=80, tone=0, drop=0, decay=0, detune=0, env_rate=0, lfo_depth=0, lfo_rate=30,
              filt="LP", lfo_shape="SINE", lfo_sync=0, nz_depth=0, nz_rate=50, sat=0, sus=0)


def decode(raw, npre, nseq, name):
    assert len(raw) == npre + nseq and npre == 16 + 5 * len(CH) + len(FX) + 2, (len(raw), npre, nseq)
    out = []
    chans = []
    for c in range(5):
        b = raw[16 + c * 25: 16 + c * 25 + 25]; d = dict(zip(CH, b))
        d["octave"] = d["octave"] - 256 if d["octave"] > 127 else d["octave"]
        bits = d.pop("lfo_bits")
        d.update(on=ON[d["on"] % 3], wave=WAVE[d["wave"] % 6], filt=FILT[bits & 3], lfo_shape=SHAPE[bits >> 2 & 3], lfo_sync=bits >> 7)
        chans.append(d)
    f = dict(zip(FX, raw[16 + 125:]))
    f["dj"] = f["dj"] - 256 if f["dj"] > 127 else f["dj"]
    f.update(mode=MODE[f["mode"] % 4], mod=MOD[f["mod"] % 4], drv=DRV[f["drv"] % 3])
    fx_off = f.pop("fx_off")
    s = raw[npre:]; ln, div, swing, bpm, gate = s[0], s[1], s[2], s[3], s[4]
    steps = []
    for i in range(ln):
        n = [x for x in s[8 + i * 4: 12 + i * 4] if x]
        steps.append(n[0] if len(n) == 1 else n if n else 0)
    out.append(f'    dict(name="{name}", ' + ", ".join(f"{k}={v!r}".replace("'", '"') for k, v in f.items()) + ",")
    out.append(f'         seq=dict(div="{["1/4", "1/8", "1/16", "1/32"][div] if div < 4 else div}", gate={gate}, bpm={bpm}, swing={swing}, steps={steps}),')
    out.append("         ch=[")
    for d in chans:
        out.append("             dict(" + ", ".join(f"{k}={v!r}".replace("'", '"') for k, v in d.items() if CH_DEF.get(k) != v) + "),")
    out.append("         ]),")
    if fx_off:
        out.append(f"    # effects switched off on the device (bits): {fx_off:#x}")
    return "\n".join(out)


def from_events(ev):
    """ev: (status, d1, d2) in order -> (bytes, npre, nseq)"""
    raw = []; npre = nseq = None; done = False
    for st, d1, d2 in ev:
        if st == 0xBF and d1 == 117: npre = d2 if d2 else None; raw = []
        elif st == 0xBF and d1 == 116: nseq = d2
        elif st == 0xEE: raw.append(d1 | d2 << 7)
        elif st == 0xBF and d1 == 118 and d2 == 127: done = True
    return raw, npre, nseq, done


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=None); ap.add_argument("--name", default="FROM THE DEVICE"); ap.add_argument("--log")
    a = ap.parse_args()
    if a.log:
        ev = [(int(m[0], 16), int(m[1]), int(m[2])) for m in re.findall(r"MIDI out ([0-9A-F]{2}) (\d+) (\d+)", open(a.log).read())]
    else:
        import mido
        names = mido.get_output_names()
        name = [n for n in names if (a.port or "FM-1").lower() in n.lower() or (not a.port and "poly5" in n.lower())]
        if not name: sys.exit(f"no FM-1 among {names}")
        o = mido.open_output(name[0]); i = mido.open_input(name[0]); time.sleep(0.3)
        for _ in i.iter_pending(): pass
        o.send(mido.Message("control_change", channel=0, control=119, value=3))
        ev = []; t0 = time.time()
        while time.time() - t0 < 45.0:   # (the device sends a byte a pass of its main loop: some 15 a second while it draws)
            for m in i.iter_pending():
                b = m.bytes()
                if len(b) == 3: ev.append(tuple(b))
            if ev and ev[-1] == (0xBF, 118, 127): break
            time.sleep(0.01)
    raw, npre, nseq, done = from_events(ev)
    # the firmware sends sizes modulo 128 (7 bits): the preset is 171 bytes
    if npre is not None and npre < 128 and len(raw) > 200: npre += 128
    if nseq is not None and len(raw) - (npre or 0) > nseq: nseq = len(raw) - npre
    if not done or npre is None: sys.exit(f"no full read-out ({len(raw)} bytes): this firmware may be older than the read-out")
    print(decode(raw, npre, nseq, a.name))
