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
  ninja
  ./pyvenv/bin/meson test --print-errorlogs

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

Assembling a machine from the host
---------------------------------

The core API is available as ``api->core`` in the same public header. It is a
RISC-V-oriented subset, not Qualcomm's complete LibQemuExports/QBox ABI.
The original function-table prefix remains compatible with first-stage hosts.
The core table exposes its own size for extension checks.

Use ``init`` instead of ``start`` to stop initialization after the board and
CLI devices are created, but before machine creation done, the initial reset,
and autostart::

  api->init(argc, argv, &error); /* e.g. -M none -m 0, no -cpu */
  /* Create QOM devices; configure properties, RAM/MMIO and IRQ wiring. */
  api->finish_qemu_init(&error);
  api->wait(&guest_status, &error);

``start`` still performs the original complete-machine sequence without an
assembly window. ``finish_qemu_init`` uses the same finalization, ROM/reset,
VMState and post-init logic as normal QEMU. ``-S`` remains effective: call
``resume`` to start the finalized VM. ``pause`` and ``system_reset`` are also
available after finalization. ``get_state`` reports lifecycle state;
``LIBQEMU_RUNNING`` means initialization is complete, including when paused.
Cancellation with ``request_shutdown`` is supported during the assembly phase.
A second init/start or finish, topology changes outside assembly, and operations
after shutdown are rejected.

The following groups are implemented:

* QOM: object creation, references, paths, child ownership and typed string,
  bool, signed/unsigned integer and link properties.
* QDev/SysBus: child bus lookup, device realization, MMIO region lookup,
  GPIO input lookup/output wiring and SysBus IRQ wiring.
* Memory: containers, allocated RAM, caller-owned RAM, aliases, callback MMIO,
  subregion map/unmap and address-space creation/read/write.
* Execution: RISC-V CPU reset registration, GPIO level injection and virtual
  clock timers (nanosecond deadlines).

``object_new`` returns an owned reference. Adding a child or realizing a device
establishes QEMU ownership where applicable; drop the caller's reference during
assembly when it is no longer needed. Objects obtained by path/bus/link lookup
are borrowed. Property strings are allocated and released with ``error_free``.
Do not unref a borrowed handle without first acquiring a reference. Runtime
unplug and changing ``realized`` through generic properties are unsupported.

Memory, AddressSpace, IRQ and timer handles are borrowed from the resident
runtime. Their storage is retained until process exit; there is no destroy API.
``memory_del`` only unmaps a region, and ``timer_del`` cancels a timer. Provide
unique memory names and aligned, adequately sized host RAM; host RAM must remain
valid until ``wait`` returns. The same lifetime applies to callback functions
and their opaque data. No handle may be used after shutdown. QOM device types
and property names must match this QEMU build; existing machine-specific
helpers are not automatically valid under ``-M none``.

Host-thread operations are queued to the QEMU worker with BQL held. A bounded
batch prevents continuous host API traffic from starving shutdown and device
I/O. Pending requests are either completed or failed when the worker stops.
Host callbacks execute synchronously on a QEMU thread, including vCPU MMIO
paths; they must not wait for the host to submit another synchronous request.
Within callbacks, only ``memory_rw``, ``irq_set``, ``get_state`` and asynchronous
``request_shutdown`` are supported. Calling other synchronous APIs, including
``wait``, returns an error rather than waiting on the current thread. User
callbacks are disabled during shutdown before ``wait`` returns.

The example builds a minimal SoC entirely through this interface::

  ninja -C build-lib libqemu/libqemu-assemble libqemu/libqemu-concurrency
  build-lib/libqemu/libqemu-assemble build-lib/libqemu-system-riscv64.so
  build-lib/libqemu/libqemu-assemble build-lib/libqemu-system-riscv64.so running
  build-lib/libqemu/libqemu-assemble build-lib/libqemu-system-riscv64.so cancel
  build-lib/libqemu/libqemu-concurrency build-lib/libqemu-system-riscv64.so

It creates an RV64 CPU, RAM, callback registers, ACLINT software interrupts and
a SiFive finisher. A small embedded guest waits for a virtual timer, reads and
writes the host MMIO registers, verifies mip.MSIP, and exits. It also verifies
alias reads, a separate address space backed by host RAM, callback reentry
rejection, no guest execution before finish, and the stopped-state contract.
``running`` tests autostart; the default tests ``-S`` followed by resume.
``cancel`` tests shutdown before finalization. The concurrency test submits
requests from four host threads while requesting shutdown.

Use ``--split-init`` with ``tests/libqemu/check.py`` to run the full Kunminghu
Linux and block regression through ``init`` followed by ``finish_qemu_init``.
