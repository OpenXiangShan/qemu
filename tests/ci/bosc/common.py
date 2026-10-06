"""Shared local/CI primitives. SPDX-License-Identifier: GPL-2.0-or-later"""
import contextlib
import datetime
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parents[2]


def read_json(path):
    return json.loads(Path(path).read_text())


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + f".{os.getpid()}.tmp")
    tmp.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
    tmp.replace(path)


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


@contextlib.contextmanager
def locked(path):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a") as stream:
        fcntl.flock(stream, fcntl.LOCK_EX)
        yield


def stop_group(proc):
    # Every caller starts its own session. Never signal an unrelated process.
    for sig in (signal.SIGTERM, signal.SIGKILL):
        try:
            os.killpg(proc.pid, sig)
        except ProcessLookupError:
            break
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            continue
        # A parent may exit before its children. Kill only its own group.
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        break
    proc.wait(timeout=5)


def run_logged(argv, log, *, cwd=None, env=None, timeout=120, on_poll=None, expected_exit=0):
    """Bounded command with process-group cleanup, including cancellation."""
    log = Path(log)
    log.parent.mkdir(parents=True, exist_ok=True)
    write_json(log.with_suffix(log.suffix + ".command.json"), list(map(str, argv)))
    with log.open("wb") as stream:
        proc = subprocess.Popen(list(map(str, argv)), cwd=cwd, env=env,
                                stdout=stream, stderr=subprocess.STDOUT,
                                start_new_session=True)
        try:
            if on_poll is None:
                code = proc.wait(timeout=timeout)
            else:
                deadline = time.monotonic() + timeout
                while True:
                    on_poll()
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise subprocess.TimeoutExpired(argv, timeout)
                    try:
                        code = proc.wait(timeout=min(0.1, remaining))
                        break
                    except subprocess.TimeoutExpired:
                        continue
            if code != expected_exit:
                raise RuntimeError(f"command exited {code}; see {log}")
        finally:
            stop_group(proc)


def git(*args, source=SOURCE):
    return subprocess.check_output(["git", "-C", str(source), *args],
                                   stderr=subprocess.PIPE)


def source_identity():
    # New source files (for example a qtest) are also build inputs. Do not hash
    # unrelated untracked artifacts or read outside source/test directories.
    untracked = {}
    for name in git("ls-files", "--others", "--exclude-standard", "-z",
                    "--", "hw", "include", "libqemu", "my-virtio-lib", "tests/libqemu",
                    "tests/qtest", "tests/ci/bosc").decode().split("\0"):
        if name and (SOURCE / name).suffix in (".c", ".h", ".py", ".json"):
            untracked[name] = sha256(SOURCE / name)
    return {"revision": git("rev-parse", "HEAD").decode().strip(),
            "tracked_diff_sha256": hashlib.sha256(git("diff", "HEAD", "--binary")).hexdigest(),
            "untracked_sources": untracked,
            "status": git("status", "--porcelain").decode().splitlines()}


def catalog():
    cases = read_json(HERE / "cases.json")["cases"]
    if len({c["id"] for c in cases}) != len(cases):
        raise ValueError("duplicate case ID")
    return cases


def pcie_segment(bus):
    if bus in ("dw-pcie", "dw-pcie1"):
        return int(bus == "dw-pcie1")
    match = re.fullmatch(r"pcie-d([0-3])-p([0-2])", bus)
    if not match:
        raise ValueError(f"unknown PCIe bus: {bus}")
    return int(match[1]) * 3 + int(match[2])


def config_hash():
    digest = hashlib.sha256()
    for path in sorted([*HERE.glob("*.json"), *HERE.glob("*.py")]):
        digest.update(path.name.encode())
        digest.update(path.read_bytes())
    return digest.hexdigest()
