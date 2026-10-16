BOSC Kunminghu single/four-die SoC (kmh-bosc-soc)
======================================================

The machine shape is selected with the soc-mode property:

* soc-mode=multi (the default) models the four-die SoC. die-mask may select
  which dies are released, but it does not change the peripheral address
  format: addresses retain hardware die IDs 1 through 4.
* soc-mode=single models a true single-die SoC. Only die0 is instantiated,
  die-mask is ignored, and peripheral/SRAM/ROM addresses use local offsets.
  Use -smp 16,maxcpus=17; all -m memory is assigned to die0, up to 32 GiB.

Example command lines:

.. code-block:: bash

   # True single-die address map
   -M kmh-bosc-soc,soc-mode=single,die-mask=0xf,core-mask=0xffff \
   -smp 16,maxcpus=17 -m 16G

   # Four-die SoC with only die0 released; addresses still carry ID 1
   -M kmh-bosc-soc,soc-mode=multi,die-mask=0x1,core-mask=0x1 \
   -smp 64,maxcpus=68 -m 16G

Multi-die model
================

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
* Per-die UARTs (the generated OS console uses die0 UART)
* DesignWare PCIe host controllers
* RISC-V IOMMU platform device with PCIe DMA remapping
* DesignWare AXI DMAC
* QSPI, trace, debug, and several control windows as unimplemented MMIO
  devices

The RISC-V trace encoder is not implemented yet.  The trace MMIO aperture is
present only as an unimplemented device.

Die addressing
--------------

Die indices in machine options, bus names and hart IDs remain zero-based.
Hardware die IDs in peripheral physical addresses are 1, 2, 3, 4::

   global peripheral address = ((die_index + 1) << 44) | local_offset
   mhartid = die_index * 16 + core_index

For a requester on a given die, addresses with die ID 0 select that die's
local peripheral. Its own nonzero die ID selects the same peripheral;
other nonzero die IDs select remote peripherals. Application harts and
MCUs have stable per-die address spaces. APLIC MSI, DMAC DMA, and PCIe DMA
without the system IOMMU use their owning die's address space, independently
of which CPU last programmed the device.

================= =============== ====================
Physical die      Hardware die ID Core0 machine IMSIC
================= =============== ====================
die0              1               0x10001c000000
die1              2               0x20001c000000
die2              3               0x30001c000000
die3              4               0x40001c000000
================= =============== ====================

For example, a die1 CPU can access its own IMSIC through either
``0x1c000000`` or ``0x20001c000000``. ``0x10001c000000`` always means
die0, including when accessed from die1. QEMU monitor/qtest physical
accesses have no die requester and should use global peripheral addresses.

DDR retains its existing global address map; the local alias rule is for
the modeled peripheral/SRAM/ROM windows, not the whole low address space.
ACLINT remains a shared 64-hart block, accessible at ``0x19000000`` and
through each nonzero die ID. Each selected die has three independent
system IOMMUs, each paired with a DID mapper and AXI4 traffic generator;
IOMMU output addresses are global.

Generated DT and ACPI use global peripheral addresses so software may
migrate between dies. In particular, the OS UART is ``0x100004000000``;
use ``earlycon`` from the generated DT, or explicitly use that address.
Older external DTBs/ACPI tables using zero-based global die IDs must be
updated. The IMSIC group index has 3 bits at bit 44, because 2 bits
cannot represent hardware die ID 4.

Cross-die APLIC priority test
----------------------------

For die0 APLIC to die1/core0 (software ``mhartid=16``), the standard
machine-level MSI configuration is::

   mmsiaddrcfg  = 0x0001c000
   mmsiaddrcfgh = 0x14034000  (HHXS=20, HHXW=3, LHXW=4, LHXS=0)
   target Hart Index = (2 << 4) | 0 = 32
   target[source] = (32 << 18) | EIID = 0x00800000 | EIID
   MSI address = 0x20001c000000, data = EIID

The APLIC Hart Index is not ``mhartid``. Do not shift the group field to
bit 45 just to make software hart16 produce hardware die ID 2.

The per-die test helper at local address ``0x0409014c`` raises all 56
non-reserved sources when written with ``0xffffffff``; a source number
clears that source. When the receiver on die1 clears a source belonging
to die0, it must write ``0x10000409014c``, not the local helper on die1.

Run the exported ITGen cross-die BIN with::

   qemu-system-riscv64 \
     -M kmh-bosc-soc,die-mask=0x3,core-mask=0x1:0x1:0:0,boot-source=ddr \
     -smp 64,maxcpus=68 -m 256M -nographic -bios cross-die.bin

The guest reports software target hart16 and ``claimed=56``. The existing
ITGen ``0x5006b`` completion opcode is not a QEMU shutdown instruction;
terminate QEMU after checking the PASS line.

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

DDR slots
---------

The DDR aperture starts at ``0x80000000`` and spans 128 GiB. Each physical
die owns a fixed 32 GiB slot, independent of ``die-mask`` and ``-m``:

======== ============== ============== ==========
Die      Slot start     Slot end       Maximum
======== ============== ============== ==========
die0     0x0080000000   0x087fffffff   32 GiB
die1     0x0880000000   0x107fffffff   32 GiB
die2     0x1080000000   0x187fffffff   32 GiB
die3     0x1880000000   0x207fffffff   32 GiB
======== ============== ============== ==========

Slot ends above are inclusive. For each selected die, QEMU maps
``-m / 4`` bytes starting at that die's slot start; the unused suffix is
unmapped. For example, ``-m 16G`` maps the first 4 GiB of each selected
slot, and ``-m 128G`` fills all selected slots with 32 GiB each.

The divisor remains four even if ``die-mask`` selects fewer dies. Disabled
dies have no DDR mapping, and the remaining dies are not renumbered or
given larger RAM regions. For example, ``die-mask=0x5,-m 16G`` exposes
4 GiB on die0 and 4 GiB on die2 at their fixed physical addresses.
Generated DT memory nodes and ACPI SRAT entries follow this same layout;
autotest DTs additionally exclude the reserved rootfs/workload windows
from ordinary System RAM. DDR does not use the peripheral die-ID encoding.

Machine options
---------------

可以用下面的命令查看 machine 属性：

.. code-block:: bash

   $ qemu-system-riscv64 -M kmh-bosc-soc,help

常用属性如下。

``soc-mode=single|multi``
   选择 SoC 形态，默认是 ``multi``。``multi`` 保持四 die SoC 的硬件
   die-ID 全局外设地址体系，即使 ``die-mask`` 只选择一个 die；``single``
   只实例化 die0，忽略 ``die-mask``，并使用不带 die-ID 的本地外设地址。
   single 模式使用 ``-smp 16,maxcpus=17``，multi 模式使用
   ``-smp 64,maxcpus=68``。

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

   CPU-to-PCI accesses first select a die and port using the global ECAM or
   memory aperture. The fabric removes the die ID in bits [46:44] before
   the selected RC compares the address against outbound iATU BASE/LIMIT.
   Each RC has an independent local address space, so identical local iATU
   windows on different dies remain isolated. Downstream configuration
   accesses also require an enabled, matching CFG viewport; only root-bus
   configuration is accessible without one.

   Generated DT nodes use ``bosc,kmh-bosc-soc-pcie``. Linux must enable
   ``CONFIG_PCIE_KMH_BOSC_SOC`` and program local outbound iATU addresses,
   while retaining die IDs in CPU-visible ``reg`` and ``ranges`` addresses.
   This outbound path is independent of endpoint DMA, IOMMU translation,
   and MSI routing.

   This machine models a 44-bit PCIe AXI master: memory requests leaving an
   RC are truncated to bits [43:0]. Local MMIO then resolves through the
   requester's existing die address space. DDR keeps its global fixed-slot
   layout. CPU, APLIC, and DMAC accesses retain their existing address widths;
   other machines using DesignWare PCIe are not truncated.

   Each RC enables 256 integrated MSI receiver (iMSI-RX) vectors, in eight
   banks of 32. A matching PCIe memory write sets the corresponding status
   bit and asserts the RC's summary interrupt into its local APLIC, then
   continues through the original endpoint's DMA requester path. With
   IOMMU bypass disabled, the forwarded write requires a valid IOMMU
   mapping, including for the MSI landing page. Reads of the receiver
   address remain ordinary DMA. Receiver addresses and pending bits belong
   to individual RCs, including when they use the same address.

   Generated DT PCIe nodes omit ``msi-parent`` and ``msi-map`` so the Linux
   ``kmh-bosc-soc-pcie`` driver selects iMSI-RX. They retain ``msi`` and ``hp``
   interrupt resources; the APLIC nodes still reference IMSIC as their MSI
   parent. External DTs must describe this same wiring to use iMSI-RX.
   ACPI does not describe this DWC-specific receiver path.

   ``kmh-bosc-pcie-test`` exercises all 256 vectors, interrupt masking/W1C,
   RC isolation, local CFG/MEM iATU routing across all twelve multi-die RCs
   and all three single-die RCs, full-address MSI matching, AXI truncation,
   global DDR DMA,
   IOMMU Off/Bare behavior, and an existing other-machine DMA/MSI path.
   The shared DWC model defaults to 32 vectors; this board explicitly sets
   its ``num-msi-vectors`` property to 256.

``iommu-sys=auto|on|off``
   是否启用 RISC-V IOMMU platform device。默认 ``auto``，当前等同于
   ``off``。需要建模 IOMMU MMIO 设备并在生成的设备树中加入
   ``riscv,iommu`` 节点时，显式设置 ``iommu-sys=on``。如果同时启用
   ``dw-pcie=on``，每个 PCIe root bus 的 DMA 会接入同 die/port 的独立 IOMMU，生成的
   PCIe 节点也会加入覆盖完整 requester-ID 空间的 ``iommu-map``。该选项
   不改变 DesignWare AXI DMAC 的 DMA 路径。使用外部 ``-dtb`` 时需要由
   外部设备树提供相同的 IOMMU 描述；当前 QEMU 生成的 ACPI 尚不包含 RIMT。

   PCIe device_id 为原 requester ID ``(bus << 8) | devfn``。每个 root 有独立
   IOMMU，DT ``iommu-map`` 使用 ``<0 iommu-phandle 0 0x10000>``；各 root 的
   ``01:00.0`` endpoint 均使用 DID ``0x100``。Mapper 默认关闭；开启且请求
   IOVA 命中有效项时，使用配置的 DID/PID/PV，否则保留原 requester 身份。
   AXI traffic 的原身份来自其 user/cfg 字段，也可由对应 mapper 覆盖。

``iommu-bypass=on|off``
   设置各 IOMMU wrapper 的复位 bypass 值，默认 ``on``，与硬件 bit0 的默认值
   一致。仅在 ``iommu-sys=on`` 创建 wrapper 后生效；该选项不删除 IOMMU 或
   设备树的 ``iommu-map``。运行时由每个 die/port 的 mapper mux bit0 独立控制：
   bit0 为 1 时，PCIe DMA 保留低 44 位地址，直接访问请求者所属 die 的
   AddressSpace，跳过 DID/PID mapper 和 IOMMU 翻译；bit0 为 0 时恢复 mapper
   和 IOMMU 路径。AXI traffic 同样使用该 bypass 位。匹配 iMSI-RX 的 MSI 写会
   先更新接收器状态，再经原 requester 的 DMA 路径转发。

   wrapper mux 本地地址为 ``0x4f80201000 + port * 0x10000``；multi 模式还需
   加上 ``(die + 1) << 44``。Guest 在加载 NVMe 前应读改写清 bit0，保留 mapper
   enable 等其他位。若 NVMe 驱动内建，可使用 ``iommu-bypass=off`` 在首次 probe
   前关闭 bypass；该参数也决定后续 machine reset 的 wrapper 默认值。

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
* 同时设置 ``dw-pcie=on,iommu-sys=on`` 时，每个 PCIe 节点生成指向
  system IOMMU 的 ``iommu-map``。
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

The ACLINT timebase is 13 MHz in both SoC modes, matching the Linux
``kmh-bosc-soc-1die1c`` and ``kmh-bosc-soc-1die16c`` board device trees.
Generated device trees and ACPI RHCT advertise the same frequency. External
device trees must describe this timebase too; a 1 MHz DT from an older QEMU
build must be updated before reuse. The ``xiangshan-kunminghu`` machine
retains its separate 1 MHz timebase.

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

   fs0:\Image console=ttyS0,115200 earlycon=uart8250,mmio32,0x100004000000,115200n8 loglevel=8 ignore_loglevel acpi=off rdinit=/bin/sh

ACPI 启动把 ``acpi=off`` 改成 ``acpi=on``：

.. code-block:: text

   fs0:\Image console=ttyS0,115200 earlycon=uart8250,mmio32,0x100004000000,115200n8 loglevel=8 ignore_loglevel acpi=on rdinit=/bin/sh

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
