.. SPDX-License-Identifier: GPL-2.0-or-later

BOSC Xiangshan Kunminghu split-QEMU platform (``xiangshan-kunminghu-cosim``)
================================================================================

``xiangshan-kunminghu-cosim`` separates CPU execution from device emulation
between two QEMU processes.  The frontend uses TCG to execute the Kunminghu
CPU and exposes proxy MMIO regions to the guest.  The backend uses the
``devproxy`` accelerator and contains the PCIe host, NVMe controller, and
DesignWare AXI DMAC device models.

The two processes exchange PCIe configuration and MMIO accesses, DMA
transfers, INTx levels, MSI/MSI-X writes, and the platform DMAC interrupt over
Unix sockets and a shared DMA bounce file.  This machine does not model a
RISC-V IOMMU and does not add RISC-V trace support.

Build configuration
-------------------

The machine and the ``devproxy`` accelerator are disabled by default.  Build
a RISC-V system emulator with them enabled as follows:

.. code-block:: bash

   $ mkdir -p build-devproxy
   $ cd build-devproxy
   $ ../configure --target-list=riscv64-softmmu \
       --enable-devproxy --disable-rust --disable-docs --disable-werror
   $ ninja qemu-system-riscv64
   $ cd ..

The resulting binary lists both accelerators and the co-simulation machine:

.. code-block:: bash

   $ ./build-devproxy/qemu-system-riscv64 -accel help
   $ ./build-devproxy/qemu-system-riscv64 -machine help | \
       grep xiangshan-kunminghu-cosim

IPC paths
---------

The commands below use a private subdirectory of ``XDG_RUNTIME_DIR``.  This
keeps sockets from different users separate and avoids global files under
``/tmp``.  Prepare it before starting either QEMU process:

.. code-block:: bash

   $ if [ -n "${XDG_RUNTIME_DIR:-}" ]; then \
       export DEVPROXY_RUNTIME="$XDG_RUNTIME_DIR/qemu-devproxy"; \
     else \
       export DEVPROXY_RUNTIME="${XDG_CACHE_HOME:-$HOME/.cache}/qemu-devproxy/runtime"; \
     fi
   $ install -d -m 700 "$DEVPROXY_RUNTIME"
   $ rm -f "$DEVPROXY_RUNTIME/mmio.sock" \
       "$DEVPROXY_RUNTIME/dma.sock" \
       "$DEVPROXY_RUNTIME/irq.sock" \
       "$DEVPROXY_RUNTIME/dmac-irq.sock" \
       "$DEVPROXY_RUNTIME/msi.sock" \
       "$DEVPROXY_RUNTIME/dma-bounce"

The channels have the following roles:

``mmio.sock``
   Frontend PCIe ECAM, PCIe MMIO, and platform DMAC register accesses sent to
   the backend.

``dma.sock`` and ``dma-bounce``
   Backend PCIe or platform-device DMA requests sent to frontend guest memory.
   The regular file ``dma-bounce`` contains the shared transfer buffer.

``irq.sock``
   PCI INTx notifications sent from the backend to the frontend APLIC.

``dmac-irq.sock``
   DesignWare AXI DMAC interrupt notifications sent to the frontend APLIC.

``msi.sock``
   Backend MSI/MSI-X doorbell writes sent to the frontend IMSIC window.

Start the device backend
------------------------

Start the backend first.  Replace ``/path/to/disk.img`` with an absolute path
to a raw NVMe image:

.. code-block:: bash

   $ ./build-devproxy/qemu-system-riscv64 \
       -accel "devproxy,socket=$DEVPROXY_RUNTIME/mmio.sock,dma-socket=$DEVPROXY_RUNTIME/dma.sock,dma-mode=shared-bounce,dma-shm=$DEVPROXY_RUNTIME/dma-bounce" \
       -machine "xiangshan-kunminghu-cosim,devproxy-mode=backend,devproxy-mmio-socket=$DEVPROXY_RUNTIME/mmio.sock,devproxy-dma-socket=$DEVPROXY_RUNTIME/dma.sock,devproxy-dma-shm=$DEVPROXY_RUNTIME/dma-bounce,devproxy-irq-socket=$DEVPROXY_RUNTIME/irq.sock,devproxy-dmac-irq-socket=$DEVPROXY_RUNTIME/dmac-irq.sock,devproxy-msi-socket=$DEVPROXY_RUNTIME/msi.sock,devproxy-dmac-requester-id=0x8" \
       -drive file=/path/to/disk.img,if=none,id=nvme0,format=raw,snapshot=on,file.locking=off \
       -device nvme,serial=devproxy-nvme0,drive=nvme0,bus=pcie.0,addr=2.0 \
       -display none -serial mon:stdio \
       >"$DEVPROXY_RUNTIME/backend.log" 2>&1 &
   $ export DEVPROXY_BACKEND_PID=$!

Wait until the backend MMIO listener exists before starting the frontend:

.. code-block:: bash

   $ while [ ! -S "$DEVPROXY_RUNTIME/mmio.sock" ]; do sleep 0.1; done

Start the TCG frontend
----------------------
.. code-block:: bash

   $ ./build-devproxy/qemu-system-riscv64 \
       -nographic -accel tcg \
       -machine "xiangshan-kunminghu-cosim,devproxy-mode=frontend,devproxy-mmio-socket=$DEVPROXY_RUNTIME/mmio.sock,devproxy-dma-socket=$DEVPROXY_RUNTIME/dma.sock,devproxy-dma-shm=$DEVPROXY_RUNTIME/dma-bounce,devproxy-irq-socket=$DEVPROXY_RUNTIME/irq.sock,devproxy-dmac-irq-socket=$DEVPROXY_RUNTIME/dmac-irq.sock,devproxy-msi-socket=$DEVPROXY_RUNTIME/msi.sock,devproxy-dmac-requester-id=0x8" \
       -smp 1 -m 8G \
       -bios /path/to/fw_payload-noiommu.bin

The same machine properties and paths must be used by both processes.

When these machine properties are omitted, QEMU derives their defaults from
GLib's per-user runtime directory and uses names such as
``qemu-devproxy-mmio.sock`` and ``qemu-devproxy-dma.sock``.  The backend
accelerator's MMIO ``socket=`` option remains mandatory, so the explicit
paths above are recommended for a complete raw invocation.

NVMe validation
---------------

After Linux reaches a shell, verify that the backend NVMe namespace supports
filesystem reads, writes, flushes, and unmount:

.. code-block:: shell

   # mkdir -p /mnt
   # mount /dev/nvme0n1 /mnt
   # echo devproxy-nvme-ok > /mnt/devproxy-smoke
   # sync
   # cat /mnt/devproxy-smoke
   devproxy-nvme-ok
   # rm /mnt/devproxy-smoke
   # sync
   # umount /mnt

The platform DMAC register proxy can be checked by reading its component ID:

.. code-block:: shell

   # devmem 0x30040000 32
   0x44574158

Shutdown and cleanup
--------------------

After the frontend exits, stop the backend and remove its IPC objects:

.. code-block:: bash

   $ kill "$DEVPROXY_BACKEND_PID"
   $ wait "$DEVPROXY_BACKEND_PID" || true
   $ rm -f "$DEVPROXY_RUNTIME/mmio.sock" \
       "$DEVPROXY_RUNTIME/dma.sock" \
       "$DEVPROXY_RUNTIME/irq.sock" \
       "$DEVPROXY_RUNTIME/dmac-irq.sock" \
       "$DEVPROXY_RUNTIME/msi.sock" \
       "$DEVPROXY_RUNTIME/dma-bounce"

Machine properties
------------------

``devproxy-mode=frontend|backend``
   Selects the TCG frontend or device backend role.  The default is
   ``frontend``.

``devproxy-mmio-socket=<path>``
   MMIO and PCIe configuration request channel.

``devproxy-dma-socket=<path>``
   DMA control channel.

``devproxy-dma-shm=<path>``
   Shared DMA bounce file or POSIX shared memory name.

``devproxy-irq-socket=<path>``
   PCI INTx channel.

``devproxy-dmac-irq-socket=<path>``
   Platform DMAC interrupt channel.

``devproxy-msi-socket=<path>``
   PCI MSI/MSI-X channel.

``devproxy-dmac-requester-id=<id>``
   The 16-bit requester ID attached to backend platform DMAC transactions.
   The default is ``0x8``.
