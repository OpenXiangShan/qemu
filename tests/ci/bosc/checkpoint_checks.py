"""Fail-closed checkpoint acceptance, shared by execution and report replay.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import math

from checks import FATAL, clean

EXPECTED = {
    "native": ["single-s", "single-u", "single-4c", "multi-2c", "sparse-4c",
               "truncated", "corrupt", "wrong-ram", "bad-entry", "bad-firmware",
               "bad-cpu-privilege", "external-stub-conflict", "bad-topology",
               "unsupported-m", "reslice", "interval", "qmp"],
    "legacy": ["single-0x1", "multi-0xf", "multi-0x5", "interval", "qmp",
               "truncated", "corrupt", "bad-topology"],
    "raw": ["sparse-unpack", "raw-restore", "unpack-truncated", "unpack-corrupt",
            "unpack-trailing"],
    "linux-v2": ["stage1", "stage2", "stage3", "shell"],
    "linux-litmus": ["switchroot", "native-restore", "litmus-window"],
}


def validate_log(text, required=(), forbidden=()):
    text = clean(text)
    if FATAL.search(text) or "NATIVE_HARTS_FAIL" in text or "STAGE_FAIL" in text:
        raise AssertionError("fatal checkpoint guest output")
    if any(token not in text for token in required):
        raise AssertionError("missing checkpoint evidence: " + repr(required))
    if any(token in text for token in forbidden):
        raise AssertionError("unexpected checkpoint output: " + repr(forbidden))
    return text


def litmus_assertions(text, seconds):
    text = validate_log(text, ("KMH_NATIVE_PLATFORM_RESTORED",
        "native firmware returned to all selected harts; no host CSR/device fixups",
        "CHECKPOINT_ROOTFS_RESUMED pid=1 before bash", "LITMUS_32CORE_ONLINE=0-31",
        "LITMUS_32CORE_ISOLATED=16-31", "Cpus_allowed_list:\t0-31",
        "LITMUS_32CORE_BEGIN"), ("Linux version", "OpenSBI", "CHECKPOINT_WORKLOAD_EXIT="))
    count = 0
    classification = None
    for line in text.splitlines():
        if line.startswith("Test "):
            classification = line.rsplit(" ", 1)[-1]
        elif line.startswith("Observation "):
            fields = line.split()
            if (classification not in ("Allowed", "Required", "Forbidden")
                    or len(fields) < 5 or not all(x.isdigit() for x in fields[-2:])):
                raise AssertionError("malformed litmus result")
            if ((classification == "Forbidden" and int(fields[-2]) != 0)
                    or (classification == "Required" and fields[-3] != "Always")):
                raise AssertionError("unexpected litmus witness: " + line)
            count += 1
    if (count == 0 or not isinstance(seconds, (int, float)) or isinstance(seconds, bool)
            or not math.isfinite(seconds) or seconds < 10):
        raise AssertionError("missing completed litmus results or 10-second observation")
    return {"online": "0-31", "isolated": "16-31", "allowed": "0-31",
            "completed_results": count, "observation_seconds": seconds,
            "guest_exit": None, "stop_reason": "host-observation-window",
            "whole_suite_completed": False}


def evidence_assertions(case, evidence, checks):
    expected = EXPECTED[case["scenario"]]
    if ([c.get("id") for c in checks] != expected
            or any(c.get("status") != "PASS" for c in checks)
            or evidence.get("scenario") != case["scenario"]
            or evidence.get("check_count") != len(expected)
            or not isinstance(evidence.get("commands"), int) or evidence["commands"] < 1):
        raise ValueError("checkpoint sub-results missing, duplicate or invalid")
    if case["scenario"] == "linux-litmus":
        duration = evidence.get("observation_seconds")
        count = evidence.get("completed_results")
        if (not isinstance(duration, (int, float)) or isinstance(duration, bool)
                or not math.isfinite(duration) or duration < case["observe_seconds"]
                or not isinstance(count, int) or isinstance(count, bool) or count < 1
                or evidence.get("online") != "0-31" or evidence.get("isolated") != "16-31"
                or evidence.get("allowed") != "0-31"
                or evidence.get("guest_exit") is not None
                or evidence.get("whole_suite_completed") is not False
                or evidence.get("stop_reason") != "host-observation-window"
                or evidence.get("restore_format") != case["restore_format"]
                or evidence.get("reserved_regions_verified") is not True):
            raise ValueError("invalid bounded checkpoint litmus evidence")
