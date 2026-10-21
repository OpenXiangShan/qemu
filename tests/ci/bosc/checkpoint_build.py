"""Build current checkpoint guest fixtures into the relocatable CI bundle.
SPDX-License-Identifier: GPL-2.0-or-later
"""
from pathlib import Path
import shutil
import subprocess

from common import HERE, SOURCE, read_json, run_logged, sha256, write_json

GUEST = HERE / "checkpoint" / "guest"
PROFILES = (
    ("single-s", "single", "0x1", "0x1:0:0:0", 1, 1),
    ("single-u", "single", "0x1", "0x1:0:0:0", 1, 0),
    ("single-4c", "single", "0x1", "0xf:0:0:0", 15, 1),
    ("multi-2c", "multi", "0x3", "0x1:0x1:0:0", 0x10001, 1),
    ("sparse-4c", "multi", "0x5", "0x5:0:0x5:0", 0x500000005, 1),
)


def source_files():
    return [*sorted(GUEST.glob("*")),
            HERE / "checkpoint/prepare-rootfs.sh",
            SOURCE / "contrib/kmh-checkpoint/Makefile",
            SOURCE / "contrib/kmh-checkpoint/kmh-zstd-unpack.c",
            *sorted(p for p in (SOURCE / "third_party/kmh-portable-restore").iterdir()
                    if p.is_file())]


def build_fixtures(work, bundle, cases):
    """Compile into work/, never in the source checkout or test worker."""
    if not any(c["kind"] == "checkpoint" for c in cases):
        return {}
    sources_before = {str(p.relative_to(SOURCE)): sha256(p) for p in source_files()}
    dest = bundle / "checkpoint"
    dest.mkdir()
    cc = "riscv64-linux-gnu-gcc"
    commands = []

    def compile_guest(name, source, linker=None, defines=(), static_c=False):
        cmd = [cc, "-static", "-Os", "-Wall", "-Wextra", "-Werror"]
        if not static_c:
            cmd += ["-nostdlib", "-no-pie", "-march=rv64gcv", "-mabi=lp64d",
                    "-Wl,--build-id=none", "-T", str(GUEST / linker)]
        cmd += ["-D" + d for d in defines]
        cmd += [str(GUEST / source), "-o", str(dest / name)]
        run_logged(cmd, work / "checkpoint-build" / (name + ".log"), timeout=120)
        commands.append(cmd)

    for name, mode, _, _, mask, privilege in PROFILES:
        compile_guest(name + ".elf", "kmh-bosc-native-stages.S",
                      "kmh-bosc-native-stages.ld",
                      [f"HART_MASK={mask}", f"SNAP_PRIV={privilege}",
                       "UART_PA=" + ("0x04000000" if mode == "single" else "0x100004000000")])
    for name, define in (("reslice", "SECOND_SLICE"), ("interval", "INTERVAL_TRIGGER"),
                         ("qmp", "QMP_TRIGGER"), ("unsupported-m", "SNAP_PRIV=3")):
        compile_guest(name + ".elf", "kmh-bosc-native-stages.S",
                      "kmh-bosc-native-stages.ld", [define])
    for name, defines in (("legacy-single", []),
                          ("legacy-multi", ["EXTRA_DDR_ADDR=0x1081000000"]),
                          ("legacy-qmp", ["QMP_TRIGGER"])):
        compile_guest(name + ".elf", "kmh-bosc-stages.S", "kmh-bosc-stages.ld", defines)
    compile_guest("linux-stages", "kmh-bosc-linux-stages.c", static_c=True)
    compile_guest("litmus-pid1", "kmh-bosc-litmus-pid1.c", static_c=True)
    stub_build = (work / "checkpoint-build" / "legacy-stub").resolve()
    run_logged(["make", "-C", SOURCE / "third_party/kmh-portable-restore",
                "BUILD_DIR=" + str(stub_build)],
               work / "checkpoint-build/legacy-stub.log", timeout=120)
    shutil.copy2(stub_build / "kmh-portable-restore.bin", dest / "legacy-stub.bin")
    run_logged(["make", "-C", SOURCE / "contrib/kmh-checkpoint",
                "TOOL=" + str((dest / "kmh-zstd-unpack").resolve())],
               work / "checkpoint-build/unpack.log", timeout=120)
    version = subprocess.check_output([cc, "--version"], text=True).splitlines()[0]
    sources_after = {str(p.relative_to(SOURCE)): sha256(p) for p in source_files()}
    if sources_before != sources_after:
        raise RuntimeError("checkpoint fixture sources changed during compilation")
    metadata = {"compiler": version, "commands": commands,
                "source_files": sources_before,
                "files": {p.name: sha256(p) for p in dest.iterdir() if p.is_file()}}
    write_json(dest / "fixtures.json", metadata)
    return {str(p.relative_to(bundle)): sha256(p) for p in dest.iterdir() if p.is_file()}


def verify_fixtures(bundle):
    directory = bundle / "checkpoint"
    metadata = read_json(directory / "fixtures.json")
    current = {str(p.relative_to(SOURCE)): sha256(p) for p in source_files()}
    if metadata["source_files"] != current:
        raise ValueError("checkpoint fixture sources differ from the tested build")
    for name, digest in metadata["files"].items():
        if Path(name).name != name or sha256(directory / name) != digest:
            raise ValueError("checkpoint fixture bundle mismatch: " + name)
