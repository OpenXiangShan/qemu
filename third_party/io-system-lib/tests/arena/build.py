#!/usr/bin/env python3
"""Build a combined QEMU host / RTL E6 ELF without the original testbench."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

SOURCE = Path(__file__).resolve().parent
MODES = {"poll": 0, "no-poll": 1, "wfi": 2}


def build(output, mode="poll", case=0, illegal=False, quiet_ticks=30000000):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    prefix = os.environ.get("CROSS_COMPILE", "riscv64-unknown-elf-")
    if not shutil.which(prefix + "gcc"):
        raise SystemExit(f"missing compiler: {prefix}gcc")
    flags = ["-march=rv64imac_zicsr_zifencei", "-mabi=lp64", "-mcmodel=medany",
             "-mno-relax", "-msmall-data-limit=0", "-ffreestanding",
             "-fno-builtin", "-fno-stack-protector", "-fno-pie", "-O2", "-g",
             "-Wall", "-Wextra", "-Werror", f"-DTEST_MODE={MODES[mode]}",
             f"-DTEST_CASE={case}", f"-DE6_ILLEGAL={int(illegal)}",
             f"-DQUIET_TICKS={quiet_ticks}ULL"]
    commands = []
    objects = []
    for name in ["start.S", "host.c", "e6.S"]:
        obj = output / (Path(name).stem + ".o")
        command = [prefix + "gcc", *flags, "-c", str(SOURCE / name), "-o", str(obj)]
        subprocess.run(command, check=True)
        commands.append(command)
        objects.append(str(obj))
    elf = output / "arena-gemm.elf"
    command = [prefix + "gcc", *flags, "-nostdlib", "-nostartfiles", "-static",
               "-Wl,--build-id=none", f"-Wl,-T,{SOURCE / 'link.ld'}",
               f"-Wl,-Map,{output / 'arena-gemm.map'}", *objects, "-o", str(elf)]
    subprocess.run(command, check=True)
    commands.append(command)
    subprocess.run([prefix + "objcopy", "-O", "binary", str(elf),
                    str(output / "arena-gemm.bin")], check=True)
    with (output / "arena-gemm.dis").open("w") as stream:
        subprocess.run([prefix + "objdump", "-d", str(elf)], stdout=stream, check=True)
    sources = [SOURCE / name for name in ["start.S", "host.c", "e6.S", "link.ld"]]
    artifacts = [elf, output / "arena-gemm.bin"]
    manifest = {"mode": mode, "case": case, "illegal": illegal,
                "quiet_ticks": quiet_ticks, "commands": commands,
                "sha256": {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in sources + artifacts},
                "expected": [16, -18, 26, -2, 35, -17] if case == 0 else
                            [-2, 1, -10, 7, 9, 6]}
    (output / "build.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return elf


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path,
                        default=SOURCE.parents[1] / "build-arena/baremetal")
    parser.add_argument("--mode", choices=MODES, default="poll")
    parser.add_argument("--case", type=int, choices=[0, 1], default=0)
    parser.add_argument("--illegal", action="store_true")
    parser.add_argument("--quiet-ticks", type=int, default=30000000)
    args = parser.parse_args()
    print(build(args.output, args.mode, args.case, args.illegal, args.quiet_ticks))


if __name__ == "__main__":
    main()
