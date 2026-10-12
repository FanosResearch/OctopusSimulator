#!/usr/bin/env python3
"""Check that the readable summary exactly transposes the original CSV.
Usage: python3 test/cli/summary_transposed.py [path/to/Octopus_Simulator]
"""
import csv
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'build/Octopus_Simulator'
ENV = {key: value for key, value in os.environ.items() if not key.startswith('OCTOPUS_')}


def main():
    with tempfile.TemporaryDirectory(prefix='octopus-summary-test-') as tmp:
        base = Path(tmp)
        workload = base / 'workload'
        workload.mkdir()
        # Different request counts exercise cores completing at different times.
        for core, count in enumerate([64, 1, 8, 3]):
            (workload / f'trace_C{core}.trc.shared').write_text(''.join(
                f'{4096 + core * 64:x} 1 R {i * 4}\n' for i in range(count)))
        for cores in [1, 4]:
            out = base / str(cores)
            result = subprocess.run([
                str(BINARY), '-s', 'MultiCoreSystem', '-c', 'MultiCoreSystem_Snoop',
                '-p', f'num_cores(i)={cores}', '-p', f'workload_path(s)={workload}/',
                '-o', str(out),
            ], cwd=ROOT, env=ENV, capture_output=True, text=True, timeout=30)
            assert result.returncode == 0, result.stdout + result.stderr
            with (out / 'Summary.csv').open(newline='') as stream:
                original = list(csv.reader(stream))
            with (out / 'Summary_transposed.csv').open(newline='') as stream:
                transposed = list(csv.reader(stream))
            assert len(original) == cores + 1, original
            ordered = sorted(original[1:], key=lambda row: int(row[0]))
            expected = [['Metric', *(f'Core {row[0]}' for row in ordered)]]
            expected.extend([metric, *(row[i] for row in ordered)]
                            for i, metric in enumerate(original[0][1:], start=1))
            assert transposed == expected, (transposed, expected)
    print('PASS: one-core and four-core summaries transpose every metric exactly, in core-ID order.')


if __name__ == '__main__':
    main()
