#!/usr/bin/env python3
"""POLY5 video manual: each chapter is a script of presses with captions. It runs on the firmware itself (Dinghy's host
mode: dinghy/build/host/poly5 renders the screen and the sound), and every frame is composed with a picture of the
FM-1's controls that lights up what is pressed, plus the caption. Out: manual/videos/NN-name.mp4 (1920x1080, 30 fps).

    python3 manual/manual.py            all chapters
    python3 manual/manual.py 3 5        chapters 3 and 5
    python3 manual/manual.py --still 3  one PNG a caption of chapter 3 (quick check, no video)
"""
import math, os, subprocess, sys, tempfile, textwrap
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent
DINGHY = ROOT.parent / "dinghy"
SIM = DINGHY / "build/host/poly5"
OUT = ROOT / "videos"
FPS = 30
W, H = 1920, 1080
TITLE_MS = 3000          # the title card before the device switches on
TAIL_MS = 1200           # the end card fades in over the last part

# ---- the FM-1's controls (ids as dinghy/firmware/src/platform.c, FM-1 build) ----
BTN = {"OCT-": 0, "OCT+": 1, "FX": 2, "SEL": 3, "ENV": 4, "LFO": 5, "EDIT": 6, "GLO": 7, "HOME": 8, "SAVE": 9,
       "ARP": 10, "SEQ": 11, "PLAY": 12, "REC": 13}
ENC = {"SELECT": 0, "ALGORITHM": 1, "KNOB1": 2, "KNOB2": 3, "KNOB3": 4, "KNOB4": 5, "PRESETS": 6}
SYNTH_RGB = [(106, 168, 240), (240, 138, 112), (125, 212, 152), (240, 200, 112), (216, 144, 168)]
AMBER = (240, 160, 48)
NAMES = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def key(n):
    """'C4' / 'F#3' / 'Bb4' -> key index (0 = F3 .. 26 = G5)"""
    s = NAMES[n[0]] + (1 if "#" in n else -1 if "b" in n[1:] else 0)
    midi = 12 * (int(n[-1]) + 1) + s
    k = midi - 53
    assert 0 <= k <= 26, n
    return k


class Chapter:
    def __init__(self, num, slug, title, subtitle):
        self.num, self.slug, self.title, self.subtitle = num, slug, title, subtitle
        self.t = 0
        self.lines = []      # the firmware script
        self.caps = []       # [t0, text, tip]
        self.hl = []         # (t0, t1, control, verb)
        self.mask = 0

    # -- time and words
    def wait(self, ms):
        self.t += ms
        return self

    def cap(self, text, tip=None):
        """a caption from now until the next one (tip: the small line under it, e.g. a reminder)"""
        self.caps.append([self.t, text, tip])
        return self

    def ev(self, s):
        self.lines.append(f"{self.t} {s}")

    def mark(self, ctrl, verb, ms):
        self.hl.append((self.t, self.t + ms, ctrl, verb))

    # -- controls
    def tap(self, b, after=500, verb="PRESS"):
        self.mark(b, verb, 900)
        self.ev(f"btn {BTN[b]}")
        return self.wait(after)

    def hold(self, b, ms=800, after=500):
        self.mark(b, "HOLD", ms + 300)
        self.ev(f"btndown {BTN[b]}")
        self.wait(ms)
        self.ev(f"btnup {BTN[b]}")
        return self.wait(after)

    def turn(self, e, n, step=70, after=400):
        """n detents (+ = clockwise), one each step ms"""
        d = 1 if n > 0 else -1
        self.mark(e, "TURN ▸" if n > 0 else "◂ TURN", abs(n) * step + 500)
        for _ in range(abs(n)):
            self.ev(f"enc {ENC[e]} {d}")
            self.wait(step)
        return self.wait(after)

    def _keys(self, mask):
        self.mask = mask
        self.ev(f"keys {mask:#x}")

    def chord(self, notes, ms, after=300, roll=0):
        m = 0
        for n in notes.split():
            m |= 1 << key(n)
            if roll:
                self._keys(m); self.wait(roll)
        self._keys(m)
        self.wait(max(0, ms - roll * len(notes.split())))
        self._keys(0)
        return self.wait(after)

    def notes(self, seq, step=350, gate=0.8):
        """'C4 E4 G4 - C5': one a step, '-' a rest; a group joined by '+' sounds together"""
        for n in seq.split():
            if n != "-":
                m = 0
                for x in n.split("+"):
                    m |= 1 << key(x)
                self._keys(m)
                self.wait(int(step * gate))
                self._keys(0)
                self.wait(step - int(step * gate))
            else:
                self.wait(step)
        return self

    def end(self, ms=1500):
        self.wait(ms)
        self.ev("end")
        self.t_end = self.t
        return self


# ---- drawing ----
def font(size, face=2):
    return ImageFont.truetype("/System/Library/Fonts/Avenir Next.ttc", size, index=face)


def mono(size, bold=True):
    return ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", size, index=1 if bold else 0)


F_CAP = font(46, 2)
F_TIP = font(30, 5)
F_TITLE = font(40, 2)
F_SMALL = font(24, 5)
F_LBL = mono(19)
F_LBL_S = mono(16)
F_VERB = mono(20)
F_BIG = font(96, 0)
F_SUB = font(44, 5)
BG = (10, 14, 28)
INK = (232, 228, 220)
DIM = (110, 118, 140)

# panel geometry (right column)
PX0, PY0 = 930, 560
ENC_POS = {}
BTN_POS = {}
for i, e in enumerate(["SELECT", "ALGORITHM", "KNOB1", "KNOB2", "KNOB3", "KNOB4", "PRESETS"]):
    ENC_POS[e] = (PX0 + 62 + i * 132, PY0 + 70)
ROW1 = ["OCT-", "OCT+", "FX", "SEL", "ENV", "LFO", "EDIT"]
ROW2 = ["GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"]
for i, b in enumerate(ROW1):
    BTN_POS[b] = (PX0 + 62 + i * 132, PY0 + 196)
for i, b in enumerate(ROW2):
    BTN_POS[b] = (PX0 + 62 + i * 132, PY0 + 286)
KB_Y = PY0 + 360
KB_X0, KB_W = PX0, 900
WHITE = [k for k in range(27) if (k + 53) % 12 in (0, 2, 4, 5, 7, 9, 11)]


def background():
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    # a faint floor grid, as POLY5's home screen
    hy, vx = 760, 460
    for i in range(-30, 31):
        d.line([(vx + i * 6, hy), (vx + i * 120, H)], fill=(20, 28, 52))
    for j in range(1, 14):
        y = hy + (H - hy) * (j / 14) ** 2
        d.line([(0, y), (W, y)], fill=(18, 25, 48))
    im = im.filter(ImageFilter.GaussianBlur(0.6))
    d = ImageDraw.Draw(im)
    # POLY5 wordmark, a letter a synth
    x = 100
    for ch, c in zip("POLY5", SYNTH_RGB):
        d.text((x, 52), ch, font=font(40, 0), fill=c)
        x += d.textlength(ch, font=font(40, 0)) + 2
    d.text((x + 16, 62), "VIDEO MANUAL", font=font(26, 5), fill=DIM)
    return im


def draw_screen_bezel(d):
    x0, y0 = 100, 160
    d.rounded_rectangle([x0 - 22, y0 - 22, x0 + 720 + 22, y0 + 720 + 22], 28, fill=(24, 27, 36), outline=(52, 58, 74), width=2)
    d.rectangle([x0 - 2, y0 - 2, x0 + 722, y0 + 722], fill=(0, 0, 0))
    return x0, y0


def glow(im, box, color, r=18):
    lay = Image.new("RGBA", im.size, (0, 0, 0, 0))
    ImageDraw.Draw(lay).ellipse(box, fill=color + (150,))
    lay = lay.filter(ImageFilter.GaussianBlur(r))
    im.paste(lay, (0, 0), lay)


def draw_panel(im, keys, key_leds, btn_leds, encpos, active):
    """active: {control: (verb, strength 0..1)}"""
    d = ImageDraw.Draw(im, "RGBA")
    d.rounded_rectangle([PX0 - 20, PY0 - 10, PX0 + 920, KB_Y + 150], 26, fill=(18, 22, 34, 235), outline=(46, 52, 70), width=2)
    for e, (x, y) in ENC_POS.items():
        a = active.get(e)
        if a:
            glow(im, [x - 70, y - 70, x + 70, y + 70], AMBER, 22)
            d = ImageDraw.Draw(im, "RGBA")
        r = 38
        d.ellipse([x - r - 6, y - r - 6, x + r + 6, y + r + 6], fill=(30, 34, 46), outline=AMBER if a else (60, 66, 84), width=3 if a else 2)
        d.ellipse([x - r, y - r, x + r, y + r], fill=(52, 56, 68))
        ang = math.radians(-90 + encpos[ENC[e]] * 18)
        d.line([(x + math.cos(ang) * 10, y + math.sin(ang) * 10), (x + math.cos(ang) * (r - 4), y + math.sin(ang) * (r - 4))], fill=INK, width=5)
        lbl = {"ALGORITHM": "ALGO"}.get(e, e)
        tw = d.textlength(lbl, font=F_LBL)
        d.text((x - tw / 2, y + r + 12), lbl, font=F_LBL, fill=AMBER if a else (170, 176, 192))
        if a:
            draw_verb(d, x, y - r - 40, a[0])
    for b, (x, y) in BTN_POS.items():
        a = active.get(b)
        if a:
            glow(im, [x - 70, y - 50, x + 70, y + 50], AMBER, 20)
            d = ImageDraw.Draw(im, "RGBA")
        lit = btn_leds >> BTN[b] & 1
        si = ["FX", "SEL", "ENV", "LFO", "EDIT"].index(b) if b in ("FX", "SEL", "ENV", "LFO", "EDIT") else -1
        col = SYNTH_RGB[si] if si >= 0 else (232, 220, 200)
        fill = col if lit else (44, 48, 60)
        d.rounded_rectangle([x - 50, y - 24, x + 50, y + 24], 10, fill=fill, outline=AMBER if a else (78, 84, 104), width=4 if a else 2)
        tw = d.textlength(b.replace("-", "−"), font=F_LBL)
        d.text((x - tw / 2, y - 12), b.replace("-", "−"), font=F_LBL, fill=(20, 20, 26) if lit else (200, 204, 216))
        if si >= 0:
            d.text((x - 5, y + 26), str(si + 1), font=F_LBL_S, fill=col)
        if a:
            draw_verb(d, x, y - 56, a[0])
    # the keys, F3..G5
    ww = KB_W / len(WHITE)
    pos = {}
    for i, k in enumerate(WHITE):
        pos[k] = (KB_X0 + i * ww, ww)
    kb_active = active.get("KEYS")
    for k in WHITE:
        x, w = pos[k]
        on = keys >> k & 1
        d.rectangle([x + 2, KB_Y, x + w - 2, KB_Y + 130], fill=AMBER if on else (214, 212, 206) if key_leds >> k & 1 == 0 else (250, 230, 180))
        if (k + 53) % 12 == 0:
            d.text((x + w / 2 - 11, KB_Y + 104), f"C{(k + 53) // 12 - 1}", font=F_LBL_S, fill=(90, 90, 96))
    for k in range(27):
        if k in pos:
            continue
        x = pos[k - 1][0] + ww * 0.68
        on = keys >> k & 1
        d.rectangle([x, KB_Y, x + ww * 0.64, KB_Y + 80], fill=AMBER if on else (28, 30, 38))
    return d


def draw_verb(d, x, y, verb):
    tw = d.textlength(verb, font=F_VERB)
    d.rounded_rectangle([x - tw / 2 - 12, y - 4, x + tw / 2 + 12, y + 28], 8, fill=AMBER)
    d.text((x - tw / 2, y), verb, font=F_VERB, fill=(20, 16, 10))


def wrap(d, text, f, width):
    words, lines, cur = text.split(), [], ""
    for w in words:
        t = (cur + " " + w).strip()
        if d.textlength(t, font=f) > width and cur:
            lines.append(cur); cur = w
        else:
            cur = t
    if cur:
        lines.append(cur)
    return lines


def draw_caption(im, ch, text, tip, a):
    d = ImageDraw.Draw(im, "RGBA")
    d.text((PX0 - 20, 150), f"{ch.num:02d}", font=font(30, 0), fill=AMBER)
    d.text((PX0 + 30, 150), ch.title.upper(), font=font(30, 2), fill=DIM)
    y = 222
    col = tuple(int(c * a + b * (1 - a)) for c, b in zip(INK, BG))
    for line in wrap(d, text, F_CAP, 900):
        d.text((PX0 - 20, y), line, font=F_CAP, fill=col)
        y += 60
    if tip:
        y += 10
        tc = tuple(int(c * a + b * (1 - a)) for c, b in zip(AMBER, BG))
        for line in wrap(d, tip, F_TIP, 900):
            d.text((PX0 - 20, y), line, font=F_TIP, fill=tc)
            y += 40


def title_card(ch, a=1.0, base=None):
    im = base.copy() if base else background()
    lay = Image.new("RGBA", (W, H), BG + (int(235 * a),))
    im.paste(lay, (0, 0), lay)
    d = ImageDraw.Draw(im, "RGBA")
    k = int(255 * a)
    d.text((160, 360), f"CHAPTER {ch.num}", font=font(36, 2), fill=AMBER + (k,))
    d.text((152, 410), ch.title, font=F_BIG, fill=INK + (k,))
    y = 560
    for line in wrap(d, ch.subtitle, F_SUB, 1500):
        d.text((160, y), line, font=F_SUB, fill=DIM + (k,))
        y += 60
    x = 160
    for i, c in enumerate(SYNTH_RGB):
        d.rounded_rectangle([x + i * 70, 300, x + i * 70 + 52, 312], 6, fill=c + (k,))
    return im


def render(ch, still=False):
    OUT.mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=f"poly5man{ch.num}-", dir=os.environ.get("TMPDIR")))
    (work / "script.txt").write_text("\n".join(sorted(ch.lines, key=lambda l: int(l.split()[0]))) + "\n")
    subprocess.run([str(SIM), str(work / "f"), str(work / "script.txt"), str(FPS)], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=DINGHY)
    log = [list(map(int, l.split())) for l in (work / "f-log.txt").read_text().split("\n") if l.strip()]
    nframes = len(log)
    base = background()
    draw_screen_bezel(ImageDraw.Draw(base))
    caps = sorted(ch.caps)

    def compose(i):
        ms = log[i][0]
        scr = Image.open(work / f"f-{i:05d}.ppm").convert("RGB").resize((720, 720), Image.NEAREST)
        im = base.copy()
        im.paste(scr, (100, 160))
        active = {}
        for t0, t1, c, v in ch.hl:
            if t0 - 60 <= ms <= t1:
                active[c] = (v, 1)
        if log[i][1]:
            active["KEYS"] = ("PLAY", 1)
        draw_panel(im, log[i][1], log[i][2], log[i][3], log[i][4:], active)
        cur = None
        for j, c in enumerate(caps):
            if c[0] <= ms:
                cur = j
        if cur is not None:
            t0 = caps[cur][0]
            a = min(1.0, (ms - t0) / 250)
            draw_caption(im, ch, caps[cur][1], caps[cur][2], a)
        # progress
        d = ImageDraw.Draw(im)
        d.rectangle([0, H - 6, W, H], fill=(24, 28, 44))
        d.rectangle([0, H - 6, int(W * (i + TITLE_MS * FPS / 1000) / (nframes + TITLE_MS * FPS / 1000)), H], fill=AMBER)
        return im

    if still:
        for j, c in enumerate(caps):
            i = min(nframes - 1, int((c[0] + 1200) * FPS / 1000))
            compose(i).save(OUT / f"{ch.num:02d}-still-{j:02d}.png")
        title_card(ch).save(OUT / f"{ch.num:02d}-still-title.png")
        return

    mp4 = OUT / f"{ch.num:02d}-{ch.slug}.mp4"
    ff = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}",
                           "-r", str(FPS), "-i", "-", "-i", str(work / "f.wav"),
                           "-filter_complex", f"[1:a]adelay={TITLE_MS}|{TITLE_MS},afade=t=out:st={(TITLE_MS + ch.t_end) / 1000 - 1.0}:d=1.0[a]",
                           "-map", "0:v", "-map", "[a]", "-c:v", "libx264", "-preset", "medium", "-crf", "20",
                           "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k", "-shortest", "-movflags", "+faststart", str(mp4)],
                          stdin=subprocess.PIPE)
    first = compose(0)
    n_title = TITLE_MS * FPS // 1000
    for i in range(n_title):     # the title card, then it fades onto the device
        a = 1.0 if i < n_title - 15 else (n_title - i) / 15
        ff.stdin.write(title_card(ch, a, first).tobytes())
    n_tail = TAIL_MS * FPS // 1000
    for i in range(nframes):
        im = compose(i)
        if i >= nframes - n_tail:
            a = (i - (nframes - n_tail)) / n_tail
            lay = Image.new("RGBA", (W, H), BG + (int(255 * a),))
            im.paste(lay, (0, 0), lay)
        ff.stdin.write(im.tobytes())
    ff.stdin.close()
    ff.wait()
    for p in work.iterdir():
        p.unlink()
    work.rmdir()
    print("->", mp4, f"{(nframes + n_title) / FPS:.1f} s")


def build_sim():
    subprocess.run(["sh", "-c", "mkdir -p build/host && cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -D'section(x)=used' "
                    "-DAPP_VER='\"manual\"' -Ifirmware/hal -Ifirmware/src -Ifirmware/gen -o build/host/poly5 firmware/src/poly5.c"],
                   check=True, cwd=DINGHY)


if __name__ == "__main__":
    sys.path.insert(0, str(ROOT))
    from chapters import CHAPTERS
    args = sys.argv[1:]
    still = "--still" in args
    nums = [int(a) for a in args if a.isdigit()]
    build_sim()
    for ch in CHAPTERS:
        if not nums or ch.num in nums:
            render(ch, still)
