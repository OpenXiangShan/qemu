"""Strict assertions, separated for negative self-tests. GPL-2.0-or-later."""
import re

from common import HERE, pcie_segment, read_json

ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
FATAL = re.compile(r"Kernel panic|Oops:|BUG:|Unable to handle kernel|"
                   r"rcu:.*stall|soft lockup|FATAL:|^TRAP\b", re.M)
BOOTARGS = "console=ttyS0,115200 earlycon loglevel=8 ignore_loglevel root=/dev/pmem0 ro"


def clean(text):
    return ANSI.sub("", text).replace("\r", "")


def libqemu_host_assertions(text, code):
    text = clean(text)
    if (FATAL.search(text) or len(re.findall(r"(?m)^HOST_STARTED$", text)) != 1
            or re.findall(r"(?m)^HOST_CONTINUES status=(-?\d+)$", text) != [str(code)]
            or text.index("HOST_STARTED") > text.index("HOST_CONTINUES")):
        raise AssertionError("missing/invalid libqemu host survival or exit status")


def libqemu_evidence(case, evidence):
    if case["kind"] == "libqemu-unit":
        valid = evidence == {"marker": case["pass_marker"], "exitcode": 0}
    elif case["kind"] == "libqemu-exit":
        valid = evidence == {"exit_statuses": [0, 7], "host_continues": [0, 7],
                             "split_init": case["split_init"]}
    elif case["kind"] == "libqemu-compose-linux":
        from compose_checks import validate_evidence
        validate_evidence(case, evidence)
        return
    else:
        valid = (evidence.get("exitcode") == 0 and evidence.get("host_continues") == 0
                 and evidence.get("host_alive_after_wait") is True
                 and evidence.get("split_init") == case["split_init"]
                 and evidence.get("online") == "0-3"
                 and evidence.get("kernel") == read_json(HERE / "images.lock.json")["assets"]["kernel"]["kernel_release"]
                 and evidence.get("root") == "pmem0 + overlay"
                 and bool(evidence.get("dt", {}).get("sha256")))
    if not valid:
        raise ValueError("libqemu acceptance evidence missing or invalid")


def cpu_set(value):
    result = set()
    for part in value.split(","):
        if not re.fullmatch(r"\d+(?:-\d+)?", part):
            raise AssertionError(f"invalid online CPU list: {value!r}")
        low, _, high = part.partition("-")
        low, high = int(low), int(high or low)
        if high < low or high > 4096:
            raise AssertionError("invalid CPU range")
        result.update(range(low, high + 1))
    return result


def linux_assertions(text, token, case):
    text = clean(text)
    if FATAL.search(text):
        raise AssertionError("fatal guest boot log")
    for required in ("OpenSBI", "Linux version", "Switching root to overlay(root=/dev/pmem0)",
                     "EXT4-fs (pmem0): mounted filesystem"):
        if required not in text:
            raise AssertionError(f"missing boot evidence: {required}")
    model = ("Xiangshan Kunminghu QEMU machine" if case["owner"] == "kmh" else
             "BOSC KMH single-die SoC" if case["mode"] == "single" else
             "BOSC KMH multi-die SoC")
    if model not in text:
        raise AssertionError("unexpected or missing machine model")
    # The token occurs in the command, but these anchored sections only occur
    # in actual output. Values and command completion are validated separately.
    pattern = (rf"(?m)^{token}:BEGIN\n{token}:CPU\n([^\n]+)\n"
               rf"{token}:KERNEL\n([^\n]+)\n{token}:MOUNTS\n"
               rf"(.*?)^{token}:RC=(\d+)\n{token}:END$")
    match = re.search(pattern, text, re.M | re.S)
    if not match:
        raise AssertionError("missing complete, non-echoed shell response")
    online, kernel, mounts, rc = match.groups()
    if cpu_set(online) != set(range(case["cpus"])):
        raise AssertionError(f"online CPUs {online}, expected 0..{case['cpus'] - 1}")
    asset = case.get("kernel_asset", "iommu_kernel" if case.get("kind") == "iommu-linux" else "kernel")
    if kernel != read_json(HERE / "images.lock.json")["assets"][asset]["kernel_release"]:
        raise AssertionError(f"unexpected kernel release: {kernel}")
    if not re.search(r"(?m)^overlay / overlay (\S+) ", mounts):
        raise AssertionError("root is not mounted as overlay")
    if rc != "0":
        raise AssertionError("guest probe command failed")
    return {"online": online, "kernel": kernel, "root": "pmem0 + overlay"}


IOMMU_FATAL = re.compile(r"iommu[^\n]*\bfault\b|nvme(?:[ :]|\d)[^\n]*(?:timeout|timed out|failed|resetting)|"
                         r"I/O error|Buffer I/O|blk_update_request|DMA (?:transfer|mapping) error", re.I)


def iommu_assertions(text, token, case, qemu_log=""):
    text = clean(text)
    combined = text + "\n" + clean(qemu_log)
    if FATAL.search(combined) or IOMMU_FATAL.search(combined):
        raise AssertionError("IOMMU/NVMe fatal output")
    for pattern in (r"iommu: Default domain type: Translated",
                    r"riscv,iommu [^\n]+ using MSIs"):
        if not re.search(pattern, text):
            raise AssertionError(f"missing translated IOMMU/MSI evidence: {pattern}")
    match = re.search(rf"(?m)^{token}:IOMMU_BEGIN\n(.*?)^{token}:IOMMU_RC=0\n{token}:IOMMU_END$",
                      text, re.S | re.M)
    if not match:
        raise AssertionError("missing successful IOMMU shell response")
    body = match[1]
    count = re.findall(rf"(?m)^{token}:NVME_COUNT=(\d+)$", body)
    if count != [str(case["disks"])]:
        raise AssertionError("wrong NVMe controller count")
    devices = re.findall(rf"(?m)^{token}:DEVICE=(.*)$", body)
    transfers = re.findall(rf"(?m)^{token}:DMA=(.*)$", body)
    if len(devices) != case["disks"] or len(transfers) != case["disks"]:
        raise AssertionError("missing or duplicate endpoint DMA evidence")
    result = []
    seen = set()
    bdfs = set()
    groups = set()
    controllers = set()
    for line in devices:
        fields = line.split("|")
        if len(fields) != 8:
            raise AssertionError("malformed endpoint evidence")
        serial, ctrl, bdf, group, domain, state, driver, iommu = fields
        if not re.fullmatch(r"BOSC-CI-\d+", serial) or serial in seen:
            raise AssertionError("unknown or duplicate NVMe serial")
        index = int(serial.removeprefix("BOSC-CI-"))
        if index >= len(case["nvme_buses"]):
            raise AssertionError("unknown NVMe serial index")
        bus = case["nvme_buses"][index]
        segment = pcie_segment(bus)
        if bdf != f"{segment:04x}:01:00.0" or bdf in bdfs:
            raise AssertionError(f"unexpected endpoint BDF: {bdf}")
        if (not re.fullmatch(r"nvme\d+", ctrl) or not group.isdigit()
                or domain not in ("DMA", "DMA-FQ") or state != "live" or driver != "nvme"):
            raise AssertionError("NVMe lacks live driver/translated IOMMU group")
        if group in groups or ctrl in controllers:
            raise AssertionError("roots share an IOMMU group or controller")
        if not re.fullmatch(r"riscv-iommu@[0-9a-f]+\.iommu", iommu):
            raise AssertionError("missing endpoint IOMMU device")
        device = iommu.removeprefix("riscv-iommu@")
        if not re.search(rf"riscv,iommu {re.escape(device)}: using MSIs", text):
            raise AssertionError("endpoint IOMMU is not using MSIs")
        if not re.search(rf"pci {re.escape(bdf)}: Adding to iommu group {group}\b", text):
            raise AssertionError("missing NVMe IOMMU group attachment")
        if not re.search(rf"nvme {ctrl}: pci function {re.escape(bdf)}\b", text):
            raise AssertionError("missing NVMe probe")
        if not re.search(rf"nvme {ctrl}: \d+/\d+/\d+ default/read/poll queues", text):
            raise AssertionError("missing initialized NVMe I/O queues")
        dma = [row.split("|") for row in transfers if row.split("|")[0] == serial]
        if len(dma) != 1 or len(dma[0]) != 8:
            raise AssertionError("missing/duplicate/malformed DMA result")
        _, source, readback, before, after, size, mode, irq = dma[0]
        if (not re.fullmatch(r"[a-f0-9]{64}", source) or source != readback
                or not before.isdigit() or not after.isdigit() or int(after) <= int(before)
                or (size, mode, irq) != ("1048576", "direct", "msi")):
            raise AssertionError("direct DMA hash or interrupt delta failed")
        seen.add(serial)
        bdfs.add(bdf)
        groups.add(group)
        controllers.add(ctrl)
        result.append({"serial": serial, "controller": ctrl, "bdf": bdf,
                       "bus": bus, "segment": segment,
                       "iommu": iommu,
                       "did": 0x100,
                       "group": int(group),
                       "domain": domain, "sha256": source, "bytes": int(size),
                       "irq_before": int(before), "irq_after": int(after)})
    if seen != {f"BOSC-CI-{i}" for i in range(case["disks"])}:
        raise AssertionError("unexpected NVMe endpoints")
    if len({r["iommu"] for r in result}) != case["disks"]:
        raise AssertionError("unexpected shared/independent endpoint IOMMUs")
    return result


def mcu_assertions(logs, dies):
    expected = len(dies)
    known = read_json(HERE / "known-issues.json")["MCU-DIE-NUM-RESET"]
    counts = []
    for die, raw in enumerate(logs):
        text = clean(raw)
        if die not in dies:
            if "[kmh-mcu]" in text:
                raise AssertionError(f"disabled die {die} executed MCU firmware")
            continue
        prefix = rf"\[kmh-mcu\] die={die} uncore={die + 1} stage="
        starts = re.findall(prefix + r"start extra=0x([0-9a-fA-F]+)", text)
        if len(starts) != 1:
            raise AssertionError(f"die {die}: missing/duplicate MCU start")
        counts.append(int(starts[0], 16))
        stages = ["start", "qspi", "copy-begin", "copy-done", "ready", "release"] if die == 0 else ["start", "qspi", "copy-seen", "ready", "release"]
        cursor = 0
        for stage in stages:
            match = re.search(prefix + stage + r" extra=0x([0-9a-fA-F]+)", text[cursor:])
            if not match:
                raise AssertionError(f"die {die}: missing/out-of-order stage {stage}")
            value = int(match[1], 16)
            exact = {"copy-begin": 0x8000000, "copy-done": 0x80000000,
                     "copy-seen": 1, "release": 2}
            if stage in exact and value != exact[stage]:
                raise AssertionError(f"die {die}: unexpected {stage} value")
            if stage == "ready" and not 1 <= value <= expected:
                raise AssertionError(f"die {die}: invalid ready count")
            cursor += match.end()
        if FATAL.search(text):
            raise AssertionError(f"die {die}: fatal log")
    if counts == [known["observed"]] * expected:
        return {"status": "XFAIL", "id": "MCU-DIE-NUM-RESET", "observed": counts,
                "expected": expected, "message": known["note"]}
    if counts == [expected] * expected:
        return {"status": "XPASS", "id": "MCU-DIE-NUM-RESET",
                "message": "MCU count recovered; remove/update the reviewed waiver"}
    raise AssertionError(f"MCU count outside exact waiver: {counts}, expected {expected}")


def itgen_assertions(text):
    text = clean(text)
    required = ["target-index=32 die-id=2", "imsic=0x20001c000000 clear=0x10000409014c",
                "controller=0 target=16 claimed=56",
                "PASS: test_RISCV_AIA_aplic_irq_priority_cross_die",
                "1 tests run, 0 failed, 1 passed"]
    cursor = 0
    for item in required:
        position = text.find(item, cursor)
        if position < 0:
            raise AssertionError(f"missing ITGen assertion: {item}")
        cursor = position + len(item)
    if re.search(r"\bFAIL(?:ED)?:|[1-9]\d* failed|assertion failed", text, re.I):
        raise AssertionError("ITGen failure in log")
    # This locked binary produces a completion trap followed by a double-fault
    # parking line. Only that exact ordered tail AFTER all success evidence is
    # accepted; arbitrary TRAP/FATAL elsewhere remains a failure.
    name = "test_RISCV_AIA_aplic_irq_priority_cross_die"
    tail = (rf"(?m)^TRAP in {name}: priv=M hart=0+ mcause=0x0*2 "
            rf"mtval=0x0*5006b mepc=0x[0-9a-f]+ mstatus=0x[0-9a-f]+\n"
            rf"(?:FATAL: double fault in {name}, halting test runner\n)?")
    remainder = text[:cursor] + re.sub(tail, "", text[cursor:], count=1)
    if FATAL.search(remainder):
        raise AssertionError("unexpected ITGen trap/fatal output")
    return {"target_index": 32, "target_hart": 16, "claimed": 56,
            "imsic": "0x20001c000000"}


def qtest_assertions(text, expected):
    if re.search(r"(?m)^not ok|Bail out!|# SKIP|# TODO", text):
        raise AssertionError("qtest failed or unexpectedly skipped")
    actual = re.findall(r"(?m)^ok \d+ (\S+)\s*$", text)
    # GLib groups paths hierarchically, so registration order is not output
    # order. Keep exact names and cardinality (duplicates must still fail).
    if sorted(actual) != sorted(expected):
        raise AssertionError(f"qtest results differ: {actual!r}")
    return [{"id": name, "status": "PASS"} for name in actual]
