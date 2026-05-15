/* Memory-mapped register definitions (mailbox, TRNG, A2B/B2A peripherals). */

#ifndef SCA_PQC_MMIO_H
#define SCA_PQC_MMIO_H

#include <stdint.h>

/* =========================================================================
 * Base addresses
 * ========================================================================= */
#define A2B_B2A_BASE        0x50000000UL
#define TRNG_BASE           0x50000100UL

/* =========================================================================
 * A2B / B2A Mask Conversion Peripheral Registers
 * =========================================================================
 *
 * Offset  Name         R/W  Description
 * ------  -----------  ---  ------------------------------------------------
 * 0x00    CTRL         W    [0]   start conversion
 *                           [1]   direction: 0=A2B, 1=B2A
 *                           [2]   wide_mode: 0=12-bit (ML-KEM), 1=23-bit (ML-DSA)
 * 0x04    STATUS       R    [0]   busy
 *                           [1]   done (write 1 to CTRL[0] clears done)
 * 0x08    SHARE0_IN    W    Input share 0
 * 0x0C    SHARE1_IN    W    Input share 1
 * 0x10    SHARE0_OUT   R    Output share 0
 * 0x14    SHARE1_OUT   R    Output share 1
 * 0x18    RANDOM_IN    W    Fresh randomness for conversion
 */

#define A2B_REG_CTRL        (A2B_B2A_BASE + 0x00)
#define A2B_REG_STATUS      (A2B_B2A_BASE + 0x04)
#define A2B_REG_SHARE0_IN   (A2B_B2A_BASE + 0x08)
#define A2B_REG_SHARE1_IN   (A2B_B2A_BASE + 0x0C)
#define A2B_REG_SHARE0_OUT  (A2B_B2A_BASE + 0x10)
#define A2B_REG_SHARE1_OUT  (A2B_B2A_BASE + 0x14)
#define A2B_REG_RANDOM_IN   (A2B_B2A_BASE + 0x18)

/* CTRL register bit positions */
#define A2B_CTRL_START      (1U << 0)
#define A2B_CTRL_DIR_B2A    (1U << 1)   /* 0=A2B, 1=B2A */
#define A2B_CTRL_WIDE       (1U << 2)   /* 0=12-bit, 1=23-bit */

/* STATUS register bit positions */
#define A2B_STATUS_BUSY     (1U << 0)
#define A2B_STATUS_DONE     (1U << 1)

/* =========================================================================
 * TRNG Peripheral Registers
 * =========================================================================
 *
 * Offset  Name         R/W  Description
 * ------  -----------  ---  ------------------------------------------------
 * 0x00    CTRL         W    [0]   enable ring oscillators
 *                           [1]   seed internal PRNG from TRNG entropy
 *                           [7:4] post-processing mode (0=raw, 1=vonNeumann)
 * 0x04    STATUS       R    [0]   entropy valid (at least 1 word available)
 *                           [1]   PRNG seeded
 *                           [7:4] health test status (0=pass)
 * 0x08    DATA         R    32-bit true random word (auto-advances FIFO)
 * 0x0C    HEALTH       R    Health test counters (repetition + adaptive)
 * 0x10    SEED_DATA    W    Manual seed word for PRNG (for deterministic test)
 * 0x14    SEED_IDX     W    Seed word index [1:0] (4 words total)
 */

#define TRNG_REG_CTRL       (TRNG_BASE + 0x00)
#define TRNG_REG_STATUS     (TRNG_BASE + 0x04)
#define TRNG_REG_DATA       (TRNG_BASE + 0x08)
#define TRNG_REG_HEALTH     (TRNG_BASE + 0x0C)
#define TRNG_REG_SEED_DATA  (TRNG_BASE + 0x10)
#define TRNG_REG_SEED_IDX   (TRNG_BASE + 0x14)

/* TRNG CTRL bits */
#define TRNG_CTRL_ENABLE    (1U << 0)
#define TRNG_CTRL_SEED_PRNG (1U << 1)
#define TRNG_CTRL_PP_RAW    (0U << 4)
#define TRNG_CTRL_PP_VN     (1U << 4)

/* TRNG STATUS bits */
#define TRNG_STATUS_VALID   (1U << 0)
#define TRNG_STATUS_SEEDED  (1U << 1)
#define TRNG_STATUS_HEALTH_MASK (0xFU << 4)

/* =========================================================================
 * Low-level MMIO access
 * ========================================================================= */

#ifdef SCA_HOST_SIM
/* Host build: MMIO target addresses are not dereferenced as physical pointers.
 * Stubs are sufficient because the host A2B/B2A path uses sca_host_a2b /
 * sca_host_b2a (declared below) which mirror the OBI peripheral semantics
 * via pure C, instead of issuing MMIO writes/reads. */
static inline void mmio_write(uint32_t addr, uint32_t val) { (void)addr; (void)val; }
static inline uint32_t mmio_read(uint32_t addr) { (void)addr; return 0u; }
#else
#define MMIO_REG(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

static inline void mmio_write(uint32_t addr, uint32_t val)
{
    MMIO_REG(addr) = val;
}

static inline uint32_t mmio_read(uint32_t addr)
{
    return MMIO_REG(addr);
}
#endif

/* =========================================================================
 * A2B / B2A Helper Functions
 * ========================================================================= */

/**
 * Perform Arithmetic-to-Boolean mask conversion.
 *
 * Converts arithmetic shares (x0 + x1 mod 2^k) to boolean shares (y0 XOR y1).
 *
 * @param share0_in   Arithmetic share 0
 * @param share1_in   Arithmetic share 1
 * @param random      Fresh randomness
 * @param wide_mode   0 = 12-bit (ML-KEM, q=3329), 1 = 23-bit (ML-DSA, q=8380417)
 * @param share0_out  Pointer to receive boolean share 0
 * @param share1_out  Pointer to receive boolean share 1
 */
#ifdef SCA_HOST_SIM
/* Host emulation of the OBI peripheral A2B/B2A, mirroring the X2X core in
 * rtl/obi/a2b_core.sv.  For ML-DSA (wide_mode=1), W_MAX=24 (23-bit modulus
 * + 1 overflow bit), NEG_Q_DSA = 2^24 - 8380417 = 8396799.  The core does
 * a raw masked sum mod 2^W, then a conditional subtract of NEG_Q_DSA to
 * bring the result into [0, Q) -- equivalent to the standard mod-Q
 * reduction for inputs whose true integer sum is in (-Q, Q).
 *
 * For ML-KEM (wide_mode=0), W=13, NEG_Q_KEM = 2^13 - 3329 = 4863.
 *
 * Output is a fresh boolean split with random as one share. */
static inline void a2b_convert(uint32_t share0_in, uint32_t share1_in,
                               uint32_t random, int wide_mode,
                               uint32_t *share0_out, uint32_t *share1_out)
{
    uint32_t W_mask  = wide_mode ? 0x00FFFFFFu : 0x00001FFFu;  /* 24 / 13 bits */
    uint32_t out_mask= wide_mode ? 0x007FFFFFu : 0x00000FFFu;  /* 23 / 12 bits */
    uint32_t neg_q   = wide_mode ? 8396799u    : 4863u;        /* 2^W - Q */
    uint32_t Q_val   = wide_mode ? 8380417u    : 3329u;
    uint32_t raw     = (share0_in + share1_in) & W_mask;
    uint32_t trial   = (raw + neg_q) & W_mask;
    uint32_t recon   = (raw >= Q_val) ? trial : raw;
    uint32_t y1      = random & out_mask;
    *share1_out = y1;
    *share0_out = (recon ^ y1) & out_mask;
}
#else
static inline void a2b_convert(uint32_t share0_in, uint32_t share1_in,
                               uint32_t random, int wide_mode,
                               uint32_t *share0_out, uint32_t *share1_out)
{
    /* Load inputs */
    mmio_write(A2B_REG_SHARE0_IN, share0_in);
    mmio_write(A2B_REG_SHARE1_IN, share1_in);
    mmio_write(A2B_REG_RANDOM_IN, random);

    /* Start A2B conversion */
    uint32_t ctrl = A2B_CTRL_START;
    if (wide_mode)
        ctrl |= A2B_CTRL_WIDE;
    mmio_write(A2B_REG_CTRL, ctrl);

    /* Poll until done */
    while (!(mmio_read(A2B_REG_STATUS) & A2B_STATUS_DONE))
        ;

    /* Read results */
    *share0_out = mmio_read(A2B_REG_SHARE0_OUT);
    *share1_out = mmio_read(A2B_REG_SHARE1_OUT);
}
#endif

/**
 * Perform Boolean-to-Arithmetic mask conversion.
 *
 * Converts boolean shares (x0 XOR x1) to arithmetic shares (y0 + y1 mod 2^k).
 *
 * @param share0_in   Boolean share 0
 * @param share1_in   Boolean share 1
 * @param random      Fresh randomness
 * @param wide_mode   0 = 12-bit (ML-KEM), 1 = 23-bit (ML-DSA)
 * @param share0_out  Pointer to receive arithmetic share 0
 * @param share1_out  Pointer to receive arithmetic share 1
 */
#ifdef SCA_HOST_SIM
static inline void b2a_convert(uint32_t share0_in, uint32_t share1_in,
                               uint32_t random, int wide_mode,
                               uint32_t *share0_out, uint32_t *share1_out)
{
    uint32_t out_mask = wide_mode ? 0x007FFFFFu : 0x00000FFFu;
    uint32_t Q_val    = wide_mode ? 8380417u    : 3329u;
    uint32_t recon    = (share0_in ^ share1_in) & out_mask;
    uint32_t y1       = random % Q_val;             /* uniform mod-Q random */
    *share1_out = y1;
    *share0_out = (recon + Q_val - y1) % Q_val;
}
#else
static inline void b2a_convert(uint32_t share0_in, uint32_t share1_in,
                               uint32_t random, int wide_mode,
                               uint32_t *share0_out, uint32_t *share1_out)
{
    /* Load inputs */
    mmio_write(A2B_REG_SHARE0_IN, share0_in);
    mmio_write(A2B_REG_SHARE1_IN, share1_in);
    mmio_write(A2B_REG_RANDOM_IN, random);

    /* Start B2A conversion */
    uint32_t ctrl = A2B_CTRL_START | A2B_CTRL_DIR_B2A;
    if (wide_mode)
        ctrl |= A2B_CTRL_WIDE;
    mmio_write(A2B_REG_CTRL, ctrl);

    /* Poll until done */
    while (!(mmio_read(A2B_REG_STATUS) & A2B_STATUS_DONE))
        ;

    /* Read results */
    *share0_out = mmio_read(A2B_REG_SHARE0_OUT);
    *share1_out = mmio_read(A2B_REG_SHARE1_OUT);
}
#endif

/* =========================================================================
 * TRNG Helper Functions
 * ========================================================================= */

/**
 * Enable the TRNG with specified post-processing mode.
 *
 * @param pp_mode  Post-processing: TRNG_CTRL_PP_RAW or TRNG_CTRL_PP_VN
 */
static inline void trng_enable(uint32_t pp_mode)
{
    mmio_write(TRNG_REG_CTRL, TRNG_CTRL_ENABLE | pp_mode);
}

/**
 * Read a 32-bit true random word. Blocks until entropy is available.
 *
 * @return  32-bit random word
 */
static inline uint32_t trng_read(void)
{
    while (!(mmio_read(TRNG_REG_STATUS) & TRNG_STATUS_VALID))
        ;
    return mmio_read(TRNG_REG_DATA);
}

/**
 * Seed the internal ISE PRNG from TRNG entropy.
 * Writes 4 seed words via the TRNG peripheral, then triggers the seed
 * operation so the coprocessor PRNG is ready for masked operations.
 */
static inline void trng_seed_prng(void)
{
    /* Wait for TRNG to have entropy */
    for (int i = 0; i < 4; i++) {
        uint32_t rng = trng_read();
        mmio_write(TRNG_REG_SEED_DATA, rng);
        mmio_write(TRNG_REG_SEED_IDX, (uint32_t)i);
    }

    /* Trigger PRNG seed latch */
    mmio_write(TRNG_REG_CTRL, TRNG_CTRL_ENABLE | TRNG_CTRL_SEED_PRNG);

    /* Wait for PRNG seeded status */
    while (!(mmio_read(TRNG_REG_STATUS) & TRNG_STATUS_SEEDED))
        ;
}

/**
 * Read the TRNG health status.
 *
 * @return  0 if health tests pass, non-zero on failure
 */
static inline uint32_t trng_health_status(void)
{
    return (mmio_read(TRNG_REG_STATUS) & TRNG_STATUS_HEALTH_MASK) >> 4;
}

#endif /* SCA_PQC_MMIO_H */
