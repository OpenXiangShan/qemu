#!/usr/bin/env python3
"""Emit the absolute picker source list for baseline or Arena RTL-system."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--arena', action='store_true')
args = parser.parse_args()
system = Path(__file__).resolve().parents[1]
root = system.parents[2]
rtl = system / 'rtl'
if args.arena:
    print('+define+IO_SYSTEM_RTL_ARENA')
    print(subprocess.check_output([
        'python3', str(system / 'scripts/arena-filelist.py'), '--verify'
    ], text=True), end='')
for source in (rtl / 'io_system_rtl_filelist.f').read_text().splitlines():
    if not source.strip() or source.lstrip().startswith('//'):
        continue
    if args.arena and source.endswith('/aplic/vendor/axi_pkg.sv'):
        continue
    if args.arena and source.endswith('/io_system_rtl_wrapper.sv'):
        for name in ('io_system_rtl_arena_pkg.sv', 'io_system_rtl_axi_guard.sv',
                     'io_system_rtl_e6_width.sv',
                     'io_system_rtl_arena.sv'):
            print(rtl / name)
    print(root / source)
