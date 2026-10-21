"""Checkpoint cases using the common build, assets, logs and cleanup lifecycle.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import contextlib
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import time

from checkpoint_build import PROFILES
from checkpoint_checks import evidence_assertions, litmus_assertions, validate_log
from checks import clean
from common import HERE, run_logged, sha256, stop_group, write_json


class CheckpointRun:
    def __init__(self, work, case, paths, directory):
        self.work, self.case, self.paths, self.directory = work, case, paths, directory
        self.scratch = work / "scratch" / case["id"]
        self.scratch.mkdir(parents=True, exist_ok=False)
        self.binary = work / "bundle/qemu-system-riscv64"
        self.fixtures = work / "bundle/checkpoint"
        self.deadline = time.monotonic() + case["timeout"]
        self.commands = []
        self.checks = []
        self.data = {"scenario": case["scenario"]}

    def remaining(self):
        value = self.deadline - time.monotonic()
        if value <= 0:
            raise TimeoutError("checkpoint case deadline expired")
        return value

    def check(self, name):
        self.checks.append({"id": name, "status": "PASS"})

    def command(self, machine, mode="single", ram=None, extra=()):
        return [str(self.binary), "-M", machine, "-smp",
                "16,maxcpus=17" if mode == "single" else "64,maxcpus=68",
                "-m", ram or ("512M" if mode == "single" else "2G"),
                "-accel", "tcg,thread=multi", "-display", "none", "-monitor", "none",
                "-serial", "stdio", "-no-reboot", "-L", str(self.work / "bundle/share/qemu"),
                "-semihosting-config", "enable=on,target=native", *map(str, extra)]

    @contextlib.contextmanager
    def running(self, name, argv):
        log = self.directory / "qemu.log"
        offset = log.stat().st_size if log.exists() else 0
        command = list(map(str, argv))
        self.commands.append(command)
        write_json(self.directory / "commands.json", self.commands)
        write_json(self.directory / (name + ".command.json"), command)
        with log.open("ab") as stream:
            proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=stream,
                                    stderr=subprocess.STDOUT, start_new_session=True)
            def text():
                with log.open("rb") as src:
                    src.seek(offset)
                    return src.read().decode(errors="replace")
            try:
                yield proc, text
            finally:
                stop_group(proc)
                (self.directory / (name + ".log")).write_text(text())

    def run(self, name, argv, success=True):
        with self.running(name, argv) as (proc, text):
            code = proc.wait(timeout=min(90, self.remaining()))
            data = text()
        if (code == 0) != success:
            raise AssertionError(f"{name}: exit={code}; see {name}.log")
        validate_log(data)
        if not success and "KMH_NATIVE_PLATFORM_RESTORED" in data:
            raise AssertionError(name + ": invalid image executed guest firmware")
        return data

    def tool(self, name, argv, success=True, input_data=None):
        if input_data is None:
            run_logged(argv, self.directory / (name + ".log"),
                       timeout=min(180, self.remaining()), expected_exit=0 if success else 1)
        else:
            # The compressor is a host fixture transform, not a guest process.
            with (self.directory / (name + ".log")).open("wb") as log:
                subprocess.run(argv, input=input_data, stdout=log, stderr=subprocess.STDOUT,
                               timeout=min(90, self.remaining()), check=True)

    @property
    def native_machine(self):
        return "kmh-bosc-soc,soc-mode=single,core-mask=1,generated-dtb=on,dw-pcie=off,iommu-sys=off"

    def native(self):
        image = None
        restore = None
        for name, mode, mask, cores, _, _ in PROFILES:
            machine = (f"kmh-bosc-soc,soc-mode={mode},die-mask={mask},core-mask={cores},"
                       "dw-pcie=off,iommu-sys=off,generated-dtb=on")
            image = self.scratch / (name + ".zst")
            create = self.command(machine + f",checkpoint-path={image}", mode,
                                  extra=["-bios", "none", "-kernel", self.fixtures / (name + ".elf")])
            validate_log(self.run(name + "-create", create), ("portable checkpoint committed",),
                         ("NATIVE_HARTS_PASS",))
            restore = self.command(machine, mode, extra=["-bios", image])
            validate_log(self.run(name + "-restore", restore),
                         ("NATIVE_HARTS_PASS", "no host CSR/device fixups"))
            continued = self.command(machine + f",checkpoint-path={self.scratch / (name + '-continue.zst')},checkpoint-exit=off",
                                     mode, extra=["-bios", "none", "-kernel", self.fixtures / (name + ".elf")])
            validate_log(self.run(name + "-continue", continued),
                         ("portable checkpoint committed", "NATIVE_HARTS_PASS"))
            self.check(name)
            if name == "single-s":
                first_image, first_restore = image, restore
        for label, blob in (("truncated", first_image.read_bytes()[:24]),
                            ("corrupt", first_image.read_bytes()[:-1] +
                             bytes([first_image.read_bytes()[-1] ^ 1]))):
            bad = self.scratch / (label + ".zst"); bad.write_bytes(blob)
            self.run(label, first_restore[:-1] + [str(bad)], False)
            self.check(label)
        wrong_ram = ["1G" if x == "512M" else x for x in first_restore]
        self.run("wrong-ram", wrong_ram, False); self.check("wrong-ram")
        raw = self.scratch / "mutate.bin"
        self.tool("mutate-unpack", [self.fixtures / "kmh-zstd-unpack", first_image, raw])
        for label, offset in (("bad-entry", 0), ("bad-firmware", 0x300000),
                              ("bad-cpu-privilege", 0x1c000000 + 0xecdc0)):
            with raw.open("r+b") as stream:
                stream.seek(offset); old = stream.read(1)
                stream.seek(offset); stream.write(b"\x03" if label == "bad-cpu-privilege" else bytes([old[0] ^ 1]))
            bad = self.scratch / (label + ".zst")
            self.tool(label + "-compress", ["zstd", "-q", "-f", str(raw), "-o", str(bad)])
            with raw.open("r+b") as stream:
                stream.seek(offset); stream.write(old)
            self.run(label, first_restore[:-1] + [str(bad)], False); self.check(label)
        conflict = first_restore.copy()
        conflict[conflict.index("-M") + 1] += ",checkpoint-stub=/not-needed.bin"
        self.run("external-stub-conflict", conflict, False); self.check("external-stub-conflict")
        other = [x.replace("die-mask=0x5", "die-mask=0x1") for x in restore]
        self.run("bad-topology", other, False); self.check("bad-topology")
        unsupported = self.command(self.native_machine + f",checkpoint-path={self.scratch / 'm.zst'}",
                                  extra=["-bios", "none", "-kernel", self.fixtures / "unsupported-m.elf"])
        validate_log(self.run("unsupported-m", unsupported, False), ("requires U/S-mode",))
        self.check("unsupported-m")
        first, second = self.scratch / "reslice-first.zst", self.scratch / "reslice-second.zst"
        self.run("reslice-create", self.command(self.native_machine + f",checkpoint-path={first}",
                 extra=["-bios", "none", "-kernel", self.fixtures / "reslice.elf"]))
        data = self.run("reslice-again", self.command(self.native_machine + f",checkpoint-path={second}",
                        extra=["-bios", first]))
        validate_log(data, ("portable checkpoint committed",), ("NATIVE_HARTS_PASS",))
        validate_log(self.run("reslice-finish", self.command(self.native_machine,
                     extra=["-bios", second])), ("NATIVE_HARTS_PASS",))
        self.check("reslice")
        first, second = self.scratch / "interval-first.zst", self.scratch / "interval-second.zst"
        self.run("interval-create", self.command(self.native_machine + f",checkpoint-path={first},checkpoint-interval=1000000",
                 extra=["-bios", "none", "-kernel", self.fixtures / "interval.elf"]))
        self.run("interval-short", self.command(self.native_machine + f",checkpoint-path={second},checkpoint-interval=10",
                 extra=["-bios", first]))
        validate_log(self.run("interval-finish", self.command(self.native_machine,
                     extra=["-bios", second])), ("NATIVE_HARTS_PASS",))
        self.check("interval")
        self.qmp(False); self.check("qmp")

    def qmp(self, legacy):
        machine = self.native_machine + (",checkpoint-format=v2" if legacy else "")
        image = self.scratch / "qmp.zst"
        with tempfile.TemporaryDirectory(prefix="bosc-cpt-") as tmp:
            sock_path = Path(tmp) / "qmp.sock"
            args = self.command(machine + f",checkpoint-path={image}", extra=[
                "-bios", "none", "-kernel", self.fixtures / ("legacy-qmp.elf" if legacy else "qmp.elf"),
                "-qmp", f"unix:{sock_path},server=on,wait=off"])
            with self.running("qmp-create", args) as (proc, text):
                marker = "STAGE1_M_READY" if legacy else "CHECKPOINT_QMP_READY"
                while marker not in text() or not sock_path.exists():
                    if proc.poll() is not None or self.remaining() <= 0:
                        raise AssertionError("QMP guest did not reach save boundary")
                    time.sleep(.05)
                with socket.socket(socket.AF_UNIX) as sock:
                    sock.settimeout(min(30, self.remaining())); sock.connect(str(sock_path))
                    stream = sock.makefile("rwb", buffering=0)
                    json.loads(stream.readline())
                    for command in ({"execute": "qmp_capabilities"},
                                    {"execute": "x-xiangshan-checkpoint-save", "arguments": {"path": str(image)}}):
                        stream.write(json.dumps(command).encode() + b"\n")
                        while True:
                            response = json.loads(stream.readline())
                            if "error" in response:
                                raise AssertionError(response["error"])
                            if "return" in response: break
                if proc.wait(timeout=min(60, self.remaining())) != 0:
                    raise AssertionError("QMP save failed")
        extra = ["-bios", "none"] if legacy else ["-bios", image]
        if legacy:
            machine += f",checkpoint-load={image},checkpoint-stub={self.fixtures / 'legacy-stub.bin'}"
        validate_log(self.run("qmp-restore", self.command(machine, extra=extra)),
                     ("STAGE3_PASS" if legacy else "NATIVE_HARTS_PASS",))

    def legacy(self):
        for mode, mask in (("single", "0x1"), ("multi", "0xf"), ("multi", "0x5")):
            name = mode + "-" + mask
            machine = (f"kmh-bosc-soc,checkpoint-format=v2,soc-mode={mode},die-mask={mask},"
                       "core-mask=0x1:0:0:0,generated-dtb=on,dw-pcie=off,iommu-sys=off")
            first, second = self.scratch / (name + "-m.zst"), self.scratch / (name + "-s.zst")
            elf = self.fixtures / ("legacy-single.elf" if mode == "single" else "legacy-multi.elf")
            validate_log(self.run(name + "-create", self.command(machine + f",checkpoint-path={first}",
                         mode, extra=["-bios", "none", "-kernel", elf])),
                         ("STAGE1_M_READY",), ("STAGE2_S_READY",))
            restore = machine + f",checkpoint-stub={self.fixtures / 'legacy-stub.bin'}"
            validate_log(self.run(name + "-restore-m", self.command(restore +
                         f",checkpoint-load={first},checkpoint-path={second}", mode, extra=["-bios", "none"])),
                         ("STAGE2_S_READY",), ("STAGE1_M_READY",))
            validate_log(self.run(name + "-restore-s", self.command(restore + f",checkpoint-load={second}",
                         mode, extra=["-bios", "none"])), ("STAGE3_PASS",), ("STAGE2_S_READY",))
            validate_log(self.run(name + "-continue", self.command(machine +
                         f",checkpoint-path={self.scratch / (name + '-continue.zst')},checkpoint-exit=off",
                         mode, extra=["-bios", "none", "-kernel", elf])),
                         ("STAGE1_M_READY", "STAGE2_S_READY", "STAGE3_PASS"))
            self.check(name)
        machine = self.native_machine + ",checkpoint-format=v2"
        restore = machine + f",checkpoint-stub={self.fixtures / 'legacy-stub.bin'}"
        first, second = self.scratch / "interval.zst", self.scratch / "short.zst"
        self.run("interval-create", self.command(machine + f",checkpoint-path={first},checkpoint-interval=100000",
                 extra=["-bios", "none", "-kernel", self.fixtures / "legacy-qmp.elf"]))
        self.run("interval-short", self.command(restore + f",checkpoint-load={first},checkpoint-path={second},checkpoint-interval=10",
                 extra=["-bios", "none"]))
        validate_log(self.run("interval-finish", self.command(restore + f",checkpoint-load={second}",
                     extra=["-bios", "none"])), ("STAGE3_PASS",))
        self.check("interval")
        self.qmp(True); self.check("qmp")
        for label, blob in (("truncated", first.read_bytes()[:24]),
                            ("corrupt", first.read_bytes()[:-1] + bytes([first.read_bytes()[-1] ^ 1]))):
            bad = self.scratch / (label + ".zst"); bad.write_bytes(blob)
            self.run(label, self.command(restore + f",checkpoint-load={bad}", extra=["-bios", "none"]), False)
            self.check(label)
        # Equal RAM backing size, different selected physical DDR slots.
        bad_machine = restore.replace("soc-mode=single", "soc-mode=multi,die-mask=0x1")
        self.run("bad-topology", self.command(bad_machine + f",checkpoint-load={first}", "multi",
                 ram="512M", extra=["-bios", "none"]), False)
        self.check("bad-topology")

    def raw(self):
        image, raw = self.scratch / "native.zst", self.scratch / "native.bin"
        self.run("raw-create", self.command(self.native_machine + f",checkpoint-path={image}",
                 extra=["-bios", "none", "-kernel", self.fixtures / "single-s.elf"]))
        self.tool("sparse-unpack", [self.fixtures / "kmh-zstd-unpack", image, raw])
        if raw.stat().st_size != 512 * 1024**2 or raw.stat().st_blocks * 512 >= raw.stat().st_size:
            raise AssertionError("unpacker did not preserve full logical size and sparse allocation")
        self.check("sparse-unpack")
        validate_log(self.run("raw-restore", self.command(self.native_machine, extra=["-bios", raw])),
                     ("NATIVE_HARTS_PASS", "no host CSR/device fixups"))
        self.check("raw-restore")
        payload = image.read_bytes()
        for label, blob in (("unpack-truncated", payload[:24]),
                            ("unpack-corrupt", payload[:-1] + bytes([payload[-1] ^ 1])),
                            ("unpack-trailing", payload + b"trailing")):
            bad, output = self.scratch / (label + ".zst"), self.scratch / (label + ".bin")
            bad.write_bytes(blob)
            self.tool(label, [self.fixtures / "kmh-zstd-unpack", bad, output], False)
            if output.exists(): raise AssertionError("failed unpack left partial output")
            self.check(label)

    def linux_v2(self):
        fs = self.scratch / "workload-root"; fs.mkdir()
        (fs / "package").mkdir()
        import shutil
        shutil.copy2(self.fixtures / "linux-stages", fs / "stages")
        (fs / "run.sh").write_text("#!/bin/bash\nset -eu\n/opt/stages\necho LINUX_WORKLOAD_DONE\n")
        (fs / "run.sh").chmod(0o755)
        workload = self.scratch / "workload.ext4"
        self.tool("workload", ["mke2fs", "-q", "-t", "ext4", "-d", fs, workload, "64M"])
        trigger = self.scratch / "trigger.info"
        trigger.write_text('[cmd]\nstart_addr: 0x90000000\nbootargs: "console=ttyS0,115200 earlycon loglevel=7 root=/dev/pmem0 rw"\n[cmd_end]\n[task]\n[task_end]\n')
        mode = self.case["mode"]
        machine = (f"kmh-bosc-soc,checkpoint-format=v2,soc-mode={mode},core-mask=0x1:0:0:0,"
                   "dw-pcie=off,iommu-sys=off")
        for stage in range(3):
            props = machine
            if stage < 2: props += f",checkpoint-path={self.scratch / f'stage{stage+1}.zst'}"
            if stage:
                props += f",checkpoint-load={self.scratch / f'stage{stage}.zst'},checkpoint-stub={self.fixtures / 'legacy-stub.bin'}"
                extra = ["-bios", "none"]
            else:
                props += ",generated-dtb=on,autotest-dtb=on,autotest-rootfs-addr=0xa0000000,autotest-workload-addr=0xc0000000"
                extra = ["-bios", self.paths["fw_jump"]]
                for path, addr in ((self.paths["kernel"], "0x80400000"), (self.paths["rootfs"], "0xa0000000"),
                                   (workload, "0xc0000000"), (trigger, "0x90000000")):
                    extra += ["-device", f"loader,file={path},addr={addr},force-raw=on"]
            command = self.command(props, mode, "4G" if mode == "single" else "16G", extra)
            with self.running(f"stage{stage+1}", command) as (proc, text):
                sent = False
                while proc.poll() is None:
                    data = text(); self.remaining()
                    if stage == 2 and "LINUX_WORKLOAD_DONE" in data and not sent:
                        proc.stdin.write(b"\nprintf 'LINUX_UART_%s\\n' PASS\npoweroff -f\n"); proc.stdin.flush(); sent = True
                    if stage == 2 and "reboot: Power down" in data:
                        break
                    time.sleep(.1)
                data = text()
                if stage < 2 and proc.wait(timeout=min(30, self.remaining())) != 0:
                    raise AssertionError("Linux checkpoint creation failed")
            required = [f"LINUX_STAGE{stage+1}_"]
            if stage < 2: required += ["portable checkpoint committed"]
            else: required += ["LINUX_STAGE3_PASS", "LINUX_UART_PASS", "reboot: Power down"]
            validate_log(data, required, ("OpenSBI", "Linux version") if stage else ())
            self.check(f"stage{stage+1}")
        if "LINUX_UART_PASS" not in clean(data).splitlines():
            raise AssertionError("missing actual UART shell response")
        self.check("shell")

    def linux_litmus(self):
        rootfs = self.scratch / "rootfs.ext4"
        self.tool("prepare-rootfs", [HERE / "checkpoint/prepare-rootfs.sh", self.paths["rootfs"],
                                    rootfs, self.fixtures / "litmus-pid1"])
        machine = ("kmh-bosc-soc,soc-mode=multi,die-mask=0x3,core-mask=0xffff:0xffff:0:0,"
                   "boot-source=ddr,dw-pcie=off,iommu-sys=off,generated-dtb=off,autotest-dtb=off")
        image = self.scratch / "switchroot-before-bash.zst"
        extra = ["-bios", self.paths["checkpoint_fw_jump"], "-dtb", self.paths["checkpoint_dtb"]]
        for path, addr in ((self.paths["checkpoint_kernel"], "0x80400000"), (rootfs, "0x3c0000000"),
                           (self.paths["checkpoint_workload"], "0x3e0000000"),
                           (self.paths["checkpoint_trigger"], "0x90000000")):
            extra += ["-device", f"loader,file={path},addr={addr},force-raw=on"]
        # Linux creation can exceed the short baremetal stage budget.
        with self.running("switchroot", self.command(machine + f",checkpoint-path={image}", "multi", "64G", extra)) as (proc, text):
            if proc.wait(timeout=min(600, self.remaining())) != 0:
                raise AssertionError("switch_root checkpoint failed")
            first = text()
        validate_log(first, ("Switching root to overlay", "portable checkpoint committed",
            "nomap non-reusable kmh-native@80300000", "nomap non-reusable kmh-checkpoint@92000000",
            "nomap non-reusable kmh-checkpoint-cpu@c7c000000"), ("CHECKPOINT_ROOTFS_RESUMED", "LITMUS_32CORE_BEGIN"))
        self.check("switchroot")
        if self.case["restore_format"] == "raw":
            raw = self.scratch / "switchroot-before-bash.bin"
            self.tool("unpack", [self.fixtures / "kmh-zstd-unpack", image, raw])
            image = raw
        observation = None
        seconds = None
        with self.running("native-restore", self.command(machine, "multi", "64G", ["-bios", image])) as (proc, text):
            while proc.poll() is None:
                data = text(); self.remaining()
                validate_log(data)
                if observation is None and any(line.startswith("Test ") for line in data.splitlines()):
                    observation = time.monotonic()
                if observation is not None and time.monotonic() - observation >= self.case["observe_seconds"]:
                    seconds = time.monotonic() - observation
                    break
                time.sleep(.1)
            if seconds is None:
                raise AssertionError("guest stopped before complete observation window")
            data = text()
        self.data.update(litmus_assertions(data, seconds))
        self.data.update(restore_format=self.case["restore_format"], reserved_regions_verified=True)
        self.check("native-restore"); self.check("litmus-window")
        write_json(self.directory / "checkpoint.json", {"path": str(image), "size": image.stat().st_size,
                   "sha256": sha256(image) if self.case["restore_format"] == "zstd" else None})


def execute(work, case, paths, directory):
    run = CheckpointRun(work, case, paths, directory)
    {"native": run.native, "legacy": run.legacy, "raw": run.raw,
     "linux-v2": run.linux_v2, "linux-litmus": run.linux_litmus}[case["scenario"]]()
    run.data.update(check_count=len(run.checks), commands=len(run.commands))
    evidence_assertions(case, run.data, run.checks)
    return run.data, run.checks
