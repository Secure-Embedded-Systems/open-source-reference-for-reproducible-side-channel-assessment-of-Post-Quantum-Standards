/* Masked polynomial type and function declarations (2 shares, d=1 DOM). */

#ifndef SCA_PQC_MASKED_H
#define SCA_PQC_MASKED_H

#include <stdint.h>
#include "sca_pqc_ise.h"
#include "sca_pqc_mmio.h"

/* =========================================================================
 * Constants
 * ========================================================================= */
#define MLKEM_N     256
#define MLKEM_Q     3329

/* Default to ML-KEM-1024 (k=4) to match the deployed CW305 KAT firmware. */
#ifndef MLKEM_K
#define MLKEM_K     4
#endif

#if   MLKEM_K == 2
#define MLKEM_ETA1  3
#define MLKEM_DU    10
#define MLKEM_DV    4
#elif MLKEM_K == 3
#define MLKEM_ETA1  2
#define MLKEM_DU    10
#define MLKEM_DV    4
#elif MLKEM_K == 4
#define MLKEM_ETA1  2
#define MLKEM_DU    11
#define MLKEM_DV    5
#else
#error "MLKEM_K must be 2, 3, or 4"
#endif
#define MLKEM_ETA2  2

#define MLDSA_N     256
#define MLDSA_Q     8380417
/* Default to ML-DSA-44 (k=4, l=4) to match the deployed CW305 KAT firmware. */
#ifndef MLDSA_K
#define MLDSA_K     4
#endif
#ifndef MLDSA_L
#define MLDSA_L     4
#endif

#define MASKED_SHARES  2    /* First-order: 2 shares */

/* =========================================================================
 * Masked polynomial types
 * ========================================================================= */

/* ML-KEM: 16-bit coefficients, 2 shares */
typedef struct {
    int16_t coeffs[MASKED_SHARES][MLKEM_N];
} masked_poly_t;

/* ML-DSA: 32-bit coefficients, 2 shares */
typedef struct {
    int32_t coeffs[MASKED_SHARES][MLDSA_N];
} masked_poly32_t;

/* =========================================================================
 * System initialization
 * ========================================================================= */

/* Initialize ISE PRNG from TRNG + seed OBI PRNG */
void sca_pqc_init(void);

/* =========================================================================
 * Masked NTT (ML-KEM, Q=3329)
 * ========================================================================= */

/* Forward NTT: 7-layer incomplete NTT (Cooley-Tukey) */
void masked_ntt_kem(masked_poly_t *p);

/* Inverse NTT: 7-layer incomplete INTT (Gentleman-Sande) */
void masked_invntt_kem(masked_poly_t *p);

/* =========================================================================
 * Masked polynomial arithmetic (ML-KEM)
 * ========================================================================= */

/* Pointwise multiply using SECMUL_STEP for cross-share products (DOM) */
void masked_poly_pointwise_mul_kem(masked_poly_t *r,
                                    const masked_poly_t *a,
                                    const masked_poly_t *b);

/* Basemul: degree-1 polynomial multiply in NTT domain */
void masked_poly_basemul_kem(masked_poly_t *r,
                              const masked_poly_t *a,
                              const masked_poly_t *b);

/* Linear operations -- applied per share independently */
void masked_poly_add_kem(masked_poly_t *r,
                          const masked_poly_t *a,
                          const masked_poly_t *b);
void masked_poly_sub_kem(masked_poly_t *r,
                          const masked_poly_t *a,
                          const masked_poly_t *b);

/* Barrett reduction on all coefficients */
void masked_poly_reduce_kem(masked_poly_t *p);

/* =========================================================================
 * Masked NTT (ML-DSA, Q=8380417)
 * ========================================================================= */

void masked_ntt_dsa(masked_poly32_t *p);
void masked_invntt_dsa(masked_poly32_t *p);

/* =========================================================================
 * Masked polynomial arithmetic (ML-DSA)
 * ========================================================================= */

void masked_poly_pointwise_mul_dsa(masked_poly32_t *r,
                                    const masked_poly32_t *a,
                                    const masked_poly32_t *b);
void masked_poly_add_dsa(masked_poly32_t *r,
                          const masked_poly32_t *a,
                          const masked_poly32_t *b);
void masked_poly_sub_dsa(masked_poly32_t *r,
                          const masked_poly32_t *a,
                          const masked_poly32_t *b);
void masked_poly_reduce_dsa(masked_poly32_t *p);

/* =========================================================================
 * Sampling
 * ========================================================================= */

/* CBD eta=2 sampling: share0 from random bytes, share1 = 0 */
void masked_poly_cbd2(masked_poly_t *r, const uint8_t *buf);

/* CBD eta=3 sampling: share0 from random bytes, share1 = 0 */
void masked_poly_cbd3(masked_poly_t *r, const uint8_t *buf);

/* =========================================================================
 * Mask / Unmask conversions
 * ========================================================================= */

/* Mask a plaintext polynomial into 2 arithmetic shares */
void mask_poly_kem(masked_poly_t *mp, const int16_t *plain);

/* Unmask: plain[i] = share0[i] + share1[i] mod Q */
void unmask_poly_kem(int16_t *plain, const masked_poly_t *mp);

/* Mask/unmask for ML-DSA (32-bit) */
void mask_poly_dsa(masked_poly32_t *mp, const int32_t *plain);
void unmask_poly_dsa(int32_t *plain, const masked_poly32_t *mp);

/* =========================================================================
 * A2B / B2A domain conversion (via OBI MMIO peripheral)
 * ========================================================================= */

/* Convert masked polynomial from arithmetic to boolean shares */
void masked_poly_a2b_kem(masked_poly_t *mp);

/* Convert masked polynomial from boolean to arithmetic shares */
void masked_poly_b2a_kem(masked_poly_t *mp);

/* =========================================================================
 * ML-DSA masked rounding (FIPS-204) -- A2B-based, OBI peripheral.
 *
 * Power2Round (used in keygen):
 *   r = t1 * 2^D + t0,  -2^(D-1) < t0 ≤ 2^(D-1),  D = 13 (ML-DSA-44/65/87).
 *   t1 is published as part of pk (PUBLIC).
 *   t0 is part of sk (MASKED).
 *
 * Decompose (used in sign for w1 and r0):
 *   r = r1 * α + r0,   α = 2γ2,  with the FIPS-204 special case at r1=Q-1.
 *   r1 (= w1) is hashed into c~ (PUBLIC).
 *   r0 stays MASKED.
 *
 * Both routines:
 *   - take an arithmetic-shared masked_poly32_t as input,
 *   - emit the public output as an int32_t array (one share, unmasked),
 *   - emit the secret output as a re-masked masked_poly32_t (fresh share split).
 * ========================================================================= */
void masked_poly_power2round_dsa(int32_t          t1_pub[MLDSA_N],
                                  masked_poly32_t *t0_masked,
                                  const masked_poly32_t *r_masked);

void masked_poly_decompose_dsa(int32_t          r1_pub[MLDSA_N],
                                masked_poly32_t *r0_masked,
                                const masked_poly32_t *r_masked,
                                int32_t          alpha);

/* Streaming masked-secret × public-cp pointwise multiply for ML-DSA-44 sign.
 *
 * Computes  r[i] = montgomery_reduce(secret[i] · cp[i])  for i=0..N-1,
 * with secret[i] freshly masked per-coefficient and the DOM cross-term
 * routed through SECMUL_LATCH_D + SECMUL_REUSE_D.
 *
 * Storage: 0 BSS, ~80 bytes stack per coefficient (locals only).  Trades
 * ~7 KB of buffer scratch (mask_poly + cp_trivial + prod) for per-coefficient
 * inline computation, letting masked ML-DSA-44 sign fit the 64 KB CW305 SRAM.
 *
 * Output is centred to (-Q/2, Q/2].  Caller (masked sign loop) feeds the
 * result into invNTT, which expects the canonical centred representative.
 */
void masked_poly_pointwise_sxp_streaming_dsa(int32_t       r_out[MLDSA_N],
                                              const int32_t cp_pub[MLDSA_N],
                                              const int32_t secret[MLDSA_N]);

/* Fully-masked Power2Round (no clear-r window).
 *
 * Uses a software ripple-carry masked adder built on AND_LATCH/REUSE to
 * compute (r + 4095) entirely in boolean shares; the high bits are then
 * unmasked as t1 (public per FIPS-204 Algorithm 35), and t0 is recovered
 * in arithmetic shares by per-share subtract of t1·2^D.  Closes the
 * single-cycle clear-r leak window of masked_poly_power2round_dsa.
 *
 * Cost per coefficient (host emulation):
 *   1 A2B + 24-bit masked ripple-carry (24 iterations × 1 AND_LATCH+REUSE)
 *   + 1 mask_refresh.  Approximately 6× the cost of the unmask-version.
 */
void masked_poly_power2round_dsa_boolean(int32_t          t1_pub[MLDSA_N],
                                          masked_poly32_t *t0_masked,
                                          const masked_poly32_t *r_masked);

#endif /* SCA_PQC_MASKED_H */
