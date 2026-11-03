#!/usr/bin/env python3
"""Figure 7 — the copy-on-write measurement, drawn from docs/results.txt.

The numbers are not typed in here. They are parsed out of the raw
experiment output, so the chart cannot drift away from the data.

    python3 docs/figures/make_chart.py
"""
import re
import pathlib
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import FixedLocator, FuncFormatter

HERE = pathlib.Path(__file__).resolve().parent
RESULTS = HERE.parent / "results.txt"
OUT = HERE / "fig7_cow_chart.pdf"

# palette: validated categorical slots 1 and 2 (light mode)
BLUE, ORANGE = "#2a78d6", "#eb6834"
INK, INK2, GRID = "#0b0b0b", "#52514e", "#d8d8d4"
SURFACE = "#fcfcfb"


def parse(path):
    """Pull the two EXP2 series out of the raw results file."""
    text = path.read_text()
    block = text.split("EXPERIMENT 2")[1].split("EXPERIMENT 3")[0]

    rows = re.findall(r"parent holds\s+(\d+) MB -> fork\+wait\s+([\d.]+) ms", block)
    if len(rows) != 8:
        raise SystemExit(f"expected 8 EXP2 rows, found {len(rows)}")

    sizes = [int(mb) for mb, _ in rows[:4]]
    lazy = [float(ms) for _, ms in rows[:4]]
    forced = [float(ms) for _, ms in rows[4:]]
    return sizes, lazy, forced


def main():
    sizes, lazy, forced = parse(RESULTS)

    fig, ax = plt.subplots(figsize=(7.4, 3.6))
    fig.patch.set_facecolor(SURFACE)
    ax.set_facecolor(SURFACE)

    x = range(len(sizes))
    w = 0.38
    # 2px surface gap between adjacent bars: achieved with the offset below
    left = [i - w / 2 - 0.012 for i in x]
    right = [i + w / 2 + 0.012 for i in x]

    b1 = ax.bar(left, lazy, w, label="child exits immediately (nothing copied)",
                color=BLUE, zorder=3)
    b2 = ax.bar(right, forced, w, label="child writes every page (copy forced)",
                color=ORANGE, zorder=3)

    # log scale: the range is 0.2 ms to 185 ms, 800x -- linear hides the small bars
    ax.set_yscale("log")
    ax.set_ylim(0.1, 900)

    # direct labels on every bar, so the log axis never has to be read precisely
    for bars in (b1, b2):
        for rect in bars:
            h = rect.get_height()
            ax.annotate(f"{h:.2f}", (rect.get_x() + rect.get_width() / 2, h),
                        xytext=(0, 3), textcoords="offset points",
                        ha="center", va="bottom", fontsize=7.5, color=INK2)

    # the finding: the ratio at each size
    for i, (a, b) in enumerate(zip(lazy, forced)):
        ax.annotate(f"{b / a:.1f}x", (i, max(a, b) * 2.6),
                    ha="center", fontsize=8.5, color="#c0392b", weight="bold")

    ax.set_xticks(list(x))
    ax.set_xticklabels([f"{s} MB" for s in sizes], fontsize=9, color=INK)
    ax.set_xlabel("memory the parent is holding when it forks", fontsize=9, color=INK2)
    ax.set_ylabel("fork + wait, ms  (log scale)", fontsize=9, color=INK2)

    ax.yaxis.set_major_locator(FixedLocator([0.1, 1, 10, 100]))
    ax.yaxis.set_minor_locator(FixedLocator([]))
    ax.yaxis.set_major_formatter(FuncFormatter(
        lambda v, _: f"{v:g}" if v >= 1 else f"{v}"))
    ax.tick_params(axis="y", labelsize=8, colors=INK2)
    ax.tick_params(axis="x", length=0)

    ax.grid(axis="y", color=GRID, linewidth=0.8, zorder=0)
    ax.set_axisbelow(True)
    for side in ("top", "right", "left"):
        ax.spines[side].set_visible(False)
    ax.spines["bottom"].set_color(GRID)

    ax.legend(loc="upper left", fontsize=8.5, frameon=False, labelcolor=INK2)

    ax.set_title("fork() does not copy the parent's memory — until something writes",
                 fontsize=10.5, color=INK, weight="bold", loc="left", pad=28)

    fig.tight_layout()
    fig.savefig(OUT, facecolor=SURFACE, bbox_inches="tight")
    print(f"wrote {OUT}")
    print(f"  sizes  {sizes}")
    print(f"  lazy   {lazy}")
    print(f"  forced {forced}")
    print(f"  ratios {[round(b / a, 1) for a, b in zip(lazy, forced)]}")


if __name__ == "__main__":
    main()
