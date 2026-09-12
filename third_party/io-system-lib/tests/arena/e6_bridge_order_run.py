#!/usr/bin/env python3
"""Reproduce the unmodified E6 bridge's global read-order limitation.

Exit 0 means ordered controls passed AND legal reversed/interleaved reads
exposed wrong data lanes. It does not mean general AXI ordering is supported.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time


def main():
    source = Path(__file__).resolve().parent
    root = source.parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=root / 'build-arena/e6-bridge-order')
    parser.add_argument('--verilator', default='verilator')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    vendor = root / 'src/rtl_model/io_system/arena/vendor/src/RA/src/IP/AXIAXI_64T512'
    files = [vendor / name for name in [
        'AXIAXO_64T512.v', 'ADRWID_64T512.v', 'DATWID_64T512.v',
        'WIDCON_64T512.v', 'AXISPF.v']]
    files.append(source / 'e6_bridge_order_tb.sv')
    command = [args.verilator, '--binary', '--timing', '-j', '2',
               '-Wno-fatal', '--top-module', 'e6_bridge_order_tb',
               '--Mdir', str(output / 'obj_dir'), '-o', 'e6_bridge_order',
               *map(str, files)]
    (output / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    with (output / 'compile.log').open('w') as log:
        built = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                               cwd=output, timeout=180)
    if built.returncode:
        raise SystemExit(f'Verilator build failed; see {output / "compile.log"}')
    start = time.monotonic()
    with (output / 'run.log').open('w') as log:
        run = subprocess.run([str(output / 'obj_dir/e6_bridge_order')],
                             cwd=output, stdout=log, stderr=subprocess.STDOUT,
                             timeout=15)
    seconds = time.monotonic() - start
    text = (output / 'run.log').read_text()
    evidence = [line for line in text.splitlines()
                if line.startswith(('IN_ORDER_PASS ', 'EXPECTED_LIMITATION ',
                                    'E6_BRIDGE_ORDER_LIMITATION_CONFIRMED'))]
    confirmed = (run.returncode == 0 and
                 sum(line.startswith('IN_ORDER_PASS ') for line in evidence) == 2 and
                 sum(line.startswith('EXPECTED_LIMITATION ') for line in evidence) == 2 and
                 'E6_BRIDGE_ORDER_LIMITATION_CONFIRMED' in evidence)
    report = {'limitation_confirmed': confirmed, 'returncode': run.returncode,
              'seconds': seconds, 'evidence': evidence,
              'meaning': 'Ordered controls pass; legal cross-ID reordering/interleaving '
                         'corrupts data while RID is preserved.',
              'sha256': {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                         for path in files + [Path(__file__).resolve()]}}
    (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(text, end='')
    print(f'Evidence: {output / "result.json"}')
    raise SystemExit(0 if confirmed else 1)


if __name__ == '__main__':
    main()
