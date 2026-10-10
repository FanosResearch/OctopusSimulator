#!/usr/bin/env python3
"""Compare saved runs in AXIS/SETTING/Summary.csv; never launches simulations."""
import argparse
import csv
import math
from pathlib import Path
import re


def natural_key(value):
    return [int(s) if s.isdigit() else s.lower() for s in re.split(r'(\d+)', value)]


def load_settings(axis):
    settings = []
    for directory in sorted(axis.iterdir(), key=lambda p: natural_key(p.name)):
        if not directory.is_dir():
            continue
        source = directory / 'Summary.csv'
        if not source.exists():
            if list(directory.glob('LatencyReport_C*.csv')):
                raise ValueError(f'{directory}: Summary.csv missing; run may be incomplete')
            continue
        with source.open(newline='') as stream:
            reader = csv.DictReader(stream)
            headers = reader.fieldnames or []
            rows = list(reader)
        required = ['Core Id', 'Finish Cycle', 'Average Latency', 'Worst-case Total Latency']
        if not rows or any(h not in headers for h in required):
            raise ValueError(f'{source}: empty or unsupported summary')
        cores = [r['Core Id'] for r in rows]
        if len(set(cores)) != len(cores):
            raise ValueError(f'{source}: duplicate core rows')
        reports = {p.stem.removeprefix('LatencyReport_C') for p in directory.glob('LatencyReport_C*.csv')}
        if reports and reports != set(cores):
            raise ValueError(f'{source}: core rows do not match latency reports; run may be incomplete')
        metrics = {}
        for header in headers:
            if header not in required[1:] and not header.startswith('Worst-case '):
                continue
            try:
                values = [float(r[header]) for r in rows]
            except (ValueError, TypeError):
                raise ValueError(f'{source}: invalid values in {header}') from None
            if any(not math.isfinite(v) or v < 0 for v in values):
                raise ValueError(f'{source}: invalid values in {header}')
            metrics[header] = sum(values) / len(values) if header == 'Average Latency' else max(values)
        settings.append((directory.name, metrics))
    if len(settings) < 2:
        raise ValueError(f'{axis}: need at least two setting folders with Summary.csv')
    return settings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('axis', type=Path, help='e.g. results/Arbiter')
    parser.add_argument('-o', '--out', type=Path, help='figure directory (default: AXIS/figures)')
    args = parser.parse_args()
    try:
        settings = load_settings(args.axis)
    except (OSError, ValueError) as error:
        parser.error(str(error))

    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.colors import LogNorm
    import numpy as np

    out = args.out or args.axis / 'figures'
    out.mkdir(parents=True, exist_ok=True)
    labels = [name for name, _ in settings]
    fig, axes = plt.subplots(1, 3, figsize=(max(11, len(labels) * 1.8), 4.8))
    for ax, (metric, title), color in zip(axes, [
        ('Finish Cycle', 'Finish cycle (last request completion)'),
        ('Average Latency', 'Mean effective latency'),
        ('Worst-case Total Latency', 'Worst-case total latency'),
    ], ['#3977b7', '#23856c', '#b64d4d']):
        values = [m[metric] for _, m in settings]
        bars = ax.bar(labels, values, color=color)
        ax.bar_label(bars, labels=[f'{v:,.2f}' if metric == 'Average Latency' else f'{v:,.0f}' for v in values], padding=4, fontsize=9)
        ax.set_title(title, fontsize=11)
        ax.set_ylabel('Cycles')
        ax.set_ylim(0, max(values) * 1.2 if max(values) else 1)
        ax.tick_params(axis='x', labelrotation=25)
        ax.grid(axis='y', alpha=.2)
        ax.set_axisbelow(True)
    fig.suptitle(f'{args.axis.name}: comparison across settings')
    fig.text(.5, .015, 'Average: unweighted mean of per-core averages. Worst case and finish: maximum across cores.', ha='center', fontsize=9)
    fig.tight_layout(rect=(0, .06, 1, .94))
    save(fig, out / 'comparison')
    plt.close(fig)

    excluded = {'Worst-case Total Latency', 'Worst-case Effective Latency', 'Worst-case Oldest Latency'}
    stages = list(dict.fromkeys(h for _, m in settings for h in m if h.startswith('Worst-case ') and h not in excluded))
    if stages:
        data = np.array([[m.get(stage, np.nan) for _, m in settings] for stage in stages])
        positive = data[np.isfinite(data) & (data > 0)]
        norm = LogNorm(vmin=max(1, positive.min()), vmax=max(2, positive.max())) if positive.size else None
        fig, ax = plt.subplots(figsize=(max(7, len(labels) * 1.3), max(4, len(stages) * .55)))
        im = ax.imshow(np.ma.masked_where(~np.isfinite(data) | (data <= 0), data), aspect='auto', cmap='Blues', norm=norm)
        ax.set_xticks(range(len(labels)), labels, rotation=25, ha='right')
        ax.set_yticks(range(len(stages)), [s.removeprefix('Worst-case ').replace('Requst', 'Request') for s in stages])
        for i in range(len(stages)):
            for j in range(len(labels)):
                value = data[i, j]
                ax.text(j, i, f'{value:,.0f}' if np.isfinite(value) else 'N/A', ha='center', va='center', color='white' if value > 0 and norm is not None and norm(value) > .6 else 'black')
        fig.colorbar(im, ax=ax, label='Cycles (log color scale)' if norm else 'Cycles')
        ax.set_title(f'{args.axis.name}: independent worst-case stage latencies')
        fig.text(.5, .015, 'Each cell is a separate maximum; stage maxima do not sum to worst-case total latency.', ha='center', fontsize=9)
        fig.tight_layout(rect=(0, .06, 1, 1))
        save(fig, out / 'stages')
        plt.close(fig)

    fields = list(dict.fromkeys(h for _, m in settings for h in m))
    with (out / 'metrics.csv').open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=['setting', *fields])
        writer.writeheader()
        for name, metrics in settings:
            writer.writerow({'setting': name, **metrics})
    print(f'Compared {len(settings)} settings; figures and metrics: {out}')


def save(fig, stem):
    for extension in ('png', 'pdf'):
        fig.savefig(stem.with_suffix('.' + extension), dpi=160)


if __name__ == '__main__':
    main()
