#!/usr/bin/env python3
"""Offline YAML and manifest consistency; NOT server-side GitLab CI Lint."""
# SPDX-License-Identifier: GPL-2.0-or-later
import yaml

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
    for case in catalog():
        if not set(case["assets"]).issubset(assets):
            raise ValueError(f"unknown asset in {case['id']}")
    if config["bosc:report"]["when"] != "always":
        raise ValueError("final report must run after failures")
    print("PASS: offline YAML/manifest checks (target GitLab CI Lint still required)")


if __name__ == "__main__":
    main()
