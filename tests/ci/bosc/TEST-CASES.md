# BOSC/KMH CI 测试项、通过条件与运行命令

核对日期：2026-10-06；libqemu 源码基线：`0f4bd7dc0f`，包含本轮 CI 工作树修改。
本文说明当前代码**测什么、如何判定、用哪些输入、怎样编译和启动**，不是某次运行的通过报告。
后续修改用例或断言时，应同步更新本文。

依据文件：

- [cases.json](cases.json)：用例 ID、分组、拓扑、层级、超时和镜像依赖。
- [checks.py](checks.py)、[runner.py](runner.py)：启动流程及实际 PASS 断言。
- [build.py](build.py)：宿主检查、configure 参数、构建和 bundle 校验。
- [images.lock.json](images.lock.json)、[dependencies.lock.json](dependencies.lock.json)：固定输入。
- [selection.py](selection.py)、[report.py](report.py)：选测与最终门禁。
- [.gitlab-ci.d/bosc.yml](../../../.gitlab-ci.d/bosc.yml)：GitLab job 入口。

目录：

- [覆盖范围与计数](#1-覆盖范围与计数)
- [测试项与测试点](#2-测试项与测试点)
- [PASS、XFAIL 与最终门禁](#3-passxfail-与最终门禁)
- [软件镜像与构建依赖](#4-软件镜像与构建依赖)
- [QEMU 编译命令](#5-qemu-编译命令)
- [启动测试的标准命令](#6-启动测试的标准命令)
- [QEMU/qtest 底层启动参数与复现](#7-qemuqtest-底层启动参数与复现)
- [日志、结果和故障定位](#8-日志结果和故障定位)

## 1. 覆盖范围与计数

当前有 **44 个执行单元**：23 个 Linux 配置、1 个 ITGen 配置、4 个 qtest
可执行文件、16 个 libqemu 用例。qtest 内部共 **32 个子测试**：die/timebase/DDR
7 个、AXI traffic 4 个、PCIe 9 个、DID mapper 12 个。普通 MR 最多选择
33 个执行单元，另有 11 个 extended 配置。

| GitLab job | `--group` | 执行单元数 | 主要覆盖 |
| --- | --- | ---: | --- |
| `bosc:qtest` | `qtest` | 4 | die 地址、13 MHz 时基、DDR 槽位、AXI/IOMMU、PCIe DMA/MSI |
| `bosc:kmh-linux` | `kmh-linux` | 2 | KMH 1/16 核 Linux、pmem 根和 shell |
| `bosc:ddr` | `bosc-ddr` | 4 | BOSC single/multi、稀疏 die、省略 soc-mode 的 DDR 启动 |
| `bosc:mcu` | `bosc-mcu` | 3 | MCU、QSPI/DMAC 搬运、应用核释放、Linux 启动 |
| `bosc:itgen` | `itgen` | 1 | die0 到 die1/hart16 的 APLIC 优先级中断测试 |
| `bosc:iommu` | `iommu` | 5 | 原生 system IOMMU、Linux NVMe probe、direct DMA 和中断 |
| `bosc:did-mapper` | `did-mapper` | 9 | debugfs 首次绑定前配置、NVMe direct DMA、故障恢复与各实例隔离 |
| `bosc:libqemu` | `libqemu` | 16 | ABI/生命周期、core/compose API、ROM/reset、并发、guest exit、库模式 Linux |

libqemu 的逐项测试点、PASS、独立构建 job/profile、资产和运行命令见
[LIBQEMU.md](LIBQEMU.md)。它复用现有固定 release 镜像，覆盖原 machine 和 compose API 从零组装后的
Linux 启动；组装存储用例从已有内核的 initramfs shell 测试独立磁盘。

边界：本门禁不覆盖 my-virtio、完整 ACPI/UEFI 启动、KVM/M-mode/nested、VFIO、
cosim/devproxy、RTL/FPGA、直接 generated-DTB MCU 启动，也不包含 ITGen IOMMU
完整套件。时基 qtest 会检查生成的 ACPI RHCT 字段，但不等于通过 UEFI/ACPI 启动。
PCIe 测试使用 EDU 或 NVMe；当前没有单独的 ACS 配置空间能力/控制位断言。

## 2. 测试项与测试点

表中镜像 ID 在第 4 节给出准确文件名；判定规则在第 3 节展开。
`timeout` 是 case 配置的执行限时，worker 外层另加 120 秒；它不是所有资产下载和
构建时间之和，也不是每个 qtest 子项各有 120 秒。

### 2.1 KMH 与 BOSC DDR Linux

全部为 `tier=mr`、`kind=linux`、`-m 16G -accel tcg`，依赖
`fw_jump + kernel + rootfs`，每项限时 300 秒。

| Case ID | 拓扑/预期在线 CPU | 测试点 | PASS 条件 |
| --- | --- | --- | --- |
| `kmh-linux-1` | `xiangshan-kunminghu`，1 核 | KMH 最小 DDR 启动 | L：在线集合恰为 `{0}`；DT/model、内核、pmem/overlay、shell 均正确 |
| `kmh-linux-16` | `xiangshan-kunminghu`，16 核 | KMH SMP 启动 | L：在线集合恰为 `0..15`，其余同上 |
| `bosc-ddr-single` | `soc-mode=single,die-mask=0xf`，16 核 | 真正 single SoC；验证 single 忽略 die-mask，使用本地地址 | L + D：single model、IMSIC/UART 本地地址；在线 `0..15` |
| `bosc-ddr-multi` | `soc-mode=multi,die-mask=0xf`，64 核 | 四 die DDR 启动和 SMP | L + D：multi model、全局 IMSIC/UART 地址；在线 `0..63` |
| `bosc-ddr-multi-die0` | `soc-mode=multi,die-mask=0x1`，16 核 | 仅启用 die0 仍保持 multi SoC 语义 | L + D：仍为 multi model/全局地址；在线 `0..15` |
| `bosc-ddr-default` | 省略 `soc-mode`，`die-mask=0x1`，16 核 | 默认模式兼容显式 multi | L + D；同一 QEMU 生成的默认 DTB 与显式 multi DTB 逐字节相同 |

这些用例是启动与 OS 可用性检查，不是 DDR 压力/带宽测试。
DDR 容量边界和未映射区域由 `qtest-die` 检查。

### 2.2 BOSC MCU Linux

全部为 `tier=mr`、`kind=mcu`、`-m 16G -accel tcg`，依赖
`fw_jump + rootfs + mcu + qspi`，每项限时 600 秒。

| Case ID | 拓扑/预期在线 CPU | 测试点 | 门禁接受条件 |
| --- | --- | --- | --- |
| `bosc-mcu-single` | single，16 核 | die0 MCU 从 QSPI 搬运、释放应用核 | L + D + M；启动计数目前只能命中精确 XFAIL |
| `bosc-mcu-multi` | multi，4 die，64 核 | die0 搬运，其余 MCU 观察结果并释放核 | 四个活动 MCU 的有序阶段日志均正确；L + D + M |
| `bosc-mcu-multi-die0` | multi，仅 die0，16 核 | 稀疏 die 下 MCU 启动；未选中 die 不执行固件 | L + D + M；禁用 die 不得出现 `[kmh-mcu]` 日志 |

流程是先用当前 QEMU 的 DDR 启动配置导出 DTB，再以
`boot-source=mcu,generated-dtb=off,-dtb machine.dtb` 启动。
`fw_jump` 用于导出 DTB；实际 MCU 启动的 `-bios` 是包含 OpenSBI 和 minimal Image
的 `qspi` payload。不是单独 preload 内核，也不是直接生成 DTB 的 MCU 路径。

### 2.3 ITGen 跨 die APLIC

| Case ID | 拓扑 | 镜像 | 测试点与 PASS |
| --- | --- | --- | --- |
| `bosc-itgen-cross-die` | multi，`die-mask=0x3,core-mask=0x1:0x1:0:0`，256M RAM | `itgen` | die0/core0 控制，die1/core0 即 hart16 接收；target-index=32、die-id=2、claimed=56 和完整 ITGen 成功输出，按 I 判定 |

该项为 MR、120 秒，直接 `-bios` 启动 BIN，不依赖 Linux、rootfs 或外部 OpenSBI。

### 2.4 原生 system IOMMU + Linux NVMe

全部为 `kind=iommu-linux`，启用 `dw-pcie=on,iommu-sys=on,generated-dtb=on`，
`-m 16G -accel tcg`，每项限时 600 秒。依赖 `fw_jump + iommu_kernel + rootfs`；
**不能用 minimal `kernel` 替代 `iommu_kernel`**。
BOSC 各项另外设置 `iommu-bypass=off`，在内建 NVMe 首次 probe 前清 wrapper
的复位 bypass；KMH 不使用该选项。运行时 bit0 仍可按 die/port 独立切换。

| Case ID | 层级 | 配置与 endpoint bus | 盘数/在线 CPU | 测试点与 PASS |
| --- | --- | --- | --- | --- |
| `kmh-iommu-linux` | mr | KMH，`dw-pcie` | 1/1 | L + N；root0 的翻译、NVMe probe、direct DMA 和 MSI |
| `kmh-iommu-linux-dual-root` | mr | KMH，`dw-pcie`、`dw-pcie1` | 2/1 | L + N；两个独立 IOMMU，均使用原始 RID，逐盘校验 |
| `bosc-iommu-linux-single` | mr | BOSC single，`die-mask=0x1,core-mask=0x1`；`pcie-d0-p0` | 1/1 | L + D + N；single 本地地址路径 |
| `bosc-iommu-linux-multi` | extended | BOSC multi，`die-mask=0xf,core-mask=0x1`；`pcie-d0-p0`、`pcie-d1-p0`、`pcie-d2-p0`、`pcie-d3-p0` | 4/4 | L + D + N；独立 IOMMU 的 segment 0/3/6/9 |
| `bosc-iommu-linux-all-roots` | extended | BOSC multi，同上 CPU 配置；每个 die 的 p0/p1/p2 | 12/4 | L + D + N；segment 0..11 全部 probe、逐盘 direct I/O 和 IRQ 验证 |

12 root 用例按 d0p0、d0p1、d0p2、d1p0……d3p2 顺序分配 `BOSC-CI-0..11`
serial。Linux 的 `nvmeN` 编号不保证与这个顺序相同。

### 2.5 qtest-die：7 个子项

Case ID：`qtest-die`；层级 mr；整个可执行文件限时 120 秒。

执行文件 `kmh-bosc-die-test`，源码
[kmh-bosc-die-test.c](../../qtest/kmh-bosc-die-test.c)。不依赖锁定软件镜像；
CPU SRAM 子测试在运行时生成很小的裸机指令序列。qtest harness 启动/控制 QEMU。

| 注册路径 | 测试点 | 关键 PASS 断言 |
| --- | --- | --- |
| `/kmh-bosc/single/addresses` | single SoC SRAM 地址 | 本地 `0x01100000` 读回写入值；对应 multi 编码地址不能读到同一值 |
| `/kmh-bosc/timebase/single` | single 时基一致性 | 虚拟时钟推进 1 秒，mtime 增加 13,000,000；DT `/cpus/timebase-frequency` 和唯一 ACPI RHCT 时基均为 13 MHz |
| `/kmh-bosc/timebase/multi` | multi 时基、各 die 视图 | 同上；四个 die 的全局 mtime 读值相等；DT/ACPI 表地址与长度合法 |
| `/kmh-bosc/die-routing` | APLIC/IMSIC 本地和跨 die 路由、force/clear、DMAC、CPU SRAM 别名 | 只有目标 hart 的 EIP 位置位；其他 die 的 clear 不清本 die；DMAC 本地源到各 die 目标数据正确；实际运行的每 die core0/core15 SRAM 结果为 `die+17` |
| `/kmh-bosc/ddr/16g` | multi，16G RAM，`die-mask=0xf` | 每 die 固定 32 GiB 槽位内映射 4 GiB；槽位首尾数据独立，其余空洞不可按 RAM 写回 |
| `/kmh-bosc/ddr/128g` | multi，128G RAM，`die-mask=0xf` | 四个 32 GiB 槽位首尾独立；整个 DDR aperture 之外不可按 RAM 写回 |
| `/kmh-bosc/ddr/sparse` | multi，16G RAM，`die-mask=0x5` | 仅 die0/die2 各映射 4 GiB，槽位不重排；禁用 die 和剩余空洞不成为 RAM |

DDR 槽位基址为 `0x80000000 + die * 32 GiB`。这些测试检查边界和 sentinel，
不写满 128 GiB，也不是硬件内存时序验证。

### 2.6 qtest-traffic：4 个子项

Case ID：`qtest-traffic`；层级 mr；整个可执行文件限时 120 秒。

执行文件 `bosc-axi4-traffic-test`，源码
[bosc-axi4-traffic-test.c](../../qtest/bosc-axi4-traffic-test.c)。不依赖软件镜像。

| 注册路径 | 测试点 | 关键 PASS 断言 |
| --- | --- | --- |
| `/riscv64/bosc_axi4_traffic/reset_and_readback` | traffic 寄存器初值、字段宽度和读回 | mux/status/config 初值、mapper 区间初值正确；DID/PID 截断及 64 位地址拆分读回正确 |
| `/riscv64/bosc_axi4_traffic/bare_traffic_windows` | 三个 traffic MMIO 窗口的 Bare DMA | 写入旋转 pattern 正确；AW/AR DONE 均置位；读 burst 数为 2、错误数为 0 |
| `/riscv64/bosc_axi4_traffic/user_requester_selection` | 非恒等 Sv39 翻译和 AXI user DID/PID/PV | mapper 指定 DID=4，但实际 AW user DID=5 命中另一物理页；PDTV 场景使用 user PID=0x22 而非 mapper PID=0x11；数据、DONE、burst/error 均正确 |
| `/riscv64/bosc_axi4_traffic/error_dma_has_no_done` | 失败 DMA 不能伪报完成 | 失败写没有 AW_DONE；失败读没有 AR_DONE，读错误数为 1 |

### 2.7 qtest-pcie：9 个子项

Case ID：`qtest-pcie`；层级 mr；整个可执行文件限时 120 秒。

执行文件 `kmh-bosc-pcie-test`，源码
[kmh-bosc-pcie-test.c](../../qtest/kmh-bosc-pcie-test.c)。使用 EDU endpoint，
不是 NVMe；不依赖软件镜像。该执行单元的 owners 同时包含 KMH、BOSC。

| 注册路径 | 测试点 | 关键 PASS 断言 |
| --- | --- | --- |
| `/kmh-bosc/pcie/outbound-multi` | BOSC multi 的全局 ECAM/MEM 到本地 RC | iATU 使用清除 die-id 的 BASE/LIMIT；全局 ECAM/MEM 经本地窗口访问正确 endpoint，错误全局 iATU base 不命中 |
| `/kmh-bosc/pcie/outbound-single` | BOSC single 的本地 ECAM/MEM | 本地 iATU base 正常命中；下游配置和 BAR 访问正确 |
| `/kmh-bosc/pcie/imsi-axi44` | BOSC 四个 p0 root，IOMMU 不启用；44 位 DMA 和 iMSI receiver | 256 vectors 的 bank/status、W1C、mask/enable、die0 IRQ 聚合正确；高位 MSI 匹配先于截断；普通 DMA 截断高位并使用 requester-local SRAM；跨 DDR 槽位复制正确；reset 清 enable |
| `/kmh-bosc/pcie/imsi-iommu` | 同四 root，IOMMU Off/Bare 与 iMSI receiver 顺序 | Off 阻止普通 DMA 而 iMSI 仍到达；Bare 放行数据；移动/关闭 receiver 后原地址恢复普通 DMA；MSI 地址读仍是 DMA |
| `/kmh-bosc/pcie/generic-dwc` | KMH 的通用 DWC 行为 | 保持默认 32 个 iMSI vectors 属性；MSI 直接写 system memory，不被 BOSC receiver 行为接管；高地址 DMA 不别名到低 DDR |
| `/kmh-bosc/pcie/kmh-iommu-root-contexts` | KMH 双独立 IOMMU 的 context/cache 隔离 | 两 root 使用相同 RID=`0x100`、PSCID、IOVA=`0x1000`，分别映射不同物理页；每次 DMA 只更新对应页面 |
| `/kmh-bosc/pcie/iommu-root-contexts` | BOSC 四个独立 IOMMU 的跨 root context 隔离 | 四 p0 root 的 DID 均为 `0x100`；同 RID/PSCID/IOVA、独立页表映射不同物理页，DMA 不串 root |
| `/kmh-bosc/pcie/iommu-msi-mode` | BOSC G-stage Bare/Sv39x4 与 MSIPTP Off/Flat | Bare+Flat 阻止 DMA、fault cause=259；其余组合放行，Off 保留非零 PPN |
| `/kmh-bosc/pcie/kmh-iommu-msi-mode` | KMH 同上合法性矩阵 | 同上，不把 MSIPTP 非零 PPN 当作启用 MSI 翻译 |

## 3. PASS、XFAIL 与最终门禁

### L：Linux 通用断言

Linux、MCU Linux、IOMMU Linux 都必须满足：

1. 日志包含 `OpenSBI`、`Linux version`、`EXT4-fs (pmem0): mounted filesystem`、
   `Switching root to overlay(root=/dev/pmem0)`，model 与 case 匹配。
2. 出现 root shell 后，runner 发送带随机 `BOSC_CI_<token>` 的探针。
   必须收到完整 `BEGIN/CPU/KERNEL/MOUNTS/RC=0/END` 输出；命令回显不能替代。
3. `/sys/devices/system/cpu/online` 展开的集合恰为 `0..cpus-1`，不只检查数量。
4. `uname -r` 与镜像锁中的 `kernel_release` 完全相同；`/proc/mounts` 存在
   `overlay / overlay ...` 根挂载。
5. Linux 串口不得命中 `Kernel panic`、`Oops:`、`BUG:`、`Unable to handle kernel`、
   RCU stall、soft lockup、`FATAL:` 或行首 `TRAP`。超时、提前退出、console
   在验证完成前关闭均失败。

普通 Linux 测试不要求所有 warning 都消失；缺少未装载的 `/opt` workload 不是
独立失败条件。上述 fatal 扫描针对实际捕获的 UART；IOMMU 测试还扫描 QEMU 日志。

### D：设备树与 BOSC 地址断言

每个 Linux 配置都会先导出 DTB/DTS，校验 model，并保存 DTB SHA256。
BOSC 额外检查：single 的 S-IMSIC 起始地址为 `0x1d000000`、group-index-bits=0，
UART stdout 使用 `0x04000000`；multi 则为 `0x10001d000000`、bits=3，
UART stdout 使用 `0x100004000000`。省略 soc-mode 的 case 还做 DTB 字节一致性检查。

这组断言检查的是 runner 导出的 DT；普通 DDR/IOMMU 实际启动仍由 QEMU 生成 DT，
MCU 实际启动才通过 `-dtb` 使用导出的文件。

### M：MCU 阶段与唯一已知豁免

每个活动 die 只允许一次 `stage=start`，必须按序出现：

- die0：`start → qspi → copy-begin → copy-done → ready → release`。
- 其他活动 die：`start → qspi → copy-seen → ready → release`。

额外数值为 `copy-begin=0x8000000`、`copy-done=0x80000000`、`copy-seen=1`、
`release=2`；ready 在 1 到活动 die 数之间。未启用 die 不得执行 MCU，活动 UART
不得有 fatal，最终还必须通过 L 和 D。

[known-issues.json](known-issues.json) 的 `MCU-DIE-NUM-RESET` **仅豁免 start
extra 计数**：所有活动 die 均为 0 时记 XFAIL，其余断言正确才允许 case 为 PASS。
若全部恢复为预期 die 数，记 XPASS 并阻塞，要求复核/移除豁免；其他数值或混合
结果直接 FAIL。因此目前 MCU case PASS 不证明 all-ready 同步屏障正确。

### I：ITGen 输出断言

以下内容必须按序出现：

```text
target-index=32 die-id=2
imsic=0x20001c000000 clear=0x10000409014c
controller=0 target=16 claimed=56
PASS: test_RISCV_AIA_aplic_irq_priority_cross_die
1 tests run, 0 failed, 1 passed
```

不得出现失败计数、FAIL/FAILED 或 assertion failed。只在上述成功输出之后允许
该固定 BIN 的完成 trap：同一 test 名、priv=M、hart=0、mcause=2、mtval=0x5006b，
以及紧随的该 test double-fault 停车行。任意其他 TRAP/FATAL 都失败。
runner 在成功输出后等待串口安静，再检查完整尾部并关闭 QEMU；它不靠进程自然退出。

### N：IOMMU/NVMe 附加断言

在 L（以及 BOSC 的 D）之外，每项必须满足：

1. DT 中所有 root 都有完整 `iommu-map`，从 RID 0 覆盖 `0x10000` 个 ID；
   `0xffff` 是错误长度。KMH 检查 2 个 root，BOSC single 检查 3 个，multi 检查 12 个。
2. KMH 两个 root 的 map 指向两个独立 IOMMU，DID 保留 RID；BOSC 每个
   root 独立连接一个 IOMMU，map 基址均为 0，segment=`die * 3 + port`。Linux PCI domain 不属于 RID。
3. Linux 输出 `iommu: Default domain type: Translated`；每个 endpoint 实际关联
   的 `riscv,iommu ...: using MSIs` 都存在。
4. controller、DEVICE、DMA 结果数均等于 `disks`；serial 恰为 `BOSC-CI-0..N-1`，
   不重复、不缺失。按 serial 找配置中的 bus，再校验 BDF=`segment:01:00.0`。
   不假设 Linux `nvmeN` 编号/枚举顺序，支持 `nvme10`、`nvme11`。
5. 每个 endpoint 有不同的 IOMMU group 和 controller；domain 为 `DMA` 或 `DMA-FQ`，
   state=`live`、driver=`nvme`，有 group attachment、PCI function、I/O queues 日志。
   sysfs `device/iommu` 与 DT 的 IOMMU 归属一致，MSI IRQ 类型有效。
6. 每块本次私有测试盘用随机 **1 MiB** 数据，在 **1 MiB 偏移**执行
   `oflag=direct,conv=fsync` 写、`iflag=direct` 读，SHA256 相同。
   对应 `nvmeNq1/q2/...` 非 admin 队列中断计数必须增长；q0 不计入。
7. 完整 UART 和 QEMU 日志不能出现 IOMMU fault、NVMe timeout/timed out/failed/resetting、
   I/O error、Buffer I/O、blk_update_request、DMA transfer/mapping error 或通用 fatal。

每盘是 runner 创建的 64 MiB sparse raw 文件。正常结束、异常、父进程超时清理均
删除本次 scratch 盘，保留 `nvme-disks.json` 和原始日志，不上传原始大盘。
仅看到 `/sys/class/iommu` 或 `/dev/nvme*` 不能 PASS。

KMH root1 当前使用 `0x311f1000/0x1000`、IRQ `0x28..0x2b`；这是当前 QEMU 模型
分配，不是经过硬件地址表确认的承诺。两实例相同 RID 的隔离由 qtest 验证；结果 JSON
中的 DID 数值是按预期拓扑推导的证据字段，并非额外采集的硬件事务 trace。

### Q：qtest 结果与总报告

qtest 可执行文件必须正常返回 0，所有内部 C 断言通过。TAP 中不得出现 `not ok`、
`Bail out!`、`# SKIP`、`# TODO`；成功子项的**名称和数量**必须与 cases.json 完全一致。
实际 TAP 名称由 libqtest 在注册路径前加 `/riscv64`；AXI traffic 原注册路径已含该前缀，
所以该套实际输出为 `/riscv64/riscv64/bosc_axi4_traffic/...`，不可自行删去一层。

最终 [report.py](report.py) 还检查选中用例结果是否存在、case ID、run ID、harness hash、
qtest 子结果、MCU 计数断言、IOMMU endpoint serial 集合。构建来源及文件 SHA256
由执行前的 `verify_bundle()` 校验；选测所属 revision 也必须匹配当前 checkout。

- PASS：选中的执行/断言满足要求。MCU 可另附上述精确 XFAIL。
- FAIL：执行、资产、构建、断言、结果身份或缺结果等任何错误。
- XFAIL：仅当前已审查的 MCU 计数缺陷；不是功能成功。
- XPASS：已豁免缺陷意外恢复，阻塞并要求复核。
- NOT_SELECTED：没有执行，不计作通过。

总报告无 FAIL/XPASS 才返回 0；JUnit 用于展示，退出码负责阻塞。报告同时列 qtest
容器与子项、MCU 附加断言，因此报告行数不等于执行单元数。全组未选中时也可返回 0，
必须查看 selection 和 NOT_SELECTED，而不能只看绿色 job。

## 4. 软件镜像与构建依赖

### 4.1 固定运行镜像

`--assets-dir` 指向下表相对路径的根。未提供本地映射时才使用 SHA256 缓存或 JF；
本地文件、缓存、下载文件都检查 size/SHA256。显式本地路径不存在时不会自动回退 JF。
源镜像复制为本轮只读输入，rootfs 由 loader 装进 guest RAM，不把共享源 ext4 当测试盘写。

| Asset ID | 相对离线路径 | 大小（bytes） | 用途 |
| --- | --- | ---: | --- |
| `fw_jump` | `st-release-v0.9.99/fw_jump/fw_jump-kmh-v2-no-dtb.bin` | 274704 | 无内嵌 DTB 的 OpenSBI；DDR/IOMMU 启动及 MCU DT 导出 |
| `kernel` | `st-release-v0.9.99/minimal/Image` | 8321912 | 普通 Linux；MCU QSPI 内嵌同一 Image |
| `iommu_kernel` | `riscv-linux-devel-clean/d3e51a02cc62/Image` | 29611520 | 含原生 IOMMU MSI 修复、DWC/NVMe 和 compose RAM shell 的内核 |
| `did_mapper_kernel` | `qemu-ci/linux/did-mapper/20261012/1411c50e0909/Image` | 29603328 | mapper helper/debugfs 内建、NVMe 模块化的专用内核 |
| `did_mapper_modules` | `qemu-ci/linux/did-mapper/20261012/1411c50e0909/mapper-modules.ext4` | 16777216 | 与专用内核同构建的 NVMe 模块、加载顺序及 hash |
| `rootfs` | `st-release-v0.9.99/openEuler-minimal-rootfs/openEuler-minimal-rootfs-24.03-SP3-RVA23.ext4` | 536870912 | pmem0 根文件系统输入 |
| `mcu` | `qemu-ci/mcu/20260927/kmh-bosc-mcu.bin` | 1058 | MCU boot ROM 固定 BIN |
| `qspi` | `qemu-ci/mcu/20260927/fw_payload.bin` | 12516216 | QSPI payload，含 fw_jump 和 minimal Image |
| `itgen` | `qemu-ci/itgen/aplic-cross-die/dieid2-20260924/test_RISCV_AIA_aplic_irq_priority_cross_die.bin` | 56760 | 跨 die APLIC priority 裸机 BIN |

| Asset ID | 锁定 SHA256 |
| --- | --- |
| `fw_jump` | `d8da253945e544c284b0757a9ddc9cb799c13912e34616a33279bc288dbf4bd2` |
| `kernel` | `55afc258b442384edd1a0a789eda747b715cda4f9401a170b209ff334e4985e9` |
| `iommu_kernel` | `1c75064fa376a4286bad372ce2f4a67154f23aec0c8f8010b20f8867d821798a` |
| `did_mapper_kernel` | `c037c191a40d6b5def44c07e1e40b4516343481b039e0f276c44e44776ee3983` |
| `did_mapper_modules` | `9e7bbb78194569e5f38097817a19d1a7beb3788be8b668928a2f13cdb9d5447c` |
| `rootfs` | `4b0ddf5b06eb26369f5c2df1f87c63d94fb47d249e9c99d7d0291cf80a720ee5` |
| `mcu` | `d50f8728880707d3f529bd0bcd032701113b5b560fb89237faf854b36068563c` |
| `qspi` | `cee4dcbfbd773a4456ff01cf650181c72ae6d16654b9846ce4bb6fc2bef413b9` |
| `itgen` | `d8c37599a2e438995a44a0eda1eb0851f46497df4a05b98c63b3a6b3eb461a57` |

kernel release 分别为 `20260824_6.18_g3520522921abc-minimal` 和
`6.18.34`（Linux 源码 `d3e51a02cc629221404c9a871da9aec8ec42767d`）。
Mapper 专用 release 为 `6.18.34-bosc-did-mapper`（Linux 源码 `1411c50e0909`）；
两个 mapper 资产已于 2026-10-12 发布并校验远端 size/SHA256 及实际下载，
发布和本地验收证据见 [ASSETS.md](ASSETS.md)。
QSPI 中 fw_jump 位于偏移 0，kernel 位于偏移 `0x400000`；启动时搬运到
`0x80000000`、`0x80400000`。资产准备还校验 payload 内这两个组件的 hash。

JF 路径规则以锁文件 `jf_path` 为准：三个 release 输入将上述
`st-release-v0.9.99/` 前缀替换为 `xs-sw/st-release/v0.9.99/`；MCU/QSPI/ITGen
路径前加 `hejian-workspace/guoyaxing/`。`iommu_kernel` 位于
`hejian-workspace/guoyaxing/qemu-ci/linux/iommu/20261007/d3e51a02cc62/Image`。
来源和发布说明见 [ASSETS.md](ASSETS.md)。

CI 不编译这些 guest 软件、不运行 st-downloader、不上传或修改 JF 镜像。
NVMe scratch 盘是运行时创建的测试数据，不属于 images.lock。

### 4.2 原生构建依赖和宿主

宿主为 Linux x86_64；需要 Python >= 3.10、PyYAML、C/C++ 编译器、make、Ninja、
pkg-config、Git、ldd、GLib/pixman/zlib/libfdt 开发环境及 dtc/fdtget/fdtput。
需要可用的 Unix socket 和临时目录；本地执行不要求 Docker、Runner 或 RISC-V 交叉编译器。

| 依赖 | 锁定 revision | 归档大小（bytes） |
| --- | --- | ---: |
| `berkeley-softfloat-3` | `b64af41c3276f97f0e181920400ee056b9c88037` | 2273280 |
| `berkeley-testfloat-3` | `e7af9751d9f9fd3b47911f51a5cfd08af256a9ab` | 1935360 |
| `keycodemapdb` | `f5772a62ec52591ff6870b7e8ef32482371f22c6` | 133120 |

这三项由 [dependencies.py](dependencies.py) 按依赖锁恢复，检查归档及当前 Meson
wrap revision，再应用当前源码的 packagefiles。已有 Git checkout 也必须匹配锁定
revision 且无 tracked 修改；不会 reset 用户依赖树。

完全离线时 `--dependencies-dir` 的目录按 `dependencies.lock.json` 的 `path` 布局，
即 `native-deps/<name>-<revision>.tar`。`--assets-dir` 只提供 guest 输入，不能替代它。
QEMU 的 Meson/pycotap wheels 来自源码树，宿主系统包需预先准备。

## 5. QEMU 编译命令

### 5.1 CI 实际调用

`ci.py build` 先恢复上述原生依赖，再执行 `build.py`；只构建当前 selection 需要的
QEMU/qtest。全选时 configure 参数如下，工作目录为 `<work-dir>/qemu-build`：

```bash
# 在 QEMU 仓库根目录设置；manual-build-01 必须是新的构建目录。
source_root="$PWD"
ci_work="$source_root/build/ci/manual-build-01"
mkdir -p "$ci_work/qemu-build"
(
  set -e
  cd "$ci_work/qemu-build" || exit 1
  export CCACHE_DIR="$ci_work/ccache"
  export PIP_CACHE_DIR="$ci_work/pip-cache"
  "$source_root/configure" \
    --target-list=riscv64-softmmu \
    --enable-tcg \
    --disable-kvm \
    --disable-werror \
    --disable-docs \
    --disable-gtk \
    --disable-sdl \
    --disable-vnc \
    --disable-guest-agent \
    --disable-tools \
    --disable-download \
    --disable-my-virtio \
    --disable-io-system
  ninja -j 8 qemu-system-riscv64 \
    tests/qtest/kmh-bosc-die-test \
    tests/qtest/bosc-axi4-traffic-test \
    tests/qtest/kmh-bosc-pcie-test
)
```

这是构建参数的等价展开，**前提是原生依赖已准备好**；正式验收推荐第 6 节的
`ci.py`，因为手工 configure/ninja 不会自动生成受校验的 bundle/selection/result。
`--disable-werror` 不表示忽略编译失败；configure/ninja 非零仍阻塞。

configure 限时 600 秒，ninja 限时 3600 秒，GitLab build job 限时 80 分钟。
build 保存源码 revision、tracked diff 和新增源码身份，构建前后不能变化；记录每个
产物 SHA256，复制到 `<work-dir>/bundle/`。随后每个 case 检查来源、产物和动态库。
bundle 还复制源码树的两份 OpenSBI fw_dynamic 文件到 `share/qemu/`，不等同于
Linux case 显式使用的锁定 `fw_jump`。

## 6. 启动测试的标准命令

以下均从 **qemu-bosc-soc 仓库根目录**执行。镜像路径是示例，应对应实际离线布局；
不指定本地目录时可通过已配置的 JF/缓存获取。所有例子都是可选运行方式，不要求逐个重复执行。

### 6.1 一键全量或指定用例

```bash
# 全量，包括 extended；自动创建并打印独立 WORK_DIR。
python3 tests/ci/bosc/ci.py all --full --jobs 8 \
  --assets-dir /srv/bosc-ci/assets \
  --dependencies-dir /srv/bosc-ci/dependencies
```

只测某项：将 `case_id` 改成第 2 节任意执行单元 ID。此命令同时选测、预检查、
执行框架自测/lint、校验资产、编译、运行和汇总；无需另找历史 QEMU binary。

```bash
case_id=bosc-ddr-multi
python3 tests/ci/bosc/ci.py all --case "$case_id" --jobs 8 \
  --assets-dir /srv/bosc-ci/assets \
  --dependencies-dir /srv/bosc-ci/dependencies
```

同一次构建选择多个 case，可以重复 `--case`，例如：

```bash
python3 tests/ci/bosc/ci.py all --jobs 8 \
  --assets-dir /srv/bosc-ci/assets \
  --dependencies-dir /srv/bosc-ci/dependencies \
  --case kmh-iommu-linux-dual-root \
  --case bosc-iommu-linux-all-roots \
  --case qtest-pcie --case qtest-traffic
```

显式 `--case` 可以直接选择 extended。只跑 qtest 时没有 guest 镜像依赖，仍需要
原生构建依赖。`all --group` 和 `select --group` 不支持；新 selection 用 `--case`。

### 6.2 分步构建和按组运行

下面模拟 GitLab 的 check/build/test/report 分阶段。`ci_work` 必须是新的绝对路径，
不能含空格、逗号或换行；每个 case 只能在同一 work-dir 执行一次。

```bash
ci_work="$PWD/build/ci/full-20261002-01"
python3 tests/ci/bosc/ci.py select --work-dir "$ci_work" --full
python3 -m unittest discover -s tests/ci/bosc -p test_ci.py -v
python3 tests/ci/bosc/lint_ci.py
python3 tests/ci/bosc/ci.py doctor --work-dir "$ci_work"
python3 tests/ci/bosc/ci.py build --work-dir "$ci_work" --jobs 8 \
  --dependencies-dir /srv/bosc-ci/dependencies
python3 tests/ci/bosc/ci.py run --work-dir "$ci_work" --group qtest
python3 tests/ci/bosc/ci.py run --work-dir "$ci_work" --group kmh-linux \
  --assets-dir /srv/bosc-ci/assets
python3 tests/ci/bosc/ci.py run --work-dir "$ci_work" --group bosc-ddr \
  --assets-dir /srv/bosc-ci/assets
python3 tests/ci/bosc/ci.py run --work-dir "$ci_work" --group bosc-mcu \
  --assets-dir /srv/bosc-ci/assets
python3 tests/ci/bosc/ci.py run --work-dir "$ci_work" --group itgen \
  --assets-dir /srv/bosc-ci/assets
python3 tests/ci/bosc/ci.py run --work-dir "$ci_work" --group iommu \
  --assets-dir /srv/bosc-ci/assets
python3 tests/ci/bosc/ci.py report --work-dir "$ci_work"
```

`run --group` 只执行该组已被选中的 case，不会自动扩充 selection。
`run` 会校验并准备镜像，所以事先 `assets` 不是必需；也可先运行
`ci.py assets --work-dir "$ci_work" --assets-dir ...` 单独排查输入。
复跑使用新 work-dir，不能覆盖旧日志或改 provenance 来复用不匹配的 binary。

若只具备 release 目录而没有 `qemu-ci/mcu`、`qemu-ci/itgen` 离线文件，不能把该目录
作为全量资产目录直接使用；按缺项提供 `--asset ID=/absolute/file` 或准备完整布局。
每个 override 仍必须匹配锁定 hash。JF 模式使用 `BOSC_CI_JF_BASE_URL`、
`BOSC_CI_JF_TOKEN` 和可选 `BOSC_CI_CACHE_DIR`；凭据由环境注入，不写进命令或文档。

### 6.3 选测与 GitLab 入口

| 场景 | 选择规则 |
| --- | --- |
| 普通 MR | 从完整 MR diff 按 machine 取并集，仅 `tier=mr` |
| MR diff 缺失、未知路径 | 保守选择两种 machine 的 MR 集合 |
| 默认分支、schedule、tag | 包含 extended 的全量 |
| `BOSC_CI_FULL=1` 或本地 `--full` | 显式全量 |
| 本地无 base 的 `all` | 全量，包含 extended |
| 普通 web，非默认分支且无 full | 所有 MR 层级配置 |
| 本地 `--base <ref>` | 对提交范围及工作树变更做影响分析 |

GitLab 入口为 `.gitlab-ci.d/bosc.yml`，不是默认的上游 QEMU pipeline。
`bosc:check` 跑选测、52 项框架自测、离线 YAML/manifest lint 和 `git diff --check`；
框架自测覆盖选测/资产锁/断言反例/清理/日志/缺结果，不能替代真实 QEMU 测试。
`bosc:build` 跑 doctor 和 system profile build；`bosc:build-libqemu` 独立构建库。
七个测试 job 调用：

```bash
python3 tests/ci/bosc/ci.py run \
  --work-dir "$BOSC_CI_WORK" --group "$BOSC_CI_GROUP"
```

默认 `BOSC_CI_WORK=build/ci/pipeline`、`BOSC_CI_JOBS=8`，测试 job 各限时 45 分钟。
最后 `bosc:report` 以 `when: always` 汇集所有结果，缺选中结果会失败。
本地无需 Runner；线上还需要匹配 `x86_64/tcg/$BOSC_CI_RUNNER_TAG` 的可用 Runner。

## 7. QEMU/qtest 底层启动参数与复现

正式 CI 使用 QEMUMachine 管理 QMP、socket 串口、探针和进程清理。
下面是去掉临时 FD/QMP 管理参数后的**手工启动模板**，使用 `-serial stdio` 观察输出。
手工启动不会自动执行第 3 节探针、检查或汇总；需要自动 PASS/FAIL 时使用第 6 节。

### 7.1 模板变量与原始命令

| 变量 | 含义 |
| --- | --- |
| `ci_work` | 一次当前源码构建产生的绝对 work-dir |
| `qemu` | `$ci_work/bundle/qemu-system-riscv64` |
| `firmware_dir` | `$ci_work/bundle/share/qemu` |
| `fw_jump` / `image` / `rootfs` / `mcu` / `qspi` / `itgen` | 选定 case 的 `assets.json` 中对应已校验资产的绝对路径；`image` 取 kernel 或 iommu_kernel |
| `debug_dir` | 本次新建的独立调试目录，保存串口、复制的 DT/trigger，不能覆盖原验收日志 |
| `dtb` | 当前 case 由同一 QEMU 导出的 DTB，复制到独立调试目录 |
| `trigger` | 对应 case 的 `boot-rootfs.trigger`，复制到独立调试目录 |
| `machine_base` / `smp` | 下表的 machine 参数及拓扑参数 |

每次 Linux/MCU/IOMMU/ITGen 实跑已保存：

```text
<work-dir>/results/<case-id>/replay-command.txt
<work-dir>/results/<case-id>/process.json
<work-dir>/results/<case-id>/assets.json
```

`replay-command.txt` 是 stdio 形式的完整命令；`process.json` 记录真实 PID/argv，
可能带继承 FD，不能原样复制到新 shell。复现时将输出日志和临时盘改到新路径，
尤其不能覆盖原 `uart1.log..uart3.log`。NVMe 原盘已清理，必须重新创建私有测试盘。

### 7.2 每项 Linux/MCU 的 machine 参数

`machine_base` 对应 `-M` 的基础部分，后续模板追加 generated/autotest 或 mcu-bios。
它不是自行推算在线核数：BOSC 的 `-smp` 规定 SoC 拓扑，在线核数由 masks 决定。

| Case ID | `machine_base` | `smp` |
| --- | --- | --- |
| `kmh-linux-1` | `xiangshan-kunminghu` | `1` |
| `kmh-linux-16` | `xiangshan-kunminghu` | `16` |
| `bosc-ddr-single` | `kmh-bosc-soc,soc-mode=single,die-mask=0xf,core-mask=0xffff,boot-source=ddr` | `16,maxcpus=17` |
| `bosc-ddr-multi` | `kmh-bosc-soc,soc-mode=multi,die-mask=0xf,core-mask=0xffff,boot-source=ddr` | `64,maxcpus=68` |
| `bosc-ddr-multi-die0` | `kmh-bosc-soc,soc-mode=multi,die-mask=0x1,core-mask=0xffff,boot-source=ddr` | `64,maxcpus=68` |
| `bosc-ddr-default` | `kmh-bosc-soc,die-mask=0x1,core-mask=0xffff,boot-source=ddr` | `64,maxcpus=68` |
| `bosc-mcu-single` | `kmh-bosc-soc,soc-mode=single,die-mask=0xf,core-mask=0xffff,boot-source=mcu` | `16,maxcpus=17` |
| `bosc-mcu-multi` | `kmh-bosc-soc,soc-mode=multi,die-mask=0xf,core-mask=0xffff,boot-source=mcu` | `64,maxcpus=68` |
| `bosc-mcu-multi-die0` | `kmh-bosc-soc,soc-mode=multi,die-mask=0x1,core-mask=0xffff,boot-source=mcu` | `64,maxcpus=68` |
| `kmh-iommu-linux` | `xiangshan-kunminghu,dw-pcie=on,iommu-sys=on` | `1` |
| `kmh-iommu-linux-dual-root` | `xiangshan-kunminghu,dw-pcie=on,iommu-sys=on` | `1` |
| `bosc-iommu-linux-single` | `kmh-bosc-soc,soc-mode=single,die-mask=0x1,core-mask=0x1,boot-source=ddr,dw-pcie=on,iommu-sys=on,iommu-bypass=off` | `16,maxcpus=17` |
| `bosc-iommu-linux-multi` | `kmh-bosc-soc,soc-mode=multi,die-mask=0xf,core-mask=0x1,boot-source=ddr,dw-pcie=on,iommu-sys=on,iommu-bypass=off` | `64,maxcpus=68` |
| `bosc-iommu-linux-all-roots` | `kmh-bosc-soc,soc-mode=multi,die-mask=0xf,core-mask=0x1,boot-source=ddr,dw-pcie=on,iommu-sys=on,iommu-bypass=off` | `64,maxcpus=68` |

### 7.3 DDR Linux 公共命令

选定第 7.2 节的参数、设置第 7.1 节的绝对路径后，准备 Bash 参数数组：

```bash
qemu="$ci_work/bundle/qemu-system-riscv64"
firmware_dir="$ci_work/bundle/share/qemu"
dt_options="generated-dtb=on,autotest-dtb=on"
dt_options+=",autotest-rootfs-addr=0xa0000000"
dt_options+=",autotest-workload-addr=0xc0000000"
serial_args=()
# BOSC multi/default（包括仅 die0）需设置如下三个额外串口；KMH/single 留空。
# serial_args=(-serial "file:$debug_dir/uart1.log"
#              -serial "file:$debug_dir/uart2.log"
#              -serial "file:$debug_dir/uart3.log")
linux_args=(
  -L "$firmware_dir" -display none -monitor none -serial stdio
  -M "${machine_base},${dt_options}"
  -smp "$smp" -m 16G -accel tcg
  -bios "$fw_jump"
  -device "loader,file=$image,addr=0x80400000"
  -device "loader,file=$rootfs,addr=0xa0000000"
  -device "loader,file=$trigger,addr=0x90000000"
  "${serial_args[@]}"
)
# 普通 DDR Linux 使用 minimal Image；IOMMU 请改用第 7.5 节命令。
timeout --foreground 300 "$qemu" "${linux_args[@]}"
```

runner 导出的 trigger 内容如下，IOMMU case 在 bootargs 尾部另加 `iommu.passthrough=0`：

```text
[cmd]
bootargs: "console=ttyS0,115200 earlycon loglevel=8 ignore_loglevel root=/dev/pmem0 ro"
[cmd_end]
[task]
[task_end]
```

没有装载 workload，`autotest-workload-addr=0xc0000000` 只描述预留 pmem 窗口。
CI 不通过 `-kernel`、`-append` 或 virtio-blk rootfs 启动；OpenSBI/loader/trigger
组合是当前实际路径。

DT 导出阶段使用相同 machine（MCU case 此阶段改为 `boot-source=ddr`），追加
`dumpdtb=<path>`，`-serial null -bios <fw_jump>`；随后用 fdtput 修改 `/chosen/bootargs`、
dtc 转 DTS，并做 D/N 断言。准确导出 argv 保存在 `dumpdtb.log.command.json`。

### 7.4 MCU 启动命令

使用对应 MCU case 的 `machine_base`，不再追加 generated/autotest 开启参数。
`dtb` 和 `trigger` 已由前述 DDR 导出阶段产生；multi 的串口数组同第 7.3 节。

```bash
timeout --foreground 600 "$qemu" \
  -L "$firmware_dir" -display none -monitor none -serial stdio \
  -M "${machine_base},generated-dtb=off,mcu-bios=${mcu}" \
  -smp "$smp" -m 16G -accel tcg \
  -bios "$qspi" -dtb "$dtb" \
  -device "loader,file=$rootfs,addr=0xa0000000" \
  -device "loader,file=$trigger,addr=0x90000000" \
  "${serial_args[@]}"
```

此处没有 `loader,file=Image`，因为 QSPI payload 搬运会提供内核。
`qspi` 文件、`mcu-bios` 文件和 `fw_jump` 不是同一个输入。

### 7.5 IOMMU NVMe 启动命令

使用相应 IOMMU case 的 machine 参数，令 `image` 指向 `iommu_kernel`，
trigger 含 `iommu.passthrough=0`。沿用第 7.3 节的 `linux_args` 定义，
**不执行其中末尾的普通 Linux timeout 命令**，改为以下命令追加 NVMe 参数。

各配置的 bus 数组如下，每次只选一行：

```bash
# kmh-iommu-linux
buses=(dw-pcie)
# kmh-iommu-linux-dual-root
buses=(dw-pcie dw-pcie1)
# bosc-iommu-linux-single
buses=(pcie-d0-p0)
# bosc-iommu-linux-multi
buses=(pcie-d0-p0 pcie-d1-p0 pcie-d2-p0 pcie-d3-p0)
# bosc-iommu-linux-all-roots
buses=(pcie-d0-p0 pcie-d0-p1 pcie-d0-p2
       pcie-d1-p0 pcie-d1-p1 pcie-d1-p2
       pcie-d2-p0 pcie-d2-p1 pcie-d2-p2
       pcie-d3-p0 pcie-d3-p1 pcie-d3-p2)
```

以下子 shell 用新目录生成私有 sparse 盘并在退出时清理，展开的
`-drive/-device` 参数与 runner 一致：

```bash
(
  set -e
  nvme_dir=$(mktemp -d /tmp/bosc-nvme-replay-XXXXXX)
  cleanup_nvme_replay() {
    rm -f -- "$nvme_dir"/nvme*.raw
    rmdir -- "$nvme_dir"
  }
  trap cleanup_nvme_replay EXIT
  nvme_args=()
  for i in "${!buses[@]}"; do
    disk="$nvme_dir/nvme${i}.raw"
    truncate -s 64M "$disk"
    nvme_args+=(
      -drive "file=$disk,if=none,id=nvme$i,format=raw,cache=none"
      -device "nvme,drive=nvme$i,bus=${buses[$i]},addr=0,serial=BOSC-CI-$i"
    )
  done
  timeout --foreground 600 "$qemu" "${linux_args[@]}" "${nvme_args[@]}"
)
```

即使手工看到所有 NVMe，也仍需 N 中的 direct I/O/hash/IRQ/归属断言。
标准 CI 自动完成这些检查并保存 `iommu-probe.sh`；建议优先用 `ci.py all --case ...`。

### 7.6 ITGen 启动命令

`itgen` 是已校验的 BIN，使用同一 bundle 的 QEMU：

```bash
itgen_machine="kmh-bosc-soc,soc-mode=multi,die-mask=0x3"
itgen_machine+=",core-mask=0x1:0x1:0:0,boot-source=ddr"
timeout --foreground 120 "$qemu" \
  -L "$firmware_dir" -display none -monitor none -serial stdio \
  -M "$itgen_machine" -smp 64,maxcpus=68 -m 256M -accel tcg \
  -bios "$itgen"
```

手工命令在成功输出后仍可能等待到 timeout，因为裸机程序会停车；timeout 的退出码
不替代 I 中的判定。标准 runner 会识别成功尾部后主动关闭，不必等到 120 秒。

### 7.7 qtest 启动命令

以下直接运行已构建 bundle 中的三个测试可执行文件。libqtest 自行传递 QMP/qtest
socket 参数，并逐子项创建所需 machine，不应以一个裸 QEMU 命令代替其断言。

```bash
export QTEST_QEMU_BINARY="$ci_work/bundle/qemu-system-riscv64"
timeout 120 "$ci_work/bundle/tests/qtest/kmh-bosc-die-test" --tap
timeout 120 "$ci_work/bundle/tests/qtest/bosc-axi4-traffic-test" --tap
timeout 120 "$ci_work/bundle/tests/qtest/kmh-bosc-pcie-test" --tap
```

每个 qtest 的具体 `# starting QEMU: ...` 在 TAP 日志内；普通子项用 `-accel qtest`，
die-routing 中运行指令序列的部分还启用 TCG。正式 runner 使用独立短路径 TMPDIR
避免 socket 路径问题，检查完整 TAP 并生成 JSON/JUnit。

## 8. 日志、结果和故障定位

| 文件或目录（相对 work-dir） | 内容 |
| --- | --- |
| `selected-tests.json` | 本次选中/未选中的 case、tier、revision、run ID、harness hash |
| `doctor.json` | 宿主工具、版本、Unix socket 等预检查 |
| `configure.log*`、`build.log*` | 完整构建输出及真实 argv |
| `native-dependencies.json`、`asset-evidence/` | 原生依赖/镜像来源、size、SHA256 |
| `bundle/build.json` | 当前源码身份、configure、二进制/firmware hash |
| `results/<case>/uart*.log`、`qemu.log` | 完整 guest/QEMU 原始日志；qtest 用 `qtest.log` |
| `results/<case>/machine.dtb`、`machine.dts`、`dt-evidence.json` | 导出 DT、断言依据与 hash |
| `results/<case>/replay-command.txt`、`process.json` | stdio 复现命令与真实启动 argv/PID |
| `results/<case>/assets.json`、`result.json` | 实际资产绝对路径、判定及检查证据 |
| `results/<case>/nvme-disks.json`、`iommu-probe.sh` | 私有盘 bus/segment/serial/大小/清理状态与 guest 探针 |
| `results/<case>/cleanup.json`、`exception.log`、`worker.log` | VM 清理、异常栈和 worker 输出；按实际执行阶段产生 |
| `stage-errors/` | 框架级错误，会进入最终报告 |
| `reports/<group>/`、`reports/all/` | `summary.md`、`summary.json`、`junit.xml` |

页面实时输出预算由 `BOSC_CI_LIVE_LOG_KIB` 控制；调小或设 0 不会截断原始日志，
失败尾部另有预算。IOMMU 测试盘在 `scratch/<case>/` 创建，结束后删除且不上传。

几个容易误判的现象：

- build 找不到 `berkeley-softfloat-3`：先查原生依赖目录/JF 配置，不是 Linux 启动问题。
- IOMMU 用例一开始报 JF 401：尚未验证 QEMU 功能；build 的缓存命中不证明运行镜像可读。
- GitLab 页面停在下载制品：先按同一 job ID 查看 Runner。若 Runner 已 `Job succeeded`
  而 `PATCH .../trace`、最终状态上传失败，属于日志/状态传输问题，不能认定 DDR 卡死。
- `.bashrc` 的 module/devtoolset 警告：应结合阶段退出码判断，不据此直接归因于 QEMU。

本文覆盖清单与命令，不代表当前提交已经重新运行全量验收。实际结果以某次 run 的
selection、构建来源、完整日志和最终报告共同确认；本地 PASS 不等于线上 GitLab PASS。

## 9. DID mapper 的 12 个 qtest 子项与 9 个 Linux 配置

完整专项清单及断言见 [DID-MAPPER.md](DID-MAPPER.md)。新增 MR case 是
`qtest-did-mapper` 和 `bosc-did-mapper-linux-single`；其余八种 Linux 拓扑/负向
配置为 extended。每种 Linux 正向配置都包含六轮逐盘 direct DMA/IRQ/hash 验证，
negative case 额外要求明确 cause/DID/实例、未受影响盘持续 I/O 和目标恢复。
专用内核首次加载 NVMe 模块前设置 mapper；不配置 mapper 的既有 IOMMU case
继续验证原 RID。输入资产和发布状态见 ASSETS.md，日志新增 mapper-evidence.json
及逐阶段 shell 脚本。
