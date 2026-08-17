# Source layout

`core/`
: 对外统一入口、backend selector、trace、host memory callback 和公共
  backend ops 定义。
  QEMU 调用 `io_system_q2io_read/write()` 后，在这里按配置选择 C-model
  或 RTL-model。`core/` 不做设备 MMIO decode，也不保存 C-model MMIO
  window table。

`cmodel/`
: 软件 C-model。所有纯软件模拟设备都归到这里，包括 APLIC、DWC DMAC、
  my-virtio，以及依赖 QEMU runtime 的兼容适配。

`cmodel/bus/`
: C-model 私有的 MMIO dispatcher。C-model 设备通过
  `io_cmodel_register_mmio_window()` 注册到这里；RTL-system 不使用这一路径。

`cmodel/devices/`
: C-model 自己实现的设备模型。

`cmodel/adapters/qemu/`
: C-model 对 QEMU 宿主环境的适配。这里可以使用 QEMU runtime，例如复用
  QEMU PCI/NVMe 设备；它仍然属于 C-model，不是独立模型后端。

`rtl_model/`
: picker + RTL DUT 路径。RTL-model 的 MMIO decode 属于 DUT 自身行为，
  QEMU 不按窗口拆设备。

`test_model/`
: 纯软件测试模型。这里的 `rtl_template` 只是验证 backend selector、
  Q2IO 和 IO2Q transaction plumbing 的 test double，不是 picker+RTL
  DUT 路径。

`manifest/`
: QEMU 和模型共享的地址/中断 manifest。
