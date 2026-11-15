#!/usr/bin/env python3
"""
validate_with_qhull.py -- Validación EXTERNA de nuestras implementaciones con Qhull.

Qhull (Barber, Dobkin y Huhdanpaa, 1996) es la implementación de referencia de
QuickHull y es la que usa scipy.spatial.ConvexHull. Aquí se usa como "oráculo":
para varios datasets se compara el hull de Qhull con el de cada uno de nuestros
algoritmos (andrew, jarvis, quickhull, chan) y con el de la inserción incremental
(DynamicHull). También se mide el tiempo de Qhull como referencia (incluye el
sobrecosto de Python/NumPy, así que NO es comparable directamente con bin/bench).

    make && python3 python/validate_with_qhull.py [n]

Requiere: numpy, scipy.
Referencias:
  - C. B. Barber, D. P. Dobkin, H. Huhdanpaa. The Quickhull algorithm for convex
    hulls. ACM TOMS 22(4), 1996.  http://www.qhull.org
  - P. Virtanen et al. SciPy 1.0. Nature Methods 17, 2020 (scipy.spatial.ConvexHull).
"""
import os
import subprocess
import sys
import tempfile
import time

import numpy as np
from scipy.spatial import ConvexHull

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLI = os.path.join(ROOT, "bin", "hull_cli")
ALGOS = ["andrew", "jarvis", "quickhull", "chan"]
R = 5e8


def datasets(n, rng):
    """Misma familia de distribuciones que cpp/generators.hpp (coordenadas enteras)."""
    def clamp(a):
        return np.rint(np.clip(a, -R, R)).astype(np.int64)

    ang = rng.uniform(0, 2 * np.pi, n)
    rad = R * np.sqrt(rng.uniform(0, 1, n))
    centers = rng.uniform(-0.8 * R, 0.8 * R, (10, 2))
    sq = rng.uniform(-R + 1, R - 1, (n, 2))
    sq[:4] = [[-R, -R], [R, -R], [R, R], [-R, R]]
    side = max(2, int(np.sqrt(n)))
    return {
        "uniform_square": clamp(rng.uniform(-R, R, (n, 2))),
        "uniform_disk": clamp(np.c_[rad * np.cos(ang), rad * np.sin(ang)]),
        "gaussian": clamp(rng.normal(0, R / 5, (n, 2))),
        "circle": clamp(np.c_[R * np.cos(ang), R * np.sin(ang)]),
        "square_h4": clamp(sq),
        "clustered": clamp(centers[rng.integers(0, 10, n)] + rng.normal(0, R / 25, (n, 2))),
        "grid": (rng.integers(0, side, (n, 2)) - side // 2) * 1000,
    }


def canonical(vertices):
    """Pone los vértices en nuestra convención: CCW desde el lexicográficamente menor."""
    v = [tuple(map(int, p)) for p in vertices]
    i = v.index(min(v))
    return v[i:] + v[:i]


def qhull_hull(pts):
    h = ConvexHull(pts.astype(np.float64))  # en 2D, h.vertices viene en sentido CCW
    return canonical(pts[h.vertices])


class Cli:
    def __init__(self):
        self.p = subprocess.Popen([CLI], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)

    def ask(self, line):
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()
        r = self.p.stdout.readline().split()
        if r[0] == "ERR":
            raise RuntimeError(" ".join(r))
        v = list(map(int, r[2:]))
        return list(zip(v[0::2], v[1::2]))[: int(r[1])]

    def close(self):
        self.p.stdin.write("QUIT\n")
        self.p.stdin.flush()


def main():
    if not os.path.exists(CLI):
        sys.exit("Compila primero con `make`.")
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 5000
    rng = np.random.default_rng(42)
    cli = Cli()
    ok_all = True
    print(f"n = {n}\n{'dataset':<16}{'h(Qhull)':>9}{'t Qhull[ms]':>13}  " + "  ".join(f"{a:>9}" for a in ALGOS + ["incremental"]))
    for name, pts in datasets(n, rng).items():
        t0 = time.perf_counter()
        ref = qhull_hull(pts)
        t_q = (time.perf_counter() - t0) * 1000
        with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
            np.savetxt(f, pts, fmt="%d")
            path = f.name
        cli.ask("RESET")
        inc = cli.ask(f"FILE {path}")
        res = {"incremental": inc}
        for a in ALGOS:
            res[a] = cli.ask(f"ALGO {a}")
        os.unlink(path)
        marks = []
        for k in ALGOS + ["incremental"]:
            same = res[k] == ref
            ok_all &= same
            marks.append(f"{'OK' if same else 'DIFIERE':>9}")
        print(f"{name:<16}{len(ref):>9}{t_q:>13.2f}  " + "  ".join(marks))
    cli.close()
    print("\nTodos coinciden con Qhull." if ok_all else "\nHAY DIFERENCIAS con Qhull.")
    sys.exit(0 if ok_all else 1)


if __name__ == "__main__":
    main()
