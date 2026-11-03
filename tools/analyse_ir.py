"""Measure the wah model's frequency response from impulse responses (absolute dB)."""
import sys, wave
import numpy as np

def read_wav(path):
    with wave.open(path, 'rb') as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        data = w.readframes(n)
    x = np.frombuffer(data, dtype='<i2').astype(np.float64) / 32768.0
    return (x.reshape(-1, ch) if ch > 1 else x), sr

def analyse(path, start=100, amp=0.1):
    x, sr = read_wav(path)
    if x.ndim > 1:
        x = x[:, 0]
    ir = x[start:start + 32768]
    n = 1 << 16
    H = np.fft.rfft(ir, n=n) / amp
    f = np.fft.rfftfreq(n, 1.0 / sr)
    db = 20 * np.log10(np.abs(H) + 1e-12)

    band = (f > 120) & (f < 8000)
    fb, db_b = f[band], db[band]
    i = np.argmax(db_b)
    fpeak, peak = fb[i], db_b[i]

    def at(fq):
        return db[np.argmin(np.abs(f - fq))]

    # -3 dB bandwidth by walking out from the peak
    lo_i = i
    while lo_i > 0 and db_b[lo_i] > peak - 3:
        lo_i -= 1
    hi_i = i
    while hi_i < len(db_b) - 1 and db_b[hi_i] > peak - 3:
        hi_i += 1
    bw = fb[hi_i] - fb[lo_i]
    q = fpeak / bw if bw > 0 else 0

    print(f"{path.split(chr(92))[-1]:>10}: peak {fpeak:7.1f} Hz  {peak:+6.2f} dB  Q~{q:4.2f}  "
          f"| 100Hz {at(100):+7.2f}  300Hz {at(300):+7.2f}  1k {at(1000):+7.2f}  "
          f"4k {at(4000):+7.2f}  8k {at(8000):+7.2f}")
    return f, db

if __name__ == '__main__':
    for p in sys.argv[1:]:
        analyse(p)
