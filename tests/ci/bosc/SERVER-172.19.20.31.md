# 在 172.19.20.31 部署 BOSC/KMH CI

日期：2026-09-27。用户模式 Runner 已注册并运行，线上流水线尚待验收。无需 Docker。

## 最新进展：个人账号模式已注册并运行

2026-09-27 已按用户要求安装官方 Runner 17.1.2（与本机可访问 GitLab 帮助页显示的
17.1.2 对齐），没有使用 sudo、系统服务或新账号。安装位置：

- `/nfs/home/guoyaxing/.local/bin/gitlab-runner`；
- `/nfs/home/guoyaxing/.gitlab-runner/config.toml`，权限 600，注册凭据仅在本机保存；
- `/nfs/home/guoyaxing/ci-bosc/`，权限 700，含独立 builds/cache 和说明。

已准备 `/nfs/home/guoyaxing/ci-bosc/register-runner.sh`、`run-runner.sh` 和
`check-config.py`。注册脚本在本机终端隐藏输入项目 Runner authentication token，
不把 token 放进命令行或源码；启动脚本在未注册时拒绝运行，并用文件锁防止双实例。
全局 concurrent=1，注册脚本固定 limit=1、bash/shell，并禁用 CI debug trace。

首次部署时先在 GitLab 创建项目 Runner，再执行以下步骤；本机现已完成，无需重复注册：

```sh
/nfs/home/guoyaxing/ci-bosc/register-runner.sh
tmux new -s bosc-gitlab-runner
/nfs/home/guoyaxing/ci-bosc/run-runner.sh
```

完整个人模式步骤见本机 `/nfs/home/guoyaxing/ci-bosc/README.md`。此模式的项目变量
`BOSC_CI_CACHE_DIR` 使用 `/nfs/home/guoyaxing/ci-bosc/cache/assets`，不使用下文
系统账号方案的 `/srv` 路径。JF token 仍放 GitLab CI/CD Variables，不在脚本内。

官方 SHA256 校验、5 项配置自测、脚本语法和登录 shell 下的 doctor 均通过。
个人 `.bashrc` 的旧 module/devtoolset 命令仍打印警告，但未阻止本次 doctor；
未修改个人 shell 配置。用户随后完成注册和启动；2026-09-27 状态检查确认
`bosc-gitlab-runner` tmux 会话内进程运行，`gitlab-runner verify` 返回 `is valid`。
当时最近 120 行可见日志没有领取任务或报错的记录。此状态不代表线上流水线通过；
项目 custom CI path、JF 变量和实际 job 仍需检查验收。

个人模式让 CI 拥有 guoyaxing 的全部文件权限，只能运行明确审查过的可信提交；
不是安全隔离。17.1 系列已旧，建议服务端与 Runner 一起升级。下文独立服务账号
仍是长期部署建议，安装前检查结果保留为历史记录。

## 已完成与尚待完成

已在本机以 `guoyaxing` 完成独立源码 checkout、JF 空缓存下载、当前 QEMU 构建和
第一阶段全量验证。6 个运行镜像和 3 个固定构建依赖均实际从 JF 下载并按 SHA256
核验，12 个执行单元通过，报告为 21 PASS + 3 MCU 已知缺陷 XFAIL。
新制品已经发布，不需要 CI 下载 st-downloader 或编译 MCU。

最初安装前的本机检查结果（用户模式最新状态见上节）：

- 本机地址 `172.19.20.31`，Linux x86_64、Ubuntu 24.04、systemd。
- 未发现 `gitlab-runner` 程序、服务账号或 `/etc/gitlab-runner` 配置。
- 编译工具链/开发库已能完成本轮构建；新服务账号仍须运行 doctor 验证环境。
- QEMU origin 为 `gitlab.bosoc.cc:openxiangshan/qemu.git`。
- `http://gitlab.bosoc.cc/users/sign_in` 可达；HTTPS 当前返回 TLS SNI 错误。
- JF 为 `http://10.13.18.1:8082/artifactory/`，匿名文件访问返回 401。

后续已完成上述用户模式安装、注册和启动；仍需确认 GitLab 项目设置/CI secret，
并完成服务端 CI Lint 和真实流水线的 checkout/artifacts/调度验收。
只 push 代码不会自动配置项目变量或 CI 入口。

## 1. 管理员准备独立执行账号和目录

登录管理入口可以是 `guoyaxing@172.19.20.31`，job 执行账号应为专用非 root 的
`gitlab-runner`。不要把个人 workspace、SSH key、JF CLI 配置授权给该账号。
使用组织批准的软件源/安装包安装与当前 GitLab 兼容的 GitLab Runner；不要直接安装
不明版本。官方 Linux deb 包通常会创建同名账号和 systemd 服务，安装后检查：

```sh
gitlab-runner --version
getent passwd gitlab-runner
sudo systemctl status gitlab-runner --no-pager
```

安装包未创建账号时先由管理员创建服务账号；不要改成用 `guoyaxing` 或 root 执行 job。
Ubuntu 宿主依赖参考（已安装的无需重复配置）：

```sh
sudo apt-get install build-essential ninja-build pkg-config git \
  libglib2.0-dev libpixman-1-dev libfdt-dev zlib1g-dev \
  device-tree-compiler python3-venv python3-pip python3-yaml
sudo install -d -m 0750 -o gitlab-runner -g gitlab-runner \
  /srv/bosc-ci/builds /srv/bosc-ci/cache
```

系统 PyYAML 用于离线 YAML 检查；也可由管理员准备固定 venv 并把其 bin 加到 Runner
环境 PATH。不要在 job 内临时访问 PyPI，或依赖个人 shell 启动文件。
`doctor` 不自动安装软件。Runner 不需要 RISC-V 交叉编译器、JF CLI、Docker、KVM
或 sudo 权限。确认服务账号能访问 GitLab/JF、建立 Unix socket、使用 `/tmp`。

初始仅运行一个 job、编译 `-j8`。建议工作盘先预留 20 GiB 以上，首轮实测后再调整；
QEMU 的 128 GiB qtest 是稀疏地址空间，不等于必须预留 128 GiB 物理 RAM。

## 2. GitLab 创建项目 Runner，注册到本机

在 `openxiangshan/qemu` 项目的 Settings → CI/CD → Runners 创建 Project Runner：

- 标签：`x86_64,tcg,bosc-ci,host-172-19-20-31`。
- 关闭 Run untagged；仅分配给本项目。
- 先选择仅可信/受保护 ref 的试运行策略，MR 策略见下文。
- 若界面提供最大 job timeout，至少设置为 4800 秒（80 分钟）。

拿到 Runner authentication token 后在本服务器交互注册，不把 token 写进命令、
历史记录、聊天或仓库：

```sh
sudo gitlab-runner register \
  --url http://gitlab.bosoc.cc/ \
  --executor shell \
  --description bosc-tcg-172-19-20-31 \
  --builds-dir /srv/bosc-ci/builds \
  --cache-dir /srv/bosc-ci/cache
```

在提示时粘贴 authentication token；新式 Runner 的 tags/access level 等在 GitLab
创建界面配置，不依赖旧 registration token 流程。按安装的 GitLab/Runner 版本核对
注册向导。`172.19.20.31` 是执行节点，不是上面 `--url` 的 GitLab 服务地址。

在 `/etc/gitlab-runner/config.toml` 中保留注册产生的 ID/token/TLS 等字段，只调整
以下非秘密字段；不要用示例覆盖整个已有配置：

```toml
concurrent = 1
check_interval = 3

[[runners]]
  name = "bosc-tcg-172-19-20-31"
  url = "http://gitlab.bosoc.cc/"
  executor = "shell"
  shell = "bash"
  builds_dir = "/srv/bosc-ci/builds"
  cache_dir = "/srv/bosc-ci/cache"
  limit = 1
  request_concurrency = 1
```

`cache_dir` 是 Runner 缓存目录设置；镜像下载缓存另由 `BOSC_CI_CACHE_DIR` 控制，
两者不要混淆。若机器已有其他 Runner，global concurrent 会影响它们，应统筹调整。
检查 systemd 服务启动参数，确认 Shell job 以 `gitlab-runner` 执行，而不是 root。

```sh
sudo systemctl restart gitlab-runner
sudo gitlab-runner verify
sudo systemctl status gitlab-runner --no-pager
```

正常情况下 Runner 主动连接 GitLab 领取任务，不需要给 172.19.20.31 新开一个
入站 CI 监听端口。须允许到 GitLab 的连接（含配置的 clone/artifact 地址）以及
`10.13.18.1:8082` 的出站访问。GitLab clone 用 Runner/CI_JOB_TOKEN 的标准流程，
不要复制个人 SSH key。

## 3. 项目入口和变量

代码 push 后，在 Settings → CI/CD → General pipelines 把 CI/CD configuration
file 设置为 `.gitlab-ci.d/bosc.yml`。保留上游顶层 `.gitlab-ci.yml`；不要把它改成
include 全部上游 QEMU jobs。对实际提交的配置运行 GitLab 服务端 CI Lint。

项目变量按下表设置；环境 scope 使用 `*`：

| Key | Value / 要求 |
| --- | --- |
| `BOSC_CI_RUNNER_TAG` | `host-172-19-20-31` |
| `BOSC_CI_JF_BASE_URL` | `http://10.13.18.1:8082/artifactory` |
| `BOSC_CI_ALLOW_HTTP_ORIGIN` | `http://10.13.18.1:8082` |
| `BOSC_CI_JF_TOKEN` | 管理员安全填写的只读 Bearer token；Masked，版本支持时 Hidden，关闭变量展开 |
| `BOSC_CI_CACHE_DIR` | `/srv/bosc-ci/cache/assets` |
| `BOSC_CI_JOBS` | `8`（YAML 已给默认值） |

首次 JF 验收不要设置 `BOSC_CI_ASSETS_DIR`、`BOSC_CI_DEPENDENCIES_DIR` 或
`BOSC_CI_JF_PATHS`：默认锁文件已经指向核验过的真实路径。不要覆盖
`BOSC_CI_WORK=build/ci/pipeline`，因为 artifact 路径与之配套。

给 CI 专用 token 最小读取权限：

- `xs-sw/st-release/v0.9.99/fw_jump/fw_jump-kmh-v2-no-dtb.bin`；
- `xs-sw/st-release/v0.9.99/minimal/Image`；
- `xs-sw/st-release/v0.9.99/openEuler-minimal-rootfs/openEuler-minimal-rootfs-24.03-SP3-RVA23.ext4`；
- `hejian-workspace/guoyaxing/qemu-ci/` 的已发布输入（包括 `native-deps/`）。

CI 不需要上传、覆盖或删除权限，也不需要直接读取现有个人 `~/.jfrog` 配置。
本次获用户授权的上传/冷测使用了个人已有配置；这不等于已创建 CI 专用只读 token。
禁止开启 `CI_DEBUG_TRACE` 或输出 env，以免泄露 secret。

当前 HTTP 会明文传送 JF token、GitLab Runner/job token 和代码。只能在管理方认可的
可信内网使用；更推荐修复 GitLab/JF TLS 后切到 HTTPS 并删除 HTTP 例外。CI 没有
使用 `curl -k` 或关闭 HTTPS 证书验证。镜像 SHA256 校验不能保护传输中的 token。

### MR 的凭据和信任策略必须单独决定

Shell Runner 不是安全沙箱。独立用户只能限制操作系统权限，不能防止 MR 代码读取
同账号的缓存、文件、环境变量或已注入的 token。Masking 只隐藏日志，不能阻止外传。

建议分两步上线：

1. 首轮使用 Protected Runner + Protected JF token，在已审查的受保护分支手动跑
   全量。只信任明确批准的内部提交；不要在这台含开发数据的机器上运行任意 fork MR。
2. 要覆盖普通同项目 MR 时，管理员明确批准可信贡献者范围，再调整 Runner/ref 和
   只读 token 可见性。通常来自未保护分支的 MR 拿不到 Protected 变量，也用不了
   Protected Runner；不能一边配置全 Protected，一边假设所有 MR 自动可运行。

如需要测试不可信 MR，应先引入合适的隔离执行环境/无秘密输入方案，这是下一阶段
部署工作；不要简单把个人 token 改为 unprotected 来消除失败。Runner tag 只决定
调度位置，不是权限隔离。

## 4. 首次线上验收与 push 行为

1. 提交 `.gitlab-ci.d/bosc.yml` 和整个 `tests/ci/bosc/`，包括两份 lock、依赖恢复
   模块及检查器。不要提交本地 build、镜像、test-artifacts 或凭据。
2. 在 GitLab 确认 custom CI path 和变量，Runner 为 online，配置 Lint 通过。
3. 对已审查的提交选择 Run pipeline 手动全量运行。当前 workflow 支持 MR、默认
   分支 push、schedule、tag 和手动 web；普通非默认分支仅 push 且没有 MR，不创建
   这条流水线，这是避免重复流水线的设计。
4. 首个 job 确认实际主机/用户，doctor 成功；首轮用空缓存观察 JF 下载。build
   应恢复三个固定依赖并重新编译当前提交，不借用本机个人旧 build。
5. 等待 check → build → 五个 test group → report 完成。初始 Runner limit=1，
   五组 job 会依次执行。全量 report 期望 21 PASS + 3 明确 XFAIL，不应有 FAIL、
   XPASS、缺失结果或旧 run ID。
6. 下载 artifacts 验证 full UART、DTB、命令、镜像 hash、build provenance、
   JUnit/summary 均齐全；确认取消任务后测试进程会清理。
7. MR 策略批准后，补实际选测验收：BOSC 文件 → BOSC，KMH 文件 → KMH，
   公共代码/CI 文件 → 双 machine，纯文档 → 不编译/不启动 guest。

项目 job timeout 至少允许 YAML 中的 80 分钟 build；初始建议最大 artifact
200 MB 或更高，实际 bundle 约 76 MB（未压缩），远小于 rootfs。日志/artifacts
保留期为 14 天，最终 report 为 30 天，rootfs/cache 不上传为 GitLab artifacts。

Runner online 只证明注册成功，不证明 clone、JF 权限、完整构建或 artifact 上传
已通过；必须完成一次真实流水线后才能标记“部署验收通过”。
