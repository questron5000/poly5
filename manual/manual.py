#!/usr/bin/env python3
"""POLY5 video manual: each chapter is a script of presses with captions. It runs on the firmware itself (Dinghy's host
mode: dinghy/build/host/poly5 renders the screen and the sound), and every frame is composed with a picture of the
FM-1's controls that lights up what is pressed, plus the caption. Out: manual/videos/NN-name.mp4 (1920x1080, 30 fps).

    python3 manual/manual.py            all chapters
    python3 manual/manual.py 3 5        chapters 3 and 5
    python3 manual/manual.py --still 3  one PNG a caption of chapter 3 (quick check, no video)
    python3 manual/manual.py --voice    with the spoken narration (manual/narrate.py)
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


class FM1:
    """The FM-1 as it is laid out (knobs left, the square screen, KNOB1-4 and the button block right, pill keys), in
    panel units of 1760 x 1072, drawn at scale s from (ox, oy). The still parts are drawn once, at twice the size and
    smoothed; each frame adds the screen, the knob marks, the lights, the keys and what is being pressed."""
    PW, PH = 1760, 1072
    KNOBS = {"MASTER": (155, 165), "SELECT": (340, 165), "PRESETS": (155, 348), "ALGORITHM": (340, 348),
             "KNOB1": (990, 165), "KNOB2": (1185, 165), "KNOB3": (1380, 165), "KNOB4": (1575, 165)}
    DOES = {"MASTER": "VOLUME", "SELECT": "CURSOR", "PRESETS": "PATCHES", "ALGORITHM": "PAGES",
            "KNOB1": "FILTER", "KNOB2": "RESONANCE", "KNOB3": "DELAY", "KNOB4": "REVERB"}
    ROWS = [("FX", "SEL", "ENV", "LFO", "EDIT", "GLO"), ("HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC")]
    SUB = {"GLO": "RANDOM", "PLAY": "START", "HOME": "BACK"}
    SCREEN = (481, 101, 372)

    def __init__(self, ox, oy, s, functions=True, ground=BG):
        self.ox, self.oy, self.s, self.functions = ox, oy, s, functions
        self.btn = {"OCT-": (181, 491, 42, 21), "OCT+": (314, 491, 42, 21)}
        for r, row in enumerate(self.ROWS):
            for i, b in enumerate(row):
                self.btn[b] = (1012 + i * 108.4, 357 + r * 109, 36, 36)
        low = [149 + i * 97.6 for i in range(16)]
        up = [(low[a] + low[a + 1]) / 2 for a in (0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14)]
        whites = [k for k in range(27) if (k + 53) % 12 in (0, 2, 4, 5, 7, 9, 11)]
        blacks = [k for k in range(27) if k not in whites]
        self.key = {k: (x, 792) for k, x in zip(whites, low)}
        self.key.update({k: (x, 626) for k, x in zip(blacks, up)})
        self.low = low
        self.base = self._base(ground)
        self.f_btn = font(max(8, int(20 * s * 1.0)), 0)
        self.f_sub = mono(max(6, int(11 * s)), False)
        self.f_verb = mono(max(10, int(30 * s)))

    def P(self, x, y):
        return (self.ox + x * self.s, self.oy + y * self.s)

    def _base(self, ground):
        Z = 2 * self.s
        w, h = int(self.PW * Z), int(self.PH * Z)
        im = Image.new("RGB", (w, h), ground)
        d = ImageDraw.Draw(im)
        z = lambda *v: [x * Z for x in v]
        def ctext(x, y, t, f, c):
            tw = d.textlength(t, font=f); d.text((x * Z - tw / 2, y * Z), t, font=f, fill=c)
        CREAM, DIMG = (236, 232, 224), (96, 100, 112)
        d.rounded_rectangle(z(8, 8, self.PW - 8, self.PH - 8), radius=64 * Z, fill=(30, 31, 37), outline=(48, 50, 58), width=max(1, int(3 * Z)))
        for name, (x, y) in self.KNOBS.items():
            ctext(x, 78 if y < 300 else 262, name, mono(int(19 * Z), False), DIMG)
            r = 42
            d.ellipse(z(x - r - 4, y - r - 4, x + r + 4, y + r + 4), fill=(18, 19, 23))
            for k in range(36):
                a = math.radians(k * 10)
                d.line(z(x + math.cos(a) * (r - 2), y + math.sin(a) * (r - 2), x + math.cos(a) * (r + 2), y + math.sin(a) * (r + 2)), fill=(54, 56, 64), width=max(1, int(2 * Z)))
            if self.functions:
                i = list(self.KNOBS).index(name)
                col = SYNTH_RGB[i - 4] if i >= 4 else CREAM
                ctext(x, y + 58, self.DOES[name], font(int(22 * Z), 2), col)
        d.rounded_rectangle(z(118, 455, 376, 528), radius=14 * Z, fill=(22, 23, 28))
        d.rounded_rectangle(z(462, 82, 872, 492), radius=30 * Z, fill=(0, 0, 0), outline=(14, 14, 16), width=max(1, int(6 * Z)))
        d.rounded_rectangle(z(944, 292, 1626, 542), radius=18 * Z, fill=(22, 23, 28))
        d.rounded_rectangle(z(68, 584, 1692, 982), radius=40 * Z, fill=(22, 23, 28))
        for k, (x, y0) in self.key.items():
            d.rounded_rectangle(z(x - 42, y0, x + 42, y0 + 150), radius=42 * Z, fill=(46, 48, 56))
        ctext(self.low[4], 906, "C4", mono(int(17 * Z), False), DIMG); ctext(self.low[11], 906, "C5", mono(int(17 * Z), False), DIMG)
        return im.resize((int(self.PW * self.s), int(self.PH * self.s)), Image.LANCZOS)

    def draw(self, im, screen=None, keys=0, key_leds=0, btn_leds=0, encpos=(0,) * 7, active=None):
        active = active or {}
        s = self.s
        im.paste(self.base, (int(self.ox), int(self.oy)))
        if screen is not None:
            x, y, n = self.SCREEN
            size = int(n * s)
            im.paste(screen.resize((size, size), Image.NEAREST if size >= screen.width else Image.LANCZOS), tuple(int(v) for v in self.P(x, y)))
        for name, (cx, cy) in self.KNOBS.items():           # glows first, under everything
            if name in active: self._glow(im, cx, cy, 80)
        for b, (cx, cy, hw, hh) in self.btn.items():
            if b in active: self._glow(im, cx, cy, 70)
        d = ImageDraw.Draw(im)
        for name, (cx, cy) in self.KNOBS.items():
            r = 34
            X, Y = self.P(cx, cy)
            d.ellipse([X - r * s, Y - r * s, X + r * s, Y + r * s], fill=(44, 46, 54), outline=AMBER if name in active else (60, 62, 72), width=max(1, int(3 * s)) if name in active else 1)
            pos = encpos[ENC[name]] if name in ENC else 0
            a = math.radians(-90 + pos * 18)
            i = list(self.KNOBS).index(name)
            col = SYNTH_RGB[i - 4] if (i >= 4 and self.functions) else INK
            d.line([X + math.cos(a) * 8 * s, Y + math.sin(a) * 8 * s, X + math.cos(a) * 28 * s, Y + math.sin(a) * 28 * s], fill=col, width=max(2, int(5 * s)))
        for b, (cx, cy, hw, hh) in self.btn.items():
            X, Y = self.P(cx, cy)
            lit = btn_leds >> BTN[b] & 1
            si = ["FX", "SEL", "ENV", "LFO", "EDIT"].index(b) if b in ("FX", "SEL", "ENV", "LFO", "EDIT") else -1
            if si >= 0:
                c = SYNTH_RGB[si]
                fill = c if lit else tuple(int(v * 0.42) for v in c)
                ink = (16, 16, 20) if lit else tuple(min(255, int(v * 0.95)) for v in c)
            elif b == "GLO":
                fill, ink = (AMBER, (24, 16, 6)) if lit else ((118, 80, 30), (250, 214, 160))
            elif b == "REC":
                fill, ink = ((236, 64, 56), (255, 240, 236)) if lit else ((110, 34, 32), (236, 200, 196))
            else:
                fill, ink = ((236, 228, 210), (24, 22, 20)) if lit else ((80, 82, 92), (236, 232, 224))
            d.rounded_rectangle([X - hw * s, Y - hh * s, X + hw * s, Y + hh * s], radius=max(2, int(9 * s)), fill=fill,
                                outline=AMBER if b in active else None, width=max(2, int(5 * s)) if b in active else 0)
            t = b.replace("-", "−")
            if si >= 0:
                self._ct(d, X, Y - 22 * s, t, self.f_btn, ink); self._ct(d, X, Y + 2 * s, str(si + 1), self.f_btn, ink)
            elif b in self.SUB and hh > 30:
                self._ct(d, X, Y - 22 * s, t, self.f_btn, ink); self._ct(d, X, Y + 6 * s, self.SUB[b], self.f_sub, ink)
            else:
                self._ct(d, X, Y - (14 if hh > 30 else 9) * s, t, self.f_btn, ink)
        for k, (cx, y0) in self.key.items():
            X, Y = self.P(cx, y0)
            on, led = keys >> k & 1, key_leds >> k & 1
            c = AMBER if on else (250, 226, 170) if led else (112, 114, 122)
            d.rounded_rectangle([X - 8 * s, Y + 24 * s, X + 8 * s, Y + 86 * s], radius=max(2, int(8 * s)), fill=c)
            if on:
                d.rounded_rectangle([X - 42 * s, Y, X + 42 * s, Y + 150 * s], radius=int(42 * s), outline=AMBER, width=max(1, int(3 * s)))
        for name, (cx, cy) in self.KNOBS.items():
            if name in active: self._verb(d, cx, cy - 92 if cy < 300 else cy - 88, active[name][0])
        for b, (cx, cy, hw, hh) in self.btn.items():
            if b in active: self._verb(d, cx, cy - hh - 40, active[b][0])
        return d

    def _ct(self, d, X, Y, t, f, c):
        tw = d.textlength(t, font=f); d.text((X - tw / 2, Y), t, font=f, fill=c)

    def _glow(self, im, cx, cy, r):
        X, Y = self.P(cx, cy); R = r * self.s; pad = int(R + 30)
        box = (int(X - pad), int(Y - pad), int(X + pad), int(Y + pad))
        lay = Image.new("RGBA", (box[2] - box[0], box[3] - box[1]), (0, 0, 0, 0))
        ImageDraw.Draw(lay).ellipse([pad - R, pad - R, pad + R, pad + R], fill=AMBER + (170,))
        lay = lay.filter(ImageFilter.GaussianBlur(R / 3))
        im.paste(lay, box[:2], lay)

    def _verb(self, d, cx, cy, verb):
        X, Y = self.P(cx, cy)
        tw = d.textlength(verb, font=self.f_verb)
        h = self.f_verb.size
        d.rounded_rectangle([X - tw / 2 - 10, Y - 4, X + tw / 2 + 10, Y + h + 6], 7, fill=AMBER)
        d.text((X - tw / 2, Y), verb, font=self.f_verb, fill=(20, 16, 10))


PANEL = None


def draw_panel(im, keys, key_leds, btn_leds, encpos, active, screen=None):
    """the manual's FM-1, in the right column under the caption"""
    global PANEL
    if PANEL is None:
        PANEL = FM1(940, 528, 0.5, functions=False)
    return PANEL.draw(im, screen, keys, key_leds, btn_leds, encpos, active)


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


VOICE_AT = 300          # a caption's voice starts this long after the caption
VOICE_GAP = 550         # and the next caption waits at least this long after it ends


def voice_plan(ch):
    """each caption's voice (manual/narrate.py) and its length; the chapter's timeline stretched where a voice needs
    longer than its caption had: everything from the next caption on moves later (the FM-1 just runs on meanwhile)"""
    import narrate
    title = narrate.speech(f"Chapter {ch.num}. {ch.title}.")
    ch.title_ms = max(TITLE_MS, narrate.duration_ms(title) + 1100)
    clips = []
    for c in sorted(ch.caps):
        w = narrate.speech(c[1] + (" " + c[2] if c[2] else ""))
        clips.append([c, w, narrate.duration_ms(w)])
    for i, (c, w, dur) in enumerate(clips):
        nxt = clips[i + 1][0][0] if i + 1 < len(clips) else ch.t_end
        need = VOICE_AT + dur + VOICE_GAP
        if nxt - c[0] < need:
            shift(ch, nxt, need - (nxt - c[0]))
    return title, [(c[0], w) for c, w, _ in clips]


def shift(ch, at, d):
    """everything at or after `at` ms d ms later"""
    out = []
    for l in ch.lines:
        t, rest = l.split(" ", 1)
        out.append(f"{int(t) + d if int(t) >= at else int(t)} {rest}")
    ch.lines = out
    for c in ch.caps:
        if c[0] >= at:
            c[0] += d
    ch.hl = [(t0 + d, t1 + d, c, v) if t0 >= at else ((t0, t1 + d, c, v) if t1 >= at else (t0, t1, c, v)) for t0, t1, c, v in ch.hl]
    ch.t_end += d


def mix(work, title_ms, total_ms, title_wav, clips):
    """the FM-1's sound after the title, the voice over it; the synth ducks (about -9 dB) while she speaks"""
    import wave
    import numpy as np
    def load(f):
        with wave.open(str(f)) as w:
            a = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(np.float32) / 32768
            return a.reshape(-1, w.getnchannels())[:, :2] if w.getnchannels() > 1 else np.repeat(a[:, None], 2, 1)
    n = int((title_ms + total_ms) * 44.1) + 44100
    synth, voice = np.zeros((n, 2), np.float32), np.zeros((n, 2), np.float32)
    s = load(work / "f.wav"); o = int(title_ms * 44.1); synth[o:o + len(s)] = s[:n - o]
    marks = np.zeros(n, np.float32)
    def put(f, at_ms):
        v = load(f); i = int(at_ms * 44.1); v = v[:n - i]
        voice[i:i + len(v)] += v; marks[i:i + len(v)] = 1
    put(title_wav, 400)
    for t, w in clips:
        put(w, title_ms + t + VOICE_AT)
    k = int(0.18 * 44100)                                          # 180 ms in and out
    duck = np.convolve(marks, np.ones(k) / k, mode="same")
    duck = np.convolve(np.minimum(1, duck * 3), np.ones(k) / k, mode="same")
    out = synth * (1 - 0.65 * duck)[:, None] + voice * 0.95
    pk = np.abs(out).max()
    if pk > 0.95:
        out *= 0.95 / pk
    pcm = (out * 32767).astype(np.int16)
    with wave.open(str(work / "mix.wav"), "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100); w.writeframes(pcm.tobytes())
    return work / "mix.wav"


def render(ch, still=False, voiced=False):
    OUT.mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=f"poly5man{ch.num}-", dir=os.environ.get("TMPDIR")))
    ch.title_ms = TITLE_MS
    if voiced:
        title_wav, clips = voice_plan(ch)
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
        draw_panel(im, log[i][1], log[i][2], log[i][3], log[i][4:], active, scr)
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
        d.rectangle([0, H - 6, int(W * (i + ch.title_ms * FPS / 1000) / (nframes + ch.title_ms * FPS / 1000)), H], fill=AMBER)
        return im

    if still:
        for j, c in enumerate(caps):
            i = min(nframes - 1, int((c[0] + 1200) * FPS / 1000))
            compose(i).save(OUT / f"{ch.num:02d}-still-{j:02d}.png")
        title_card(ch).save(OUT / f"{ch.num:02d}-still-title.png")
        return

    mp4 = OUT / f"{ch.num:02d}-{ch.slug}.mp4"
    tm = ch.title_ms
    if voiced:
        audio, filt = mix(work, tm, ch.t_end, title_wav, clips), f"[1:a]afade=t=out:st={(tm + ch.t_end) / 1000 - 1.0}:d=1.0[a]"
    else:
        audio, filt = work / "f.wav", f"[1:a]adelay={tm}|{tm},afade=t=out:st={(tm + ch.t_end) / 1000 - 1.0}:d=1.0[a]"
    ff = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}",
                           "-r", str(FPS), "-i", "-", "-i", str(audio),
                           "-filter_complex", filt,
                           "-map", "0:v", "-map", "[a]", "-c:v", "libx264", "-preset", "medium", "-crf", "20",
                           "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k", "-shortest", "-movflags", "+faststart", str(mp4)],
                          stdin=subprocess.PIPE)
    first = compose(0)
    n_title = tm * FPS // 1000
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
    voiced = "--voice" in args      # the narration (manual/narrate.py: an ElevenLabs key and voice)
    nums = [int(a) for a in args if a.isdigit()]
    build_sim()
    for ch in CHAPTERS:
        if not nums or ch.num in nums:
            render(ch, still, voiced)
