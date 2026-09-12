#!/usr/bin/env python3
"""Build and validate the complete AXI-only Arena integration; retain evidence."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

from build import build

SOURCE = Path(__file__).resolve().parent
ROOT = SOURCE.parents[1]
QEMU = ROOT.parents[1]
SYSTEM = ROOT / 'src/rtl_model/io_system'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build-arena/acceptance')
    parser.add_argument('--qemu-build', type=Path, default=QEMU / 'build-qti-check')
    parser.add_argument('--vcs-home', type=Path,
                        default=Path('/nfs/tools/synopsys/vcs/V-2023.12-SP1-1'))
    parser.add_argument('--vcs-env', type=Path, default=Path('/nfs/home/guoyaxing/vcs_env'))
    parser.add_argument('--jobs', type=int, default=8)
    parser.add_argument('--only', choices=['engine', 'width', 'soc', 'legacy', 'qemu'])
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    reports = []
    env = os.environ.copy()
    result = subprocess.run(['bash', '-c', 'set +u; source "$1"; env -0',
                             'arena-env', str(args.vcs_env)], check=True,
                            stdout=subprocess.PIPE)
    for field in result.stdout.split(b'\0'):
        if b'=' in field:
            key, value = field.split(b'=', 1)
            env[key.decode()] = value.decode()
    env['VCS_HOME'] = str(args.vcs_home)
    env['VCS_LIBDIR'] = str(args.vcs_home / 'linux64/lib')
    env['PATH'] = str(args.vcs_home / 'bin') + ':/nfs/share/unitychip/bin:' + env['PATH']
    env['IO_SYSTEM_BUILD_JOBS'] = str(args.jobs)

    def run(name, command, *, marker=None, child_env=None, timeout=1800):
        log = output / (name + '.log')
        start = time.monotonic()
        print(f'RUN {name}: {log}', flush=True)
        error = None
        rc = None
        with log.open('w') as stream:
            try:
                proc = subprocess.run([str(x) for x in command], cwd=ROOT,
                                      env=child_env or env, stdout=stream,
                                      stderr=subprocess.STDOUT, timeout=timeout)
                rc = proc.returncode
            except subprocess.TimeoutExpired:
                error = 'timeout'
        text = log.read_text(errors='replace')
        passed = rc == 0 and (marker is None or marker in text)
        reports.append({'name': name, 'passed': passed, 'returncode': rc,
                        'error': error, 'seconds': time.monotonic() - start,
                        'command': [str(x) for x in command], 'log': str(log)})
        (output / 'summary.json').write_text(json.dumps(reports, indent=2) + '\n')
        print(('PASS ' if passed else 'FAIL ') + name, flush=True)
        if not passed:
            raise SystemExit(f'{name} failed; see {log}')

    def make(*options):
        return ['make', '-C', ROOT, f'-j{args.jobs}', *options]

    run('source-verify', ['python3', SYSTEM / 'scripts/arena-filelist.py', '--verify'])
    if args.only in (None, 'engine'):
        run('engine', make(f'BUILD_DIR={output / "engine-build"}', 'arena-engine-test'),
            marker='AXI_MASTER_ENGINE_PASS')
        run('cmodel', make('IO_SYSTEM_RTL_ARENA=0', 'IO_SYSTEM_RTL_SYSTEM=0',
                          f'BUILD_DIR={output / "cmodel-build"}',
                          f'OUTPUT_DIR={output / "cmodel-output"}', 'test'))
    if args.only in (None, 'width'):
        run('width', ['python3', SOURCE / 'width_run.py', '--output', output / 'width',
                      '--vcs-home', args.vcs_home, '--vcs-env', args.vcs_env])
    if args.only in (None, 'legacy'):
        run('legacy', make('IO_SYSTEM_RTL_ARENA=0', 'IO_SYSTEM_RTL_SYSTEM=1',
                          f'BUILD_DIR={output / "legacy-build"}',
                          f'OUTPUT_DIR={output / "legacy-output"}', 'test'))
    if args.only in (None, 'soc', 'qemu'):
        run('picker', make('IO_SYSTEM_RTL_ARENA=1', 'output-arena/libio_rtl_system_api.so'))
        if args.only is None:
            run('arena-legacy-devices', make('IO_SYSTEM_RTL_ARENA=1', 'test'))
        # Runtime snapshots keep tests independent from subsequent rebuilds.
        package = output / 'package'
        package.mkdir(exist_ok=True)
        source_package = ROOT / 'output-arena'
        shutil.copy2(source_package / 'libio_rtl_system_api.so', package)
        picker = package / 'rtl-system-picker'
        picker.mkdir(exist_ok=True)
        for path in (source_package / 'rtl-system-picker').iterdir():
            if path.name.endswith(('.so', '.json')):
                shutil.copy2(path, picker)
            elif path.name.endswith('.daidir'):
                shutil.copytree(path, picker / path.name, dirs_exist_ok=True)
        library = package / 'libio_rtl_system_api.so'
        runtime_env = env.copy()
        runtime_env['LD_LIBRARY_PATH'] = f'{picker}:{env["VCS_LIBDIR"]}:' + env.get('LD_LIBRARY_PATH', '')
        runtime_env['LD_PRELOAD'] = ':'.join(str(picker / n) for n in [
            'libDPIio_system_rtl_wrapper.so', 'libUTio_system_rtl_wrapper.so'])
        manifest = {str(p.relative_to(package)): hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in package.rglob('*') if p.is_file()}
        (package / 'sha256.json').write_text(json.dumps(manifest, indent=2) + '\n')
        if args.only in (None, 'soc'):
            filelist = output / 'soc.f'
            with filelist.open('w') as stream:
                subprocess.run(['python3', str(SYSTEM / 'scripts/system-filelist.py'),
                                '--arena'], stdout=stream, env=env, check=True)
            config_test = output / 'config-smoke'
            run('config-build', ['bash', args.vcs_home / 'bin/vcs', '-full64', '-sverilog',
                '-timescale=1ns/1ps', '-f', filelist,
                SYSTEM / 'rtl/tests/io_system_rtl_arena_smoke_tb.sv',
                '-top', 'io_system_rtl_arena_smoke_tb',
                '-Mdir=' + str(output / 'config-csrc'), '-o', config_test, '-j2'])
            run('config-smoke', [config_test, '-no_save'], marker='ARENA_RTL_CONFIG_SMOKE_PASS')
            images = [build(output / f'case{i}', case=i).with_suffix('.bin') for i in (0, 1)]
            standalone = output / 'standalone'
            run('standalone-build', ['g++', '-std=c++17', '-O2', '-g', '-Wall', '-Wextra',
                '-Werror', '-I' + str(ROOT / 'include'), '-I' + str(SYSTEM / 'include'),
                SOURCE / 'standalone.cpp', '-ldl', '-o', standalone])
            for depth in (1, 4, 16):
                run(f'soc-depth{depth}', [standalone, library, *images, depth],
                    marker='ARENA STANDALONE CLOSED', child_env=runtime_env)
        if args.only in (None, 'qemu'):
            binary = args.qemu_build.resolve() / 'qemu-system-riscv64'
            run('qemu-build', ['ninja', '-C', args.qemu_build, 'qemu-system-riscv64',
                              'tests/qtest/qemu-to-iosystem-test'])
            qtest_env = runtime_env.copy()
            qtest_env['QTEST_QEMU_BINARY'] = str(binary)
            qtest_env['IO_RTL_SYSTEM_API_SO'] = str(library)
            qtest_env['IO_RTL_SYSTEM_VCS_LIBDIR'] = env['VCS_LIBDIR']
            run('qtest', [args.qemu_build.resolve() / 'tests/qtest/qemu-to-iosystem-test'],
                child_env=qtest_env)
            run('continuous', ['python3', SOURCE / 'test_continuous.py', '--qemu', binary,
                               '--output', output / 'continuous'])
            run('qemu-gemm', ['python3', SOURCE / 'run_qemu.py', '--qemu', binary,
                '--library', library, '--vcs-libdir', env['VCS_LIBDIR'],
                '--suite', '--pause-resume', '--output', output / 'qemu'], timeout=2400)
    print('ARENA_ACCEPTANCE_PASS', flush=True)


if __name__ == '__main__':
    main()
