"""Native library tests and locked-image Linux boot. GPL-2.0-or-later."""
import os
import re
import selectors
import shlex
import struct
import subprocess
import time
import uuid

from build import bundle_dir
from checks import FATAL, clean, libqemu_host_assertions, linux_assertions
from common import run_logged, write_json


def execute(work, case, paths, directory):
    if case["kind"] == "libqemu-compose-linux":
        from compose_runner import execute as compose_execute
        return compose_execute(work, case, paths, directory)
    bundle = bundle_dir(work, case)
    library = bundle / "libqemu-system-riscv64.so"
    launcher = [bundle / "libqemu" / case["binary"], library]
    if case["kind"] == "libqemu-unit":
        command = [*launcher, *case["arguments"]]
        run_logged(command, directory / "qemu.log", timeout=case["timeout"])
        text = clean((directory / "qemu.log").read_text(errors="replace"))
        marker = case["pass_marker"]
        if FATAL.search(text) or not re.search(rf"(?m)^{re.escape(marker)}(?: |$)", text):
            raise AssertionError("missing libqemu success marker or fatal output")
        return {"marker": marker, "exitcode": 0}, []
    if case["split_init"]:
        launcher.append("--split-init")
    if case["kind"] == "libqemu-exit":
        statuses = []
        for code in (0, 7):
            guest = directory / f"finisher-{code}.bin"
            value = (code << 16) | 0x5555
            words = [0x001002b7, (((value + 0x800) >> 12) << 12) | 0x337,
                     ((value & 0xfff) << 20) | 0x30313, 0x0062a023, 0x0000006f]
            guest.write_bytes(struct.pack("<5I", *words))
            command = [*launcher, "-M", "virt", "-m", "128M", "-bios", "none",
                       "-display", "none", "-monitor", "none", "-serial", "none",
                       "-accel", "tcg", "-device",
                       f"loader,file={guest},addr=0x80000000,cpu-num=0"]
            log = directory / f"exit-{code}.log"
            run_logged(command, log, timeout=45, expected_exit=code)
            libqemu_host_assertions(log.read_text(errors="replace"), code)
            statuses.append(code)
        return {"exit_statuses": statuses, "host_continues": statuses,
                "split_init": case["split_init"]}, []
    from runner import bootargs, dt_options, machine, prepare_dtb
    _, dt = prepare_dtb(library, case, paths, directory, launcher=launcher)
    write_json(directory / "dt-evidence.json", dt)
    trigger = directory / "boot-rootfs.trigger"
    trigger.write_text(f'[cmd]\nbootargs: "{bootargs(case)}"\n[cmd_end]\n[task]\n[task_end]\n')
    command = [*launcher, "-L", library.parent / "share/qemu",
               *machine(case, extra=dt_options()), "-bios", paths["fw_jump"],
               "-display", "none", "-monitor", "none", "-serial", "stdio",
               "-device", "loader,file=" + paths["kernel"] + ",addr=0x80400000",
               "-device", "loader,file=" + paths["rootfs"] + ",addr=0xa0000000",
               "-device", f"loader,file={trigger},addr=0x90000000"]
    (directory / "replay-command.txt").write_text(shlex.join(map(str, command)) + "\n")
    evidence = linux_host(command, case, directory)
    evidence["dt"] = dt
    return evidence, []


def linux_host(command, case, directory):
    """Keep the host alive after API shutdown, then release it via control pipe."""
    from runner import shell_probe
    token = "BOSC_CI_" + uuid.uuid4().hex
    read_fd, write_fd = os.pipe()
    # The launcher consumes control-fd before split-init and QEMU options.
    command = list(map(str, command))
    command[2:2] = ["--control-fd", str(read_fd)]
    proc = None
    captured = ""
    shutdown = False
    host_alive = False
    sent = False
    deadline = time.monotonic() + case["timeout"]
    try:
        # Stay in the worker process group so its outer deadline also kills us.
        proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, pass_fds=(read_fd,))
        write_json(directory / "process.json", {"pid": proc.pid, "argv": command})
        os.close(read_fd)
        read_fd = None
        with selectors.DefaultSelector() as selector, (directory / "uart0.log").open("wb") as log:
            selector.register(proc.stdout, selectors.EVENT_READ)
            while time.monotonic() < deadline:
                for key, _ in selector.select(0.1):
                    data = os.read(key.fileobj.fileno(), 65536)
                    if not data:
                        raise RuntimeError("libqemu host closed output before verification")
                    log.write(data)
                    log.flush()
                    captured += data.decode(errors="replace")
                normalized = clean(captured)
                if FATAL.search(normalized):
                    raise AssertionError("fatal libqemu Linux output")
                if proc.poll() is not None:
                    raise RuntimeError("libqemu host exited before API shutdown verification")
                if not sent and re.search(r"\[root@.*\]#\s*$", normalized):
                    proc.stdin.write(shell_probe(token).encode())
                    proc.stdin.flush()
                    sent = True
                if sent and not shutdown and re.search(rf"(?m)^{token}:END$", normalized):
                    linux_assertions(normalized, token, case)
                    os.write(write_fd, b"q")
                    shutdown = True
                if shutdown and re.search(r"(?m)^HOST_CONTINUES status=0$", normalized):
                    libqemu_host_assertions(normalized, 0)
                    host_alive = proc.poll() is None
                    if not host_alive:
                        raise AssertionError("host did not survive wait()")
                    os.write(write_fd, b"x")
                    tail, _ = proc.communicate(timeout=10)
                    log.write(tail)
                    captured += tail.decode(errors="replace")
                    break
            else:
                raise TimeoutError("libqemu Linux/host shutdown deadline exceeded")
        if proc.returncode != 0:
            raise AssertionError(f"libqemu host exit status {proc.returncode}")
        libqemu_host_assertions(captured, 0)
        evidence = linux_assertions(captured, token, case)
        evidence.update({"host_continues": 0, "host_alive_after_wait": host_alive,
                         "exitcode": proc.returncode, "split_init": case["split_init"]})
        return evidence
    finally:
        if read_fd is not None:
            os.close(read_fd)
        os.close(write_fd)
        if proc is not None:
            forced = proc.poll() is None
            if forced:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=5)
            for stream in (proc.stdin, proc.stdout):
                if stream:
                    stream.close()
            write_json(directory / "cleanup.json", {"exitcode": proc.returncode,
                       "running": proc.poll() is None, "forced": forced})
