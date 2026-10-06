#!/usr/bin/env python3
"""Boot the host-assembled Kunminghu example and verify both storage paths.

Uses a Linux Image with built-in initramfs (/bin/sh), DW PCIe/NVMe/virtio/ext4
and an openEuler rootfs with coreutils. Input disk is copied twice, never edited.
See libqemu/examples/README.kunminghu.md for build and run commands.
"""
import argparse
import json
import os
from pathlib import Path
import re
import selectors
import signal
import subprocess
import time


SOURCE = Path(__file__).resolve().parents[2]


def test_linux(args):
    disk = args.output / 'disk.ext4'
    nvme = args.output / 'nvme.ext4'
    for destination in (disk, nvme):
        subprocess.run(['cp', '--reflink=auto', '--sparse=always', str(args.disk),
                        str(destination)], check=True)
    read_fd, write_fd = os.pipe()
    os.set_inheritable(read_fd, True)
    cmd = [str(args.build / 'libqemu/libqemu-kunminghu'),
           str(args.build / 'libqemu-system-riscv64.so'),
           str(SOURCE / 'pc-bios/opensbi-riscv64-xiangshan-kmh-fw_dynamic.bin'),
           str(args.kernel), str(disk), str(nvme), '--control-fd', str(read_fd),
           '--hold-after-wait',
           '--dump-dtb', str(args.output / 'assembled.dtb')]
    if args.normal:
        cmd = [str(args.build / 'qemu-system-riscv64'), '-M',
               f'xiangshan-kunminghu,generated-dtb=on,dw-pcie=on,'
               f'my-virtio-blk=on,my-virtio-devices=blk,my-virtio-blk-image={disk}',
               '-smp', '4', '-m', '16G', '-accel', 'tcg',
               '-display', 'none', '-serial', 'stdio', '-monitor', 'none',
               '-L', str(SOURCE / 'pc-bios'), '-kernel', str(args.kernel),
               '-append', 'console=ttyS0,115200 earlycon=sbi loglevel=8 rdinit=/bin/sh',
               '-drive', f'if=none,id=nvmedisk,format=raw,file={nvme}',
               '-device', 'nvme,drive=nvmedisk,serial=LIBQEMU-KMH-NVME,bus=dw-pcie']
    (args.output / 'command.json').write_text(json.dumps(cmd, indent=2))
    with (args.output / 'linux.log').open('wb') as log:
        child = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT,
                                 pass_fds=(read_fd,))
        selector = selectors.DefaultSelector()
        selector.register(child.stdout, selectors.EVENT_READ)
        captured = b''
        consumed = 0

        def expect(pattern, timeout=180):
            nonlocal captured, consumed
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                match = re.search(pattern, captured[consumed:])
                if match:
                    consumed += match.end()
                    return match
                for key, _ in selector.select(timeout=1):
                    chunk = os.read(key.fd, 65536)
                    if not chunk:
                        raise RuntimeError(f'guest EOF waiting for {pattern!r}')
                    log.write(chunk)
                    log.flush()
                    captured += chunk
            raise TimeoutError(f'waiting for {pattern!r}: {captured[-1000:]!r}')

        def command(text, tag):
            child.stdin.write((text + f'; task_rc=$?; echo CHECK_{tag}:$task_rc\n').encode())
            child.stdin.flush()
            result = expect(rb'[\r\n](?:# )?CHECK_' + tag.encode() +
                            rb':(\d+)\r?\n')
            assert result.group(1) == b'0', (tag, captured[-2000:])
            expect(rb'# ')

        try:
            expect(rb'# ')
            command('mount -t proc proc /proc; mount -t sysfs sysfs /sys; '
                    'mount -t devtmpfs devtmpfs /dev', 'MOUNTS')
            command('test -b /dev/vda && ls -l /sys/block/vda | grep 310a0000', 'VDA')
            command('test -b /dev/nvme0n1 && '
                    'ls -l /sys/block/nvme0n1 | grep nvme && '
                    'cat /sys/class/nvme/nvme0/serial | grep LIBQEMU-KMH-NVME && '
                    'ls -l /sys/block/nvme0n1 | grep 32000000.pcie', 'NVME')
            command('cat /proc/interrupts', 'IRQS_BEFORE')
            command('mkdir -p /mnt/vda; mount -t ext4 /dev/vda /mnt/vda', 'MOUNT')
            # The kernel's initramfs omits coreutils. Copy the tools and their
            # libraries into RAM so vda can be unmounted during the test.
            command('mkdir -p /tmp/blk-tools && '
                    'cp /mnt/vda/usr/lib/ld-linux-riscv64-lp64d.so.1 '
                    '/tmp/blk-tools/ld.so && '
                    'cp /mnt/vda/usr/lib64/libc.so.6 '
                    '/mnt/vda/usr/lib64/libcrypto.so.3 '
                    '/mnt/vda/usr/lib64/libz.so.1 /tmp/blk-tools/ && '
                    'cp /mnt/vda/usr/bin/dd /mnt/vda/usr/bin/sha256sum '
                    '/mnt/vda/usr/bin/sync /tmp/blk-tools/ && '
                    'export LD_LIBRARY_PATH=/tmp/blk-tools', 'TOOLS')
            command('umount /mnt/vda', 'TOOLS_UNMOUNT')
            for name in ('vda', 'nvme0n1'):
                command(f'mkdir -p /mnt/{name} && '
                        f'mount -t ext4 /dev/{name} /mnt/{name}', f'{name}_MOUNT')
                loader = '/tmp/blk-tools/ld.so /tmp/blk-tools/'
                command(f'{loader}dd if=/dev/urandom '
                        f'of=/mnt/{name}/libqemu-test.bin bs=4096 count=64 conv=fsync',
                        f'{name}_WRITE')
                command(f'{loader}sha256sum /mnt/{name}/libqemu-test.bin '
                        f'> /tmp/{name}.sha256 && {loader}sync && umount /mnt/{name}',
                        f'{name}_UNMOUNT')
                command(f'mount -t ext4 /dev/{name} /mnt/{name} && '
                        f'{loader}sha256sum -c /tmp/{name}.sha256', f'{name}_READBACK')
                command(f'umount /mnt/{name}', f'{name}_FINAL_UNMOUNT')
            command('cat /proc/interrupts', 'IRQS_AFTER')
            if args.normal:
                child.send_signal(signal.SIGTERM)
            else:
                os.write(write_fd, b'q')
                expect(rb'HOST_CONTINUES status=0', timeout=60)
                for fd in Path(f'/proc/{child.pid}/fd').iterdir():
                    try:
                        assert not any(os.path.samefile(fd, img) for img in (disk, nvme)), \
                            'disk fd leaked'
                    except FileNotFoundError:
                        pass
                os.write(write_fd, b'x')
            assert child.wait(timeout=60) == 0, captured[-2000:]
        finally:
            if child.poll() is None:
                child.kill()
                child.wait()
            selector.close()
            os.close(read_fd)
            os.close(write_fd)
    content = (args.output / 'linux.log').read_text()
    for bad in ['Kernel panic', 'I/O error, dev vda', 'Buffer I/O error',
                'LIBQEMU_BLK_FAIL', 'I/O error, dev nvme', 'failed to set APST',
                'timeout, aborting', 'Device not ready']:
        assert bad not in content, bad
    if not args.normal:
        assert 'HOST_ASSEMBLED Kunminghu:' in content
    # Linux shows the leaf IRQ domain (PCI-MSIX), not its IMSIC parent.
    for pattern in (r'PCI-MSIX-.*nvme0q1', r'APLIC-MSI-.*virtio0'):
        counters = []
        for line in content.splitlines():
            if re.search(pattern, line):
                fields = line.split(':', 1)[1].split()
                counters.append(sum(int(v) for v in fields if v.isdecimal()))
        assert len(counters) == 2 and counters[1] > counters[0], (pattern, counters)
    for img in (disk, nvme):
        result = subprocess.run(['e2fsck', '-fn', str(img)], capture_output=True,
                                text=True)
        (args.output / f'{img.stem}-fsck.log').write_text(result.stdout + result.stderr)
        assert result.returncode == 0, result
    print(('NORMAL' if args.normal else 'ASSEMBLED') +
          '_KUNMINGHU_LINUX_BLK_NVME_PASS', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--disk', type=Path, required=True)
    parser.add_argument('--normal', action='store_true')
    args = parser.parse_args()
    args.build = args.build.resolve()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    test_linux(args)


if __name__ == '__main__':
    main()
