#!/usr/bin/env python3
"""The webpage's UI loops: short silent videos of POLY5's screens, made from the firmware (Dinghy's host mode).
    python3 site/make_loops.py [name ...]   -> site/img/loop-<name>.mp4 (480 px, 30 fps)
Each script starts at switch-on; the loop is cut from START to END ms."""
import subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DINGHY = ROOT.parent / "dinghy"
IMG = ROOT / "img"
FPS = 30


def ev(t, *a):
    return f"{t} " + " ".join(str(x) for x in a)


def notes(t0, keys, step, gate=0.8):
    out, t = [], t0
    for k in keys:
        out.append(ev(t, "keys", hex(k)))
        out.append(ev(t + int(step * gate), "keys", 0))
        t += step
    return out


A3, C4, E4, G4, A4, C5, E5, D4, F4 = (1 << 4, 1 << 7, 1 << 11, 1 << 14, 1 << 16, 1 << 19, 1 << 23, 1 << 9, 1 << 12)
LINE = [A3, C4, E4, G4, A4, G4, E4, C4]

def loops():
    L = {}
    # RANDOM play mode: a line scattering over the synths
    s = [ev(1500, "btndown", 10), ev(2300, "btnup", 10)] + notes(2600, LINE * 4, 220)
    L["random"] = (s, 2600, 8600)
    # the RANDOM arpeggiator on a held chord
    s = [ev(1500 + i * 250, "btn", 10) for i in range(4)] + [ev(2600, "keys", hex(A3 | C4 | E4 | G4)), ev(9500, "keys", 0)]
    L["arp-random"] = (s, 3000, 9000)
    # a synth's page: its wave changes as SELECT turns
    s = [ev(1500, "btn", 2)] + notes(1900, [A3, E4] * 18, 400, 0.85) + [ev(2700 + i * 1200, "enc", 0, 1) for i in range(6)]
    L["synth-page"] = (s, 2200, 9400)
    # LOFI: the tape loop running with the sequence; wow up and back, then a little failing
    s = [ev(1500, "enc", 1, 7), ev(1600, "btn", 12)]
    s += [ev(3000 + i * 60, "enc", 2, 1) for i in range(20)] + [ev(5500 + i * 60, "enc", 2, -1) for i in range(20)]
    s += [ev(7000 + i * 50, "enc", 5, 1) for i in range(25)] + [ev(9000 + i * 30, "enc", 5, -1) for i in range(25)]
    L["lofi"] = (s, 2500, 10000)
    # the filter: down to lowpass, up through open to highpass, back
    s = [ev(1500, "btn", 12)]
    t = 2300
    for d, n in ((-1, 32), (1, 64), (-1, 32)):
        for _ in range(n):
            s.append(ev(t, "enc", 2, d)); t += 70
    L["filter"] = (s, 2400, t + 300)
    # the delay: each note and its echoes
    s = [ev(1500 + i * 30, "enc", 4, 1) for i in range(22)] + notes(2300, [A3, E4, C4, G4, A4, E4, C4, E4, G4, C5], 620, 0.5)
    s += [ev(3300 + i * 1500, "enc", 4, (-1) ** i) for i in range(5)]
    L["delay"] = (s, 2400, 8600)
    # the sequencer playing (six steps of 1/8 at 118: four passes)
    s = [ev(1500, "btn", 11), ev(1600, "btn", 12)]
    L["sequencer"] = (s, 2618, 2618 + 4 * 1525)
    # the save page: the slot and the name's words turn
    s = [ev(1500, "btn", 9)] + [ev(2400 + i * 500, "enc", 2, 1) for i in range(3)] + [ev(4000 + i * 450, "enc", 4, 1) for i in range(4)]
    s += [ev(6000 + i * 450, "enc", 5, 1) for i in range(4)]
    L["save"] = (s, 2000, 8200)
    # switch-on, then a chord in round robin
    s = [ev(1700, "keys", hex(A3)), ev(1800, "keys", hex(A3 | C4)), ev(1900, "keys", hex(A3 | C4 | E4)), ev(2000, "keys", hex(A3 | C4 | E4 | G4)), ev(3400, "keys", 0)]
    L["intro"] = (s, 0, 4600)
    return L


def main(names):
    sim = Path(tempfile.mkdtemp()) / "poly5"       # its own build: never the one a manual render is running
    subprocess.run(["cc", "-O2", "-Wall", "-Wno-unused-function", "-DDINGHY_HOST", "-Dsection(x)=used", '-DAPP_VER="loops"',
                    "-Ifirmware/hal", "-Ifirmware/src", "-Ifirmware/gen", "-o", str(sim), "firmware/src/poly5.c"], check=True, cwd=DINGHY)
    for name, (script, t0, t1) in loops().items():
        if names and name not in names:
            continue
        with tempfile.TemporaryDirectory() as d:
            lines = sorted(script, key=lambda l: int(l.split()[0])) + [f"{t1 + 100} end"]
            (Path(d) / "s.txt").write_text("\n".join(lines) + "\n")
            subprocess.run([str(sim), f"{d}/f", f"{d}/s.txt", str(FPS)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=DINGHY)
            f0, n = t0 * FPS // 1000, (t1 - t0) * FPS // 1000
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(FPS), "-start_number", str(f0), "-i", f"{d}/f-%05d.ppm",
                            "-frames:v", str(n), "-vf", "scale=480:480:flags=neighbor", "-an", "-c:v", "libx264", "-preset", "slow",
                            "-crf", "25", "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(IMG / f"loop-{name}.mp4")], check=True)
            print(f"img/loop-{name}.mp4  {(t1 - t0) / 1000:.1f} s")


if __name__ == "__main__":
    main(sys.argv[1:])
