# 固定制品来源与发布要求

2026-09-27 已完成首批 JF 发布及远端大小/SHA256 核验，当时包括
六个文件；release 三项复用现有正式文件，其他三项新上传。
随后从空缓存下载全部六项，在独立 checkout 构建 QEMU 并通过第一阶段全量验证。
CI 本身仍只有读取接口，不执行上传或固件构建。
当前锁文件包含十四项输入，按选中用例取用；其中五项 checkpoint 输入仅在手动运行时使用。

2026-10-27 的 APP 中断修复镜像由干净 Linux 提交
`867b35c450d24d5cde20bcf9f1cc0b135fc872d9` 构建，工具链仍为
`riscv64-linux-gnu-gcc 13.3.0`。QEMU 新增的 APP 锁存在 `APP + 0x80000`，
只清 DWC 的 `MSI_INTRn_STATUS` 会保持父中断；新内核分发 vector 后清除 APP
bits 15:8，再完成父中断 EOI。`iommu_kernel` 位于
`linux/iommu/20261027/867b35c450d2/`，DID mapper Image 与模块位于
`linux/did-mapper/20261027/867b35c450d2/`。三者已从 JF 重新下载并核对大小、
SHA256，随后使用空 CI work 目录验证下载器来源为 `jf-verified`。本地正式
harness 中 DID mapper 9/9、IOMMU 5/5 PASS；其中 BOSC all-roots 验证 12 个
NVMe 的 direct DMA 和 MSI。GitLab pipeline 重跑仍需包含本锁文件的新提交。

上一轮 PCIe outbound 配套镜像由干净 Linux 提交
`142f576d83ed424471143d9ec9cbc452a198297b` 构建（2026-10-19），
于 2026-10-21 发布；三份制品的 JF metadata size/SHA256 均已核验。
历史 `iommu_kernel` 位于 `linux/iommu/20261019/142f576d83ed/`，
`did_mapper_kernel` 和 `did_mapper_modules` 位于
`linux/did-mapper/20261019/142f576d83ed/`（均在下表 qemu-ci 发布前缀下）。

QEMU `e53ae7f5d29f` 将 BOSC ECAM/MEM 路由到 RC 的本地 44-bit 窗口，
并生成 `bosc,kmh-bosc-soc-pcie` compatible。旧 Linux `360ac10968fa`
缺少该 platform driver，因此未创建 PCI host/NVMe；即使只退回 generic compatible，
multi 模式也缺少 iATU BASE/LIMIT 的 die-id 修正。新内核启用
`CONFIG_PCIE_KMH_BOSC_SOC=y`，仅在 BOSC outbound iATU 比较地址上清除
`[46:44]`，保留 CPU resource、PCI target 和 inbound DMA 地址。
`CONFIG_IRQ_MSI_IOMMU=y` 及既有 iMSI-RX 支持继续启用。
mapper Image 和 NVMe 模块必须使用同一源码/配置成对更新。

qtest 另需在 `cases.json` 登记新增的 `outbound-multi`、`outbound-single`；
严格 TAP 清单检查会拒绝额外测试，即使 C 测试本身全部成功。
本轮 4 个 qtest 执行单元共 32 个子测试；另外验收 5 个 native IOMMU、
9 个 DID mapper 和 4 个消费同一 Image 的 compose Linux 用例。
正式 CI 命令、完整 UART/DT/结果、空缓存下载来源及发布记录保存在 workspace
`test-artifacts/pcie-outbound-ci-20261021/`，准确结果以其中最终
`reports/all/summary.json` 为准。此前旧镜像失败证据在
`/tmp/qemu-142-preserve/7372-repro-01/`。
这不表示 GitLab pipeline #7372 或 GitLab 专用只读 token 已验收。

JF 根地址：`http://10.13.18.1:8082/artifactory/`。
镜像锁的 `jf_path` 为实际远端位置，`path` 保留原离线布局：

| 输入 | 远端目录（相对 JF 根） |
| --- | --- |
| OpenSBI、Image、rootfs | `xs-sw/st-release/v0.9.99/` 下各原发布目录 |
| MCU BIN、QSPI payload、源码/构建证据 | `hejian-workspace/guoyaxing/qemu-ci/mcu/20260927/` |
| ITGen BIN、源码/ELF/原始 log | `hejian-workspace/guoyaxing/qemu-ci/itgen/aplic-cross-die/dieid2-20260924/` |
| IOMMU/compose Linux Image | `hejian-workspace/guoyaxing/qemu-ci/linux/iommu/20261027/867b35c450d2/` |
| DID mapper Image 与配套 NVMe 模块 | `hejian-workspace/guoyaxing/qemu-ci/linux/did-mapper/20261027/867b35c450d2/` |
| Checkpoint 五项输入、源码和验证记录 | `hejian-workspace/guoyaxing/qemu-ci/checkpoint/2die32c-20261019/` |
| 固定构建依赖源码 tar | `hejian-workspace/guoyaxing/qemu-ci/native-deps/` |

发布索引目录为 `hejian-workspace/guoyaxing/qemu-ci/baselines/` 下的
`st-v0.9.99-mcu-20260927-itgen-dieid2-20260924/`，包含 manifest、SHA256SUMS、
两份锁文件及本轮验证摘要。索引不是 CI 的替代信任根，CI 仍以提交的锁文件为准。

## Release 输入

- `st-release-v0.9.99/fw_jump/fw_jump-kmh-v2-no-dtb.bin`
- `st-release-v0.9.99/minimal/Image`，kernel release 为 `20260824_6.18_g3520522921abc-minimal`
- `st-release-v0.9.99/openEuler-minimal-rootfs/openEuler-minimal-rootfs-24.03-SP3-RVA23.ext4`

优先引用现有 JF release，而非另传一份。仓库内锁文件是校验信任基线，不自动信任
与镜像一同下载、可能一起被覆盖的 SHA256SUMS。已发布版本不得原地覆盖。

## MCU 20260927 快照

原维护位置：XSV `contrib/kmh-bosc-soc-mcu`。本地来源树为用户指定的
`xsv-qemu-flow-clean`，历史验证产物在工作区 `test-artifacts/kmh-mcu-20260927`。
不虚构对应源码 commit：以下源码快照 hash 是本版本的准确身份。

| 文件 | SHA256 |
| --- | --- |
| main.c | `18bf031199760ed9742b8ad34c5b08f09dbf5122f1c00b8394fa65337a140ce5` |
| start.S | `8f1f0b1808806f9491098eb7d86bda4ff046decb3a5eb214cdd2584d32e07bd8` |
| link.ld | `0a21a2a7d92818e47679fc36760033757c364b2f8b1798db1f5d46f391278d5c` |
| Makefile | `a0f1d0f5c46908ca1694af1941fa4df32a1bd0406e2085f6bf908703dbdd1762` |
| Kbuild | `cbce0a44a1ef7026614e80855b17b006e524617315a4c1837d536cbbd9fe61e2` |
| kmh-bosc-mcu.elf | `e4eb1be4c817f4a1fc799c49925da747e422f3411dbd7e44bd1c93b89196b39b` |
| kmh-bosc-mcu.map | `4f3d439b492a86fbc8214c6239c10d26c125457efe677efa1123edac2a3e9d12` |

许可证 GPL-2.0-or-later；工具链 `riscv64-unknown-elf-gcc 15.1.0 (g1b306039ac4)`；
原始构建 `make CROSS_COMPILE=riscv64-unknown-elf- -j2`，RV32IMAC_Zicsr、ILP32，
ELF32 entry `0x01000000`。BIN 为 1058 bytes，hash 见锁文件。这是本基线属性，
不是以后固件通用大小要求。普通 QEMU CI 不执行这条构建命令。

发布时同版本归档：BIN、ELF、map、上述源码快照及许可证、完整 build log、工具链
信息和三拓扑验证报告。源码仍在 XSV 管理，不为消费预编译 BIN 再复制进 QEMU。

## QSPI payload

`fw_payload.bin` 不是 MCU boot ROM；它是 MCU 经 DMAC 从 QSPI 复制到 DDR 的 payload。
当前锁定长度 12516216 bytes，闪存窗口 128 MiB：

| 偏移 | 输入 | 搬运后地址 |
| --- | --- | --- |
| 0 | 锁定的 fw_jump-kmh-v2-no-dtb.bin | 0x80000000 |
| 0x400000 | 锁定的 minimal/Image | 0x80400000 |

无内嵌 DTB；CI 每次由当前 QEMU 生成当前拓扑 DTB。发布保留组装 recipe、组件 hash、
输出 hash 和启动验证记录；本地原始 recipe 为 `.incbin fw_jump`、`.org 0x400000`、
`.incbin Image`，抽取 `.rodata` raw bytes。CI 不重新组装，不 preload 一个会被
128 MiB DMA 覆盖的独立内核。锁文件及运行器还核验 payload 内的组件 hash。

## ITGen

来源为 ITGen 导出目录
`export_kmh_bosc_aplic_priority_cross_die_hart0_hart16_dieid2_20260924` 下的
`AIA/001_test_RISCV_AIA_aplic_irq_priority_cross_die_62f69c45/`。
固定 BIN 56760 bytes；hash 见锁文件。发布同时归档对应 ELF、导出 C 和原始验证日志。
`source-evidence.tar.gz` 保存 cross-die C、ELF 和原始 log；其 include 的公共
`test_RISCV_AIA_aplic_irq_priority.c` 作为同目录独立文件发布，不能遗漏此依赖。

该 BIN 用 die0/core0 控制，die1/core0（软件 hart16）接收；硬件 target index=32，
IMSIC=`0x20001c000000`，56 个有效中断。通过后会执行 `0x5006b` 完成指令，输出
精确 TRAP 和 double-fault 停车行。检查器只在所有成功断言之后接受该有序尾部。

## 干净 checkout 的构建依赖

`dependencies.lock.json` 锁定 `berkeley-softfloat-3`、`berkeley-testfloat-3`、
`keycodemapdb`。归档来自相应 Git 仓库的准确 wrap revision，使用 `git archive`
保留源码和许可证，不带 `.git`，不带开发目录的 Meson overlay。恢复时检查大小/hash、
归档路径和类型，再叠加本次 QEMU 提交的 `subprojects/packagefiles`。
本轮冷测三项均从 JF 下载并恢复，没有借用个人目录中已经填充的 subprojects。

Python/Meson 使用仓库内 wheels，宿主系统开发包仍由服务器管理员准备；这不是
把 GCC、GLib 等整套宿主环境做成 JF 镜像。

## 更新基线

软件发布与 QEMU 回归分离：由有权限的发布流程构建新软件，独立验证并上传新的
不可覆盖版本，再通过 MR 更新锁文件/预期行为并运行第一阶段全量。普通 QEMU MR
不持有发布权限，不会验证 MCU 源码能否重新构建，不会自行修复 MCU_DIE_NUM 缺陷。

## 原生 IOMMU 与 mapper 构建来源

当前 Linux source tree 为 `e653527d43357a779da5c3352c0c640d9532fa73`。
使用该提交的 `git archive`，保留其中的 `initramfs.cpio`，不使用 dirty Linux
工作树的 Image。工具链为 `riscv64-linux-gnu-gcc 13.3.0`
（Ubuntu 13.3.0-6ubuntu2~24.04）；原生 Image release 为 `6.18.34`。

```sh
make -C "$src" O="$native_out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- defconfig xiangshan.config
make -C "$src" O="$native_out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j8 Image
cp -a "$native_out" "$mapper_out"
"$src/scripts/config" --file "$mapper_out/.config" --enable DEBUG_FS \
  --disable LOCALVERSION_AUTO --set-str LOCALVERSION -bosc-did-mapper \
  --module BLK_DEV_NVME --module NVME_CORE
make -C "$src" O="$mapper_out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- olddefconfig
make -C "$src" O="$mapper_out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j8 Image modules
python3 tests/ci/bosc/package_mapper_modules.py --build "$mapper_out" --output "$publish/mapper-modules.ext4"
```

两种配置内建 RISC-V IOMMU、DWC、BOSC PCIe、virtio-blk、pmem、ext4 和
mapper debugfs helper。原生配置内建 NVMe；mapper 配置将 NVMe 编为模块，
release 为 `6.18.34-bosc-did-mapper`，使测试能在首次 NVMe probe 前配置 mapper。
包装器检查模块依赖/vermagic，生成只读 pmem1 使用的 16 MiB ext4 payload。
每项准确大小、SHA256、config/initramfs hash 在 `images.lock.json` 中。

原构建使用宿主生成的 build timestamp/user/host，未设置固定 KBUILD 时间；
发布 provenance 保留实际 `compile.h`、`utsversion.h`、配置、源码归档、
模块 manifest 和构建命令。原编译 stdout 未单独保存为文件，后续 make 日志
仅证明增量完成检查，不冒充首次完整编译日志。vmlinux/System.map 同版本归档。

旧的 20261007、20261012、20261015 版本保留，不原地覆盖。
新版本附带来源、许可证、SHA256SUMS 和验收索引；远端 metadata 核验与实际
CI 下载/执行分开记录。使用已有用户 JF 认证不证明 GitLab 专用 token 的权限。
若部署设置 `BOSC_CI_JF_PATHS`，须同步更新三项路径；CI 本身只读不发布。

## Checkpoint 五段镜像（2026-10-21 已发布）

`checkpoint_fw_jump`、`checkpoint_dtb`、`checkpoint_kernel`、
`checkpoint_workload`、`checkpoint_trigger` 已发布到上表 checkpoint 目录，
逐项核验了 JF metadata 的 size/SHA256。原有锁定字节与离线 `path` 保持不变，
新增 `jf_path` 并移除未发布标记；`rootfs` 继续复用现有 release 输入。
手动运行 `bosc:checkpoint` 可按锁文件下载全部输入，不参与自动选测。

同目录 `source-evidence.tar.gz` 保留 OpenSBI `c620d76df37e` 源码归档和许可证、
派生 DTB/DTS、固件 ELF/配置/构建命令与日志、内嵌 DTB 校验、原始五段包说明，
以及历史 QEMU 源码快照和验证记录。历史 CI 报告按原状态保留，不作为新流水线
通过证明。原始 Image 内嵌版本为 `6.18.34-00133-g28347680f615-dirty`，
未保留其完整 dirty Linux 源码和首次构建日志，不能声称来自干净源码重编。

本次发布及空缓存下载证据位于 workspace
`test-artifacts/checkpoint-jf-publish-20261021-y5mkxqm4/`。
`publish-inputs.published.json` 记录远端 metadata；
`cold-download/asset-evidence/checkpoint.json` 记录全部八项输入的真实下载来源。
下载使用现有用户 JF 认证，不代表 GitLab 专用只读 token 或线上流水线已验收。
旧 pipeline 重试仍使用旧锁文件，须在包含本次锁文件更新的新提交上运行。

固件制品准备脚本为 `checkpoint/prepare-firmware.py`，要求显式传入
`--opensbi`。普通 CI 不调用它、不重编 OpenSBI，也不会自动发布制品。
