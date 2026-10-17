#!/usr/bin/env python3
"""
Recherche des constantes du modèle (Calibration) sur plusieurs enregistrements à la fois.
Pour chaque jeu de constantes essayé, les potards de chaque enregistrement sont réajustés
(Nelder-Mead depuis le meilleur réglage connu), et on minimise l'écart moyen.

Usage : fit303_global.py <exe> <dossier> <fichiers séparés par des virgules> <sortie.json>
"""
import sys, os, json, numpy as np
from multiprocessing import Pool
from scipy.optimize import minimize
sys.path.insert(0, os.path.dirname(__file__))
import fit303 as F

EXE, FOLDER = sys.argv[1], sys.argv[2]
NAMES = sys.argv[3].split(",")
OUT = sys.argv[4]

# Constantes cherchées : nom, valeur de départ, bornes, échelle log ?
SPACE = [
    ("resonanceScale",   1.0,   (0.6, 1.25), False),
    ("resonanceCurve",   3.0,   (0.0, 6.0),  False),
    ("ladderSaturation", 3.0,   (0.5, 30.0), True),
    ("feedbackHighpass", 150.0, (20.0, 400.0), True),
    ("ampDecay",         1230.0, (500.0, 6000.0), True),
    ("ampFromFilterEnv", 0.45,  (0.0, 1.5),  False),
    ("inputHighpass",    44.486, (10.0, 150.0), True),
    ("squareDrive",      70.0,  (10.0, 200.0), True),
    ("squareOffset",     4.37,  (0.0, 15.0), False),
]

_r = None
_refs = {}

def init():
    global _r, _refs
    _r = F.Renderer(EXE)
    for n in NAMES:
        x = F.load(os.path.join(FOLDER, n))
        sec = min(2.6, len(x) / F.RATE - 0.05)
        f0 = F.estimate_f0(x)
        _refs[n] = (F.features(x, f0, sec), f0, sec, 1 if n in F.SQUARE_FILES else 0)


def fit_one(args):
    name, cal, start, full = args
    ref, f0, sec, sq = _refs[name]
    def cost(k):
        k = np.clip(k, 0, 1)
        return F.distance(ref, F.features(_r.render(F.model_params(k, sq, f0, sec, cal)), f0, sec))
    if full or start is None:
        d, k, _ = F.fit_file(_r, ref, f0, sec, cal, quick=True, squares=(sq,))
        return name, d, list(k)
    res = minimize(cost, start, method="Nelder-Mead", options=dict(maxfev=45, xatol=3e-3, fatol=2e-3, initial_simplex=None))
    k = np.clip(res.x, 0, 1)
    return name, float(res.fun), list(k)


def to_cal(v):
    cal = {}
    for (name, _, (lo, hi), log), x in zip(SPACE, v):
        x = min(max(x, 0.0), 1.0)
        cal[name] = float(np.exp(np.log(lo) + x * (np.log(hi) - np.log(lo)))) if log else lo + x * (hi - lo)
    return cal


def from_cal(cal):
    v = []
    for name, d, (lo, hi), log in SPACE:
        x = cal.get(name, d)
        v.append((np.log(x) - np.log(lo)) / (np.log(hi) - np.log(lo)) if log else (x - lo) / (hi - lo))
    return np.array(v)


def main():
    pool = Pool(2, initializer=init)
    state = dict(knobs={n: None for n in NAMES}, best=1e9, cal=None, log=[])

    def evaluate(v, full=False):
        cal = to_cal(v)
        res = pool.map(fit_one, [(n, cal, state["knobs"][n], full) for n in NAMES])
        err = float(np.mean([d for _, d, _ in res]))
        if err < state["best"]:
            state.update(best=err, cal=cal)
            for n, d, k in res: state["knobs"][n] = k
            json.dump(dict(err=err, cal=cal, per_file={n: dict(err=d, knobs=k) for n, d, k in res}), open(OUT, "w"), indent=1)
        state["log"].append(err)
        print(f"{len(state['log']):3d}  err {err:5.3f}  best {state['best']:5.3f}  " +
              " ".join(f"{k}={v:.3g}" for k, v in cal.items()), flush=True)
        return err

    v0 = from_cal({})
    evaluate(v0, full=True)
    # Coordonnée par coordonnée, puis Nelder-Mead sur tout
    for sweep in range(2):
        for i in range(len(SPACE)):
            base = from_cal(state["cal"])
            for delta in (-0.25, 0.25, -0.1, 0.1):
                v = base.copy(); v[i] = min(1, max(0, v[i] + delta))
                evaluate(v)
    minimize(evaluate, from_cal(state["cal"]), method="Nelder-Mead", options=dict(maxfev=60, xatol=0.01, fatol=0.01))
    # Réajustement complet des potards avec les meilleures constantes
    evaluate(from_cal(state["cal"]), full=True)
    print("FIN", state["best"], json.dumps(state["cal"]))


if __name__ == "__main__":
    main()
