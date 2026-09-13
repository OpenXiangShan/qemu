QEMU to io-system machine
=========================

``qemu_to_iosystem`` 是 XiangShan/Kunminghu RISC-V machine 的
io-system 协同仿真入口，支持 C-model 和 Picker/VCS RTL-system backend。
CPU、DRAM、CLINT、UART 和 IMSIC 仍由 QEMU 创建。默认 C-model 配置中，
APLIC 和首版 ``my-virtio-blk`` 由
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

RTL-system build and run
-----------------------

QEMU 在构建过程中调用 Picker/VCS，生成 RTL 模型和
``libio_rtl_system_api.so``，不需要先单独执行 io-system-lib 的 make。

准备 QEMU 的常规编译依赖、CMake、支持 C++20 的宿主编译器、配套的
Picker/模板/XSPComm，以及可用的 VCS/Verdi 和 license 环境。Picker 模板
必须支持 ``make build`` 和 ``make install``；开启 FSDB 时 Picker 还需
支持 ``-W fsdb``。先在当前 shell source 工具环境脚本，保证
``VCS_HOME``、``VERDI_HOME``、``LM_LICENSE_FILE`` 已设置。Arena 使用
VCS 2023.12 工具链，Verdi 应选择与之匹配的版本。

下面编译包含 E6 + RA 的 RTL-system。将示例中的 ``/path/to/...`` 替换为
实际绝对路径；``QEMU_BUILD`` 是使用者自行选择的构建输出目录，名称和
位置没有固定要求，不能与源码目录相同。示例通过 ``mkdir -p`` 创建它：

.. code-block:: bash

   QEMU_SRC=/path/to/qemu
   QEMU_BUILD=/path/to/build
   UC_ROOT=/path/to/unitychip

   mkdir -p "$QEMU_BUILD"
   cd "$QEMU_BUILD"
   "$QEMU_SRC/configure" \
       --target-list=riscv64-softmmu \
       --enable-io-system --enable-my-virtio \
       --disable-rust --disable-docs \
       -Dio_system_rtl_system=true \
       -Dio_system_rtl_arena=true \
       -Dio_system_iommu_rtl=false \
       -Dio_system_rtl_system_picker_out="$QEMU_BUILD/rtl-system-picker" \
       -Dio_system_picker="$UC_ROOT/bin/picker" \
       -Dio_system_picker_template="$UC_ROOT/share/picker/template" \
       -Dio_system_picker_xspcomm_include="$UC_ROOT/share/picker/include" \
       -Dio_system_picker_xspcomm_lib="$UC_ROOT/share/picker/lib/libxspcomm.so" \
       -Dio_system_rtl_wave=false \
       -Dio_system_rtl_wave_file="$QEMU_BUILD/arena.fsdb"
   IO_SYSTEM_BUILD_JOBS=8 make -j16 qemu-system-riscv64

``UC_ROOT`` 指向已安装的 Picker、模板和 XSPComm 所在目录。
QEMU 和生成的 RTL 模型写入 ``QEMU_BUILD``。四个 ``io_system_picker*``
路径也可以分别指定，不要求工具安装一定采用上述目录布局。

``io_system_rtl_system=true`` 启用 RTL-system 动态库构建；
``io_system_rtl_arena=true`` 在模型中加入 E6 + RA。只需要不含 Arena 的
基础 RTL-system 时，将后者改为 ``false``。``io_system_iommu_rtl`` 是
另一项独立构建选择，运行 E6 + RA 算子并不需要开启它。
``IO_SYSTEM_BUILD_JOBS`` 控制 Picker 内部并行度，外层 ``make -j16``
控制 QEMU 构建并行度。

主要产物位于：

.. code-block:: text

   $QEMU_BUILD/qemu-system-riscv64
   $QEMU_BUILD/libio_rtl_system_api.so
   $QEMU_BUILD/rtl-system-picker/libDPIio_system_rtl_wrapper.so
   $QEMU_BUILD/rtl-system-picker/libUTio_system_rtl_wrapper.so
   $QEMU_BUILD/rtl-system-picker/libxspcomm.so
   $QEMU_BUILD/rtl-system-picker/libDPIio_system_rtl_wrapper.so.daidir/

运行时保留 API 库及整个 ``rtl-system-picker`` 目录。编译启用 RTL-system
不会自动改变 machine 的默认 backend，启动时仍须选择
``io-system-backend=rtl-system``。

以下示例直接运行已编译的 Add 联合 ELF。``RA_ELF`` 必须同时包含 QEMU
主核启动程序和 E6 程序，不能填只有 E6 代码的子镜像。在当前算子工程中
对应 ``e6_ra_subsystem/rvv_operator_cases/build/add_rvv/rvv_add_hardware.elf``。
若更换 shell，先重新加载相同工具环境，并设置 ``QEMU_BUILD``。

.. code-block:: bash

   RA_ELF=/path/to/rvv_add_hardware.elf
   RTL_PICKER="$QEMU_BUILD/rtl-system-picker"

   LD_LIBRARY_PATH="$RTL_PICKER:$QEMU_BUILD:$VCS_HOME/linux64/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
   LD_PRELOAD="$RTL_PICKER/libDPIio_system_rtl_wrapper.so:$RTL_PICKER/libUTio_system_rtl_wrapper.so${LD_PRELOAD:+:$LD_PRELOAD}" \
   "$QEMU_BUILD/qemu-system-riscv64" \
       -M "qemu_to_iosystem,generated-dtb=off,io-system-backend=rtl-system,io-system-iommu=none,io-system-backend-lib=$QEMU_BUILD/libio_rtl_system_api.so,io-system-service-mode=bh,io-system-rtl-continuous=on,io2q-outstanding=16" \
       -smp 1 -m 128M -accel tcg,thread=single \
       -display none -monitor none -serial stdio \
       -bios none -kernel "$RA_ELF"

``io-system-backend-lib`` 指定要 ``dlopen`` 的 API 库；省略时也可通过
``IO_RTL_SYSTEM_API_SO`` 指定。QEMU 当前不会自动按可执行文件
目录查找这个库，因此以上示例显式给出路径。Picker 的 DPI/UT 库需要在
QEMU 创建 vCPU 线程前通过 ``LD_PRELOAD`` 加载，以满足 VCS 的 TLS 要求。

``io-system-vcs-libdir`` machine 属性当前只被保存并传入
``IoSystemConfig.backend_vcs_libdir``，RTL-system loader 未使用它来加载
VCS 依赖，也不会据此设置 ``LD_LIBRARY_PATH``。启动 QEMU 前需要像上例
一样设置动态库环境。如果
source 的脚本已加入 ``$VCS_HOME/linux64/lib``，该目录无需重复添加，
但仍需要 Picker 库路径及 DPI/UT preload。只传这个 machine 属性不能
替代动态库搜索路径。

Add 应输出 ``RVV HOST START``、``RVV WAIT RA_DONE`` 和
``RVV PASS case=rvv_add_hardware``。该联合 ELF 的主核检查 E6 完成状态和
共享报告后执行 ``0x0005006b``，成功时 QEMU 自动退出且状态码为 0；
同时检查 PASS 日志和退出码。其他 ELF 是否自动退出取决于其主核程序。

需要 FSDB 时，把上述 configure 中的 ``io_system_rtl_wave=false`` 改为
``true`` 并重新构建。波形在运行期间写入 ``$QEMU_BUILD/arena.fsdb``，
正常 shutdown 时关闭；再次运行会覆盖同一路径。修改波形开关或路径会
触发模型重建。当前配置指纹记录 ``VCS_HOME``，尚未记录 ``VERDI_HOME``；
仅切换 Verdi 时应使用新的构建目录，确保重新链接所选版本的 PLI/dumper。

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

``io-system-rtl-continuous=on`` 用于包含自主执行 CPU/NPU 的 RTL SoC。
默认关闭；开启时必须同时使用 ``io-system-backend=rtl-system``、
``io-system-service-mode=bh`` 和 ``-smp 1``。首次 Q2IO MMIO 在 vCPU
线程构造 picker/VCS 后，每次 BH 投递最多 128 次单周期 service，每次
最多处理 16 个内存请求。即使当前没有 AXI 请求，下一批 RTL 时钟仍会
继续推进，因此主核 WFI 或停止轮询不会冻结 E6/AME。该选项不改变
``io_system_needs_service()`` 的事务语义。

QEMU 暂停时停止连续推进，恢复时重新调度；正常 shutdown 将 VCS
销毁工作投递到其所属 vCPU 线程。连续模式中的 service 错误会停止后续
推进并输出诊断，自动化测试还必须设置外部墙钟超时。这是功能协同仿真，
128 周期批次不表示 QEMU 主核与 RTL 的真实频率比例。

Bare-metal test exit instruction
-------------------------------

QEMU host harts enable the XiangShan ``0x0005006b`` debug instruction. In TCG,
executing ``.word 0x0005006b`` requests normal guest shutdown, including the
io-system shutdown notifier that closes picker/VCS on its owning thread.
This uses the existing RISC-V CPU implementation and requires no extra MMIO
device. It applies to the QEMU host CPU; the E6 CPU executes inside RTL.

An Arena host test must wait for E6's completion register, inspect the shared
report and finish printing its verdict before executing this instruction.
The instruction reads ``a0``: zero requests process exit status 0 (PASS),
and nonzero requests status 1 (FAIL). Automation must check the test verdict
as well as process exit. Older firmware that ends in WFI still needs an
external shutdown request.

E6/AME AXI-only acceptance test
-----------------------------

按上述流程构建 RA 版本动态库后，保留工具环境和 ``QEMU_BUILD``，从
QEMU 源码目录运行内置 GEMM 验收。脚本编译测试 ELF，需要
``riscv64-unknown-elf-gcc`` 等交叉工具，可通过 ``CROSS_COMPILE`` 指定前缀：

.. code-block:: bash

   python3 third_party/io-system-lib/tests/arena/run_qemu.py \
     --qemu "$QEMU_BUILD/qemu-system-riscv64" \
     --library "$QEMU_BUILD/libio_rtl_system_api.so" \
     --vcs-libdir "$VCS_HOME/linux64/lib" \
     --output "$QEMU_BUILD/acceptance/arena" \
     --suite --pause-resume

测试将主核、E6 和矩阵编译进同一个裸机 ELF，分别放在
``0x80000000``、``0x80010000``、``0x80020000``。QEMU 使用
``-bios none -device loader,file=ELF,cpu-num=0`` 将其装入唯一一份 RAM。
主核通过 ``0x50000000`` SYSCTRL 配置并启动 RTL E6，E6 发射原始 AME
指令，经 io-system AXI master 读写 QEMU RAM。第一组 GEMM 结果为
``[16, -18, 26, -2, 35, -17]``，第二组为 ``[-2, 1, -10, 7, 9, 6]``。

``--suite`` 覆盖两组输入和 outstanding 深度 1/4/16，另验证 30 秒不访问
RA、通过 QEMU 原生 CLINT 定时唤醒的 WFI，以及 E6 非法指令诊断。
``--pause-resume`` 在静默阶段暂停一秒后恢复。``--quiet-ticks`` 可调整
静默长度，CLINT 频率为 1 MHz；静默结束后的第一笔 RA 读取必须已经看到
完成状态。慢速仿真环境应明确增大该间隔，不能通过恢复轮询掩盖后台停钟。

运行脚本默认墙钟超时为 600 秒；结果检查包含六个数值、128 字节输出
哨兵及 E6 对主核判定的确认。成功标记来自 QEMU UART，不依赖 RA UART
或原 testbench mailbox。脚本用 QMP 退出，保存 ELF/BIN、编译和运行命令、
AXI trace、UART 日志、SHA256 与 JSON 结果；没有最终标记的进程退出不会
被判为成功。单独构建 ELF 可运行 ``tests/arena/build.py --output DIR``。

``tests/arena/test_continuous.py`` 可在 RA 动态库构建前单独验证 QEMU 调度。
它使用只计数的测试库检查默认模式、无轮询、WFI、暂停/恢复和销毁线程，
测试库不模拟 E6/AME，不能替代上述真实 RTL 数值验收。

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
``bosc-iommu-v2/third_party``。

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
