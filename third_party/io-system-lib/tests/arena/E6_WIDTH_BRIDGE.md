# E6 AXIM 多 outstanding 位宽转换桥

## 实现范围

原 E6 `AXIAXI_64T512` 的转换核心只按全局请求 FIFO 选择返回 lane，不能处理不同 ID 的响应乱序或交织。Arena 构建现在使用 `io_system_rtl_e6_width`：

```text
原 64 位 AXIBRF 缓冲
  → AXI guard（支持窗口起点 WRAP 的等价归一化）
  → PULP axi_dw_converter（64→512，16 个按 RID 匹配的读上下文）
  → 原 512 位 AXIBUF 缓冲
  → 原 BMC300 互连
```

`E6x.sv` 只在 `IO_SYSTEM_RTL_ARENA` 下选择新模块，端口保持一致。生成的 E6/RVV/AME 核不修改，其他访存端口不改线。`import-arena.py` 可重现手写 E6x 的这一适配，`SOURCE_MANIFEST.json` 和 `cosim.patch` 记录来源。

共享 DDR 实例 `io_system_rtl_arena.u_ddr_width` 的 `AxiMaxReads` 从兼容性配置 1 恢复到 16。QEMU 的 `io2q-outstanding` 是另一个层次的参数，运行时也使用 16。C++ runtime 和 QEMU ABI 无需为新桥修改。

## 支持边界

- 标准窄访问保留正确的 byte lane；地址、访问大小及 beat 状态按返回 RID 关联。
- 不同 ID 可以并发、乱序或交织返回；同 ID 的后续请求等待其上下文可用，不覆盖旧请求。
- PULP 对可修改的 INCR burst 可能合并宽侧 beat；窄侧请求的值、长度和 LAST 语义必须保持。
- 原 guard 支持的窗口起点 WRAP 在新桥之前转换为等价 INCR；任意起点 WRAP 仍返回错误，不盲目改 burst 类型。
- 当前 guard 拒绝 FIXED、exclusive 等不支持的请求。被拒绝请求保持响应 ID 和读拍数，不产生下游访问。
- 保留原输入/输出缓冲，以支持独立通道背压。新的每 ID 上下文替代旧转换核心中的全局 lane 状态。

## 定向验证

从 io-system-lib 根目录运行：

```sh
python3 tests/arena/e6_width_run.py --output build-arena/e6-width-final
```

需要 Verilator 和 C++ 构建工具，不需要 VCS。脚本从已导入 E6 文件提取共同模块以避免重复声明，编译实际新桥、guard、PULP 转换器及原缓冲，记录编译命令、日志和源码 SHA-256。

`e6_width_tb.sv` 使用独立 byte-memory scoreboard，覆盖：

| 场景 | 通过标准 |
|---|---|
| 不同 ID 的逆序单拍响应 | ID、数据和 LAST 全部正确 |
| 两个 burst 交织并施加背压 | 独立 beat 进度，所有有效字节匹配 |
| 16 个不同 ID 同时 outstanding | 实际观测到 `max_pending=16`，逆序完成无错 |
| 同 ID 连续请求及跨 64 字节边界 | 上下文不覆盖，响应保持同 ID 顺序 |
| 窗口起点 WRAP | 宽侧为等价 INCR，窄侧各拍数据正确 |
| 每拍 1/2/4/8 字节读取 | 正确 byte lane |
| 可修改 INCR burst 合并 | 16 拍窄读数据完整还原 |
| 不支持的 WRAP | 错误响应，且无下游读请求 |
| 下游 SLVERR | 原 ID 的错误响应正确传播 |
| 稀疏/全零 WSTRB 和写背压 | byte-memory 最终状态及 B 响应正确 |
| 两个读请求未完成时复位，再复用 ID | 无旧上下文污染，新请求正确完成 |

实际结果（2026-09-12）：

```text
E6_WIDTH_PASS transactions=31 max_pending=16
```

证据在 `build-arena/e6-width-final/{result.json,run.log,compile.log,command.json}`。31 是已完成读事务数；复位中取消的请求不计入完成数。该测试通过表示新桥检查零数据错误，与旧的 `e6_bridge_order_run.py` “观察到缺陷才成功”的标准不同。旧复现用例保留，用于记录原桥限制。

## 完整 SoC 验证

在独立的 VCS 2023 模型包上执行：

```sh
python3 tests/arena/run_qemu.py \
  --library build-arena/new-bridge-package/libio_rtl_system_api.so \
  --vcs-libdir /nfs/tools/synopsys/vcs/V-2023.12-SP1-1/linux64/lib \
  --suite --pause-resume --output build-arena/new-bridge-gemm
python3 tests/arena/run_standalone.py \
  --library build-arena/new-bridge-package/libio_rtl_system_api.so \
  --vcs-libdir /nfs/tools/synopsys/vcs/V-2023.12-SP1-1/linux64/lib \
  --depth 16 --output build-arena/new-bridge-standalone
```

两组实际结果均通过：QEMU GEMM 9 个场景包括 depth 1/4/16、无轮询、WFI、暂停恢复及预期非法指令；standalone 在同一个实例执行 3 次复位/重载并检查 DMA/APLIC 并发访问。

RVV 全量运行及软件同步适配的最终状态另外记录在工作区 `e6_ra_subsystem/rvv_operator_cases/MIGRATION_STATUS.md`。桥的定向和 GEMM 通过不等同于所有 RVV 数值用例通过。
