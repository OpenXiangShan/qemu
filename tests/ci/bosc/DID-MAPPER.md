# DID mapper CI

QEMU 源码和所有构建/验证均使用 `qemu-bosc-soc`。本页说明覆盖和验收条件，
实际 PASS 以某次 run 的结果、源码身份和完整日志为准。

## qtest

`qtest-did-mapper`（MR，180s）使用真实 EDU PCI DMA，含 12 个子项：

| 子项 | 覆盖 |
| --- | --- |
| reset-single / reset-multi | 3 / 12 实例，全部 8 slot 的 reset、64 位 split-MMIO、字段掩码 |
| reset-no-bypass | single 三实例使用 iommu-bypass=off，首次启动及 machine reset 均清 bit0 |
| ranges | 不同有效 DID 到不同物理页；关闭、空项、非空未命中、start/end、反向区间、8 slot、重叠优先级 |
| cache-update | 建立 ctx/IOT cache 后修改 DC/PT；mapper 写入必须刷新两类内部 cache |
| fault-recovery | 错误 DID 的 FQ cause 258、DID 0x200、目标内存保护、恢复有效 DID |
| all-roots | 相同 RID/PSCID/IOVA，12 套不同物理页；逐一切换 mapper，其余 11 套保持独立 |
| bypass | 12 root 逐一切换 bit0；direct/RID/mapper 三种目标可区分，错误 DID 和 DDTP Off 下 bypass 不 fault，清 bit0 恢复翻译，44-bit 截断及本 die SRAM 别名 |
| boundary | 一次 DMA 同页/跨页、1 GiB 超页及非对齐 slot 边界；读写、命中到未命中及未命中到命中 |
| pid-pv | PV=0 使用 DC；PV=1 与 PDTV=0 冲突必须 fault，FQ 核对 PID/PV/DID |
| imsi | mapper 错误 DID 下 bypass 开/关均由 iMSI 前置消费；移开 receiver 后普通 DMA 上报 mapper DID |
| cpu-local | 四 die CPU 以不带 die ID 地址访问本地三套 mapper，再核对带硬件 die ID 1..4 的别名 |

原 PCIe、AXI traffic 和 die qtest 继续运行。PCIe ordinary DMA 初始化每个 die
自己的 IOMMU；独立实例不加 PCI segment 到 DID。
Wrapper bit0 默认置位；旧翻译测试显式清 bit0。Linux IOMMU/mapper CI 使用
`iommu-bypass=off`，使内建 NVMe 首次 probe 即可翻译；mapper 各阶段写 mux
时仍保持 bit0=0。不配置 mapper 的原生 IOMMU case 继续使用原 RID。

## Linux NVMe

每个 case 先确认冷插入端点未绑定，mapper helper 已 probe；通过 debugfs
配置并逐实例读回 mapper，再加载与 Image 同构建的 `nvme-core.ko` / `nvme.ko`。
这是首次绑定前配置，内核 NVMe 必须为模块。加载器等待预期 controller 数量，
后续按测试盘 serial / BDF 定位端点，不能假设 nvme 编号等于 root 编号。

所有正向 case 依次执行 same-DID、disabled、enabled empty-miss、enabled
nonempty range-miss、slot7 和 overlapping priority；每一阶段、每个盘都检查
translated domain、driver/live/queues、独立 IOMMU group、DT attachment、1 MiB
direct 写读 SHA256 相等、I/O queue MSI 计数增加。非空 miss 的区间从 2^44
开始，普通 PCI DMA 截断到低 44 位后不可能命中。priority 的 slot0 为 RID，
slot7 为错误 DID，确保高编号不能覆盖低编号。

| Case 后缀（前缀 `bosc-did-mapper-linux-`） | 拓扑 | Tier |
| --- | --- | --- |
| single | single die0 p0，1 NVMe | MR |
| single-all-ports | single die0 p0/p1/p2，3 NVMe | extended |
| multi | multi 四 die 的 p0，4 NVMe | extended |
| all-roots | multi 四 die 全部 12 root | extended |
| sparse | multi die-mask=0x5，die0/die2 的 p2 | extended |
| multi-die0 | multi die-mask=0x1，三端口仍使用编码地址 | extended |
| default | 省略 soc-mode、die-mask=0x1，DT 与显式 multi 一致 | extended |
| fault-single | single p0 错误 DID，明确 fault、I/O 完成和 NVMe 恢复 | extended |
| fault-isolation | 12 root，仅 die3 p2 错误 DID，其余 11 盘 direct I/O 正常，再恢复目标 | extended |

稀疏 die 用例还检查共享 ACLINT 的 `interrupts-extended` 按物理 hart ID 排列，
保留未启用 die 的位置；例如 die2 的 hart32 仍使用寄存器索引 32。IMSIC 的
regset/parent 列表则按启用的 die 组织，两者不能使用相同的压缩规则。

负向 case 使用带唯一 token 的有界阶段，只豁免目标 IOMMU cause 258 / DID
0x200 以及对应 controller/namespace 的预期超时/I/O error。其他实例的 fault、其他盘错误、
panic、缺少故障记录、I/O 永久阻塞、恢复失败均为 FAIL。保留完整 UART；检查器
剔除预期错误行后继续检查 observer/recovery 和晚到日志，不全局忽略 fault。
为避免 SMP 下内核 printk 与用户态输出在 UART 中交错，负向阶段暂时降低
console loglevel，恢复配置后完整采集内核 ring buffer 为 `negative-kernel.log`，
保存其 SHA256，再恢复原 console loglevel。故障等待和检查使用实际内核记录，
不会因禁用 console 输出而省略 fault 检查。
NVMe core 模块加载时设置并读回 I/O/admin timeout 为 3s；宿主 case deadline
为 900s，普通正向为 600s。

Linux 同 DID 正向不能单独证明 override 到另一有效 DC；这个行为由 qtest 的
不同 DID/物理页和 Linux 的显式错误 DID 验证共同覆盖。原 `bosc-iommu-linux-*`
不配置 mapper，继续验证默认 RID fallback。

## 固定资产、运行与证据

新增 `did_mapper_kernel` / `did_mapper_modules`，来源为 Linux 干净归档提交
`1411c50e0909`，release `6.18.34-bosc-did-mapper`。Image 内建 helper、debugfs、
native IOMMU；NVMe 是模块。小型 ext4 payload 装入 pmem1，包含模块加载顺序
和模块 SHA256。配置/源码/compiler/制品身份见 `images.lock.json` 和 ASSETS.md。
普通 CI 只消费固定制品；手工重建后用 `package_mapper_modules.py` 包装。

两份资产已发布到 `linux/did-mapper/20261012/1411c50e0909/` 版本目录，
锁中的 publication 为 `verified-20261012`。此次干净重建后 mapper qtest
10/10、Linux 9/9 和框架自测 69/69 通过；远端 metadata 与空缓存实际下载
均已校验。来源、制品 hash 和证据目录见 ASSETS.md；CI 只读 token 和 GitLab
pipeline 尚未单独验收。本地可用精确 hash 的 `--asset` 覆盖。

```sh
python3 tests/ci/bosc/ci.py all --jobs 8 \
  --case qtest-did-mapper --case bosc-did-mapper-linux-single \
  --assets-dir <workspace> \
  --asset did_mapper_kernel=<Image> \
  --asset did_mapper_modules=<mapper-modules.ext4>
```

完整 mapper 组可在 `select --full` / fresh `build` 后用 `run --group did-mapper`。
MR 影响规则覆盖 mapper/AXI 源码及头文件；共享 IOMMU 修改同时选 KMH/BOSC。
GitLab 独立 job `bosc:did-mapper`，最终 report 消费该组。

保存 `mapper-evidence.json`、各阶段 guest 脚本、完整 UART/QEMU 日志、DT 及
逐 root 归属、scratch disk 清理状态、QEMU 构建身份。报告要求首次绑定、全部
配置/DMA 阶段和负向 case 的 completed I/O/observer/recovery，缺失不能 PASS。
本地验收不代表 GitLab pipeline 或远端下载已通过。
