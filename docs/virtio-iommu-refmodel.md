# virtio-iommu 协议与 IOMMU ref cmodel 对接说明

本文说明 `my-virtio-lib` 中 virtio-iommu 的协议边界、地址转换路径，以及
它与 vendored RISC-V IOMMU reference C model（以下简称 refmodel）的关系。

## 1. 总体架构

```text
Linux virtio-iommu 前端
        │ virtio-mmio / virtio-pci
        ▼
my-virtio-lib/virtio/virtio_iommu.c
        │ 请求解析、VQ、fault、地址转换入口
        ▼
third_party/iommu_refmodel/src/io_virtio_iommu.c
        │ virtio 语义适配、domain/endpoint 状态
        ▼
RISC-V IOMMU ref cmodel / libiommu
        │ 页表、DDT、命令队列、S1/S2 转换，返回目标地址
        ▼
virtio 数据路径 → 平台 memory/MSI callback → 真实内存或 MMIO
```

`my-virtio-lib` 不依赖 `io-system-lib`。refmodel 代码作为
`my-virtio-lib/third_party/iommu_refmodel` 的一部分编译，并由
`io_virtio_iommu_*()` 接口封装。

## 2. 标准 virtio-iommu 协议

virtio-iommu 的设备 ID 是 `23`。当前设备使用两个 split virtqueue：

| VQ | 功能 |
| --- | --- |
| Request VQ（0） | 前端提交 ATTACH、DETACH、MAP、UNMAP、PROBE 请求；设备在写 descriptor 中返回 status |
| Event VQ（1） | 设备异步报告 DMA fault；前端预先提供可写 fault buffer |

配置空间对应 `struct virtio_iommu_config`，包括：

- `page_size_mask`：支持的页大小；
- `input_range`：可接受的 IOVA 范围；
- `domain_range`：domain ID 范围；
- `probe_size`：PROBE 属性空间大小；
- `bypass`：是否支持 bypass。

### 2.1 ATTACH / DETACH

ATTACH 把 endpoint 绑定到 domain：

```text
ATTACH { domain, endpoint, flags }
```

之后该 endpoint 的 DMA 使用这个 domain 的地址空间。`flags` 可以包含
`VIRTIO_IOMMU_ATTACH_F_BYPASS`。DETACH 解除绑定。

### 2.2 MAP / UNMAP

MAP 建立一段 IOVA 到物理地址的映射：

```text
MAP { domain, virt_start, virt_end, phys_start, flags }
```

flag 为：

- `READ`：允许设备读取内存；
- `WRITE`：允许设备写入内存；
- `MMIO`：该映射指向 MMIO 区域。

UNMAP 删除 IOVA 区间。请求完成后，设备将 `S_OK`、`S_INVAL`、`S_RANGE`、
`S_NOENT`、`S_FAULT` 等状态写入 response tail。

### 2.3 PROBE 与 fault

PROBE 查询 endpoint 能力和保留区域，例如 MSI 保留内存。发生未映射访问、
权限错误或非法 endpoint 访问时，设备通过 Event VQ 写出：

```text
{ reason, flags, endpoint, address }
```

当前实现把 refmodel fault 转换为 virtio-iommu fault 格式。

## 3. 控制面与数据面

MAP/UNMAP 是控制面操作；实际 DMA 是数据面操作，两者不能混为一谈。

```text
控制面：Linux → Request VQ → virtio-iommu → refmodel map/unmap

数据面：virtio-blk/genirq DMA
             → virtio_iommu_translate()
             → refmodel translate
             → guest_mem_read/write 或 send_msi
```

在 `my-virtio-lib` 中，virtio-blk、virtio-genirq 等设备通过自己的
`endpoint_id` 调用 `libvirtio_gphys_read()`、`libvirtio_gphys_write()` 或
`libvirtio_genirq_send_msi()`。这些入口在有 IOMMU 时调用
`virtio_iommu_translate()`；失败时调用 `virtio_iommu_report_fault()`。

## 4. my-virtio-lib 与 refmodel 的职责边界

### my-virtio-lib 负责

- virtio-mmio 寄存器和 feature negotiation；
- Request/Event VQ 的 descriptor 解析；
- 请求长度、字段、little-endian 和 flag 校验；
- virtio status 与 errno 的转换；
- endpoint 与 virtio 设备的关联；
- DMA 访问入口和 fault Event VQ；
- 选择设备实例的 S1、S2 或 S1+S2 模式。

### refmodel 负责

- domain、endpoint 和映射状态；
- S1 页表和 S2 页表；
- RISC-V IOMMU DDT、命令队列和 IOTINVAL/IOFENCE；
- 地址权限和 MMIO 属性；
- 实际 IOVA 翻译、页表 walk、A/D 位和 fault 原因；
- 返回转换后的地址，由 virtio 数据路径调用平台 memory/MSI callback。

当前适配器的 DDT、CQ/FQ 和 S1/S2 页表由 C heap 中的 shadow pages
保存；refmodel 的 translation memory callback 读写这些私有页，不访问
QEMU guest RAM。VQ buffer 和设备 DMA 数据才经平台 guest-memory
callback 访问目标地址空间。XSV 的这类访问需要在协程中执行。

典型调用关系如下：

```c
io_virtio_iommu_attach(...)
io_virtio_iommu_map(...)
io_virtio_iommu_unmap(...)
io_virtio_iommu_translate(...)
```

virtio 层不直接修改 RISC-V IOMMU 页表，也不直接管理 DDT 或 command queue。

## 5. S1、S2 和 S1+S2

标准 virtio-iommu 的 MAP 请求没有 `stage`、`HGATP`、`IOHGATP`、VMID 或
`VS-file` 字段。因此标准协议本身不直接描述 RISC-V 的两阶段硬件状态。
当前工程通过设备实例配置选择模式：

```text
my-virtio-iommu-mode=s1
my-virtio-iommu-mode=s2
my-virtio-iommu-mode=two-stage
```

### S1 only

```text
设备 IOVA → S1 → 物理地址
```

用于 host Linux 直接使用 virtio-iommu 的场景。

### S2 only

```text
设备使用的 GPA → S2 / IOHGATP → HPA
```

用于 VFIO platform 直通。inner Linux 不需要看到 IOMMU，设备直接使用 GPA，
outer virtio-iommu/refmodel 完成 GPA 到 HPA 的转换。

### S1 + S2

```text
设备 IOVA → S1 → GPA → S2 / IOHGATP → HPA
```

该模式需要区分 inner domain 和 outer domain。当前 refmodel 通过额外的
relay API 保存 namespace、inner domain、endpoint 和 outer domain 的关系，
避免把 inner domain ID 直接当成 outer RISC-V IOMMU domain ID。

## 6. relay 扩展不是标准 virtio 协议

以下接口是工程扩展，不属于 virtio-iommu 标准 Request VQ：

```c
io_virtio_iommu_relay_attach()
io_virtio_iommu_relay_detach()
io_virtio_iommu_relay_map()
io_virtio_iommu_relay_unmap()
```

它们用于 inner Linux 也拥有 virtio-iommu 的 nested 场景：

```text
inner virtio-iommu → KVM/QEMU relay → outer refmodel
```

VFIO platform + S2 场景不需要 relay。此时 inner guest 不枚举 IOMMU，VFIO
设备的 DMA 直接进入 outer endpoint 的 S2 地址转换。

## 7. MSI/AIA 的特殊边界

MSI 写入本质上也是设备 DMA。genirq 的 MSI 地址先经过
`virtio_iommu_translate(..., ACCESS_WRITE)`，再写入 IMSIC 目标页。

VFIO platform 直通时，还需要 KVM AIA 维护：

```text
inner IMSIC GPA → hardware VS-file HPA
```

这部分不是标准 virtio-iommu MAP/UNMAP 自动表达的，而由 KVM 与 VFIO 的
扩展接口管理：

```c
kvm_riscv_aia_dma_attach()
kvm_riscv_aia_dma_update()
kvm_riscv_aia_dma_detach()
```

因此职责边界是：

- virtio-iommu/refmodel：负责 endpoint DMA 地址转换；
- KVM AIA：负责 VS-file 分配、迁移、释放及 GPA→VS-file HPA 映射；
- VFIO platform：在设备打开/关闭时触发 KVM AIA DMA attach/detach；
- virtio-genirq：发起 MSI，并通过 virtio-iommu 数据路径发送。

## 8. 关键约束

1. 标准 virtio-iommu 只规定 domain/endpoint/IOVA 映射，不规定 RISC-V
   `IOHGATP`、VMID 或 KVM VS-file 生命周期。
2. `phys_start` 的含义取决于所在层次：S1 中通常是 GPA，S2 中通常是
   HPA；两阶段场景必须由上下文和 relay 语义区分。
3. IOMMU 的 Request VQ 只改变转换状态；设备数据访问必须显式调用
   `translate()`，不能只提交 MAP 请求而绕过数据面。
4. refmodel 是地址转换和页表语义的唯一实现边界；QEMU 只提供 MMIO、内存
   callback 和设备包装，不应再复制一套页表逻辑。
5. Event VQ 是异步 fault 通道，不是普通请求的错误返回替代品；请求本身
   仍必须写 response status。

## 9. 相关源码

- `virtio/virtio_iommu.h`：virtio-iommu 配置、请求和 fault 结构；
- `virtio/virtio_iommu.c`：virtqueue、协议校验和 refmodel 调用；
- `virtio/utils.c`：virtio 设备 DMA/MSI 的转换入口；
- `third_party/iommu_refmodel/include/io_virtio_iommu.h`：refmodel 适配 API；
- `third_party/iommu_refmodel/src/io_virtio_iommu.c`：domain、页表、S1/S2
  和 relay 实现。
