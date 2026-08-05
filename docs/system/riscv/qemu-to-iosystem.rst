QEMU to io-system machine
=========================

``qemu_to_iosystem`` 是 XiangShan/Kunminghu RISC-V machine 的一个
io-system C-model bring-up 入口。CPU、DRAM、CLINT、UART 和 IMSIC 仍由
QEMU 创建；APLIC 和首版 ``my-virtio-blk`` 由 sibling ``io-system-lib``
静态库实现，并通过 Q2IO/IO2Q transaction-level AXI 接口和 QEMU 交互。

首版目标是 TCG 单核 Linux 闭环：

* OpenSBI ``fw_jump`` 进入 Linux；
* Linux 通过 APLIC MSI 发现 ``virtio,mmio`` block 设备；
* ``/dev/vda`` 对应 ``my-virtio-blk-image`` 指定的 raw ext4；
* 设备 DMA 通过 io-system host callbacks 访问 QEMU guest memory。

Build
-----

需要配置 QEMU 时启用 io-system：

.. code-block:: bash

   ../configure --target-list=riscv64-softmmu --enable-io-system --disable-docs
   ninja qemu-system-riscv64

``--enable-io-system`` 会调用 QEMU 源码树 sibling
``../io-system-lib/Makefile``，生成并链接静态库 ``libio_system.a`` 和
``libio_system_backend.a``。

Boot Linux with my-virtio-blk
-----------------------------

用外部 ``fw_jump`` 和 ``-device loader`` 启动时，必须使用不带内置 DTB 的
OpenSBI 固件，或确认固件最终使用 QEMU 传入的 DTB。若使用带内置 DTB 的
``fw_jump-kmh-v2-1core.bin``，OpenSBI 可能绕过 QEMU 生成的
``qemu_to_iosystem`` DTB，Linux 会看到旧的 ``bosc,kmh-v2-1core`` 设备树。

当前 workspace 中已验证的最小命令如下：

.. code-block:: bash

   ./qemu-system-riscv64 \
       -M qemu_to_iosystem,generated-dtb=on,my-virtio-blk=on,my-virtio-blk-image=/nfs/home/guoyaxing/my-workspace/st-release/openEuler-minimal-rootfs/openEuler-minimal-rootfs-24.03-SP3-RVA23.ext4 \
       -smp 1 -m 1G \
       -accel tcg,thread=single \
       -display none -serial stdio -monitor none \
       -bios /nfs/home/guoyaxing/my-workspace/st-release/fw_jump/fw_jump-kmh-v2-no-dtb.bin \
       -device loader,file=/nfs/home/guoyaxing/my-workspace/st-release/minimal/Image,addr=0x80400000

预期日志中应能看到：

.. code-block:: text

   Platform Name             : QEMU to io-system Xiangshan machine
   Machine model: QEMU to io-system Xiangshan machine
   riscv-aplic 31120000.aplic: 96 interrupts forwarded to MSI base 0x000000003b000000
   virtio_blk virtio0: [vda] ...
   Root device auto-detected: /dev/vda
   Overlay root mounted successfully.

Trace
-----

可通过 ``io-system-trace-file`` 记录 io-system C-model 的 AXI beat trace：

.. code-block:: bash

   -M qemu_to_iosystem,generated-dtb=on,my-virtio-blk=on,my-virtio-blk-image=/path/to/rootfs.ext4,io-system-trace-file=/tmp/qti.trace

trace 每行包含：

.. code-block:: text

   port=<q2io|io2q> channel=<read|write|memory-read|memory-write> transaction_id=<id> address=<addr> beat_index=<n> beat_size=<bytes> response=<OKAY|SLVERR|DECERR> data=<hex> strobe=<hex>

UART
----

UART0 仍由 QEMU 实现，设备树中不带 ``interrupts`` 属性。首版只要求轮询式
console 输出；Linux 可能打印 ``IRQ index 0 not found``，这是当前设计约束
下的预期现象，不影响串口 console 输出。

DWC PCIe and NVMe
-----------------

``dw-pcie=on`` 会在 ``io-system-lib`` 侧启用 DesignWare PCIe C-model，
并在 QEMU 侧创建 ``qti-pcie`` secondary bus。当前生成的 PCIe 节点使用
``snps,dw-pcie`` 兼容串，因此 Linux 直接走 DesignWare host driver，
不需要改内核。

当前生成的 PCIe 节点同时提供 MSI 和 hotplug 两根 IRQ。DWC iATU、ECAM 和
BAR 窗口都由 ``io-system-lib`` 里的 DWC PCIe C-model 处理，NVMe 仍然可以挂到
``qti-pcie`` bus 上复用 QEMU 的现有 endpoint 实现。

示例：继续用 ``my-virtio-blk`` 作为 rootfs，同时挂一个 QEMU NVMe endpoint：

注意：``st-release/minimal/Image`` 是最小启动镜像，不保证内建 DWC PCIe/NVMe
驱动。NVMe 测试请使用带 PCIe/NVMe 支持的内核，例如当前 workspace 中已验证的
``build/linux/arch/riscv/boot/Image``。

.. code-block:: bash

   truncate -s 64M /tmp/qti-nvme.raw
   ./qemu-system-riscv64 \
       -M qemu_to_iosystem,generated-dtb=on,fw-jump-fdt-addr=0x82200000,my-virtio-blk=on,dw-pcie=on,my-virtio-blk-image=/path/to/rootfs.ext4 \
       -smp 1 -m 1G \
       -accel tcg,thread=single \
       -display none -serial stdio -monitor none \
       -bios /nfs/home/guoyaxing/my-workspace/st-release/fw_jump/fw_jump-kmh-v2-no-dtb.bin \
       -device loader,file=/nfs/home/guoyaxing/my-workspace/build/linux/arch/riscv/boot/Image,addr=0x80400000 \
       -drive file=/tmp/qti-nvme.raw,format=raw,if=none,id=nvme0 \
       -device nvme,drive=nvme0,serial=qti-nvme0,bus=qti-pcie

Linux 日志中应能看到 DesignWare host bridge、PCI bus 枚举和
``nvme0n1``。如果后续要把 NVMe 作为根盘，再把内核命令行切到对应的
``root=/dev/nvme0n1``。
