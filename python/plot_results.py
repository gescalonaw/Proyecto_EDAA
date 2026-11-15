#!/usr/bin/env python3
"""
plot_results.py -- Gráficos y tablas a partir de los CSV de bin/bench.

    python3 python/plot_results.py static resultados_static.csv  figuras/
    python3 python/plot_results.py incremental resultados_inc.csv figuras/

static      -> una figura por dataset (tiempo mediano vs n, escala log-log) + tabla
               resumen en CSV y en LaTeX (resumen_static.tex).
incremental -> barras: tiempo de k inserciones incrementales vs recomputar con Andrew.
"""
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

COLORS = {"andrew": "#1a73e8", "jarvis": "#d93025", "quickhull": "#188038", "chan": "#9334e6", "dynamic": "#e8710a"}


def static(csv, outdir):
    df = pd.read_csv(csv)
    med = df.groupby(["dataset", "n", "algorithm"], as_index=False).agg(time_ms=("time_ms", "median"), h=("h", "first"))
    med.to_csv(os.path.join(outdir, "resumen_static.csv"), index=False)
    datasets = list(med["dataset"].drop_duplicates())
    cols = 3
    rows = -(-len(datasets) // cols)
    fig, axes = plt.subplots(rows, cols, figsize=(5 * cols, 3.8 * rows), squeeze=False)
    for ax, ds in zip(axes.flat, datasets):
        sub = med[med.dataset == ds]
        for algo, g in sub.groupby("algorithm"):
            ax.plot(g.n, g.time_ms, "o-", label=algo, c=COLORS.get(algo), lw=1.6, ms=4)
        hmax = sub.sort_values("n").groupby("n").h.first()
        ax.set_title(f"{ds}  (h = {hmax.iloc[0]} ... {hmax.iloc[-1]})", fontsize=10)
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.set_xlabel("n")
        ax.set_ylabel("tiempo mediano [ms]")
        ax.grid(True, which="both", alpha=0.25)
    for ax in list(axes.flat)[len(datasets):]:
        ax.axis("off")
    axes.flat[0].legend(fontsize=8)
    fig.tight_layout()
    p = os.path.join(outdir, "tiempos_por_dataset.png")
    fig.savefig(p, dpi=140)
    print("escrito", p)

    # tabla ancha para el informe: filas (dataset,n), columnas algoritmos
    wide = med.pivot_table(index=["dataset", "n"], columns="algorithm", values="time_ms").round(3)
    wide["h"] = med.groupby(["dataset", "n"]).h.first()
    wide.to_csv(os.path.join(outdir, "tabla_tiempos.csv"))
    with open(os.path.join(outdir, "resumen_static.tex"), "w") as f:
        f.write(wide.to_latex(float_format="%.3f", na_rep="--"))
    print("escrito tabla_tiempos.csv y resumen_static.tex")


def incremental(csv, outdir):
    df = pd.read_csv(csv)
    med = df.groupby(["dataset", "n", "method"], as_index=False).time_ms.median()
    datasets = list(med["dataset"].drop_duplicates())
    cols = 3
    rows = -(-len(datasets) // cols)
    fig, axes = plt.subplots(rows, cols, figsize=(5 * cols, 3.8 * rows), squeeze=False)
    k = int(df.k.iloc[0])
    for ax, ds in zip(axes.flat, datasets):
        sub = med[med.dataset == ds]
        for m, g in sub.groupby("method"):
            ax.plot(g.n, g.time_ms, "o-", label=m, lw=1.6, ms=4)
        ax.set_title(ds, fontsize=10)
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.set_xlabel("n (puntos ya en el hull)")
        ax.set_ylabel(f"tiempo de {k} inserciones [ms]")
        ax.grid(True, which="both", alpha=0.25)
    for ax in list(axes.flat)[len(datasets):]:
        ax.axis("off")
    axes.flat[0].legend(fontsize=8)
    fig.tight_layout()
    p = os.path.join(outdir, "incremental_vs_recomputo.png")
    fig.savefig(p, dpi=140)
    print("escrito", p)
    wide = med.pivot_table(index=["dataset", "n"], columns="method", values="time_ms")
    wide["speedup"] = (wide["recompute"] / wide["incremental"]).round(0)
    wide.round(4).to_csv(os.path.join(outdir, "tabla_incremental.csv"))
    print("escrito tabla_incremental.csv")


if __name__ == "__main__":
    if len(sys.argv) != 4 or sys.argv[1] not in ("static", "incremental"):
        sys.exit(__doc__)
    os.makedirs(sys.argv[3], exist_ok=True)
    (static if sys.argv[1] == "static" else incremental)(sys.argv[2], sys.argv[3])
