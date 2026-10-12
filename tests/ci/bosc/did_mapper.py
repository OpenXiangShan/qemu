"""Linux debugfs mapper and real NVMe DMA acceptance. GPL-2.0-or-later."""
import re
import shlex
import socket
import tempfile
import time
import uuid

from common import bosc_mapper_address, pcie_segment, sha256, write_json
from checks import FATAL, IOMMU_FATAL, clean, iommu_assertions, linux_assertions

PHASES = ("same-did", "disabled", "empty-miss", "range-miss", "slot7", "priority")


def evidence_assertions(case, evidence):
    """Fail closed if a worker omits any selected mapper acceptance phase."""
    phases = set(PHASES) | ({"recovery"} if case["mapper_test"] == "fault" else set())
    if evidence.get("first_bind") is not True or set(evidence.get("phases", {})) != phases:
        raise ValueError("mapper first binding or DMA phases missing")
    for rows in evidence["phases"].values():
        if len(rows) != case["disks"] or {r.get("serial") for r in rows} != {f"BOSC-CI-{i}" for i in range(case["disks"])}:
            raise ValueError("mapper per-phase endpoint evidence missing")
    expected_configs = set(PHASES) | {"final-disable"} | ({"fault"} if case["mapper_test"] == "fault" else set())
    if set(evidence.get("mapper", {})) != expected_configs:
        raise ValueError("mapper register phase missing")
    for phase, rows in evidence["mapper"].items():
        expected = {f"{a:x}": [en, list(s0), list(s7)] for a, (en, s0, s7) in states(case, "disabled" if phase == "final-disable" else phase).items()}
        if rows != expected:
            raise ValueError("mapper register/isolation evidence mismatch")
    if case["mapper_test"] == "fault":
        target = bosc_mapper_address(pcie_segment(case["nvme_buses"][-1]), case["mode"]) - 0x1000
        if (type(evidence.get("negative_io_rc")) is not int
                or evidence["negative_io_rc"] <= 0
                or evidence.get("nvme_timeouts") != {"io": 3, "admin": 3}
                or not re.fullmatch(r"[a-f0-9]{64}", evidence.get("negative_kernel_log_sha256", ""))
                or not evidence.get("expected_faults")
                or any(not re.search(rf"riscv,iommu {target:x}\.iommu: Fault 258 devid: 0x200\b", f) for f in evidence["expected_faults"])
                or len(evidence.get("observers", [])) != case["disks"] - 1):
            raise ValueError("mapper explicit fault, completed I/O or observers missing")


def addresses(case):
    dies = [0] if case["mode"] == "single" else [d for d in range(4) if int(case["die_mask"], 16) & (1 << d)]
    return [bosc_mapper_address(d * 3 + p, case["mode"]) for d in dies for p in range(3)]


def states(case, phase):
    active = {bosc_mapper_address(pcie_segment(b), case["mode"]) for b in case["nvme_buses"]}
    result = {}
    empty = (0xffffffffffffffff, 0xffffffffffffffff, 0)
    for addr in addresses(case):
        enable, slot0, slot7 = 0, empty, empty
        if addr in active:
            enable = int(phase != "disabled")
            if phase in ("same-did", "priority", "fault"):
                slot0 = (0, 1 << 44, 0x100)
            elif phase == "disabled":
                slot0 = (0, 1 << 44, 0x200)
            elif phase == "range-miss":
                slot0 = (1 << 44, (1 << 44) + 0x1000, 0x200)
            if phase == "slot7":
                slot7 = (0, 1 << 44, 0x100)
            elif phase == "priority":
                slot7 = (0, 1 << 44, 0x200)
        result[addr] = (enable, slot0, slot7)
    if phase == "fault":
        target = bosc_mapper_address(pcie_segment(case["nvme_buses"][-1]), case["mode"])
        result[target] = (1, (0, 1 << 44, 0x200), empty)
    return result


def config_script(case, phase):
    lines = ["set -eu", "mountpoint -q /sys/kernel/debug || mount -t debugfs debugfs /sys/kernel/debug"]
    lines += [f"[ $(find /sys/kernel/debug -maxdepth 1 -name 'bosc-did-mapper@*' | wc -l) -eq {len(addresses(case))} ]"]
    for addr, (enable, slot0, slot7) in states(case, phase).items():
        lines += [f"m=/sys/kernel/debug/bosc-did-mapper@{addr:x}", "[ -d \"$m\" ]",
                  'echo 0 > "$m/enable"', 'echo 1 > "$m/reset_slots"']
        for slot, values in ((0, slot0), (7, slot7)):
            for field, value in zip(("start", "end", "did"), values):
                lines.append(f'echo 0x{value:x} > "$m/slot{slot}/{field}"')
            lines += [f'[ $(( $(cat "$m/slot{slot}/{f}") )) -eq 0 ]' for f in ("pid", "pv", "translated")]
        lines += [f'echo {enable} > "$m/enable"',
                  "printf '%s:MAP=%s|%s|%s|%s|%s|%s|%s|%s\\n' \"$t\" "
                  f"{addr:x} \"$(cat \"$m/enable\")\" "
                  '"$(cat "$m/slot0/start")" "$(cat "$m/slot0/end")" "$(cat "$m/slot0/did")" '
                  '"$(cat "$m/slot7/start")" "$(cat "$m/slot7/end")" "$(cat "$m/slot7/did")"']
    return "\n".join(lines)


def config_assertions(text, token, case, phase):
    rows = re.findall(rf"(?m)^{re.escape(token)}:MAP=([^\n]+)$", clean(text))
    actual = {}
    for row in rows:
        fields = row.split("|")
        if len(fields) != 8:
            raise AssertionError("malformed mapper register readback")
        addr = int(fields[0], 16)
        if addr in actual:
            raise AssertionError("duplicate mapper register readback")
        vals = [int(x, 0) for x in fields[1:]]
        actual[addr] = (vals[0], tuple(vals[1:4]), tuple(vals[4:]))
    if actual != states(case, phase):
        raise AssertionError(f"incorrect {phase} mapper registers or instance isolation")
    return {f"{a:x}": v for a, v in actual.items()}


def negative_assertions(text, token, case, controller):
    """Allow only this endpoint's deliberate invalid DID inside its phase."""
    normalized = clean(text)
    pattern = rf"(?m)^{re.escape(token)}:NEGATIVE_BEGIN$\n(.*?)^{re.escape(token)}:NEGATIVE_END$"
    match = re.search(pattern, normalized, re.S)
    if not match or FATAL.search(normalized):
        raise AssertionError("missing bounded negative phase or fatal output")
    target = bosc_mapper_address(pcie_segment(case["nvme_buses"][-1]), case["mode"]) - 0x1000
    expected = re.compile(rf"riscv,iommu {target:x}\.iommu: Fault 258 devid: 0x200\b")
    faults, retained = [], []
    for line in match[1].splitlines():
        if re.search(r"\bFault\b", line, re.I) and "iommu" in line.lower():
            if not expected.search(line):
                raise AssertionError("unexpected cause, DID or IOMMU in negative phase")
            faults.append(line)
        elif IOMMU_FATAL.search(line):
            if not (re.search(rf"\bnvme {re.escape(controller)}:", line)
                    or re.search(rf"\b{re.escape(controller)}n1:", line)
                    or re.search(rf"\bdev {re.escape(controller)}n1\b", line)):
                raise AssertionError("unrelated I/O failure during mapper negative phase")
        else:
            retained.append(line)
    if not faults:
        raise AssertionError("wrong DID did not produce an explicit Linux IOMMU fault")
    # Keep observer evidence and reject unrelated errors. The complete original
    # UART remains an artifact; only validated target errors are waived here.
    return normalized[:match.start()] + "\n".join(retained) + "\n" + normalized[match.end():], faults


class Guest:
    def __init__(self, vm, directory, timeout):
        self.vm = vm
        self.console = vm.console_socket
        self.console.settimeout(0.25)
        self.directory = directory
        self.deadline = time.monotonic() + timeout
        self.text = ""
        self.uart = (directory / "uart0.log").open("wb")
        self.sequence = 0

    def wait(self, predicate, timeout=120):
        until = min(self.deadline, time.monotonic() + timeout)
        while time.monotonic() < until:
            if FATAL.search(clean(self.text)):
                raise AssertionError("fatal Linux output")
            if predicate(clean(self.text)):
                return
            if not self.vm.is_running():
                raise RuntimeError("QEMU exited before mapper verification")
            try:
                data = self.console.recv(65536)
            except socket.timeout:
                continue
            if not data:
                raise RuntimeError("guest console closed")
            self.uart.write(data)
            self.uart.flush()
            self.text += data.decode(errors="replace")
        raise TimeoutError("mapper guest phase did not complete within its deadline")

    def command(self, body, name, timeout=120):
        self.sequence += 1
        token = f"MAPPER_{self.sequence}_{uuid.uuid4().hex}"
        script = (f"t={shlex.quote(token)}\n(\n{body}\n)\nr=$?\n"
                  "printf '\\n%s:COMMAND_RC=%s\\n%s:COMMAND_END\\n' \"$t\" \"$r\" \"$t\"\n")
        (self.directory / f"{self.sequence:02d}-{name}.sh").write_text(script)
        start = len(self.text)
        self.console.sendall(script.encode())
        self.wait(lambda s: bool(re.search(rf"(?m)^{token}:COMMAND_END$", s)), timeout)
        response = clean(self.text[start:])
        if re.findall(rf"(?m)^{token}:COMMAND_RC=(\d+)$", response) != ["0"]:
            raise AssertionError(f"guest command failed: {name}")
        return response, token

    def probe(self, name, skip_bdf=None):
        from runner import iommu_probe
        token = "DMA_" + uuid.uuid4().hex
        script = iommu_probe(token)
        if skip_bdf:
            script = script.replace('group=$(basename', f'[ "$bdf" != {skip_bdf} ] || continue\n    group=$(basename', 1)
        (self.directory / f"dma-{name}.sh").write_text(script)
        self.console.sendall(script.encode())
        self.wait(lambda s: bool(re.search(rf"(?m)^{token}:IOMMU_END$", s)))
        return token

    def drain(self):
        until = time.monotonic() + 2
        while time.monotonic() < until:
            try:
                data = self.console.recv(65536)
            except socket.timeout:
                break
            if not data:
                break
            self.uart.write(data)
            self.uart.flush()
            self.text += data.decode(errors="replace")


def execute(work, case, paths, directory):
    from runner import (QEMUMachine, bootargs, dt_options, machine,
                        nvme_disks, prepare_dtb, shell_probe, shutdown_vm)
    binary = work / "bundle/qemu-system-riscv64"
    dtb, dt = prepare_dtb(binary, case, paths, directory)
    write_json(directory / "dt-evidence.json", dt)
    trigger = directory / "boot-rootfs.trigger"
    trigger.write_text(f'[cmd]\nbootargs: "{bootargs(case)}"\n[cmd_end]\n[task]\n[task_end]\n')
    args = ["-L", str(binary.parent / "share/qemu"), *machine(case, extra=dt_options()),
            "-bios", paths["fw_jump"],
            "-device", f"loader,file={paths['did_mapper_kernel']},addr=0x80400000",
            "-device", f"loader,file={paths['rootfs']},addr=0xa0000000",
            "-device", f"loader,file={paths['did_mapper_modules']},addr=0xc0000000",
            "-device", f"loader,file={trigger},addr=0x90000000"]
    if case["mode"] != "single":
        args += [v for die in range(1, 4) for v in ("-serial", f"file:{directory / f'uart{die}.log'}")]
    token = "BOSC_CI_" + uuid.uuid4().hex
    evidence = {"phases": {}, "mapper": {}, "dt": dt}
    with nvme_disks(work, case, directory) as disks, tempfile.TemporaryDirectory(prefix="mapper-vm-") as temp:
        (directory / "replay-command.txt").write_text(shlex.join([str(binary), "-display", "none", "-monitor", "none", "-serial", "stdio", *args, *disks]) + "\n")
        vm = QEMUMachine(str(binary), args=args + disks, name="qemu", base_temp_dir=temp,
                         log_dir=str(directory), qmp_timer=5)
        vm.set_console()
        guest = None
        try:
            vm.launch()
            write_json(directory / "process.json", {"pid": vm.get_pid(), "argv": list(vm._qemu_full_args)})
            guest = Guest(vm, directory, case["timeout"])
            guest.wait(lambda s: bool(re.search(r"\[root@.*\]#\s*$", s)), timeout=300)
            guest.console.sendall(shell_probe(token).encode())
            guest.wait(lambda s: bool(re.search(rf"(?m)^{token}:END$", s)))
            evidence.update(linux_assertions(guest.text, token, case))
            prebind = ['set -eu', '[ ! -e /sys/class/nvme/nvme0 ]', '[ ! -d /sys/bus/pci/drivers/nvme ]',
                       'if [ ! -f /opt/mapper-module-order ]; then mkdir -p /tmp/mapper-modules; mount -o ro /dev/pmem1 /tmp/mapper-modules; fi',
                       'modules=/opt; [ -f "$modules/mapper-module-order" ] || modules=/tmp/mapper-modules',
                       '[ -f "$modules/mapper-module-order" ]']
            for bus in case["nvme_buses"]:
                bdf = f"{pcie_segment(bus):04x}:01:00.0"
                prebind += [f'[ -e /sys/bus/pci/devices/{bdf} ]', f'[ ! -L /sys/bus/pci/devices/{bdf}/driver ]']
            guest.command("\n".join(prebind), "unbound-and-module-payload")
            for phase in PHASES:
                response, marker = guest.command(config_script(case, phase), phase + "-registers")
                evidence["mapper"][phase] = config_assertions(response, marker, case, phase)
                if phase == "same-did":
                    load = 'while IFS= read -r m; do insmod "$modules/$m"; done < "$modules/mapper-module-order"'
                    if case["mapper_test"] == "fault":
                        load = ('while IFS= read -r m; do if [ "$m" = nvme-core.ko ]; then '
                                'insmod "$modules/$m" io_timeout=3 admin_timeout=3; else '
                                'insmod "$modules/$m"; fi; done < "$modules/mapper-module-order"')
                    bind = ['set -eu', 'modules=/opt; [ -f "$modules/mapper-module-order" ] || modules=/tmp/mapper-modules',
                            load,
                            'i=0; while [ $(find /sys/class/nvme -mindepth 1 -maxdepth 1 -name "nvme*" | wc -l) -ne ' + str(case["disks"]) + ' ]; do i=$((i+1)); [ "$i" -lt 60 ]; sleep 1; done']
                    if case["mapper_test"] == "fault":
                        bind += ['printf "%s:NVME_TIMEOUTS=%s|%s\\n" "$t" "$(cat /sys/module/nvme_core/parameters/io_timeout)" "$(cat /sys/module/nvme_core/parameters/admin_timeout)"']
                    response, marker = guest.command("\n".join(bind), "first-nvme-bind")
                    if case["mapper_test"] == "fault":
                        if re.findall(rf"(?m)^{marker}:NVME_TIMEOUTS=(\d+)\|(\d+)$", response) != [("3", "3")]:
                            raise AssertionError("NVMe module timeout parameters did not take effect")
                        evidence["nvme_timeouts"] = {"io": 3, "admin": 3}
                    evidence["first_bind"] = True
                marker = guest.probe(phase)
                evidence["phases"][phase] = iommu_assertions(guest.text, marker, case)
            positive_text = guest.text
            if case["mapper_test"] == "fault":
                target_bdf = f"{pcie_segment(case['nvme_buses'][-1]):04x}:01:00.0"
                # Endpoint enumeration may be reordered; identify by BDF.
                controller = next(e["controller"] for e in evidence["phases"]["priority"] if e["bdf"] == target_bdf)
                # printk and userspace UART writes can interleave inside one
                # fault line under SMP. Keep the complete kernel ring buffer,
                # suppress asynchronous console output only in this phase,
                # and explicitly capture dmesg before restoring the console.
                guest.command('set -eu\n[ "$(cat /sys/module/printk/parameters/ignore_loglevel)" = N ]\n'
                              'cat /proc/sys/kernel/printk > /tmp/mapper-printk\necho 1 > /proc/sys/kernel/printk\n'
                              'set -- $(cat /proc/sys/kernel/printk); [ "$1" -eq 1 ]', "negative-console-level")
                guest.command(f"printf '%s:%s\\n' {token} NEGATIVE_BEGIN", "negative-begin")
                response, marker = guest.command(config_script(case, "fault"), "fault-registers")
                evidence["mapper"]["fault"] = config_assertions(response, marker, case, "fault")
                guest.command('set -eu\nrm -f /tmp/mapper-negative.rc\n'
                              f'(set +e; dd if=/dev/{controller}n1 of=/tmp/mapper-negative-data bs=4096 count=1 iflag=direct; echo $? > /tmp/mapper-negative.rc) > /tmp/mapper-negative.log 2>&1 &\n'
                              'echo $! > /tmp/mapper-negative.pid', "wrong-did-io")
                target_iommu = bosc_mapper_address(pcie_segment(case['nvme_buses'][-1]), case['mode']) - 0x1000
                expected_fault = f"riscv,iommu {target_iommu:x}.iommu: Fault 258 devid: 0x200"
                guest.command('set -eu\ni=0\nwhile ! dmesg | grep -Fq ' + shlex.quote(expected_fault) +
                              '; do i=$((i+1)); [ "$i" -lt 30 ]; sleep 1; done', "wait-kernel-fault", timeout=45)
                observer_marker = None
                if case["disks"] > 1:
                    observer_marker = guest.probe("unaffected-observers", skip_bdf=target_bdf)
                response, marker = guest.command(config_script(case, "same-did"), "restore-registers")
                config_assertions(response, marker, case, "same-did")
                recovery = (f'set -eu\nif [ -L /sys/bus/pci/devices/{target_bdf}/driver ]; then echo {target_bdf} > /sys/bus/pci/drivers/nvme/unbind; fi\n'
                            f'echo {target_bdf} > /sys/bus/pci/drivers/nvme/bind\n'
                            'i=0; while [ ! -f /tmp/mapper-negative.rc ]; do i=$((i+1)); [ "$i" -lt 30 ]; sleep 1; done\n'
                            'printf "%s:NEGATIVE_IO_RC=%s\\n" "$t" "$(cat /tmp/mapper-negative.rc)"\n'
                            'i=0; while grep -qv "^live$" /sys/class/nvme/nvme*/state; do i=$((i+1)); [ "$i" -lt 30 ]; sleep 1; done')
                response, _ = guest.command(recovery, "rebind-and-completed-negative-io", timeout=90)
                evidence["negative_io_rc"] = int(re.search(r"NEGATIVE_IO_RC=(\d+)", response)[1])
                if evidence["negative_io_rc"] == 0:
                    raise AssertionError("deliberate wrong-DID I/O unexpectedly succeeded")
                response, marker = guest.command('printf "%s:KERNEL_LOG_BEGIN\\n" "$t"\ndmesg | cat\nprintf "\\n%s:KERNEL_LOG_END\\n" "$t"', "negative-kernel-log")
                snapshot = re.search(rf"(?m)^{marker}:KERNEL_LOG_BEGIN\n(.*?)^{marker}:KERNEL_LOG_END$", response, re.S)
                if not snapshot:
                    raise AssertionError("missing complete negative kernel log")
                log = directory / "negative-kernel.log"
                log.write_text(snapshot[1])
                evidence["negative_kernel_log_sha256"] = sha256(log)
                guest.command('set -eu\ncat /tmp/mapper-printk > /proc/sys/kernel/printk', "restore-console-level")
                guest.command(f"printf '%s:%s\\n' {token} NEGATIVE_END", "negative-end")
                positive_text, faults = negative_assertions(guest.text, token, case, controller)
                evidence["expected_faults"] = faults
                if observer_marker:
                    # Original serial indices remain 0..N-2; the target is
                    # always the last bus in the manifest, not the last nvme#.
                    observers = dict(case, disks=case["disks"] - 1, nvme_buses=case["nvme_buses"][:-1])
                    evidence["observers"] = iommu_assertions(positive_text, observer_marker, observers)
                marker = guest.probe("recovery")
                positive_text, _ = negative_assertions(guest.text, token, case, controller)
                evidence["phases"]["recovery"] = iommu_assertions(positive_text, marker, case)
            response, marker = guest.command(config_script(case, "disabled"), "final-disable")
            evidence["mapper"]["final-disable"] = config_assertions(response, marker, case, "disabled")
            vm.qmp("stop")
            guest.drain()
            positive_text = guest.text
            if case["mapper_test"] == "fault":
                positive_text, _ = negative_assertions(guest.text, token, case, controller)
            for die in range(1, 4):
                uart = directory / f"uart{die}.log"
                if uart.exists():
                    positive_text += uart.read_text(errors="replace")
            evidence["iommu"] = evidence["phases"].get("recovery", evidence["phases"]["priority"])
            if FATAL.search(positive_text) or IOMMU_FATAL.search(positive_text + (directory / "qemu.log").read_text(errors="replace")):
                raise AssertionError("late fatal or QEMU IOMMU/NVMe error")
        finally:
            try:
                shutdown_vm(vm)
            finally:
                if guest:
                    guest.uart.close()
                write_json(directory / "cleanup.json", {"exitcode": vm.exitcode(), "running": vm.is_running()})
    roots = {r["segment"]: r for r in dt["pcie_roots"]}
    for phase in evidence["phases"].values():
        for endpoint in phase:
            if endpoint["iommu"] != roots[endpoint["segment"]]["iommu"]:
                raise AssertionError("mapper NVMe IOMMU attachment differs from generated DT")
    write_json(directory / "mapper-evidence.json", evidence)
    return evidence, []
