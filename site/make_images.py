#!/usr/bin/env python3
"""The webpage's pictures, made from the firmware (Dinghy's host mode) and the manual's panel drawing.
    python3 site/make_images.py   -> site/img/*.png"""
import math, subprocess, sys, tempfile
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


# the FM-1 as it is laid out: knobs left, the square screen, KNOB1-4 and the button block right, the pill keys below.
# Each control: its printed name small above, what it does in POLY5 under it.
from PIL import ImageDraw, ImageFont, ImageFilter

def fm1_panel(screen_png, out, lit_keys=(), Z=2):
    W0, H0 = 1760, 1072
    im = Image.new("RGB", (W0 * Z, H0 * Z), (6, 7, 11))
    d = ImageDraw.Draw(im)
    def z(*v): return [int(x * Z) for x in v]
    def F(sz, face=2): return ImageFont.truetype("/System/Library/Fonts/Avenir Next.ttc", sz * Z, index=face)
    def M(sz): return ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", sz * Z, index=0)
    def ctext(x, y, t, f, c):
        w = d.textlength(t, font=f); d.text((x * Z - w / 2, y * Z), t, font=f, fill=c)
    CREAM, GREY, DIM = (236, 232, 224), (128, 132, 146), (88, 92, 104)
    COL = [(106, 168, 240), (240, 138, 112), (125, 212, 152), (240, 200, 112), (216, 144, 168)]
    AMBER = (240, 160, 48)
    d.rounded_rectangle(z(8, 8, W0 - 8, H0 - 8), radius=64 * Z, fill=(30, 31, 37), outline=(48, 50, 58), width=3 * Z)
    def knob(x, y, printed, does, col=None):
        ctext(x, 78, printed, M(19), DIM) if y < 300 else ctext(x, 260, printed, M(19), DIM)
        r = 42
        d.ellipse(z(x - r - 4, y - r - 4, x + r + 4, y + r + 4), fill=(18, 19, 23))
        for k in range(36):                                  # the knurl
            a = math.radians(k * 10)
            d.line(z(x + math.cos(a) * (r - 2), y + math.sin(a) * (r - 2), x + math.cos(a) * (r + 2), y + math.sin(a) * (r + 2)), fill=(54, 56, 64), width=2 * Z)
        d.ellipse(z(x - r + 8, y - r + 8, x + r - 8, y + r - 8), fill=(44, 46, 54), outline=(60, 62, 72), width=Z)
        d.line(z(x, y - r + 12, x, y - r + 30), fill=col or CREAM, width=5 * Z)
        ctext(x, y + 58, does, F(22), col or CREAM)
    knob(155, 165, "MASTER", "VOLUME")
    knob(340, 165, "SELECT", "CURSOR")
    knob(155, 348, "PRESETS", "PATCHES")
    knob(340, 348, "ALGORITHM", "PAGES")
    for i, (does, c) in enumerate((("FILTER", COL[0]), ("RESONANCE", COL[1]), ("DELAY", COL[2]), ("REVERB", COL[3]))):
        knob(990 + i * 195, 165, f"KNOB{i + 1}", does, c)
    # OCT- / OCT+
    d.rounded_rectangle(z(118, 455, 376, 528), radius=14 * Z, fill=(22, 23, 28))
    for x, t in ((181, "OCT-"), (314, "OCT+")):
        d.rounded_rectangle(z(x - 42, 470, x + 42, 512), radius=9 * Z, fill=(86, 88, 98))
        ctext(x, 478, t, F(20, 2), CREAM)
    # the screen, with POLY5 on it
    d.rounded_rectangle(z(462, 82, 872, 492), radius=30 * Z, fill=(0, 0, 0), outline=(14, 14, 16), width=6 * Z)
    scr = Image.open(screen_png).convert("RGB").resize((372 * Z, 372 * Z), Image.NEAREST)
    im.paste(scr, (int(481 * Z), int(101 * Z)))
    # the button block
    d.rounded_rectangle(z(944, 292, 1626, 542), radius=18 * Z, fill=(22, 23, 28))
    rows = [("FX", "SEL", "ENV", "LFO", "EDIT", "GLO"), ("HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC")]
    sub = {"GLO": "RANDOM", "PLAY": "START", "HOME": "BACK"}
    for r, row in enumerate(rows):
        for i, b in enumerate(row):
            x, y = 1012 + i * 108.4, 357 + r * 109
            fill, ink = (86, 88, 98), CREAM
            if r == 0 and i < 5: fill, ink = COL[i], (16, 16, 20)
            if b == "GLO": fill, ink = AMBER, (24, 16, 6)
            if b == "REC": fill = (128, 36, 34)
            d.rounded_rectangle(z(x - 36, y - 36, x + 36, y + 36), radius=8 * Z, fill=fill)
            if r == 0 and i < 5:
                ctext(x, y - 22, b, F(20, 0), ink); ctext(x, y + 2, str(i + 1), F(22, 0), ink)
            else:
                ctext(x, y - 14 if b not in sub else y - 22, b, F(20, 0), ink)
                if b in sub: ctext(x, y + 6, sub[b], M(11), ink if b == "GLO" else (196, 198, 206))
    # the keys: pills with a light slit; 16 below, 11 above (3, 2, 3, 2, 1)
    d.rounded_rectangle(z(68, 584, 1692, 982), radius=40 * Z, fill=(22, 23, 28))
    low = [149 + i * 97.6 for i in range(16)]
    up = [(low[a] + low[a + 1]) / 2 for a in (0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14)]
    def pill(x, y0, y1, on):
        d.rounded_rectangle(z(x - 42, y0, x + 42, y1), radius=42 * Z, fill=(46, 48, 56))
        d.rounded_rectangle(z(x - 8, y0 + 24, x + 8, y0 + 86), radius=8 * Z, fill=AMBER if on else (112, 114, 122))
    whites = [k for k in range(27) if (k + 53) % 12 in (0, 2, 4, 5, 7, 9, 11)]
    blacks = [k for k in range(27) if k not in whites]
    for k, x in zip(whites, low): pill(x, 792, 942, k in lit_keys)
    for k, x in zip(blacks, up): pill(x, 626, 776, k in lit_keys)
    ctext(low[4], 906, "C4", M(17), DIM); ctext(low[11], 906, "C5", M(17), DIM)
    im = im.resize((W0, H0), Image.LANCZOS)
    im.save(out, optimize=True)

if __name__ == "__main__":
    main()
    hero()
    fm1_panel(IMG / "screen-home-chord.png", IMG / "panel.png", lit_keys=(4, 7, 11, 14))
    print("img/panel.png (the FM-1)")
