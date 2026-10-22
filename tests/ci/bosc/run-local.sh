#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Local one-shot entrypoint for the same CI harness used by GitLab.
# Usage:
#   tests/ci/bosc/run-local.sh
#   tests/ci/bosc/run-local.sh --case kmh-iommu-linux --case bosc-iommu-linux-single
#   tests/ci/bosc/run-local.sh --assets-dir /srv/bosc-ci/assets \
#       --dependencies-dir /srv/bosc-ci/dependencies --jobs 8
#
# The default run is a complete regression in a new work directory. Set
# BOSC_CI_JF_TOKEN for locked assets not already present in BOSC_CI_CACHE_DIR.
# Use BOSC_CI_FULL=0 with --base REF --head REF for impact-based selection.
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source_root=$(cd -- "$script_dir/../../.." && pwd)

usage() {
    cat <<'EOF'
Usage: tests/ci/bosc/run-local.sh [options]

Run the QEMU BOSC/KMH CI harness in a new work directory. By default this is
a full local regression, including extended cases. The script never deletes an
existing work directory.

Options:
  --full                       Include extended cases (the default).
  --case ID                    Run one case; may be repeated.
  --jobs N                     QEMU build parallelism (default: BOSC_CI_JOBS or 8).
  --work-dir DIR               Use an explicit new work directory.
  --assets-dir DIR             Offline assets root matching images.lock.json paths.
  --dependencies-dir DIR       Offline native dependency archive root.
  --cache-dir DIR              SHA256-addressed download cache.
  --base REF --head REF        Use impact selection for a revision range.
  -h, --help                   Show this help.

Environment:
  BOSC_CI_CASES                 Comma-separated case IDs, equivalent to --case.
  BOSC_CI_JF_TOKEN              Read-only JF token; never put it in this script.
  BOSC_CI_JF_BASE_URL           Defaults to the local JF origin used by BOSC CI.
EOF
}

jobs=${BOSC_CI_JOBS:-8}
work_dir=${BOSC_CI_WORK:-}
assets_dir=${BOSC_CI_ASSETS_DIR:-}
dependencies_dir=${BOSC_CI_DEPENDENCIES_DIR:-}
cache_dir=${BOSC_CI_CACHE_DIR:-/nfs/home/guoyaxing/ci-bosc/cache/assets}
base_ref=${BOSC_CI_BASE:-}
head_ref=${BOSC_CI_HEAD:-HEAD}
full=${BOSC_CI_FULL:-1}
case_args=()

if [[ -n ${BOSC_CI_CASES:-} ]]; then
    IFS=',' read -r -a requested_cases <<< "$BOSC_CI_CASES"
    for case_id in "${requested_cases[@]}"; do
        [[ -n "$case_id" ]] || continue
        case_args+=(--case "$case_id")
    done
fi

while (($#)); do
    case "$1" in
        --full)
            full=1
            shift
            ;;
        --case)
            (($# >= 2)) || { echo "--case requires an ID" >&2; exit 2; }
            case_args+=(--case "$2")
            shift 2
            ;;
        --jobs)
            (($# >= 2)) || { echo "--jobs requires a value" >&2; exit 2; }
            jobs=$2
            shift 2
            ;;
        --work-dir)
            (($# >= 2)) || { echo "--work-dir requires a path" >&2; exit 2; }
            work_dir=$2
            shift 2
            ;;
        --assets-dir)
            (($# >= 2)) || { echo "--assets-dir requires a path" >&2; exit 2; }
            assets_dir=$2
            shift 2
            ;;
        --dependencies-dir)
            (($# >= 2)) || { echo "--dependencies-dir requires a path" >&2; exit 2; }
            dependencies_dir=$2
            shift 2
            ;;
        --cache-dir)
            (($# >= 2)) || { echo "--cache-dir requires a path" >&2; exit 2; }
            cache_dir=$2
            shift 2
            ;;
        --base)
            (($# >= 2)) || { echo "--base requires a revision" >&2; exit 2; }
            base_ref=$2
            shift 2
            ;;
        --head)
            (($# >= 2)) || { echo "--head requires a revision" >&2; exit 2; }
            head_ref=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

[[ "$full" == 0 || "$full" == 1 ]] || {
    echo "BOSC_CI_FULL must be 0 or 1" >&2
    exit 2
}
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || {
    echo "jobs must be a positive integer" >&2
    exit 2
}

if [[ -z "$work_dir" ]]; then
    work_parent=${BOSC_CI_WORK_ROOT:-/nfs/home/guoyaxing/my-workspace}
    work_dir="$work_parent/qemu-ci-local-$(date +%Y%m%d-%H%M%S)"
fi

if [[ -e "$work_dir/selected-tests.json" ]]; then
    echo "refusing to reuse an existing CI work directory: $work_dir" >&2
    echo "set BOSC_CI_WORK or use --work-dir with a new path" >&2
    exit 2
fi

# This is the known private JF origin for the local BOSC deployment. The token
# remains an environment input; an empty cache will fail closed without it.
export BOSC_CI_JF_BASE_URL=${BOSC_CI_JF_BASE_URL:-http://10.13.18.1:8082/artifactory}
export BOSC_CI_ALLOW_HTTP_ORIGIN=${BOSC_CI_ALLOW_HTTP_ORIGIN:-http://10.13.18.1:8082}

command=(python3 "$source_root/tests/ci/bosc/ci.py" all
         --work-dir "$work_dir" --jobs "$jobs" --cache-dir "$cache_dir")
if [[ "$full" == 1 ]]; then
    command+=(--full)
elif [[ -n "$base_ref" ]]; then
    command+=(--base "$base_ref" --head "$head_ref")
else
    echo "BOSC_CI_FULL=0 requires --base REF (or BOSC_CI_BASE)" >&2
    exit 2
fi
if [[ -n "$assets_dir" ]]; then
    command+=(--assets-dir "$assets_dir")
fi
if [[ -n "$dependencies_dir" ]]; then
    command+=(--dependencies-dir "$dependencies_dir")
fi
command+=("${case_args[@]}")

cd "$source_root"
echo "QEMU_SOURCE=$source_root"
echo "QEMU_CI_WORK=$work_dir"
echo "QEMU_CI_JOBS=$jobs"
exec "${command[@]}"
