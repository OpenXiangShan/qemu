# KMH BOSC checkpoint CI

所有用例通过现有 `ci.py` 入口，在 `checkpoint` 组执行，并进入统一的
选测、构建校验、进程组清理、实时日志和 fail-closed JUnit 报告。
所有 checkpoint case 均只接受显式手动选择，不进入 MR、schedule、默认分支、
tag 或 `--full` 的自动选测。普通功能说明、格式和 PLDM 装载协议保留在
`docs/system/riscv/kmh-bosc-soc-checkpoint.rst`；解压工具移到
`contrib/kmh-checkpoint/`。

## 用例矩阵

| case | 层级 | 验证范围 |
| --- | --- | --- |
| `bosc-checkpoint-v2` | manual | M→S→结束、single/multi/稀疏 DDR、continue、interval/QMP、损坏/拓扑拒绝 |
| `bosc-checkpoint-v3` | manual | U/S、单 die 四核、跨 die 两核、稀疏四核、寄存器/向量/timer/AIA、真实 trap、再次切片、短 interval/QMP、错误输入拒绝 |
| `bosc-checkpoint-raw` | manual | 稀疏解压、完整逻辑长度、raw `-bios` 恢复、截断/损坏/尾随输入拒绝 |
| `bosc-checkpoint-linux-single` | manual | v2 单核 Linux 连续两次切片、进程内存/文件校验、真实串口 shell 响应 |
| `bosc-checkpoint-linux-multi` | manual | 同上，multi SoC 仅启用 hart0，验证 DDR 布局，不作为多核覆盖 |
| `bosc-checkpoint-linux-2die32c` | manual | v3 五段镜像、switch_root 后/bash 前切片、32 核恢复、首个结果后 litmus 至少 10 秒 |
| `bosc-checkpoint-linux-2die32c-raw` | manual | 同一五段式场景，先解压成 raw，再只传 `-bios raw.bin` 恢复 |

v2/v3 裸机 case 均有逐项 checks；缺少、重复、失败的 checks 无法得到最终
PASS。Linux bounded case 必须包含完整 Observation 结果、正确的 online、
isolated 和允许核范围，以及实际至少 10 秒的观察，记录 host 主动结束与
null guest exit。Forbidden/Required witness 不符合预期时失败。

不以恢复完成的 host 日志单独证明“没有 host fixup”：原生 guest 同时检查
寄存器、设备状态及真实 trap 返回。真实 PLDM/RTL 仍属于未实跑范围。

## 输入与构建边界

- `build.py` 对选中 checkpoint 的 system profile 显式开启 Zstd；当前源码的
  QEMU 与 guest fixture 在构建阶段生成，运行阶段只消费 hash 校验的 bundle。
- 宿主需有 `riscv64-linux-gnu-gcc`、`riscv64-linux-gnu-objcopy`、`zstd` 和
  libzstd 开发包。Linux case 还需 debugfs/mke2fs/cp。doctor 按选中 case
  检查工具；其他测试不因此强制要求交叉工具链。
- `checkpoint_build.py` 在 work-dir 编译当前 guest C/汇编，记录源 hash、
  编译命令与工具链版本。legacy stub 的 BUILD_DIR 也放在 work-dir，
  不在源码树构建，不运行 native-resources 再生成。
- `checkpoint/guest/` 保存 guest 源码及链接脚本，
  `checkpoint/prepare-rootfs.sh` 只修改工作目录中的 rootfs 副本。
- Linux 普通输入沿用 `images.lock.json` 的 fw_jump/kernel/rootfs。
  2die32c 固定输入来自 20261015 五段包与 20261019 验证过的 no-map DT/fw_jump，
  使用新 asset ID `checkpoint_fw_jump/dtb/kernel/workload/trigger`。
- 五项 checkpoint asset 已于 2026-10-21 发布到 JF，`images.lock.json`
  锁定其下载路径、大小和 SHA256。手动 job 使用现有 JF 下载配置即可取用；
  也可提供同 hash 的离线输入/缓存。发布与下载证据见 `ASSETS.md`。
  远端发布不是普通 CI 的副作用。
- `checkpoint/prepare-firmware.py` 是独立制品准备工具，必须显式提供
  `--opensbi` 源码路径。它不由 CI worker 调用；固件更新后需重新锁定输入。
  仓库不再依赖开发机的相邻目录和日期命名 `test-artifacts`。

## 本地执行

只跑无需软件镜像的基础用例：

```sh
python3 tests/ci/bosc/ci.py all --case bosc-checkpoint-v2 \
  --case bosc-checkpoint-v3 --case bosc-checkpoint-raw --jobs 8
```

测试 2die32c 时提供锁定输入：

```sh
python3 tests/ci/bosc/ci.py all --case bosc-checkpoint-linux-2die32c \
  --assets-dir /path/to/offline-layout --jobs 8
```

离线文件布局由 `images.lock.json` 的 `path` 定义；也可重复使用
`--asset checkpoint_fw_jump=/path/fw_jump.bin` 等覆盖，size/SHA256 必须相同。
每次使用新的 work-dir，不覆盖旧证据，不修改旧 bundle manifest 来复用 build。

GitLab 的 `bosc:checkpoint` 为独立的 `when: manual` job，点击运行后调用：

```sh
python3 tests/ci/bosc/ci.py all --group checkpoint \
  --work-dir "$BOSC_CI_CHECKPOINT_WORK" --jobs "$BOSC_CI_JOBS"
```

本地也可直接使用 `all --group checkpoint` 执行全部七项，或重复 `--case`
只选需要的子集。`--group` 在新建 selection 时只允许用于 manual-only 组；
其他自动组保持原有 staged 语义。

手动 job 自行选测、构建、运行和汇总，工作目录固定为 `build/ci/checkpoint`，
不依赖自动 pipeline 的 selection/build artifacts，也不让自动 report 等待
手动结果。未点击时不会下载 checkpoint 镜像、编译 guest 或运行 QEMU；
`--full` 和定时任务也不会自动包含这些 case。

完整矩阵可直接消费锁文件中的 JF 输入，或使用同 hash 的离线镜像。
旧 pipeline 重试使用原提交的锁文件，需在包含本次发布路径更新的新提交上运行。
本地下载成功不代表 GitLab 专用只读 token 或线上 job 已通过。

## 结果与大文件

`results/<case>/qemu.log` 为各启动阶段按顺序追加的原始串口/QEMU 输出，
各阶段另保存日志与命令 JSON。统一 worker 产生带 run ID/config hash 的
`result.json`，report 再验证 checks 与观察证据后输出 JUnit。

切片、raw bin、rootfs 副本等大文件放在 `work/scratch/<case>/`；这些数据
不在 GitLab results artifacts 中，避免 64 GiB logical raw 上传膨胀。
保留 `checkpoint.json`、输入锁、命令和完整日志以重新制作镜像。scratch
仅由本次 run 所有，取消时只清理本次进程组，不停止其他开发进程。
