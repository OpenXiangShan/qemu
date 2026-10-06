"""Bounded real QEMU execution using the in-tree QMP harness. GPL-2.0-or-later."""
import os
import contextlib
import re
import shlex
import signal
import socket
import subprocess
import sys
import tempfile
import time
import uuid

from common import SOURCE, pcie_segment, run_logged, sha256, write_json
from checks import BOOTARGS, FATAL, clean, iommu_assertions, itgen_assertions, linux_assertions, mcu_assertions, qtest_assertions

sys.path.insert(0, str(SOURCE / "python"))
from qemu.machine import QEMUMachine  # noqa: E402


def machine(case, boot="ddr", extra=None):
    if case["owner"] == "kmh":
        options = ["xiangshan-kunminghu"]
        smp = str(case["cpus"])
    else:
        options = ["kmh-bosc-soc"]
        if case["mode"] != "default":
            options.append("soc-mode=" + case["mode"])
        options += ["die-mask=" + case["die_mask"], "core-mask=" + case.get("core_mask", "0xffff"), "boot-source=" + boot]
        smp = "16,maxcpus=17" if case["mode"] == "single" else "64,maxcpus=68"
    if case["kind"] == "iommu-linux":
        options += ["dw-pcie=on", "iommu-sys=on"]
    return ["-M", ",".join(options + (extra or [])), "-smp", smp, "-m", "16G", "-accel", "tcg"]


def dt_options():
    return ["generated-dtb=on", "autotest-dtb=on", "autotest-rootfs-addr=0xa0000000",
            "autotest-workload-addr=0xc0000000"]


def bootargs(case):
    return BOOTARGS + (" iommu.passthrough=0" if case["kind"] == "iommu-linux" else "")


def prepare_dtb(binary, case, paths, directory, launcher=None):
    dtb = directory / "machine.dtb"
    argv = [*(launcher or [binary]), "-L", binary.parent / "share/qemu", *machine(case, extra=dt_options() + [f"dumpdtb={dtb}"]),
            "-display", "none", "-monitor", "none", "-serial", "null",
            "-bios", paths["fw_jump"]]
    run_logged(argv, directory / "dumpdtb.log", timeout=30)
    run_logged(["fdtput", "-t", "s", dtb, "/chosen", "bootargs", bootargs(case)],
               directory / "fdtput.log", timeout=30)
    run_logged(["dtc", "-I", "dtb", "-O", "dts", dtb], directory / "machine.dts", timeout=30)
    def prop(node, name, kind="s"):
        return subprocess.check_output(["fdtget", "-t", kind, str(dtb), node, name],
                                       stderr=subprocess.PIPE, timeout=10).decode().strip()
    model = prop("/", "model")
    expected = ("Xiangshan Kunminghu QEMU machine" if case["owner"] == "kmh" else
                "BOSC KMH single-die SoC" if case["mode"] == "single" else
                "BOSC KMH multi-die SoC")
    if model != expected:
        raise AssertionError(f"DT model {model!r}, expected {expected!r}")
    evidence = {"model": model, "sha256": sha256(dtb)}
    if case["kind"] == "iommu-linux":
        nodes = subprocess.check_output(["fdtget", "-l", str(dtb), "/soc"], timeout=10).decode().split()
        iommus = {}
        for node in nodes:
            if node.startswith("iommu@") and prop("/soc/" + node, "compatible") == "riscv,iommu":
                phandle = int(prop("/soc/" + node, "phandle", "x"), 16)
                iommus[phandle] = "riscv-iommu@" + node.split("@")[1] + ".iommu"
        roots = []
        for node in nodes:
            if not node.startswith(("pcie@", "pci@")):
                continue
            node = "/soc/" + node
            mapping = [int(x, 16) for x in prop(node, "iommu-map", "x").split()]
            segment = int(prop(node, "linux,pci-domain", "i"))
            base = segment << 16 if case["owner"] == "bosc" else 0
            if (len(mapping) != 4 or mapping[0] != 0 or mapping[2:] != [base, 0x10000]
                    or mapping[1] not in iommus):
                raise AssertionError(f"incorrect DT IOMMU mapping for {node}: {mapping}")
            roots.append({"node": node, "segment": segment, "iommu_map": mapping,
                          "iommu": iommus[mapping[1]]})
        count = 2 if case["owner"] == "kmh" else 3 if case["mode"] == "single" else 12
        if sorted(r["segment"] for r in roots) != list(range(count)):
            raise AssertionError("missing or duplicate PCIe roots in DT")
        targets = {r["iommu_map"][1] for r in roots}
        if len(iommus) != (2 if case["owner"] == "kmh" else 1) or targets != set(iommus):
            raise AssertionError("incorrect per-root IOMMU attachment in DT")
        evidence["pcie_roots"] = roots
    if case["owner"] == "bosc":
        single = case["mode"] == "single"
        imsic = [int(x, 16) for x in prop("/soc/imsics-s", "reg", "x").split()]
        bits = prop("/soc/imsics-s", "riscv,group-index-bits", "i")
        if imsic[:2] != ([0, 0x1d000000] if single else [0x1000, 0x1d000000]):
            raise AssertionError("DT IMSIC global/local address mismatch")
        if bits != ("0" if single else "3"):
            raise AssertionError("DT IMSIC group-index-bits mismatch")
        stdout = prop("/chosen", "stdout-path")
        if ("4000000" if single else "100004000000") not in stdout:
            raise AssertionError("DT UART does not match SoC mode")
        evidence.update({"imsic_reg": imsic, "group_index_bits": bits, "stdout": stdout})
    # Omitted-mode compatibility compares DT bytes, generated by the SAME
    # binary/config, with explicit multi; this is not an old frozen DT fixture.
    if case.get("mode") == "default":
        explicit = dict(case, mode="multi")
        compare = directory / "explicit-multi"
        compare.mkdir()
        other, _ = prepare_dtb(binary, explicit, paths, compare)
        if dtb.read_bytes() != other.read_bytes():
            raise AssertionError("default and explicit multi DTBs differ")
        evidence["default_equals_explicit_multi"] = True
    return dtb, evidence


def shell_probe(token):
    # The values are computed by the guest. Splitting marker/label arguments
    # keeps the full anchored response out of the command's echoed text.
    return (f"stty -echo; t={shlex.quote(token)}; "
            "printf '\\n%s:BEGIN\\n%s:CPU\\n' \"$t\" \"$t\"; "
            "cat /sys/devices/system/cpu/online; r=$?; "
            "printf '%s:KERNEL\\n' \"$t\"; uname -r || r=$?; "
            "printf '%s:MOUNTS\\n' \"$t\"; cat /proc/mounts || r=$?; "
            "printf '%s:RC=%s\\n%s:END\\n' \"$t\" \"$r\" \"$t\"\n")


def iommu_probe(token):
    # Run in a subshell so set -e reports a nonzero RC without killing the
    # interactive console. Every line fits the tty canonical input buffer.
    return "t=" + shlex.quote(token) + "\n" + r'''(
set -eu
printf '\n%s:IOMMU_BEGIN\n' "$t"
irq_count() {
    awk -v c="$1" '$NF ~ ("^" c "q[1-9][0-9]*$") {for(i=2;i<=NF && $i ~ /^[0-9]+$/;i++) n+=$i} END {print n+0}' /proc/interrupts
}
n=0
for p in /sys/class/nvme/nvme*; do
    [ -e "$p" ] || continue
    c=${p##*/}
    serial=$(tr -d ' ' < "$p/serial")
    state=$(cat "$p/state")
    bdf=$(basename "$(readlink -f "$p/device")")
    group=$(basename "$(readlink -f "$p/device/iommu_group")")
    domain=$(cat "$p/device/iommu_group/type")
    driver=$(basename "$(readlink -f "$p/device/driver")")
    [ -L "$p/device/iommu" ]
    iommu=$(basename "$(readlink -f "$p/device/iommu")")
    msis=$(cat "$p/device/msi_irqs/"*)
    printf '%s:DEVICE=%s|%s|%s|%s|%s|%s|%s|%s\n' "$t" "$serial" "$c" "$bdf" "$group" "$domain" "$state" "$driver" "$iommu"
    [ "$state" = live ] && [ "$driver" = nvme ]
    case "$domain" in DMA|DMA-FQ) ;; *) exit 1;; esac
    case "$msis" in *msi*) ;; *) exit 1;; esac
    dev=/dev/${c}n1
    [ -b "$dev" ]
    src=/tmp/bosc-ci-source
    dst=/tmp/bosc-ci-readback
    dd if=/dev/urandom of="$src" bs=4096 count=256
    before=$(irq_count "$c")
    printf '%s:IRQ_BEFORE=%s\n' "$t" "$c"
    cat /proc/interrupts
    dd if="$src" of="$dev" bs=4096 count=256 seek=256 oflag=direct conv=fsync
    dd if="$dev" of="$dst" bs=4096 count=256 skip=256 iflag=direct
    after=$(irq_count "$c")
    printf '%s:IRQ_AFTER=%s\n' "$t" "$c"
    cat /proc/interrupts
    a=$(sha256sum "$src"); a=${a%% *}
    b=$(sha256sum "$dst"); b=${b%% *}
    printf '%s:DMA=%s|%s|%s|%s|%s|1048576|direct|msi\n' "$t" "$serial" "$a" "$b" "$before" "$after"
    [ "$a" = "$b" ] && [ "$after" -gt "$before" ]
    rm -f "$src" "$dst"
    n=$((n+1))
done
printf '%s:NVME_COUNT=%s\n' "$t" "$n"
)
r=$?
printf '%s:IOMMU_RC=%s\n%s:IOMMU_END\n' "$t" "$r" "$t"
'''


def cleanup_nvme(work, case, directory):
    """Also called by the parent after a worker timeout/kill."""
    from common import read_json
    manifest = directory / "nvme-disks.json"
    if not manifest.exists():
        return
    data = read_json(manifest)
    scratch = work / "scratch" / case["id"]
    for index in range(case["disks"]):
        (scratch / f"nvme{index}.raw").unlink(missing_ok=True)
    if scratch.exists():
        scratch.rmdir()
    data["cleaned"] = True
    write_json(manifest, data)


@contextlib.contextmanager
def nvme_disks(work, case, directory):
    if case["kind"] != "iommu-linux":
        yield []
        return
    scratch = work / "scratch" / case["id"]
    scratch.mkdir(parents=True, exist_ok=False)
    disks = [{"path": str(scratch / f"nvme{i}.raw"), "bus": bus,
              "segment": pcie_segment(bus),
              "serial": f"BOSC-CI-{i}", "size": 64 * 1024 * 1024}
             for i, bus in enumerate(case["nvme_buses"])]
    write_json(directory / "nvme-disks.json", {"disks": disks, "cleaned": False})
    try:
        args = []
        for i, disk in enumerate(disks):
            with open(disk["path"], "xb") as stream:
                stream.truncate(disk["size"])
            args += ["-drive", f"file={disk['path']},if=none,id=nvme{i},format=raw,cache=none",
                     "-device", f"nvme,drive=nvme{i},bus={disk['bus']},addr=0,serial={disk['serial']}"]
        yield args
    finally:
        cleanup_nvme(work, case, directory)


def shutdown_vm(vm):
    """QMP quit, then TERM, then the native harness KILL/cleanup fallback."""
    try:
        if vm.is_running():
            try:
                vm.qmp("quit")
            except Exception:
                pass
            deadline = time.monotonic() + 5
            while vm.is_running() and time.monotonic() < deadline:
                time.sleep(0.05)
            if vm.is_running():
                try:
                    os.kill(vm.get_pid(), signal.SIGTERM)
                except ProcessLookupError:
                    pass
                deadline = time.monotonic() + 5
                while vm.is_running() and time.monotonic() < deadline:
                    time.sleep(0.05)
    finally:
        vm.shutdown(hard=vm.is_running(), timeout=1)


def run_vm(binary, args, directory, timeout, kind, case):
    args = ["-L", str(binary.parent / "share/qemu"), *args]
    token = "BOSC_CI_" + uuid.uuid4().hex
    text = ""
    sent = False
    iommu_sent = False
    last_data_at = time.monotonic()
    deadline = time.monotonic() + timeout
    # Never emit a command containing an inherited fd as a reproducible CLI.
    # The raw actual argv is saved separately; replay uses ordinary stdio.
    replay = [str(binary), "-display", "none", "-monitor", "none", "-serial", "stdio", *args]
    (directory / "replay-command.txt").write_text(shlex.join(replay) + "\n")
    with tempfile.TemporaryDirectory(prefix="bosc-vm-") as tmp:
        vm = QEMUMachine(str(binary), args=args, name="qemu", base_temp_dir=tmp,
                         log_dir=str(directory), qmp_timer=5)
        vm.set_console()
        try:
            vm.launch()
            write_json(directory / "process.json", {"pid": vm.get_pid(), "argv": list(vm._qemu_full_args)})
            console = vm.console_socket
            console.settimeout(0.25)
            with (directory / "uart0.log").open("wb") as uart:
                while time.monotonic() < deadline:
                    try:
                        data = console.recv(65536)
                        if not data:
                            raise RuntimeError("guest console closed before verification")
                        uart.write(data)
                        uart.flush()
                        text += data.decode(errors="replace")
                        last_data_at = time.monotonic()
                    except socket.timeout:
                        pass
                    if not vm.is_running():
                        raise RuntimeError(f"QEMU exited early: {vm.exitcode()}")
                    normalized = clean(text)
                    if kind == "itgen":
                        if "1 tests run, 0 failed, 1 passed" in normalized:
                            # A UART read can stop halfway through "TRAP".
                            # Validate the whole locked completion tail only
                            # after the stream has become quiet.
                            if time.monotonic() - last_data_at >= 0.5:
                                break
                    else:
                        if FATAL.search(normalized):
                            raise AssertionError("fatal guest boot output")
                        if not sent and re.search(r"\[root@.*\]#\s*$", normalized):
                            console.sendall(shell_probe(token).encode())
                            sent = True
                        if sent and re.search(rf"(?m)^{token}:END$", normalized):
                            if kind != "iommu-linux":
                                break
                            if not iommu_sent:
                                probe = iommu_probe(token)
                                (directory / "iommu-probe.sh").write_text(probe)
                                console.sendall(probe.encode())
                                iommu_sent = True
                            if re.search(rf"(?m)^{token}:IOMMU_END$", normalized):
                                break
                else:
                    raise TimeoutError(f"{kind} exceeded {timeout}s; last stage preserved in UART log")
            # Quiesce before consuming other UARTs and asserting no late panic.
            vm.qmp("stop")
            drain_deadline = time.monotonic() + 2
            with (directory / "uart0.log").open("ab") as uart:
                while time.monotonic() < drain_deadline:
                    try:
                        data = console.recv(65536)
                    except socket.timeout:
                        break
                    if not data:
                        break
                    uart.write(data)
                    text += data.decode(errors="replace")
        finally:
            try:
                shutdown_vm(vm)
            finally:
                write_json(directory / "cleanup.json", {"exitcode": vm.exitcode(), "running": vm.is_running()})
        if kind == "itgen":
            return itgen_assertions(text), []
        evidence = linux_assertions(text, token, case)
        if kind == "iommu-linux":
            evidence["iommu"] = iommu_assertions(text, token, case,
                (directory / "qemu.log").read_text(errors="replace"))
        checks = []
        if kind == "mcu":
            logs = [text] + [(directory / f"uart{d}.log").read_text(errors="replace")
                             if (directory / f"uart{d}.log").exists() else "" for d in range(1, 4)]
            dies = [0] if case["mode"] == "single" else [d for d in range(4) if int(case["die_mask"], 16) & (1 << d)]
            checks.append(mcu_assertions(logs, dies))
        return evidence, checks


def execute(work, case, paths, directory):
    if case["kind"].startswith("libqemu"):
        from libqemu_runner import execute as execute_libqemu
        return execute_libqemu(work, case, paths, directory)
    binary = work / "bundle" / "qemu-system-riscv64"
    if case["kind"] == "qtest":
        env = os.environ.copy()
        env["QTEST_QEMU_BINARY"] = str(binary)
        with tempfile.TemporaryDirectory(prefix="bosc-qt-") as tmp:
            env["TMPDIR"] = tmp
            run_logged([work / "bundle" / "tests/qtest" / case["binary"], "--tap"],
                       directory / "qtest.log", env=env, timeout=case["timeout"])
        # qtest_add_func adds the architecture prefix, even when the test's
        # registered name itself already starts with /riscv64.
        expected = ["/riscv64" + name for name in case["tests"]]
        checks = qtest_assertions((directory / "qtest.log").read_text(), expected)
        return {"subtests": len(checks)}, checks
    if case["kind"] == "itgen":
        args = ["-M", "kmh-bosc-soc,soc-mode=multi,die-mask=0x3,core-mask=0x1:0x1:0:0,boot-source=ddr",
                "-smp", "64,maxcpus=68", "-m", "256M", "-accel", "tcg", "-bios", paths["itgen"]]
        return run_vm(binary, args, directory, case["timeout"], "itgen", case)
    dtb, dt_evidence = prepare_dtb(binary, case, paths, directory)
    write_json(directory / "dt-evidence.json", dt_evidence)
    trigger = directory / "boot-rootfs.trigger"
    trigger.write_text(f'[cmd]\nbootargs: "{bootargs(case)}"\n[cmd_end]\n[task]\n[task_end]\n')
    if case["kind"] == "mcu":
        args = machine(case, "mcu", ["generated-dtb=off", "mcu-bios=" + paths["mcu"]])
        args += ["-bios", paths["qspi"], "-dtb", str(dtb)]
    else:
        args = machine(case, extra=dt_options())
        kernel = "iommu_kernel" if case["kind"] == "iommu-linux" else "kernel"
        args += ["-bios", paths["fw_jump"], "-device", "loader,file=" + paths[kernel] + ",addr=0x80400000"]
    args += ["-device", "loader,file=" + paths["rootfs"] + ",addr=0xa0000000",
             "-device", f"loader,file={trigger},addr=0x90000000"]
    if case["owner"] == "bosc" and case["mode"] != "single":
        for die in range(1, 4):
            args += ["-serial", f"file:{directory / f'uart{die}.log'}"]
    with nvme_disks(work, case, directory) as disk_args:
        evidence, checks = run_vm(binary, args + disk_args, directory, case["timeout"], case["kind"], case)
    if case["kind"] == "iommu-linux":
        roots = {r["segment"]: r for r in dt_evidence["pcie_roots"]}
        for endpoint in evidence["iommu"]:
            if endpoint["iommu"] != roots[endpoint["segment"]]["iommu"]:
                raise AssertionError(f"NVMe IOMMU differs from DT: {endpoint['bdf']}")
    evidence["dt"] = dt_evidence
    return evidence, checks
