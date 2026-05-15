/* Masked ML-DSA helpers built on the shared d=1 DOM gadgets. */

#include "sca_pqc_masked.h"
#include <string.h>

/* =========================================================================
 * ML-DSA-44 parameters (FIPS-204 Table 1) -- matches the deployed CW305 KAT
 * firmware in mldsa_work/fw/.  k = 4, l = 4, eta = 2, gamma2 = (Q-1)/88.
 * ========================================================================= */
#define MLDSA44_K       4
#define MLDSA44_L       4
#define MLDSA44_ETA     2
#define MLDSA44_TAU     39
#define MLDSA44_BETA    78          /* tau * eta */
#define MLDSA44_GAMMA1  (1 << 17)
#define MLDSA44_GAMMA2  ((MLDSA_Q - 1) / 88)   /* = 95232 */
#define MLDSA44_OMEGA   80
#define MLDSA44_D       13          /* Power2Round split bit */

#define MLDSA44_PKBYTES     1312
#define MLDSA44_SKBYTES     2560
#define MLDSA44_SIGBYTES    2420
#define MLDSA44_SEEDBYTES   32
#define MLDSA44_CRHBYTES    64

/* =========================================================================
 * Masked polynomial vector types for ML-DSA-44
 * ========================================================================= */
typedef struct {
    masked_poly32_t vec[MLDSA44_K];
} masked_polyvec_dsa_k_t;

typedef struct {
    masked_poly32_t vec[MLDSA44_L];
} masked_polyvec_dsa_l_t;

/* =========================================================================
 * Masked polyvec operations (ML-DSA)
 * ========================================================================= */

static void masked_polyvec_ntt_k(masked_polyvec_dsa_k_t *pv)
{
    for (int i = 0; i < MLDSA44_K; i++)
        masked_ntt_dsa(&pv->vec[i]);
}

static void masked_polyvec_ntt_l(masked_polyvec_dsa_l_t *pv)
{
    for (int i = 0; i < MLDSA44_L; i++)
        masked_ntt_dsa(&pv->vec[i]);
}

static void masked_polyvec_invntt_k(masked_polyvec_dsa_k_t *pv)
{
    for (int i = 0; i < MLDSA44_K; i++)
        masked_invntt_dsa(&pv->vec[i]);
}

/* Inner product: r = sum(a[i] * b[i]) for i in [0, L) */
static void masked_polyvec_pointwise_acc_l(masked_poly32_t *r,
                                            const masked_polyvec_dsa_l_t *a,
                                            const masked_polyvec_dsa_l_t *b)
{
    masked_poly32_t tmp;
    masked_poly_pointwise_mul_dsa(r, &a->vec[0], &b->vec[0]);
    for (int i = 1; i < MLDSA44_L; i++) {
        masked_poly_pointwise_mul_dsa(&tmp, &a->vec[i], &b->vec[i]);
        masked_poly_add_dsa(r, r, &tmp);
    }
    masked_poly_reduce_dsa(r);
}

/* =========================================================================
 * Masked Power2Round (uses Group 5 ISE)
 *
 * Splits t into (t1, t0) where t = t1 * 2^d + t0.
 * Used in key generation. Since t is derived from public A and masked s,
 * t itself can be unmasked after computation (t is public in the pk).
 * However, the computation A*s needs masking.
 * ========================================================================= */
static void masked_poly_power2round(int32_t t1[MLDSA_N],
                                     int32_t t0[MLDSA_N],
                                     const masked_poly32_t *t_masked)
{
    /* Unmask t first (t will become public as part of pk) */
    int32_t t_plain[MLDSA_N];
    unmask_poly_dsa(t_plain, t_masked);

    /* Apply Power2Round using ISE */
    for (unsigned int i = 0; i < MLDSA_N; i++) {
        int32_t d_param = 13;  /* ML-DSA uses d=13 */
        int32_t result;
        SCA_POWER2ROUND(result, t_plain[i], d_param);
        /* ISE returns packed (t1, t0): t1 in upper bits, t0 in lower */
        t1[i] = result >> 16;       /* High 16 bits = t1 */
        t0[i] = (int16_t)(result);  /* Low 16 bits = t0 (sign-extended) */
    }
}

/* =========================================================================
 * Masked Decompose (uses Group 5 ISE)
 *
 * Decomposes r into (r1, r0) such that r = r1*alpha + r0
 * where alpha = 2*gamma2.
 *
 * Used in signing: w = A*y, then Decompose(w) to get w1 for hashing.
 * Since w depends on masked y, decomposition input must be unmasked
 * carefully (w is made public as part of the signature).
 * ========================================================================= */
static void masked_poly_decompose(int32_t r1[MLDSA_N],
                                   int32_t r0[MLDSA_N],
                                   const masked_poly32_t *r_masked)
{
    /* Unmask r (it becomes public as part of commitment) */
    int32_t r_plain[MLDSA_N];
    unmask_poly_dsa(r_plain, r_masked);

    for (unsigned int i = 0; i < MLDSA_N; i++) {
        int32_t alpha = 2 * MLDSA44_GAMMA2;
        int32_t result;
        SCA_DECOMPOSE(result, r_plain[i], alpha);
        r1[i] = result >> 16;
        r0[i] = (int16_t)(result);
    }
}

/* =========================================================================
 * Masked Norm Check
 *
 * Check if ||s||_inf < bound. Used for rejection in signing.
 * Must be done in masked domain to avoid leaking information
 * about the secret through timing of rejection.
 *
 * Strategy: Convert to boolean domain (A2B), then check magnitude
 * using MASKED_AND for comparison circuit.
 * ========================================================================= */
static int masked_poly_chknorm(const masked_poly32_t *p, int32_t bound)
{
    /* TODO: Full masked norm check implementation.
     *
     * Naive approach (INSECURE -- breaks masking, for structure only):
     *   Unmask, check norm, return result.
     *
     * Secure approach:
     *   1. For each coefficient, compute |coeff| in masked domain
     *      - Use A2B to convert to boolean shares
     *      - Compute absolute value using MASKED_AND/MASKED_XOR circuit
     *   2. Compare |coeff| with bound in boolean domain
     *      - Subtraction (|coeff| - bound) using masked full adder
     *      - Check sign bit
     *   3. OR all comparison results (masked OR = De Morgan via AND/XOR)
     *   4. Unmask single-bit result (acceptable leakage: pass/fail)
     *
     * The timing of the check MUST be constant regardless of
     * whether the norm exceeds the bound.
     */

    int32_t plain[MLDSA_N];
    unmask_poly_dsa(plain, p);

    for (unsigned int i = 0; i < MLDSA_N; i++) {
        int32_t val = plain[i];
        if (val < 0) val = -val;
        if (val >= bound) return 1; /* norm exceeded */
    }
    return 0;
}

/* =========================================================================
 * ML-DSA.KeyGen (Algorithm 1 of FIPS 204) -- Masked Version
 *
 * Key generation:
 *   1. (rho, rho', K) = H(xi)    -- SHAKE-256 via Keccak ISE
 *   2. A = ExpandA(rho)           -- SHAKE-128 via Keccak ISE
 *   3. (s1, s2) = ExpandS(rho')   -- CBD-like sampling (uniform eta)
 *   4. t = A * NTT(s1) + s2       -- masked NTT + masked multiply
 *   5. (t1, t0) = Power2Round(t)  -- ISE POWER2ROUND
 *   6. pk = (rho, t1), sk = (rho, K, tr, s1, s2, t0)
 * ========================================================================= */
void mldsa44_masked_keygen(uint8_t pk[MLDSA44_PKBYTES],
                            uint8_t sk[MLDSA44_SKBYTES],
                            const uint8_t xi[MLDSA44_SEEDBYTES])
{
    /* TODO: Full implementation.
     *
     * Secret values (s1, s2) are masked throughout.
     * A is public. t = A*s1 + s2 is computed in masked domain,
     * then unmasked for Power2Round (t becomes public in pk).
     *
     * ISE usage:
     *   - Keccak ISE (Group 0): SHAKE-256 for seed expansion
     *   - MONT_D, BARRETT_D (Group 1): NTT arithmetic
     *   - SECMUL_STEP_D (Group 6): masked A*s1 multiply
     *   - MASK_REFRESH_A (Group 6): NTT layer refresh
     *   - POWER2ROUND (Group 5): t -> (t1, t0) decomposition
     *   - GET_RANDOM (Group 7): masking randomness
     */

    (void)pk; (void)sk; (void)xi;
}

/* =========================================================================
 * ML-DSA.Sign (Algorithm 2 of FIPS 204) -- Masked Version
 *
 * This is the most complex masked operation. The rejection sampling
 * loop makes it especially challenging for side-channel protection.
 *
 * Signing loop:
 *   1. y = ExpandMask(rho', kappa)      -- sample masking vector
 *   2. w = A * NTT(y)                    -- masked NTT multiply
 *   3. w1 = HighBits(w)                  -- DECOMPOSE ISE
 *   4. c_tilde = H(mu || w1)             -- Keccak ISE
 *   5. c = SampleInBall(c_tilde)         -- sparse polynomial
 *   6. z = y + c*s1                      -- masked multiply + add
 *   7. r0 = LowBits(w - c*s2)            -- masked subtract + DECOMPOSE
 *   8. if ||z||_inf >= gamma1-beta: REJECT (goto 1)
 *   9. if ||r0||_inf >= gamma2-beta: REJECT
 *  10. Output signature (c_tilde, z, h)
 *
 * Critical masking points:
 *   - y must be masked to prevent DPA on step 2
 *   - c*s1 and c*s2 use SECMUL_STEP_D (c is public, s1/s2 masked)
 *     Actually c*s is LINEAR (public * masked), so per-share multiply.
 *     BUT: c is derived from w which depends on masked y, so timing
 *     of rejection leaks information about s through y.
 *   - Norm checks (steps 8-9) must be constant-time and masked
 *   - The NUMBER OF REJECTIONS must not depend on the secret key
 *     (this is the hardest part to protect)
 * ========================================================================= */
void mldsa44_masked_sign(uint8_t sig[MLDSA44_SIGBYTES],
                          const uint8_t *msg, size_t msglen,
                          const uint8_t sk[MLDSA44_SKBYTES])
{
    /* TODO: Full implementation with masked rejection sampling.
     *
     * Key ISE usage in signing loop:
     *
     * Step 2 (A*NTT(y)):
     *   - masked_ntt_dsa on y: MONT_D, BARRETT_D, MASK_REFRESH_A
     *   - Matrix multiply: per-share (A is public)
     *
     * Step 3 (Decompose):
     *   - DECOMPOSE ISE (Group 5) after unmasking w
     *
     * Step 4 (Hash):
     *   - Keccak-f[1600] via Group 0 ISE: SET_C, BCOP32, XOR3,
     *     SET_HI, ROL32_L, ROL32_H
     *
     * Step 6 (c*s1):
     *   - c is public challenge polynomial (sparse, tau nonzero coeffs)
     *   - s1 is masked secret
     *   - Multiply is LINEAR in s1 (public * masked), per-share
     *   - Uses NTT domain: NTT(c) * NTT_hat(s1) via MONT_D per share
     *
     * Steps 8-9 (Norm checks):
     *   - Masked norm check using A2B + boolean comparison circuit
     *   - MASKED_AND, MASKED_XOR for comparison
     *   - Must be constant-time regardless of result
     */

    (void)sig; (void)msg; (void)msglen; (void)sk;
}

/* =========================================================================
 * ML-DSA.Verify (Algorithm 3 of FIPS 204)
 *
 * Verification is a PUBLIC operation (no secrets involved), so it does
 * not require masking. However, ISE instructions still accelerate it:
 *   - NTT via MONT_D, BARRETT_D
 *   - DECOMPOSE ISE for HighBits/LowBits
 *   - Keccak ISE for hashing
 * ========================================================================= */
int mldsa44_verify(const uint8_t sig[MLDSA44_SIGBYTES],
                    const uint8_t *msg, size_t msglen,
                    const uint8_t pk[MLDSA44_PKBYTES])
{
    /* TODO: Full implementation.
     *
     * Verification does not need masking but benefits from ISE:
     *   1. Expand A from pk       (SHAKE-128 via Keccak ISE)
     *   2. w' = A*NTT(z) - NTT(c)*t1*2^d  (MONT_D, BARRETT_D, NTT)
     *   3. w1' = HighBits(w')     (DECOMPOSE ISE)
     *   4. c' = H(mu || w1')      (Keccak ISE)
     *   5. Check c == c' and ||z||_inf < gamma1 - beta
     */

    (void)sig; (void)msg; (void)msglen; (void)pk;
    return 0;
}
