#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
build_dir=${BUILD_DIR:-"$repo_dir/build-lib"}
if [[ -z ${BUILD_DIR:-} && ! -x $build_dir/libqemu/libqemu-kunminghu &&
      -x /tmp/qemu-xs-compose-build/libqemu/libqemu-kunminghu ]]; then
    build_dir=/tmp/qemu-xs-compose-build
fi
image_dir=${IMAGE_DIR:-"$repo_dir/../test-artifacts/virtio-blk-slot0-three-image-20260908"}
firmware=${FIRMWARE:-"$repo_dir/pc-bios/opensbi-riscv64-xiangshan-kmh-fw_dynamic.bin"}
kernel=${KERNEL:-"$image_dir/Image"}
disk=${DISK:-"$image_dir/openEuler-minimum-rootfs.ext4"}
library=${LIBQEMU_LIBRARY:-"$build_dir/libqemu-system-riscv64.so"}
launcher=${LIBQEMU_LAUNCHER:-"$build_dir/libqemu/libqemu-kunminghu"}
run_dir=${RUN_DIR:-}

usage() {
    cat <<EOF
Usage: $0 [--help]

Starts the host-assembled Kunminghu example with my-virtio-blk and DWC/NVMe.
The Linux guest opens an initramfs shell on this terminal.

Environment overrides:
  BUILD_DIR   libqemu build directory (default: $build_dir)
  IMAGE_DIR   directory containing Image and the rootfs (default: $image_dir)
  KERNEL      Linux Image path
  DISK        source raw ext4 image
  RUN_DIR     parent directory for a unique run subdirectory
  KEEP_RUN_DIR=1  retain writable disk copies and assembled.dtb after exit
  BOOTARGS    override the kernel command line

To stop cleanly from another terminal while this script is running:
  printf qx > <the control path printed by the script>
EOF
}

if [[ ${1:-} == --help || ${1:-} == -h ]]; then
    usage
    exit 0
fi
if (($#)); then
    echo "unknown argument: $1" >&2
    usage >&2
    exit 2
fi

for required in "$launcher" "$library" "$firmware" "$kernel" "$disk"; do
    if [[ ! -f $required || ! -r $required ]]; then
        echo "missing required file: $required" >&2
        echo "Set BUILD_DIR, KERNEL, DISK or the corresponding environment variable." >&2
        exit 1
    fi
done

if [[ -n $run_dir ]]; then
    mkdir -p "$run_dir"
    run_dir=$(mktemp -d "$run_dir/kmh.XXXXXX")
else
    run_dir=$(mktemp -d /tmp/qemu-xs-lib-run.XXXXXX)
fi

cleanup() {
    if [[ -n ${child_pid:-} ]] && kill -0 "$child_pid" 2>/dev/null; then
        printf qx >&3 2>/dev/null || true
        wait "$child_pid" 2>/dev/null || true
    fi
    exec 3>&-
    if [[ ${KEEP_RUN_DIR:-0} != 1 ]]; then
        # Delete only files created by this run; never a caller-owned directory.
        rm -f -- "$run_dir/blk.ext4" "$run_dir/nvme.ext4" \
            "$run_dir/control" "$run_dir/assembled.dtb"
        rmdir -- "$run_dir" 2>/dev/null || true
    fi
}
stop_child() {
    # q requests shutdown; x also releases a launcher using --hold-after-wait.
    if [[ -n ${child_pid:-} ]] && kill -0 "$child_pid" 2>/dev/null; then
        printf qx >&3 2>/dev/null || true
    fi
}
trap stop_child INT TERM
trap cleanup EXIT

cp --reflink=auto --sparse=always "$disk" "$run_dir/blk.ext4"
cp --reflink=auto --sparse=always "$disk" "$run_dir/nvme.ext4"
mkfifo "$run_dir/control"
exec 3<>"$run_dir/control"

cat >&2 <<EOF
Starting host-assembled Kunminghu.
  my-virtio-blk: /dev/vda
  DWC PCIe + NVMe: /dev/nvme0n1
  writable run directory: $run_dir
  generated DTB: $run_dir/assembled.dtb
  clean shutdown: printf qx > $run_dir/control
EOF

extra_args=()
if [[ -n ${BOOTARGS:-} ]]; then
    extra_args+=(--append "$BOOTARGS")
fi
"$launcher" \
    "$library" \
    "$firmware" \
    "$kernel" \
    "$run_dir/blk.ext4" \
    "$run_dir/nvme.ext4" \
    --control-fd 3 --dump-dtb "$run_dir/assembled.dtb" \
    "${extra_args[@]}" <&0 &
child_pid=$!
set +e
while true; do
    wait "$child_pid"
    status=$?
    # A trapped signal interrupts wait before the child actually exits.
    if ! kill -0 "$child_pid" 2>/dev/null; then
        break
    fi
done
child_pid=
set -e
exit "$status"
