#!/usr/bin/env python3
"""
visualizer.py -- Herramienta de visualización de convex hull (2D) con inserción incremental.

Se conecta al ejecutable C++ `bin/hull_cli` (compilar antes con `make`), de modo que
el hull que se dibuja es el calculado por NUESTRAS implementaciones:
    - al hacer clic se usa la inserción incremental (cpp/dynamic_hull.hpp);
    - las teclas 1-4 recalculan desde cero con andrew / jarvis / quickhull / chan.

Uso interactivo:
    python3 python/visualizer.py

Controles:
    clic izquierdo   insertar un punto (verde = el hull cambió; naranja = quedó dentro)
    g                insertar 40 puntos aleatorios
    o                insertar 20 puntos sobre un círculo
    r                reiniciar
    1 2 3 4          recalcular desde cero con andrew / jarvis / quickhull / chan
                     y comparar con el hull incremental
    i                volver a la vista incremental
    q                salir

Modo sin ventana (genera imágenes para el informe):
    python3 python/visualizer.py --demo salida/
"""
import argparse
import math
import os
import random
import subprocess
import sys

import matplotlib

ALGOS = {"1": "andrew", "2": "jarvis", "3": "quickhull", "4": "chan"}
SIZE = 1000  # el lienzo es [0, SIZE] x [0, SIZE] con coordenadas enteras


class HullBackend:
    """Proceso hijo `hull_cli`: un comando por línea, una respuesta por línea."""

    def __init__(self, exe=None):
        if exe is None:
            root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
            exe = os.path.join(root, "bin", "hull_cli")
        if not os.path.exists(exe):
            sys.exit(f"No existe {exe}. Compila primero con `make`.")
        self.proc = subprocess.Popen(
            [exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1
        )

    def _ask(self, line):
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()
        resp = self.proc.stdout.readline().split()
        if not resp or resp[0] == "ERR":
            raise RuntimeError("hull_cli: " + " ".join(resp))
        changed, h = int(resp[0]), int(resp[1])
        v = list(map(int, resp[2:]))
        return bool(changed), list(zip(v[0::2], v[1::2]))[:h]

    def add(self, x, y):
        return self._ask(f"ADD {x} {y}")

    def recompute(self, algo):
        return self._ask(f"ALGO {algo}")[1]

    def reset(self):
        self._ask("RESET")

    def close(self):
        try:
            self.proc.stdin.write("QUIT\n")
            self.proc.stdin.flush()
        except Exception:
            pass


class Session:
    """Estado de la visualización (puntos, hull actual, hull anterior, último punto)."""

    def __init__(self, backend):
        self.be = backend
        self.reset()

    def reset(self):
        self.be.reset()
        self.points = []
        self.hull = []
        self.prev_hull = []
        self.shown = []  # hull recalculado desde cero (vistas 1-4)
        self.last = None  # (x, y, changed)
        self.view = "incremental"  # o nombre del algoritmo usado para recalcular
        self.msg = "Haz clic para insertar puntos"

    def insert(self, x, y):
        self.points.append((x, y))
        self.prev_hull = self.hull
        changed, self.hull = self.be.add(x, y)
        self.last = (x, y, changed)
        self.view = "incremental"
        self.msg = (
            "El hull CAMBIÓ: el punto es un nuevo vértice"
            if changed
            else "El punto quedó DENTRO (o sobre el borde): el hull no cambia"
        )

    def show_algo(self, algo):
        h = self.be.recompute(algo)
        same = h == self.hull
        self.view = algo
        self.prev_hull = []
        self.shown = h
        self.msg = f"Recalculado desde cero con {algo}: h={len(h)} -> " + (
            "coincide con el hull incremental" if same else "¡DIFIERE del incremental!"
        )
        return h

    def current_hull(self):
        return self.shown if self.view != "incremental" else self.hull


def render(ax, s):
    """Dibuja el estado `s` en el eje `ax`."""
    ax.clear()
    ax.set_xlim(0, SIZE)
    ax.set_ylim(0, SIZE)
    ax.set_aspect("equal")
    ax.set_xticks([])
    ax.set_yticks([])
    if s.points:
        xs, ys = zip(*s.points)
        ax.scatter(xs, ys, s=14, c="#9aa0a6", zorder=2, label=f"puntos (n={len(s.points)})")
    hull = s.current_hull()
    # hull anterior (qué vértices se perdieron al insertar)
    if s.view == "incremental" and s.prev_hull and s.last and s.last[2] and len(s.prev_hull) > 1:
        px, py = zip(*(s.prev_hull + [s.prev_hull[0]]))
        ax.plot(px, py, "--", c="#d93025", lw=1, zorder=3, label="hull anterior")
    if len(hull) >= 2:
        hx, hy = zip(*(hull + [hull[0]]))
        ax.fill(hx, hy, alpha=0.10, c="#1a73e8", zorder=1)
        ax.plot(hx, hy, "-", c="#1a73e8", lw=2, zorder=4, label=f"hull (h={len(hull)})")
    if hull:
        vx, vy = zip(*hull)
        ax.scatter(vx, vy, s=42, c="#1a73e8", zorder=5)
    if s.last and s.view == "incremental":
        x, y, ch = s.last
        ax.scatter([x], [y], s=110, c="#188038" if ch else "#e8710a", edgecolors="k", zorder=6,
                   label="último punto (cambió)" if ch else "último punto (dentro)")
    ax.set_title(s.msg, fontsize=10)
    ax.legend(loc="lower right", fontsize=8)


def random_points(k, rng):
    return [(rng.randint(100, SIZE - 100), rng.randint(100, SIZE - 100)) for _ in range(k)]


def circle_points(k, rng):
    pts = []
    for _ in range(k):
        a = rng.random() * 2 * math.pi
        pts.append((int(500 + 380 * math.cos(a)), int(500 + 380 * math.sin(a))))
    return pts


def run_interactive():
    import matplotlib.pyplot as plt

    be = HullBackend()
    s = Session(be)
    rng = random.Random()
    fig, ax = plt.subplots(figsize=(7, 7))
    fig.canvas.manager.set_window_title("Convex hull con inserción incremental")
    render(ax, s)

    def on_click(ev):
        if ev.inaxes != ax or ev.button != 1 or ev.xdata is None:
            return
        s.insert(int(round(ev.xdata)), int(round(ev.ydata)))
        render(ax, s)
        fig.canvas.draw_idle()

    def on_key(ev):
        if ev.key == "q":
            plt.close(fig)
            return
        if ev.key == "r":
            s.reset()
        elif ev.key == "g":
            for p in random_points(40, rng):
                s.insert(*p)
        elif ev.key == "o":
            for p in circle_points(20, rng):
                s.insert(*p)
        elif ev.key in ALGOS:
            if not s.points:
                return
            s.show_algo(ALGOS[ev.key])
        elif ev.key == "i":
            s.view = "incremental"
            s.msg = "Vista incremental"
        else:
            return
        render(ax, s)
        fig.canvas.draw_idle()

    fig.canvas.mpl_connect("button_press_event", on_click)
    fig.canvas.mpl_connect("key_press_event", on_key)
    try:
        plt.show()
    finally:
        be.close()


def run_demo(outdir):
    """Escenario fijo para el informe: 3 inserciones y comparación con recomputo."""
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    os.makedirs(outdir, exist_ok=True)
    be = HullBackend()
    s = Session(be)
    base = [(200, 250), (700, 180), (820, 520), (600, 800), (250, 700), (480, 480), (400, 380), (560, 560)]
    for p in base:
        s.insert(*p)
    steps = [("estado inicial", None), ("(a) punto interior (500,420)", (500, 420)),
             ("(b) punto exterior (900,850)", (900, 850)), ("(c) punto exterior (80,500)", (80, 500))]
    fig, axes = plt.subplots(1, len(steps), figsize=(4.2 * len(steps), 4.6))
    for ax, (title, p) in zip(axes, steps):
        if p:
            s.insert(*p)
        else:
            s.last, s.msg = None, "8 puntos iniciales"
        render(ax, s)
        ax.set_title(f"{title}\n{s.msg}", fontsize=8)
        ax.legend(fontsize=6, loc="lower right")
    fig.tight_layout(rect=(0, 0, 1, 0.94))
    path = os.path.join(outdir, "insercion_demo.png")
    fig.savefig(path, dpi=140)
    print("escrito", path)

    # comparación de los 4 algoritmos recomputando desde cero sobre el mismo conjunto
    fig2, axes2 = plt.subplots(1, 4, figsize=(16, 4.2))
    ok = True
    for ax, name in zip(axes2, ALGOS.values()):
        s.show_algo(name)
        ok &= s.be.recompute(name) == s.hull
        render(ax, s)
        ax.set_title(f"{name}\n{s.msg}", fontsize=8)
    fig2.tight_layout(rect=(0, 0, 1, 0.93))
    path2 = os.path.join(outdir, "algoritmos_demo.png")
    fig2.savefig(path2, dpi=120)
    print("escrito", path2, "| todos coinciden con el incremental:", ok)
    be.close()


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--demo", metavar="DIR", help="no abre ventana: guarda imágenes de ejemplo en DIR")
    args = ap.parse_args()
    run_demo(args.demo) if args.demo else run_interactive()
