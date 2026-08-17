QEMU to io-system machine
=========================

``qemu_to_iosystem`` 是 XiangShan/Kunminghu RISC-V machine 的一个
io-system C-model bring-up 入口。CPU、DRAM、CLINT、UART 和 IMSIC 仍由
QEMU 创建；APLIC 和首版 ``my-virtio-blk`` 由
``third_party/io-system-lib`` 静态库实现，并通过 Q2IO/IO2Q
transaction-level AXI 接口和 QEMU 交互。

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

``--enable-io-system`` 会调用 QEMU 源码树内
``third_party/io-system-lib/Makefile``，生成并静态链接
``libio_system.a`` 和 ``libio_system_backend.a``。QEMU adapter 仍位于
``third_party/io-system-lib/src/cmodel/adapters/qemu``，因此 QEMU
generated headers 由 QEMU Meson 在 configure/setup 后传给该 optional
adapter 编译。

Scheduler/service properties
----------------------------

``io2q-async=off``、``io2q-outstanding=1`` 和
``io-system-service-mode=inline`` 是稳定兼容默认值。可用
``io2q-async=on`` 开启协作式 IO2Q 排队，用 ``io2q-outstanding=N`` 设置
outstanding 深度，并用 ``io-system-service-mode=bh`` 让 QEMU bottom half
以有限 budget 调用 ``io_system_service()``；``inline`` 模式则在每次成功
Q2IO MMIO 后立即 service。对于具有线程亲和性的 picker/VCS RTL backend，BH
会把有限 budget 的 service 投递回首次处理 Q2IO MMIO 的 vCPU 线程，避免从
QEMU 主循环线程跨线程进入同一个 VCS runtime；该模式不创建额外 worker thread。

io-system IOMMU
---------------

``io-system-iommu=none|cmodel|rtl`` 选择 io-system 内的 IOMMU model。
默认值为 ``none``，此时设备 DMA 仍然直接访问最终 guest physical memory。

``io-system-iommu-placement=auto|external|embedded`` 控制 IOMMU 放置位置。
``auto`` 规则如下：

* ``io-system-backend=cmodel`` 配合 ``io-system-iommu=cmodel`` 或 ``rtl``
  时使用 ``external``，C-model 设备 DMA 先进入 IOMMU API，再由 IOMMU 的
  downstream/translation callbacks 访问最终 guest memory；
* ``io-system-backend=rtl-system`` 配合 ``io-system-iommu=rtl`` 时使用
  ``embedded``，host 侧不创建 external IOMMU，避免对 RTL 已发出的最终
  ``m_axi`` transaction 再做一次翻译；
* ``io-system-backend=rtl-system,io-system-iommu=cmodel`` 为 v1 不支持组合，
  启动时报错。

C-model IOMMU refmodel 源码已经 vendored 到
``third_party/io-system-lib/src/cmodel/iommu_refmodel``，并直接编进
``libio_system.a``；选择 ``io-system-iommu=cmodel`` 时不再 ``dlopen``
``libiommu_refmodel_api.so``。``io-system-iommu-refmodel-dir`` 作为兼容属性
保留，但内置 cmodel 路径不依赖它。

RTL IOMMU 仍通过 ``.so`` 动态加载。RTL IP、``iommu-api`` 源码和 picker
支持文件放在 workspace sibling ``bosc-iommu-v2`` 下。先执行：

.. code-block:: bash

   cd ../bosc-iommu-v2
   VCS_ENV=/path/to/vcs_env ./build-picker-iommu-vcs.sh

也可以不设置 ``VCS_ENV``，直接由调用环境提供 ``VCS_HOME``、license 和
``PATH``。picker、模板、xspcomm 和 ``iommu-api`` 源码默认都来自
``bosc-iommu-v2/third_party``，不再依赖旧 XSV 源树。

这会生成 ``bosc-iommu-v2/output/iommu-api/lib/libiommu_api.so`` 以及配套
``libUTiommu_wrap.so``、``libDPIiommu_wrap.so`` 和 ``libxspcomm.so``。
再在 ``third_party/io-system-lib`` 下执行
``make IO_SYSTEM_IOMMU_RTL=1 iommu-rtl-api``，会把该本地产物 stage 到
``output/iommu-rtl``。如果指定
``io-system-iommu-picker-out=PATH``，会优先查找
``PATH/lib/libiommu_api.so`` 和 ``PATH/libiommu_api.so``；默认测试脚本使用
``bosc-iommu-v2/output/iommu-api``。也可用 ``IO_SYSTEM_IOMMU_RTL_API_SO``
直接指定 ``.so``。
``io-system-iommu-rtl-ip-dir`` 和 ``io-system-iommu-vcs-libdir`` 保留给
RTL IOMMU 构建/运行路径管理。

生成 DTB 且启用 io-system IOMMU 时，``/soc/iommu@311f0000`` 会使用
``riscv,iommu`` binding 暴露 ``0x311f0000/0x1000`` MMIO aperture；如果同时
启用 ``dw-pcie=on``，PCIe 节点默认会带 ``iommu-map`` 指向该 IOMMU。
调试 RTL IOMMU bypass/PA 路径时，可以用
``io-system-pcie-iommu-map=off`` 保留 IOMMU MMIO 节点但不把 PCIe requester
绑定到 Linux IOMMU driver。DWC DMAC 这类 platform DMA requester 会通过
``iommus`` 属性携带 requester id。
``my-virtio-blk`` 当前作为 legacy boot 设备使用，未协商
``VIRTIO_F_ACCESS_PLATFORM``，因此生成 DTB 不把它挂到 IOMMU 下。使用外部
``-dtb`` 时，需要外部设备树自己描述同一个 IOMMU。

Boot Linux with my-virtio-blk
-----------------------------

用外部 ``fw_jump`` 和 ``-device loader`` 启动时，必须使用不带内置 DTB 的
OpenSBI 固件，或确认固件最终使用 QEMU 传入的 DTB。若使用带内置 DTB 的
``fw_jump-kmh-v2-1core.bin``，OpenSBI 可能绕过 QEMU 生成的
``qemu_to_iosystem`` DTB，Linux 会看到旧的 ``bosc,kmh-v2-1core`` 设备树。

假设 ``WORKSPACE`` 指向当前 my-workspace，已验证的最小命令如下：

.. code-block:: bash

   WORKSPACE=/path/to/my-workspace
   ROOTFS=$WORKSPACE/st-release/openEuler-minimal-rootfs/openEuler-minimal-rootfs-24.03-SP3-RVA23.ext4
   BIOS=$WORKSPACE/st-release/fw_jump/fw_jump-kmh-v2-no-dtb.bin
   KERNEL=$WORKSPACE/st-release/minimal/Image

   ./qemu-system-riscv64 \
       -M qemu_to_iosystem,generated-dtb=on,my-virtio-blk=on,my-virtio-blk-image="$ROOTFS" \
       -smp 1 -m 1G \
       -accel tcg,thread=single \
       -display none -serial stdio -monitor none \
       -bios "$BIOS" \
       -device loader,file="$KERNEL",addr=0x80400000

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

``dw-pcie=on`` 会在 ``third_party/io-system-lib`` 侧启用 DesignWare PCIe C-model，
并在 QEMU 侧创建 ``qti-pcie`` secondary bus。当前生成的 PCIe 节点使用
``snps,dw-pcie`` 兼容串，因此 Linux 直接走 DesignWare host driver，
不需要改内核。

当前生成的 PCIe 节点同时提供 MSI 和 hotplug 两根 IRQ。DWC iATU、ECAM 和
BAR 窗口都由 ``third_party/io-system-lib`` 里的 DWC PCIe C-model 处理，
NVMe 仍然可以挂到 ``qti-pcie`` bus 上复用 QEMU 的现有 endpoint 实现。

示例：继续用 ``my-virtio-blk`` 作为 rootfs，同时挂一个 QEMU NVMe endpoint：

注意：``st-release/minimal/Image`` 是最小启动镜像，不保证内建 DWC PCIe/NVMe
驱动。NVMe 测试请使用带 PCIe/NVMe 支持的内核，例如当前 workspace 中已验证的
``build/linux/arch/riscv/boot/Image``。

.. code-block:: bash

   WORKSPACE=/path/to/my-workspace
   ROOTFS=/path/to/rootfs.ext4
   BIOS=$WORKSPACE/st-release/fw_jump/fw_jump-kmh-v2-no-dtb.bin
   KERNEL=$WORKSPACE/build/linux/arch/riscv/boot/Image

   truncate -s 64M /tmp/qti-nvme.raw
   ./qemu-system-riscv64 \
       -M qemu_to_iosystem,generated-dtb=on,fw-jump-fdt-addr=0x82200000,my-virtio-blk=on,dw-pcie=on,my-virtio-blk-image="$ROOTFS" \
       -smp 1 -m 1G \
       -accel tcg,thread=single \
       -display none -serial stdio -monitor none \
       -bios "$BIOS" \
       -device loader,file="$KERNEL",addr=0x80400000 \
       -drive file=/tmp/qti-nvme.raw,format=raw,if=none,id=nvme0 \
       -device nvme,drive=nvme0,serial=qti-nvme0,bus=qti-pcie

Linux 日志中应能看到 DesignWare host bridge、PCI bus 枚举和
``nvme0n1``。如果后续要把 NVMe 作为根盘，再把内核命令行切到对应的
``root=/dev/nvme0n1``。
