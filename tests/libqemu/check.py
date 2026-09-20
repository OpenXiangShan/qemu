#!/usr/bin/env python3
"""Integration tests. Disk images are always copied before boot.

python3 tests/libqemu/check.py --build /tmp/build --output /tmp/results \
    --kernel /path/to/Image --disk /path/to/rootfs.ext4 [--normal]
"""
import argparse
import json
import re
import os
from pathlib import Path
import signal
import struct
import subprocess
import time
import selectors


SOURCE = Path(__file__).resolve().parents[2]


def finisher(path, code):
    value = (code << 16) | 0x5555
    # lui t0,0x100; lui t1,HI(value); addi t1,t1,LO(value); sw; loop
    hi = (value + 0x800) >> 12
    lo = value & 0xfff
    words = [0x001002b7, (hi << 12) | 0x337,
             (lo << 20) | 0x30313, 0x0062a023, 0x0000006f]
    path.write_bytes(struct.pack('<5I', *words))


def prefix(args):
    if args.normal:
        return [str(args.build / 'qemu-system-riscv64')]
    return [str(args.build / 'libqemu/libqemu-run'),
            str(args.build / 'libqemu-system-riscv64.so')]


def test_exit(args):
    for code in [0, 7]:
        guest = args.output / f'finisher-{code}.bin'
        finisher(guest, code)
        cmd = prefix(args) + ['-M', 'virt', '-m', '128M', '-bios', 'none',
                             '-display', 'none', '-serial', 'none',
                             '-monitor', 'none', '-accel', 'tcg',
                             '-device', f'loader,file={guest},addr=0x80000000,cpu-num=0']
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=45)
        (args.output / f'exit-{code}.log').write_text(result.stdout + result.stderr)
        assert result.returncode == code, result
        if not args.normal:
            assert f'HOST_CONTINUES status={code}' in result.stdout, result
    print('GUEST_EXIT_PASS', flush=True)


def test_linux(args):
    disk = args.output / 'disk.ext4'
    subprocess.run(['cp', '--reflink=auto', '--sparse=always', str(args.disk),
                    str(disk)], check=True)
    cmd = prefix(args)
    read_fd, write_fd = os.pipe()
    os.set_inheritable(read_fd, True)
    if not args.normal:
        cmd += ['--control-fd', str(read_fd)]
    cmd += ['-M', f'xiangshan-kunminghu,generated-dtb=on,my-virtio-blk=on,'
            f'my-virtio-devices=blk,my-virtio-blk-image={disk}',
            '-smp', '4', '-m', '16G', '-accel', 'tcg',
            '-display', 'none', '-serial', 'stdio', '-monitor', 'none',
            '-L', str(SOURCE / 'pc-bios'), '-kernel', str(args.kernel),
            '-append', 'console=ttyS0,115200 earlycon=sbi loglevel=8 rdinit=/bin/sh']
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
            command('/tmp/blk-tools/ld.so /tmp/blk-tools/dd if=/dev/urandom of=/mnt/vda/libqemu-test.bin bs=4096 '
                    'count=64 conv=fsync', 'WRITE')
            command('/tmp/blk-tools/ld.so /tmp/blk-tools/sha256sum /mnt/vda/libqemu-test.bin > /tmp/libqemu.sha256 '
                    '&& /tmp/blk-tools/ld.so /tmp/blk-tools/sync && umount /mnt/vda', 'UNMOUNT')
            command('mount -t ext4 /dev/vda /mnt/vda && '
                    '/tmp/blk-tools/ld.so /tmp/blk-tools/sha256sum -c /tmp/libqemu.sha256', 'READBACK')
            command('umount /mnt/vda', 'FINAL_UNMOUNT')
            if args.normal:
                child.send_signal(signal.SIGTERM)
            else:
                os.write(write_fd, b'q')
                expect(rb'HOST_CONTINUES status=0', timeout=60)
                for fd in Path(f'/proc/{child.pid}/fd').iterdir():
                    try:
                        assert not os.path.samefile(fd, disk), 'disk fd leaked'
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
                'LIBQEMU_BLK_FAIL']:
        assert bad not in content, bad
    result = subprocess.run(['e2fsck', '-fn', str(disk)], capture_output=True,
                            text=True)
    (args.output / 'fsck.log').write_text(result.stdout + result.stderr)
    assert result.returncode == 0, result
    print('LINUX_BLK_PASS', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--kernel', type=Path)
    parser.add_argument('--disk', type=Path)
    parser.add_argument('--normal', action='store_true')
    args = parser.parse_args()
    args.build = args.build.resolve()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    test_exit(args)
    if args.kernel and args.disk:
        test_linux(args)


if __name__ == '__main__':
    main()
