/* Inline-asm macros that lower to the 38 Custom-3 ISE instructions. */

#ifndef SCA_PQC_ISE_H
#define SCA_PQC_ISE_H

#include <stdint.h>

/* Custom-3 opcode */
#define SCA_CUSTOM3_OPCODE  0x7B

/* =========================================================================
 * Group 0: Keccak Primitives (funct3 = 0)
 * ========================================================================= */

/* SET_C: acc_c <- rs1; rd = rs1 (echo) */
#define SCA_SET_C(rd, rs1) \
    asm volatile(".insn r 0x7b, 0, 0, %0, %1, x0" \
        : "=r"(rd) : "r"(rs1))

/* BCOP32_AB: rd = rs1 ^ (~rs2 & acc_c) */
#define SCA_BCOP32(rd, rs1, rs2) \
    asm volatile(".insn r 0x7b, 0, 1, %0, %1, %2" \
        : "=r"(rd) : "r"(rs1), "r"(rs2))

/* SET_HI: acc_hi <- rs1; rd = rs1 (echo) */
#define SCA_SET_HI(rd, rs1) \
    asm volatile(".insn r 0x7b, 0, 2, %0, %1, x0" \
        : "=r"(rd) : "r"(rs1))

/* ROL32_L: rd = low word of ROL64({acc_hi, rs1}, rs2[5:0]) */
#define SCA_ROL32_L(rd, rs1, rs2) \
    asm volatile(".insn r 0x7b, 0, 3, %0, %1, %2" \
        : "=r"(rd) : "r"(rs1), "r"(rs2))

/* ROL32_H: rd = high word of ROL64({acc_hi, rs1}, rs2[5:0]) */
#define SCA_ROL32_H(rd, rs1, rs2) \
    asm volatile(".insn r 0x7b, 0, 4, %0, %1, %2" \
        : "=r"(rd) : "r"(rs1), "r"(rs2))

/* XOR3: rd = rs1 ^ rs2 ^ acc_c */
#define SCA_XOR3(rd, rs1, rs2) \
    asm volatile(".insn r 0x7b, 0, 5, %0, %1, %2" \
        : "=r"(rd) : "r"(rs1), "r"(rs2))

/* =========================================================================
 * Group 1: Modular Arithmetic (funct3 = 1)
 * ========================================================================= */

/* BARRETT_K: rd = rs1 mod 3329 (Barrett reduction) */
#define SCA_BARRETT_K(rd, rs1) \
    asm volatile(".insn r 0x7b, 1, 0, %0, %1, x0" \
        : "=r"(rd) : "r"(rs1))

/* MONT_K: rd = Montgomery multiply rs1 * rs2, Q=3329 */
#define SCA_MONT_K(rd, rs1, rs2) \
    asm volatile(".insn r 0x7b, 1, 1, %0, %1, %2" \
        : "=r"(rd) : "r"(rs1), "r"(rs2))

/* CADDQ: rd = rs1 + Q if rs1 < 0, else rs1 */
#define SCA_CADDQ(rd, rs1) \
    asm volatile(".insn r 0x7b, 1, 2, %0, %1, x0" \
        : "=r"(rd) : "r"(rs1))

/* BARRETT_D: rd = rs1 mod 8380417 (Barrett reduction) */
#define SCA_BARRETT_D(rd, rs1) \
    asm volatile(".insn r 0x7b, 1, 3, %0, %1, x0" \
        : "=r"(rd) : "r"(rs1))

#define SCA_MONT_D_REDUCE64(rd, rs1_lo32, rs2_hi32) \
    asm volatile(".insn r 0x7b, 1, 4, %0, %1, %2" \
        : "=r"(rd) : "r"(rs1_lo32), "r"(rs2_hi32))

/* SCA_MONT_D_MUL: software multiply + ISE reduce.
 *   rd = (a * b) * R^{-1} mod 8380417, R = 2^32.
 * Equivalent to pq-crystals fqmul.  a, b are signed 32-bit. */
#define SCA_MONT_D_MUL(rd, a, b) do {                                         \
    int64_t _scapd_prod = (int64_t)(a) * (int64_t)(b);                        \
    int32_t _scapd_lo = (int32_t)_scapd_prod;                                 \
    int32_t _scapd_hi = (int32_t)(_scapd_prod >> 32);                         \
    SCA_MONT_D_REDUCE64((rd), _scapd_lo, _scapd_hi);                          \
} while (0)

/* =========================================================================
 * Group 2: NTT Butterfly (funct3 = 2)
 * ========================================================================= */

/* NTT_BFLY_CT: Cooley-Tukey butterfly */
#define SCA_NTT_BFLY_CT(rd, rs1, rs2) \
    asm volatile(".insn r 0x7b, 2, 0, %0, %1, %2" \
        : "=r"(rd) : "r"(rs1), "r"(rs2))

/* NTT_BFLY_GS: Gentleman-Sande butterfly */
#define SCA_NTT_BFLY_GS(rd, rs1, rs2) \
    asm volatile(".insn r 0x7b, 2, 1, %0, %1, %2" \
        : "=r"(rd) : "r"(rs1), "r"(rs2))

/* NTT_BFLY_CT_MUL: forward CT butterfly with twiddle Mont-mul folded in.
 * rs1 = a (low 16 bits, signed)
 * rs2 = packed { zeta[15:0], b[15:0] }   (high half = zeta, low half = b)
 * rd  = packed { (a-t)[15:0], (a+t)[15:0] }  with t = mont(b*zeta)
 */
#define SCA_NTT_BFLY_CT_MUL(rd, a, packed_zb) \
    asm volatile(".insn r 0x7b, 2, 2, %0, %1, %2" \
        : "=r"(rd) : "r"(a), "r"(packed_zb))

/* NTT_BFLY_GS_MUL: inverse GS butterfly with twiddle Mont-mul folded in.
 * Same operand layout as CT_MUL.
 * rd = packed { mont((a-b)*zeta)[15:0], (a+b)[15:0] }
 */
#define SCA_NTT_BFLY_GS_MUL(rd, a, packed_zb) \
    asm volatile(".insn r 0x7b, 2, 3, %0, %1, %2" \
        : "=r"(rd) : "r"(a), "r"(packed_zb))

/* =========================================================================
 * Group 3: CBD Sampling (funct3 = 3)
 * ========================================================================= */

/* CBD eta=2: 8 pipeline steps */
#define SCA_CBD2_1(rd, rs1) \
    asm volatile(".insn r 0x7b, 3, 0, %0, %1, x0" : "=r"(rd) : "r"(rs1))

#define SCA_CBD2_2(rd, rs1) \
    asm volatile(".insn r 0x7b, 3, 1, %0, %1, x0" : "=r"(rd) : "r"(rs1))

#define SCA_CBD2_3(rd, rs1) \
    asm volatile(".insn r 0x7b, 3, 2, %0, %1, x0" : "=r"(rd) : "r"(rs1))

#define SCA_CBD2_4(rd, rs1) \
    asm volatile(".insn r 0x7b, 3, 3, %0, %1, x0" : "=r"(rd) : "r"(rs1))

#define SCA_CBD2_5(rd, rs1) \
    asm volatile(".insn r 0x7b, 3, 4, %0, %1, x0" : "=r"(rd) : "r"(rs1))

#define SCA_CBD2_6(rd, rs1) \
    asm volatile(".insn r 0x7b, 3, 5, %0, %1, x0" : "=r"(rd) : "r"(rs1))

#define SCA_CBD2_7(rd, rs1) \
    asm volatile(".insn r 0x7b, 3, 6, %0, %1, x0" : "=r"(rd) : "r"(rs1))

#define SCA_CBD2_8(rd, rs1) \
    asm volatile(".insn r 0x7b, 3, 7, %0, %1, x0" : "=r"(rd) : "r"(rs1))

/* CBD eta=3: 4 pipeline steps */
#define SCA_CBD3_1(rd, rs1) \
    asm volatile(".insn r 0x7b, 3, 8, %0, %1, x0" : "=r"(rd) : "r"(rs1))

#define SCA_CBD3_2(rd, rs1) \
    asm volatile(".insn r 0x7b, 3, 9, %0, %1, x0" : "=r"(rd) : "r"(rs1))

#define SCA_CBD3_3(rd, rs1) \
    asm volatile(".insn r 0x7b, 3, 10, %0, %1, x0" : "=r"(rd) : "r"(rs1))

#define SCA_CBD3_4(rd, rs1) \
    asm volatile(".insn r 0x7b, 3, 11, %0, %1, x0" : "=r"(rd) : "r"(rs1))

/* =========================================================================
 * Group 4: Compress + Sampling (funct3 = 4)
 * ========================================================================= */

#define SCA_COMPRESS_1(rd, rs1) \
    asm volatile(".insn r 0x7b, 4, 0, %0, %1, x0" : "=r"(rd) : "r"(rs1))

#define SCA_COMPRESS_2(rd, rs1) \
    asm volatile(".insn r 0x7b, 4, 1, %0, %1, x0" : "=r"(rd) : "r"(rs1))

#define SCA_COMPRESS_3(rd, rs1) \
    asm volatile(".insn r 0x7b, 4, 2, %0, %1, x0" : "=r"(rd) : "r"(rs1))

#define SCA_COMPRESS_4(rd, rs1) \
    asm volatile(".insn r 0x7b, 4, 3, %0, %1, x0" : "=r"(rd) : "r"(rs1))

/* REJ_UNIFORM: Rejection uniform sampling */
#define SCA_REJ_UNIFORM(rd, rs1) \
    asm volatile(".insn r 0x7b, 4, 4, %0, %1, x0" : "=r"(rd) : "r"(rs1))

/* COMPRESS_5: d=11 (ML-KEM-1024 du). rd = round(rs1 * 2^11 / 3329) & 0x7FF */
#define SCA_COMPRESS_5(rd, rs1) \
    asm volatile(".insn r 0x7b, 4, 5, %0, %1, x0" : "=r"(rd) : "r"(rs1))

/* =========================================================================
 * Group 5: ML-DSA Operations (funct3 = 5)
 * ========================================================================= */

/* POWER2ROUND: Power2Round decomposition */
#define SCA_POWER2ROUND(rd, rs1, rs2) \
    asm volatile(".insn r 0x7b, 5, 0, %0, %1, %2" \
        : "=r"(rd) : "r"(rs1), "r"(rs2))

/* DECOMPOSE: HighBits/LowBits decomposition */
#define SCA_DECOMPOSE(rd, rs1, rs2) \
    asm volatile(".insn r 0x7b, 5, 1, %0, %1, %2" \
        : "=r"(rd) : "r"(rs1), "r"(rs2))

/* =========================================================================
 * Group 6: Masking Primitives (funct3 = 6)
 *
 * funct7[4] (bit 29) = Q selector: 0 = Q=3329 (ML-KEM), 1 = Q=8380417 (ML-DSA)
 * For MASK_REFRESH: 0 = boolean, 1 = arithmetic
 * ========================================================================= */

/* SECMUL_STEP: (rs1 * rs2 + PRNG) mod Q */
/* Q=3329 (ML-KEM): funct7 = (0 << 4) | 0 = 0 */
#define SCA_SECMUL_STEP_K(rd, rs1, rs2) \
    asm volatile(".insn r 0x7b, 6, 0, %0, %1, %2" \
        : "=r"(rd) : "r"(rs1), "r"(rs2))

/* Q=8380417 (ML-DSA): funct7 = (1 << 4) | 0 = 16 */
#define SCA_SECMUL_STEP_D(rd, rs1, rs2) \
    asm volatile(".insn r 0x7b, 6, 16, %0, %1, %2" \
        : "=r"(rd) : "r"(rs1), "r"(rs2))

/* MASK_REFRESH: Re-randomize share */
/* Boolean refresh: funct7 = (0 << 4) | 1 = 1 */
#define SCA_MASK_REFRESH_B(rd, rs1) \
    asm volatile(".insn r 0x7b, 6, 1, %0, %1, x0" \
        : "=r"(rd) : "r"(rs1))

/* Arithmetic refresh: funct7 = (1 << 4) | 1 = 17 */
#define SCA_MASK_REFRESH_A(rd, rs1) \
    asm volatile(".insn r 0x7b, 6, 17, %0, %1, x0" \
        : "=r"(rd) : "r"(rs1))

/* MASKED_AND: DOM-AND gate */
#define SCA_MASKED_AND(rd, rs1, rs2) \
    asm volatile(".insn r 0x7b, 6, 2, %0, %1, %2" \
        : "=r"(rd) : "r"(rs1), "r"(rs2))

/* MASKED_XOR: Linear XOR of masked shares */
#define SCA_MASKED_XOR(rd, rs1, rs2) \
    asm volatile(".insn r 0x7b, 6, 3, %0, %1, %2" \
        : "=r"(rd) : "r"(rs1), "r"(rs2))

/* =========================================================================
 * Paired DOM gadgets (Gross-Mangard "+r/-r" cancellation).
 *
 * Use as a back-to-back pair within a single masked-multiply or DOM-AND.
 * Bare-metal firmware MUST disable interrupts around the pair to guarantee
 * no other PRNG-consuming op clobbers r_latched between LATCH and REUSE.
 *
 *   SECMUL_LATCH_K(rd, rs1, rs2): rd = (rs1*rs2 + r_new) mod 3329; latch r_new
 *   SECMUL_REUSE_K(rd, rs1, rs2): rd = (rs1*rs2 - r_latched) mod 3329
 *   SECMUL_LATCH_D(rd, rs1, rs2): rd = (rs1*rs2 + r_new) mod 8380417
 *   SECMUL_REUSE_D(rd, rs1, rs2): rd = (rs1*rs2 - r_latched) mod 8380417
 *   AND_LATCH      (rd, rs1, rs2): rd = (rs1 & rs2) ^ r_new; latch r_new
 *   AND_REUSE      (rd, rs1, rs2): rd = (rs1 & rs2) ^ r_latched
 * ========================================================================= */

/* SECMUL_LATCH_K: funct7 = (0 << 4) | 4 = 4 */
#define SCA_SECMUL_LATCH_K(rd, rs1, rs2) \
    asm volatile(".insn r 0x7b, 6, 4, %0, %1, %2" \
        : "=r"(rd) : "r"(rs1), "r"(rs2))

/* SECMUL_LATCH_D: funct7 = (1 << 4) | 4 = 20 */
#define SCA_SECMUL_LATCH_D(rd, rs1, rs2) \
    asm volatile(".insn r 0x7b, 6, 20, %0, %1, %2" \
        : "=r"(rd) : "r"(rs1), "r"(rs2))

/* SECMUL_REUSE_K: funct7 = (0 << 4) | 5 = 5 */
#define SCA_SECMUL_REUSE_K(rd, rs1, rs2) \
    asm volatile(".insn r 0x7b, 6, 5, %0, %1, %2" \
        : "=r"(rd) : "r"(rs1), "r"(rs2))

/* SECMUL_REUSE_D: funct7 = (1 << 4) | 5 = 21 */
#define SCA_SECMUL_REUSE_D(rd, rs1, rs2) \
    asm volatile(".insn r 0x7b, 6, 21, %0, %1, %2" \
        : "=r"(rd) : "r"(rs1), "r"(rs2))

/* AND_LATCH: funct7 = 6 */
#define SCA_AND_LATCH(rd, rs1, rs2) \
    asm volatile(".insn r 0x7b, 6, 6, %0, %1, %2" \
        : "=r"(rd) : "r"(rs1), "r"(rs2))

/* AND_REUSE: funct7 = 7 */
#define SCA_AND_REUSE(rd, rs1, rs2) \
    asm volatile(".insn r 0x7b, 6, 7, %0, %1, %2" \
        : "=r"(rd) : "r"(rs1), "r"(rs2))

/* =========================================================================
 * Atomicity-region macros for paired DOM gadgets.
 *
 * The hardware r_latched register is now guarded by a r_latched_valid bit:
 * any *_REUSE op that fires while the bit is low takes a coprocessor trap.
 * Even with that hardware enforcement, software must avoid PRNG-consuming
 * ops between LATCH and REUSE within a single dom_mul gadget, since GET_RANDOM,
 * MASK_REFRESH, MASKED_AND or legacy SECMUL_STEP would invalidate the latch
 * and convert a correctness path into a trap path.
 *
 * SCA_DOM_REGION_BEGIN() disables M-mode interrupts (mstatus.MIE bit 3)
 * SCA_DOM_REGION_END()   re-enables M-mode interrupts.
 *
 * On host (-DSCA_HOST_SIM) these are no-ops because there is no ISR; the
 * sequential C call order already guarantees atomicity.
 * ========================================================================= */
#define SCA_DOM_REGION_BEGIN() \
    asm volatile("csrrci x0, mstatus, 8" ::: "memory")
#define SCA_DOM_REGION_END() \
    asm volatile("csrrsi x0, mstatus, 8" ::: "memory")

/* =========================================================================
 * Group 7: PRNG Control (funct3 = 7)
 * ========================================================================= */

/* SEED_PRNG: Seed PRNG word; rs1 = seed data, rs2[1:0] = word index */
#define SCA_SEED_PRNG(rd, rs1, rs2) \
    asm volatile(".insn r 0x7b, 7, 0, %0, %1, %2" \
        : "=r"(rd) : "r"(rs1), "r"(rs2))

/* GET_RANDOM: Get 32-bit random word from PRNG */
#define SCA_GET_RANDOM(rd) \
    asm volatile(".insn r 0x7b, 7, 1, %0, x0, x0" \
        : "=r"(rd))

/* =========================================================================
 * Convenience: Keccak round helpers
 * ========================================================================= */

/* Execute chi step: rd = a ^ (~b & c) using SET_C + BCOP32 */
static inline uint32_t sca_chi_step(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t tmp, result;
    SCA_SET_C(tmp, c);
    SCA_BCOP32(result, a, b);
    return result;
}

/* Execute XOR3: rd = a ^ b ^ c using SET_C + XOR3 */
static inline uint32_t sca_xor3(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t tmp, result;
    SCA_SET_C(tmp, c);
    SCA_XOR3(result, a, b);
    return result;
}

/* Execute 64-bit rotate left using SET_HI + ROL32_L + ROL32_H */
static inline void sca_rol64(uint32_t hi_in, uint32_t lo_in, uint32_t rot,
                             uint32_t *hi_out, uint32_t *lo_out)
{
    uint32_t tmp;
    SCA_SET_HI(tmp, hi_in);
    SCA_ROL32_L(*lo_out, lo_in, rot);
    SCA_ROL32_H(*hi_out, lo_in, rot);
}

/* =========================================================================
 * Host simulation override (build with -DSCA_HOST_SIM)
 *
 * Replaces the RV32 inline-asm bodies above with pure-C functions that
 * reproduce the RTL semantics bit-for-bit, so masked code can KAT-test
 * on the development host without flashing CW305 each iteration.  The
 * NTT-butterfly / CBD / Compress / REJ / P2Round / Decompose / Keccak
 * macros are deliberately NOT host-overridden -- the host KAT path
 * uses the pq-crystals reference helpers for those, and only the
 * mask-related primitives must match RTL.
 * ========================================================================= */
#ifdef SCA_HOST_SIM
#include "sca_pqc_ise_host.h"

#undef  SCA_GET_RANDOM
#define SCA_GET_RANDOM(rd)            ((rd) = sca_host_get_random())

#undef  SCA_BARRETT_K
#define SCA_BARRETT_K(rd, rs1)        ((rd) = sca_host_barrett_k((int32_t)(rs1)))
#undef  SCA_MONT_K
#define SCA_MONT_K(rd, rs1, rs2)      ((rd) = sca_host_mont_k((int32_t)(rs1), (int32_t)(rs2)))
#undef  SCA_CADDQ
#define SCA_CADDQ(rd, rs1)            ((rd) = sca_host_caddq((int32_t)(rs1)))
#undef  SCA_BARRETT_D
#define SCA_BARRETT_D(rd, rs1)        ((rd) = sca_host_barrett_d((int32_t)(rs1)))
#undef  SCA_MONT_D_REDUCE64
#define SCA_MONT_D_REDUCE64(rd, rs1, rs2)  ((rd) = sca_host_mont_d((int32_t)(rs1), (int32_t)(rs2)))
/* SCA_MONT_D_MUL is a do-while macro defined above; it expands to two int32 ops
 * plus a SCA_MONT_D_REDUCE64.  Nothing to override here for host mode. */

#undef  SCA_SECMUL_STEP_K
#define SCA_SECMUL_STEP_K(rd, a, b)   ((rd) = sca_host_secmul_step_k((int32_t)(a), (int32_t)(b)))
#undef  SCA_SECMUL_STEP_D
#define SCA_SECMUL_STEP_D(rd, a, b)   ((rd) = sca_host_secmul_step_d((int32_t)(a), (int32_t)(b)))

#undef  SCA_SECMUL_LATCH_K
#define SCA_SECMUL_LATCH_K(rd, a, b)  ((rd) = sca_host_secmul_latch_k((int32_t)(a), (int32_t)(b)))
#undef  SCA_SECMUL_REUSE_K
#define SCA_SECMUL_REUSE_K(rd, a, b)  ((rd) = sca_host_secmul_reuse_k((int32_t)(a), (int32_t)(b)))
#undef  SCA_SECMUL_LATCH_D
#define SCA_SECMUL_LATCH_D(rd, a, b)  ((rd) = sca_host_secmul_latch_d((int32_t)(a), (int32_t)(b)))
#undef  SCA_SECMUL_REUSE_D
#define SCA_SECMUL_REUSE_D(rd, a, b)  ((rd) = sca_host_secmul_reuse_d((int32_t)(a), (int32_t)(b)))

#undef  SCA_AND_LATCH
#define SCA_AND_LATCH(rd, a, b)       ((rd) = sca_host_and_latch((uint32_t)(a), (uint32_t)(b)))
#undef  SCA_AND_REUSE
#define SCA_AND_REUSE(rd, a, b)       ((rd) = sca_host_and_reuse((uint32_t)(a), (uint32_t)(b)))
#undef  SCA_MASKED_AND
#define SCA_MASKED_AND(rd, a, b)      ((rd) = sca_host_masked_and((uint32_t)(a), (uint32_t)(b)))
#undef  SCA_MASKED_XOR
#define SCA_MASKED_XOR(rd, a, b)      ((rd) = ((uint32_t)(a) ^ (uint32_t)(b)))

/* SEED_PRNG on host: rs1 = seed word, rs2 ignored */
#undef  SCA_SEED_PRNG
#define SCA_SEED_PRNG(rd, rs1, rs2)   do { sca_host_seed_prng((uint32_t)(rs1)); (rd) = (rs1); } while (0)

/* DOM-region atomicity macros are no-ops on host (no ISR can preempt). */
#undef  SCA_DOM_REGION_BEGIN
#define SCA_DOM_REGION_BEGIN() ((void)0)
#undef  SCA_DOM_REGION_END
#define SCA_DOM_REGION_END()   ((void)0)

/* -------- CBD eta=2 / eta=3 host helpers (functional reference only) ----
 * Pure-C versions of the 8 CBD2 and 4 CBD3 pipeline steps.  Each call
 * extracts one coefficient from the input 32-bit / 24-bit word in
 * the same lane order as the RTL ise_cbd module.
 * ----------------------------------------------------------------------- */
static inline int32_t _sca_host_cbd2_step(uint32_t w, int slot)
{
    /* Each coefficient consumes 4 bits: 2 bits "a" + 2 bits "b" at offset 4*slot. */
    int a = (int)((w >> (4 * slot     )) & 0x3u);
    int b = (int)((w >> (4 * slot + 2)) & 0x3u);
    int popa = (a & 1) + ((a >> 1) & 1);
    int popb = (b & 1) + ((b >> 1) & 1);
    return (int32_t)(popa - popb);
}

static inline int32_t _sca_host_cbd3_step(uint32_t w, int slot)
{
    int a = (int)((w >> (6 * slot     )) & 0x7u);
    int b = (int)((w >> (6 * slot + 3)) & 0x7u);
    int popa = (a & 1) + ((a >> 1) & 1) + ((a >> 2) & 1);
    int popb = (b & 1) + ((b >> 1) & 1) + ((b >> 2) & 1);
    return (int32_t)(popa - popb);
}

#undef SCA_CBD2_1
#undef SCA_CBD2_2
#undef SCA_CBD2_3
#undef SCA_CBD2_4
#undef SCA_CBD2_5
#undef SCA_CBD2_6
#undef SCA_CBD2_7
#undef SCA_CBD2_8
#define SCA_CBD2_1(rd, w) ((rd) = _sca_host_cbd2_step((uint32_t)(w), 0))
#define SCA_CBD2_2(rd, w) ((rd) = _sca_host_cbd2_step((uint32_t)(w), 1))
#define SCA_CBD2_3(rd, w) ((rd) = _sca_host_cbd2_step((uint32_t)(w), 2))
#define SCA_CBD2_4(rd, w) ((rd) = _sca_host_cbd2_step((uint32_t)(w), 3))
#define SCA_CBD2_5(rd, w) ((rd) = _sca_host_cbd2_step((uint32_t)(w), 4))
#define SCA_CBD2_6(rd, w) ((rd) = _sca_host_cbd2_step((uint32_t)(w), 5))
#define SCA_CBD2_7(rd, w) ((rd) = _sca_host_cbd2_step((uint32_t)(w), 6))
#define SCA_CBD2_8(rd, w) ((rd) = _sca_host_cbd2_step((uint32_t)(w), 7))

#undef SCA_CBD3_1
#undef SCA_CBD3_2
#undef SCA_CBD3_3
#undef SCA_CBD3_4
#define SCA_CBD3_1(rd, w) ((rd) = _sca_host_cbd3_step((uint32_t)(w), 0))
#define SCA_CBD3_2(rd, w) ((rd) = _sca_host_cbd3_step((uint32_t)(w), 1))
#define SCA_CBD3_3(rd, w) ((rd) = _sca_host_cbd3_step((uint32_t)(w), 2))
#define SCA_CBD3_4(rd, w) ((rd) = _sca_host_cbd3_step((uint32_t)(w), 3))

/* COMPRESS host helpers: just pass-through identity with a TODO marker.
 * The DOM tests never exercise compression, but masked_poly_compress_d in
 * mlkem_masked.c references SCA_COMPRESS_1 -- give it a minimal stub so the
 * file still links if compiled.  Real compression is bench/RTL only. */
#undef  SCA_COMPRESS_1
#define SCA_COMPRESS_1(rd, rs1)       ((rd) = (int32_t)((((int32_t)(rs1) & 0xFFFF) + 1665) / 3329 & 1))
#undef  SCA_COMPRESS_2
#define SCA_COMPRESS_2(rd, rs1)       ((rd) = (int32_t)(rs1))
#undef  SCA_COMPRESS_3
#define SCA_COMPRESS_3(rd, rs1)       ((rd) = (int32_t)(rs1))
#undef  SCA_COMPRESS_4
#define SCA_COMPRESS_4(rd, rs1)       ((rd) = (int32_t)(rs1))
#undef  SCA_COMPRESS_5
#define SCA_COMPRESS_5(rd, rs1)       ((rd) = (int32_t)(rs1))

#endif /* SCA_HOST_SIM */

#endif /* SCA_PQC_ISE_H */
