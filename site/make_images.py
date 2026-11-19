#!/usr/bin/env python3
"""The webpage's pictures, made from the firmware (Dinghy's host mode) and the manual's panel drawing.
    python3 site/make_images.py   -> site/img/*.png"""
import subprocess, sys, tempfile
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent / "manual"))
import manual  # noqa: E402

IMG = ROOT / "img"
SHOTS = {  # ms -> name
    900: "intro", 2900: "home-chord", 5500: "random", 7700: "delay", 10700: "synth-page",
    14000: "lofi", 16500: "sequencer", 18000: "save", 19900: "filter", 25300: "arp-random",
}
SCRIPT = """
900 shot
2500 keys 0x4890
2900 shot
3500 keys 0
4000 btndown 10
4800 btnup 10
5100 keys 0x10
5300 keys 0x810
5500 shot
6000 keys 0
6400 enc 4 15
6600 keys 0x80
6900 keys 0
7100 keys 0x800
7400 keys 0
7700 shot
8200 enc 4 -15
10000 btn 2
10300 keys 0x810
10700 shot
11000 keys 0
11500 btn 2
12000 enc 1 7
12100 btn 12
14000 shot
14200 btn 8
14500 btn 11
16500 shot
16600 btn 12
17000 btn 11
17500 btn 9
18000 shot
18300 btn 8
19000 btn 12
19100 enc 2 -30
19900 shot
20400 btn 12
20000 enc 2 30
23200 btn 10
23500 btn 10
23800 btn 10
24100 btn 10
24400 keys 0x4890
25300 shot
25800 keys 0
26000 end
"""

def main():
    IMG.mkdir(exist_ok=True)
    manual.build_sim()
    with tempfile.TemporaryDirectory() as d:
        s = Path(d) / "s.txt"
        s.write_text(SCRIPT)
        subprocess.run([str(manual.SIM), f"{d}/p", str(s)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=manual.DINGHY)
        for ms, name in SHOTS.items():
            Image.open(f"{d}/p-{ms}.ppm").convert("RGB").resize((480, 480), Image.NEAREST).save(IMG / f"screen-{name}.png", optimize=True)
            print("img/screen-%s.png" % name)
    # the panel, nothing pressed, on the page's own ground
    im = Image.new("RGB", (manual.W, manual.H), (8, 9, 14))
    manual.draw_panel(im, 0, 0, 0, [0] * 7, {})
    box = (manual.PX0 - 22, manual.PY0 - 12, manual.PX0 + 922, manual.KB_Y + 152)
    im.crop(box).save(IMG / "panel.png", optimize=True)
    print("img/panel.png")


# the hero's loop: switch-on, round robin lines and chords, a roll of GLO, then RANDOM; silent, 480 px, ~14 s
HERO = """
1700 keys 0x10
1950 keys 0
2000 keys 0x80
2250 keys 0
2300 keys 0x800
2550 keys 0
2600 keys 0x4000
2850 keys 0
2900 keys 0x10000
3150 keys 0
3200 keys 0x4000
3450 keys 0
3500 keys 0x800
3750 keys 0
3900 keys 0x10
4000 keys 0x90
4100 keys 0x890
4200 keys 0x4890
5600 keys 0
6000 btn 7
6600 keys 0x20
6850 keys 0
6900 keys 0x100
7150 keys 0
7200 keys 0x1000
7450 keys 0
7500 keys 0x8000
7750 keys 0
7800 keys 0x20000
8050 keys 0
8300 btndown 10
9100 btnup 10
9400 keys 0x10
9600 keys 0
9700 keys 0x800
9900 keys 0
10000 keys 0x80
10200 keys 0
10300 keys 0x4000
10500 keys 0
10600 keys 0x10000
10800 keys 0
10900 keys 0x800
11100 keys 0
11200 keys 0x10
11300 keys 0x810
11400 keys 0x4810
12900 keys 0
14200 end
"""

def hero():
    with tempfile.TemporaryDirectory() as d:
        s = Path(d) / "h.txt"
        s.write_text(HERO)
        subprocess.run([str(manual.SIM), f"{d}/f", str(s), "30"], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=manual.DINGHY)
        for out in ("hero.mp4",):
            enc = ["-c:v", "libx264", "-preset", "slow", "-crf", "24", "-pix_fmt", "yuv420p", "-movflags", "+faststart"]
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", "30", "-i", f"{d}/f-%05d.ppm",
                            "-vf", "scale=480:480:flags=neighbor", "-an", *enc, str(IMG / out)], check=True)
            print("img/" + out)

if __name__ == "__main__":
    main()
    hero()
