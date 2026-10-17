#!/usr/bin/env python3
"""
Calage d'Acid Machine sur des enregistrements de vraie TB-303.

Pour chaque enregistrement (note tenue), on cherche les positions de potards (CUT OFF,
RESONANCE, ENV MOD, DECAY, SAW / SQUARE) qui rapprochent le plus la voix de l'enregistrement,
puis on mesure l'écart restant. Avec --global, on cherche aussi les constantes du modèle
(Calibration) qui réduisent l'écart sur l'ensemble des enregistrements.

Écart = différence moyenne (dB) des harmoniques 1 à 100, trame par trame (40 ms), chaque
enregistrement étant normalisé sur son maximum (les fichiers sont normalisés à -0,3 dBFS,
le niveau absolu est perdu).

Usage : fit303.py <exe Fit303> <dossier des wav> [--global] [--files 01,02,...]
"""
import sys, glob, os, json, struct, subprocess, argparse
import numpy as np
from scipy.io import wavfile
from scipy.optimize import differential_evolution, minimize

RATE = 32000
HARMONICS = 100
HOP = 0.04
WIN = 2048
FLOOR = -60.0


class Renderer:
    def __init__(self, exe):
        self.p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    def render(self, params):
        line = " ".join(f"{k}={v:.9g}" for k, v in params.items()) + "\n"
        self.p.stdin.write(line.encode()); self.p.stdin.flush()
        n = struct.unpack("<i", self.p.stdout.read(4))[0]
        return np.frombuffer(self.p.stdout.read(4 * n), dtype=np.float32).astype(float)


def load(path):
    sr, x = wavfile.read(path)
    x = x.astype(float)
    if x.ndim > 1: x = x.mean(axis=1)
    x /= np.abs(x).max()
    # Début de la note
    start = int(np.argmax(np.abs(x) > 0.02))
    x = x[max(0, start - int(0.002 * sr)):]
    if sr != RATE:
        from scipy.signal import resample_poly
        from math import gcd
        g = gcd(sr, RATE); x = resample_poly(x, RATE // g, sr // g)
    return x


def harmonic_matrix(f0, n=WIN, count=HARMONICS):
    t = np.arange(n) / RATE
    w = np.hanning(n)
    h = np.arange(1, count + 1)[:, None]
    return (np.exp(-2j * np.pi * f0 * h * t[None, :]) * w[None, :])


def features(x, f0, seconds):
    E = harmonic_matrix(f0)
    hop = int(HOP * RATE)
    frames = []
    for start in range(0, int(seconds * RATE) - WIN, hop):
        seg = x[start:start + WIN]
        if len(seg) < WIN: break
        frames.append(np.abs(E @ seg))
    F = 20 * np.log10(np.array(frames) + 1e-9)
    return np.maximum(F - F.max(), FLOOR)


def estimate_f0(x):
    best, bestf = -1, 0
    seg = x[int(0.3 * RATE):int(0.3 * RATE) + WIN]
    for f in np.arange(62.0, 69.0, 0.01):
        E = harmonic_matrix(f, count=20)
        v = np.abs(E @ seg).sum()
        if v > best: best, bestf = v, f
    return bestf


def distance(F, G):
    n = min(len(F), len(G))
    mask = (F[:n] > FLOOR + 5) | (G[:n] > FLOOR + 5)
    d = F[:n] - G[:n]
    return float(np.sqrt(np.mean(d[mask] ** 2)))


def model_params(knobs, square, f0, seconds, cal):
    p = dict(rate=RATE, seconds=seconds, note=36, cutoff=knobs[0], resonance=knobs[1],
             envMod=knobs[2], decay=knobs[3], square=square,
             tuning=12 * np.log2(f0 / 65.40639))
    for k, v in cal.items(): p["cal." + k] = v
    return p


SQUARE_FILES = {f"TB3_Bass_{i:02d}.wav" for i in list(range(2, 17)) + [30]}


def fit_file(r, ref, f0, seconds, cal, quick=False, squares=(0, 1)):
    best = None
    for square in squares:
        def cost(k):
            y = r.render(model_params(k, square, f0, seconds, cal))
            return distance(ref, features(y, f0, seconds))
        res = differential_evolution(cost, [(0, 1)] * 4, popsize=6 if quick else 8, maxiter=10 if quick else 16,
                                     tol=1e-3, seed=3, polish=False, init="sobol")
        loc = minimize(cost, res.x, method="Nelder-Mead", bounds=[(0, 1)] * 4,
                       options=dict(maxfev=50 if quick else 100, xatol=2e-3, fatol=1e-3))
        x, f = (loc.x, loc.fun) if loc.fun < res.fun else (res.x, res.fun)
        if best is None or f < best[0]:
            best = (f, x, square)
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe"); ap.add_argument("folder")
    ap.add_argument("--files", default="")
    ap.add_argument("--cal", default="{}")
    ap.add_argument("--seconds", type=float, default=2.6)
    ap.add_argument("--out", default="")
    ap.add_argument("--quick", action="store_true")
    a = ap.parse_args()

    cal = json.loads(a.cal)
    files = sorted(glob.glob(os.path.join(a.folder, "*.wav")))
    if a.files:
        keep = a.files.split(",")
        files = [f for f in files if any(k in os.path.basename(f) for k in keep)]

    r = Renderer(a.exe)
    results = {}
    for f in files:
        x = load(f)
        seconds = min(a.seconds, len(x) / RATE - 0.05)
        f0 = estimate_f0(x)
        ref = features(x, f0, seconds)
        name = os.path.basename(f)
        squares = ((1,) if name in SQUARE_FILES else (0,)) if name.startswith("TB3_Bass_") else (0, 1)
        d, k, sq = fit_file(r, ref, f0, seconds, cal, quick=a.quick, squares=squares)
        results[os.path.basename(f)] = dict(err=d, knobs=[round(float(v), 3) for v in k], square=sq, f0=round(f0, 2), seconds=seconds)
        print(f"{os.path.basename(f)}  ecart {d:5.2f} dB  cutoff {k[0]:.2f} res {k[1]:.2f} env {k[2]:.2f} decay {k[3]:.2f} {'SQR' if sq else 'SAW'}  f0 {f0:.2f}", flush=True)

    errs = [v["err"] for v in results.values()]
    print(f"moyenne {np.mean(errs):.2f} dB  mediane {np.median(errs):.2f} dB")
    if a.out:
        json.dump(results, open(a.out, "w"), indent=1)


if __name__ == "__main__":
    main()
