#!/usr/bin/env python3
"""Compile and run the actual Arena AXI guards/converters with a byte scoreboard."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

SOURCE = Path(__file__).resolve().parent
ROOT = SOURCE.parents[1]
SYSTEM = ROOT / 'src/rtl_model/io_system'
VENDOR = SYSTEM / 'arena/vendor'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build-arena/width-tests')
    parser.add_argument('--seed', type=lambda x: int(x, 0), action='append')
    parser.add_argument('--timeout', type=int, default=180)
    parser.add_argument('--vcs-home', type=Path,
                        default=Path('/nfs/tools/synopsys/vcs/V-2023.12-SP1-1'))
    parser.add_argument('--vcs-env', type=Path, default=Path('/nfs/home/guoyaxing/vcs_env'))
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    sources = json.loads((SYSTEM / 'arena/sources.json').read_text())
    # Use the exact lzc/rr_arb_tree modules embedded in pinned E6 source. The
    # generated cores are not elaborated by this bounded AXI bridge test.
    embedded = (VENDOR / 'src/RA/src/E6/core/ChiselCoreWrapper.sv').read_text()
    extracted = output / 'embedded_common_cells.sv'
    bodies = []
    for module in ['lzc', 'rr_arb_tree']:
        match = re.search(r'^module\s+' + module + r'\b.*?^endmodule\b[^\n]*',
                          embedded, re.M | re.S)
        if not match:
            raise SystemExit(f'missing original E6 module {module}')
        bodies.append(match.group(0))
    extracted.write_text('\n'.join(bodies) + '\n')
    packages = [VENDOR / p for p in sources['packages']
                if p.endswith('/axi_pkg.sv') or p.endswith('/cf_math_pkg.sv') or
                p.endswith('/E6FPU_pkg.sv')]
    modules = [VENDOR / p for p in sources['modules']
               if p.startswith('src/axi/') or '/common_cells/src/' in p]
    files = packages + [extracted] + modules + [
        SYSTEM / 'rtl/io_system_rtl_arena_pkg.sv',
        SYSTEM / 'rtl/io_system_rtl_axi_guard.sv', SOURCE / 'width_tb.sv']
    origin = VENDOR / 'src/RA/src/E6/core/ChiselCoreWrapper.sv'
    source_hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in files + [origin, SYSTEM / 'arena/SOURCE_MANIFEST.json']}
    filelist = output / 'width.f'
    filelist.write_text('+define+IO_SYSTEM_RTL_ARENA\n' +
        ''.join(f'+incdir+{VENDOR / p}\n' for p in sources['include_dirs']) +
        ''.join(f'{p}\n' for p in files))
    env = os.environ.copy()
    sourced = subprocess.run(['bash', '-c', 'set +u; source "$1"; env -0',
                              'width-env', str(args.vcs_env)], check=True,
                             stdout=subprocess.PIPE)
    for item in sourced.stdout.split(b'\0'):
        if b'=' in item:
            key, value = item.split(b'=', 1)
            env[key.decode()] = value.decode()
    env['VCS_HOME'] = str(args.vcs_home)
    env['PATH'] = str(args.vcs_home / 'bin') + ':' + env.get('PATH', '')
    exe = output / 'simv'
    command = ['bash', str(args.vcs_home / 'bin/vcs'), '-full64', '-sverilog',
               '-timescale=1ns/1ps', '-f', str(filelist), '-top', 'width_tb',
               '-Mdir=' + str(output / 'csrc'), '-o', str(exe),
               '-l', str(output / 'compile.log'), '-j2']
    (output / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    with (output / 'compile.stdout').open('w') as stream:
        result = subprocess.run(command, cwd=output, env=env, stdout=stream,
                                stderr=subprocess.STDOUT, timeout=args.timeout)
    if result.returncode:
        raise SystemExit(f'VCS build failed; see {output / "compile.stdout"}')
    reports = []
    for seed in args.seed or [0x1234, 0x5a5a, 0xc0ffee]:
        log = output / f'seed-{seed}.log'
        returncode = None
        timed_out = False
        with log.open('w') as stream:
            try:
                result = subprocess.run([str(exe), f'+seed={seed}'], cwd=output,
                                        env=env, stdout=stream, stderr=subprocess.STDOUT,
                                        timeout=args.timeout)
                returncode = result.returncode
            except subprocess.TimeoutExpired:
                timed_out = True
        text = log.read_text()
        passed = returncode == 0 and 'AXI_WIDTH_RTL_PASS' in text
        reports.append({'seed': seed, 'passed': passed, 'returncode': returncode,
                        'timed_out': timed_out,
                        'log': str(log), 'summary': [line for line in text.splitlines()
                                                  if 'WIDTH_' in line]})
        print(('PASS' if passed else 'FAIL'), seed, log, flush=True)
        if not passed:
            print(text[-5000:])
            break
    manifest = {'tests': reports, 'sha256': source_hashes,
        'source_commit': json.loads((SYSTEM / 'arena/SOURCE_MANIFEST.json').read_text())['commit'],
        'vcs_home': str(args.vcs_home), 'top': 'width_tb'}
    (output / 'result.json').write_text(json.dumps(manifest, indent=2) + '\n')
    raise SystemExit(0 if len(reports) == len(args.seed or [1, 2, 3]) and
                     all(t['passed'] for t in reports) else 1)


if __name__ == '__main__':
    main()
