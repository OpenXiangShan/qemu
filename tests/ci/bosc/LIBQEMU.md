# libqemu CI：基础 API、SoC 组装与整机 Linux

`bosc:build-libqemu` 构建当前源码的动态库和宿主测试程序，`bosc:libqemu` 执行下表
16 个用例（15 个 MR、1 个 extended），`bosc:report` 汇总结果及 JUnit。它们与普通 QEMU 共用
`cases.json`、影响分析、固定镜像、日志和缺结果即失败的规则。库构建使用独立目录
和 `bundle-libqemu/`，普通 `bundle/` 继续提供关闭 libqemu 的 executable。

## 已纳入的 case

| Case ID | 测试点 | PASS 条件 |
| --- | --- | --- |
| `libqemu-lifecycle` | ABI version/size 拒绝、重复 start/wait、shutdown 幂等 | 当前 `tests/libqemu/lifecycle.c` 的 C 断言全部通过，退出 0 且 `LIFECYCLE_PASS` |
| `libqemu-rom` | RAM/ROM boot blob 复制、延迟生效、写保护、reset、越界/晚加载拒绝 | `ROM_RESET_PASS`，退出 0 |
| `libqemu-assembly` | 分阶段组装 CPU/RAM/MMIO/alias/独立 AS、ACLINT、IRQ、timer、回调重入和停止后拒绝 | guest 真正运行，读写/IRQ/timer 计数和 callback quiescence 断言通过；`ASSEMBLY_PASS`，退出 0 |
| `libqemu-autostart` | 不使用 `-S` 的自动运行路径 | 同 assembly，`running` 参数分支通过 |
| `libqemu-cancel` | finish 前取消 ASSEMBLY | `ASSEMBLY_CANCEL_PASS`，STOPPED 且退出 0 |
| `libqemu-concurrency` | 四个宿主线程提交 API 调用，同时关机 | 至少 1000 次调用后请求 shutdown，所有线程 join，STOPPED；`CONCURRENCY_PASS`，退出 0 |
| `libqemu-compose` | 模块创建、移动 UART/AIA/ACLINT/RC、DT phandle、可选节点、名称/IRQ/backend 冲突、启动文件/区域检查 | `COMPOSE_MODULE_DTB_PREFLIGHT_PASS`，退出 0 |
| `libqemu-compose-profile` | KMH profile convenience builder | `COMPOSE_PROFILE_PASS`，退出 0 |
| `libqemu-exit-start` | `start()` 下 virt guest finisher 返回 0/7，宿主继续运行 | 每次都有 `HOST_STARTED` 和对应 `HOST_CONTINUES status=0/7`，实际进程返回对应值 |
| `libqemu-exit-split` | `init()` → `finish_qemu_init()` 下相同 guest exit | 与上项相同；两条命令都必须含 `--split-init` |
| `libqemu-linux-start` | `start()` 启动四核 KMH Linux；宿主 GLib loop 与 QEMU loop 共存 | 下述 Linux、真实 shell 和 API shutdown 检查全部满足 |
| `libqemu-linux-split` | 分阶段初始化原 KMH machine 后四核 Linux | 同上；经 `init()` → `finish_qemu_init()` |

前八项复用 Meson 已注册测试的可执行文件和参数，各在新进程中加载当前库，
不在 GitLab test job 依赖 Meson build 目录。每个 case 单独保存结果；退出非零、
成功标记缺失、fatal、超时、取消均失败，不添加 XFAIL。C 宿主测试显式保留 assert。

Linux 两项复用 `images.lock.json` 的 `fw_jump`、minimal `kernel` 和 `rootfs`，
不引入新的 JF 镜像。QEMU 库生成 DT，使用 `-bios fw_jump`、loader 将 Image 放到
`0x80400000`、rootfs 放到 `0xa0000000`、trigger 放到 `0x90000000`，TCG/16G/4 核。
PASS 要求：model 正确，OpenSBI/Linux/pmem0/overlay 成功，随机 token 标记的
真实 shell 返回、在线 CPU 集合恰为 `0..3`、kernel release 匹配锁文件，无 fatal。
随后通过控制 pipe 让宿主调用 `request_shutdown()`，必须看到 wait 返回后的
`HOST_CONTINUES status=0`，确认宿主仍活着，再放行宿主退出并检查实际 rc=0。
不以 TERM/KILL 或仅看到 kernel banner 代替 API 生命周期验证。

## 从零组装 SoC 后启动 Linux

这四项使用 `libqemu-kunminghu`，从 `libqemu_compose_init()` 的 `-M none` 开始，
创建 CPU、RAM/ROM、M/S APLIC/IMSIC、ACLINT、UART 和可选存储，生成 DTB、调用
`libqemu_riscv_boot_load()` 后 `finish_qemu_init()`。没有调用 KMH machine 的初始化。

| Case ID | 层级 | 组装方式和设备 | 测试点 |
| --- | --- | --- | --- |
| `libqemu-compose-linux-minimal` | mr | 显式 builders，无 blk/PCIe | 单 hart Linux、RAM shell、可选节点缺省、UART/DT/API shutdown |
| `libqemu-compose-linux-blk` | mr | 显式 builders，my-virtio-blk | 上述启动 + vda ext4/direct I/O/重挂校验 + APLIC-MSI |
| `libqemu-compose-linux-nvme` | mr | 显式 builders，DWC/NVMe | 上述启动 + NVMe probe/ext4/direct I/O/重挂校验 + PCI-MSIX |
| `libqemu-compose-linux-dual` | extended | `kmh_soc_build()`，两种盘并存 | profile 组装、双盘路径/数据隔离、逐盘中断与 backend 清理 |

使用已锁定的 `iommu_kernel`（st-release-v0.9.99/xiangshan/Image），此处名称表示
资产 ID，不表示启用 IOMMU。已检查该 Image 的压缩 initramfs 中有 `bin/sh` →
`busybox`，内核配置内建 virtio-blk、DWC/NVMe 和 ext4。无需新增/编译 Linux。
固件使用同次源码 bundle 中有 SHA256 的 `opensbi-riscv64-xiangshan-kmh-fw_dynamic.bin`；
compose 的 raw boot loader 接收 dynamic handoff，不使用 DDR CI 的 fw_jump。

最小项只需内核；三个存储项另复用锁定 `rootfs`，每盘在 `scratch/<case>/` 下创建
独立可写副本。启动参数为 `rdinit=/bin/sh`，不挂磁盘作为 `/`。rootfs 盘只用于
提供工具及数据测试：工具从只读挂载复制进 guest RAM 后立即卸载。

所有项目检查 model、在线 CPU 恰为 `0`、锁定 kernel release、随机标记的完整 shell
回应，且 `/proc/mounts` 显示 `/` 在 rootfs/ramfs/tmpfs 中。DTB 的 console、CPU、
可选 blk/PCIe 节点和中断 phandle 与选择的设备一致。存储项目额外要求：

1. 设备集合准确；vda 来自 `310a0000`，NVMe 来自 `32000000.pcie`，BDF/serial 匹配。
2. 每盘 ext4 挂载，写入 256 KiB 随机数据（direct + fsync），保存 SHA256；卸载、
   重挂、direct 读回，SHA256 相同，再完全卸载。前后 `/tmp` 都在 RAM，磁盘不留挂载。
3. 对应 vda 的 APLIC-MSI 或 NVMe 非 admin 队列 PCI-MSIX 中断计数增长。
4. API shutdown/wait 返回 0 后，宿主仍存活；同一进程 namespace 检查磁盘 fd 均关闭。
5. 宿主退出码为 0，逐盘 `e2fsck -fn` 的退出码及完整日志均通过，计数错误或跳过
   journal recovery 即失败，不能先修复再判通过。
6. 无 panic、I/O error、NVMe timeout；普通结束、异常和 worker 外层清理删除本轮副本。

结果保留 `compose-probe.sh`、`compose-disks.json`、UART、DTB/DTS、IRQ/hash、
`vda-fsck.log`/`nvme0n1-fsck.log`、命令和 cleanup。磁盘不上传 artifacts。
手工 replay 前按元数据重建私有副本，不能写共享镜像。

## 构建与运行

库构建沿用 `build.py:CONFIGURE`，将 `--disable-my-virtio` 改为
`--enable-my-virtio`，并追加：

```text
--enable-libqemu --disable-plugins --disable-modules
--disable-opengl --disable-rust --extra-cflags=-UNDEBUG
```

即 riscv64-softmmu/TCG，关闭 KVM、io-system、tools、guest-agent、
GUI 和下载；PIC 由 `--enable-libqemu` 开启。普通构建显式
`--disable-libqemu`。两个 profile 都恢复 `dependencies.lock.json` 的固定归档，
只编译被选 case 需要的目标。库、宿主程序及随包固件均记录 SHA256、当前源码
revision、tracked diff 和新增源码 hash；执行前校验来源、hash 和动态库依赖。

以下在 QEMU 根目录执行，`ci_work` 使用尚未使用过的新目录。资产目录按锁布局
准备，也可由已配置的 JF/缓存提供；`assets-dir` 不包含原生编译依赖。

```sh
ci_work="$PWD/build/ci/libqemu-01"
python3 tests/ci/bosc/ci.py select --work-dir "$ci_work" \
  --case libqemu-lifecycle --case libqemu-rom \
  --case libqemu-assembly --case libqemu-autostart --case libqemu-cancel \
  --case libqemu-concurrency --case libqemu-compose --case libqemu-compose-profile \
  --case libqemu-exit-start --case libqemu-exit-split \
  --case libqemu-linux-start --case libqemu-linux-split \
  --case libqemu-compose-linux-minimal --case libqemu-compose-linux-blk \
  --case libqemu-compose-linux-nvme --case libqemu-compose-linux-dual
python3 tests/ci/bosc/ci.py doctor --work-dir "$ci_work" --group libqemu
python3 tests/ci/bosc/ci.py build --work-dir "$ci_work" --profile libqemu \
  --group libqemu --jobs 8 --dependencies-dir /srv/bosc-ci/dependencies
python3 tests/ci/bosc/ci.py run --work-dir "$ci_work" --group libqemu \
  --assets-dir /srv/bosc-ci/assets
python3 tests/ci/bosc/ci.py report --work-dir "$ci_work"
```

也可 `ci.py all --case libqemu-compose --jobs 8`，或不限制 case 运行全量。
`--group` 只过滤既有 selection；不会补入未选中的 case。
`--profile` 只用于 build；本地 all/build 默认按 selection 构建两个必要的 profile。

每项日志在 `results/<case>/`。原生测试为 `qemu.log` 和 `.command.json`；退出码
测试为 `exit-0.log`、`exit-7.log` 和生成的微型 finisher BIN；Linux 为 `uart0.log`
（含 host 输出）、DTB/DTS、`dt-evidence.json`、`assets.json`、`process.json`、
`cleanup.json` 和 `replay-command.txt`。完整日志随 artifacts 保存，页面实时展示
及失败尾部遵守共享预算。Linux replay 不带继承的 control fd，手工启动仅用于调试，
不会自动生成正式 PASS。跨 checkout 复现携带 bundle，不携带旧 Meson 树。

## 选测及后续覆盖

libqemu 源码/测试和 KMH 变化选中这些 MR 用例；公共 CPU/中断/运行时/构建代码、
未知路径的保守选测同样包含它们。文档变动遵循现有 docs 规则；BOSC 专属变动
不自动要求 KMH 库测试。默认分支、schedule/full 仍包含它们。

以下是适合继续添加的独立 case，当前尚未作为通过门禁：

- 普通 executable 的 my-virtio-blk/NVMe 双盘对照；当前普通构建仍关闭 my-virtio。
- 安装目录的 C/C++ 公开头、compose 静态库及 pkg-config 链接；旧宿主 ABI 前缀
  与新库的二进制兼容（当前 lifecycle 仅检查版本/大小拒绝）。
- 更广的配置组合拒绝、更多设备/多 root 组装。当前 compose 只支持单 RV64 hart，
  不应把原 KMH 四核 boot 视为组装 API 多核支持。

原 `libqemu-compose`/`libqemu-compose-profile` 仍为基础 API/DT 单元测试；
四个 `libqemu-compose-linux-*` 才验证从 `-M none` 组装后真正执行 Linux。
