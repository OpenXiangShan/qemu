"""Fail-closed aggregation and JUnit. SPDX-License-Identifier: GPL-2.0-or-later."""
from collections import Counter
import xml.etree.ElementTree as ET

from common import HERE, catalog, read_json, write_json
from checks import libqemu_evidence


def aggregate(work, group=None):
    selection = read_json(work / "selected-tests.json")
    rows = []
    errors = []
    for case in catalog():
        if group and group != case["group"]:
            continue
        case_id = case["id"]
        if case_id not in selection["selected"]:
            rows.append({"id": case_id, "status": "NOT_SELECTED", "message": "not selected by impact analysis"})
            continue
        path = work / "results" / case_id / "result.json"
        try:
            result = read_json(path)
            if (result["id"] != case_id or result.get("selection_sha256") != selection["config_sha256"]
                    or result.get("run_id") != selection["run_id"]):
                raise ValueError("result identity/configuration mismatch")
            if result["status"] not in ("PASS", "FAIL"):
                raise ValueError("invalid case status")
            if result["status"] == "PASS":
                if case["kind"].startswith("libqemu"):
                    libqemu_evidence(case, result.get("evidence", {}))
                ids = [c["id"] for c in result.get("checks", [])]
                if case["kind"] == "qtest" and sorted(ids) != sorted("/riscv64" + n for n in case["tests"]):
                    raise ValueError("qtest sub-results missing or duplicated")
                if case["kind"] == "mcu" and ids != ["MCU-DIE-NUM-RESET"]:
                    raise ValueError("MCU reset-count assertion missing")
                if case["kind"] == "iommu-linux":
                    endpoints = result.get("evidence", {}).get("iommu", [])
                    if (len(endpoints) != case["disks"] or
                            {e.get("serial") for e in endpoints} !=
                            {f"BOSC-CI-{i}" for i in range(case["disks"])}):
                        raise ValueError("IOMMU endpoint results missing or duplicated")
            rows.append({"id": case_id, "status": result["status"],
                         "message": result.get("message", ""), "seconds": result.get("seconds", 0)})
            for check in result.get("checks", []):
                if check.get("status") not in ("PASS", "FAIL", "XFAIL", "XPASS"):
                    raise ValueError("invalid assertion status")
                rows.append(dict(check, id=case_id + ":" + check["id"]))
        except (OSError, ValueError, KeyError) as exc:
            errors.append(case_id)
            rows.append({"id": case_id + ":result", "status": "FAIL", "message": f"missing/invalid result: {exc}"})
    for stage in sorted((work / "stage-errors").glob("*.json")) if (work / "stage-errors").exists() else []:
        failure = read_json(stage)
        if not group or failure.get("group") in (None, group):
            rows.append({"id": "infrastructure:" + stage.stem, "status": "FAIL", "message": failure["message"]})
    counts = dict(Counter(row["status"] for row in rows))
    failed = bool(counts.get("FAIL") or counts.get("XPASS"))
    report = {"status": "FAIL" if failed else "PASS", "counts": counts,
              "run_id": selection["run_id"], "harness_sha256": selection["config_sha256"],
              "selection": selection,
              "asset_baseline": read_json(HERE / "images.lock.json"),
              "selected_cases": len(selection["selected"]), "results": rows,
              "missing_or_invalid": errors,
              "deferred": read_json(HERE / "cases.json")["deferred"],
              "deployment": "Local host implementation; GitLab/JF deployment not implied"}
    build_manifest = work / "bundle/build.json"
    if build_manifest.exists():
        report["build"] = read_json(build_manifest)
    library_manifest = work / "bundle-libqemu/build.json"
    if library_manifest.exists():
        report["libqemu_build"] = read_json(library_manifest)
    report["counting_note"] = "Counts include qtest executable containers AND their subtests; MCU count assertions are separate. Consult the selected case manifest for MR/extended coverage."
    report_dir = work / "reports" / (group or "all")
    write_json(report_dir / "summary.json", report)
    suite = ET.Element("testsuite", name="bosc-first-phase", tests=str(len(rows)),
                       failures=str(sum(r["status"] in ("FAIL", "XPASS") for r in rows)),
                       skipped=str(sum(r["status"] in ("XFAIL", "NOT_SELECTED") for r in rows)))
    for row in rows:
        test = ET.SubElement(suite, "testcase", classname="bosc-ci", name=row["id"], time=str(row.get("seconds", 0)))
        if row["status"] in ("FAIL", "XPASS"):
            ET.SubElement(test, "failure", type=row["status"], message=row.get("message", row["status"]))
        elif row["status"] in ("XFAIL", "NOT_SELECTED"):
            ET.SubElement(test, "skipped", type=row["status"], message=row.get("message", row["status"]))
    ET.indent(suite)
    ET.ElementTree(suite).write(report_dir / "junit.xml", encoding="utf-8", xml_declaration=True)
    lines = ["# BOSC CI results", "", f"Overall: **{report['status']}**", "",
             "| Case/assertion | Status | Details |", "| --- | --- | --- |"]
    for row in rows:
        message = str(row.get("message", "")).replace("|", "/").replace("\n", " ")
        lines.append(f"| {row['id']} | {row['status']} | {message} |")
    lines += ["", "Deferred coverage: " + ", ".join(report["deferred"]), "",
              "XFAIL is an acknowledged failing assertion, not a successful synchronization test.",
              "Local PASS is not evidence of deployed GitLab, published JF assets or Docker validation."]
    (report_dir / "summary.md").write_text("\n".join(lines) + "\n")
    return report
