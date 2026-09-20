Embedding RISC-V system QEMU
============================

This tree can build a Linux shared library instead of a system executable.
The option is off by default. The machine, TCG vCPU threads, device models,
firmware loading and QEMU main loop are reused. The implementation follows
Qualcomm/GreenSocs libqemu's embedding boundaries, but its public ABI is
specific to this tree and does not require QBox or SystemC.

Build
-----

Use separate build directories for normal and library configurations::

  mkdir build-lib
  cd build-lib
  ../configure --target-list=riscv64-softmmu --enable-libqemu \
      --enable-my-virtio --disable-docs --disable-sdl --disable-gtk \
      --disable-opengl --disable-vnc --disable-plugins --disable-kvm \
      --disable-rust --disable-tools --disable-guest-agent --disable-modules
  ninja libqemu-system-riscv64.so libqemu/libqemu-run libqemu/libqemu-lifecycle

``--enable-libqemu`` enables PIC, including the vendored MyVirtio archives.
``--disable-libqemu`` (the default) retains the regular executable build.
Library mode currently supports Linux, riscv64-softmmu and TCG without a GUI,
plugins or dynamically loaded QEMU device modules. Use a fresh directory when
changing mode; the vendored Make archives do not track flag changes.

``meson install`` installs the library, ``include/libqemu/libqemu.h`` and the
selected firmware blobs. Provide ``-L`` explicitly when running from a build
tree so firmware lookup does not depend on the embedding executable's path.

Host interface
--------------

The standalone public header is ``libqemu/libqemu.h``. Load the library with
``dlopen(RTLD_NOW | RTLD_LOCAL)``, resolve ``libqemu_get_api``, and pass
``LIBQEMU_ABI_VERSION`` and ``sizeof(LibQemuExports)``. An incompatible request
returns NULL. The returned function table is immutable and resident.

The complete-machine interface is::

  api->start(qemu_argc, qemu_argv, &error);
  /* Another host thread can call api->request_shutdown(&error). */
  api->wait(&guest_status, &error);
  /* The host continues here. */

``start`` copies argv (including argv[0]) and waits until the QEMU worker has
finished initialization. Normal QEMU arguments select the machine and its
devices. ``-S`` still pauses the guest; a QMP monitor can resume it.
``request_shutdown`` asks the main loop to exit even with ``-no-shutdown``.
``wait`` joins the main worker and returns the guest exit status; only one
waiter is allowed. API errors return -1 and optionally allocate an error
string which must be released using ``error_free``. Pass a NULL-initialized
error pointer. Calls are not signal-handler-safe.

For example::

  build-lib/libqemu/libqemu-run build-lib/libqemu-system-riscv64.so \
      -M xiangshan-kunminghu,generated-dtb=on,my-virtio-blk=on,my-virtio-devices=blk,my-virtio-blk-image=/path/to/disk.ext4 \
      -accel tcg -smp 4 -m 16G -display none -serial stdio -monitor none \
      -L pc-bios -kernel /path/to/Image \
      -append 'console=ttyS0,115200 earlycon=sbi rdinit=/bin/sh'

The example prints ``HOST_CONTINUES status=...`` after joining QEMU. Its
optional ``--control-fd FD`` consumes one byte to request shutdown and another
byte (or EOF) to let the host exit after inspection. This descriptor must be
inherited by the process. The example also runs a host GLib default loop;
QEMU dispatches its main-loop and chardev sources on its own context.

Lifetime and limits
-------------------

One runtime may be initialized once per process. Do not call ``dlclose`` or
try to reinitialize it. Normal guest shutdown and host shutdown return to the
caller. This does not turn every QEMU ``exit``, fatal initialization error,
assertion or abort into a recoverable error; help/list commands may also exit.
``-daemonize``, ``-run-with`` and ``-preconfig`` are not supported by ``start``.

Shutdown stops vCPUs, cleans up QEMU backends and invokes exit notifiers once
on the QEMU worker. MyVirtio block BHs and backend file descriptors are closed.
QOM/RAM and some stopped runtime/RCU threads remain resident until process
exit. The library does not install the standalone SIGINT/SIGHUP/SIGTERM
handlers. Existing SIGPIPE/SIGBUS, terminal and other process-wide QEMU
behavior is not completely isolated; the host should use explicit chardevs
and coordinate any signal handling it requires.

Validation
----------

Run the lifecycle test and the integration script::

  build-lib/libqemu/libqemu-lifecycle build-lib/libqemu-system-riscv64.so
  python3 tests/libqemu/check.py --build build-lib --output /tmp/libqemu-test \
      --kernel /path/to/Image --disk /path/to/openEuler-rootfs.ext4

The integration script first boots a small guest on ``virt`` and verifies
exit codes 0 and 7. It then boots four Kunminghu harts, checks vda at slot 0,
mounts ext4, writes 256 KiB with fsync, unmounts, mounts again, verifies SHA256,
and unmounts. After library shutdown it checks that the still-running host
has closed the disk descriptor, then runs host ``e2fsck -fn``. The source disk
is copied to the result directory, which must be a new directory. Logs and
the exact invocation are saved there.

The Linux test expects the workspace kernel with its built-in minimal
initramfs and an openEuler RISC-V disk with coreutils, glibc, libcrypto and
zlib. It copies these tools into guest RAM before unmounting the disk.
Kunminghu's existing TCG ``-kernel`` path does not load an external ``-initrd``.
Add ``--normal`` and point at a normal build to run the same disk checks
against the executable; that test uses SIGTERM after the final unmount.
