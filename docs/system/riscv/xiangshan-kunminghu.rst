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
* Optional my-virtio MMIO blk/net/console/gpu/input devices

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

``generated-acpi=on|off``
   是否生成 ACPI 表并把它们写入 DDR handoff 区域，默认 ``off``。打开后
   QEMU 会生成 RSDP、XSDT、FADT、DSDT、MADT、RHCT 和 SPCR 等基础表。
   如果同时使用 QEMU 生成设备树，设备树会加入 compatible 为
   ``bosc,kmh-acpi-handoff`` 的 reserved-memory 节点，固件可通过该节点
   找到 ACPI handoff 区域。当前路径不使用 ``fw_cfg``，需要 UEFI 从 DDR
   handoff 区域安装 ACPI 表。

``acpi-handoff-addr=<addr>``
   generated ACPI handoff 区域的 DDR 基地址，默认 ``0x90200000``。该地址
   必须 16-byte 对齐，并且落在 guest DDR 范围内。

``acpi-handoff-size=<size>``
   generated ACPI handoff 区域大小，默认 ``0x20000``。该大小必须 16-byte
   对齐，并且能够容纳 QEMU 生成的 ACPI blob。

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

``my-virtio-blk=on|off``
   是否创建基于 libMyVirtio 的 virtio-mmio blk 设备，默认 ``off``。
   如果 QEMU 当前使用生成的设备树，打开后会加入
   ``/soc/my_virtio_blk@310a0000``。blk backend 使用
   ``my-virtio-blk-image`` 指定的 raw image 文件。

``my-virtio-blk-image=<path>``
   my-virtio blk 后端镜像路径，默认 ``disk.img``。建议运行时使用绝对路径。

``my-virtio-net=on|off``
   是否创建基于 libMyVirtio 的 virtio-mmio net 设备，默认 ``off``。
   如果 QEMU 当前使用生成的设备树，打开后会加入
   ``/soc/my_virtio_net@31090000``。net backend 使用 libslirp user-mode
   网络，不需要 ``/dev/net/tun`` 或 ``tap0`` 权限。

``my-virtio-net-hostfwd=<rules>``
   my-virtio net 的 libslirp hostfwd 规则，默认空字符串表示不占用宿主端口。
   格式示例：``tcp:0.0.0.0:2222:10.0.2.15:22``。
   也可以省略 host bind IP 和 guest IP，例如 ``tcp::2222::22``；
   host bind IP 默认 ``0.0.0.0``，guest IP 默认使用当前
   ``my-virtio-net-dhcp-start``。

``my-virtio-net-network=<ipv4>``
   my-virtio net 的 libslirp IPv4 network，默认使用 backend 默认值
   ``10.0.2.0``。

``my-virtio-net-netmask=<ipv4>``
   my-virtio net 的 libslirp IPv4 netmask，默认使用 backend 默认值
   ``255.255.255.0``。

``my-virtio-net-host-ip=<ipv4>``
   my-virtio net 的 libslirp host IPv4 地址，默认使用 backend 默认值
   ``10.0.2.2``。

``my-virtio-net-dhcp-start=<ipv4>``
   my-virtio net 的 libslirp DHCP 起始地址，默认使用 backend 默认值
   ``10.0.2.15``。这个地址也是 hostfwd 规则省略 guest IP 时使用的默认值。

``my-virtio-net-dns-ip=<ipv4>``
   my-virtio net 的 libslirp DNS IPv4 地址，默认使用 backend 默认值
   ``10.0.2.3``。

``my-virtio-console=on|off``
   是否创建基于 libMyVirtio 的 virtio-mmio console 设备，默认 ``off``。
   如果 QEMU 当前使用生成的设备树，打开后会加入
   ``/soc/my_virtio_console@31080000``，并把 ``stdout-path`` 指到该节点。
   如果没有外部 command line，QEMU 会给生成 DTB 设置
   ``console=hvc1 earlycon=sbi vt.nr_consoles=6`` 这一类默认 bootargs。
   没有打开 ``my-virtio-console`` 时，默认 bootargs 使用 UART0：
   ``console=ttyS0,115200 earlycon=sbi loglevel=8``。
   该设备使用 QEMU 第三路 ``-serial`` 后端，也就是 ``serial_hd(2)``。
   ``hvc0`` 保留给 SBI HVC/earlycon；当前 OpenSBI virtio-console 只实现
   ``putc``，``getc`` 返回 ``-1``，所以登录控制台需要绑定到 Linux
   virtio-console 驱动注册出来的 ``hvc1``。

``my-virtio-gpu=on|off``
   是否创建基于 libMyVirtio 的 virtio-mmio GPU 设备，默认 ``off``。
   如果 QEMU 当前使用生成的设备树，打开后会加入
   ``/soc/my_virtio_gpu@310c0000``。GPU backend 默认创建一个独立的
   VNC server，不使用 QEMU 原生 ``-vnc``。

``my-virtio-vnc-listen=<host:port>``
   my-virtio GPU backend VNC server 的监听地址，默认
   ``127.0.0.1:5915``。打开默认 VNC input backend 时，keyboard、mouse
   和 tablet 复用这个 VNC server 收到的输入事件，因此需要同时打开
   ``my-virtio-gpu=on``。

``my-virtio-keyboard=on|off``
   是否创建标准 virtio-input keyboard 设备，默认 ``off``。如果 QEMU 当前
   使用生成的设备树，打开后会加入 ``/soc/my_virtio_keyboard@310d0000``。
   guest Linux 需要启用 ``CONFIG_VIRTIO_INPUT``。

``my-virtio-mouse=on|off``
   是否创建标准 virtio-input relative mouse 设备，默认 ``off``。如果 QEMU
   当前使用生成的设备树，打开后会加入
   ``/soc/my_virtio_mouse@310e0000``。这个设备上报 ``REL_X``、``REL_Y``
   和鼠标按键，适合需要相对移动输入的场景。

``my-virtio-tablet=on|off``
   是否创建标准 virtio-input absolute tablet 设备，默认 ``off``。如果 QEMU
   当前使用生成的设备树，打开后会加入
   ``/soc/my_virtio_tablet@310f0000``。这个设备上报 ``ABS_X``、``ABS_Y``
   和鼠标按键，更适合 Linux 桌面这类绝对光标场景。

``my-virtio-keyboard-backend=vnc|evdev|ui``、``my-virtio-mouse-backend=vnc|evdev|ui``、``my-virtio-tablet-backend=vnc|evdev|ui``
   my-virtio input 的宿主输入来源，默认都是 ``vnc``。``vnc`` 和 ``ui``
   都表示从 my-virtio GPU backend VNC server 读取输入事件；``evdev``
   表示直接读取宿主机 Linux ``/dev/input/eventX``。

``my-virtio-keyboard-evdev=<path>``、``my-virtio-mouse-evdev=<path>``、``my-virtio-tablet-evdev=<path>``
   当对应 input backend 设置为 ``evdev`` 时使用的宿主 evdev 节点路径。
   如果不指定路径，backend 会扫描 ``/dev/input/event*``。直接访问宿主
   evdev 通常需要 root 权限，或者当前用户属于有读权限的 ``input`` 组。

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

my-virtio 设备默认不参与编译。如果需要使用 ``my-virtio-*`` machine 属性，
configure 时需要显式加 ``--enable-my-virtio``：

.. code-block:: bash

   $ ./configure --target-list=riscv64-softmmu --disable-werror --disable-docs --enable-my-virtio
   $ ninja -C build qemu-system-riscv64

没有打开该选项时，运行时请求 ``my-virtio-*=on`` 会直接报错：
``my-virtio support is not compiled in; reconfigure QEMU with --enable-my-virtio``。

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
一起使用。bootargs 来自 QEMU 生成的设备树或固件对设备树的修改。使用
QEMU 生成设备树且没有外部 command line 时，默认串口 bootargs 为
``console=ttyS0,115200 earlycon=sbi loglevel=8``；如果同时打开
``my-virtio-console=on``，默认改为 ``console=hvc1 earlycon=sbi``。

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

Boot with generated ACPI tables
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``generated-acpi=on`` 会让 QEMU 在 DDR 中生成 ACPI handoff blob。UEFI 需要
从设备树中的 ``bosc,kmh-acpi-handoff`` 节点读取 handoff 地址和大小，扫描
RSDP，并通过 ``EFI_ACPI_TABLE_PROTOCOL`` 安装这些 ACPI 表。该机制仍需要
OpenSBI 正常把 FDT 传给 UEFI，因为 handoff 地址本身通过 FDT 描述。

使用 ``fw_jump.bin`` 启动 UEFI 并打开 generated ACPI 的示例：

.. code-block:: bash

   $ ./build/qemu-system-riscv64 \
       -M xiangshan-kunminghu,generated-dtb=on,generated-acpi=on \
       -smp 4 -m 16G -nographic \
       -bios /path/to/fw_jump.bin \
       -device loader,file=/path/to/uefi.fd,addr=0x80200000

如果固件没有使用 QEMU 生成的 DTB，仍可以通过外部 DTB 描述同一个 handoff
区域；该 DTB 需要包含 compatible 为 ``bosc,kmh-acpi-handoff`` 的节点，且
``reg`` 与 ``acpi-handoff-addr``、``acpi-handoff-size`` 一致。handoff 地址
或大小配置错误时，QEMU 会在启动早期报错。

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

Use my-virtio MMIO devices
~~~~~~~~~~~~~~~~~~~~~~~~~~

``my-virtio-blk``、``my-virtio-net``、``my-virtio-console``、
``my-virtio-gpu`` 和 ``my-virtio-*`` input 的 machine 属性只控制是否创建
设备。设备树仍然由 ``-kernel``、``generated-dtb=on`` 或
``autotest-dtb=on`` 这些已有路径决定；使用外部 ``-dtb`` 时，需要外部设备树
自己描述已打开的 my-virtio 设备。

当前 my-virtio MMIO 资源固定分配如下：

.. list-table::
   :header-rows: 1

   * - 设备
     - DTB 节点
     - MMIO
     - IRQ
   * - console
     - ``/soc/my_virtio_console@31080000``
     - ``0x31080000``
     - ``17``
   * - net
     - ``/soc/my_virtio_net@31090000``
     - ``0x31090000``
     - ``16``
   * - blk
     - ``/soc/my_virtio_blk@310a0000``
     - ``0x310a0000``
     - ``15``
   * - gpu
     - ``/soc/my_virtio_gpu@310c0000``
     - ``0x310c0000``
     - ``18``
   * - keyboard
     - ``/soc/my_virtio_keyboard@310d0000``
     - ``0x310d0000``
     - ``19``
   * - mouse
     - ``/soc/my_virtio_mouse@310e0000``
     - ``0x310e0000``
     - ``20``
   * - tablet
     - ``/soc/my_virtio_tablet@310f0000``
     - ``0x310f0000``
     - ``21``

blk 设备需要通过 ``my-virtio-blk-image`` 指定 raw image 文件：

.. code-block:: bash

   $ ./build/qemu-system-riscv64 \
       -M xiangshan-kunminghu,generated-dtb=on,my-virtio-blk=on,my-virtio-blk-image=/path/to/disk.img \
       -smp 4 -m 16G -nographic \
       -bios /path/to/fw_jump.bin \
       -device loader,file=/path/to/Image-virtio,addr=0x80400000

virtio-console 使用第三路 ``-serial`` 后端。下面的命令会让 UART0、UART1 和
my-virtio-console 分别监听三个 TCP 端口；连接第三个端口可以看到 Linux
virtio-console 的 ``hvc1`` 输出：

.. code-block:: bash

   $ ./build/qemu-system-riscv64 \
       -M xiangshan-kunminghu,generated-dtb=on,my-virtio-console=on \
       -smp 4 -m 16G -nographic \
       -serial tcp:127.0.0.1:2234,server,nowait \
       -serial tcp:127.0.0.1:2235,server,nowait \
       -serial tcp:127.0.0.1:2236,server,nowait \
       -bios /path/to/fw_jump.bin \
       -device loader,file=/path/to/Image-virtio,addr=0x80400000

如果使用 ``fw_payload.bin``，bootargs 来自固件里打包的 DTB，需要确认该 DTB
同样使用 ``console=hvc1 earlycon=sbi``。旧的 ``console=hvc0`` 会把登录输入
绑定到 SBI HVC，而当前 OpenSBI 的 virtio-console ``getc`` 不会从 virtio RX
queue 取字符。

使用 ``Image-virtio`` 时还要确认 OpenSBI 的 FDT 位置没有覆盖内核镜像。当前
generic OpenSBI 常见配置 ``FW_JUMP_FDT_OFFSET=0x2200000`` 会让传给 Linux 的
FDT 落在 ``0x82200000``；如果 Image 从 ``0x80400000`` 加载且镜像约 60MiB，
这个位置会落在 Image/initramfs 区间内，Linux 会在解压 rootfs 时报
``uncompression error``。这种情况下需要重编 ``fw_jump.bin``，把
``FW_JUMP_FDT_OFFSET`` 或 ``FW_JUMP_FDT_ADDR`` 放到 Image 之后，或者调整
Image/FDT 的加载地址组合。

blk 和 console 可以一起打开：

.. code-block:: bash

   $ ./build/qemu-system-riscv64 \
       -M xiangshan-kunminghu,generated-dtb=on,my-virtio-blk=on,my-virtio-console=on,my-virtio-blk-image=/path/to/disk.img \
       -smp 4 -m 16G -nographic \
       -serial tcp:127.0.0.1:2234,server,nowait \
       -serial tcp:127.0.0.1:2235,server,nowait \
       -serial tcp:127.0.0.1:2236,server,nowait \
       -bios /path/to/fw_jump.bin \
       -device loader,file=/path/to/Image-virtio,addr=0x80400000

``my-virtio-net=on`` 会创建 net 设备并在生成 DTB 中加入
``/soc/my_virtio_net@31090000``。如需从宿主机转发 SSH 到 guest，可以加上：

.. code-block:: bash

   -M xiangshan-kunminghu,generated-dtb=on,my-virtio-net=on,my-virtio-net-hostfwd=tcp:0.0.0.0:2222:10.0.2.15:22

也可以让 hostfwd 的 guest IP 跟随当前 DHCP 配置：

.. code-block:: bash

   -M xiangshan-kunminghu,generated-dtb=on,my-virtio-net=on,my-virtio-net-hostfwd=tcp::2222::22,my-virtio-net-network=10.10.0.0,my-virtio-net-netmask=255.255.255.0,my-virtio-net-host-ip=10.10.0.2,my-virtio-net-dhcp-start=10.10.0.15,my-virtio-net-dns-ip=10.10.0.3

``my-virtio-gpu=on`` 会创建 GPU 设备并在生成 DTB 中加入
``/soc/my_virtio_gpu@310c0000``。GPU backend 会启动独立 VNC server，
默认监听 ``127.0.0.1:5915``：

.. code-block:: bash

   $ ./build/qemu-system-riscv64 \
       -M xiangshan-kunminghu,generated-dtb=on,my-virtio-gpu=on,my-virtio-vnc-listen=127.0.0.1:5915 \
       -smp 4 -m 16G -nographic \
       -bios /path/to/fw_jump.bin \
       -device loader,file=/path/to/Image-virtio-gpu,addr=0x80400000

宿主机连接这个 backend VNC server：

.. code-block:: bash

   $ vncviewer 127.0.0.1:5915

默认 VNC input backend 复用 my-virtio GPU 创建的 VNC server，所以使用
``my-virtio-keyboard``、``my-virtio-mouse`` 或 ``my-virtio-tablet`` 的默认
输入后端时，需要同时打开 ``my-virtio-gpu=on``。guest 内核需要启用
``CONFIG_VIRTIO_GPU`` 和 ``CONFIG_VIRTIO_INPUT``。启动后可以检查：

.. code-block:: bash

   # ls -l /dev/fb0 /dev/input/event*
   # cat /proc/bus/input/devices
   # dmesg | grep -i 'virtio.*input\|virtio.*gpu'

Linux 桌面通常使用 keyboard + absolute tablet，这样 VNC 光标位置可以和
guest 桌面光标对齐：

.. code-block:: bash

   $ ./build/qemu-system-riscv64 \
       -M xiangshan-kunminghu,generated-dtb=on,my-virtio-gpu=on,my-virtio-keyboard=on,my-virtio-tablet=on,my-virtio-vnc-listen=127.0.0.1:5915 \
       -smp 4 -m 16G -nographic \
       -bios /path/to/fw_jump.bin \
       -device loader,file=/path/to/Image-virtio-gpu,addr=0x80400000

如果 guest 需要相对鼠标输入，可以使用 keyboard + relative mouse，不要同时
打开 tablet：

.. code-block:: bash

   $ ./build/qemu-system-riscv64 \
       -M xiangshan-kunminghu,generated-dtb=on,my-virtio-gpu=on,my-virtio-keyboard=on,my-virtio-mouse=on,my-virtio-vnc-listen=127.0.0.1:5915 \
       -smp 4 -m 16G -nographic \
       -bios /path/to/fw_jump.bin \
       -device loader,file=/path/to/Image-virtio-gpu,addr=0x80400000

如果不想通过 VNC 输入，也可以让 input backend 直接读取宿主机 evdev。下面
示例只演示 keyboard，mouse 和 tablet 使用同样的 ``*-backend=evdev`` 和
``*-evdev=`` 形式：

.. code-block:: bash

   $ ./build/qemu-system-riscv64 \
       -M xiangshan-kunminghu,generated-dtb=on,my-virtio-keyboard=on,my-virtio-keyboard-backend=evdev,my-virtio-keyboard-evdev=/dev/input/eventX \
       -smp 4 -m 16G -nographic \
       -bios /path/to/fw_jump.bin \
       -device loader,file=/path/to/Image-virtio,addr=0x80400000

宿主机 evdev 后端读取真实 ``/dev/input/eventX``，通常需要 root 权限或
``input`` 组读权限。VNC backend 不需要宿主机 evdev 权限。

Run autotest with pmem/nvdimm
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

打开 ``autotest-dtb=on`` 后，QEMU 会在生成的设备树中加入：

* trigger reserved-memory：默认 ``0x90000000``，大小 ``0x200000``。
* rootfs pmem/nvdimm：默认 ``0x3c0000000``，大小 ``0x20000000``。
* workload pmem/nvdimm：默认 ``0x3e0000000``，大小 ``0x80000000``。
* ``/memory`` 会避开 rootfs 和 workload pmem 区间，避免 Linux 把它们当作
  System RAM 使用。

autotest 不依赖 QEMU 解析 trigger。trigger 文件仍然需要用 loader 放到
``autotest-trigger-addr``，OpenSBI 会解析 trigger 并对设备树做 overlay。
如果同时打开 ``my-virtio-console=on``，QEMU 生成 DTB 的默认 bootargs 会
包含 ``task=`` 占位符，最终值仍由 OpenSBI overlay 更新。

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
       -M xiangshan-kunminghu,autotest-dtb=on,autotest-workload-addr=0x400000000 \
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
