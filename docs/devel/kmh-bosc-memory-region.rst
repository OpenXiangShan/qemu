KmhBoscSoc 多 die 地址空间与 MemoryRegion 映射
================================================

本文说明 ``kmh-bosc-soc`` 中使用的 QEMU ``MemoryRegion`` 映射方式，重点解释
``memory_region_init_alias``、``memory_region_add_subregion`` 和
``memory_region_add_subregion_overlap``，以及 alias、priority 和 overlay
之间的关系。

基本概念
--------

``MemoryRegion`` 是 QEMU 对一段可寻址区域的抽象，可以代表 RAM、ROM、SRAM、
MMIO 设备、容器，或者已有区域的 alias。容器只组织子区域，alias 也不拥有
新的 RAM；真正的数据存储或设备行为由它引用的原始区域提供。

``system_memory`` 是机器的全局系统地址空间。设备和 DDR 首先被挂到这个
地址空间上；每个 die 的 ``die_mem[die]`` 则是在此基础上建立的本地地址
空间视图。

``subregion`` 描述的是 MemoryRegion 树中的父子关系。把 child 挂到 parent
的某个偏移处：

.. code-block:: c

   memory_region_add_subregion(parent, parent_offset, child);

父地址 ``parent_offset + x`` 会访问子区域地址 ``x``。child 可以是 RAM、
MMIO、容器，也可以是 alias。

``alias`` 是已有 ``MemoryRegion`` 的另一个地址视图，不会复制 RAM，也不会
创建第二个设备实例。访问 alias 时，地址会转换为：

.. code-block:: text

   alias[offset_in_alias]
       -> original[alias_offset + offset_in_alias]

因此，多个地址可以访问同一份数据或同一个 MMIO 设备。

alias 与 subregion 的区别
-------------------------

两者解决的是不同问题，不能简单理解为二选一：

* ``memory_region_init_alias`` 创建 alias 类型的 ``MemoryRegion``，回答
  “访问这个区域时，实际转发到哪个原始区域的哪一段”；
* ``memory_region_add_subregion`` 把一个已经存在的 ``MemoryRegion`` 挂到
  父容器的某个地址，回答“从哪个父地址可以找到这个区域”。

因此常见的完整路径是：

.. code-block:: text

   parent 地址
       -> child（由 add_subregion 挂载）
       -> original（如果 child 是 alias）

例如，直接挂载一段 RAM：

.. code-block:: c

   memory_region_init_ram(&ram, NULL, "ram", 0x1000, &error_fatal);
   memory_region_add_subregion(system_memory, 0x80000000, &ram);

访问 ``0x80000000 + x`` 直接到 ``ram[x]``，这里只有挂载关系，没有 alias。

如果想让另一段地址访问同一块 RAM，则先创建 alias，再挂载 alias：

.. code-block:: c

   memory_region_init_alias(&ram_alias, NULL, "ram-alias",
                            &ram, 0, 0x1000);
   memory_region_add_subregion(system_memory, 0x90000000, &ram_alias);

访问路径是：

.. code-block:: text

   system_memory[0x90000000 + x]
       -> ram_alias[x]
       -> ram[x]

``memory_region_add_subregion`` 不会复制 child 的内容，也不会把 child 转换
成 alias；它只是把 child 放进 parent 的地址树。反过来，
``memory_region_init_alias`` 也不会自动把 alias 放进任何父地址空间；不执行
add-subregion，现有 AddressSpace 就无法通过该地址找到 alias。

memory_region_init_alias
------------------------

接口定义为：

.. code-block:: c

   void memory_region_init_alias(MemoryRegion *mr,
                                  Object *owner,
                                  const char *name,
                                  MemoryRegion *orig,
                                  hwaddr offset,
                                  uint64_t size);

参数含义：

``mr``
   要初始化的新 alias 区域。

``owner``
   QOM 对象所有者，用于对象层级和生命周期管理。当前机器代码通常传入
   ``OBJECT(s)``，DDR alias 可以使用 ``NULL``。

``name``
   区域名称，主要用于调试、QOM 层级和地址空间查看。

``orig``
   被引用的原始 ``MemoryRegion``，例如 ``system_memory`` 或机器 RAM。

``offset``
   alias 对应的原始区域起始偏移。

``size``
   alias 窗口大小。

例如：

.. code-block:: c

   memory_region_init_alias(&alias, NULL, "ram-window",
                            machine_ram, 0x100000000, 0x40000000);

建立的关系是：

.. code-block:: text

   alias[0x00000000] -> machine_ram[0x100000000]
   alias[0x00001000] -> machine_ram[0x100001000]

``memory_region_init_alias`` 只定义 alias 本身；要让 CPU 或其他 master
能够通过某个父地址空间访问它，还必须把 alias 挂到父区域上。

memory_region_add_subregion
---------------------------

接口为：

.. code-block:: c

   memory_region_add_subregion(parent, offset, child);

它以默认 priority ``0`` 将 child 挂到 parent 的 ``offset`` 处，适用于
地址范围不重叠，或者不需要用优先级解决重叠的普通映射。当前代码把真实
SRAM、BootROM 等设备挂到全局 ``system_memory`` 时使用这个 API：

.. code-block:: c

   memory_region_add_subregion(system_memory,
                               global_base,
                               &s->sram0[die]);

其含义是：

.. code-block:: text

   system_memory[global_base + x] -> sram0[die][x]

memory_region_add_subregion_overlap
-----------------------------------

接口为：

.. code-block:: c

   memory_region_add_subregion_overlap(parent, offset, child, priority);

它允许子区域在父区域中重叠，并为子区域指定 priority。重叠范围内，priority
较高的子区域优先可见；较低 priority 的区域只在未被覆盖的位置生效。

当前实现使用：

.. code-block:: text

   本地 alias       priority =  1
   全局 fallback    priority = -1

因此本地 alias 会覆盖同一 ``die_mem[die]`` 地址范围内的全局 fallback。
priority 只影响同一个父容器中的子区域选择，不会改变 alias 指向的原始
MemoryRegion，也不会把一个 die 的设备物理搬到另一个 die。

overlay 的含义
--------------

overlay 不是一个单独的 MemoryRegion API，而是地址区域重叠并按 priority
选择可见区域时产生的拓扑效果：

.. code-block:: text

   同一个 parent
   ├── child A: [base, base + size), priority -1
   └── child B: [base, base + size), priority  1

在重叠范围内，child B overlay（覆盖）child A；在 B 没有覆盖的范围内，
A 仍然可见。这里的“覆盖”只表示地址解析时优先选择谁，不表示删除 A、
复制 B 的数据，或修改 ``system_memory`` 中的设备布局。

当前 kmh-bosc-soc 的 die 地址空间
----------------------------------

相关代码位于 ``hw/riscv/kmh_bosc_soc.c`` 的
``kmh_bosc_init_die_address_spaces()``。

每个 die 首先创建一个容器：

.. code-block:: c

   memory_region_init(&s->die_mem[die], OBJECT(s), name, UINT64_MAX);

然后加入 priority 为 ``-1`` 的全局 fallback alias：

.. code-block:: c

   memory_region_init_alias(&s->die_system_alias[die], OBJECT(s), name,
                            system_memory, 0, UINT64_MAX);
   memory_region_add_subregion_overlap(&s->die_mem[die], 0,
                                       &s->die_system_alias[die], -1);

关系是：

.. code-block:: text

   die_mem[die][x] -> system_memory[x]       priority -1

这是 die 地址空间中的全局回退，不是覆盖 ``system_memory``。如果没有更高
priority 的本地窗口命中，访问就转发到相同的全局地址。

随后，对每个 die 的本地外设窗口建立 priority 为 ``1`` 的 alias：

.. code-block:: c

   memory_region_init_alias(alias, OBJECT(s), name, system_memory,
                            kmh_bosc_die_addr(die, entry->base),
                            entry->size);
   memory_region_add_subregion_overlap(&s->die_mem[die], entry->base,
                                       alias, 1);

关系变为：

.. code-block:: text

   die_mem[die][entry->base + x]
       -> system_memory[kmh_bosc_die_addr(die, entry->base) + x]
       priority 1

由于本地窗口 priority 高于 fallback，同一个本地地址范围内会优先访问
当前 die 编码后的全局设备地址。

地址空间示意
~~~~~~~~~~~~~

.. code-block:: text

   die_mem[die]
   ├── priority 1
   │   本地窗口
   │   local entry->base
   │       -> global kmh_bosc_die_addr(die, entry->base)
   │
   └── priority -1
       全局 fallback
       local x
           -> global x

全局 ``system_memory`` 保存真实设备和 DDR 的映射：

.. code-block:: text

   system_memory
   ├── die0 的设备：带硬件 die-ID 的全局地址
   ├── die1 的设备：带硬件 die-ID 的全局地址
   ├── die2 的设备：带硬件 die-ID 的全局地址
   ├── die3 的设备：带硬件 die-ID 的全局地址
   └── 各 die 的 DDR alias

``die_mem[die]`` 不是第二份设备树，而是针对某个 die 的访问视图。

die1 的具体例子
----------------

代码中的 die 数组下标从 ``0`` 开始，而硬件 die-ID 为 ``die + 1``：

.. code-block:: text

   die index       = 1
   hardware die-ID = 2
   KMH_BOSC_DIE_SHIFT = 44

假设 die1 的 SRAM0 本地偏移为 ``0x01100000``，则：

.. code-block:: text

   kmh_bosc_die_addr(1, 0x01100000)
       = (2 << 44) | 0x01100000
       = 0x200001100000

访问 die1 本地地址：

.. code-block:: text

   die_mem[1][0x01100100]
       -> 本地 alias priority 1
       -> system_memory[0x200001100100]
       -> die1 SRAM0 + 0x100

访问同一 die1 设备的带 ID 全局地址：

.. code-block:: text

   die_mem[1][0x200001100100]
       -> 本地窗口不匹配
       -> fallback alias priority -1
       -> system_memory[0x200001100100]
       -> die1 SRAM0 + 0x100

访问 die0 的带 ID 地址：

.. code-block:: text

   die_mem[1][0x100001100100]
       -> 本地窗口不匹配
       -> fallback alias priority -1
       -> system_memory[0x100001100100]
       -> die0 SRAM0 + 0x100

IMSIC 也遵循同样的规则。例如 die1 IMSIC 的全局地址为
``0x20001c000000`` 时：

.. code-block:: text

   die_mem[1][0x1c000000]
       -> system_memory[0x20001c000000]

而从 die0 访问 die1，则直接使用带 die-ID 的全局地址：

.. code-block:: text

   system_memory[0x20001c000000]
       -> die1 IMSIC

DDR alias 的区别
----------------

DDR 映射也是 alias，但它不参与上述“本地窗口覆盖全局 fallback”的层次。
代码中的逻辑抽象为：

.. code-block:: c

   memory_region_init_alias(&s->ddr_alias[die], NULL, name,
                            machine->ram,
                            node * die_ram_size,
                            die_ram_size);
   memory_region_add_subregion(system_memory,
                               kmh_bosc_ddr_base(die),
                               &s->ddr_alias[die]);

它把 ``machine->ram`` 中属于某个 die 的连续切片映射到该 die 的全局 DDR
地址范围：

.. code-block:: text

   system_memory[kmh_bosc_ddr_base(die) + x]
       -> machine->ram[node * die_ram_size + x]

这里 alias 的作用是把一段机器 RAM 映射到另一个地址范围；
``memory_region_add_subregion`` 的作用是把这个 alias 挂到全局地址空间。
它和 ``die_mem`` 中的本地 alias/fallback overlay 是两个不同层次的映射。

常见误区
--------

* ``memory_region_init_alias`` 不会分配新的 RAM，也不会复制设备。
* ``memory_region_add_subregion_overlap`` 的 priority 只在同一个父区域内
  解决重叠，不能直接改变另一个地址空间的映射。
* ``die_system_alias`` 是 ``die_mem`` 中的 fallback，不是覆盖
  ``system_memory`` 的全局 overlay。
* 本地 alias 的 ``entry->base`` 是 die 内的本地地址；alias 的目标地址
  使用 ``kmh_bosc_die_addr()`` 编码后的全局地址。
* DDR alias 的目标是 ``machine->ram`` 的切片，而外设本地 alias 的目标
  通常是 ``system_memory`` 中已经挂载好的设备区域。
