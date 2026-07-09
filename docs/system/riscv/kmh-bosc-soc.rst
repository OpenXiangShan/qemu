BOSC Kunminghu multi-die SoC (``kmh-bosc-soc``)
================================================

``kmh-bosc-soc`` models the BOSC Kunminghu multi-die SoC.  It is a
64-bit RISC-V machine with four dies.  Each die has 16 application harts
and one RV32 MCU hart.

The machine is intended for TCG.  It creates all 64 application harts, but
``die-mask`` and ``core-mask`` decide which dies and application harts are
visible to the guest firmware/device tree and which harts are released from
reset.  Harts not selected by the masks are powered off and described as
``disabled`` in the generated device tree.

Supported devices
-----------------

The ``kmh-bosc-soc`` machine currently supports or models the following
devices:

* 64 Kunminghu application harts
* 4 RV32 MCU harts, one per die
* Per-die DDR windows
* Per-die local SRAM, boot control, system control, and MCU-local address
  views
* AIA interrupt controllers: IMSIC and APLIC
* ACLINT timer/software interrupt blocks
* One UART
* DesignWare PCIe host controllers
* RISC-V IOMMU platform device MMIO block
* DesignWare AXI DMAC
* QSPI, trace, debug, and several control windows as unimplemented MMIO
  devices

The RISC-V trace encoder is not implemented yet.  The trace MMIO aperture is
present only as an unimplemented device.

Required command line
---------------------

The machine validates a fixed CPU topology:

.. code-block:: bash

   -smp 64,maxcpus=68

``64`` is the total number of application harts.  ``maxcpus=68`` leaves room
for the four MCU harts.  This requirement does not change when ``core-mask``
selects fewer application harts.

RAM is split evenly across the four dies.  The size must be divisible by four
and must not exceed the documented 128 GiB DDR aperture.

Machine options
---------------

可以用下面的命令查看 machine 属性：

.. code-block:: bash

   $ qemu-system-riscv64 -M kmh-bosc-soc,help

常用属性如下。

``boot-source=ddr|mcu``
   选择启动来源。默认是 ``ddr``。

   ``ddr`` 启动直接从第一个被选中的 die 的 DDR 入口启动 application
   harts。使用 ``-bios none`` 时必须同时提供 ``-kernel <elf>``。

   ``mcu`` 启动先运行每个被选中 die 的 RV32 MCU hart，再由 MCU 通过
   boot-control 寄存器释放 application harts。该模式需要
   ``mcu-bios=<raw-bin>``、``-bios fw_payload.bin``，不能同时使用
   ``-kernel``，并且 ``die-mask`` 必须包含 die0。

``mcu-bios=<path>``
   ``boot-source=mcu`` 时复制到每个被选中 die MCU boot ROM 的 RV32 raw
   boot image。

``die-mask=<mask>``
   低 4 bit 的 die 选择掩码。默认 ``0xf``。被选中的 die 会参与 DDR
   映射、外设建模、设备树生成和 application hart release。

   例如 ``die-mask=0x1`` 只选择 die0；``die-mask=0xf`` 选择四个 die。

``core-mask=<mask>`` 或 ``core-mask=<d0>:<d1>:<d2>:<d3>``
   application hart 选择掩码。每个 die 最多 16 个 application harts，所以
   每个 entry 的有效 bit 为低 16 bit。默认 ``0xffff``，保持每个被选中
   die 的 16 个 application harts 全部启用。

   单个 mask 会应用到所有 die：

   .. code-block:: bash

      -M kmh-bosc-soc,die-mask=0x1,core-mask=0x1

   上面的配置只在 die0 中启用 application hart0。

   也可以给四个 die 分别设置掩码。因为 QEMU ``-M`` 使用逗号分隔 machine
   属性，所以这里使用冒号分隔各 die 掩码：

   .. code-block:: bash

      -M kmh-bosc-soc,die-mask=0x1,core-mask=0x1:0x0:0x0:0x0

   上面的配置也只启用 die0 application hart0。被 ``die-mask`` 排除的 die
   即使在 ``core-mask`` 中设置了 bit，也不会启用对应 hart。

``generated-dtb=auto|on|off``
   是否使用 QEMU 生成的设备树。默认是 ``auto``，当前等同于 ``off``：
   普通 firmware 启动时默认使用 OpenSBI 里的 DTB。需要让 QEMU 生成并
   传入 DTB 时，显式设置 ``generated-dtb=on``。

   ``generated-dtb=on`` 不能和外部 ``-dtb`` 同时使用。

``generated-acpi=on|off``
   是否生成 ACPI 表并把它们写入 DDR handoff 区域，默认 ``off``。打开后
   QEMU 会生成 RSDP、XSDT、FADT、DSDT、MADT、RHCT 和 SPCR 等基础表。
   如果同时使用 QEMU 生成设备树，设备树会加入 compatible 为
   ``bosc,kmh-acpi-handoff`` 的 reserved-memory 节点，UEFI 可通过该节点
   找到 ACPI handoff 区域。当前路径不使用 ``fw_cfg``。

``acpi-handoff-addr=<addr>``
   generated ACPI handoff 区域的 DDR 基地址，默认 ``0x90200000``。该地址
   必须 16-byte 对齐，并且落在被选中 die 的 guest DDR 范围内。

``acpi-handoff-size=<size>``
   generated ACPI handoff 区域大小，默认 ``0x20000``。该大小必须 16-byte
   对齐，并且能够容纳 QEMU 生成的 ACPI 数据。

``dw-pcie=on|off``
   是否启用 DesignWare PCIe host controllers。默认 ``off``。打开后，QEMU
   会实例化每个被选中 die 上的 PCIe host controller，并在生成的设备树中
   加入对应 PCIe 节点。

   PCIe endpoint 挂到 ``pcie-d<die>-p<port>`` bus。例如在 die0 的 port0
   上挂一个 NVMe 盘：

   .. code-block:: bash

      -drive file=./kmh-nvme.raw,format=raw,if=none,id=nvme0 \
      -device nvme,drive=nvme0,serial=kmh-nvme0,bus=pcie-d0-p0

``iommu-sys=auto|on|off``
   是否启用 RISC-V IOMMU platform device。默认 ``auto``，当前等同于
   ``off``。需要建模 IOMMU MMIO 设备并在生成的设备树中加入
   ``riscv,iommu`` 节点时，显式设置 ``iommu-sys=on``。

``autotest-dtb=on|off``
   是否在 QEMU 生成的设备树中加入 automated-test 节点。默认 ``off``。
   打开后，QEMU 会添加 pmem/nvdimm、reserved-memory 和 trigger bootargs
   节点。该选项必须和 ``generated-dtb=on`` 一起使用，不能和外部
   ``-dtb`` 组合。

``autotest-trigger-addr=<addr>``
   automated-test trigger reserved-memory 地址。默认 ``0x90000000``。

``autotest-rootfs-addr=<addr>``
   automated-test rootfs pmem/nvdimm 地址。默认 ``0x3c0000000``。

``autotest-workload-addr=<addr>``
   automated-test workload pmem/nvdimm 地址。默认 ``0x3e0000000``。

Generated device tree
---------------------

设置 ``generated-dtb=on`` 时，``kmh-bosc-soc`` 会生成设备树。没有打开该
选项且没有传 ``-dtb`` 时，设备树由 firmware 提供或管理。QEMU 生成的
设备树内容会根据 ``die-mask`` 和 ``core-mask`` 裁剪：

* 未选中的 die 不生成对应 memory、AIA、DMAC、PCIe 等节点。
* ``dw-pcie=off`` 时不生成 PCIe 节点。
* ``iommu-sys=off`` 或默认 ``auto`` 时不生成 IOMMU 节点。
* 未选中的 application hart 会保留 CPU 节点，但 ``status`` 为
  ``disabled``。
* ``/chosen/opensbi-config/cold-boot-harts`` 只指向第一个被选中的
  application hart。
* ACLINT、IMSIC interrupt properties 和 NUMA distance map 只覆盖当前
  被选中的 harts/dies。

可以使用 ``dumpdtb=`` 导出生成的设备树：

.. code-block:: bash

   $ qemu-system-riscv64 \
       -M kmh-bosc-soc,die-mask=0x1,core-mask=0x1,generated-dtb=on,dumpdtb=/tmp/kmh.dtb \
       -smp 64,maxcpus=68 -m 4G -nographic \
       -bios none -kernel /path/to/kernel.elf

Direct DDR boot
---------------

使用 ``-bios none`` 直接启动 ELF：

.. code-block:: bash

   $ qemu-system-riscv64 \
       -M kmh-bosc-soc,die-mask=0x1,core-mask=0x1,boot-source=ddr,generated-dtb=on \
       -smp 64,maxcpus=68 -m 4G -nographic \
       -bios none -kernel /path/to/kernel.elf

QEMU 会把 ``a0`` 设置为 hartid，把 ``a1`` 设置为生成的 FDT 地址。直接
``-bios none -kernel`` 启动没有 firmware 可以提供 DTB，因此必须使用
``generated-dtb=on`` 或外部 ``-dtb``。

如果使用 firmware payload，则不要同时传 ``-kernel``：

.. code-block:: bash

   $ qemu-system-riscv64 \
       -M kmh-bosc-soc,die-mask=0x1,core-mask=0x1,boot-source=ddr \
       -smp 64,maxcpus=68 -m 4G -nographic \
       -bios /path/to/fw_payload.bin

上面的普通 firmware 启动默认使用 OpenSBI 里的 DTB。若要改用 QEMU 生成的
DTB，需要额外设置 ``generated-dtb=on``。

UEFI minimal Linux boot
-----------------------

``KUNMINGHU-BOSC-SOC`` UEFI 可以通过 PCIe NVMe 上的 FAT 分区加载 Linux
``Image``。当前最小启动路径只依赖 UART、AIA 中断和内存；``dw-pcie=on``
用于 UEFI 读取 FAT 盘，UEFI 在进入 Linux 前会隐藏生成 DTB 中的 PCIe 节点，
QEMU 生成的最小 ACPI 表也不描述 PCIe。

FAT 分区根目录放置 ``Image`` 和 ``startup.nsh``。设备树启动可以使用下面的
``startup.nsh``：

.. code-block:: text

   fs0:\Image console=ttyS0,115200 earlycon=uart8250,mmio32,0x4000000,115200n8 loglevel=8 ignore_loglevel acpi=off rdinit=/bin/sh

ACPI 启动把 ``acpi=off`` 改成 ``acpi=on``：

.. code-block:: text

   fs0:\Image console=ttyS0,115200 earlycon=uart8250,mmio32,0x4000000,115200n8 loglevel=8 ignore_loglevel acpi=on rdinit=/bin/sh

单 die 设备树启动示例：

.. code-block:: bash

   $ qemu-system-riscv64 \
       -M kmh-bosc-soc,boot-source=ddr,die-mask=0x1,core-mask=0x1:0x0:0x0:0x0,generated-dtb=on,dw-pcie=on \
       -smp 64,maxcpus=68 -m 4G -nographic \
       -bios /path/to/fw_jump.bin \
       -device loader,file=/path/to/KUNMINGHUBOSCSOC.fd,addr=0x80200000 \
       -drive file=/path/to/kmh-bosc-dt-fat.img,format=raw,if=none,id=nvme0 \
       -device nvme,drive=nvme0,serial=kmh-bosc-nvme0,bus=pcie-d0-p0

多 die 设备树启动只需要调整 die 和 hart 掩码，例如启用 die0/hart0 与
die1/hart0：

.. code-block:: bash

   $ qemu-system-riscv64 \
       -M kmh-bosc-soc,boot-source=ddr,die-mask=0x3,core-mask=0x1:0x1:0x0:0x0,generated-dtb=on,dw-pcie=on \
       -smp 64,maxcpus=68 -m 4G -nographic \
       -bios /path/to/fw_jump.bin \
       -device loader,file=/path/to/KUNMINGHUBOSCSOC.fd,addr=0x80200000 \
       -drive file=/path/to/kmh-bosc-dt-fat.img,format=raw,if=none,id=nvme0 \
       -device nvme,drive=nvme0,serial=kmh-bosc-nvme0,bus=pcie-d0-p0

打开 generated ACPI 时，QEMU 把 ACPI 数据写入 DDR handoff 区域；UEFI 通过
设备树中的 ``bosc,kmh-acpi-handoff`` 节点扫描 RSDP 并安装 ACPI 表。单 die
ACPI 示例：

.. code-block:: bash

   $ qemu-system-riscv64 \
       -M kmh-bosc-soc,boot-source=ddr,die-mask=0x1,core-mask=0x1:0x0:0x0:0x0,generated-dtb=on,generated-acpi=on,dw-pcie=on \
       -smp 64,maxcpus=68 -m 4G -nographic \
       -bios /path/to/fw_jump.bin \
       -device loader,file=/path/to/KUNMINGHUBOSCSOC.fd,addr=0x80200000 \
       -drive file=/path/to/kmh-bosc-acpi-fat.img,format=raw,if=none,id=nvme0 \
       -device nvme,drive=nvme0,serial=kmh-bosc-nvme0,bus=pcie-d0-p0

多 die ACPI 示例：

.. code-block:: bash

   $ qemu-system-riscv64 \
       -M kmh-bosc-soc,boot-source=ddr,die-mask=0x3,core-mask=0x1:0x1:0x0:0x0,generated-dtb=on,generated-acpi=on,dw-pcie=on \
       -smp 64,maxcpus=68 -m 4G -nographic \
       -bios /path/to/fw_jump.bin \
       -device loader,file=/path/to/KUNMINGHUBOSCSOC.fd,addr=0x80200000 \
       -drive file=/path/to/kmh-bosc-acpi-fat.img,format=raw,if=none,id=nvme0 \
       -device nvme,drive=nvme0,serial=kmh-bosc-nvme0,bus=pcie-d0-p0

如果不使用 QEMU 生成的设备树，固件传入的 DTB 也必须包含同一个
``bosc,kmh-acpi-handoff`` 节点，并且 ``reg`` 与 ``acpi-handoff-addr``、
``acpi-handoff-size`` 一致。

MCU boot
--------

MCU boot 用 RV32 MCU 先启动，再由 MCU 释放 application harts：

.. code-block:: bash

   $ qemu-system-riscv64 \
       -M kmh-bosc-soc,boot-source=mcu,die-mask=0x1,core-mask=0x1,mcu-bios=/path/to/mcu.bin \
       -smp 64,maxcpus=68 -m 4G -nographic \
       -bios /path/to/fw_payload.bin

``core-mask`` 同样会限制 MCU 最终释放的 application harts。

Automated-test device tree
--------------------------

``generated-dtb=on,autotest-dtb=on`` 会生成 DTB，并加入 automated-test 所需
节点：

* ``/reserved-memory/my_reserved_buffer@<trigger-addr>``，compatible 为
  ``my,reserved-mem``。
* rootfs 和 workload 的 ``reserved-memory`` 区域，带 ``no-map``。
* rootfs 和 workload 的 ``pmem-region``, ``nvdimm`` 节点。
* ``/chosen/bootargs`` 中保留宽度固定的
  ``task=0x0000000000000000`` placeholder，供 OpenSBI trigger overlay
  原地改写。

最小导出示例：

.. code-block:: bash

   $ qemu-system-riscv64 \
       -M kmh-bosc-soc,die-mask=0x1,core-mask=0x1,generated-dtb=on,autotest-dtb=on,dumpdtb=/tmp/kmh-autotest.dtb \
       -smp 64,maxcpus=68 -m 4G -nographic \
       -bios none -kernel /path/to/kernel.elf

若使用已经打包 DTB 的 firmware，则不要设置 ``generated-dtb=on`` 或
``autotest-dtb=on``，让 firmware 自己把 DTB 传给下一阶段。

Limitations
-----------

* 当前仅支持 TCG。
* 即使只启用一个 application hart，也仍需 ``-smp 64,maxcpus=68``。
* ``initrd`` 暂不支持。
* ``generated-dtb=on`` 和 ``autotest-dtb=on`` 不能与外部 ``-dtb`` 组合。
* ``generated-acpi=on`` 当前覆盖最小系统启动，不包含 PCIe ACPI 描述。
* ``kmh-bosc-soc`` 当前没有 RISC-V trace encoder 支持。
