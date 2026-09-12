#!/usr/bin/env python3
"""Run real QEMU+picker E6/AME tests; accept numeric UART results, never exit alone."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import socket
import subprocess
import tempfile
import time

from build import SOURCE, build

IO_SYSTEM = SOURCE.parents[1]
QEMU = IO_SYSTEM.parents[1]
RESULT = re.compile(r"ARENA RESULT index=(0x[0-9a-f]+) actual=(0x[0-9a-f]+) "
                    r"expected=(0x[0-9a-f]+)")


def runtime_environment(library, vcs_libdir=None):
    env = dict(os.environ)
    picker = library.resolve().parent / "rtl-system-picker"
    library_dirs = [picker, library.resolve().parent]
    if vcs_libdir:
        library_dirs.append(vcs_libdir.resolve())
    env["LD_LIBRARY_PATH"] = ":".join(map(str, library_dirs)) + ":" + env.get("LD_LIBRARY_PATH", "")
    # Picker/VCS uses static TLS; reserve it before QEMU creates vCPU threads.
    preload = [picker / "libDPIio_system_rtl_wrapper.so",
               picker / "libUTio_system_rtl_wrapper.so"]
    if all(p.is_file() for p in preload):
        env["LD_PRELOAD"] = ":".join(map(str, preload)) + (
            ":" + env["LD_PRELOAD"] if env.get("LD_PRELOAD") else "")
    return env


def qmp_command(path, command):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.settimeout(5)
        client.connect(str(path))
        stream = client.makefile("rwb", buffering=0)
        json.loads(stream.readline())
        for execute in ["qmp_capabilities", command]:
            stream.write(json.dumps({"execute": execute}).encode() + b"\n")
            while True:
                reply = json.loads(stream.readline())
                if "error" in reply:
                    raise RuntimeError(reply["error"])
                if "return" in reply:
                    break


def run(args, mode, case, depth, illegal=False):
    name = f"{mode}-case{case}-depth{depth}" + ("-illegal" if illegal else "")
    output = args.output.resolve() / name
    elf = build(output, mode, case, illegal, args.quiet_ticks)
    trace = output / "axi.log"
    with tempfile.TemporaryDirectory(prefix="arena-qmp-") as temporary:
        qmp = Path(temporary) / "qmp.sock"
        machine = ",".join(["qemu_to_iosystem", "generated-dtb=off",
                            "io-system-backend=rtl-system",
                            "io-system-service-mode=bh",
                            "io-system-rtl-continuous=on",
                            f"io2q-outstanding={depth}",
                            f"io-system-backend-lib={args.library.resolve()}",
                            f"io-system-trace-file={trace}"])
        if args.vcs_libdir:
            machine += f",io-system-vcs-libdir={args.vcs_libdir.resolve()}"
        command = [str(args.qemu.resolve()), "-M", machine, "-smp", "1", "-m", "128M",
                   "-accel", "tcg,thread=single", "-bios", "none", "-display", "none",
                   "-monitor", "none", "-serial", "stdio", "-qmp",
                   f"unix:{qmp},server=on,wait=off", "-device",
                   f"loader,file={elf},cpu-num=0", "-d", "guest_errors"]
        (output / "command.json").write_text(json.dumps(command, indent=2) + "\n")
        start = time.monotonic()
        captured = bytearray()
        failure = None
        marker = False
        paused = False
        resume_at = None
        proc = subprocess.Popen(command, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, cwd=output,
                                env=runtime_environment(args.library, args.vcs_libdir))
        selector = selectors.DefaultSelector()
        selector.register(proc.stdout, selectors.EVENT_READ)
        with (output / "qemu.log").open("wb") as log:
            try:
                while time.monotonic() - start < args.timeout:
                    if resume_at and time.monotonic() >= resume_at:
                        qmp_command(qmp, "cont")
                        resume_at = None
                    for key, _ in selector.select(timeout=0.1):
                        block = os.read(key.fd, 65536)
                        if not block:
                            selector.unregister(key.fileobj)
                            continue
                        log.write(block)
                        log.flush()
                        captured.extend(block)
                    text = captured.decode(errors="replace")
                    if (args.pause_resume and mode != "poll" and not paused and
                            "ARENA QUIET" in text and "BEGIN" in text):
                        qmp_command(qmp, "stop")
                        resume_at = time.monotonic() + 1
                        paused = True
                    if "ARENA FAIL " in text or "async service failed" in text:
                        failure = "guest/runtime failure"
                        break
                    final = ("ARENA EXPECTED-FAIL reason=e6-illegal phase=0xe002"
                             if illegal else "ARENA PASS results=6 guards=128 ack=0xff")
                    if final in text:
                        marker = True
                        break
                    if proc.poll() is not None:
                        failure = "QEMU exited before final verdict"
                        break
                else:
                    failure = f"wall-clock timeout ({args.timeout}s)"
            except (OSError, RuntimeError, ValueError) as error:
                failure = f"runner error: {error}"
            finally:
                if proc.poll() is None:
                    try:
                        qmp_command(qmp, "quit")
                        proc.wait(timeout=10)
                    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired):
                        proc.terminate()
                        try:
                            proc.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            proc.kill()
                            proc.wait()
                tail = proc.stdout.read()
                captured.extend(tail)
                log.write(tail)
                selector.close()
        text = captured.decode(errors="replace")
        if marker and not illegal:
            expected = [16, -18, 26, -2, 35, -17] if case == 0 else [-2, 1, -10, 7, 9, 6]
            values = [(int(i, 16), int(actual, 16), int(ref, 16))
                      for i, actual, ref in RESULT.findall(text)]
            want = [(i, v & ((1 << 64) - 1), v & ((1 << 64) - 1))
                    for i, v in enumerate(expected)]
            if values != want:
                failure = "UART result records differ from independent expected values"
        if marker and args.pause_resume and mode != "poll" and not paused:
            failure = "pause/resume point was not reached"
        if "ARENA FAIL " in text or "async service failed" in text:
            failure = failure or "late guest/runtime failure"
        if proc.returncode != 0:
            failure = failure or f"QEMU shutdown failed (exit {proc.returncode})"
        if "unexpected termination" in text or "Segmentation fault" in text:
            failure = failure or "simulator crashed after guest verdict"
        success = marker and failure is None
        report = {"name": name, "passed": success, "failure": failure,
                  "seconds": time.monotonic() - start, "qemu_exit": proc.returncode,
                  "pause_resume": paused,
                  "library_sha256": hashlib.sha256(args.library.read_bytes()).hexdigest(),
                  "qemu_sha256": hashlib.sha256(args.qemu.read_bytes()).hexdigest(),
                  "last_phase_lines": [s for s in text.splitlines() if "ARENA" in s][-12:]}
        (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        print(f"{'PASS' if success else 'FAIL'} {name}: {output / 'qemu.log'}", flush=True)
        if not success:
            print(failure)
            print(text[-4000:])
        return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, default=QEMU / "build-qti-check/qemu-system-riscv64")
    parser.add_argument("--library", type=Path, default=IO_SYSTEM / "output-arena/libio_rtl_system_api.so")
    parser.add_argument("--vcs-libdir", type=Path)
    parser.add_argument("--output", type=Path, default=IO_SYSTEM / "build-arena/qemu-tests")
    parser.add_argument("--mode", choices=["poll", "no-poll", "wfi"], default="poll")
    parser.add_argument("--case", type=int, choices=[0, 1], default=0)
    parser.add_argument("--depth", type=int, choices=[1, 4, 16], default=16)
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--quiet-ticks", type=int, default=30000000)
    parser.add_argument("--illegal", action="store_true")
    parser.add_argument("--suite", action="store_true")
    parser.add_argument("--pause-resume", action="store_true")
    args = parser.parse_args()
    for path in [args.qemu, args.library]:
        if not path.is_file():
            parser.error(f"missing required artifact: {path}")
    tests = ([("poll", case, depth, False) for depth in [1, 4, 16] for case in [0, 1]] +
             [("no-poll", 0, 16, False), ("wfi", 0, 16, False), ("poll", 0, 16, True)]
             if args.suite else [(args.mode, args.case, args.depth, args.illegal)])
    reports = []
    for test in tests:
        report = run(args, *test)
        reports.append(report)
        if not report["passed"]:
            break
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "summary.json").write_text(json.dumps(reports, indent=2) + "\n")
    raise SystemExit(0 if len(reports) == len(tests) and all(r["passed"] for r in reports) else 1)


if __name__ == "__main__":
    main()
