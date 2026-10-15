.. _RISC-V-System-emulator:

RISC-V System emulator
======================

QEMU can emulate both 32-bit and 64-bit RISC-V CPUs. Use the
``qemu-system-riscv64`` executable to simulate a 64-bit RISC-V machine,
``qemu-system-riscv32`` executable to simulate a 32-bit RISC-V machine.

QEMU has generally good support for RISC-V guests. It has support for
several different machines. The reason we support so many is that
RISC-V hardware is much more widely varying than x86 hardware. RISC-V
CPUs are generally built into "system-on-chip" (SoC) designs created by
many different companies with different devices, and these SoCs are
then built into machines which can vary still further even if they use
the same SoC.

For most boards the CPU type is fixed (matching what the hardware has),
so typically you don't need to specify the CPU type by hand, except for
special cases like the ``virt`` board.

Choosing a board model
----------------------

For QEMU's RISC-V system emulation, you must specify which board
model you want to use with the ``-M`` or ``--machine`` option;
there is no default.

Because RISC-V systems differ so much and in fundamental ways, typically
operating system or firmware images intended to run on one machine
will not run at all on any other. This is often surprising for new
users who are used to the x86 world where every system looks like a
standard PC. (Once the kernel has booted, most user space software
cares much less about the detail of the hardware.)

If you already have a system image or a kernel that works on hardware
and you want to boot with QEMU, check whether QEMU lists that machine
in its ``-machine help`` output. If it is listed, then you can probably
use that board model. If it is not listed, then unfortunately your image
will almost certainly not boot on QEMU. (You might be able to
extract the file system and use that with a different kernel which
boots on a system that QEMU does emulate.)

If you don't care about reproducing the idiosyncrasies of a particular
bit of hardware, such as small amount of RAM, no PCI or other hard
disk, etc., and just want to run Linux, the best option is to use the
``virt`` board. This is a platform which doesn't correspond to any
real hardware and is designed for use in virtual machines. You'll
need to compile Linux with a suitable configuration for running on
the ``virt`` board. ``virt`` supports PCI, virtio, recent CPUs and
large amounts of RAM. It also supports 64-bit CPUs.

Board-specific documentation
----------------------------

Unfortunately many of the RISC-V boards QEMU supports are currently
undocumented; you can get a complete list by running
``qemu-system-riscv64 --machine help``, or
``qemu-system-riscv32 --machine help``.

..
   This table of contents should be kept sorted alphabetically
   by the title text of each file, which isn't the same ordering
   as an alphabetical sort by filename.

.. toctree::
   :maxdepth: 1

   riscv/microchip-icicle-kit
   riscv/shakti-c
   riscv/sifive_u
   riscv/virt

RISC-V CPU firmware
-------------------

When using the ``sifive_u`` or ``virt`` machine there are three different
firmware boot options:

* ``-bios default``

This is the default behaviour if no ``-bios`` option is included. This option
will load the default OpenSBI firmware automatically. The firmware is included
with the QEMU release and no user interaction is required. All a user needs to
do is specify the kernel they want to boot with the ``-kernel`` option

* ``-bios none``

QEMU will not automatically load any firmware. It is up to the user to load all
the images they need.

* ``-bios <file>``

Tells QEMU to load the specified file as the firmware.

Experimental BitMTE extension
----------------------------

On the Matrix proposal-12 branch, ``-cpu rv64,x-matrix=on,x-bitmte=on``
enables native ``i8/i4 * fp2pack4/i1pack8 -> i32`` operations in RV64
system and Linux-user emulation. ``x-bitmte`` defaults to off and requires
``rlen=512,mlen=65536,melen=32``. Device-tree capabilities identify all four
native combinations and the ``xbitmte`` extension.

Table 0 uses type 13 for ``fp2pack4`` and type 15 for ``i1pack8``.
Ternary codes 00, 01 and 11 represent 0, +1 and -1; consuming code 10
has undefined results. Binary bits represent 0 or +1. Elements occupy
increasing bit positions, with the earlier signed-i4 A element in the low
nibble. Native loads preserve these representations.

Normal ``mlb`` loads 128 raw 64-byte B rows, independently of ``mtilek``.
Signed-i4 ``mla`` with M=128 and K=128 also loads 128 raw 64-byte rows,
independently of N. Base addresses and byte strides use the existing
Matrix memory-access path. Other packed typed loads, stores and transposed
loads raise an illegal-instruction exception.

``mmacc`` bits [25:23] select the resident B K window. For M=N=128,
i8 uses K=64 with four ternary or eight binary slices; i4 uses K=128 with
two ternary or four binary slices. The A and B registers must differ.
Unsupported native combinations, table selections, shapes and selectors
raise an illegal-instruction exception before computation. Configuration
writes continue to store raw XLEN values; native consumers ignore bits
above bit 7. Ordinary ``mmacc`` requires selector zero. Native arithmetic
wraps modulo 2^32, or saturates after each K element when ``msaten=1``;
it does not update the sticky saturation flag.

This is a synchronous functional model. Existing proposal-12 limitations
remain visible: synchronization instructions have no token implementation,
whole transfers use the base helper's extent, and ordinary floating-point
operations retain the base K interpretation. These limitations prevent
claiming the complete software-validation gate has passed.
