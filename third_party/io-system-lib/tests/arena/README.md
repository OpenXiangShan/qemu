# Arena SoC AXI 协同仿真

本目录验证完整 `arena_soc`：E6、RVV、AME NPU、内部总线和外设寄存器。
源码来自 `nanhu-fpga-aps` 的 `6c4d6dc1ab8197cac1a7ef2cb0e8a61fb9513402`，
已自包含导入到 `src/rtl_model/io_system/arena/vendor`，构建无需原仓库。
本阶段只连接 AXI master/slave 和时钟／复位。外部引脚使用确定的空闲值。

## 连接和范围

```text
QEMU MMIO -> io-system AXI64/ID16 -> 完整 AXI demux
                                    |-> 原寄存器 MMIO mux -> DMAC/APLIC/virtio
                                    `-> guard -> ID16:4 -> AXI64:512 -> RA config

RA DDR AXI512/ID8 -> guard -> AXI512:64 -> 四路 master mux -> io-system runtime
DMAC/APLIC-M/APLIC-S ------------------------^                 |
                                                     scheduler -> QEMU RAM
```

外部公共接口保持 64 位数据、16 位 ID。master ID `[9:8]` 区分
DMAC=0、APLIC-M=1、APLIC-S=2、RA=3；`[7:0]` 保留原 ID。
配置侧通过 ID remap 恢复完整 16 位响应 ID。

| 地址／功能 | 行为 |
|---|---|
| `0x50000000–0x5000ffff` | RA config AXI slave 可访问的 APB 区域 |
| `0x50000050` | E6 时钟使能 |
| `0x50000060` | E6 复位释放，协同仿真初始为 0 |
| `0x50000100` | E6 启动地址，本测试写 `0x80010000` |
| `0x50000120/140/160` | E6 完成／host 判定／E6 阶段握手 |
| `0x80000000–0xffffffff` | RA DDR decode，原物理地址送入 QEMU RAM |
| `0x310b0000`、`0x38000000` | QEMU host UART、CLINT |

支持合法 INCR：配置 SIZE 0–3，DDR SIZE 0–6，LEN 0–255（不能跨 4 KiB），
非对齐首拍、窄传输、稀疏／全零 WSTRB、独立 AW/W、背压和多 ID。
宽 DDR burst 经真实 RTL downsizer 拆成 64 位事务；runtime 每次向 scheduler
提交一拍有效字节区间，同方向同 ID 保序，整笔写完成后才回 B。

guard 对 FIXED、WRAP、exclusive、ATOP、越界 size／地址／4 KiB 请求回 SLVERR。
ATOP filter 对 AtomicLoad 同时提供 B/R 错误响应，无内存副作用。
不实现原子操作、IOMMU、迁移、RA IRQ 到 QEMU、TCM 下载或外部 SD/SPI/I2C/GPIO 模型。
通过 GEMM 不代表这些外设已验证。

## 构建

从本库根目录执行。使用支持 `install` 的工作区 picker template：

```sh
set +u
source /nfs/home/guoyaxing/vcs_env
set -u
export VCS_HOME=/nfs/tools/synopsys/vcs/V-2023.12-SP1-1
export PATH="$VCS_HOME/bin:/nfs/share/unitychip/bin:$PATH"
export VCS_LIBDIR="$VCS_HOME/linux64/lib"
make -j8 IO_SYSTEM_RTL_ARENA=1 all
```

产物为 `output-arena/libio_rtl_system_api.so` 和同目录的
`rtl-system-picker/`，运行时必须一起保留。`IO_SYSTEM_RTL_ARENA` 默认关闭，
默认基线构建仍使用 `build/`、`output/`。工具路径可通过 `config/paths.mk`
中的变量覆盖。波形与覆盖率默认关闭；构建时可设置 `IO_SYSTEM_RTL_WAVE=1`、
`IO_SYSTEM_RTL_COVERAGE=1`。`IO_SYSTEM_BUILD_JOBS` 默认 8。

启用波形时使用 FSDB，`IO_SYSTEM_RTL_WAVE_FILE=/absolute/path/arena.fsdb`
指定输出路径，父目录自动创建。不指定时文件名为
`io_system_rtl_wrapper.fsdb`，相对运行进程的工作目录。文件在仿真期间写入，
QEMU 正常退出时完成 FSDB 收尾；每次运行会覆盖同一路径。通过 QEMU Meson
构建时对应选项为 `-Dio_system_rtl_wave=true` 和
`-Dio_system_rtl_wave_file=/absolute/path/arena.fsdb`。修改开关或路径会重新编译
Picker 模型。

`-no_save` 禁止 VCS 重执行宿主进程。picker 的 close 已适配嵌入式进程生命周期：
先完成 FSDB 收尾，再释放 host 对象，不执行会退出 QEMU 的 `$finish`；
VCS 上下文保留至进程退出。
支持同一个 handle 的全局 reset 和复用；一个进程只初始化一个 VCS 模型。
MMIO 等待通过可选的 service callback 推进已有 scheduler 事务，不递归时钟步进。

## 完整自测

准备好支持 io-system 的 QEMU 构建目录（默认 `qemu/build-qti-check`），运行：

```sh
python3 tests/arena/selftest.py
```

该入口顺序验证源码哈希、runtime 引擎、Cmodel 回归、真实位宽 RTL、关闭 RA
的基线 RTL、picker 库、完整 SoC 同实例复位／换输入、QEMU qtest、连续时钟、
真实 QEMU GEMM。所有命令、退出状态、日志和 package SHA256 保留在
`build-arena/acceptance/`。使用 `--only engine|width|soc|legacy|qemu`
可重跑单层；`--qemu-build`、`--vcs-home`、`--vcs-env` 可覆盖环境路径。
验收使用动态库副本，避免正在测试的运行库被后续构建替换。

常用分层入口：

```sh
make arena-engine-test
python3 tests/arena/width_run.py
python3 tests/arena/selftest.py --only soc
python3 tests/arena/run_qemu.py --suite --pause-resume
```

`width_tb.sv` 使用真实 guard/converter 和独立字节 scoreboard；包含 2 KiB／4 KiB、
全部 lane、错误注入、W-before-AW、复位及满 4/16 个读上下文。
`standalone.cpp` 用 4 MiB 宿主 RAM 运行真实 E6/AME，依次 reset 后运行 case0、
case1、case0，检查数值、128 字节哨兵、握手和无 MMIO 时进展；同时启动 DMAC、
APLIC-M、APLIC-S，检查四路 master 的 ID 前缀及独立的内存／MSI 结果。

QEMU suite 包含两组输入 × outstanding 1/4/16、no-poll、WFI、非法 E6 指令，
由 QEMU 中执行的 RISC-V 程序读取结果并打印。结果必须是
`[16,-18,26,-2,35,-17]` 或 `[-2,1,-10,7,9,6]`，同时检查哨兵及 E6 ACK。
no-poll/WFI 期间不访问 RA MMIO，并通过 QMP 暂停／恢复；`test_continuous.py`
另用计数模型验证暂停期间时钟停止和 owner-thread 约束。
`rtl/tests/io_system_rtl_arena_smoke_tb.sv` 是真实 SoC 配置侧定向测试，不能加入
picker 的主文件列表。

## 来源及接续

`arena/SOURCE_MANIFEST.json`、`sources.json`、`cosim.patch` 记录每个原始文件及
导入文件的 SHA256、编译闭包与适配。E6/RVV/AME 生成文件未修改。
手写适配包括关闭原测试 monitor、复位保持、引脚空闲以及内部 CLINT 的
512→64 转换；保留 CLINT 的 64 位断言。

最终结论应以最新一次 `summary.json` 和真实运行日志为准；
时钟计数探针不是 E6/AME 通过证据。


## 已验证检查点（2026-09-11）

统一入口 `python3 tests/arena/selftest.py` 完成，17 个阶段全部通过，日志总表为
`build-arena/acceptance/summary.json`。三个 RTL 随机种子、关闭／开启 RA 的旧设备
回归、三个 outstanding 深度的同实例三轮复位与四路并发、9 项 QEMU qtest、
3 项时钟探针，以及 9 个真实 QEMU 用例全部正常退出。

最终运行包哈希清单：`build-arena/acceptance/package/sha256.json`。
API SHA256：`5ada3f605d7b2168ec32ec3ddaa294659567d16a4f87875dd4271b081afb0959`。
