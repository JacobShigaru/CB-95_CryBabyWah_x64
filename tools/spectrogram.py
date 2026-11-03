"""Render a spectrogram of the wah demo (proof that the resonant peak sweeps)."""
import wave, os
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = r"C:\Users\Shigaru\Documents\deepseek-harness\default-workspace\CryBabyWah"

def read(path):
    with wave.open(path, 'rb') as w:
        sr, n, ch = w.getframerate(), w.getnframes(), w.getnchannels()
        raw = w.readframes(n)
    x = np.frombuffer(raw, dtype='<i2').astype(np.float32) / 32768.0
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return x, sr

def spectrogram(x, sr, nfft=1024, hop=128, fmax=5000):
    win = np.hanning(nfft).astype(np.float32)
    frames = 1 + (len(x) - nfft) // hop
    out = np.empty((nfft // 2 + 1, frames), dtype=np.float32)
    for i in range(frames):
        seg = x[i * hop:i * hop + nfft] * win
        out[:, i] = np.abs(np.fft.rfft(seg))
    out = 20 * np.log10(out + 1e-7)
    freqs = np.fft.rfftfreq(nfft, 1.0 / sr)
    keep = freqs <= fmax
    return out[keep], freqs[keep]

STOPS = np.array([0.0, 0.22, 0.45, 0.68, 0.86, 1.0])
COLOURS = np.array([[10, 11, 15], [46, 20, 66], [132, 38, 62],
                    [214, 92, 38], [246, 168, 46], [255, 246, 205]], dtype=np.float32)

def colorise(norm):
    r = np.interp(norm, STOPS, COLOURS[:, 0])
    g = np.interp(norm, STOPS, COLOURS[:, 1])
    b = np.interp(norm, STOPS, COLOURS[:, 2])
    return np.stack([r, g, b], axis=-1).astype(np.uint8)

def render(path, out, title, width=1200, height=360):
    x, sr = read(path)
    spec, freqs = spectrogram(x, sr)

    rows = height
    fmin = 80.0
    targets = np.logspace(np.log10(fmin), np.log10(freqs[-1]), rows)[::-1]
    idx = np.clip(np.searchsorted(freqs, targets) - 1, 0, len(freqs) - 2)
    w = (targets - freqs[idx]) / (freqs[idx + 1] - freqs[idx])
    mapped = spec[idx] * (1 - w)[:, None] + spec[idx + 1] * w[:, None]

    peak = spec.max()
    norm = np.clip((mapped - (peak - 72.0)) / 72.0, 0.0, 1.0)
    img = colorise(norm)

    im = Image.fromarray(img).resize((width, height), Image.LANCZOS)
    canvas = Image.new("RGB", (width, height + 74), (12, 13, 16))
    canvas.paste(im, (0, 46))
    draw = ImageDraw.Draw(canvas)

    try:
        font = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 14)
        small = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 11)
    except Exception:
        font = ImageFont.load_default()
        small = font

    draw.text((14, 12), title, fill=(234, 235, 240), font=font)
    draw.text((width - 190, 15), "CB-95 / CryBabyWah", fill=(120, 124, 133), font=small)

    for f in [100, 200, 500, 1000, 2000, 4000]:
        y = 46 + int((1 - (np.log(f) - np.log(fmin)) / (np.log(freqs[-1]) - np.log(fmin))) * height)
        draw.line([(64, y), (width - 8, y)], fill=(255, 255, 255, 26), width=1)
        label = f"{f} Hz" if f < 1000 else f"{f // 1000} kHz"
        draw.text((8, y - 7), label, fill=(150, 154, 163), font=small)

    dur = len(x) / sr
    for i in range(0, int(dur) + 1, 2):
        px = 64 + int(i / dur * (width - 76))
        draw.text((min(px, width - 24), height + 52), f"{i}s", fill=(120, 124, 133), font=small)

    canvas.save(out)
    print("wrote", out)

if __name__ == "__main__":
    render(os.path.join(ROOT, "docs", "demo_wah_sweep.wav"),
           os.path.join(ROOT, "docs", "wah_sweep_spectrogram.png"),
           "Pedal sweep - heel to toe, twice (recorded through the plug-in)")
