#!/usr/bin/env python3
"""
hull_gui.py -- Herramienta local para visualizar convex hull 2D.

Solo usa la biblioteca estándar de Python. Los hulls los calculan nuestras
implementaciones de C++ a través de `bin/hull_cli` (si no existe, se compila solo con g++).

Pestaña "Algoritmos":  Elegir andrew / jarvis / quickhull y generar puntos.
                       Muestra el resultado final, o lo explica con una animación paso a
                       paso que se puede saltar en cualquier momento.
Pestaña "Inserción":   solo DynamicHull (cpp/dynamic_hull.hpp). Clic o coordenadas (x, y) para
                       insertar puntos y ver si el hull cambia.

Uso:
    python python/hull_gui.py
"""
import math
import os
import random
import shutil
import subprocess
import sys
import tempfile
import tkinter as tk
from tkinter import messagebox, ttk

import hull_steps  # animación paso a paso

WORLD = 1000  # el mundo es [0, WORLD]^2 con coordenadas enteras, y hacia arriba
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

ALGOS = {  # nombre en hull_cli -> (etiqueta, costo, fuente original)
    "andrew": ("Andrew (monotone chain)", "O(n log n)", "Andrew 1979"),
    "jarvis": ("Jarvis march", "O(n·h)", "Jarvis 1973"),
    "quickhull": ("QuickHull", "O(n log n) prom., O(n²) peor", "Eddy 1977; Barber et al. 1996"),
}

C = dict(bg="#ffffff", grid="#eceff2", pt="#8c959f", hull="#1a73e8", old="#d93025",
         ok="#188038", warn="#e8710a", ink="#1f2328")


# --------------------------------------------------------------------------- backend C++
def find_or_build_cli():
    exe = os.path.join(ROOT, "bin", "hull_cli.exe" if os.name == "nt" else "hull_cli")
    if os.path.exists(exe):
        return exe
    cxx = shutil.which("g++") or shutil.which("clang++")
    if not cxx:
        raise RuntimeError(f"No existe {exe} y no hay g++ para compilarlo.\n"
                           "Compila con `make` o instala g++.")
    os.makedirs(os.path.dirname(exe), exist_ok=True)
    src = os.path.join(ROOT, "cpp", "hull_cli.cpp")
    r = subprocess.run([cxx, "-O2", "-std=c++17", src, "-o", exe], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError("Falló la compilación de hull_cli:\n" + r.stderr)
    return exe


class Backend:
    """Proceso hijo `hull_cli`: un comando por línea, una respuesta por línea."""

    def __init__(self, exe):
        self.proc = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     text=True, bufsize=1)

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

    def reset(self):
        self._ask("RESET")

    def load_file(self, path):
        return self._ask("FILE " + path)

    def algo(self, name):
        return self._ask(f"ALGO {name}")[1]

    def close(self):
        try:
            self.proc.stdin.write("QUIT\n")
            self.proc.stdin.flush()
            self.proc.wait(timeout=2)
        except Exception:
            self.proc.kill()


# generadores
def gen_uniform(n): return [(random.randint(40, 960), random.randint(40, 960)) for _ in range(n)]
def gen_gauss(n): return [(_cl(random.gauss(500, 150)), _cl(random.gauss(500, 150))) for _ in range(n)]
def gen_circle(n): return [(_cl(500 + 430 * math.cos(2 * math.pi * i / n)),
                            _cl(500 + 430 * math.sin(2 * math.pi * i / n))) for i in range(n)]


def gen_disk(n):
    out = []
    for _ in range(n):
        a, r = random.random() * 2 * math.pi, 430 * math.sqrt(random.random())
        out.append((_cl(500 + r * math.cos(a)), _cl(500 + r * math.sin(a))))
    return out


def gen_grid(n):
    k = max(2, math.ceil(math.sqrt(n)))
    s = 860 / (k - 1)
    return [(_cl(70 + i * s), _cl(70 + j * s)) for i in range(k) for j in range(k)][:n]


def gen_line(n):
    out = []
    for _ in range(n):
        t = random.uniform(60, 940)
        out.append((_cl(t), _cl(t * 0.8 + 60 + random.uniform(-6, 6))))
    return out


def _cl(v): return max(0, min(WORLD, round(v)))


DATASETS = {"Uniforme (cuadrado)": gen_uniform, "Gaussiana": gen_gauss,
            "Sobre un círculo (h = n)": gen_circle, "Dentro de un disco": gen_disk,
            "Grilla (muchos colineales)": gen_grid, "Casi colineales": gen_line}


# aplicación
class App:
    def __init__(self, root, exe):
        self.root = root
        root.title("Convex Hull 2D — visualizador local")
        root.geometry("1020x720")
        root.minsize(760, 560)

        self.be_static = Backend(exe)   # proceso para la pestaña "Algoritmos"
        self.be_inc = Backend(exe)      # proceso para la pestaña "Inserción" (DynamicHull)

        self.s_points, self.s_hull = [], []
        self.i_points, self.i_hull, self.prev_hull = [], [], []
        self.last = None   # (x, y, cambió)
        self.changes = 0
        self.gen = None        # generador de la animación en curso (None = mostrando el resultado)
        self.frame = None      # cuadro que se está dibujando
        self.running = False
        self.job = None        # id del after() pendiente
        self.step_no = 0

        self.canvas = tk.Canvas(root, bg=C["bg"], highlightthickness=1, highlightbackground="#d0d7de",
                                cursor="crosshair")
        self.canvas.pack(side="left", fill="both", expand=True, padx=(10, 5), pady=10)
        self.canvas.bind("<Configure>", lambda e: self.draw())
        self.canvas.bind("<Button-1>", self.on_click)

        side = ttk.Frame(root, padding=8)
        side.pack(side="right", fill="y", padx=(5, 10), pady=10)
        self.nb = ttk.Notebook(side, width=300)
        self.nb.pack(fill="both", expand=True)
        self.tab_s, self.tab_i = ttk.Frame(self.nb, padding=8), ttk.Frame(self.nb, padding=8)
        self.nb.add(self.tab_s, text="Algoritmos")
        self.nb.add(self.tab_i, text="Inserción (DynamicHull)")
        self.nb.bind("<<NotebookTabChanged>>", lambda e: (self.cancel_anim(), self.draw()))
        self.build_static()
        self.build_incremental()
        root.protocol("WM_DELETE_WINDOW", self.close)

        self.generate()

    # ---- paneles
    def build_static(self):
        f = self.tab_s
        ttk.Label(f, text="Algoritmo activo").pack(anchor="w")
        self.algo_var = tk.StringVar(value="andrew")
        cb = ttk.Combobox(f, state="readonly", textvariable=self.algo_var,
                          values=[f"{k}  —  {v[0]}" for k, v in ALGOS.items()])
        cb.current(0)
        cb.pack(fill="x", pady=(2, 6))
        cb.bind("<<ComboboxSelected>>", lambda e: self.on_algo())
        self.cb_algo = cb
        self.algo_info = tk.Label(f, justify="left", anchor="w", bg="#e8f0fe", padx=8, pady=6,
                                  wraplength=270)
        self.algo_info.pack(fill="x")

        ttk.Label(f, text="Animación paso a paso").pack(anchor="w", pady=(10, 0))
        arow = ttk.Frame(f)
        arow.pack(fill="x", pady=2)
        self.btn_play = ttk.Button(arow, text="▶ Animar", command=self.toggle_play)
        self.btn_play.pack(side="left", expand=True, fill="x", padx=(0, 3))
        ttk.Button(arow, text="⏭ Paso", command=self.step_once).pack(side="left", expand=True, fill="x", padx=(0, 3))
        ttk.Button(arow, text="⏩ Resultado", command=self.skip_to_result).pack(side="left", expand=True, fill="x")
        srow = ttk.Frame(f)
        srow.pack(fill="x")
        ttk.Label(srow, text="Lento").pack(side="left")
        self.speed = tk.DoubleVar(value=8)
        ttk.Scale(srow, from_=1, to=100, variable=self.speed).pack(side="left", fill="x", expand=True, padx=4)
        ttk.Label(srow, text="Rápido").pack(side="left")
        self.step_lbl = tk.Label(f, text="Mostrando el resultado final. Pulsa «Animar» o «Paso» para ver cómo se construye.",
                                 anchor="nw", justify="left", wraplength=270, height=6, bg="#f6f7f9", padx=6, pady=4)
        self.step_lbl.pack(fill="x", pady=(4, 0))

        ttk.Label(f, text="Generar puntos").pack(anchor="w", pady=(12, 0))
        self.ds_var = tk.StringVar(value=list(DATASETS)[0])
        ttk.Combobox(f, state="readonly", textvariable=self.ds_var, values=list(DATASETS)).pack(fill="x", pady=2)
        self.n_var = tk.IntVar(value=60)
        self.n_lbl = ttk.Label(f, text="Cantidad: 60")
        self.n_lbl.pack(anchor="w", pady=(6, 0))
        ttk.Scale(f, from_=3, to=2000, variable=self.n_var, command=self.on_n).pack(fill="x")
        row = ttk.Frame(f)
        row.pack(fill="x", pady=8)
        ttk.Button(row, text="Generar", command=self.generate).pack(side="left", expand=True, fill="x", padx=(0, 3))
        ttk.Button(row, text="Limpiar", command=self.clear_static).pack(side="left", expand=True, fill="x")
        self.s_stats = ttk.Label(f, text="", font=("TkDefaultFont", 10, "bold"))
        self.s_stats.pack(anchor="w", pady=4)
        ttk.Label(f, wraplength=270, foreground="#656d76",
                  text="Los puntos se generan automáticamente; el hull se recalcula desde cero con el algoritmo elegido. "
                       "Para agregar puntos a mano usa la pestaña de inserción.").pack(anchor="w")
        self.on_algo()

    def build_incremental(self):
        f = self.tab_i
        tk.Label(f, text="DynamicHull (Andrew incremental)\nInserción O(log h) amortizado", justify="left",
                 anchor="w", bg="#e8f0fe", padx=8, pady=6).pack(fill="x")
        ttk.Label(f, text="Agregar punto (x, y entre 0 y 1000)").pack(anchor="w", pady=(12, 2))
        row = ttk.Frame(f)
        row.pack(fill="x")
        self.ex, self.ey = ttk.Entry(row, width=7), ttk.Entry(row, width=7)
        self.ex.pack(side="left", padx=(0, 4))
        self.ey.pack(side="left", padx=(0, 4))
        ttk.Button(row, text="Agregar", command=self.add_xy).pack(side="left", fill="x", expand=True)
        for e in (self.ex, self.ey):
            e.bind("<Return>", lambda ev: self.add_xy())
        row2 = ttk.Frame(f)
        row2.pack(fill="x", pady=8)
        ttk.Button(row2, text="+10 al azar", command=self.add_random).pack(side="left", expand=True, fill="x", padx=(0, 3))
        ttk.Button(row2, text="Limpiar", command=self.clear_inc).pack(side="left", expand=True, fill="x")
        self.i_stats = ttk.Label(f, text="", font=("TkDefaultFont", 10, "bold"))
        self.i_stats.pack(anchor="w", pady=4)
        self.i_msg = tk.Label(f, text="Haz clic en el lienzo o ingresa coordenadas.", anchor="w",
                              justify="left", wraplength=270)
        self.i_msg.pack(fill="x")
        self.log = tk.Listbox(f, height=9, font=("Consolas", 9), activestyle="none")
        self.log.pack(fill="both", expand=True, pady=6)
        ttk.Label(f, wraplength=270, foreground="#656d76",
                  text="Verde: el punto pasó a ser vértice. Naranja: quedó dentro o sobre el borde. "
                       "Línea roja punteada: hull anterior.").pack(anchor="w")
        self.update_inc_stats()

    # ---- helpers
    def mode(self):
        return "inc" if self.nb.index(self.nb.select()) == 1 else "static"

    def algo_name(self):
        return self.cb_algo.get().split()[0]

    def fail(self, e):
        messagebox.showerror("Error", str(e))

    # ---- modo algoritmos
    def on_algo(self):
        name = self.algo_name()
        label, cost, src = ALGOS[name]
        self.algo_info.config(text=f"{label}\n{cost}\nFuente: {src}")
        if hasattr(self, "s_points"):
            self.recompute_static()

    def on_n(self, _=None):
        self.n_lbl.config(text=f"Cantidad: {int(float(self.n_var.get()))}")

    def generate(self):
        self.s_points = DATASETS[self.ds_var.get()](int(float(self.n_var.get())))
        self.recompute_static()

    def clear_static(self):
        self.s_points = []
        self.recompute_static()

    # ---- animación paso a paso
    def cancel_anim(self):
        """Detiene y descarta la animación; vuelve a mostrar el resultado final."""
        if self.job is not None:
            self.root.after_cancel(self.job)
        self.job, self.gen, self.frame, self.running, self.step_no = None, None, None, False, 0
        self.btn_play.config(text="▶ Animar")
        self.step_lbl.config(text="Mostrando el resultado final. Pulsa «Animar» o «Paso» para ver cómo se construye.")

    def _advance(self):
        """Pasa al siguiente cuadro. Devuelve False si la animación terminó."""
        if not self.s_points:
            return False
        if self.gen is None:
            self.gen = hull_steps.STEPS[self.algo_name()](list(self.s_points))
            self.step_no = 0
        try:
            self.frame = next(self.gen)
        except StopIteration:
            self.gen = None
            self.pause()
            return False
        self.step_no += 1
        text = f"Paso {self.step_no}: {self.frame['msg']}"
        if self.frame["final"]:
            ok = self.frame["chain"] == self.s_hull
            text += ("\n✓ Coincide con el hull calculado por el C++." if ok
                     else "\n⚠ DIFIERE del hull calculado por el C++.")
            self.gen = None  # fin; «Animar» vuelve a empezar
            self.pause()
        self.step_lbl.config(text=text)
        self.draw()
        return not self.frame["final"]

    def pause(self):
        if self.job is not None:
            self.root.after_cancel(self.job)
        self.job, self.running = None, False
        self.btn_play.config(text="▶ Animar")

    def _tick(self):
        self.job = None
        if self._advance() and self.running:
            self.job = self.root.after(max(10, int(1000 / self.speed.get())), self._tick)

    def toggle_play(self):
        if self.running:
            self.pause()
            return
        if not self.s_points:
            return
        if self.frame is not None and self.frame["final"]:  # ya terminó: empezar de nuevo
            self.frame, self.gen = None, None
        self.running = True
        self.btn_play.config(text="⏸ Pausar")
        self._tick()

    def step_once(self):
        self.pause()
        if self.frame is not None and self.frame["final"]:
            self.frame, self.gen = None, None
        self._advance()

    def skip_to_result(self):
        self.cancel_anim()
        self.draw()

    def recompute_static(self):
        self.cancel_anim()
        try:
            self.be_static.reset()
            if self.s_points:
                with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as tf:
                    tf.write("\n".join(f"{x} {y}" for x, y in self.s_points) + "\n")
                    path = tf.name
                try:
                    self.be_static.load_file(path.replace("\\", "/"))
                finally:
                    os.remove(path)
                self.s_hull = self.be_static.algo(self.algo_name())
            else:
                self.s_hull = []
        except Exception as e:
            self.fail(e)
            return
        self.s_stats.config(text=f"{len(self.s_points)} puntos · hull h = {len(self.s_hull)}")
        self.draw()

    # ---- modo inserción
    def insert(self, x, y):
        try:
            changed, hull = self.be_inc.add(x, y)
        except Exception as e:
            self.fail(e)
            return
        self.prev_hull, self.i_hull = self.i_hull, hull
        self.i_points.append((x, y))
        self.last = (x, y, changed)
        self.changes += changed
        if changed:
            self.i_msg.config(text=f"El hull CAMBIÓ: ({x}, {y}) es un nuevo vértice.", fg=C["ok"])
            self.log.insert(0, f"({x}, {y}) -> nuevo vértice")
            self.log.itemconfig(0, fg=C["ok"])
        else:
            self.i_msg.config(text=f"Sin cambios: ({x}, {y}) quedó dentro o sobre el borde.", fg=C["warn"])
            self.log.insert(0, f"({x}, {y}) -> dentro, hull intacto")
            self.log.itemconfig(0, fg=C["warn"])
        self.update_inc_stats()
        self.draw()

    def update_inc_stats(self):
        self.i_stats.config(text=f"{len(self.i_points)} puntos · h = {len(self.i_hull)} · {self.changes} cambios")

    def add_xy(self):
        try:
            x, y = int(self.ex.get()), int(self.ey.get())
        except ValueError:
            self.i_msg.config(text="Ingresa x e y enteros.", fg=C["old"])
            return
        self.insert(_cl(x), _cl(y))

    def add_random(self):
        for _ in range(10):
            self.insert(random.randint(60, 940), random.randint(60, 940))

    def clear_inc(self):
        try:
            self.be_inc.reset()
        except Exception as e:
            self.fail(e)
            return
        self.i_points, self.i_hull, self.prev_hull, self.last, self.changes = [], [], [], None, 0
        self.log.delete(0, "end")
        self.i_msg.config(text="Haz clic en el lienzo o ingresa coordenadas.", fg=C["ink"])
        self.update_inc_stats()
        self.draw()

    # ---- lienzo
    def geometry(self):
        w, h = self.canvas.winfo_width(), self.canvas.winfo_height()
        side = max(50, min(w, h) - 20)
        return (w - side) / 2, (h - side) / 2, side

    def to_px(self, p):
        ox, oy, s = self.geometry()
        return ox + p[0] / WORLD * s, oy + (1 - p[1] / WORLD) * s

    def on_click(self, ev):
        ox, oy, s = self.geometry()
        x, y = (ev.x - ox) / s * WORLD, (1 - (ev.y - oy) / s) * WORLD
        if not (0 <= x <= WORLD and 0 <= y <= WORLD):
            return
        x, y = _cl(x), _cl(y)
        if self.mode() == "inc":  # solo DynamicHull admite puntos a mano
            self.insert(x, y)

    def draw(self):
        cv = self.canvas
        cv.delete("all")
        ox, oy, s = self.geometry()
        cv.create_rectangle(ox, oy, ox + s, oy + s, outline="#d0d7de")
        for g in range(100, WORLD, 100):
            cv.create_line(ox + g / WORLD * s, oy, ox + g / WORLD * s, oy + s, fill=C["grid"])
            cv.create_line(ox, oy + g / WORLD * s, ox + s, oy + g / WORLD * s, fill=C["grid"])
        inc = self.mode() == "inc"
        if not inc and self.frame is not None:
            self.draw_frame(self.frame)
            cv.create_text(ox + 6, oy + s - 6, anchor="sw", fill="#656d76",
                           text=f"{ALGOS[self.algo_name()][0]} — paso {self.step_no}")
            return
        pts, hull = (self.i_points, self.i_hull) if inc else (self.s_points, self.s_hull)
        r = 1.5 if len(pts) > 600 else 3
        hr = 3 if len(hull) > 300 else 5
        if len(hull) >= 3:
            flat = [c for p in hull for c in self.to_px(p)]
            cv.create_polygon(*flat, fill="#dbe8fc", outline="")
        if inc and self.last and self.last[2] and len(self.prev_hull) > 1:
            flat = [c for p in self.prev_hull + [self.prev_hull[0]] for c in self.to_px(p)]
            cv.create_line(*flat, fill=C["old"], dash=(6, 4), width=1.5)
        if len(hull) >= 2:
            flat = [c for p in hull + [hull[0]] for c in self.to_px(p)]
            cv.create_line(*flat, fill=C["hull"], width=2)
        for p in pts:
            x, y = self.to_px(p)
            cv.create_oval(x - r, y - r, x + r, y + r, fill=C["pt"], outline="")
        for p in hull:
            x, y = self.to_px(p)
            cv.create_oval(x - hr, y - hr, x + hr, y + hr, fill=C["hull"], outline="")
        if inc and self.last:
            x, y = self.to_px(self.last[:2])
            cv.create_oval(x - 9, y - 9, x + 9, y + 9, fill=C["ok"] if self.last[2] else C["warn"],
                           outline=C["ink"], width=1.5)
        cv.create_text(ox + 6, oy + s - 6, anchor="sw", fill="#656d76",
                       text=("DynamicHull" if inc else ALGOS[self.algo_name()][0]) + "  ·  0–1000")

    def draw_frame(self, fr):
        """Dibuja un cuadro de la animación (ver hull_steps.F)."""
        cv = self.canvas
        pts = self.s_points
        r = 1.5 if len(pts) > 600 else 3
        if fr["closed"] and len(fr["chain"]) >= 3:
            cv.create_polygon(*[c for p in fr["chain"] for c in self.to_px(p)], fill="#dbe8fc", outline="")
        act = set(fr["active"])
        for p in pts:
            x, y = self.to_px(p)
            col = C["ink"] if p in act else C["pt"]
            cv.create_oval(x - r, y - r, x + r, y + r, fill=col, outline="")
        for p, q, color, dashed in fr["segs"]:
            (x1, y1), (x2, y2) = self.to_px(p), self.to_px(q)
            cv.create_line(x1, y1, x2, y2, fill=C[color], width=1.6, dash=(6, 4) if dashed else None)
        ch = fr["chain"]
        if len(ch) >= 2:
            flat = [c for p in (ch + [ch[0]] if fr["closed"] else ch) for c in self.to_px(p)]
            cv.create_line(*flat, fill=C["hull"], width=2.5)
        hr = 3 if len(ch) > 300 else 5
        for p in ch:
            x, y = self.to_px(p)
            cv.create_oval(x - hr, y - hr, x + hr, y + hr, fill=C["hull"], outline="")
        for p, color in fr["marks"]:
            x, y = self.to_px(p)
            cv.create_oval(x - 7, y - 7, x + 7, y + 7, fill=C[color], outline=C["ink"], width=1.5)

    def close(self):
        if self.job is not None:
            self.root.after_cancel(self.job)
        self.be_static.close()
        self.be_inc.close()
        self.root.destroy()


def main():
    try:
        exe = find_or_build_cli()
    except RuntimeError as e:
        sys.exit(str(e))
    root = tk.Tk()
    App(root, exe)
    root.mainloop()


if __name__ == "__main__":
    main()
