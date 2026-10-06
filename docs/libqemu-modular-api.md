# 模块化 libqemu 的组织方式与 SoC 组装 API

本文面向使用 `qemu-xs-lib` 的宿主程序开发者，说明当前模块化 libqemu 的代码组织、公开接口、Kunminghu 组装流程、生命周期和边界。这里的模块化接口用于在宿主 C 程序中显式创建一个 QEMU SoC；它不引入 QBox、Lua、SystemC 或新的 QEMU 执行线程。

## 1. 总体分层

当前实现分成两个层次：

```text
宿主 board.c / examples/kunminghu.c
        |
        v
libqemu/compose.h
  类型化配置、opaque handle、设备组装和 DTB/启动接口
        |
        v
libqemu-compose.a
  compose.c       上下文、模块注册表、校验和设备创建
  compose-dtb.c   从已创建模块生成 DTB
  compose-boot.c  固件、Linux Image、DTB 和 reset trampoline 加载
  compose-kmh.c   Kunminghu 默认 profile 和 convenience builder
        |
        v
LibQemuExports / LibQemuCoreExports
        |
        v
libqemu_call -> dispatch BH -> QEMU worker + BQL
        |
        v
QOM、qdev、MemoryRegion、IRQ、ROM loader、QEMU main loop/vCPU
```

`libqemu/libqemu.h` 是基础库 ABI，提供 QEMU runtime、对象、内存区域、IRQ、定时器、ROM loader 和生命周期接口。`libqemu/compose.h` 是面向使用者的板级组装层，隐藏 QEMU 内部结构体，只暴露类型化配置和不透明句柄。

compose 层运行在调用它的宿主线程中，不创建自己的 BH、main loop 或 vCPU 线程。它调用基础 API，把需要修改 QEMU 拓扑的操作排入现有 QEMU worker 的 BH，并由 QEMU 的 BQL 和 main loop 执行。组装完成后，TCG vCPU 和 QEMU main loop 仍由 libqemu runtime 管理。

模块库只在配置启用 libqemu 时构建；普通 `qemu-system-riscv64` 的启动路径保持不变。

## 2. 源码组织

公开头文件和实现的职责如下：

| 文件 | 职责 |
| --- | --- |
| `libqemu/libqemu.h` | 基础 `LibQemuExports`、`LibQemuCoreExports` 和 ABI 版本 |
| `libqemu/compose.h` | 公开配置结构体、opaque handle、创建/校验/DTB/启动函数 |
| `libqemu/compose.c` | compose context、资源注册表、地址/IRQ/backend 检查和设备 builder |
| `libqemu/compose-internal.h` | compose 内部模块记录、地址范围、句柄和拓扑 generation |
| `libqemu/compose-dtb.c` | 根据成功创建的模块生成 CPU、内存、AIA、UART、PCIe、块设备等 DT 节点 |
| `libqemu/compose-boot.c` | raw 固件、Linux Image、DTB、reset ROM 的文件和内存布局检查及加载 |
| `libqemu/compose-kmh.c` | Kunminghu 默认地址、中断、启动参数和可选模块策略 |
| `libqemu/examples/kunminghu.c` | 显式调用各 builder 的宿主示例 |

`compose-kmh.c` 只保存 Kunminghu board policy。通用模块的创建逻辑放在 `compose.c`，这样更换地址或在未来增加另一块板时，可以复用 CPU、AIA、ACLINT、UART、PCIe 等模块。

## 3. 配置、句柄和错误

### 3.1 初始化配置

入口是：

```c
int libqemu_compose_init(const LibQemuExports *api,
                         const LibQemuComposeLaunch *launch,
                         LibQemuCompose **out, char **errp);
```

`LibQemuComposeLaunch` 描述一次 `-M none` 风格的启动环境：

```c
typedef struct LibQemuComposeLaunch {
    const char *accel;              /* 当前只支持 "tcg" */
    unsigned smp;                   /* 当前只能是 1 */
    const LibQemuComposeBackend *backends;
    size_t backend_count;
    const LibQemuComposeChardev *chardevs;
    size_t chardev_count;
} LibQemuComposeLaunch;
```

后端和 chardev 必须在 `libqemu_compose_init()` 前声明。init 会检查 ABI、TCG、SMP、backend ID、chardev ID 和基本参数，并把可供 NVMe 使用的 `-drive if=none,id=...` 后端准备好。它不会隐式创建 UART chardev，也不会创建任何 SoC 设备。

常用配置类型包括：

| 配置 | 作用 |
| --- | --- |
| `LibQemuSocConfig` | SoC model 和 DT `compatible` |
| `LibQemuRamConfig` | RAM/ROM 名称、基址、大小和是否为 system RAM |
| `LibQemuCpuConfig` | RV64 CPU 类型、reset vector、MMU 和 cache block 属性 |
| `LibQemuAiaConfig` | M/S IMSIC、M/S APLIC、source 数量和 guest interrupt files |
| `LibQemuAclintConfig` | MSWI、MTIMER、mtime 和 timebase 位置/频率 |
| `LibQemuUartConfig` | NS16550 MMIO、IRQ、chardev 和 console 标志 |
| `LibQemuVirtioBlkConfig` | my-virtio-blk MMIO、IRQ 和 raw image |
| `LibQemuDwcConfig` | DWC RC DBI、ECAM、CPU/PCI memory window、MSI/hotplug IRQ 和 domain |
| `LibQemuNvmeConfig` | NVMe 名称、已声明的 drive backend 和 serial |
| `LibQemuFinisherConfig` | finisher/test MMIO 基址 |
| `LibQemuBootConfig` | fw_dynamic、Linux Image、bootargs 和加载地址 |

### 3.2 不透明句柄

以下类型只在 `compose.h` 中声明，宿主程序不需要了解其布局：

```c
LibQemuCompose *compose;
LibQemuSoc *soc;
LibQemuCpu *cpu;
LibQemuAia *aia;
LibQemuPcie *pcie;
LibQemuDtb *dtb;
```

`LibQemuSoc`、`LibQemuCpu`、`LibQemuAia` 和 `LibQemuPcie` 的元数据由 compose context 管理。句柄在对应 context 释放前有效。`LibQemuDtb` 是独立拥有的对象，必须用 `libqemu_dtb_free()` 释放。

### 3.3 错误处理

所有 helper 都接受 `char **errp`，调用者应先把指针置为 `NULL`，并逐步检查返回值：

```c
char *error = NULL;
if (libqemu_soc_new(compose, &soc_cfg, &soc, &error)) {
    fprintf(stderr, "%s\n", error ? error : "compose failed");
    libqemu_compose_error_free(error);
    return -1;
}
```

compose helper 返回的错误使用 `libqemu_compose_error_free()`；直接调用 `LibQemuExports` 中的函数时，使用 `api->error_free()`。参数检查失败通常不会创建对应模块，可以修正配置后重试。QEMU 的 property/realize 失败可能已经创建部分对象，没有事务回滚；此时应请求 QEMU 停止、等待 worker 结束，再释放 compose 元数据。

## 4. SoC 模块 API

### 4.1 上下文和 SoC

```c
int libqemu_soc_new(LibQemuCompose *, const LibQemuSocConfig *,
                    LibQemuSoc **out, char **errp);
```

一个 compose context 当前对应一个 `-M none`、一个 RV64 hart 和一个 SoC。`soc_new()` 建立名称、模型、compatible 和模块注册表，但不创建 CPU 或外设。

### 4.2 内存

```c
int libqemu_soc_add_memory(LibQemuSoc *, const LibQemuRamConfig *,
                           char **errp);
```

创建 RAM 或 ROM 并映射到 system address space。`system_ram=true` 指定 Linux 的 DRAM 和启动加载区。名称必须唯一，基址和大小需要按页对齐且不能溢出或与已注册窗口重叠。注册表会保留固件可见的窗口范围，因此即使 QEMU 设备实际只响应较小寄存器范围，也会检查整个声明窗口。

### 4.3 CPU、AIA 和 ACLINT

```c
int libqemu_riscv_cpu_create(LibQemuSoc *, const LibQemuCpuConfig *,
                             LibQemuCpu **out, char **errp);
int libqemu_riscv_aia_create(LibQemuSoc *, LibQemuCpu *,
                             const LibQemuAiaConfig *, LibQemuAia **out,
                             char **errp);
int libqemu_riscv_aclint_create(LibQemuSoc *, LibQemuCpu *,
                                const LibQemuAclintConfig *, char **errp);
```

CPU builder 创建 QOM CPU，设置 reset vector、MMU/cache 属性、system memory link 和 reset 注册。当前只支持一个 CPU。

AIA builder 创建 M/S APLIC 和 IMSIC，并完成 CPU 外部中断、guest interrupt file 以及 M 到 S delegation 的连接。设备外部物理中断接入 M APLIC，而 DT consumer 使用 S APLIC；`num_ids` 不包含保留的 interrupt ID 0。`guest_files=5` 表示 S 文件之外的五个 guest 文件，当前 DT 使用三位 guest index。

ACLINT builder 创建 MSWI、MTIMER、mtime 和 `rdtime` wiring。其默认布局是 MSWI 在 `base`，MTIMER 在 `base + 0x4000`，MTIME 在 `base + 0xbff8`。

### 4.4 UART、my-virtio-blk 和 finisher

```c
int libqemu_uart_create(LibQemuSoc *, const LibQemuUartConfig *,
                        LibQemuAia *, char **errp);
int libqemu_my_virtio_blk_create(LibQemuSoc *,
                                 const LibQemuVirtioBlkConfig *,
                                 LibQemuAia *, char **errp);
int libqemu_finisher_create(LibQemuSoc *, const LibQemuFinisherConfig *,
                            char **errp);
```

UART builder 创建 serial-mm/NS16550、MMIO 映射、AIA level-high IRQ 和可选 `stdout-path`。chardev 名称必须是 init 阶段声明过的 ID。

my-virtio-blk 的 `image` 是设备 backend 直接打开的 raw image，不是 `LibQemuComposeBackend` 中的 QEMU `-drive` backend。两种磁盘应使用不同的可写镜像副本，避免两个模型同时写同一文件。

finisher 是可选的 test/poweroff/reboot MMIO 设备，通常用于让 guest 退出或复位。

### 4.5 DWC PCIe 和 NVMe

```c
int libqemu_dwc_create(LibQemuSoc *, const LibQemuDwcConfig *,
                       LibQemuAia *, LibQemuPcie **out, char **errp);
int libqemu_nvme_attach(LibQemuPcie *, const LibQemuNvmeConfig *,
                        char **errp);
```

`libqemu_dwc_create()` 创建 DWC root complex、DBI 和 ECAM/config 窗口、PCI memory window、总线以及 MSI 到 AIA 的连接。系统内存用于 PCI DMA 和 MSI；Linux DWC 驱动依据 DT `ranges` 配置 iATU。`hp_irq` 是为当前 Linux 驱动保留的第二个 IRQ 条目，QEMU 模型本身不产生 hotplug event；当前没有 INTx 或 IOMMU 模块。

NVMe 必须在 RC 创建后 attach。它使用 init 阶段声明的 `backend_id`，设置 serial 并 realize 到该 RC 的下游 PCI bus。创建 RC 不强制创建 NVMe，因此也可以在此处接入其他 PCI 设备。

## 5. DTB 和启动加载 API

### 5.1 DTB

```c
int libqemu_soc_build_dtb(LibQemuSoc *, const char *bootargs,
                          LibQemuDtb **out, char **errp);
const void *libqemu_dtb_data(const LibQemuDtb *, size_t *size);
int libqemu_dtb_write(const LibQemuDtb *, const char *path, char **errp);
void libqemu_dtb_free(LibQemuDtb *);
```

DTB 由已成功创建并登记的模块生成，不是独立的手写设备清单。builder 会生成 CPU、memory、AIA、ACLINT、UART、my-virtio-blk、PCIe/NVMe、finisher 和 `chosen` 等节点，并从同一份配置填写 `reg`、`interrupts`、phandle、`ranges` 和 `stdout-path`。

创建或修改模块后必须重新生成 DTB。DTB builder 会维护拓扑 generation，启动加载器会拒绝与当前拓扑不匹配的旧 DTB。`LibQemuDtb` 的 buffer 只读，调用者不能直接修改其内部存储。

### 5.2 固件和 Linux Image

```c
int libqemu_riscv_boot_load(LibQemuSoc *, const LibQemuBootConfig *,
                            const LibQemuDtb *, char **errp);
```

该接口加载 raw `fw_dynamic.bin`、raw Linux `Image`、DTB 和 reset trampoline。它不是 ELF/initrd loader。执行顺序是：读取文件、检查 DRAM 边界和文件重叠、检查 reset ROM 和对齐、登记 ROM blob；全部通过后才关闭模块拓扑，随后由宿主调用 `finish_qemu_init()`。

reset trampoline 向 OpenSBI 传入 hartid、DTB 地址和 dynamic info。固件运行时 scratch 空间需要调用者按固件构建布局预留。

## 6. Kunminghu profile 和完整调用顺序

`KmhProfile` 将 Kunminghu 的 board policy 集中起来：

```c
void kmh_profile_init_defaults(KmhProfile *);
int kmh_soc_build(LibQemuCompose *, const KmhProfile *,
                  LibQemuSoc **out, char **errp);
```

`kmh_profile_init_defaults()` 只填写默认配置，不创建 QEMU 对象。宿主程序可以在调用 builder 前修改 DRAM 大小、设备地址、IRQ、bootargs 或 `uart_enabled`、`blk_enabled`、`pcie_enabled`、`nvme_enabled` 等开关。`kmh_soc_build()` 按固定依赖顺序完成默认组合。

需要展示模块边界或插入自定义设备时，可以像 `libqemu/examples/kunminghu.c` 一样显式调用：

```c
libqemu_compose_init(api, &launch, &compose, &error);
libqemu_soc_new(compose, &p.soc, &soc, &error);

libqemu_soc_add_memory(soc, &p.dram, &error);
libqemu_soc_add_memory(soc, &p.rom, &error);
libqemu_soc_add_memory(soc, &p.sram, &error);
libqemu_soc_add_memory(soc, &p.flash, &error);

libqemu_riscv_cpu_create(soc, &p.cpu, &cpu, &error);
libqemu_riscv_aia_create(soc, cpu, &p.aia, &aia, &error);
libqemu_riscv_aclint_create(soc, cpu, &p.aclint, &error);

libqemu_uart_create(soc, &p.uart, aia, &error);
libqemu_my_virtio_blk_create(soc, &p.blk, aia, &error);
libqemu_dwc_create(soc, &p.pcie, aia, &pcie, &error);
libqemu_nvme_attach(pcie, &p.nvme, &error);
libqemu_finisher_create(soc, &p.finisher, &error);

libqemu_soc_build_dtb(soc, p.boot.bootargs, &dtb, &error);
libqemu_riscv_boot_load(soc, &p.boot, dtb, &error);
libqemu_dtb_free(dtb);

api->finish_qemu_init(&error);
api->wait(&status, &error);
libqemu_compose_free(compose);
```

真实代码必须在每一步检查返回值。完成 `boot_load` 后，宿主调用 `finish_qemu_init()` 让 QEMU 完成剩余初始化，然后调用 `wait()` 等待 guest 退出或收到 `request_shutdown()`。需要主动停止时，使用：

```c
api->request_shutdown(NULL);
api->wait(&status, &error);
```

`libqemu_compose_free()` 只释放宿主侧注册表和配置副本，不停止 QEMU，也不销毁全局 runtime；所以应在 `wait()` 返回后调用。

## 7. 基础 core API 扩展入口

模块化接口没有试图封装 QEMU 的全部 QOM。以下函数返回借用的基础句柄：

```c
LibQemuObject *libqemu_cpu_object(LibQemuCpu *);
LibQemuObject *libqemu_pcie_bus(LibQemuPcie *);
LibQemuObject *libqemu_soc_machine(LibQemuSoc *);
```

宿主可以将这些句柄交给 `api->core`，设置尚未有专用 wrapper 的 QOM 属性或挂接额外设备。代价是：直接 core 创建的对象不会自动加入 compose 注册表，因此不会自动参加地址/IRQ 冲突检查，不会自动生成 DTB 节点，也不会被 topology generation 检测覆盖。需要完整校验和 DT 描述的新设备，应增加正式的配置结构体、handle、builder 和 DT emitter。

## 8. 所有权、校验和失败边界

- 配置字符串在模块创建时复制；调用者可以安全地释放或复用原始配置存储。
- compose context 拥有模块元数据、资源注册表和 opaque handle；QOM object、MemoryRegion、IRQ 等底层对象仍由 QEMU runtime 管理。
- 名称在一个 SoC 内必须唯一；地址、窗口必须对齐、不能溢出或重叠。
- APLIC IRQ 必须在 `1..num_sources` 内，同一物理 source 不能分配给两个模块。
- 一个 context 当前只支持一个 CPU、一个 M/S AIA、一个 ACLINT、一个 system RAM 和一个 console；UART、PCIe、块设备的 builder 可以按当前模型规则重复使用，但验收覆盖的是单 hart、单 RC、单 NVMe、单 my-virtio-blk。
- 当前支持 TCG；`-M none` 的 `max_cpus=1` 限制没有改变。当前没有多核组装、IOMMU、ACPI、热插拔框架、其他 my-virtio 设备、多实例或动态插件模块。
- QEMU realize/property 失败可能留下部分对象，模块库不提供事务回滚，也不能隔离 QEMU 深层 fatal/exit。失败后应停止并等待 runtime，再释放 compose context。
- 一个进程只初始化一次，不支持 `dlclose()` 后重新构造 QEMU runtime。

## 9. 如何增加一个新模块

建议按以下顺序扩展：

1. 在 `compose.h` 增加面向用户的配置结构体、opaque handle 和创建函数。
2. 在 `compose.c` 增加参数校验、资源注册和 QOM/MemoryRegion/IRQ 创建逻辑。
3. 在 `compose-internal.h` 增加模块记录所需的内部元数据。
4. 在 `compose-dtb.c` 增加 DT 节点 emitter，并从同一份成功配置生成 `reg`、IRQ、phandle 或 PCI `ranges`。
5. 如果是 Kunminghu 专用默认策略，把地址和默认开关放进 `compose-kmh.c`，不要硬编码到通用 builder。
6. 增加不依赖 Linux 镜像的 compose 单元测试，再增加对应的整机启动测试。

设备的连接语义应留在模块内部；例如 AIA 的 IRQ wiring、DWC 的 MSI、CPU 的 system memory link 不应由每个宿主 board 文件重复实现。宿主只负责选择配置、决定模块顺序，并在模块之间传递公开 handle。

## 10. 相关示例和验证

- `libqemu/examples/kunminghu.c`：显式组装 Kunminghu 并启动 Linux、my-virtio-blk 和 DWC/NVMe。
- `libqemu/examples/README.kunminghu.md`：构建、镜像准备和运行参数。
- `docs/devel/libqemu.rst`：libqemu 构建、动态加载和生命周期。
- `docs/devel/libqemu-architecture.rst`：从 QEMU 到 libqemu 的线程、BQL、BH 和启动边界。
- `docs/devel/libqemu-compose.rst`：compose 实现细节和内部校验说明。
- `tests/libqemu/compose.c`：ABI、模块创建、移动设备、phandle、重复资源、DTB 和加载检查。
- `tests/libqemu/kunminghu.py`：Linux、my-virtio-blk、DWC/NVMe、读写、卸载/重挂、中断和宿主 `e2fsck` 回归。

构建后的安装树提供 `include/libqemu/libqemu.h`、`include/libqemu/compose.h`、`libqemu-compose.a` 和 pkg-config 文件。compose 是源码级 C 接口，宿主程序应链接对应版本的静态库并动态加载匹配 ABI 的 `libqemu-system-riscv64.so`。
