"""Linux on host-assembled SoCs with private test disks. GPL-2.0-or-later."""
import os
import re
import selectors
import shlex
import subprocess
import time
import uuid
from pathlib import Path

from build import bundle_dir
from checks import FATAL, IOMMU_FATAL, clean
from common import read_json, run_logged, sha256, write_json
from compose_checks import BOOTARGS, MODEL, device_mode, fsck_assertions, guest_assertions


def cleanup_disks(work, case, directory):
    manifest = directory / "compose-disks.json"
    if not manifest.exists():
        return
    scratch = work / "scratch" / case["id"]
    for name in case["devices"]:
        (scratch / (name + ".ext4")).unlink(missing_ok=True)
    if scratch.exists():
        scratch.rmdir()
    data = read_json(manifest)
    data["cleaned"] = True
    write_json(manifest, data)


def prepare_disks(work, case, paths, directory):
    scratch = work / "scratch" / case["id"]
    scratch.mkdir(parents=True, exist_ok=False)
    disks = {name: scratch / (name + ".ext4") for name in case["devices"]}
    digest = sha256(paths["rootfs"]) if disks else None
    write_json(directory / "compose-disks.json", {
        "cleaned": False, "disks": {name: str(path) for name, path in disks.items()},
        "source_sha256": digest})
    for name, path in disks.items():
        run_logged(["cp", "--reflink=auto", "--sparse=always", paths["rootfs"], path],
                   directory / (name + "-copy.log"), timeout=60)
        path.chmod(0o600)  # Only the private copy becomes writable.
        if sha256(path) != digest:
            raise AssertionError("private compose disk differs from locked input")
    return disks


def probe(token, case):
    devices = " ".join(case["devices"])
    # This locked initramfs has only a small BusyBox (no printf/stty/awk/tr).
    # Empty prompts also keep host markers on their own lines after the probe.
    script = "PS1=\nPS2=\nt=" + shlex.quote(token) + "\n(\nset -eu\n" + r'''
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
echo
echo "$t:COMPOSE_BEGIN"
echo "$t:CPU"
cat /sys/devices/system/cpu/online
echo "$t:KERNEL"
uname -r
echo "$t:MODEL"
cat /proc/device-tree/model
echo
echo "$t:MOUNTS_BEFORE"
cat /proc/mounts
echo "$t:DEVICES"
'''
    if devices:
        script += "for n in " + devices + "; do\n" + r'''
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    [ ! -b /dev/$n ] || break
    sleep 1
done
[ -b /dev/$n ]
done
'''
    script += r'''
for p in /sys/block/vd* /sys/block/nvme*; do
    [ -e "$p" ] || continue
    n=${p##*/}; path=$(ls -l "$p"); serial=-
    if [ "$n" = nvme0n1 ]; then serial=$(cat /sys/class/nvme/nvme0/serial); fi
    echo "$t:DEVICE=$n|$path|$serial"
done
'''
    if devices:
        script += "first=" + case["devices"][0] + "\n" + r'''
mkdir -p /mnt/tools /tmp/blk-tools
mount -t ext4 -o ro /dev/$first /mnt/tools
cp /mnt/tools/usr/lib/ld-linux-riscv64-lp64d.so.1 /tmp/blk-tools/ld.so
cp /mnt/tools/usr/lib64/libc.so.6 /mnt/tools/usr/lib64/libcrypto.so.3 /mnt/tools/usr/lib64/libz.so.1 /tmp/blk-tools/
cp /mnt/tools/usr/bin/dd /mnt/tools/usr/bin/sha256sum /mnt/tools/usr/bin/sync /tmp/blk-tools/
export LD_LIBRARY_PATH=/tmp/blk-tools
umount /mnt/tools
tool() { /tmp/blk-tools/ld.so /tmp/blk-tools/"$@"; }
'''
        script += "for n in " + devices + "; do\n" + r'''
mkdir -p /mnt/$n
mount -t ext4 /dev/$n /mnt/$n
echo "$t:IRQ_BEFORE=$n"
cat /proc/interrupts
tool dd if=/dev/urandom of=/mnt/$n/libqemu-ci.bin bs=4096 count=64 oflag=direct conv=fsync
a=$(tool sha256sum /mnt/$n/libqemu-ci.bin); a=${a%% *}
tool sync
umount /mnt/$n
! grep "^/dev/$n " /proc/mounts
mount -t ext4 /dev/$n /mnt/$n
tool dd if=/mnt/$n/libqemu-ci.bin of=/tmp/readback.bin bs=4096 count=64 iflag=direct
b=$(tool sha256sum /tmp/readback.bin); b=${b%% *}
umount /mnt/$n
! grep "^/dev/$n " /proc/mounts
echo "$t:IRQ_AFTER=$n"
cat /proc/interrupts
[ "$a" = "$b" ]
echo "$t:DISK=$n|$a|$b|262144|unmounted"
done
'''
    script += r'''
echo "$t:MOUNTS_AFTER"
cat /proc/mounts
)
r=$?
echo "$t:COMPOSE_RC=$r"
echo "$t:COMPOSE_END"
'''
    return script


def dt_evidence(directory, case):
    dtb = directory / "machine.dtb"
    run_logged(["dtc", "-I", "dtb", "-O", "dts", dtb], directory / "machine.dts", timeout=30)

    def prop(node, name, kind="s"):
        return subprocess.check_output(["fdtget", "-t", kind, str(dtb), node, name],
                                       stderr=subprocess.PIPE, timeout=10).decode().strip()

    if prop("/", "model") != MODEL or prop("/chosen", "bootargs") != BOOTARGS:
        raise AssertionError("composed DT model/bootargs mismatch")
    if prop("/chosen", "stdout-path") != "/soc/serial@310b0000:115200n8":
        raise AssertionError("composed console address mismatch")
    if prop("/cpus/cpu@0", "reg", "x") != "0":
        raise AssertionError("composed CPU hart ID mismatch")
    nodes = subprocess.check_output(["fdtget", "-l", str(dtb), "/soc"], timeout=10).decode().split()
    for node, enabled in (("virtio@310a0000", "vda" in case["devices"]),
                          ("pcie@32000000", "nvme0n1" in case["devices"])):
        if (node in nodes) != enabled:
            raise AssertionError("optional DT device presence mismatch: " + node)
    aplic = prop("/soc/aplic@31120000", "phandle", "x")
    if prop("/soc/serial@310b0000", "interrupt-parent", "x") != aplic:
        raise AssertionError("UART does not reference S APLIC")
    if "vda" in case["devices"] and prop("/soc/virtio@310a0000", "interrupt-parent", "x") != aplic:
        raise AssertionError("blk does not reference S APLIC")
    if "nvme0n1" in case["devices"]:
        imsic = prop("/soc/imsics@3b000000", "phandle", "x")
        if prop("/soc/pcie@32000000", "msi-parent", "x") != imsic:
            raise AssertionError("DWC MSI does not reference S IMSIC")
    evidence = {"model": MODEL, "sha256": sha256(dtb), "soc_nodes": nodes}
    write_json(directory / "dt-evidence.json", evidence)
    return evidence


def disk_fds_closed(pid, disks):
    # Inspect while this exact host process is held after wait().
    for fd in Path(f"/proc/{pid}/fd").iterdir():
        for disk in disks.values():
            try:
                if os.path.samefile(fd, disk):
                    raise AssertionError("libqemu disk fd leaked after wait: " + disk.name)
            except FileNotFoundError:
                pass


def run_host(command, case, directory, disks):
    token = "BOSC_CI_" + uuid.uuid4().hex
    script = probe(token, case)
    (directory / "compose-probe.sh").write_text(script)
    read_fd, write_fd = os.pipe()
    command = [*map(str, command), "--control-fd", str(read_fd), "--hold-after-wait"]
    proc = None
    captured = ""
    deadline = time.monotonic() + case["timeout"]
    try:
        # Inherit the worker's process group for outer timeout/kill cleanup.
        proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, pass_fds=(read_fd,))
        os.close(read_fd)
        read_fd = None
        write_json(directory / "process.json", {"pid": proc.pid, "argv": command})
        with selectors.DefaultSelector() as selector, (directory / "uart0.log").open("wb") as log:
            selector.register(proc.stdout, selectors.EVENT_READ)

            def expect(pattern):
                nonlocal captured
                while time.monotonic() < deadline:
                    text = clean(captured)
                    if FATAL.search(text) or IOMMU_FATAL.search(text):
                        raise AssertionError("fatal composed Linux/storage output")
                    if re.search(pattern, text):
                        return
                    if proc.poll() is not None:
                        raise RuntimeError("composed host exited before verification")
                    for key, _ in selector.select(0.1):
                        data = os.read(key.fileobj.fileno(), 65536)
                        if not data:
                            raise RuntimeError("composed host closed output before verification")
                        log.write(data)
                        log.flush()
                        captured += data.decode(errors="replace")
                raise TimeoutError("compose Linux/probe/shutdown deadline exceeded")

            expect(r"(?m)^.*# $")
            proc.stdin.write(script.encode())
            proc.stdin.flush()
            expect(rf"(?m)^{token}:COMPOSE_END$")
            evidence = guest_assertions(captured, token, case)
            evidence["dt"] = dt_evidence(directory, case)
            os.write(write_fd, b"q")
            expect(r"(?m)^HOST_CONTINUES status=0$")
            if proc.poll() is not None:
                raise AssertionError("compose host did not survive wait")
            disk_fds_closed(proc.pid, disks)
            evidence.update({"host_alive_after_wait": True, "disk_fds_closed": True})
            os.write(write_fd, b"x")
            tail, _ = proc.communicate(timeout=10)
            log.write(tail)
            captured += tail.decode(errors="replace")
        if proc.returncode != 0:
            raise AssertionError("compose host exit was not zero")
        guest_assertions(captured, token, case)
        if re.findall(r"(?m)^HOST_CONTINUES status=(-?\d+)$", clean(captured)) != ["0"]:
            raise AssertionError("invalid compose host shutdown status")
        evidence["exitcode"] = proc.returncode
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


def execute(work, case, paths, directory):
    bundle = bundle_dir(work, case)
    try:
        disks = prepare_disks(work, case, paths, directory)
        command = [bundle / "libqemu" / case["binary"], bundle / "libqemu-system-riscv64.so",
                   bundle / "share/qemu/opensbi-riscv64-xiangshan-kmh-fw_dynamic.bin",
                   paths["iommu_kernel"], disks.get("vda", "-"), disks.get("nvme0n1", "-"),
                   "--devices", device_mode(case), "--append", BOOTARGS,
                   "--dump-dtb", directory / "machine.dtb"]
        if case["builder"] == "profile":
            command.append("--profile-builder")
        (directory / "replay-command.txt").write_text(shlex.join(map(str, command)) + "\n")
        evidence = run_host(command, case, directory, disks)
        evidence["fsck"] = {}
        for name, path in disks.items():
            log = directory / (name + "-fsck.log")
            run_logged(["e2fsck", "-fn", path], log, timeout=60)
            fsck_assertions(log.read_text(errors="replace"))
            evidence["fsck"][name] = "PASS"
    finally:
        cleanup_disks(work, case, directory)
    evidence["cleaned"] = True
    return evidence, []
