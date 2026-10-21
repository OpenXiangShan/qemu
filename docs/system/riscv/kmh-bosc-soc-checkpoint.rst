KMH BOSC portable checkpoint
============================

``kmh-bosc-soc`` 在 QEMU TCG 中制作 Zstd 切片，默认输出自包含 v3 镜像。
恢复时由宿主 QEMU 解压 RAM，各 hart 从 ``0x80000000`` 执行镜像内的
恢复程序。该程序使用真实 CSR/MMIO 指令恢复平台和 CPU，不依赖 QEMU
直接导入设备状态或在返回 guest 后补写 CSR。旧 v2 路径保留用于兼容。

接口与支持范围
--------------

* ``checkpoint-path=FILE``：开启保存并准备保留区。
* ``checkpoint-format=v3|v2``：保存格式，默认 v3；恢复时按文件版本识别。
* ``checkpoint-marker=on``：默认开启；``.word 0x0000006b`` 在其下一条指令
  保存。v3 要求所有选中 hart 处于 U/S 态；M 态使用 v2。
* ``checkpoint-interval=N``：执行计数到阈值后在下一 TB 边界保存，不是精确
  退休事件计数。恢复程序的指令不计入后续 interval。
* ``checkpoint-exit=on``：默认保存后退出；off 时继续执行，interval 文件名
  为 ``FILE.<count>.zst``，marker 原子替换 FILE。
* ``-bios FILE.zst``：v3 单文件恢复，无需外部 stub、固件、kernel、rootfs、
  workload、trigger 或 DTB。
* ``checkpoint-load=FILE``：也可显式恢复 v3，此时使用 ``-bios none``。
  v1/v2 切片仍须额外提供 ``checkpoint-stub=BIN``。v3 拒绝外部 stub 覆盖。

QMP 保存接口为：

.. code-block:: json

   {"execute":"x-xiangshan-checkpoint-save",
    "arguments":{"path":"/absolute/path/slice.zst"}}

要求 DDR 启动、TCG、默认 KMH CPU、VLEN=128。v3 支持非虚拟化 U/S 上下文，
M/S IMSIC 文件、MSI 模式 APLIC、静默 UART 和共享 ACLINT；拒绝活跃 VS
中断文件和无法软件重建的外部 IRQ 输入。my-virtio、PCIe、系统 IOMMU、
DMAC、SRAM、QSPI、ACPI 和 MCU 启动不在支持范围内。

single 使用 ``-smp 16,maxcpus=17``；multi 使用 ``-smp 64,maxcpus=68``，
应用核由 die/core mask 选择，不能用 ``-smp 1`` 替代固定 SoC 拓扑。
制作和恢复必须匹配 RAM、SoC 模式、die/core mask 及 CPU 配置。multi 的
DDR 槽间隔固定 32 GiB，每个选中槽容量为 ``-m / 4``，不随 die 数重分配。
原 Kunminghu 切片不是 BOSC 切片，platform ID 不兼容。

v3 的镜像布局与原生恢复
------------------------

输出仍是一个带校验和的 Zstd frame，解压长度恰好为 RAM backing 大小。
其中包含固件、Linux、PMEM 内容和实际 DT，以及以下恢复数据：

* ``0x80000000 .. 0x8000003f``：64 字节 stage0，关闭中断并分支到 stage1。
  原始 OpenSBI 对应字节保存在目录内，恢复时写回，不能永久覆盖 trap 代码。
* ``0x80300000 .. 0x803fffff``：stage1、私有栈和同步数据，必须保留。
* ``0x92000000 .. 0x923fffff``：192 字节 v3 header、section 表、架构寄存器
  操作、入口原字节备份和 DDR 段映射。
* 所选最高应用 die 的 DDR 尾部 64 MiB：CPU record，按物理
  ``mhartid * 1 MiB`` 索引。single 也采用尾部布局，避免多核记录覆盖 Image。

每个 DDR 槽至少 512 MiB，die0 DDR 必须存在以容纳固定复位入口和目录。
目录/header/section 采用 little-endian 与 CRC32C，CPU record ABI 保留 v2
字段偏移，不直接序列化 QEMU 的 C 结构体。架构操作记录为
``address/u64, value/u64, hart/u32, kind/u16, phase/u16``；MMIO 使用全局
物理地址，IMSIC 使用标准间接 CSR 选择值。producer 可以读取 QEMU 模型，
consumer 只需要这些明确编码的字段。

v3 加载端在第一次启动 CPU 前完成 Zstd 解压、长度、目录、拓扑、入口、
程序及操作范围校验；失败时退出，不回退为 raw BIOS。正常 ELF/raw BIOS
保持原行为。stage1 已在 RAM 中，不再由 host 加载第二个 bin。

各选中 hart 从 M 态 ``PC=0x80000000`` 开始，自行读取目录，不要求 host
预置 t5/x30。全部离开 stage0 后，leader 还原入口原字节，执行内存同步，
各 hart 执行 ``fence.i``。leader 通过 MMIO 配置 UART 和各 die 的 APLIC，
各 hart 通过 miselect/mireg、siselect/sireg 恢复 IMSIC pending/enable。
恢复逐 hart mtimecmp/msip 后，由 leader 仅写一次共享 mtime。所有 hart
完成平台配置后，再恢复 GPR/FPR/vector/CSR 并通过真实 ``mret`` 返回 U/S。

v3 不调用旧的设备导入、host timer 准备、返回后 mstatus/mepc 补正，也不
使用 ``checkpoint_restoring`` 屏蔽 IRQ。恢复期由真实的 mie/mstatus 控制
中断。MRET 按架构消费返回帧，因此 v3 不承诺保留之前已经失效的 M 态 trap
返回帧逐位原值。QEMU 的 TB 观察回调仅记录完成状态和执行计数，不改 CPU
架构状态。保存发生在各 hart 都返回 guest 后，避免对恢复程序再次切片。

保存采用流式压缩、临时文件、fsync 与 rename。stage0/stage1 只替换输出流
中的字节，避免额外复制整份 RAM，也不破坏 ``checkpoint-exit=off`` 的继续
运行。工作区在每份输出中重新置零，使恢复后再次制片不会保留旧 barrier。

v3 恢复示例
------------

.. code-block:: bash

   QEMU=/path/to/qemu-system-riscv64
   "$QEMU" \
     -M kmh-bosc-soc,soc-mode=multi,die-mask=0x3,core-mask=0xffff:0xffff:0:0,boot-source=ddr,dw-pcie=off,iommu-sys=off,generated-dtb=off,autotest-dtb=off \
     -smp 64,maxcpus=68 -m 64G -accel tcg,thread=multi \
     -bios /path/to/rootfs-switchroot-before-bash.zst \
     -display none -monitor none -serial stdio -no-reboot

在保存后的系统继续运行或再次保存时，可额外设置 ``checkpoint-path`` 和
``checkpoint-interval``。不要提供覆盖切片的 loader/kernel/DTB 输入。

旧 v2 兼容路径
--------------

显式使用 ``checkpoint-format=v2`` 制片，可保留原来的 M/S/U 保存行为。
它继续使用外部 M 态 stub、host APLIC/IMSIC/UART 导入和 mstatus/mepc
补正，入口为 ``0x80300000``，不能当作原生 RTL 恢复验证。
单 die 的旧 record base 为 ``0x80300000``，多 die 仍在 DDR 尾部；旧 v1
只保留单 hart 读取分支，不承诺未经测试的旧拓扑组合。

.. code-block:: bash

   make -C third_party/kmh-portable-restore
   "$QEMU" \
     -M kmh-bosc-soc,soc-mode=single,core-mask=1,checkpoint-load=/path/v2.zst,checkpoint-stub=third_party/kmh-portable-restore/build/kmh-portable-restore.bin \
     -smp 16,maxcpus=17 -m 512M -accel tcg \
     -bios none -display none -monitor none -serial stdio

构建与验证
----------

QEMU 需启用 Zstd。v3 固件以生成的 ``native-resources.inc`` 随源码提供，
普通 host 构建不需要 RISC-V 交叉编译器。修改固件后显式执行：

.. code-block:: bash

   make -C third_party/kmh-portable-restore native-resources

然后重建 QEMU。固件的 C 代码按整数 ISA 构建，FPR/vector 恢复在专用汇编中。
主机侧编码和固件共享 ``native-abi.h``，固件不包含 QEMU 头文件或回调。

回归已接入 BOSC CI 的 ``checkpoint`` 组。本地与 GitLab 使用同一个
``tests/ci/bosc/ci.py`` 入口；全部 checkpoint case 仅手动执行，不参与 MR
或其他自动选测。GitLab 点击 ``bosc:checkpoint``，本地可以显式选择单个
case，或使用 ``all --group checkpoint``。用例矩阵、输入锁及执行说明见
``tests/ci/bosc/CHECKPOINT.md``。

.. code-block:: bash

   python3 tests/ci/bosc/ci.py all --case bosc-checkpoint-v3 --jobs 8

此命令构建当前 QEMU 和 checkpoint guest fixture，再创建切片并在新进程
恢复。Linux 的保留区固件是固定输入；普通 CI 不依赖相邻 OpenSBI 源码树。
所有测试生成的镜像位于 CI 工作目录的 scratch，完整日志和结果位于 results。

PLDM/RTL 消费约定
-----------------

外部加载端负责解压，按 v3 DDR segment 的 PA/backing offset/size 将内容
写入各 die DDR，并忽略 backing 中未映射的尾部；不能将多 die backing 当作
从 ``0x80000000`` 开始的物理连续 RAM。完成后释放全部选中 hart，从固定
复位地址执行。未选中 hart 保持复位，外部设备 IRQ 输入保持制片时的静默条件。

目标硬件须匹配 CPU、地址映射、可写 timer、APLIC/IMSIC 和 UART 接口。
当前验证仅在 QEMU 中执行原生指令路径，未进行 PLDM/真实 RTL 实跑，不能
将本地恢复成功报告为 RTL 已通过。

解压为 raw bin 并用 ``-bios`` 启动
---------------------------------

可以使用仓库内的独立工具将 Zstd 解压为逻辑大小等于完整 RAM 的稀疏 raw
文件。工具不把零页写入磁盘，因此 64 GiB 镜像的实际占用远小于逻辑大小：

.. code-block:: bash

   cd contrib/kmh-checkpoint
   make bin zstd=/absolute/path/rootfs-switchroot-before-bash.zst

输出文件与输入同目录，后缀为 ``.bin``。QEMU 的 ``kmh-bosc-soc`` 会按 raw
文件大小和 v3 stage0 入口识别它，在 CPU 启动前将 raw RAM 内容装载到 backing，
然后仍从 ``0x80000000`` 执行原生恢复程序：

.. code-block:: bash

   "$QEMU" \
     -M kmh-bosc-soc,soc-mode=multi,die-mask=0x3,core-mask=0xffff:0xffff:0:0,boot-source=ddr,dw-pcie=off,iommu-sys=off,generated-dtb=off,autotest-dtb=off \
     -smp 64,maxcpus=68 -m 64G -accel tcg,thread=multi \
     -bios /absolute/path/rootfs-switchroot-before-bash.bin \
     -display none -monitor none -serial stdio -no-reboot

raw 装载会扫描完整 RAM 文件，启动时间通常比直接读取 Zstd 更长。验证时应
使用完整 RAM logical size，不能截断 bin；实际占用可用 ``du`` 查看。当前
raw 回归 case 为 ``bosc-checkpoint-raw``；64 GiB 五段式恢复对应
``bosc-checkpoint-linux-2die32c-raw``。结果仅表示实际完成的观察窗口，
不代表完整 litmus 或真实 RTL 已验证。
