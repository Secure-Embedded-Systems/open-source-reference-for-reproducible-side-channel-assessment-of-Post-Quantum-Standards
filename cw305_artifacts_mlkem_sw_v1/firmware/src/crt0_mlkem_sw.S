/* =========================================================================
 * crt0_mlkem_sw.S -- mlkem-sw-only startup with diagnostic trap handler.
 * Used ONLY by FW=mlkem_sw and FW=mlkem_sw_widetrig (per-target Makefile
 * rule). The shared integration/firmware/mlkem1024/crt0.S retains its
 * original `j _start` trap handler and is unchanged.
 *
 * The diagnostic trap handler captures mcause/mepc/mtval to MBOX[0..2] and
 * stamps 0xDEADBEEF to MBOX[3] before halting, so a trap inside mlkem-native
 * code (which we are still debugging) can be diagnosed instead of silently
 * reset-looping.
 *
 * Memory: 64KB SRAM at 0x10000000 .. 0x1000FFFF
 * Boot:   CPU fetches from 0x10000080 (BootAddr in soc_pkg)
 * Stack:  top of SRAM at 0x1000FFFC (grows downward)
 * MBOX base: 0x50000200 (MBOX[k] at 0x50000200 + 4*k).
 * ========================================================================= */

    /* ---- Trap vector at mtvec (0x10000000): diagnostic capture + halt ---- */
    .section .vectors, "ax"
    .globl _vectors
_vectors:
    csrr    t0, mcause
    csrr    t1, mepc
    csrr    t2, mtval
    li      t3, 0x50000200      /* MBOX base */
    sw      t0, 0(t3)           /* MBOX[0] = mcause */
    sw      t1, 4(t3)           /* MBOX[1] = mepc   */
    sw      t2, 8(t3)           /* MBOX[2] = mtval  */
    li      t4, 0xDEADBEEF
    sw      t4, 12(t3)          /* MBOX[3] = trap sentinel */
1:
    wfi
    j       1b

    /* ---- Entry point at BootAddr (0x10000080) ---- */
    .section .init, "ax"
    .globl _start
    .type  _start, @function

_start:
    /* Phase 1: _start entered.  Phase markers in MBOX[3] tell the host
     * which crt0 step we last completed.  Marker = 0xCAFE000N for step N.
     * a0 = MBOX base, kept live across the whole _start. */
    li      a0, 0x50000200
    li      a1, 0xCAFE0001
    sw      a1, 12(a0)

    /* ---- Disable interrupts ---- */
    csrci   mstatus, 0x8       /* MIE = 0 */

    /* ---- Enable mcycle / minstret. CV32E40X resets mcountinhibit with bit 0
     * (CY) and bit 2 (IR) set, so csrr mcycle reads 0 until we clear them.
     * One write to mcountinhibit gives us live per-phase cycle counts. */
    csrwi   0x320, 0           /* mcountinhibit = 0 -> all counters running */

    /* Phase 2: explicit mtvec set so traps land in our _vectors handler. */
    la      a2, _vectors
    csrw    mtvec, a2
    li      a1, 0xCAFE0002
    sw      a1, 12(a0)

    /* ---- Set stack pointer to top of 64KB SRAM ---- */
    lui     sp, %hi(0x1000FFFC)
    addi    sp, sp, %lo(0x1000FFFC)
    /* Align to 16 bytes (RISC-V ABI) */
    andi    sp, sp, -16
    li      a1, 0xCAFE0003
    sw      a1, 12(a0)

    /* ---- Set global pointer (if used) ---- */
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop
    li      a1, 0xCAFE0004
    sw      a1, 12(a0)

    /* ---- Zero .bss section ---- */
    la      t0, __bss_start
    la      t1, __bss_end
1:
    bge     t0, t1, 2f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       1b
2:
    li      a1, 0xCAFE0005
    sw      a1, 12(a0)

    /* Phase 6: about to call main(). */
    li      a1, 0xCAFE0006
    sw      a1, 12(a0)

    /* ---- Call main() ---- */
    jal     ra, main

    li      a1, 0xCAFE0007
    sw      a1, 12(a0)
3:
    wfi
    j       3b

    .size _start, . - _start
