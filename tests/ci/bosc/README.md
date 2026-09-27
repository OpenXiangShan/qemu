# BOSC/KMH 本地回归与 GitLab 薄入口

第一阶段先在 Linux x86_64 服务器本地运行。不需要 Docker、GitLab Runner、
RISC-V 交叉编译器或 st-downloader。只有当前 QEMU/qtest 在本轮编译；MCU、QSPI、
ITGen、OpenSBI、Linux 和 rootfs 都是 `images.lock.json` 锁定的输入。

测试清单和执行器是本地与 GitLab 的同一份代码。当前范围不是两个 machine 的全部
功能覆盖，详见 `cases.json` 的 deferred 字段。JF 制品已发布并完成真实下载验证；
本机用户模式 Runner 已注册；目标 GitLab CI Lint 和线上调度仍需部署验收。
Docker 化留到后续单独评估。

## 服务器环境

基线工具：Python >= 3.10、GCC/G++、GNU make、Ninja、pkg-config、Git、ldd、
GLib/pixman/zlib/libfdt 开发包，以及 dtc/fdtget/fdtput。需要 Unix socket、
正常的 `/tmp` 和足够磁盘空间。128 GiB DDR qtest 使用稀疏虚拟内存映射，不能把
QEMU 的 `-m` 或 vCPU 数直接当作宿主常驻内存/CPU 需求；先串行测量再调整并发。

`doctor` 只检查，不自动安装系统包。Ubuntu 24.04 的管理员可参考依赖包名：
`build-essential ninja-build pkg-config libglib2.0-dev libpixman-1-dev libfdt-dev
zlib1g-dev device-tree-compiler python3-venv python3-pip git`。
干净 checkout 缺少的三个 subprojects 由 `dependencies.lock.json` 从 JF 恢复，
按固定 revision/大小/SHA256 核验，再应用当前源码的 Meson packagefiles。已有 Git
checkout 必须匹配锁定 revision 且无 tracked 修改，不会 reset 或覆盖。configure
继续使用 `--disable-download`。修改 wrap revision 时必须审查并更新依赖锁。
完全离线时另设 `BOSC_CI_DEPENDENCIES_DIR`，目录按依赖锁的 `path` 布局；
`--assets-dir` 只指定运行镜像，不自动提供构建依赖。

Python 运行器及 QEMU QMP 模块使用标准库，离线 YAML 静态检查另需 PyYAML。
如服务器没有该依赖，在自己的工作目录准备 venv，不修改全局 Python：

```sh
python3 -m venv build/ci-env
build/ci-env/bin/pip install -r tests/ci/bosc/requirements.txt
source build/ci-env/bin/activate
python3 tests/ci/bosc/ci.py doctor --work-dir build/ci/preflight-01
```

QEMU configure 自己在本轮 build 目录创建 `pyvenv`，使用仓库提供的 Meson 1.8.1
和 pycotap 1.3.1 wheels。初始编译 `--jobs 8`，guest 用例串行；不自动使用整台
服务器所有逻辑 CPU。构建日志保留实际编译器和依赖版本。

## 一键本地全量

在 QEMU 仓库根目录执行，资产目录按锁文件的 `path` 布局：

```sh
python3 tests/ci/bosc/ci.py all --assets-dir /srv/bosc-ci/assets --jobs 8
```

没有指定 `--work-dir` 时自动创建 `build/ci/run-...` 独立目录，并打印绝对路径。
也可指定一个尚未运行过的目录。`all` 依次选测、预检查、自测/YAML 检查、资产校验、
新构建、guest 执行和汇总。不会覆盖已有选测或用例日志；重跑请使用新目录。

已有文件不必为了布局再复制一次，可用 `--asset NAME=PATH` 提供本地映射，仍严格
匹配锁定 hash。ID 为 `fw_jump`、`kernel`、`rootfs`、`mcu`、`qspi`、`itgen`：

```sh
python3 tests/ci/bosc/ci.py all --work-dir build/ci/local-01 \
  --assets-dir /data/software \
  --asset mcu=/data/validated/kmh-bosc-mcu.bin \
  --asset qspi=/data/validated/fw_payload.bin \
  --asset itgen=/data/validated/test_RISCV_AIA_aplic_irq_priority_cross_die.bin
```

工作目录不得包含空格、逗号或换行（QEMU loader 和 qtest 命令限制）。输入镜像复制
到本轮独立只读文件，然后由 loader 装入 guest RAM；guest 不直接写共享 ext4 文件。

## 分步运行与单用例复现

```sh
python3 tests/ci/bosc/ci.py select --work-dir build/ci/focused-01 \
  --case bosc-mcu-multi
python3 tests/ci/bosc/ci.py doctor --work-dir build/ci/focused-01
python3 tests/ci/bosc/ci.py assets --work-dir build/ci/focused-01 \
  --assets-dir /srv/bosc-ci/assets
python3 tests/ci/bosc/ci.py build --work-dir build/ci/focused-01 --jobs 8
python3 tests/ci/bosc/ci.py run --work-dir build/ci/focused-01 \
  --assets-dir /srv/bosc-ci/assets
python3 tests/ci/bosc/ci.py report --work-dir build/ci/focused-01
```

`run` 会重新验证输入，即使先运行过 `assets` 也不会直接信任旧记录。跨 GitLab
任务复制的是 `bundle/`，不是整棵 Meson build 树。相同源码的本地复现也可以把前一轮
完整 bundle 复制到新的已选测目录；运行前会检查 revision、tracked diff、二进制和
固件 hash，以及动态库。否则运行新的 `build`。每个 guest 目录中的
`replay-command.txt` 是不依赖已关闭 fd 的原始 QEMU 命令，原始 QMP argv 另存于
`process.json`；手动执行 replay 只是调试，不会更新验收结果。

`--base <revision> --head <revision>` 选择整个 merge-base 到 head 的差异，本地还会
合并 tracked/untracked 工作区修改，避免漏测未提交代码。缺少 diff 对象则全量。
`--case` 在 select/all 建立显式本地子集；`--group` 用于已经选测后的分组执行。

## JF：仅消费固定制品

设置以下环境变量（secret 由部署系统注入，不在命令行拼 token）：

| 变量 | 含义 |
| --- | --- |
| `BOSC_CI_JF_BASE_URL` | 制品根地址，默认要求 HTTPS，不含 token、userinfo、query 或 fragment |
| `BOSC_CI_JF_TOKEN` | 可选，只读 Bearer token；不应授予发布权限 |
| `BOSC_CI_ALLOW_HTTP_ORIGIN` | 内网 HTTP 的显式例外，必须精确匹配私有 IP 的 origin；通常不设置 |
| `BOSC_CI_CACHE_DIR` | 可选的 SHA256 地址化缓存目录 |
| `BOSC_CI_JF_PATHS` | 可选 JSON 文件，资产 ID 到真实 repo-relative 路径的映射 |
| `BOSC_CI_ASSETS_DIR` | 可选离线资产根目录，优先使用本地并严格校验 |
| `BOSC_CI_DEPENDENCIES_DIR` | 可选离线构建依赖目录，按 `dependencies.lock.json` 的 `path` 布局 |

锁文件的 `path` 保留离线布局，`jf_path` 是已核验的远端 repo-relative 路径。
现有 release 复用 `xs-sw/st-release/v0.9.99/`；新 MCU/QSPI/ITGen 和构建依赖位于
`hejian-workspace/guoyaxing/qemu-ci/`。可用路径映射适配其他部署，但只能改变位置，
不能改变大小/hash。例：`{"kernel":"existing-repo/v0.9.99/minimal/Image"}`。
CI 不读取个人 JF 配置或推断认证信息。

当前 JF 仅提供内网 HTTP，需要同时设置：

```sh
export BOSC_CI_JF_BASE_URL=http://10.13.18.1:8082/artifactory
export BOSC_CI_ALLOW_HTTP_ORIGIN=http://10.13.18.1:8082
```

这不是关闭 TLS 校验；HTTPS 仍验证证书，HTTP 例外不接受其他 origin。HTTP 会明文
传输 token，只能在管理员认可的可信内网使用，建议配置 TLS 后撤掉例外。SHA256
防止镜像内容被替换，不保护传输中的凭据。token 使用 GitLab masked 变量安全注入，
不写进脚本、命令行、日志或仓库。

缓存命中也校验；缓存损坏、下载错误、缺文件、大小或 hash 不符均失败，不静默换
版本、不自动编译固件、不跳过选中测试。HTTPS 保持证书验证，拒绝重定向以避免
认证信息传到其他站点；每次下载 socket timeout 30 秒，绝对上限 600 秒。
release 升级通过独立制品验证和锁文件 MR，普通 MR 不使用 latest。
制品来源和发布要求见 [ASSETS.md](ASSETS.md)。

## 首批覆盖和结果

共 12 个执行单元：9 个 Linux 配置、1 个 ITGen、2 个 qtest 可执行文件；后两者展开
为 9 个 qtest 子测试。因此功能用例是 9 Linux + 1 ITGen + 9 qtest。
JSON/JUnit 另外保留 qtest 容器结果与 3 个 MCU 已知缺陷断言，不能将行数等同于用例数。

Linux 检查实际在线 CPU 集合、固定 kernel release、机器 model、pmem0 挂载/overlay
根及有唯一标识的真实 shell 返回，不把回显或单个 PASS 当证据。BOSC single 地址不带
die-ID；multi 即使只开 die0 也带 ID。省略 mode 的 DT 与显式 multi 按字节比较。

MCU 用当前 QEMU 导出各自拓扑 DTB，再走外部 DTB MCU boot。这不是直接 generated-DTB
MCU 模式测试。预制 QSPI 组件通过额外 hash 检查，不在 CI 重组；各 die UART 分开保存。

`MCU-DIE-NUM-RESET` 是本地缺陷编号，不是假造的线上 issue。只有选中 MCU 的
`stage=start extra=0`（应为 1/4）可为 XFAIL；启动、copy、ready/release、online CPU
和 rootfs 仍必须通过。XPASS 阻塞并要求审查后移除旧豁免。启动 PASS 不证明 ready
barrier 正确。ITGen 只允许完整成功后的精确 `0x5006b` trap / double-fault 停车尾部。

默认阶段超时：DT 导出 30 秒、qtest 每个可执行文件 120 秒、ITGen 120 秒、DDR Linux
300 秒、MCU Linux 600 秒。worker 外层另有阶段预算加 120 秒的准备/清理硬上限。
QMP quit → TERM → KILL，只处理本次创建的进程/进程组；不会全局 pkill QEMU。

重要输出：

- `selected-tests.json`：路径与选测原因、revision、run ID、脚本/清单整体 hash。
- `doctor.json`、`configure.log`、`build.log`、`bundle/build.json`：环境及构建来源。
- `native-dependencies.json`：本次构建依赖的固定 revision 和实际来源。
- `results/<case>/`：完整 UART/QEMU 日志、命令、DTB/DTS、异常、清理和结果 JSON。
- `asset-evidence/`：本次使用的制品 hash/大小及 local/cache/JF 来源。
- `reports/all/summary.md`、`summary.json`、`junit.xml`：最终门禁报告。

PASS、FAIL、XFAIL、XPASS、NOT_SELECTED 分开统计。选中结果缺失、混入其他 run、
脚本变更、qtest 子结果缺失均不能全绿。失败由脚本退出码阻塞，JUnit 只是展示。

## Shell Runner 过渡接入

只在可信专用服务器、独立非 root 账号、独立 checkout 上注册 Shell Runner。
不得把个人开发账号/目录用作不可信 MR 的自动执行环境。Shell 没有容器隔离，
MR 自带代码可以读取该账号可访问的数据；只读 JF token 仍是秘密。
不向不可信 fork MR 注入凭据，Runner/受保护变量策略需部署方明确审核。

使用本目录同样的预检查和依赖。示意配置（不含注册 token，不代表已部署）：

```toml
concurrent = 1
[[runners]]
  name = "bosc-tcg-host"
  executor = "shell"
  shell = "bash"
  limit = 1
```

在注册配置中赋予 `x86_64,tcg,bosc-ci` 标签；固定主机时设置其独有 tag，并令项目
变量 `BOSC_CI_RUNNER_TAG` 为该 tag。初始同一 Runner 一次一个 job。
将项目 custom CI configuration path 指向 `.gitlab-ci.d/bosc.yml`，不改上游顶层入口。
目标 GitLab CI Lint、在线 artifact 合并和实际 Runner 调度仍需部署验收。

MR 使用统一 Python 影响映射；主分支、schedule、tag、手动执行第一阶段全量。
固定任务 DAG；未选中的组输出 NOT_SELECTED，不取得软件镜像、不执行 guest。
流水线不包含 image/services、Docker、固件编译或 JF 上传。
任务 artifact 上限至少需容纳 bundle（以本机实际大小为准），不上传 rootfs/cache。

本服务器具体目录、Runner 标签、项目变量及首次上线验收步骤见
[172.19.20.31 部署说明](SERVER-172.19.20.31.md)。

## 框架自测

```sh
python3 -m unittest discover -s tests/ci/bosc -p test_ci.py -v
python3 tests/ci/bosc/lint_ci.py
```

`lint_ci.py` 是离线语法和清单一致性检查，不能替代服务器端 GitLab CI Lint。
自测包括选测、rename/delete、未知路径、缺失/损坏资产、假 PASS、缺核、panic、
精确 XFAIL/XPASS、缺失/陈旧结果、timeout/cancel 清理和不影响其他进程。
