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

if __name__ == "__main__":
    main()
