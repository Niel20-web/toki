"""Generate Toki's sound effects: small 16-bit mono 16 kHz WAVs.

Usage: python3 make_sounds.py ../data [--force]
Existing files are kept unless --force is given.
"""
import numpy as np, wave, os, sys

SR = 16000
out = sys.argv[1]
force = '--force' in sys.argv
os.makedirs(out, exist_ok=True)

def glide(f0, f1, ms, vib=0.0, vib_hz=7.0, harm=(1.0, 0.35, 0.15), phase0=0.0):
    n = int(SR * ms / 1000)
    t = np.arange(n) / SR
    u = np.linspace(0, 1, n)
    f = f0 + (f1 - f0) * u
    f = f * (1 + vib * np.sin(2 * np.pi * vib_hz * t))
    ph = phase0 + 2 * np.pi * np.cumsum(f) / SR
    y = sum(a * np.sin((k + 1) * ph) for k, a in enumerate(harm))
    y /= sum(harm)
    env = np.minimum(1, np.minimum(u / 0.08, (1 - u) / 0.25))
    return y * env

def gap(ms):
    return np.zeros(int(SR * ms / 1000))

def seq(*parts):
    return np.concatenate(parts)

def save(name, y, peak=0.8):
    if os.path.exists(os.path.join(out, name)) and not force:
        print(f"{name:18s} exists, kept"); return
    y = y / (np.max(np.abs(y)) + 1e-9) * peak
    # tiny fade in/out against clicks
    n = len(y); k = min(80, n // 4)
    y[:k] *= np.linspace(0, 1, k); y[-k:] *= np.linspace(1, 0, k)
    with wave.open(os.path.join(out, name), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((y * 32767).astype("<i2").tobytes())
    print(f"{name:18s} {n / SR:4.2f}s  {n * 2 + 44:6d} bytes")

# startup: rising "boot-up" arpeggio ending in a happy chirp
save("startup_alt.wav", seq(glide(400, 800, 120), glide(800, 600, 80), glide(600, 1200, 160, vib=0.02),
                            gap(40), glide(1200, 1500, 90), glide(1500, 1100, 140, vib=0.03)))
# happy: bouncy up-down chirps
save("happy.wav", seq(glide(700, 1100, 90), glide(1100, 900, 60), glide(900, 1400, 120, vib=0.03),
                      gap(30), glide(1000, 1500, 100), glide(1500, 1300, 90)))
# curious: rising question
save("curious.wav", seq(glide(500, 900, 140), gap(60), glide(700, 1300, 220, vib=0.02)))
# angry: low growly buzz (square-ish, fast tremolo)
a = glide(300, 200, 160, vib=0.06, vib_hz=28, harm=(1, 0.8, 0.6, 0.4))
b = glide(300, 160, 240, vib=0.08, vib_hz=32, harm=(1, 0.8, 0.6, 0.4))
save("angry2.wav", seq(a, gap(40), b))
# surprise: fast high swoop then a "boing"
save("surprise_1.wav", seq(glide(600, 1700, 110), glide(1700, 1500, 100, vib=0.05, vib_hz=14),
                           gap(30), glide(900, 500, 120)))
# sleep: slow descending yawn
save("sleep_toki.wav", seq(glide(700, 450, 350, vib=0.02, vib_hz=4), glide(450, 250, 600, vib=0.03, vib_hz=3)))
# wake up: slow rise
save("wake_up_2.wav", seq(glide(300, 600, 200), glide(600, 900, 180), gap(30), glide(900, 1200, 140, vib=0.02)))
# dizzy: wobbly up/down
save("dizzy.wav", seq(glide(500, 900, 110), glide(900, 400, 110), glide(400, 900, 110), glide(900, 300, 160, vib=0.08, vib_hz=9)))
# love: soft two-note coo with shimmer
save("love.wav", seq(glide(800, 1000, 120, vib=0.015, vib_hz=6, harm=(1, 0.2)), gap(40),
                     glide(1000, 1300, 220, vib=0.02, vib_hz=6, harm=(1, 0.2)), glide(1300, 1200, 150, harm=(1, 0.2))), peak=0.7)
# blip: tiny confirmation beep
save("blip.wav", glide(900, 1100, 60))
