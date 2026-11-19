#!/usr/bin/env python3
"""The video manual's voice: each caption spoken by an ElevenLabs voice, cached in manual/voice/<voice id>/ so a
re-render only voices what changed (and needs no key when nothing did).

The key is never in this repository: put it in ~/.config/poly5/elevenlabs.key (or $ELEVENLABS_API_KEY); the voice
in ~/.config/poly5/elevenlabs.voice (or $ELEVENLABS_VOICE_ID).

    python3 manual/narrate.py voices                  the voices on your account
    python3 manual/narrate.py sample VOICE_ID [...]   one line in each voice -> manual/voice/samples/
"""
import hashlib, json, os, re, subprocess, sys, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent
CACHE = ROOT / "voice"
CONF = Path.home() / ".config" / "poly5"
MODEL = os.environ.get("ELEVENLABS_MODEL", "eleven_multilingual_v2")
SETTINGS = {"stability": 0.5, "similarity_boost": 0.75, "style": 0.15, "use_speaker_boost": True}

# how the panel's names are said (the captions write them as printed on the FM-1)
SAY = [
    (r"\bGLO\b", "glow"), (r"\bKNOB([1-4])\b", lambda m: "knob " + "one two three four".split()[int(m[1]) - 1]),
    (r"\bKNOB1 to KNOB4\b", "knobs one to four"), (r"OCT[−-] and OCT\+", "octave down and octave up"),
    (r"OCT[−-]", "octave down"), (r"OCT\+", "octave up"), (r"\bSEQ\b", "seq"), (r"\bARP\b", "arp"), (r"\bREC\b", "rec"),
    (r"\bFX, SEL, ENV, LFO and EDIT\b", "F X, S E L, E N V, L F O and edit"), (r"\bFX\b", "F X"), (r"\bSEL\b", "S E L"),
    (r"\bLFO\b", "L F O"), (r"\bENV\b", "E N V"), (r"\bFM\b", "F M"), (r"\bDJ\b", "D J"), (r"\bMIDI\b", "midi"),
    (r"\bF0?(\d\d?)\b", lambda m: "F " + m[1].lstrip("0")), (r"\bU0?(\d\d?)\b", lambda m: "U " + m[1].lstrip("0")),
    (r"\bPOLY5\b", "poly five"), (r"\b1/8\b", "an eighth"), (r"\b1/4\b", "a quarter"),
    (r"\bSAVE, SAVE\b", "save, save"), (r"…", "..."), (r"\s*\*\s*", " star "), (r" = ", " is "), (r"\bon/off\b", "on and off"),
]


def say_text(text):
    """the caption as it should be spoken: the panel's names, then the rest of the capitals as words"""
    s = text
    for pat, rep in SAY:
        s = re.sub(pat, rep, s)
    s = re.sub(r"\b[A-Z][A-Z]+(?:[ -][A-Z][A-Z]+)*\b", lambda m: m[0].lower(), s)   # ROUND ROBIN -> round robin
    s = re.sub(r"(^|[.!?:]\s+)([a-z])", lambda m: m[1] + m[2].upper(), s.strip())   # a sentence starts with a capital
    return s


def _secret(name, env):
    v = os.environ.get(env)
    if v:
        return v.strip()
    f = CONF / name
    return f.read_text().strip() if f.exists() else None


def key():
    return _secret("elevenlabs.key", "ELEVENLABS_API_KEY")


def voice():
    return _secret("elevenlabs.voice", "ELEVENLABS_VOICE_ID")


def _api(path, body=None, accept="application/json"):
    k = key()
    if not k:
        raise SystemExit("No ElevenLabs key: put it in ~/.config/poly5/elevenlabs.key (see manual/narrate.py)")
    req = urllib.request.Request("https://api.elevenlabs.io" + path, method="POST" if body else "GET",
                                 headers={"xi-api-key": k, "accept": accept, "content-type": "application/json"},
                                 data=json.dumps(body).encode() if body else None)
    with urllib.request.urlopen(req, timeout=120) as r:
        return r.read()


def speech(text, voice_id=None):
    """the spoken caption: a WAV at 44.1 kHz, cached by its words, voice, model and settings"""
    voice_id = voice_id or voice()
    if not voice_id:
        raise SystemExit("No voice chosen: put its id in ~/.config/poly5/elevenlabs.voice")
    said = say_text(text)
    h = hashlib.sha1(json.dumps([said, voice_id, MODEL, SETTINGS]).encode()).hexdigest()[:16]
    d = CACHE / voice_id
    wav = d / f"{h}.wav"
    if wav.exists():
        return wav
    d.mkdir(parents=True, exist_ok=True)
    mp3 = _api(f"/v1/text-to-speech/{voice_id}?output_format=mp3_44100_128",
               {"text": said, "model_id": MODEL, "voice_settings": SETTINGS}, accept="audio/mpeg")
    tmp = d / f"{h}.mp3"
    tmp.write_bytes(mp3)
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(tmp), "-ar", "44100", "-ac", "2", str(wav)], check=True)
    tmp.unlink()
    (d / f"{h}.txt").write_text(said + "\n")
    return wav


def duration_ms(wav):
    out = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", str(wav)],
                         capture_output=True, text=True, check=True).stdout
    return int(float(out) * 1000)


if __name__ == "__main__":
    a = sys.argv[1:]
    if a[:1] == ["voices"]:
        for v in json.loads(_api("/v1/voices"))["voices"]:
            lab = v.get("labels") or {}
            print(f'{v["voice_id"]}  {v["name"]:<22} {lab.get("gender", ""):<8} {lab.get("accent", ""):<12} {lab.get("description", "") or lab.get("descriptive", "")}  {lab.get("use_case", "") or lab.get("use case", "")}')
    elif a[:1] == ["sample"]:
        line = ("Play a key. POLY5 starts in ROUND ROBIN: each note goes to the next synth. "
                "Press GLO and all five synths get a brand new sound.")
        out = CACHE / "samples"
        out.mkdir(parents=True, exist_ok=True)
        for vid in a[1:]:
            w = speech(line, vid)
            dst = out / f"{vid}.wav"
            dst.write_bytes(w.read_bytes())
            print(dst)
    else:
        print(__doc__)
