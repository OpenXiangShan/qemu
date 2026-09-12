#!/usr/bin/env python3
"""Build and run the real picker API E6/AME reset/RAM acceptance harness."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

from build import SOURCE, build
from run_qemu import IO_SYSTEM, runtime_environment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, default=IO_SYSTEM / "output-arena/libio_rtl_system_api.so")
    parser.add_argument("--vcs-libdir", type=Path)
    parser.add_argument("--output", type=Path, default=IO_SYSTEM / "build-arena/standalone-tests")
    parser.add_argument("--depth", type=int, choices=[1, 4, 16], default=16)
    parser.add_argument("--timeout", type=int, default=600)
    args = parser.parse_args()
    if not args.library.is_file():
        parser.error(f"missing required artifact: {args.library}")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    images = [build(output / f"case{i}", case=i).with_suffix(".bin") for i in [0, 1]]
    executable = output / "arena-standalone"
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++17", "-O2", "-g",
                    "-Wall", "-Wextra", "-Werror", "-I" + str(IO_SYSTEM / "include"),
                    "-I" + str(IO_SYSTEM / "src/rtl_model/io_system/include"),
                    str(SOURCE / "standalone.cpp"), "-ldl", "-o", str(executable)], check=True)
    command = [str(executable), str(args.library.resolve()), *map(str, images), str(args.depth)]
    (output / "command.json").write_text(json.dumps(command, indent=2) + "\n")
    started = time.monotonic()
    error = None
    with (output / "standalone.log").open("w") as stream:
        try:
            result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT,
                                    cwd=output, timeout=args.timeout,
                                    env=runtime_environment(args.library, args.vcs_libdir))
            if result.returncode:
                error = f"process exit {result.returncode}"
        except subprocess.TimeoutExpired:
            error = f"wall-clock timeout {args.timeout}s"
    text = (output / "standalone.log").read_text(errors="replace")
    if ("ARENA STANDALONE PASS resets=3 cases=3" not in text or
            "ARENA STANDALONE CLOSED" not in text):
        error = error or "missing final verdict"
    report = {"passed": error is None, "error": error,
              "seconds": time.monotonic() - started,
              "sha256": {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in [args.library, executable, *images]}}
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(text[-6000:])
    print(f"{'PASS' if not error else 'FAIL'} standalone: {output / 'standalone.log'}")
    raise SystemExit(0 if error is None else 1)


if __name__ == "__main__":
    main()
