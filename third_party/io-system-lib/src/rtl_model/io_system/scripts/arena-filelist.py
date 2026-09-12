#!/usr/bin/env python3
"""Print a relocatable arena vendor filelist using absolute paths at build time."""
import argparse
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--arena", type=Path, default=Path(__file__).resolve().parents[1] / "arena")
parser.add_argument("--packages-only", action="store_true")
parser.add_argument("--modules-only", action="store_true")
parser.add_argument("--verify", action="store_true", help="check all imported hashes before emitting paths")
args = parser.parse_args()
root = args.arena.resolve()
if args.packages_only and args.modules_only:
    parser.error("choose at most one subset")
if args.verify:
    import hashlib
    for entry in json.loads((root / "SOURCE_MANIFEST.json").read_text())["files"]:
        path = root / "vendor" / entry["source_path"]
        if hashlib.sha256(path.read_bytes()).hexdigest() != entry["imported_sha256"]:
            parser.error(f"import integrity mismatch: {path}")
sources = json.loads((root / "sources.json").read_text())
for directory in sources["include_dirs"]:
    print("+incdir+" + str(root / "vendor" / directory))
for define in ["PRINTF_COND=0", "ASSERT_VERBOSE_COND=0", "STOP_COND=0"]:
    print("+define+" + define)
for kind in ["packages", "modules"]:
    if (args.packages_only and kind != "packages") or (args.modules_only and kind != "modules"):
        continue
    for path in sources[kind]:
        print(root / "vendor" / path)
