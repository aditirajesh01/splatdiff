#!/usr/bin/env python3
"""Turn splatdiff_experiments CSVs into report figures.

Usage: python3 tools/plot_results.py assets/experiments
Writes fig_cases.png, fig_tau_sweep.png, fig_resolution.png, fig_thread_scaling.png and
fig_time_breakdown.png into the same folder.

Colours: categorical slots 1-3 (blue, orange, aqua), validated for colour-vision deficiency on the light
surface. Aqua is low-contrast on white, so every series also has a marker and a direct label.
"""
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import pandas as pd  # noqa: E402

BLUE, ORANGE, AQUA = "#2a78d6", "#eb6834", "#1baf7a"
SURFACE, INK, INK2, GRID = "#fcfcfb", "#1f1f1d", "#5f5e58", "#e4e3de"

plt.rcParams.update({
    "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "savefig.facecolor": SURFACE,
    "axes.edgecolor": GRID, "axes.labelcolor": INK2, "axes.titlecolor": INK, "axes.titlesize": 12,
    "axes.titleweight": "bold", "axes.titlelocation": "left", "axes.spines.top": False,
    "axes.spines.right": False, "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.8,
    "xtick.color": INK2, "ytick.color": INK2, "font.size": 10, "legend.frameon": False,
    "lines.linewidth": 2, "lines.markersize": 6, "text.color": INK,
})


def label_end(ax, x, y, text, colour, dx=4):
    """Direct label at the end of a line (text in ink, a colour swatch is the marker itself)."""
    ax.annotate(text, (x, y), xytext=(dx, 0), textcoords="offset points", va="center", fontsize=9, color=INK2)


def fig_cases(d: Path):
    df = pd.read_csv(d / "cases.csv").rename(columns={"pass": "ok"})  # "pass" is a Python keyword
    real = df[df.expect_change == 1]
    none = df[df.expect_change == 0]
    fig, (a, b) = plt.subplots(1, 2, figsize=(11, 4.2), gridspec_kw={"width_ratios": [1.15, 1]})

    y = range(len(real))
    a.barh(list(y), real.mean_iou, color=BLUE, height=0.6, edgecolor=SURFACE, linewidth=2)
    for i, r in enumerate(real.itertuples()):
        verdict = "pass" if r.ok else "FAIL"
        a.text(r.mean_iou + 0.01, i, f"found {r.gt_detected}/{r.gt_boxes}, {r.false_regions} false  ({verdict})",
               va="center", fontsize=9, color=INK2)
    a.set_yticks(list(y), real.name)
    a.invert_yaxis()
    a.set_xlim(0, 1.15)
    a.set_xlabel("mean box IoU with the ground-truth change")
    a.set_title("Real changes: all found, no false regions")
    a.grid(axis="y", visible=False)

    y2 = range(len(none))
    b.barh(list(y2), none.changed_voxels, color=ORANGE, height=0.6, edgecolor=SURFACE, linewidth=2)
    for i, r in enumerate(none.itertuples()):
        b.text(max(r.changed_voxels, 0) + 0.02, i, f"{r.regions} regions, {r.changed_voxels} voxels  "
               f"({'pass' if r.ok else 'FAIL'})", va="center", fontsize=9, color=INK2)
    b.set_yticks(list(y2), [f"{g}: {n}" for g, n in zip(none.group, none.name)])
    b.invert_yaxis()
    b.set_xlim(0, max(1, none.changed_voxels.max()) * 3)
    b.set_xlabel("changed voxels (should be 0)")
    b.set_title("Re-scans and lighting: nothing flagged")
    b.grid(axis="y", visible=False)
    fig.tight_layout()
    fig.savefig(d / "fig_cases.png", dpi=200)
    plt.close(fig)


def fig_tau(d: Path):
    df = pd.read_csv(d / "tau_sweep.csv")
    fig, (a, b) = plt.subplots(1, 2, figsize=(11, 4))
    for (mode, g), colour, marker in zip(df.groupby("mode", sort=False), [BLUE, ORANGE], ["o", "s"]):
        name = "with 1-voxel tolerance" if mode.endswith("1") else "no tolerance"
        a.plot(g.x, 100 * g.false_change_rate, color=colour, marker=marker, label=name)
        b.plot(g.x, g.mean_iou, color=colour, marker=marker, label=name)
        label_end(b, g.x.iloc[-1], g.mean_iou.iloc[-1], name, colour)
    a.set_xlabel("threshold tau")
    a.set_ylabel("false-change rate (% of occupied voxels)")
    a.set_title("False changes vs threshold")
    a.legend()
    b.set_xlabel("threshold tau")
    b.set_ylabel("mean box IoU")
    b.set_ylim(0, 1)
    b.set_xlim(right=df.x.max() * 1.45)
    b.set_title("Localisation vs threshold")
    fig.tight_layout()
    fig.savefig(d / "fig_tau_sweep.png", dpi=200)
    plt.close(fig)


def fig_resolution(d: Path):
    df = pd.read_csv(d / "resolution_sweep.csv")
    fig, (a, b) = plt.subplots(1, 2, figsize=(11, 4))
    a.plot(df.x, df.mean_iou, color=BLUE, marker="o")
    for r in df.itertuples():  # only call out resolutions that missed a change
        if r.detection_rate < 1.0:
            a.annotate(f"{100 * r.detection_rate:.0f}% of changes found", (r.x, r.mean_iou), xytext=(6, -14),
                       textcoords="offset points", fontsize=8, color=INK2)
    a.set_xlabel("grid resolution (voxels on the longest axis)")
    a.set_ylabel("mean box IoU")
    a.set_ylim(0, 1)
    a.set_title("Accuracy vs grid resolution (all changes found from 48)")
    b.plot(df.x, df.total_ms, color=ORANGE, marker="s")
    b.set_xlabel("grid resolution (voxels on the longest axis)")
    b.set_ylabel("detection time (ms, 8 threads)")
    b.set_ylim(bottom=0)
    b.set_title("Cost vs grid resolution")
    fig.tight_layout()
    fig.savefig(d / "fig_resolution.png", dpi=200)
    plt.close(fig)


def fig_threads(d: Path):
    df = pd.read_csv(d / "threads.csv")
    resolutions = sorted(df.resolution.unique())
    fig, axes = plt.subplots(1, len(resolutions), figsize=(4.2 * len(resolutions), 4), sharey=True, squeeze=False)
    for ax, res in zip(axes[0], resolutions):
        g = df[df.resolution == res]
        serial = g[g["mode"] == "serial"].total_ms.iloc[0]
        tmax = g.threads.max()
        ax.plot([1, tmax], [1, tmax], color=GRID, linestyle="--", linewidth=1.5)
        ax.annotate("ideal", (tmax, tmax), xytext=(-4, -12), textcoords="offset points", ha="right", fontsize=8,
                    color=INK2)
        for mode, colour, marker, name in [("OpenMP (atomic)", BLUE, "o", "atomic"),
                                           ("OpenMP (private grids)", ORANGE, "s", "private grids")]:
            m = g[g["mode"] == mode].sort_values("threads")
            sp = serial / m.total_ms
            ax.plot(m.threads, sp, color=colour, marker=marker, label=name)
            label_end(ax, m.threads.iloc[-1], sp.iloc[-1], f"{name} {sp.iloc[-1]:.1f}x", colour)
        ax.set_xscale("log", base=2)
        ax.set_xticks(sorted(g.threads.unique()), [str(t) for t in sorted(g.threads.unique())])
        ax.set_xlim(0.8, tmax * 3.2)
        ax.set_xlabel("threads")
        ax.set_title(f"{res}³-class grid (serial {serial:.0f} ms)")
    axes[0][0].set_ylabel("speedup over serial")
    axes[0][0].legend(loc="upper left")
    fig.suptitle("Strong scaling of the full detector (Apple M2: 4 performance + 4 efficiency cores)",
                 x=0.01, ha="left", fontsize=12, fontweight="bold", color=INK)
    fig.tight_layout()
    fig.savefig(d / "fig_thread_scaling.png", dpi=200)
    plt.close(fig)


def fig_breakdown(d: Path):
    df = pd.read_csv(d / "threads.csv")
    tmax = df.threads.max()
    rows = []
    for res in sorted(df.resolution.unique()):
        g = df[df.resolution == res]
        for mode, short in [("serial", "serial"), ("OpenMP (atomic)", f"atomic x{tmax}"),
                            ("OpenMP (private grids)", f"private x{tmax}")]:
            m = g[(g["mode"] == mode) & ((g.threads == tmax) | (g["mode"] == "serial"))]
            if not m.empty:
                r = m.iloc[0]
                rows.append((f"{res}: {short}", r.splat_ms, r.compare_ms, r.regions_ms))
    fig, ax = plt.subplots(figsize=(10, 0.45 * len(rows) + 1.5))
    labels = [r[0] for r in rows]
    left = [0.0] * len(rows)
    for k, (name, colour) in enumerate([("splat (kernel 1)", BLUE), ("compare (kernel 2)", ORANGE),
                                        ("regions (kernel 3)", AQUA)]):
        vals = [r[k + 1] for r in rows]
        ax.barh(labels, vals, left=left, color=colour, height=0.62, edgecolor=SURFACE, linewidth=2, label=name)
        left = [l + v for l, v in zip(left, vals)]
    for i, total in enumerate(left):
        ax.text(total + max(left) * 0.01, i, f"{total:.0f} ms", va="center", fontsize=9, color=INK2)
    ax.invert_yaxis()
    ax.set_xlabel("time per scan pair (ms)")
    ax.set_title("Where the time goes, per kernel (serial vs 8 threads)")
    ax.legend(loc="lower right", ncol=3)
    ax.grid(axis="y", visible=False)
    fig.tight_layout()
    fig.savefig(d / "fig_time_breakdown.png", dpi=200)
    plt.close(fig)


if __name__ == "__main__":
    folder = Path(sys.argv[1] if len(sys.argv) > 1 else "assets/experiments")
    for f in (fig_cases, fig_tau, fig_resolution, fig_threads, fig_breakdown):
        f(folder)
    print("figures written to", folder)
