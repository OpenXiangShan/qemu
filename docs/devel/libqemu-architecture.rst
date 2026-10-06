libqemu 实现边界与代码导览
============================

本文说明把 system QEMU 编译成 ``libqemu-system-riscv64.so`` 时，为什么需要
修改 ``libqemu/`` 以外的 QEMU 源文件。本文关注实现边界和代码路径；构建、
运行参数和 Kunminghu 组装示例见 :doc:`libqemu` 和
``libqemu/examples/README.kunminghu.md``。

宿主模块封装见 :doc:`libqemu-compose`。该层的配置、设备 builder、DTB 和 boot loader
分别位于 ``libqemu/compose*.c``，通过现有 core ABI 工作，不新增 QEMU 线程、
core 导出项或原 machine 初始化分支。

库化的目标不是重新实现 CPU、内存、设备和 Linux 启动，而是把原来的

::

   system QEMU executable
       -> qemu_init()
       -> qemu_main_loop()
       -> process exit

流程改成：

::

   host process
       -> dlopen(libqemu-system-riscv64.so)
       -> libqemu_get_api()
       -> api->start() / api->init()
       -> QEMU worker thread
       -> qemu_init() and qemu_main_loop()
       -> api->wait()
       -> return to host process

因此，库化必须处理 QEMU 的线程、主循环、退出、信号、RCU、标准输入输出和
构建入口。这些内容分散在 QEMU 原有子系统中，不能只放在 ``libqemu/`` 目录。

配置开关和构建边界
------------------

配置入口在 ``configure`` 的 ``--enable-libqemu`` 分支，以及
``meson_options.txt`` 的 ``libqemu`` 选项。主要实现位于
``meson.build``：

* 只允许 Linux、``riscv64-softmmu`` 和 TCG；
* 自动启用 PIC，并拒绝 plugins、fuzzing、静态链接、modules、GUI、tools
  和 guest-agent 等与当前库生命周期不兼容的功能；
* 设置 ``CONFIG_LIBQEMU``，使 QEMU 通用代码可以选择库模式分支；
* 将原 system objects 链接为 ``libqemu-system-riscv64.so``，而不是生成
  ``qemu-system-riscv64`` 可执行文件；
* 使用 ``libqemu/libqemu.version`` 限制共享库的导出符号。

默认配置仍然是 ``--disable-libqemu``。因此普通构建继续生成原来的 QEMU
可执行文件，下面的库模式分支均不会影响默认行为。

``hw/misc/meson.build`` 和 ``my-virtio-lib/Makefile`` 还负责让 my-virtio
静态归档使用 ``-fPIC``。这是链接共享库的必要条件，不是设备行为变化。

公共 ABI 与内部桥接
-------------------

公共头文件是 ``libqemu/libqemu.h``。它只暴露不透明句柄和函数表：

::

   LibQemuObject
   LibQemuMemory
   LibQemuAddressSpace
   LibQemuIRQ
   LibQemuTimer

``LibQemuExports`` 以 ``abi_version`` 和 ``struct_size`` 开头。完整启动的
前缀包含 ``start()``, ``request_shutdown()``, ``wait()`` 和 ``error_free()``；
尾部追加 ``init()``, ``finish_qemu_init()``, ``pause()``, ``resume()``,
``system_reset()`` 和 ``core``。这样旧宿主只使用前缀时仍可工作，新宿主可以
先检查表大小再使用扩展。

``libqemu/libqemu.c`` 是公共 API 的实现。它创建名为 ``libqemu-main`` 的
可 join worker，并在 worker 中执行：

::

   qemu_lib_rcu_init();
   qemu_init(runtime.argc, runtime.argv);
   runtime.status = qemu_main_loop();
   qemu_cleanup(runtime.status);
   qemu_run_exit_notifiers();

宿主线程不能直接持有 QEMU 的 BQL 来调用内部函数。``libqemu_call()`` 将
QOM、内存、IRQ 和设备操作放入请求队列，使用 bottom half 唤醒 worker，在
worker 中执行并把结果传回宿主线程。该函数还检查：

* 当前是否处于 ``ASSEMBLY`` 或 ``RUNNING`` 阶段；
* 是否从 QEMU 回调中同步重入；
* QEMU 停止时是否需要失败队列中的请求；
* 回调中只能使用标记为 callback-safe 的操作。

``libqemu/internal.h`` 只供 QEMU 内部使用，连接 ``libqemu.c``、
``core.c``、``system/vl.c`` 和清理路径。它不属于宿主 ABI。

分阶段初始化：``system/vl.c``
-------------------------------

正常 QEMU 在 ``system/vl.c:qemu_init()`` 中完成 machine、board、CLI 设备、
machine creation done、reset 和 autostart，然后进入 main loop。

组装模式需要在设备创建后暂停，所以原来的后半段被提取到：

::

   static void qemu_finish_machine_init(Error **errp)

库模式的 ``qemu_init()`` 在 ``libqemu_deferred_init()`` 为真时执行：

::

   qemu_init_board();
   qemu_create_cli_devices();
   return;

此时 QEMU worker 仍然运行自己的事件循环，但状态是 ``LIBQEMU_ASSEMBLY``。
宿主可以用 ``core`` 创建和连接额外设备。``finish_qemu_init()`` 再调用
``qemu_lib_finish_init()``，完成：

* ``qemu_machine_creation_done()``；
* drive orphan 检查；
* snapshot、replay、migration 的后置处理；
* ROM/reset 处理；
* 根据 ``autostart`` 调用 ``qmp_cont()``；
* display、accelerator post setup 和 resume mux。

``qmp_x_exit_preconfig()`` 仍保留原有完整路径。也就是说，拆出
``qemu_finish_machine_init()`` 是为了复用原逻辑，而不是重写 QEMU 启动顺序。

主循环隔离：``util/main-loop.c``
--------------------------------

普通 QEMU 把 AIO、IO handler 和 GLib poll source 放入
``g_main_context_default()``。嵌入场景中，宿主进程可能已经拥有自己的 GLib
主循环；如果 QEMU 继续使用 default context，宿主和 QEMU 会互相 dispatch
对方的 source。

``util/main-loop.c`` 增加 ``qemu_main_context()``：

::

   CONFIG_LIBQEMU  -> g_main_context_get_thread_default()
   normal QEMU    -> g_main_context_default()

``libqemu/libqemu.c`` 的 worker 先创建并 push 一个独立的 thread-default
context。随后 ``qemu_init_main_loop()``、GLib poll、AIO source 和 IO handler
都绑定到这个 context。这样宿主可以在自己的线程运行 GLib loop，而 QEMU
事件只在 QEMU worker 中处理。

标准输入输出和 chardev
-----------------------

``chardev/char.c`` 在库模式下，如果调用者没有显式指定 context，就把
chardev read handler 绑定到当前 worker 的 thread-default context。这与
``util/main-loop.c`` 的 context 隔离配套，覆盖 UART、stdio 和 socket 等
chardev 的 GLib source。

``chardev/char-stdio.c`` 还处理宿主 fd 的所有权。普通 QEMU 直接把 fd 0 和
fd 1 交给 stdio chardev；库模式改为：

::

   fd_in = dup(STDIN_FILENO);
   fd_out = dup(STDOUT_FILENO);
   qemu_chr_open_fd(chr, fd_in, fd_out);

QEMU cleanup 只会关闭副本，宿主的 stdin/stdout 仍然有效。终端 echo、
canonical mode 和 SIGCONT 恢复逻辑仍由 QEMU 管理。这个修改是嵌入式退出后
宿主继续运行的必要条件。

退出和 exit notifier：``system/runstate.c``
----------------------------------------------

普通 QEMU 在 ``qemu_init_subsystems()`` 中注册：

::

   atexit(qemu_run_exit_notifiers);

库模式不能这样做，因为 QEMU 停止不等于宿主进程停止。否则 exit notifier
会延迟到宿主进程退出，或者和库自己的清理顺序重复执行。

因此 ``system/runstate.c`` 在 ``CONFIG_LIBQEMU`` 下导出
``qemu_run_exit_notifiers()``，不再注册 ``atexit``。worker 的退出顺序为：

::

   qemu_main_loop() 返回
       -> qemu_cleanup()
       -> libqemu_core_cleanup()
       -> qemu_run_exit_notifiers()
       -> 释放/停止 QEMU worker 状态
       -> api->wait() 返回宿主

exit notifier 在 BQL 下运行。my-virtio-blk 使用它关闭 backend 和 bottom half，
从而保证宿主检查磁盘 fd 时不会看到 QEMU 遗留的打开句柄。

信号和进程所有权：``os-posix.c``
--------------------------------

普通 QEMU 会安装 SIGINT、SIGHUP 和 SIGTERM handler，并将这些信号转换为
QEMU shutdown。库模式运行在宿主进程内部，不能未经宿主同意覆盖这些进程级
handler，因此 ``os-posix.c`` 在 ``CONFIG_LIBQEMU`` 下跳过这三类 handler 的
安装。

宿主应使用：

::

   api->request_shutdown(&error);

来请求 QEMU 停止。QEMU 仍保留 main loop 自身需要的 AIO、signalfd 等内部
机制；改变的是不再接管宿主的常规终止信号语义。

RCU 初始化：``util/rcu.c`` 和 ``include/qemu/rcu.h``
-------------------------------------------------------

普通 QEMU 通过 ELF constructor 初始化 RCU。libqemu 是由宿主用 ``dlopen``
加载的，constructor 可能在宿主线程运行，而 QEMU 实际在 worker 线程使用
RCU。线程不一致会导致 reader 注册、RCU grace period 和退出注销不匹配。

库模式将 constructor 入口替换成公开给 QEMU 内部的：

::

   void qemu_lib_rcu_init(void);

worker 在调用 ``qemu_init()`` 前显式调用它。普通构建仍然使用原 constructor。

``include/system/system.h`` 和退出/初始化桥接
------------------------------------------------

``include/system/system.h`` 在库模式下声明：

::

   void qemu_run_exit_notifiers(void);
   void qemu_lib_finish_init(Error **errp);

前者连接 ``libqemu.c`` 和 ``system/runstate.c``，后者连接
``libqemu.c`` 和 ``system/vl.c``。这些是 QEMU 内部桥接，不是给宿主直接调用
的 ABI；宿主只能通过 ``LibQemuExports`` 调用对应的包装函数。

组装 API 的边界：``libqemu/core.c``
-------------------------------------

``core.c`` 不是完整 machine 的替代实现，而是 QEMU 内部 QOM 能力的受控导出：

* QOM object 创建、引用、路径查找和 property；
* device realize、bus 查找和 SysBus MMIO；
* RAM、ROM、container、alias、MMIO callback 和 address space；
* GPIO、SysBus IRQ、宿主 IRQ callback；
* QEMU virtual clock、timer 和 CPU reset 注册；
* 组装阶段的 boot blob/ROM 加载。

所有 topology 操作要求 ``LIBQEMU_ASSEMBLY``。运行阶段只开放明确标为
runtime-safe 的内存访问、IRQ 和 timer 操作。这样可以防止 guest 已经运行后
任意修改 QOM 拓扑，也避免宿主线程绕过 QEMU worker 直接碰内部对象。

Kunminghu 设备文件不是完整 libqemu 框架
----------------------------------------

下面这些文件是 Kunminghu ``-M none`` 组装示例的适配，而不是完整启动型
libqemu 的必需修改：

* ``hw/intc/riscv_aclint.c`` 和 ``include/hw/intc/riscv_aclint.h``：将
  ACLINT timer 的 timer callback、``rdtime`` 接入变成 QOM realize 属性；
* ``hw/intc/riscv_aplic.c`` 和 ``include/hw/intc/riscv_aplic.h``：增加
  ``delegate-parent``，让 APLIC 父子级联可通过公共 property 组装；
* ``hw/misc/my_virtio_blk.c``：将原来的文件级 ``base/len`` 和板级 helper
  参数变成每个实例的 QOM properties；
* ``hw/pci-host/designware.c`` 和 ``include/hw/pci-host/designware.h``：增加
  ``dma-to-system-memory``，让组装示例复现原 Kunminghu 无 IOMMU 时的 PCI DMA
  行为；
* ``target/riscv/cpu.c``：增加只读 ``isa-string`` property，用实际 CPU ISA
  更新组装示例的 DTB。

这些修改保留原有默认 property 和 machine helper 行为，因此普通
``xiangshan-kunminghu`` machine 仍按原方式创建设备。

两种模式的最终边界
------------------

完整启动模式：

::

   api->start(argc, argv, &error)
       -> qemu_init()
       -> -M 选择 MachineClass
       -> machine init 创建 SoC/设备
       -> qemu_finish_machine_init()
       -> qemu_main_loop()
       -> api->wait()

组装模式：

::

   api->init(argc, argv, &error)       /* 通常是 -M none */
       -> qemu_init_board()
       -> qemu_create_cli_devices()
       -> LIBQEMU_ASSEMBLY
       -> core->object_new()/memory_add()/gpio_connect()/device_realize()
       -> api->finish_qemu_init(&error)
       -> reset/autostart
       -> qemu_main_loop()
       -> api->wait()

默认普通 QEMU 不启用 ``CONFIG_LIBQEMU``，继续使用默认 GLib context、信号
处理、RCU constructor、``atexit`` exit notifier 和原始 system executable
入口。库模式只是增加一个嵌入边界，并复用原 QEMU machine、设备、固件加载和
guest execution 实现。
