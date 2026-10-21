#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build a derived fw_jump with native checkpoint regions in its embedded DT."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifact', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--opensbi', type=Path,
                        required=True, help='explicit OpenSBI source tree; preparation only')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    dtb = out / 'native-checkpoint.dtb'
    original = args.artifact.resolve() / 'kmh-bosc-soc-2die32c-isolcpus16-31.dtb'
    shutil.copy2(original, dtb)
    for name, words in (
        ('kmh-native@80300000', ['0', '80300000', '0', '100000']),
        ('kmh-checkpoint@92000000', ['0', '92000000', '0', '400000']),
        ('kmh-checkpoint-cpu@c7c000000', ['c', '7c000000', '0', '4000000']),
    ):
        node = '/reserved-memory/' + name
        subprocess.run(['fdtput', '-c', str(dtb), node], check=True)
        subprocess.run(['fdtput', '-t', 'x', str(dtb), node, 'reg'] + words, check=True)
        subprocess.run(['fdtput', str(dtb), node, 'no-map'], check=True)
    with (out / 'native-checkpoint.dts').open('w') as dst:
        subprocess.run(['dtc', '-I', 'dtb', '-O', 'dts', str(dtb)], stdout=dst, check=True)
    command = ['make', '-C', str(args.opensbi.resolve()),
               'O=' + str(out / 'opensbi-build'), 'PLATFORM=generic',
               'CROSS_COMPILE=riscv64-linux-gnu-', 'FW_TEXT_START=0x80000000',
               'FW_JUMP_FDT_OFFSET=0x200000', 'FW_JUMP_OFFSET=0x400000',
               'FW_FDT_PATH=' + str(dtb), '-j32']
    (out / 'build-command.json').write_text(json.dumps(command, indent=2) + '\n')
    with (out / 'opensbi-build.log').open('wb') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    built = out / 'opensbi-build/platform/generic/firmware'
    for suffix in ('bin', 'elf'):
        shutil.copy2(built / ('fw_jump.' + suffix), out / ('fw_jump.' + suffix))
    symbols = subprocess.check_output(['riscv64-linux-gnu-nm', '-n',
                                      str(out / 'fw_jump.elf')], text=True)
    start = fdt = None
    for line in symbols.splitlines():
        fields = line.split()
        if len(fields) == 3:
            if fields[2] == '_fw_start': start = int(fields[0], 16)
            if fields[2] == 'fw_fdt_bin': fdt = int(fields[0], 16)
    assert start == 0x80000000 and fdt is not None
    blob = (out / 'fw_jump.bin').read_bytes()
    data = dtb.read_bytes()
    assert blob[fdt - start:fdt - start + len(data)] == data
    (out / 'provenance.json').write_text(json.dumps({
        'base_artifact': str(args.artifact.resolve()),
        'source_head': subprocess.check_output(['git', 'rev-parse', 'HEAD'],
                    cwd=args.opensbi, text=True).strip(),
        'original_dtb_sha256': hashlib.sha256(original.read_bytes()).hexdigest(),
        'derived_dtb_sha256': hashlib.sha256(data).hexdigest(),
        'embedded_dtb_offset': hex(fdt - start),
        'embedded_dtb_verified': True,
        'fw_jump_sha256': hashlib.sha256(blob).hexdigest(),
    }, indent=2) + '\n')


if __name__ == '__main__':
    main()
