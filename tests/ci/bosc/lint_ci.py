#!/usr/bin/env python3
"""Offline YAML and manifest consistency; NOT server-side GitLab CI Lint."""
# SPDX-License-Identifier: GPL-2.0-or-later
import yaml
import re

from common import HERE, SOURCE, catalog, read_json


class UniqueLoader(yaml.SafeLoader):
    pass


def mapping(loader, node, deep=False):
    result = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if key in result:
            raise ValueError(f"duplicate YAML key: {key}")
        result[key] = loader.construct_object(value_node, deep=deep)
    return result


UniqueLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, mapping)


def validate_cases(cases, assets):
    for spec in assets.values():
        if (not isinstance(spec.get("size"), int) or spec["size"] <= 0
                or not re.fullmatch(r"[a-f0-9]{64}", spec.get("sha256", ""))
                or not spec.get("jf_path")):
            raise ValueError("asset must have fixed size, SHA256 and JF path")
    for case in cases:
        if case.get("tier") not in ("mr", "extended"):
            raise ValueError(f"invalid case tier: {case['id']}")
        if not set(case["assets"]).issubset(assets):
            raise ValueError(f"unknown asset in {case['id']}")
        if case["kind"] == "iommu-linux":
            buses = case.get("nvme_buses", [])
            expected = ([f"pcie-d{i}-p0" for i in range(4)]
                        if case.get("topology") == "four-die" else
                        ["dw-pcie" if case["owner"] == "kmh" else "pcie-d0-p0"])
            if (buses != expected or case.get("disks") != len(buses)
                    or set(case["assets"]) != {"fw_jump", "iommu_kernel", "rootfs"}
                    or case["group"] != "iommu"
                    or case.get("machine") != ("xiangshan-kunminghu" if case["owner"] == "kmh" else "kmh-bosc-soc")
                    or case.get("topology") not in ("single-root", "four-die")
                    or case.get("topology") == "four-die" and case["tier"] != "extended"):
                raise ValueError(f"invalid IOMMU Linux topology/assets: {case['id']}")


def main():
    config = yaml.load((SOURCE / ".gitlab-ci.d/bosc.yml").read_text(), Loader=UniqueLoader)
    groups = set()
    for name, job in config.items():
        if not isinstance(job, dict) or name in ("default", "variables", "workflow"):
            continue
        if any(key in job for key in ("image", "services", "allow_failure")):
            raise ValueError(f"unexpected container/waiver key in {name}")
        for dep in job.get("dependencies", []):
            if dep not in config:
                raise ValueError(f"unknown dependency: {dep}")
        if "script" in job and not all(isinstance(line, str) for line in job["script"]):
            raise ValueError(f"invalid script in {name}")
        if "BOSC_CI_GROUP" in job.get("variables", {}):
            groups.add(job["variables"]["BOSC_CI_GROUP"])
    if groups != {case["group"] for case in catalog()}:
        raise ValueError("YAML/test group mismatch")
    assets = read_json(HERE / "images.lock.json")["assets"]
    validate_cases(catalog(), assets)
    if "bosc:iommu" not in config["bosc:report"]["dependencies"]:
        raise ValueError("final report must consume IOMMU results")
    if config["bosc:report"]["when"] != "always":
        raise ValueError("final report must run after failures")
    print("PASS: offline YAML/manifest checks (target GitLab CI Lint still required)")


if __name__ == "__main__":
    main()
