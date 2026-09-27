"""One selection policy for local runs and GitLab. GPL-2.0-or-later."""
import fnmatch
import os
import uuid

from common import HERE, SOURCE, catalog, config_hash, git, read_json, write_json


def parse_status(data):
    """Use NUL records: whitespace, deletes and both rename paths are safe."""
    records = data.decode().split("\0")
    paths = []
    i = 0
    while i < len(records) and records[i]:
        status = records[i]
        if status[0] not in "ACDMRTUXB":
            raise ValueError("invalid git name-status output")
        count = 2 if status[0] in "RC" else 1
        if i + count >= len(records) or not all(records[i + 1:i + 1 + count]):
            raise ValueError("incomplete git name-status output")
        paths.extend(records[i + 1:i + 1 + count])
        i += count + 1
    return paths


def changed_paths(base, head, source=SOURCE):
    merge_base = git("merge-base", base, head, source=source).decode().strip()
    return parse_status(git("diff", "--name-status", "-z", "--find-renames",
                            merge_base, head, "--", source=source))


def choose(paths=None, full_reason=None):
    policy = read_json(HERE / "impact-map.json")
    owners = set()
    reasons = []
    if full_reason:
        owners.update(("bosc", "kmh"))
        reasons.append({"path": None, "reason": full_reason})
    else:
        for path in paths or []:
            matches = [r for r in policy["rules"]
                       if any(fnmatch.fnmatchcase(path, p) for p in r["patterns"])]
            # First matching category wins. Machine-specific headers must not
            # fall through to the conservative include/* catch-all.
            if matches:
                rule = matches[0]
                owners.update(rule["owners"])
                reasons.append({"path": path, "reason": rule["reason"]})
            elif any(fnmatch.fnmatchcase(path, p) for p in policy["docs"]):
                reasons.append({"path": path, "reason": "documentation-only"})
            else:
                owners.update(("bosc", "kmh"))
                reasons.append({"path": path, "reason": "unknown path: conservative full"})
    cases = catalog()
    return {"schema": 1, "config_sha256": config_hash(),
            "selected": [c["id"] for c in cases if c["owner"] in owners],
            "not_selected": [c["id"] for c in cases if c["owner"] not in owners],
            "reasons": reasons}


def select(work, base=None, head="HEAD", case_ids=None):
    if case_ids:
        known = {c["id"] for c in catalog()}
        unknown = set(case_ids) - known
        if unknown:
            raise ValueError(f"unknown case IDs: {sorted(unknown)}")
        result = choose(full_reason="explicit local case selection")
        result["selected"] = list(dict.fromkeys(case_ids))
        result["not_selected"] = sorted(known - set(case_ids))
    else:
        reason = None
        pipeline = os.environ.get("CI_PIPELINE_SOURCE")
        if pipeline == "merge_request_event" and not base:
            base = os.environ.get("CI_MERGE_REQUEST_DIFF_BASE_SHA")
            head = os.environ.get("CI_COMMIT_SHA", head)
            if not base:
                reason = "missing MR diff base: conservative full"
        elif pipeline:
            reason = f"{pipeline}: full first-phase regression"
        if reason or not base:
            result = choose(full_reason=reason or "local full regression")
        else:
            try:
                paths = changed_paths(base, head)
                if not pipeline:
                    # Local builds consume working-tree edits too. Do not let
                    # a clean commit range hide dirty code from selection.
                    paths += parse_status(git("diff", "HEAD", "--name-status", "-z", "--find-renames"))
                    paths += [p for p in git("ls-files", "--others", "--exclude-standard", "-z").decode().split("\0") if p]
                result = choose(paths)
            except Exception:
                result = choose(full_reason="unavailable/incomplete diff: conservative full")
        result.update({"base": base, "head": head})
    result["revision"] = git("rev-parse", "HEAD").decode().strip()
    result["run_id"] = uuid.uuid4().hex
    write_json(work / "selected-tests.json", result)
    return result


def selected_cases(work, group=None):
    selection = read_json(work / "selected-tests.json")
    if selection["config_sha256"] != config_hash():
        raise ValueError("selection predates manifest changes; use a fresh run")
    if selection["revision"] != git("rev-parse", "HEAD").decode().strip():
        raise ValueError("selection belongs to another source revision")
    known = {c["id"] for c in catalog()}
    if not set(selection["selected"]).issubset(known):
        raise ValueError("selection contains unknown cases")
    return [c for c in catalog() if c["id"] in selection["selected"]
            and (not group or c["group"] == group)]
