#!/usr/bin/env python3
"""
Genera gráficos individuales de:
  - Tiempo (t_mean_ms) vs n o h_mean
  - Espacio (peak_mem_bytes o peak_rss_bytes) vs n o h_mean
para todos los algoritmos de cada dataset en exp1, exp1_h, exp2 y exp2_h.

Uso:
    python3 graficar_resultados.py
    python3 graficar_resultados.py --dir . --out figuras --mem-col peak_mem_bytes --linear
"""
import argparse
import os
import re
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

ALGO_STYLES = {
    "andrew": {"color": "#1a73e8", "marker": "o", "label": "Andrew"},
    "jarvis": {"color": "#d93025", "marker": "s", "label": "Jarvis"},
    "quickhull": {"color": "#188038", "marker": "^", "label": "QuickHull"},
    "chan": {"color": "#9334e6", "marker": "D", "label": "Chan"},
    "dynamic_hull": {"color": "#e8710a", "marker": "o", "label": "DynamicHull"},
    "recompute_andrew": {"color": "#1a73e8", "marker": "s", "label": "Recompute Andrew"},
}


def slugify(name: str) -> str:
    return re.sub(r"[^a-zA-Z0-9_-]+", "_", name).strip("_")


def format_mem_series(series: pd.Series):
    """Convierte bytes (>= 0) a KiB o MiB según el máximo del subconjunto."""
    vals = series.astype(float)
    max_val = vals.max() if not vals.empty else 0.0
    if max_val >= 1024 * 1024:
        return vals / (1024.0 * 1024.0), "MiB"
    return vals / 1024.0, "KiB"


def plot_dataset_metric(
    df_ds: pd.DataFrame,
    x_col: str,
    x_label: str,
    y_col: str,
    y_label: str,
    title: str,
    out_path: str,
    log_scale: bool = True,
    is_memory: bool = False,
) -> bool:
    """Genera un gráfico con todos los algoritmos presentes en df_ds."""
    sub = df_ds.copy()
    sub = sub[sub["status"].astype(str) != "omitido"]
    sub = sub[sub[x_col].notna() & sub[y_col].notna()]

    if is_memory:
        sub = sub[sub[y_col] >= 0]

    if sub.empty:
        return False

    if is_memory:
        sub = sub.copy()
        sub["y_plot"], unit = format_mem_series(sub[y_col])
        y_label_final = f"{y_label} [{unit}]"
    else:
        sub["y_plot"] = sub[y_col].astype(float)
        y_label_final = y_label

    fig, ax = plt.subplots(figsize=(7.2, 4.6))
    plotted_any = False

    for algo, grp in sub.groupby("algo", sort=True):
        grp = grp.sort_values(x_col)
        x_vals = grp[x_col].astype(float).to_numpy()
        y_vals = grp["y_plot"].to_numpy()

        # En escala logarítmica, filtramos valores <= 0 para evitar errores de eje
        if log_scale:
            mask = (x_vals > 0) & (y_vals > 0)
            x_vals, y_vals = x_vals[mask], y_vals[mask]

        if len(x_vals) == 0:
            continue

        style = ALGO_STYLES.get(algo, {"marker": "o", "label": algo})
        ax.plot(
            x_vals,
            y_vals,
            marker=style.get("marker", "o"),
            color=style.get("color", None),
            label=style.get("label", algo),
            linewidth=1.8,
            markersize=5,
        )
        plotted_any = True

    if not plotted_any:
        plt.close(fig)
        return False

    if log_scale:
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.grid(True, which="both", linestyle="--", alpha=0.3)
    else:
        ax.grid(True, linestyle="--", alpha=0.35)

    ax.set_title(title, fontsize=11, fontweight="bold")
    ax.set_xlabel(x_label, fontsize=10)
    ax.set_ylabel(y_label_final, fontsize=10)
    ax.legend(title="Algoritmo", fontsize=9, title_fontsize=9, loc="best")
    fig.tight_layout()

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    return True


def process_experiment(
    csv_path: str,
    x_col: str,
    x_label: str,
    exp_tag: str,
    out_root: str,
    mem_col: str,
    log_scale: bool,
) -> int:
    """Procesa un CSV de experimento y genera gráficos de tiempo y espacio por dataset."""
    if not os.path.exists(csv_path):
        print(f"[AVISO] No se encontró {csv_path}, se omite.")
        return 0

    df = pd.read_csv(csv_path)
    if df.empty:
        print(f"[AVISO] {csv_path} está vacío.")
        return 0

    count = 0
    datasets = list(df["dataset"].dropna().unique())

    for ds in datasets:
        sub_ds = df[df["dataset"] == ds]
        ds_slug = slugify(str(ds))

        # Si hay n fijo en experimentos vs h, lo indicamos en el subtítulo
        fixed_n_info = ""
        if x_col == "h_mean" and "n" in sub_ds.columns and sub_ds["n"].nunique() == 1:
            fixed_n_info = f" (n={int(sub_ds['n'].iloc[0])})"

        # 1. Tiempo vs n o h
        t_path = os.path.join(out_root, f"{exp_tag}_tiempo", f"{exp_tag}_tiempo_{ds_slug}.png")
        if plot_dataset_metric(
            df_ds=sub_ds,
            x_col=x_col,
            x_label=x_label,
            y_col="t_mean_ms",
            y_label="Tiempo medio [ms]",
            title=f"{exp_tag} — Tiempo vs {x_col}{fixed_n_info}\nDataset: {ds}",
            out_path=t_path,
            log_scale=log_scale,
            is_memory=False,
        ):
            count += 1

        # 2. Espacio vs n o h
        m_path = os.path.join(out_root, f"{exp_tag}_espacio", f"{exp_tag}_espacio_{ds_slug}.png")
        if plot_dataset_metric(
            df_ds=sub_ds,
            x_col=x_col,
            x_label=x_label,
            y_col=mem_col,
            y_label=f"Memoria pico ({mem_col})",
            title=f"{exp_tag} — Espacio vs {x_col}{fixed_n_info}\nDataset: {ds}",
            out_path=m_path,
            log_scale=log_scale,
            is_memory=True,
        ):
            count += 1

    return count


def main():
    parser = argparse.ArgumentParser(description="Grafica tiempo y espacio vs n o h por dataset.")
    parser.add_argument("--dir", default=".", help="Carpeta donde están los CSV (default: .)")
    parser.add_argument("--out", default="figuras", help="Carpeta de salida para las figuras (default: figuras)")
    parser.add_argument(
        "--mem-col",
        default="peak_mem_bytes",
        choices=["peak_mem_bytes", "peak_rss_bytes"],
        help="Columna de memoria a graficar (default: peak_mem_bytes)",
    )
    parser.add_argument(
        "--linear",
        action="store_true",
        help="Usa escala lineal en vez de log-log",
    )
    args = parser.parse_args()
    log_scale = not args.linear

    configs = [
        ("resultados_exp1.csv", "n", "Tamaño de entrada n", "exp1"),
        ("resultados_exp1_h.csv", "h_mean", "Tamaño del hull h (h_mean)", "exp1_h"),
        ("resultados_exp2.csv", "n", "Tamaño de entrada n", "exp2"),
        ("resultados_exp2_h.csv", "h_mean", "Tamaño del hull h (h_mean)", "exp2_h"),
    ]

    total = 0
    for fname, x_col, x_label, tag in configs:
        path = os.path.join(args.dir, fname)
        n_plots = process_experiment(
            csv_path=path,
            x_col=x_col,
            x_label=x_label,
            exp_tag=tag,
            out_root=args.out,
            mem_col=args.mem_col,
            log_scale=log_scale,
        )
        print(f"{tag} ({fname}): {n_plots} gráficos generados")
        total += n_plots

    print(f"\nTotal de gráficos generados: {total} en '{args.out}/'")


if __name__ == "__main__":
    main()