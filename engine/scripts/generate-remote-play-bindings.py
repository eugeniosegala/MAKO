#!/usr/bin/env python3
"""Bundle one shared implementation for independently installed components."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check', action='store_true')
arguments = parser.parse_args()
root = Path(__file__).resolve().parents[2]
source = root / 'engine/scripts/mako_remote_play'
destination = root / 'plugin/py_modules/mako_plugin'
names = ('remote_play_core.py', 'remote_play_launch.py', 'managed_files.py')
stale = []
for name in names:
    expected = (source / name).read_bytes()
    output = destination / name
    if not output.exists() or output.read_bytes() != expected:
        if arguments.check:
            stale.append(name)
        else:
            output.write_bytes(expected)
if stale:
    parser.error('Shared Remote Play bindings are stale: ' + ', '.join(stale))
print('Shared Remote Play bindings are current.' if arguments.check else 'Generated shared Remote Play bindings.')
