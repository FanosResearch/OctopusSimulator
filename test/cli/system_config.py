#!/usr/bin/env python3
"""Smoke-test system CSV selection without changing the repository configuration.
Usage: python3 test/cli/system_config.py [path/to/Octopus_Simulator]
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'build/Octopus_Simulator'
CONFIG = ROOT / 'configuration/SystemConfigurations'
ENV = {k: v for k, v in os.environ.items() if not k.startswith('OCTOPUS_')}


def main():
    original = {p: p.read_bytes() for p in (ROOT / 'configuration').rglob('*.csv')}
    with tempfile.TemporaryDirectory(prefix='octopus-config-test-') as tmp:
        base = Path(tmp)
        work = base / 'work'
        work.mkdir()
        for core in range(4):
            (work / f'trace_C{core}.trc.shared').write_text(''.join(
                f'{4096 + core * 64:x} 1 R {i * 4}\n' for i in range(64)))

        def run(label, system='MultiCoreSystem', options=()):
            out = base / label
            result = subprocess.run([
                str(BINARY), '-s', system, *options,
                '-p', 'workload_path(s)=work/', '-o', str(out),
            ], cwd=base, env=ENV, capture_output=True, text=True, timeout=30)
            assert result.returncode == 0, result.stdout + result.stderr
            summary = (out / 'Summary.csv').read_bytes()
            assert len(summary.splitlines()) == 5, summary
            return summary

        default = run('default')
        assert run('explicit-default', options=['-c', 'MultiCoreSystem']) == default
        snoop = run('snoop', options=['-c', 'MultiCoreSystem_Snoop'])
        assert run('long', options=['--config', 'MultiCoreSystem_Snoop.csv']) == snoop
        assert run('absolute', options=['--config', str(CONFIG / 'MultiCoreSystem_Snoop.csv')]) == snoop
        # Aliases share ordering: the last occurrence wins.
        assert run('last-long', options=['-c', 'missing', '--config', 'MultiCoreSystem_Snoop']) == snoop
        assert run('last-short', options=['--config', 'missing', '-c', 'MultiCoreSystem_Snoop']) == snoop
        run('directory', options=['-c', 'MultiCoreSystem_Directory', '--trace'])
        assert (base / 'directory/trace.bin').read_bytes().startswith(b'OCTV2')
        assert (base / 'directory/trace.bin.names').exists()
        mesh = run('mesh-default', system='MultiCoreSystem_Mesh')
        assert run('mesh-explicit', system='MultiCoreSystem_Mesh', options=['-c', 'MultiCoreSystem_Mesh.csv']) == mesh
        # Extends follows the selected CSV's directory; CLI overrides still win.
        custom = base / 'custom configs'
        custom.mkdir()
        (custom / 'base.csv').write_bytes((CONFIG / 'MultiCoreSystem_Snoop.csv').read_bytes())
        (custom / 'experiment.csv').write_text('Extends,base\nworkload_path(s),does-not-exist/\nnum_cores(i),1\n')
        assert run('custom', options=['--config', './custom configs/experiment.csv', '-p', 'num_cores(i)=4']) == snoop
        for options in [['-c'], ['--config'], ['-c', ''], ['--config', '-p'], ['-c', 'missing']]:
            result = subprocess.run([str(BINARY), '-s', 'MultiCoreSystem', *options], cwd=base,
                                    env=ENV, capture_output=True, text=True, timeout=5)
            assert result.returncode != 0, options
        assert all(p.read_bytes() == contents for p, contents in original.items())
    print('PASS: aliases, presets, paths, defaults, both systems, overrides, tracing, errors; configuration unchanged.')


if __name__ == '__main__':
    main()
