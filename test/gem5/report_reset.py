#!/usr/bin/env python3
"""Build and run the standalone gem5 API/report-reset regression against build/libOctopus.so.
Usage: python3 test/gem5/report_reset.py [build-directory]
Requires a C++17 compiler (CXX or g++), but no gem5 installation.
"""
import csv
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'build'


def main():
    env = {key: value for key, value in os.environ.items() if not key.startswith('OCTOPUS_')}
    with tempfile.TemporaryDirectory(prefix='octopus-report-reset-') as tmp:
        base = Path(tmp)
        includes = [ROOT / 'header', *(p for p in (ROOT / 'header').iterdir() if p.is_dir())]
        executable = base / 'report_reset'
        command = shlex.split(os.environ.get('CXX', 'g++')) + ['-O1', '-std=c++17']
        command += [f'-I{path}' for path in includes]
        command += [str(ROOT / 'test/gem5/report_reset.cpp'), f'-L{BUILD}', '-lOctopus',
                    f'-Wl,-rpath,{BUILD}', '-o', str(executable)]
        subprocess.run(command, check=True, env=env)
        workload = base / 'workload'
        (workload / 'newLogger').mkdir(parents=True)
        result = subprocess.run([str(executable), str(workload)], cwd=ROOT, env=env,
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 0 and 'PASS:' in result.stdout, result.stdout + result.stderr
        with (workload / 'newLogger/Summary.csv').open(newline='') as stream:
            rows = list(csv.DictReader(stream))
        assert [row['Core Id'] for row in rows] == ['5'], rows
        print(result.stdout.strip())


if __name__ == '__main__':
    main()
