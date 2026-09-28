"""Bounded job-log views of untouched raw artifacts. GPL-2.0-or-later."""
import codecs
import os
from pathlib import Path
import re
import stat
import sys
import time

UART_LOGS = tuple(f"uart{index}.log" for index in range(4))
LIVE_LOGS = (*UART_LOGS, "qemu.log", "qtest.log")
DIAGNOSTIC_LOGS = (*LIVE_LOGS, "exception.log", "worker.log",
                   "dumpdtb.log", "fdtput.log")
READ_CHUNK = 64 * 1024
LINE_CHARS = 2048
TAIL_BYTES = 16 * 1024
TAIL_LINES = 200
ANSI = re.compile(r"\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)|\x1b\[[0-?]*[ -/]*[@-~]")


def visible(text):
    # Guest data must not emit terminal commands or GitLab section markers.
    text = ANSI.sub("", text)
    return "".join(c for c in text if c.isprintable() or c == "\t")


def open_log(path):
    # Never block on a FIFO or follow a log symlink into unrelated files.
    fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
    if not stat.S_ISREG(os.fstat(fd).st_mode):
        os.close(fd)
        raise OSError("not a regular log file")
    return os.fdopen(fd, "rb")


class JobConsole:
    """One budget shared by every case in this run/job, not per UART."""
    def __init__(self, stream=None, live_bytes=1024 * 1024,
                 diagnostic_bytes=512 * 1024, gitlab=None):
        self.stream = stream if stream is not None else sys.stdout
        self.remaining = {"live": live_bytes, "diagnostic": diagnostic_bytes,
                          "meta": 64 * 1024}
        self.limited = set()
        self.disabled = False
        self.gitlab = os.environ.get("GITLAB_CI") == "true" if gitlab is None else gitlab

    @classmethod
    def from_environment(cls):
        kib = int(os.environ.get("BOSC_CI_LIVE_LOG_KIB", "1024"))
        if not 0 <= kib <= 2048:
            raise ValueError("BOSC_CI_LIVE_LOG_KIB must be between 0 and 2048")
        return cls(live_bytes=kib * 1024)

    def write(self, text, kind="meta"):
        if self.disabled:
            return
        size = len(text.encode("utf-8", errors="replace"))
        if size > self.remaining[kind]:
            self.remaining[kind] = 0
            if kind != "meta" and kind not in self.limited:
                self.limited.add(kind)
                self.write(f"[ci-log] {kind} output limit reached; full raw logs remain "
                           "in artifacts. Live and failure-tail budgets are separate.\n")
            return
        self.remaining[kind] -= size
        try:
            self.stream.write(text)
            self.stream.flush()
        except (OSError, ValueError, UnicodeError):
            # Console presentation must not replace the actual guest result.
            self.disabled = True

    def line(self, case_id, label, text, kind="live"):
        self.write(f"[{case_id}][{label}] {visible(text)}\n", kind)

    def section(self, case_id, phase, start, collapsed=True):
        if not self.gitlab:
            return
        name = re.sub(r"[^a-zA-Z0-9_]", "_", f"bosc_{case_id}_{phase}")
        if start:
            option = "[collapsed=true]" if collapsed else ""
            self.write(f"\x1b[0Ksection_start:{int(time.time())}:{name}{option}\r\x1b[0K"
                       f"{case_id}: {phase}\n")
        else:
            self.write(f"\x1b[0Ksection_end:{int(time.time())}:{name}\r\x1b[0K\n")


class LogFollower:
    def __init__(self, path, case_id, console):
        self.path = Path(path)
        self.case_id = case_id
        self.console = console
        self.stream = None
        self.decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
        self.pending = ""
        self.last_data = time.monotonic()
        self.failed = False

    def emit(self, text):
        self.console.line(self.case_id, self.path.stem, text)

    def feed(self, data):
        self.pending += self.decoder.decode(data)
        self.last_data = time.monotonic()
        while self.pending:
            newline = self.pending.find("\n")
            if 0 <= newline <= LINE_CHARS:
                self.emit(self.pending[:newline])
                self.pending = self.pending[newline + 1:]
            elif len(self.pending) >= LINE_CHARS:
                self.emit(self.pending[:LINE_CHARS])
                self.pending = self.pending[LINE_CHARS:]
            else:
                break

    def poll(self, final=False):
        if self.failed or self.console.disabled:
            return
        if self.console.remaining["live"] == 0:
            # Stop scanning flood output; the producer keeps the raw artifact.
            return
        try:
            if self.stream is None:
                self.stream = open_log(self.path)
            if os.fstat(self.stream.fileno()).st_size < self.stream.tell():
                self.stream.seek(0)
                self.decoder.reset()
                self.pending = ""
            # Bounded work per tick, and a bounded final catch-up even for a
            # stream containing only stripped control characters.
            budget = 1024 * 1024 if final else READ_CHUNK
            while budget and self.console.remaining["live"]:
                data = self.stream.read(min(READ_CHUNK, budget))
                if not data:
                    break
                budget -= len(data)
                self.feed(data)
            if final:
                self.pending += self.decoder.decode(b"", final=True)
                if os.fstat(self.stream.fileno()).st_size > self.stream.tell():
                    self.console.write(f"[{self.case_id}] Live catch-up capped for "
                                       f"{self.path.name}; see the full artifact.\n")
            if self.pending and (final or time.monotonic() - self.last_data >= 0.5):
                self.emit(self.pending)
                self.pending = ""
        except FileNotFoundError:
            pass  # QEMU/worker may not have created this UART yet.
        except OSError:
            self.failed = True
            self.console.write(f"[{self.case_id}] Cannot display {self.path.name}; "
                               "check the raw artifact.\n")

    def close(self):
        if self.stream is not None:
            self.stream.close()


class CaseConsole:
    def __init__(self, directory, case_id, console):
        self.directory = Path(directory)
        self.case_id = case_id
        self.console = console
        self.followers = [LogFollower(self.directory / name, case_id, console)
                          for name in LIVE_LOGS]

    def __enter__(self):
        self.console.section(self.case_id, "live", True, collapsed=False)
        self.console.write(f"[{self.case_id}] Raw log artifacts: {self.directory}\n")
        if not self.console.remaining["live"]:
            self.console.write(f"[{self.case_id}] Live display disabled/exhausted; "
                               "raw capture and failure tails remain enabled.\n")
        return self

    def poll(self):
        for follower in self.followers:
            follower.poll()

    def __exit__(self, *_exc):
        try:
            for follower in self.followers:
                try:
                    follower.poll(final=True)
                finally:
                    follower.close()
        finally:
            self.console.section(self.case_id, "live", False)

    def diagnostics(self, reason):
        self.console.section(self.case_id, "failure", True, collapsed=False)
        try:
            self.console.line(self.case_id, "failure", str(reason)[:LINE_CHARS], "diagnostic")
            self.console.write(f"[{self.case_id}] Failure tails: last {TAIL_LINES} lines / "
                               f"{TAIL_BYTES} bytes per file; full logs: {self.directory}\n")
            for name in DIAGNOSTIC_LOGS:
                if not self.console.remaining["diagnostic"]:
                    break
                try:
                    with open_log(self.directory / name) as stream:
                        size = os.fstat(stream.fileno()).st_size
                        if not size:
                            continue
                        stream.seek(max(0, size - TAIL_BYTES))
                        data = stream.read(TAIL_BYTES)
                    lines = data.decode("utf-8", errors="replace").splitlines()[-TAIL_LINES:]
                    for line in lines:
                        suffix = " ... [line truncated; see artifact]" if len(line) > LINE_CHARS else ""
                        self.console.line(self.case_id, name + " tail",
                                          line[:LINE_CHARS] + suffix, "diagnostic")
                except FileNotFoundError:
                    continue
                except OSError:
                    self.console.write(f"[{self.case_id}] Cannot read failure tail: {name}\n")
        finally:
            self.console.section(self.case_id, "failure", False)
