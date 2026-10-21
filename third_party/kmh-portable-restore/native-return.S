    .option norvc
    .section .text
    .globl kmh_native_return

    .equ MTIMECMP,    0x19004000
    .equ MTIME,       0x1900bff8
    .equ MAGIC_OFF,   0xECDB0
    .equ PC_OFF,      0xECDB8
    .equ MODE_OFF,    0xECDC0
    .equ MTIME_OFF,   0xECDC8
    .equ MTIMECMP_OFF,0xECDD0
    .equ GPR_OFF,     0xEDDE0
    .equ FPR_OFF,     0xEDEE8
    .equ CSR_OFF,     0xEDFF0
    .equ VREG_OFF,    0xFDFF8

    .macro LOAD_CSR csr
        li t1, CSR_OFF + (\csr * 8)
        add t1, t0, t1
        ld t1, 0(t1)
        csrw \csr, t1
    .endm

kmh_native_return:
    /* a0 is the physical CPU record selected by the firmware itself. */
    mv t0, a0

    LOAD_CSR 0x302
    LOAD_CSR 0x303
    LOAD_CSR 0x305
    LOAD_CSR 0x306
    LOAD_CSR 0x30a
    LOAD_CSR 0x30c
    LOAD_CSR 0x340
    LOAD_CSR 0x350
    LOAD_CSR 0x150
    LOAD_CSR 0x250
    LOAD_CSR 0x341
    LOAD_CSR 0x342
    LOAD_CSR 0x343
    LOAD_CSR 0x105
    LOAD_CSR 0x106
    LOAD_CSR 0x10a
    LOAD_CSR 0x140
    LOAD_CSR 0x141
    LOAD_CSR 0x142
    LOAD_CSR 0x143
    LOAD_CSR 0x180
    LOAD_CSR 0x14d
    LOAD_CSR 0x600
    LOAD_CSR 0x602
    LOAD_CSR 0x603
    LOAD_CSR 0x606
    LOAD_CSR 0x607
    LOAD_CSR 0x643
    LOAD_CSR 0x645
    LOAD_CSR 0x64a
    LOAD_CSR 0x60a
    LOAD_CSR 0x680
    LOAD_CSR 0x200
    LOAD_CSR 0x205
    LOAD_CSR 0x240
    LOAD_CSR 0x241
    LOAD_CSR 0x242
    LOAD_CSR 0x243
    LOAD_CSR 0x280
    # RV64 exposes pmpcfg0/pmpcfg2; pmpcfg1/pmpcfg3 are RV32-only.
    # Program addresses before cfg so locked entries can be reconstructed.
    LOAD_CSR 0x3b0
    LOAD_CSR 0x3b1
    LOAD_CSR 0x3b2
    LOAD_CSR 0x3b3
    LOAD_CSR 0x3b4
    LOAD_CSR 0x3b5
    LOAD_CSR 0x3b6
    LOAD_CSR 0x3b7
    LOAD_CSR 0x3b8
    LOAD_CSR 0x3b9
    LOAD_CSR 0x3ba
    LOAD_CSR 0x3bb
    LOAD_CSR 0x3bc
    LOAD_CSR 0x3bd
    LOAD_CSR 0x3be
    LOAD_CSR 0x3bf
    LOAD_CSR 0x3a0
    LOAD_CSR 0x3a2

    li t1, (3 << 13) | (3 << 9)
    csrs mstatus, t1
    LOAD_CSR 0x003

    li t1, CSR_OFF + (0xc21 * 8)
    add t1, t0, t1
    ld t1, 0(t1)
    li t2, CSR_OFF + (0xc20 * 8)
    add t2, t0, t2
    ld t2, 0(t2)
    vsetvl zero, t2, t1
    li t1, VREG_OFF
    add t1, t0, t1
    vl1re64.v v0, (t1)
    addi t1, t1, 16
    vl1re64.v v1, (t1)
    addi t1, t1, 16
    vl1re64.v v2, (t1)
    addi t1, t1, 16
    vl1re64.v v3, (t1)
    addi t1, t1, 16
    vl1re64.v v4, (t1)
    addi t1, t1, 16
    vl1re64.v v5, (t1)
    addi t1, t1, 16
    vl1re64.v v6, (t1)
    addi t1, t1, 16
    vl1re64.v v7, (t1)
    addi t1, t1, 16
    vl1re64.v v8, (t1)
    addi t1, t1, 16
    vl1re64.v v9, (t1)
    addi t1, t1, 16
    vl1re64.v v10, (t1)
    addi t1, t1, 16
    vl1re64.v v11, (t1)
    addi t1, t1, 16
    vl1re64.v v12, (t1)
    addi t1, t1, 16
    vl1re64.v v13, (t1)
    addi t1, t1, 16
    vl1re64.v v14, (t1)
    addi t1, t1, 16
    vl1re64.v v15, (t1)
    addi t1, t1, 16
    vl1re64.v v16, (t1)
    addi t1, t1, 16
    vl1re64.v v17, (t1)
    addi t1, t1, 16
    vl1re64.v v18, (t1)
    addi t1, t1, 16
    vl1re64.v v19, (t1)
    addi t1, t1, 16
    vl1re64.v v20, (t1)
    addi t1, t1, 16
    vl1re64.v v21, (t1)
    addi t1, t1, 16
    vl1re64.v v22, (t1)
    addi t1, t1, 16
    vl1re64.v v23, (t1)
    addi t1, t1, 16
    vl1re64.v v24, (t1)
    addi t1, t1, 16
    vl1re64.v v25, (t1)
    addi t1, t1, 16
    vl1re64.v v26, (t1)
    addi t1, t1, 16
    vl1re64.v v27, (t1)
    addi t1, t1, 16
    vl1re64.v v28, (t1)
    addi t1, t1, 16
    vl1re64.v v29, (t1)
    addi t1, t1, 16
    vl1re64.v v30, (t1)
    addi t1, t1, 16
    vl1re64.v v31, (t1)

    li t1, FPR_OFF
    add t1, t0, t1
    fld f0, 0(t1)
    fld f1, 8(t1)
    fld f2, 16(t1)
    fld f3, 24(t1)
    fld f4, 32(t1)
    fld f5, 40(t1)
    fld f6, 48(t1)
    fld f7, 56(t1)
    fld f8, 64(t1)
    fld f9, 72(t1)
    fld f10, 80(t1)
    fld f11, 88(t1)
    fld f12, 96(t1)
    fld f13, 104(t1)
    fld f14, 112(t1)
    fld f15, 120(t1)
    fld f16, 128(t1)
    fld f17, 136(t1)
    fld f18, 144(t1)
    fld f19, 152(t1)
    fld f20, 160(t1)
    fld f21, 168(t1)
    fld f22, 176(t1)
    fld f23, 184(t1)
    fld f24, 192(t1)
    fld f25, 200(t1)
    fld f26, 208(t1)
    fld f27, 216(t1)
    fld f28, 224(t1)
    fld f29, 232(t1)
    fld f30, 240(t1)
    fld f31, 248(t1)

    # Whole-register vector loads reset vstart; restore it last.
    LOAD_CSR 0x00f
    LOAD_CSR 0x008
    LOAD_CSR 0x344
    LOAD_CSR 0x304
    li t1, CSR_OFF + (0x300 * 8)
    add t1, t0, t1
    ld t1, 0(t1)
    csrw mstatus, t1
    sfence.vma
    fence.i

    # Enter the saved privilege mode through mret.  The checkpoint is
    # normally taken in S/U mode, so mepc must be restored for every mode,
    # not only when the saved mode happens to be M.
    li t1, MODE_OFF
    add t1, t0, t1
    ld t2, 0(t1)
    li t1, PC_OFF
    add t1, t0, t1
    ld t5, 0(t1)
    csrw mepc, t5
    li t1, 3
    slli t1, t1, 11
    slli t3, t2, 11
    li t4, (3 << 11)
    csrc mstatus, t4
    csrs mstatus, t3

.Lload_gprs:
    li t6, GPR_OFF
    add t6, t0, t6
    ld x1, 8(t6)
    ld x3, 24(t6)
    ld x4, 32(t6)
    ld x5, 40(t6)
    ld x6, 48(t6)
    ld x7, 56(t6)
    ld x8, 64(t6)
    ld x9, 72(t6)
    ld x10, 80(t6)
    ld x11, 88(t6)
    ld x12, 96(t6)
    ld x13, 104(t6)
    ld x14, 112(t6)
    ld x15, 120(t6)
    ld x16, 128(t6)
    ld x17, 136(t6)
    ld x18, 144(t6)
    ld x19, 152(t6)
    ld x20, 160(t6)
    ld x21, 168(t6)
    ld x22, 176(t6)
    ld x23, 184(t6)
    ld x24, 192(t6)
    ld x25, 200(t6)
    ld x26, 208(t6)
    ld x27, 216(t6)
    ld x28, 224(t6)
    ld x29, 232(t6)
    ld x30, 240(t6)
    ld x2, 16(t6)
    ld x31, 248(t6)
    mret

.Lbad:
    li a0, 0xbad
    .insn r 0x6b, 0, 0, x0, x0, x0
    wfi
    j .Lbad
