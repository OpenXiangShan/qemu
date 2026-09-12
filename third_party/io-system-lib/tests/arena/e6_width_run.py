#!/usr/bin/env python3
"""Validate the production E6 RID-aware width wrapper with Verilator."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
SYSTEM = ROOT / 'src/rtl_model/io_system'
VENDOR = SYSTEM / 'arena/vendor'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build-arena/e6-width')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    sources = json.loads((SYSTEM / 'arena/sources.json').read_text())
    origin = VENDOR / 'src/RA/src/E6/core/ChiselCoreWrapper.sv'
    embedded = origin.read_text()
    extracted = out / 'embedded_common_cells.sv'
    bodies = []
    for name in ['lzc', 'rr_arb_tree']:
        match = re.search(r'^module\s+' + name + r'\b.*?^endmodule\b[^\n]*', embedded, re.M | re.S)
        if not match:
            raise RuntimeError('missing embedded module '+name)
        bodies.append(match.group(0))
    extracted.write_text('\n'.join(bodies)+'\n')
    packages = [VENDOR / p for p in sources['packages'] if p.endswith(('/axi_pkg.sv', '/cf_math_pkg.sv', '/E6FPU_pkg.sv'))]
    modules = [VENDOR / p for p in sources['modules'] if p.startswith('src/axi/') or '/common_cells/src/' in p]
    legacy = VENDOR / 'src/RA/src/IP/AXIAXI_64T512'
    files = packages + [extracted] + modules + [legacy / (name+'.v') for name in ['AXIBRF_64T512','AXIBUF_64T512','AXISPF','AXISRF','OSLLMT']] + [SYSTEM / 'rtl/io_system_rtl_arena_pkg.sv', SYSTEM / 'rtl/io_system_rtl_axi_guard.sv', SYSTEM / 'rtl/io_system_rtl_e6_width.sv', Path(__file__).with_name('e6_width_tb.sv')]
    command = ['verilator','--binary','--timing','--assert','-j','4','-Wno-fatal','+define+IO_SYSTEM_RTL_ARENA','--top-module','e6_width_tb','--Mdir',str(out/'obj_dir'),'-o','e6_width']
    command += ['+incdir+'+str(VENDOR / p) for p in sources['include_dirs']]
    command += list(map(str, files))
    (out/'command.json').write_text(json.dumps(command,indent=2)+'\n')
    hashes = {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in files+[origin,Path(__file__).resolve()]}
    with (out/'compile.log').open('w') as f:
        built=subprocess.run(command,stdout=f,stderr=subprocess.STDOUT,timeout=240)
    if built.returncode:
        raise SystemExit('build failed: '+str(out/'compile.log'))
    start=time.monotonic()
    with (out/'run.log').open('w') as f:
        run=subprocess.run([str(out/'obj_dir/e6_width')],stdout=f,stderr=subprocess.STDOUT,timeout=30)
    log=(out/'run.log').read_text()
    passed=run.returncode==0 and 'E6_WIDTH_PASS ' in log
    result={'passed':passed,'returncode':run.returncode,'seconds':time.monotonic()-start,'evidence':[s for s in log.splitlines() if s.startswith('E6_WIDTH_')],'sha256':hashes}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(log,end='')
    raise SystemExit(0 if passed else 1)


if __name__=='__main__':
    main()
