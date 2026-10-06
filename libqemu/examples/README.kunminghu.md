# 宿主组装 Kunminghu，启动 Linux、my-virtio-blk 和 DWC PCIe/NVMe

`kunminghu.c` 从 `-M none -m 0` 开始，通过 `compose.h` 的类型化 C 模块创建和连接硬件。
模块内部调用公开的 `api->core`；示例只表达内存布局、模块配置和连接关系。
它不调用 `xiangshan-kunminghu` machine 或 `xiangshan.kunminghu.soc` 的初始化函数，
不包含 QEMU 内部头文件，也不需要 QBox。宿主链接 `libqemu-compose.a`，
依赖 libdl、pthread、GLib 和 libfdt。

这是采用 Kunminghu CPU、地址映射和中断布局的**单核 Linux 示例**：16 GiB RAM、
M/S IMSIC（5 个 guest interrupt files）、M→S APLIC、CLINT、UART0、
slot 0 my-virtio-blk、DWC RC0 和一个 NVMe。它不复制原 machine 的全部配置选项；
UART1、第二个 PCIe RC、IOMMU、ACPI 和其他 my-virtio 设备不在此示例范围内。
`none` machine 的 CPU 上限仍然是 1，普通 machine 的实现和默认构建选择不变。

## 编译

从仓库根目录执行（需要 libfdt 开发包和 QEMU 构建依赖；`dtc` 用于反编译查看 DTB）：

```sh
mkdir build-lib
cd build-lib
../configure --target-list=riscv64-softmmu --enable-libqemu \
    --enable-my-virtio --disable-docs --disable-sdl --disable-gtk \
    --disable-opengl --disable-vnc --disable-plugins --disable-kvm \
    --disable-rust --disable-tools --disable-guest-agent --disable-modules
ninja
cd ..
```

产物为 `build-lib/libqemu-system-riscv64.so` 和
`build-lib/libqemu/libqemu-kunminghu`，模块库为 `build-lib/libqemu/libqemu-compose.a`。
示例在组装阶段从成功创建的模块记录动态生成
DTB；`kunminghu.dtb` 目标只用于检查固定 DTS 样例，不是 launcher 的输入。

## 运行

仓库根目录提供了可直接启动的 `run.sh`，它自动复制测试 rootfs、启动本示例，
并在退出后清理临时副本：

```sh
./run.sh
```

默认使用仓库下的 `build-lib`；该目录没有 launcher 时，会检查本工作区已经验证的
`/tmp/qemu-xs-compose-build`。默认镜像在相邻的
`test-artifacts/virtio-blk-slot0-three-image-20260908` 目录。
也可以用 `BUILD_DIR`、`KERNEL`、`DISK` 和 `RUN_DIR` 覆盖默认路径；脚本启动时
会打印控制 FIFO 路径，另一个终端执行 `printf qx > /path/to/control` 可请求
libqemu 干净退出；向脚本发送 SIGTERM 也会请求干净退出。
`RUN_DIR` 是父目录，脚本在其中新建独立的 `kmh.XXXXXX` 子目录，保存两份磁盘和
`assembled.dtb`；清理只删除本次创建的文件，保留父目录中的其他内容。
设置 `KEEP_RUN_DIR=1` 可保留这些产物。`BOOTARGS` 覆盖内核命令行。

输入固件必须是 RV64 **fw_dynamic.bin**，内核必须是原始 Linux **Image**。
默认 bootargs 使用内核自带 initramfs 中的 `/bin/sh`。
磁盘是可写的 raw ext4 文件；建议使用独立副本，两个设备必须使用不同文件。

```sh
cp --reflink=auto /path/to/openEuler-rootfs.ext4 /tmp/kmh-blk.ext4
cp --reflink=auto /path/to/openEuler-rootfs.ext4 /tmp/kmh-nvme.ext4
build-lib/libqemu/libqemu-kunminghu \
    build-lib/libqemu-system-riscv64.so \
    pc-bios/opensbi-riscv64-xiangshan-kmh-fw_dynamic.bin \
    /path/to/Image /tmp/kmh-blk.ext4 /tmp/kmh-nvme.ext4 \
    --dump-dtb /tmp/assembled.dtb

dtc -I dtb -O dts /tmp/assembled.dtb
```

可用 `--append 'console=ttyS0,115200 earlycon=sbi root=/dev/vda rw rootwait'`
切换到磁盘根文件系统。此示例不加载外部 initrd。Linux 应内建
`VIRTIO_MMIO`、`VIRTIO_BLK`、`PCIE_DW_PLAT_HOST`、`BLK_DEV_NVME`、
`RISCV_IMSIC`、`RISCV_APLIC` 和 `EXT4_FS` 驱动。

串口使用 stdin/stdout。示例额外放置一个 SiFive test finisher，动态生成
syscon-poweroff/reboot 节点；内核支持这些驱动且用户空间提供 poweroff 时，
可用 `poweroff -f` 正常退出。验收镜像的最小 initramfs 没有 poweroff 命令，
自动测试使用下述 control-fd 关闭。
`--control-fd FD` 从继承的文件描述符（至少为 3）收到第一个字节或 EOF 后调用
`request_shutdown()`；`wait()` 返回后打印 `HOST_CONTINUES status=0`；
默认随后退出。自动测试额外使用 `--hold-after-wait`，收到第二个字节或 EOF 后
才退出，便于在宿主仍存活时检查磁盘 fd 是否关闭。libqemu 每进程初始化一次，不能 dlclose。

## 组装与启动边界

1. `init()` 解析 `-M none`，创建 stdio chardev 和 NVMe 的块后端，停在组装阶段。
2. 宿主创建 RAM/ROM、Kunminghu CPU，注册 CPU reset；创建 IMSIC、APLIC、CLINT，
   将输出接到 CPU GPIO，并将 UART/blk 中断接到 M APLIC。
3. 宿主创建 `designware-pcie-host`，取得 root bridge 的 `dw-pcie` 子总线，
   将 `nvme` realize 到该总线。PCI DMA/MSI 直接访问系统地址空间。
4. 模块库从实际拓扑生成 DTB，读取已 realize CPU 的 `isa-string`；
   检查启动文件、地址、重叠及 Linux Image 的有效内存大小，再注册 reset trampoline、
   OpenSBI dynamic info、固件、Image 和 DTB 的加载内容。无需外部 DTS 输入。
5. `finish_qemu_init()` 执行标准 QEMU machine-done、ROM 加载、reset 和 autostart。
   OpenSBI 初始化 AIA 并进入 Linux；Linux 的 DWC 驱动配置 iATU、枚举 NVMe。

| 硬件/内容 | 地址或连接 |
| --- | --- |
| Reset ROM | `0x1000`，a0=hartid、a1=DTB、a2=fw_dynamic_info |
| OpenSBI / Linux Image / DTB | `0x80000000` / `0x80200000` / `0xfe000000` |
| DRAM | `0x80000000` 起 16 GiB |
| my-virtio-blk | `0x310a0000` → APLIC 源 15 → `/dev/vda` |
| UART0 | `0x310b0000` → APLIC 源 10 → `ttyS0` |
| APLIC M / S | `0x31100000` / `0x31120000` |
| CLINT | MSWI `0x38000000`；MTIMER `0x38004000`，1 MHz |
| IMSIC M / S | `0x3a800000` / `0x3b000000` |
| DWC RC0 | DBI `0x32000000`；配置窗口 `0x67ff0000` |
| PCI BAR 窗口 | CPU `0x60000000` → PCI bus `0x40000000` |
| NVMe | RC0 下 `0000:01:00.0` → `/dev/nvme0n1`，MSI-X → S IMSIC |

新加的通用能力包括 `LIBQEMU_ROM` 和 `core->rom_add_blob()`（复制宿主 buffer，
交给 QEMU reset loader，限定在 ASSEMBLY 阶段）；QOM 属性包括
CPU 的只读 `isa-string`、ACLINT 的 `provide-rdtime`、APLIC 的 `delegate-parent`、
my-virtio-blk 的 `mmio-base/mmio-size/image-path/packed`、
DWC 的 `dma-to-system-memory`。这些属性解决原先只能经板级 C helper 配置的问题。
DWC 新属性默认为 false，旧板级 helper 的行为保持一致。

模块接口、依赖关系、参数所有权以及添加新设备的方法见
[`docs/devel/libqemu-compose.rst`](../../docs/devel/libqemu-compose.rst)。

## 在其他宿主程序中复用

公开头文件为 `libqemu/compose.h`，只依赖同目录的 `libqemu.h`。
未安装时，从仓库根目录编译宿主示例：

```sh
cc -I. libqemu/examples/kunminghu.c \
    build-lib/libqemu/libqemu-compose.a \
    $(pkg-config --cflags --libs glib-2.0) -lfdt -ldl -pthread \
    -o /tmp/kunminghu-host
```

`meson install -C build-lib` 会安装静态模块库、两个公开头文件、
`libqemu-compose.pc` 和 libqemu 动态库。安装后可以使用：

```sh
cc my-host.c $(pkg-config --cflags --libs --static libqemu-compose) \
    -ldl -pthread -o my-host
```

宿主通过 `dlopen()` 单独加载 `libqemu-system-riscv64.so`。
静态模块库不链接 QEMU 内部对象；它通过传入的 API 表调用动态库。
外部宿主使用这组 compose 头文件时，应重新链接配套版本的静态库；
compose 当前是源码级接口，不单独承诺跨版本二进制 ABI。

## 自动验收

使用内建 BusyBox initramfs 的 Linux 和包含 coreutils/glibc/libcrypto/zlib 的
openEuler ext4 镜像，从仓库根目录运行：

```sh
python3 tests/libqemu/kunminghu.py \
    --build build-lib --output /tmp/kmh-assembly-check \
    --kernel /path/to/Image --disk /path/to/openEuler-rootfs.ext4
```

脚本自动复制两个磁盘，检查 vda 的 MMIO 路径与 NVMe 的 DWC PCI 路径，分别写入
256 KiB 随机数据、fsync、卸载、重新挂载、SHA256 校验，再检查 virtio 和 NVMe
中断计数增长。宿主退出 QEMU 后检查两个磁盘 fd 都关闭，最后运行 `e2fsck -fn`。
输出目录必须不存在；其中保存命令、串口日志、磁盘副本和 fsck 结果。
成功标志为 `ASSEMBLED_KUNMINGHU_LINUX_BLK_NVME_PASS`。

加上 `--normal` 并指定普通 QEMU 构建目录，可对原 `xiangshan-kunminghu` machine
做相同的四核、双磁盘回归。
