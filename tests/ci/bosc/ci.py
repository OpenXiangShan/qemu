#!/usr/bin/env python3
"""Local-first QEMU regression entrypoint. SPDX-License-Identifier: GPL-2.0-or-later."""
import argparse
import os
from pathlib import Path
import signal
import resource
import sys
import tempfile
import time
import traceback

from assets import prepare
from build import build, doctor, verify_bundle
from dependencies import prepare_dependencies
from live_log import CaseConsole, JobConsole
from common import HERE, SOURCE, catalog, locked, read_json, run_logged, utc_now, write_json
from report import aggregate
from selection import select, selected_cases


def interrupted(signum, _frame):
    raise KeyboardInterrupt(f"signal {signum}")


def pairs(values):
    result = {}
    for value in values:
        name, sep, path = value.partition("=")
        if not sep or not path or name in result:
            raise ValueError("--asset requires unique NAME=PATH values")
        result[name] = path
    return result


def worker(args, work):
    from runner import execute
    case = next(c for c in selected_cases(work) if c["id"] == args.case[0])
    directory = work / "results" / case["id"]
    start = time.monotonic()
    selection = read_json(work / "selected-tests.json")
    result = {"id": case["id"], "status": "FAIL", "started": utc_now(),
              "selection_sha256": selection["config_sha256"],
              "run_id": selection["run_id"], "checks": []}
    try:
        verify_bundle(work, case)
        paths = read_json(directory / "assets.json")
        evidence, checks = execute(work, case, paths, directory)
        result.update({"status": "PASS", "evidence": evidence, "checks": checks})
        if any(c["status"] in ("FAIL", "XPASS") for c in checks):
            result["status"] = "FAIL"
            result["message"] = "assertion failed or known defect unexpectedly passed"
    except BaseException as exc:
        result["message"] = f"{type(exc).__name__}: {exc}"
        (directory / "exception.log").write_text(traceback.format_exc())
    finally:
        result["seconds"] = round(time.monotonic() - start, 3)
        usage = resource.getrusage(resource.RUSAGE_CHILDREN)
        result["resources"] = {"max_child_rss_kib": usage.ru_maxrss,
                               "child_user_seconds": usage.ru_utime,
                               "child_system_seconds": usage.ru_stime}
        write_json(directory / "result.json", result)
    return result["status"] == "PASS"


def run_cases(args, work, cases):
    selection = read_json(work / "selected-tests.json")
    console = JobConsole.from_environment()
    for case in cases:
        directory = work / "results" / case["id"]
        if directory.exists():
            raise ValueError(f"existing case evidence: {directory}; use a fresh run")
        directory.mkdir(parents=True)
        print(f"RUN {case['id']} (limit {case['timeout']}s)", flush=True)
        interrupted_error = None
        with CaseConsole(directory, case["id"], console) as live:
            try:
                paths = prepare(work, [case], args.assets_dir, pairs(args.asset), args.cache_dir,
                                read_json(args.jf_paths) if args.jf_paths else None)
                write_json(directory / "assets.json", paths)
                # Read raw UART files in the parent so worker redirection cannot
                # hide live output. The worker and its deadline stay independent.
                run_logged([sys.executable, HERE / "ci.py", "_worker", "--work-dir", work,
                            "--case", case["id"]], directory / "worker.log",
                           timeout=case["timeout"] + 120, on_poll=live.poll)
            except BaseException as exc:
                result_path = directory / "result.json"
                if result_path.exists():
                    result = read_json(result_path)
                    if result["status"] != "FAIL":
                        result.update({"status": "FAIL", "message": f"worker lifecycle failed: {type(exc).__name__}: {exc}"})
                        write_json(result_path, result)
                else:
                    write_json(result_path, {"id": case["id"], "status": "FAIL",
                               "selection_sha256": selection["config_sha256"], "run_id": selection["run_id"],
                               "message": f"{type(exc).__name__}: {exc}", "checks": []})
                if isinstance(exc, KeyboardInterrupt):
                    interrupted_error = exc
            finally:
                if case.get("kind") == "iommu-linux":
                    from runner import cleanup_nvme
                    cleanup_nvme(work, case, directory)
        result = read_json(directory / "result.json")
        print(f"{result['status']} {case['id']} {result.get('message', '')}", flush=True)
        if result["status"] != "PASS":
            live.diagnostics(result.get("message", "case failed"))
        if interrupted_error is not None:
            raise interrupted_error


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("command", choices=("doctor", "select", "assets", "build", "run", "report", "all", "_worker"))
    p.add_argument("--work-dir", type=Path, help="unique run directory; required except for all")
    p.add_argument("--assets-dir", type=Path, default=os.environ.get("BOSC_CI_ASSETS_DIR"), help="offline root matching images.lock paths")
    p.add_argument("--asset", action="append", default=[], metavar="NAME=PATH", help="local input override, same locked hash required")
    p.add_argument("--cache-dir", type=Path, default=os.environ.get("BOSC_CI_CACHE_DIR"), help="optional shared SHA256-addressed download cache")
    p.add_argument("--dependencies-dir", type=Path, default=os.environ.get("BOSC_CI_DEPENDENCIES_DIR"), help="optional offline root for locked native-deps archives")
    p.add_argument("--jf-paths", type=Path, default=os.environ.get("BOSC_CI_JF_PATHS"), help="deployment JSON: asset ID to actual repo-relative JF path (hashes unchanged)")
    p.add_argument("--base")
    p.add_argument("--full", action="store_true", help="include extended cases regardless of changed paths")
    p.add_argument("--head", default="HEAD")
    p.add_argument("--case", action="append", help="select explicit local case(s), or filter already selected run cases")
    p.add_argument("--group", choices=sorted({c["group"] for c in catalog()}))
    p.add_argument("--jobs", type=int, default=8)
    return p


def main():
    args = parser().parse_args()
    if not 1 <= args.jobs <= 64:
        raise ValueError("--jobs must be between 1 and 64")
    if args.group and args.command in ("all", "select"):
        raise ValueError("use --case to narrow a new selection; --group is for staged execution")
    if args.work_dir is None:
        if args.command != "all":
            raise ValueError("--work-dir is required for staged commands")
        parent = SOURCE / "build" / "ci"
        parent.mkdir(parents=True, exist_ok=True)
        args.work_dir = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    if any(x in str(work) for x in (",", "\n", " ")):
        raise ValueError("work-dir must not contain commas, newlines or spaces (QEMU/qtest CLI)")
    print(f"WORK_DIR={work}", flush=True)
    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGINT, interrupted)
    if args.command == "_worker":
        return 0 if worker(args, work) else 1
    # Local all and staged commands cannot mutate the same run concurrently.
    # CI jobs have independent checkouts and merge only immutable evidence.
    with locked(work / ".lock"):
        if args.command in ("all", "select") and (work / "selected-tests.json").exists():
            print("selection already exists; refusing to overwrite previous evidence", file=sys.stderr)
            return 2
        if args.command == "run":
            existing = [c["id"] for c in selected_cases(work, args.group)
                        if (not args.case or c["id"] in args.case)
                        and (work / "results" / c["id"]).exists()]
            if existing:
                print(f"case evidence already exists: {existing}; use a fresh run", file=sys.stderr)
                return 2
        try:
            if args.command in ("all", "select"):
                result = select(work, args.base, args.head, args.case, args.full)
                print("SELECTED=" + ",".join(result["selected"]), flush=True)
            if args.command == "doctor" and not (work / "selected-tests.json").exists():
                cases = catalog()
            else:
                cases = selected_cases(work, args.group)
            if args.command == "run" and args.case:
                selected = {c["id"] for c in cases}
                if not set(args.case).issubset(selected):
                    raise ValueError("--case is not in this run's selection")
                cases = [c for c in cases if c["id"] in args.case]
            if args.command in ("all", "doctor"):
                doctor(work, cases)
            if args.command == "all":
                run_logged([sys.executable, "-m", "unittest", "discover", "-s", HERE,
                            "-p", "test_ci.py", "-v"], work / "selftest.log", timeout=120)
                run_logged([sys.executable, HERE / "lint_ci.py"], work / "lint.log", timeout=30)
            if args.command in ("all", "assets"):
                prepare(work, cases, args.assets_dir, pairs(args.asset), args.cache_dir,
                        read_json(args.jf_paths) if args.jf_paths else None)
            if args.command in ("all", "build"):
                prepare_dependencies(work, cases, args.dependencies_dir, args.cache_dir)
                build(work, cases, args.jobs)
            if args.command in ("all", "run"):
                run_cases(args, work, cases)
            if args.command in ("all", "run", "report"):
                result = aggregate(work, args.group)
                print(f"SUMMARY {result['status']} {result['counts']}", flush=True)
                return 0 if result["status"] == "PASS" else 1
            return 0
        except BaseException as exc:
            # Do not serialize environment variables/credentials in errors.
            message = f"{type(exc).__name__}: {exc}"
            print(message, file=sys.stderr, flush=True)
            write_json(work / "stage-errors" / f"{args.command}-{args.group or 'all'}.json",
                       {"message": message, "group": args.group})
            if (work / "selected-tests.json").exists():
                aggregate(work, args.group)
            return 130 if isinstance(exc, KeyboardInterrupt) else 1


if __name__ == "__main__":
    sys.exit(main())
