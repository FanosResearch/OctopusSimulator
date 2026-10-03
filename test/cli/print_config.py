#!/usr/bin/env python3
"""Check --PrintConfig destinations and resolved overrides.
Usage: python3 test/cli/print_config.py [path/to/Octopus_Simulator]
"""
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'build/Octopus_Simulator'
ENV = {key: value for key, value in os.environ.items() if not key.startswith('OCTOPUS_')}


def main():
    with tempfile.TemporaryDirectory(prefix='octopus-print-config-') as tmp:
        base = Path(tmp)
        workload = base / 'workload'
        workload.mkdir()
        for core in range(4):
            (workload / f'trace_C{core}.trc.shared').write_text(f'{4096 + core * 64:x} 1 R 0\n')
        common = [str(BINARY), '-s', 'MultiCoreSystem', '-c', 'MultiCoreSystem_Snoop',
                  '-p', f'workload_path(s)={workload}/',
                  '-p', 'cache_controller[*].num_mshr(i)=4']

        def run(*options):
            return subprocess.run(common + list(options), cwd=base, env=ENV,
                                  capture_output=True, text=True, timeout=30)

        out = base / 'new output directory'
        result = run('-o', str(out), '--PrintConfig')
        assert result.returncode == 0, result.stdout + result.stderr
        log = (out / 'config.log').read_text()
        assert 'MultiCoreSystem_Snoop.num_cores = 4' in log, log
        assert '.num_mshr = 4' in log, log
        assert '.m_request_latency = ' in log, log
        assert '\x1b' not in log, 'Saved config must not contain terminal color codes'
        assert 'MultiCoreSystem_Snoop.num_cores = ' not in result.stdout, result.stdout
        summary = (out / 'Summary.csv').read_bytes()
        # Reusing the output directory replaces the previous log.
        (out / 'config.log').write_text('stale configuration\n')
        result = run('-o', str(out), '--PrintConfig')
        assert result.returncode == 0, result.stdout + result.stderr
        assert 'stale configuration' not in (out / 'config.log').read_text()
        assert (out / 'Summary.csv').read_bytes() == summary

        plain = base / 'no printing'
        result = run('-o', str(plain))
        assert result.returncode == 0, result.stdout + result.stderr
        assert not (plain / 'config.log').exists()
        assert (plain / 'Summary.csv').read_bytes() == summary

        result = run('--PrintConfig')
        assert result.returncode == 0, result.stdout + result.stderr
        assert 'MultiCoreSystem_Snoop.num_cores = 4' in re.sub(r'\x1b\[[0-9;]*m', '', result.stdout)
        assert not (workload / 'newLogger/config.log').exists()

        blocked = base / 'blocked'
        (blocked / 'config.log').mkdir(parents=True)
        result = run('-o', str(blocked), '--PrintConfig')
        assert result.returncode != 0
        assert 'Cannot write configuration log' in result.stderr
    print('PASS: config file, resolved overrides, plain text, overwrite, stdout fallback, optional flag, and write errors.')


if __name__ == '__main__':
    main()
