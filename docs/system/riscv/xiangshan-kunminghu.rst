BOSC Xiangshan Kunminghu FPGA prototype platform (``xiangshan-kunminghu``)
==========================================================================

The ``xiangshan-kunminghu`` machine is compatible with our FPGA prototype
platform.

XiangShan is an open-source high-performance RISC-V processor project.
The third generation processor is called Kunminghu. Kunminghu is a 64-bit
RV64GCBSUHV processor core. More information can be found in our Github
repository:
https://github.com/OpenXiangShan/XiangShan

Supported devices
-----------------

The ``xiangshan-kunminghu`` machine supports the following devices:

* Up to 16 xiangshan-kunminghu cores
* Core Local Interruptor (CLINT)
* Incoming MSI Controller (IMSIC)
* Advanced Platform-Level Interrupt Controller (APLIC)
* 1 UART
* PCIe host bridge
* Optional system IOMMU platform device

Boot options
------------

本机型当前支持三种常用启动方式：

* ``-kernel Image``：QEMU 使用内置的 KMH OpenSBI
  ``opensbi-riscv64-xiangshan-kmh-fw_dynamic.bin``，并自动生成设备树。
* ``-bios fw_jump.bin`` + ``-device loader``：外部 OpenSBI ``fw_jump.bin``
  作为固件，Linux ``Image`` 由 loader 放到指定物理地址，设备树仍由
  QEMU 生成。
* ``autotest-dtb=on``：在 QEMU 生成的设备树中额外加入自动测试需要的
  reserved-memory、pmem/nvdimm 和 trigger bootargs。

老的 ``fw_payload.bin`` 方式仍可通过 ``-bios`` 使用，但如果固件里已经
打包了 DTB，则不会使用 QEMU 当前生成的设备树。

Machine options
---------------

可以用下面的命令查看本机型支持的 machine 属性：

.. code-block:: bash

   $ ./build/qemu-system-riscv64 -M xiangshan-kunminghu,help

常用属性如下：

``generated-dtb=auto|on|off``
   是否使用 QEMU 生成的设备树。默认是 ``auto``：使用 ``-kernel`` 启动
   或打开 ``autotest-dtb=on`` 时自动生成；使用普通 ``-bios`` 且没有
   ``-kernel`` 时默认不生成。需要用 ``fw_jump.bin`` 加 loader 启动内核时，
   建议显式设置 ``generated-dtb=on``。

``autotest-dtb=on|off``
   是否在 QEMU 生成的设备树里加入自动测试节点。打开后会隐含需要
   QEMU 生成设备树，并且不能同时使用 ``-dtb``。

``dw-pcie=on|off``
   是否创建 DWC PCIe RC0 设备。默认 ``off``。如果 QEMU 当前使用生成的
   设备树，打开后会在设备树里加入 DWC PCIe RC0 节点；使用外部 ``-dtb``
   时，需要外部设备树自己描述同一个 PCIe host。

``iommu-sys=auto|on|off``
   是否创建 RISC-V IOMMU platform device。默认 ``auto``，当前等同于
   ``off``。如果 QEMU 当前使用生成的设备树，打开 ``on`` 后会加入
   ``riscv,iommu`` 节点；如果同时打开 ``dw-pcie=on``，PCIe 节点会加入
   ``iommu-map``。使用外部 ``-dtb`` 时，需要外部设备树自己描述同一个
   IOMMU。

``fw-jump-fdt-addr=<addr>``
   ``-bios fw_jump.bin`` + ``-device loader`` 启动时，QEMU 生成 DTB 的入口
   传入地址，默认 ``0x80200000``。这个地址需要和 OpenSBI ``fw_jump`` 能够
   接收的 FDT 源地址一致。OpenSBI 最终打印给 Linux 的 ``Next Arg1`` 可能是
   固件重定位后的地址，由 ``FW_JUMP_FDT_OFFSET`` 或 ``FW_JUMP_FDT_ADDR``
   决定。

``autotest-image-addr=<addr>``
   自动测试 Linux ``Image`` 的 loader 地址，默认 ``0x80400000``。

``autotest-rootfs-addr=<addr>``
   rootfs ext4 的 pmem/nvdimm 地址，默认 ``0x3c0000000``。

``autotest-rootfs-size=<size>``
   rootfs ext4 的 pmem/nvdimm 和 reserved-memory 大小，默认
   ``0x20000000``。

``autotest-workload-addr=<addr>``
   测试集 ext4 的 pmem/nvdimm 地址，默认 ``0x3e0000000``。

``autotest-workload-size=<size>``
   测试集 ext4 的 pmem/nvdimm 和 reserved-memory 大小，默认
   ``0x80000000``。

``autotest-trigger-addr=<addr>``
   trigger 文件的 reserved-memory 地址，默认 ``0x90000000``。QEMU 只负责
   在设备树中保留这段内存；trigger 内容解析和 ``task=`` bootargs overlay
   由 OpenSBI 负责。

``autotest-trigger-size=<size>``
   trigger 文件的 reserved-memory 大小，默认 ``0x200000``。

``iommu-sys=auto|on|off``
   是否打开系统 IOMMU platform device。最小 Linux 和 autotest 启动流程
   不需要打开该选项。

Running
-------

下面示例假设当前目录为 QEMU 源码目录，QEMU 已构建到 ``build/``。示例中的
``/path/to/...`` 需要替换成自己的固件、内核、rootfs、workload 和 trigger
文件路径。

Build QEMU
~~~~~~~~~~

.. code-block:: bash

   $ ./configure --target-list=riscv64-softmmu --disable-werror --disable-docs
   $ ninja -C build qemu-system-riscv64

Boot Linux with ``-kernel``
~~~~~~~~~~~~~~~~~~~~~~~~~~~

这是最小 Linux 启动路径。QEMU 会加载 KMH OpenSBI，并为 ``Image`` 生成
设备树：

.. code-block:: bash

   $ ./build/qemu-system-riscv64 \
       -M xiangshan-kunminghu \
       -smp 4 -m 16G -nographic \
       -kernel /path/to/Image \
       -append "console=ttyS0,115200 earlycon loglevel=8"

这个模式只需要 CPU、串口、timer 和中断相关设备即可进入 Linux 命令行。
如果使用的是自动测试用 ``Image`` 或 initramfs，而没有打开
``autotest-dtb=on``，启动日志里出现 ``/dev/pmem0`` 不存在是预期现象。

Boot Linux with ``fw_jump.bin``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

外部 ``fw_jump.bin`` 启动时，内核需要用 loader 放到 OpenSBI 编译时配置的
跳转地址。当前默认示例使用 ``Image`` 地址 ``0x80400000``：

.. code-block:: bash

   $ ./build/qemu-system-riscv64 \
       -M xiangshan-kunminghu,generated-dtb=on \
       -smp 4 -m 16G -nographic \
       -bios /path/to/fw_jump.bin \
       -device loader,file=/path/to/Image,addr=0x80400000

这种模式不能使用 ``-append``，因为 QEMU 只允许 ``-append`` 和 ``-kernel``
一起使用。bootargs 来自 QEMU 生成的设备树或固件对设备树的修改。

QEMU 默认会把生成的 DTB 加载到 ``0x80200000``，作为传给 KMH OpenSBI
``fw_jump`` 的入口 FDT 地址。这个默认值匹配常见的编译配置：

.. code-block:: bash

   FW_TEXT_START=0x80000000
   FW_JUMP_FDT_OFFSET=0x200000
   FW_JUMP_OFFSET=0x400000

如果修改 ``Image`` 加载地址，需要同步用匹配的 ``FW_JUMP_OFFSET`` 重新编译
``fw_jump.bin``。如果修改 ``FW_JUMP_FDT_OFFSET``，需要用
``fw-jump-fdt-addr`` 指定 QEMU 传给 OpenSBI 的 FDT 源地址，例如：

.. code-block:: bash

   $ ./build/qemu-system-riscv64 \
       -M xiangshan-kunminghu,generated-dtb=on,fw-jump-fdt-addr=0x80200000 \
       ...

FDT handoff with ``fw_jump.bin``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``generated-dtb=on`` 只表示 QEMU 会生成 DTB，并把 DTB 放到
``fw_jump`` 约定的位置；外部 ``fw_jump.bin`` 是否最终使用这份 DTB，仍然
取决于 OpenSBI 自身的编译参数和平台代码。

OpenSBI ``fw_jump`` 的通用逻辑如下：

* 如果编译时定义了 ``FW_JUMP_FDT_ADDR``，Linux 的 ``a1`` 固定使用该地址。
* 如果编译时定义了 ``FW_JUMP_FDT_OFFSET``，Linux 的 ``a1`` 固定使用
  ``FW_TEXT_START + FW_JUMP_FDT_OFFSET``。
* 只有两者都没有定义时，``fw_jump`` 才会把 QEMU 入口传入的 ``a1`` 原样
  传给 Linux。

OpenSBI 启动早期还会尝试把入口 ``a1`` 指向的 DTB 搬运到
``fw_next_arg1()`` 返回的目标地址。因此 QEMU 的 ``fw-jump-fdt-addr`` 是
OpenSBI 入口看到的 FDT 源地址；OpenSBI 打印的 ``Next Arg1`` 是传给 Linux
的目标地址，两者可以不同，只要固件能从入口 ``a1`` 正确搬运 DTB。

如果 ``fw_jump.bin`` 内部打包了旧 DTB，或者 KMH 平台代码固定从内嵌 DTB
地址、固定 ``FDT_ADDR``、或其它编译时地址读取设备树，那么即使 QEMU 设置了
``generated-dtb=on``，这份固件也可能绕开 QEMU 生成的 DTB。可以用下面的
命令检查固件里是否存在内嵌 DTB magic：

.. code-block:: bash

   $ grep -oba $'\xd0\x0d\xfe\xed' /path/to/fw_jump.bin

如果这里有输出，需要确认该 ``fw_jump.bin`` 的平台代码是否真的使用入口
``a1`` 或 OpenSBI scratch 中的 ``next_arg1`` 作为 FDT 来源。

Use DWC PCIe
~~~~~~~~~~~~

打开 ``dw-pcie=on`` 后，QEMU 会创建 DWC PCIe RC0 设备。如果当前使用
QEMU 生成的设备树，设备树会加入 ``/soc/pcie@32000000``，compatible 为
``snps,dw-pcie``。当前实现先建模最小可用 PCIe host：

* DBI window：``0x32000000``，参考 KMH DTS 的 ``dbi`` reg。
* config window：``0x67ff0000``，大小 ``0x10000``。
* 低 MMIO window：CPU ``0x60000000..0x67feffff`` 映射到 PCI
  ``0x40000000..0x47feffff``。
* MSI 使用生成 DTB 中的 ``msi-parent = <&imsics_s>``，也就是 RISC-V
  IMSIC 外部 MSI 域。
* 如果同时打开 ``iommu-sys=on``，PCIe 节点会加入指向 system IOMMU 的
  ``iommu-map``。

QEMU DWC root port 的下游 bus 名为 ``dw-pcie``。例如挂一个 virtio PCIe
网卡：

.. code-block:: bash

   $ ./build/qemu-system-riscv64 \
       -M xiangshan-kunminghu,generated-dtb=on,dw-pcie=on \
       -smp 4 -m 16G -nographic \
       -bios /path/to/fw_jump.bin \
       -device loader,file=/path/to/Image,addr=0x80400000 \
       -netdev user,id=n0 \
       -device virtio-net-pci,netdev=n0,bus=dw-pcie

启动日志中预期可以看到类似输出：

.. code-block:: text

   dw-pcie 32000000.pcie: PCI host bridge to bus 0000:00
   pci 0000:01:00.0: [1af4:1041] type 00 class 0x020000 PCIe Endpoint

当前生成 DTB 按 ``kmh-v2-synps-pcie.dtsi`` 保留 ``interrupts = <12>, <13>``
和 ``interrupt-names = "msi", "hp"``，但暂不生成 PCI legacy INTx
``interrupt-map``。因此 ``pcieport ... of_irq_parse_pci: failed`` 这类 legacy
INTx 解析日志是预期现象；优先使用 MSI/MSI-X 设备。真实 DTS 中其它 RC 和
64-bit high MMIO window 还没有在 QEMU 生成 DTB 中打开。

Run autotest with pmem/nvdimm
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

打开 ``autotest-dtb=on`` 后，QEMU 会在生成的设备树中加入：

* trigger reserved-memory：默认 ``0x90000000``，大小 ``0x200000``。
* rootfs pmem/nvdimm：默认 ``0x3c0000000``，大小 ``0x20000000``。
* workload pmem/nvdimm：默认 ``0x3e0000000``，大小 ``0x80000000``。
* ``/memory`` 会避开 rootfs 和 workload pmem 区间，避免 Linux 把它们当作
  System RAM 使用。

QEMU 不生成 ``task=`` bootargs。trigger 文件仍然需要用 loader 放到
``autotest-trigger-addr``，OpenSBI 会解析 trigger 并对设备树做 overlay。

默认自动测试命令：

.. code-block:: bash

   $ ./build/qemu-system-riscv64 \
       -M xiangshan-kunminghu,autotest-dtb=on \
       -smp 4 -m 16G -nographic \
       -bios /path/to/fw_jump.bin \
       -device loader,file=/path/to/Image,addr=0x80400000 \
       -device loader,file=/path/to/rootfs.ext4,addr=0x3c0000000 \
       -device loader,file=/path/to/workload.ext4,addr=0x3e0000000 \
       -device loader,file=/path/to/test.trigger,addr=0x90000000

启动后预期现象：

* OpenSBI 打印 ``Next Address=0x80400000``，并给 Linux 传入有效的
  ``Next Arg1``。
* Linux command line 中出现 OpenSBI 根据 trigger overlay 后的 ``task=...``。
* rootfs 通过 ``/dev/pmem0`` 挂载。
* workload/test data 通过 ``/dev/pmem1`` 挂载。
* 自动测试脚本从 trigger 指向的 task 配置启动测试。

Change autotest addresses and sizes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

如果要改变 rootfs、workload 或 trigger 的加载地址，需要对应的 machine
属性和 loader 地址保持一致：

* rootfs：``autotest-rootfs-addr`` 对应 rootfs ext4 的 loader 地址。
* workload：``autotest-workload-addr`` 对应 workload ext4 的 loader 地址。
* trigger：``autotest-trigger-addr`` 对应 trigger 文件的 loader 地址。

如果对应文件大小超出默认保留区间，可以再配置大小属性；不配置时使用默认
大小。``autotest-rootfs-size`` 和 ``autotest-workload-size`` 同时影响
pmem/nvdimm 节点和 reserved-memory 区间，``autotest-trigger-size`` 只影响
trigger reserved-memory 区间。

例如把 rootfs 放到 ``0x3c0000000``，workload 放到 ``0x400000000``，
trigger 放到 ``0x90000000``：

.. code-block:: bash

   $ ./build/qemu-system-riscv64 \
       -M xiangshan-kunminghu,autotest-dtb=on,autotest-rootfs-addr=0x3c0000000,autotest-workload-addr=0x400000000,autotest-trigger-addr=0x90000000 \
       -smp 4 -m 16G -nographic \
       -bios /path/to/fw_jump.bin \
       -device loader,file=/path/to/Image,addr=0x80400000 \
       -device loader,file=/path/to/rootfs.ext4,addr=0x3c0000000 \
       -device loader,file=/path/to/workload.ext4,addr=0x400000000 \
       -device loader,file=/path/to/test.trigger,addr=0x90000000

上面 rootfs 和 trigger 使用的是默认地址，所以实际命令里可以省略
``autotest-rootfs-addr`` 和 ``autotest-trigger-addr``。只有改成非默认地址时
才需要显式传入对应 machine 属性。大小属性同理，只有需要覆盖默认大小时
才需要显式传入。

Dump generated DTB
~~~~~~~~~~~~~~~~~~

调试设备树时可以导出 QEMU 生成的 DTB：

.. code-block:: bash

   $ ./build/qemu-system-riscv64 \
       -M xiangshan-kunminghu,autotest-dtb=on,dumpdtb=/tmp/kmh.dtb \
       -smp 4 -m 16G -nographic \
       -bios /path/to/fw_jump.bin \
       -device loader,file=/path/to/Image,addr=0x80400000

``generated-dtb=on`` 或 ``autotest-dtb=on`` 不能和 ``-dtb`` 同时使用。
如果需要完全使用外部设备树，请关闭 QEMU 生成 DTB 的路径，并确认固件和内核
都使用同一份 DTB。
