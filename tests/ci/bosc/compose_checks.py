"""Acceptance contracts for Linux on a host-assembled SoC. GPL-2.0-or-later."""
import re

from checks import FATAL, IOMMU_FATAL, clean
from common import HERE, read_json

MODEL = "Host-assembled Xiangshan Kunminghu"
BOOTARGS = "console=ttyS0,115200 earlycon=sbi loglevel=8 rdinit=/bin/sh"
SERIAL = "LIBQEMU-KMH-NVME"


def device_mode(case):
    return {(): "none", ("vda",): "blk", ("nvme0n1",): "nvme",
            ("vda", "nvme0n1"): "blk-nvme"}[tuple(case["devices"])]


def root_in_ram(mounts):
    rows = [line.split() for line in mounts.splitlines()]
    roots = [r for r in rows if len(r) >= 4 and r[1] == "/"]
    if len(roots) != 1 or roots[0][2] not in ("rootfs", "ramfs", "tmpfs"):
        raise AssertionError("compose shell root must be in RAM")
    if any(r[0].startswith(("/dev/vd", "/dev/nvme")) for r in rows if r):
        raise AssertionError("test disk still mounted")
    if any(len(r) >= 2 and r[1].startswith(("/tmp", "/mnt")) for r in rows):
        raise AssertionError("tools or test mount unexpectedly backed by another filesystem")


def irq_count(snapshot, name):
    pattern = r"APLIC-MSI-.*\bvirtio0$" if name == "vda" else r"PCI-MSIX-.*\bnvme0q[1-9][0-9]*$"
    total = 0
    matched = False
    for line in snapshot.splitlines():
        if not re.search(pattern, line):
            continue
        counts = re.match(r"\s*\d+:((?:\s+\d+)+)\s+\S", line)
        if not counts:
            raise AssertionError("invalid IRQ snapshot counters")
        total += sum(map(int, counts[1].split()))
        matched = True
    if not matched:
        raise AssertionError("missing device IRQ domain/queue")
    return total


def guest_assertions(text, token, case):
    text = clean(text)
    if FATAL.search(text) or IOMMU_FATAL.search(text):
        raise AssertionError("fatal composed Linux/storage output")
    host = f"HOST_ASSEMBLED Kunminghu: devices={device_mode(case)} builder={case['builder']}"
    if not re.search(rf"(?m)^{re.escape(host)}$", text):
        raise AssertionError("missing composed-host identity")
    for value in ("OpenSBI", "Linux version", "Machine model: " + MODEL):
        if value not in text:
            raise AssertionError("missing composed Linux boot evidence: " + value)
    pattern = (rf"(?m)^{token}:COMPOSE_BEGIN\n{token}:CPU\n([^\n]+)\n"
               rf"{token}:KERNEL\n([^\n]+)\n{token}:MODEL\n([^\n]+)\n"
               rf"{token}:MOUNTS_BEFORE\n(.*?)^{token}:DEVICES\n(.*?)"
               rf"^{token}:MOUNTS_AFTER\n(.*?)^{token}:COMPOSE_RC=0\n{token}:COMPOSE_END$")
    match = re.search(pattern, text, re.S)
    if not match:
        raise AssertionError("missing successful non-echoed compose shell response")
    online, kernel, model, before, body, after = match.groups()
    model = model.rstrip("\0")  # DT strings end with NUL; this BusyBox lacks tr.
    expected_kernel = read_json(HERE / "images.lock.json")["assets"]["iommu_kernel"]["kernel_release"]
    if (online, kernel, model) != ("0", expected_kernel, MODEL):
        raise AssertionError("compose CPU/kernel/model mismatch")
    root_in_ram(before)
    root_in_ram(after)
    devices = re.findall(rf"(?m)^{token}:DEVICE=(.*)$", body)
    transfers = re.findall(rf"(?m)^{token}:DISK=(.*)$", body)
    if len(devices) != len(case["devices"]) or len(transfers) != len(devices):
        raise AssertionError("missing/extra compose block device or transfer")
    result = []
    seen = set()
    for entry in devices:
        fields = entry.split("|")
        if len(fields) != 3:
            raise AssertionError("invalid compose device evidence")
        name, path, serial = fields
        serial = serial.strip()
        if name not in case["devices"] or name in seen:
            raise AssertionError("unexpected/duplicate composed block device")
        seen.add(name)
        if name == "vda":
            if "310a0000.virtio" not in path or serial != "-":
                raise AssertionError("virtio block is not on the composed MMIO device")
        elif "32000000.pcie" not in path or "0000:01:00.0" not in path or serial != SERIAL:
            raise AssertionError("NVMe is not on the composed DWC root")
        rows = [r.split("|") for r in transfers if r.startswith(name + "|")]
        if len(rows) != 1 or len(rows[0]) != 5:
            raise AssertionError("missing/duplicate block I/O evidence")
        _, written, readback, size, state = rows[0]
        snapshots = re.findall(rf"(?ms)^{token}:IRQ_BEFORE={name}\n(.*?)"
                               rf"^{token}:IRQ_AFTER={name}\n(.*?)^{token}:DISK={name}\|", body)
        if len(snapshots) != 1:
            raise AssertionError("missing/duplicate IRQ snapshots")
        irq_before, irq_after = [irq_count(s, name) for s in snapshots[0]]
        if (not re.fullmatch(r"[a-f0-9]{64}", written) or written != readback
                or irq_after <= irq_before or (size, state) != ("262144", "unmounted")):
            raise AssertionError("compose readback/hash/IRQ/unmount failed")
        result.append({"device": name, "path": path, "serial": serial, "sha256": written,
                       "bytes": int(size), "irq_before": int(irq_before),
                       "irq_after": int(irq_after), "unmounted": True})
    return {"online": online, "kernel": kernel, "model": model, "root": "initramfs",
            "devices": result, "builder": case["builder"]}


def fsck_assertions(text):
    # Some fsck versions return zero despite skipped recovery/count warnings.
    if (any(f"Pass {i}:" not in text for i in range(1, 6))
            or not re.search(r"\d+/\d+ files.*\d+/\d+ blocks", text)
            or re.search(r"warning|wrong|error|corrupt|skipping|recovering|UNEXPECTED|fix\?", text, re.I)):
        raise AssertionError("filesystem check did not complete cleanly")


def validate_evidence(case, evidence):
    expected_kernel = read_json(HERE / "images.lock.json")["assets"]["iommu_kernel"]["kernel_release"]
    devices = evidence.get("devices", [])
    expected = set(case["devices"])
    if (evidence.get("online") != "0" or evidence.get("kernel") != expected_kernel
            or evidence.get("model") != MODEL or evidence.get("root") != "initramfs"
            or evidence.get("builder") != case["builder"]
            or evidence.get("host_alive_after_wait") is not True
            or evidence.get("disk_fds_closed") is not True or evidence.get("exitcode") != 0
            or evidence.get("cleaned") is not True
            or not evidence.get("dt", {}).get("sha256")
            or len(devices) != len(expected)
            or {d.get("device") for d in devices} != expected
            or evidence.get("fsck") != {d: "PASS" for d in expected}):
        raise ValueError("compose Linux evidence missing or invalid")
    for device in devices:
        if (device.get("unmounted") is not True or device.get("bytes") != 262144
                or not re.fullmatch(r"[a-f0-9]{64}", device.get("sha256", ""))
                or device.get("irq_after", -1) <= device.get("irq_before", -1)):
            raise ValueError("compose disk evidence missing or invalid")
