#!/usr/bin/env bash
set -euo pipefail

src=${1:?source rootfs ext4 image}
dst=${2:?derived rootfs ext4 image}
wrapper=${3:?static PID1 wrapper}

mkdir -p "$(dirname "$dst")"
cp --reflink=auto --sparse=always "$src" "$dst"
chmod u+w "$dst"

# /sbin is a symlink to usr/sbin in the artifact.  The embedded switch_root
# helper may enter either /init or /sbin/init, so cover both entry points while
# preserving /bin/bash for the post-restore workload.
debugfs -w -f - "$dst" <<EOF
unlink /init
write $wrapper /init
set_inode_field /init mode 0100755
unlink /usr/sbin/init
write $wrapper /usr/sbin/init
set_inode_field /usr/sbin/init mode 0100755
EOF

debugfs -R 'stat /init' "$dst" | grep -q 'Type: regular'
debugfs -R 'stat /usr/sbin/init' "$dst" | grep -q 'Type: regular'
