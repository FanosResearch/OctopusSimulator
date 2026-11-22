#!/usr/bin/env python3
"""Latency versus offered load, Octopus against BookSim, one figure per routing, from compare.py's
points.csv (docs/NoC_BookSim_Validation.md). Needs matplotlib; not part of the test suite.
  plot.py <points.csv> <output dir>"""
import csv, os, sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

PATTERNS = ['uniform', 'transpose', 'bitcomp', 'bitrev', 'shuffle', 'tornado']


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    rows = [r for r in csv.DictReader(open(src)) if r['stable'] == '1' and r['latency_bs_units']]
    for routing in sorted({r['routing'] for r in rows}):
        fig, axes = plt.subplots(2, 3, figsize=(11, 6.2), sharey=False)
        for ax, pattern in zip(axes.flat, PATTERNS):
            for sim, style in (('booksim', dict(color='#1f4e79', marker='o', label='BookSim 2 (network latency)')),
                               ('octopus', dict(color='#c55a11', marker='s', linestyle='--', label='Octopus + 3 cycles'))):
                pts = sorted((float(r['offered']), float(r['latency_bs_units'])) for r in rows
                             if r['routing'] == routing and r['pattern'] == pattern and r['simulator'] == sim)
                if pts:
                    ax.plot([p[0] for p in pts], [p[1] for p in pts], markersize=3.5, linewidth=1.2, **style)
            ax.set_title(pattern, fontsize=10)
            ax.set_xlabel('offered load (flits/cycle/node)', fontsize=8)
            ax.set_ylabel('latency (cycles)', fontsize=8)
            ax.tick_params(labelsize=7); ax.grid(alpha=0.3)
        axes.flat[0].legend(fontsize=7, loc='upper left')
        fig.suptitle(f'8x8 mesh, {routing}, 2 VCs, 4-flit buffers, 5-flit packets (stable points only)', fontsize=11)
        fig.tight_layout()
        fig.savefig(os.path.join(out, f'booksim_{routing}.png'), dpi=110)
        plt.close(fig)


if __name__ == '__main__':
    main()
