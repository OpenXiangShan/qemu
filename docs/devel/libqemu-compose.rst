宿主 C 模块库：组装 Kunminghu SoC
================================

目的与分层
----------

``libqemu/compose.h`` 提供类型化的宿主 C 接口，把 QOM 属性、realize、MMIO 映射、
IRQ 连接和 DTB 描述封装成设备模块。``examples/kunminghu.c`` 负责选择和连接模块，
``compose-kmh.c`` 提供 Kunminghu 地址、中断和启动布局的默认配置。

调用关系为::

   host / kunminghu.c
      -> compose.h: typed configs + opaque handles
      -> libqemu-compose.a
           compose.c       lifecycle, validation, device builders
           compose-dtb.c   DTB from successfully assembled modules
           compose-boot.c  raw firmware/Image/reset loader
           compose-kmh.c   Kunminghu defaults and convenience builder
      -> LibQemuExports / LibQemuCoreExports
      -> libqemu_call -> dispatch BH -> QEMU worker + BQL
      -> QOM, qdev, MemoryRegion, IRQ, ROM loader

模块库运行在调用它的宿主线程中，不新增 BH、main loop 或 vCPU 线程。
实际 QEMU 拓扑修改通过已有 core API 同步提交给 worker。
模块库自身的配置注册表不支持并发修改；在同一个宿主线程中顺序组装即可。

本层没有增加 ``LibQemuCoreExports`` 项目，也没有调用 Kunminghu machine 的板级
初始化函数。``core->rom_add_blob``、设备 QOM 属性等来自前一步组装能力扩展。
模块库仅在 ``--enable-libqemu`` 时构建，普通 QEMU 默认构建路径保持不变。

接口与模块边界
--------------

所有公开配置和函数集中在 ``libqemu/compose.h``；宿主不需要 GLib、libfdt 或
QEMU 内部类型来填写配置。实现链接 GLib 和 libfdt。

.. list-table:: 模块职责
   :header-rows: 1
   :widths: 30 40 30

   * - 接口
     - 负责内容
     - 依赖或输出
   * - ``libqemu_compose_init``
     - 校验 ABI；构造 ``-M none -m 0 -smp 1 -accel tcg`` 和后端参数
     - 进入 ASSEMBLY，输出 compose context
   * - ``libqemu_soc_new``
     - 保存 model/compatible，建立名称、地址、模块注册表
     - 一个 context 对应一个 SoC
   * - ``libqemu_soc_add_memory``
     - 创建 RAM/ROM，映射到系统内存
     - ``system_ram=true`` 指定 Linux DRAM 和启动加载区
   * - ``libqemu_riscv_cpu_create``
     - QOM CPU、resetvec、cache block 配置、系统内存 link、reset 注册
     - 输出 ``LibQemuCpu``
   * - ``libqemu_riscv_aia_create``
     - M/S IMSIC、M→S APLIC、CPU 外部及 guest IRQ 连接
     - 依赖 CPU，输出 ``LibQemuAia``
   * - ``libqemu_riscv_aclint_create``
     - MSWI、MTIMER、CPU 软件/定时器中断、rdtime
     - 依赖 CPU；CLINT 布局，时钟频率可配置
   * - ``libqemu_uart_create``
     - serial-mm、stdio/null chardev、MMIO、APLIC IRQ、可选 console
     - 依赖已声明的 chardev 和 AIA
   * - ``libqemu_my_virtio_blk_create``
     - my-virtio-blk 的原始磁盘后端、MMIO、APLIC IRQ
     - 依赖 AIA；镜像直接由 MyVirtio backend 打开
   * - ``libqemu_dwc_create``
     - DWC RC、总线名称、DBI、MSI、DMA 到系统内存、配置/BAR 窗口描述
     - 依赖 AIA，输出 ``LibQemuPcie``
   * - ``libqemu_nvme_attach``
     - 设置序列号和 drive，将 NVMe realize 到 RC 的下游总线
     - 依赖 PCIe handle 和 init 阶段声明的块后端
   * - ``libqemu_finisher_create``
     - SiFive test MMIO 和 poweroff/reboot DT 节点
     - 可选；用于退出/复位
   * - ``libqemu_soc_build_dtb``
     - 遍历成功模块，生成 reg、interrupts、phandle、chosen、CPU ISA
     - 返回独立拥有的 ``LibQemuDtb``
   * - ``libqemu_riscv_boot_load``
     - 预检查 raw 固件、Image、DTB 和 reset ROM；登记启动 blob
     - 成功后封闭模块拓扑；随后调用 ``finish_qemu_init``

AIA 是一个有连接约束的子系统模块：外设物理 IRQ 接到 **M APLIC**，设备树中的
``interrupt-parent`` 指向 **S APLIC**；OpenSBI 根据 M→S delegation 描述配置委派。
``num_ids`` 不包含 IMSIC 的保留 ID 0，模块为 QOM 的 ``num-irqs`` 加 1。
``guest_files=5`` 表示 S 文件之外的五个 guest 文件，guest stride 为 8 页，
设备树 ``riscv,guest-index-bits`` 为 3。实际设备映射六页，注册表保留整个 stride。

DWC 和 NVMe 分离，创建 RC 不要求创建 NVMe。``libqemu_pcie_bus()`` 返回借用的
core 总线句柄，可用于通过原始 core API 挂接其他 PCI 设备。模块配置给出 CPU BAR
窗口和 PCI bus 地址，Linux DWC 驱动通过 DT ``ranges`` 配置 iATU。
``msi_irq`` 连接 DWC 的 MSI 输出；NVMe MSI-X 通过 DMA 写 S IMSIC。
当前内核的 DWC 平台驱动还要求第二个 ``hp`` IRQ 条目，``hp_irq`` 为此保留且参与
冲突检查；QEMU 模型没有对应热插拔事件输出。本模块未封装 INTx 或 IOMMU。

两类磁盘后端有不同边界：NVMe 使用 ``LibQemuComposeBackend``，在 init 前生成
``-drive if=none,id=...``；my-virtio-blk 使用 ``LibQemuVirtioBlkConfig.image``，
在设备 realize 时由 MyVirtio backend 打开。二者应使用不同的可写镜像副本。

源码拼接方式
------------

完整可编译示例为 ``libqemu/examples/kunminghu.c``。它保留显式组装顺序，主要部分为::

   libqemu_soc_new(compose, &p.soc, &soc, &error);
   libqemu_soc_add_memory(soc, &p.dram, &error);
   libqemu_soc_add_memory(soc, &p.rom, &error);
   libqemu_soc_add_memory(soc, &p.sram, &error);
   libqemu_soc_add_memory(soc, &p.flash, &error);
   libqemu_riscv_cpu_create(soc, &p.cpu, &cpu, &error);
   libqemu_riscv_aia_create(soc, cpu, &p.aia, &aia, &error);
   libqemu_riscv_aclint_create(soc, cpu, &p.aclint, &error);
   libqemu_uart_create(soc, &p.uart, aia, &error);
   libqemu_my_virtio_blk_create(soc, &p.blk, aia, &error);
   libqemu_dwc_create(soc, &p.pcie, aia, &pcie, &error);
   libqemu_nvme_attach(pcie, &p.nvme, &error);
   libqemu_finisher_create(soc, &p.finisher, &error);

以上摘录省略错误处理；完整示例逐项检查返回值，任一失败立即停止组装。
``kmh_soc_build()`` 提供同样的默认组合。``kmh_profile_init_defaults()``
只填写配置，不创建 QEMU 对象；创建前可以改地址、IRQ、RAM 大小和可选模块开关。

例如，将 UART 移到另一个地址，只需改配置::

   p.uart.base = 0x310c0000;
   p.uart.irq = 20;

模块的 MMIO 映射、DT 节点 unit address、``reg``、``interrupts`` 和 console 的
``stdout-path`` 都使用该配置。直接省略某个可选模块调用后，DTB 不会保留它的节点。
不需要同步维护一份手写 DTS；``examples/kunminghu.dts`` 只作为固定参考拓扑。

后端必须在 ``libqemu_compose_init()`` 前声明。默认 init 不隐式创建 chardev，
示例通过 ``LibQemuComposeChardev { "uart0", LIBQEMU_CHAR_STDIO }`` 声明串口，
通过 ``LibQemuComposeBackend`` 声明 ``nvmedisk``。这样错误的后端 ID 可在
创建相应设备之前检出。

完成组装后的调用顺序为::

   libqemu_soc_build_dtb(soc, p.boot.bootargs, &dtb, &error);
   libqemu_dtb_write(dtb, "/tmp/assembled.dtb", &error);  /* 可选 */
   libqemu_riscv_boot_load(soc, &p.boot, dtb, &error);
   libqemu_dtb_free(dtb);
   api->finish_qemu_init(&error);
   api->wait(&status, &error);
   libqemu_compose_free(compose);

默认引导使用 RV64 ``fw_dynamic.bin`` 和 raw Linux ``Image``，无 ELF/initrd loader。
reset trampoline 设置 a0=hartid、a1=DTB、a2=OpenSBI dynamic info。
CPU/vCPU/main loop 的启动和执行仍由 libqemu/QEMU 负责。

检查、所有权和失败处理
----------------------

* 名称在 SoC 内唯一；地址/窗口必须 4 KiB 对齐，不得溢出或相互重叠。
  注册表检查固件可见的预留窗口，部分窗口大于当前设备实际响应的寄存器范围。
* APLIC IRQ 必须在 1..num_sources 范围内，同一物理源不能被两个模块占用。
  已用的 chardev、NVMe backend 和 PCI domain 不能重复分配。
* 一个 CPU、一个 M/S AIA 子系统、一个 ACLINT、一个 system RAM 和一个 console。
  UART/PCIe/块设备的模块 API 可重复调用，但本轮 Linux 验收只覆盖默认单 RC、单 NVMe、
  单 blk；专项测试覆盖两个 UART 的创建以及移动后的 RC 描述。
* DTB 来自实际成功创建的模块。创建更多模块后必须重建 DTB；boot loader 会拒绝旧版本。
  libfdt 插入节点会移动后续节点 offset，因此各 emitter 通过路径重新定位父节点，
  不跨插入操作保留兄弟节点 offset。
* boot loader 先读完两个文件，再检查 DRAM 边界、文件重叠、reset ROM 和对齐。
  Linux Image 头存在时，使用 ``image_size`` 检查包含 BSS 的内存占用；raw 固件的
  运行时 scratch 空间由调用者按固件构建布局预留。通过所有检查后才登记 ROM blob。
* 配置字符串在创建时复制。``LibQemuSoc/Cpu/Aia/Pcie`` handle 和模块元数据归
  compose context；DTB 独立拥有；core handle 仍归 QEMU runtime。
* helper 返回的错误通过 ``libqemu_compose_error_free()`` 释放；直接 ``api->...``
  返回的错误通过 ``api->error_free()`` 释放。调用时传入 NULL 初始化的 ``char *error``。
* 检出的参数错误尚未修改该模块，可修正后重试；QEMU 创建/realize 错误可能留下部分
  对象，此时 context 标记失败，应 ``request_shutdown -> wait -> compose_free``。
  模块库不承诺事务回滚，也不能隔离 QEMU 深层 fatal/exit 错误。
* ``compose_free()`` 只释放宿主元数据，不停止 QEMU，不销毁其全局 runtime。
  仍是每进程一次初始化，不支持卸载动态库或在同一进程重建实例。

扩展新设备
----------

复用既有模块时，用户只填写配置并调用创建函数。封装新类别设备时，在 ``compose.h``
添加配置/handle，在 ``compose.c`` 添加创建函数和校验，再在 ``compose-dtb.c``
增加对应 emitter。配置、映射和 DT 描述使用同一份成功模块记录。

默认 board policy 放在 ``compose-kmh.c``，不要把 Kunminghu 固定地址写进通用模块。
CPU/IRQ/MSI 等固定连接语义留在相应设备模块。若 QOM 没有暴露某个必要属性，应在
设备模型中补充可选属性并保持默认行为，之后再由 core API 设置。

也可以通过 ``libqemu_cpu_object()``、``libqemu_pcie_bus()``、
``libqemu_soc_machine()`` 取得 core handle，自行扩展。直接 core 创建的对象不会自动
加入模块注册表，地址检查、DTB 和旧 DTB 检测也不会覆盖它们；如需这些功能，应添加
正式模块。当前 DTB 数据访问接口是只读的，不提供外部 DTS overlay 接口。

构建与验证
----------

配置和运行命令见 ``libqemu/examples/README.kunminghu.md``。安装提供：

* ``include/libqemu/libqemu.h``、``include/libqemu/compose.h``
* ``libqemu-compose.a``、``pkgconfig/libqemu-compose.pc``
* 独立通过 dlopen 加载的 ``libqemu-system-riscv64.so``

compose 是源码级接口，应配套头文件和静态库重新链接。已有 core ABI 表不因本层改变。
当前支持 TCG、RV64 hart 0；``-M none`` 的 max_cpus=1 限制未修改。
没有 QBox、Lua、SystemC、热插拔框架、多实例、IOMMU、ACPI 或其他 my-virtio 模块。

``tests/libqemu/compose.c`` 验证真实 ABI 下的模块创建、移动的 UART/AIA/ACLINT/RC、
phandle 引用、可选节点缺省、重复名称/IRQ、无效 backend、文件和加载区域检查，以及
profile convenience builder。无需 Linux/磁盘镜像，也不要求启用 my-virtio。

``tests/libqemu/kunminghu.py`` 启动完整的宿主拼接示例，验证 Linux、my-virtio-blk、
DWC/NVMe、随机写入、卸载/重挂、SHA256 读回、中断计数增长、wait 后磁盘 fd 关闭
和宿主 e2fsck。它只写磁盘副本。``--normal`` 对默认四核原 machine 执行相同回归。
``tests/libqemu/check.py`` 覆盖整机 ``start`` 和 ``--split-init`` 启动方式。
