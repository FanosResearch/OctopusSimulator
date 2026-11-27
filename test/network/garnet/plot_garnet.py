#!/usr/bin/env python3
"""Latency versus offered load, Octopus against gem5 Garnet, one figure per mesh, from
compare_garnet.py's points.csv files (docs/NoC_Garnet_Validation.md). Needs matplotlib; not part of
the test suite.
  plot_garnet.py <output dir> <points.csv> [<points.csv> ...]"""
import csv, os, sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

PATTERNS = ['uniform_random', 'transpose', 'bit_complement', 'bit_reverse', 'shuffle', 'tornado', 'neighbor', 'bit_rotation']
SERIES = (
    ('garnet', 'latency_garnet_units', dict(color='#1f4e79', marker='o', label='Garnet, head sent to tail received')),
    ('octopus', 'latency_garnet_units', dict(color='#c55a11', marker='s', linestyle='--', label='Octopus + 5 cycles')),
    ('garnet', 'latency_stock_units', dict(color='#7f7f7f', marker='.', linestyle=':', label='Garnet stock network latency + 1')),
)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    rows = []
    for src in sys.argv[2:]:
        rows += [r for r in csv.DictReader(open(src)) if r['stable'] == '1' and r['latency_garnet_units']]
    for mesh in sorted({r['mesh'] for r in rows}):
        vcs = {r.get('vcs', '') for r in rows if r['mesh'] == mesh}
        fig, axes = plt.subplots(2, 4, figsize=(14, 6.4))
        for ax, pattern in zip(axes.flat, PATTERNS):
            for sim, col, style in SERIES:
                pts = sorted((float(r['offered']), float(r[col]) + (1 if col == 'latency_stock_units' else 0)) for r in rows
                             if r['mesh'] == mesh and r['pattern'] == pattern and r['simulator'] == sim and r[col])
                if pts:
                    ax.plot([p[0] for p in pts], [p[1] for p in pts], markersize=3.5, linewidth=1.2, **style)
            ax.set_title(pattern, fontsize=10)
            ax.set_xlabel('offered load (flits/cycle/node)', fontsize=8)
            ax.set_ylabel('latency (cycles)', fontsize=8)
            ax.tick_params(labelsize=7); ax.grid(alpha=0.3)
        axes.flat[0].legend(fontsize=7, loc='upper left')
        fig.suptitle(f'{mesh} mesh, XY, 4 VCs, 4-flit buffers, 5-flit packets (stable points only)', fontsize=11)
        fig.tight_layout()
        fig.savefig(os.path.join(out, f'garnet_{mesh}.png'), dpi=110)
        plt.close(fig)


if __name__ == '__main__':
    main()
