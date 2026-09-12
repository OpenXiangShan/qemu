#!/usr/bin/env python3
"""Test QEMU owner-thread clock/WFI/pause behavior with a non-RTL counter library."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time

from build import SOURCE
from run_qemu import IO_SYSTEM, QEMU, qmp_command


def wait_until(predicate, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.02)
    raise AssertionError("condition did not become true")


def run(args, continuous, wfi):
    name = f"continuous-{int(continuous)}-wfi-{int(wfi)}"
    output = args.output.resolve() / name
    output.mkdir(parents=True, exist_ok=True)
    stats = output / "stats.bin"
    stats.write_bytes(bytes(4096))
    source = output / "probe.S"
    source.write_text(".global _start\n_start:\nli t0, 0x50000000\nlw t1, 0(t0)\n"
                      "1:\n" + ("wfi\n" if wfi else "nop\n") + "j 1b\n")
    elf = output / "probe.elf"
    subprocess.run([os.environ.get("CROSS_COMPILE", "riscv64-unknown-elf-") + "gcc",
                    "-nostdlib", "-static", "-Wl,-Ttext=0x80000000",
                    "-Wl,--build-id=none", "-o", str(elf), str(source)], check=True)

    def values():
        return struct.unpack("=5Q", stats.read_bytes()[:40])

    with tempfile.TemporaryDirectory(prefix="arena-clock-") as temporary:
        qmp = Path(temporary) / "qmp.sock"
        machine = ("qemu_to_iosystem,generated-dtb=off,io-system-backend=rtl-system,"
                   "io-system-service-mode=bh,io-system-backend-lib=" + str(args.library))
        if continuous:
            machine += ",io-system-rtl-continuous=on"
        command = [str(args.qemu.resolve()), "-M", machine, "-smp", "1", "-m", "128M",
                   "-accel", "tcg,thread=single", "-bios", "none", "-display", "none",
                   "-monitor", "none", "-serial", "none", "-device",
                   f"loader,file={elf},cpu-num=0", "-qmp",
                   f"unix:{qmp},server=on,wait=off"]
        env = dict(os.environ, ARENA_CLOCK_PROBE=str(stats))
        with (output / "qemu.log").open("w") as log:
            proc = subprocess.Popen(command, stdout=log, stderr=log, env=env)
            try:
                wait_until(lambda: values()[2] == 1)
                first = values()[1]
                if continuous:
                    wait_until(lambda: values()[1] > first + 128)
                    qmp_command(qmp, "stop")
                    stopped = values()[1]
                    time.sleep(0.2)
                    assert values()[1] == stopped, "RTL clock advanced during pause"
                    qmp_command(qmp, "cont")
                    wait_until(lambda: values()[1] > stopped + 128)
                else:
                    time.sleep(0.2)
                    stopped = values()[1]
                    time.sleep(0.2)
                    assert values()[1] == stopped, "default mode continuously clocked"
                qmp_command(qmp, "quit")
                proc.wait(timeout=10)
                result = values()
                assert result[2] == 1, "guest made unexpected MMIO accesses"
                assert result[4] == 0, "simulation API called from non-owner thread"
                if continuous:
                    assert result[3] == 1, "runtime not closed on owner thread"
                assert proc.returncode == 0, proc.returncode
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()
    report = dict(name=name, passed=True, owner=result[0], cycles=result[1],
                  mmio=result[2], close=result[3], wrong_thread=result[4])
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS {name}: {report}", flush=True)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, default=QEMU / "build-qti-check/qemu-system-riscv64")
    parser.add_argument("--output", type=Path, default=IO_SYSTEM / "build-arena/clock-tests")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    args.library = (args.output / "libclock_probe.so").resolve()
    subprocess.run([os.environ.get("CC", "cc"), "-shared", "-fPIC", "-O0",
                    "-I" + str(IO_SYSTEM / "include"),
                    "-I" + str(IO_SYSTEM / "src/rtl_model/io_system/include"),
                    str(SOURCE / "clock_probe.c"), "-o", str(args.library)], check=True)
    reports = [run(args, continuous, wfi) for continuous, wfi in
               [(False, False), (True, False), (True, True)]]
    (args.output / "summary.json").write_text(json.dumps(reports, indent=2) + "\n")


if __name__ == "__main__":
    main()
