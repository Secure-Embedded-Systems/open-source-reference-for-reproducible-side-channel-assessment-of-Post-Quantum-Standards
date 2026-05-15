/* Masked polynomial operations: mask/unmask, basemul, reduce, in-place arithmetic. */

#include "sca_pqc_masked.h"

/* =========================================================================
 * ML-KEM zetas for basemul (= pq-crystals zetas[64..127] in centred form).
 * Lifted directly from integration/firmware/mlkem1024_fips203/src/ntt.c so
 * the masked basemul matches the KAT-verified unmasked reference exactly.
 * ========================================================================= */
static const int16_t zetas_basemul_kem[64] = {
    -1103,   430,   555,   843, -1251,   871,  1550,   105,
      422,   587,   177,  -235,  -291,  -460,  1574,  1653,
     -246,   778,  1159,  -147,  -777,  1483,  -602,  1119,
    -1590,   644,  -872,   349,   418,   329,  -156,   -75,
      817,  1097,   603,   610,  1322, -1285, -1465,   384,
    -1215,  -136,  1218, -1335,  -874,   220, -1187, -1659,
    -1185, -1530, -1278,   794, -1510,  -854,  -870,   478,
     -108,  -308,   996,   991,   958, -1460,  1522,  1628
};

/* Montgomery multiply helper (ML-KEM) */
static inline int16_t fqmul_kem(int16_t a, int16_t b)
{
    int32_t result;
    int32_t op_a = (int32_t)a;
    int32_t op_b = (int32_t)b;
    SCA_MONT_K(result, op_a, op_b);
    return (int16_t)result;
}

/* =========================================================================
 * Internal: paired-DOM cross-term multiply (ML-KEM)
 *
 *   Computes (z0, z1) = (a0+a1) * (b0+b1) mod 3329 in shared form.
 *   Uses the LATCH/REUSE pair so the +r and -r cross-blinds cancel
 *   exactly on share recombination (KAT-correct, probing-secure d=1).
 * ========================================================================= */
static inline void dom_mul_kem(int16_t a0, int16_t a1,
                               int16_t b0, int16_t b1,
                               int16_t *z0, int16_t *z1)
{
    int32_t u00, u11, v01, v10;
    /* Open atomic DOM region: no interrupt or other PRNG-consuming op may
     * fire between LATCH and REUSE, otherwise the hardware trap fires. */
    SCA_DOM_REGION_BEGIN();
    /* Same-share inner products (no DOM blinding needed). */
    SCA_MONT_K(u00, (int32_t)a0, (int32_t)b0);
    SCA_MONT_K(u11, (int32_t)a1, (int32_t)b1);
    /* Cross-term pair: latches r, then consumes r with opposite sign. */
    SCA_SECMUL_LATCH_K(v01, (int32_t)a0, (int32_t)b1);
    SCA_SECMUL_REUSE_K(v10, (int32_t)a1, (int32_t)b0);
    SCA_DOM_REGION_END();
    *z0 = (int16_t)(u00 + v01);
    *z1 = (int16_t)(u11 + v10);
}

/* =========================================================================
 * Masked Pointwise Multiplication (ML-KEM)
 *
 * Simple coefficient-by-coefficient multiply using paired DOM.
 * For NTT-domain ML-KEM, use masked_poly_basemul_kem instead.
 * ========================================================================= */
void masked_poly_pointwise_mul_kem(masked_poly_t *r,
                                    const masked_poly_t *a,
                                    const masked_poly_t *b)
{
    for (unsigned int i = 0; i < MLKEM_N; i++) {
        int16_t z0, z1;
        dom_mul_kem(a->coeffs[0][i], a->coeffs[1][i],
                    b->coeffs[0][i], b->coeffs[1][i],
                    &z0, &z1);
        r->coeffs[0][i] = z0;
        r->coeffs[1][i] = z1;
    }

    /* Barrett reduce all coefficients to keep bounded */
    masked_poly_reduce_kem(r);
}

/* =========================================================================
 * Masked Basemul (ML-KEM NTT domain)
 *
 * In the NTT domain, ML-KEM's incomplete NTT leaves 128 degree-1
 * polynomial blocks. Each block multiply is:
 *
 *   (a0 + a1*X) * (b0 + b1*X) mod (X^2 - gamma)
 *     = (a0*b0 + a1*b1*gamma) + (a0*b1 + a1*b0)*X
 *
 * For masked version, each multiply a_si * b_sj is handled as:
 *   - Same share (si==sj): direct fqmul (no leakage of secret)
 *   - Cross share (si!=sj): SECMUL_STEP (DOM-blinded)
 * ========================================================================= */
void masked_poly_basemul_kem(masked_poly_t *r,
                              const masked_poly_t *a,
                              const masked_poly_t *b)
{
    /*
     * Each NTT-domain pair (a[2i], a[2i+1]) is a degree-1 polynomial mod
     * (X^2 - gamma_i) where gamma_i comes from zetas_basemul_kem with sign
     * alternation.  Block product:
     *   c[2i  ] = a[2i  ]*b[2i  ] + a[2i+1]*b[2i+1] * gamma_i
     *   c[2i+1] = a[2i  ]*b[2i+1] + a[2i+1]*b[2i  ]
     *
     * Each coefficient multiply on shared inputs becomes one paired-DOM
     * call (dom_mul_kem).  Multiplying a SHARED value by a public constant
     * gamma is linear, so it's a per-share fqmul.
     */
    for (unsigned int i = 0; i < MLKEM_N / 2; i++) {
        unsigned int idx = 2 * i;
        int16_t gamma = zetas_basemul_kem[i / 2];
        if (i & 1) gamma = -gamma;

        /* p0 = a[idx] * b[idx]   (DOM) */
        int16_t p0_s0, p0_s1;
        dom_mul_kem(a->coeffs[0][idx], a->coeffs[1][idx],
                    b->coeffs[0][idx], b->coeffs[1][idx],
                    &p0_s0, &p0_s1);

        /* p1 = a[idx+1] * b[idx+1]   (DOM) */
        int16_t p1_s0, p1_s1;
        dom_mul_kem(a->coeffs[0][idx+1], a->coeffs[1][idx+1],
                    b->coeffs[0][idx+1], b->coeffs[1][idx+1],
                    &p1_s0, &p1_s1);

        
        {
            int32_t _t;
            SCA_BARRETT_K(_t, (int32_t)p1_s0); p1_s0 = (int16_t)_t;
            SCA_BARRETT_K(_t, (int32_t)p1_s1); p1_s1 = (int16_t)_t;
        }

        /* p1 *= gamma  (public constant, per share) */
        p1_s0 = fqmul_kem(gamma, p1_s0);
        p1_s1 = fqmul_kem(gamma, p1_s1);

        /* c[idx] = p0 + p1 -- promote to int32 to avoid int16 transient
         * overflow when both summands are near +/-Q (post-Barrett bounds). */
        int32_t c0_s0 = (int32_t)p0_s0 + (int32_t)p1_s0;
        int32_t c0_s1 = (int32_t)p0_s1 + (int32_t)p1_s1;

        /* p2 = a[idx] * b[idx+1]   (DOM) */
        int16_t p2_s0, p2_s1;
        dom_mul_kem(a->coeffs[0][idx],   a->coeffs[1][idx],
                    b->coeffs[0][idx+1], b->coeffs[1][idx+1],
                    &p2_s0, &p2_s1);

        /* p3 = a[idx+1] * b[idx]   (DOM) */
        int16_t p3_s0, p3_s1;
        dom_mul_kem(a->coeffs[0][idx+1], a->coeffs[1][idx+1],
                    b->coeffs[0][idx],   b->coeffs[1][idx],
                    &p3_s0, &p3_s1);

        /* c[idx+1] = p2 + p3 (same int32 promotion). */
        int32_t c1_s0 = (int32_t)p2_s0 + (int32_t)p3_s0;
        int32_t c1_s1 = (int32_t)p2_s1 + (int32_t)p3_s1;

        /* Barrett-reduce each share before storing back so values stay in
         * (-Q, Q) and the int16_t store cannot truncate. */
        int32_t tmp;
        SCA_BARRETT_K(tmp, c0_s0); r->coeffs[0][idx]   = (int16_t)tmp;
        SCA_BARRETT_K(tmp, c0_s1); r->coeffs[1][idx]   = (int16_t)tmp;
        SCA_BARRETT_K(tmp, c1_s0); r->coeffs[0][idx+1] = (int16_t)tmp;
        SCA_BARRETT_K(tmp, c1_s1); r->coeffs[1][idx+1] = (int16_t)tmp;
    }

    /* Final reduce is now redundant after per-coefficient Barrett, but
     * keep it for defence-in-depth and to match the public ABI of the
     * legacy code path. */
    masked_poly_reduce_kem(r);
}

/* =========================================================================
 * Linear operations -- per share, no masking overhead
 * ========================================================================= */

void masked_poly_add_kem(masked_poly_t *r,
                          const masked_poly_t *a,
                          const masked_poly_t *b)
{
    for (unsigned int i = 0; i < MLKEM_N; i++) {
        r->coeffs[0][i] = a->coeffs[0][i] + b->coeffs[0][i];
        r->coeffs[1][i] = a->coeffs[1][i] + b->coeffs[1][i];
    }
}

void masked_poly_sub_kem(masked_poly_t *r,
                          const masked_poly_t *a,
                          const masked_poly_t *b)
{
    for (unsigned int i = 0; i < MLKEM_N; i++) {
        r->coeffs[0][i] = a->coeffs[0][i] - b->coeffs[0][i];
        r->coeffs[1][i] = a->coeffs[1][i] - b->coeffs[1][i];
    }
}

/* Barrett reduction on all coefficients */
void masked_poly_reduce_kem(masked_poly_t *p)
{
    for (unsigned int i = 0; i < MLKEM_N; i++) {
        int32_t tmp;
        int32_t s0 = (int32_t)p->coeffs[0][i];
        int32_t s1 = (int32_t)p->coeffs[1][i];
        SCA_BARRETT_K(tmp, s0);
        p->coeffs[0][i] = (int16_t)tmp;
        SCA_BARRETT_K(tmp, s1);
        p->coeffs[1][i] = (int16_t)tmp;
    }
}

/* =========================================================================
 * ML-DSA polynomial operations (32-bit, Q=8380417)
 * ========================================================================= */

void masked_poly_pointwise_mul_dsa(masked_poly32_t *r,
                                    const masked_poly32_t *a,
                                    const masked_poly32_t *b)
{
    for (unsigned int i = 0; i < MLDSA_N; i++) {
        int32_t u00, u11, v01, v10;

        SCA_DOM_REGION_BEGIN();
        /* Same-share inner products: SCA_MONT_D_MUL = software int64 multiply
         * + ISE Montgomery reduction.  Matches pq-crystals fqmul exactly. */
        SCA_MONT_D_MUL(u00, a->coeffs[0][i], b->coeffs[0][i]);
        SCA_MONT_D_MUL(u11, a->coeffs[1][i], b->coeffs[1][i]);
        /* Paired DOM cross-terms: SECMUL_LATCH/REUSE_D take FACTORS, do
         * the multiply internally, blind with +r / -r_latched, and apply
         * Montgomery reduction so the output is at the same Mont-level as
         * MONT_D_MUL.  Sum of u00+v01+u11+v10 = (a*b)/R mod Q. */
        SCA_SECMUL_LATCH_D(v01, a->coeffs[0][i], b->coeffs[1][i]);
        SCA_SECMUL_REUSE_D(v10, a->coeffs[1][i], b->coeffs[0][i]);
        SCA_DOM_REGION_END();

        r->coeffs[0][i] = u00 + v01;
        r->coeffs[1][i] = u11 + v10;
    }

    masked_poly_reduce_dsa(r);
}

void masked_poly_add_dsa(masked_poly32_t *r,
                          const masked_poly32_t *a,
                          const masked_poly32_t *b)
{
    for (unsigned int i = 0; i < MLDSA_N; i++) {
        r->coeffs[0][i] = a->coeffs[0][i] + b->coeffs[0][i];
        r->coeffs[1][i] = a->coeffs[1][i] + b->coeffs[1][i];
    }
}

void masked_poly_sub_dsa(masked_poly32_t *r,
                          const masked_poly32_t *a,
                          const masked_poly32_t *b)
{
    for (unsigned int i = 0; i < MLDSA_N; i++) {
        r->coeffs[0][i] = a->coeffs[0][i] - b->coeffs[0][i];
        r->coeffs[1][i] = a->coeffs[1][i] - b->coeffs[1][i];
    }
}

void masked_poly_reduce_dsa(masked_poly32_t *p)
{
    for (unsigned int i = 0; i < MLDSA_N; i++) {
        int32_t tmp;
        SCA_BARRETT_D(tmp, p->coeffs[0][i]);
        p->coeffs[0][i] = tmp;
        SCA_BARRETT_D(tmp, p->coeffs[1][i]);
        p->coeffs[1][i] = tmp;
    }
}

/* =========================================================================
 * CBD Sampling
 * ========================================================================= */

/*
 * CBD eta=2: extracts 8 coefficients from a 32-bit word.
 * Each coefficient is (popcount(a) - popcount(b)) where a,b are 2-bit fields.
 * Output range: [-2, 2].
 *
 * Sampling produces unmasked share (share0 = CBD output, share1 = 0).
 * Caller should mask_refresh after sampling if needed.
 */
void masked_poly_cbd2(masked_poly_t *r, const uint8_t *buf)
{
    for (unsigned int i = 0; i < MLKEM_N; i += 8) {
        uint32_t word = *(const uint32_t *)(buf + i / 2);
        int32_t coeff;

        SCA_CBD2_1(coeff, word); r->coeffs[0][i+0] = (int16_t)coeff;
        SCA_CBD2_2(coeff, word); r->coeffs[0][i+1] = (int16_t)coeff;
        SCA_CBD2_3(coeff, word); r->coeffs[0][i+2] = (int16_t)coeff;
        SCA_CBD2_4(coeff, word); r->coeffs[0][i+3] = (int16_t)coeff;
        SCA_CBD2_5(coeff, word); r->coeffs[0][i+4] = (int16_t)coeff;
        SCA_CBD2_6(coeff, word); r->coeffs[0][i+5] = (int16_t)coeff;
        SCA_CBD2_7(coeff, word); r->coeffs[0][i+6] = (int16_t)coeff;
        SCA_CBD2_8(coeff, word); r->coeffs[0][i+7] = (int16_t)coeff;

        /* Second share = 0 (unmasked at this point) */
        r->coeffs[1][i+0] = 0; r->coeffs[1][i+1] = 0;
        r->coeffs[1][i+2] = 0; r->coeffs[1][i+3] = 0;
        r->coeffs[1][i+4] = 0; r->coeffs[1][i+5] = 0;
        r->coeffs[1][i+6] = 0; r->coeffs[1][i+7] = 0;
    }
}

/*
 * CBD eta=3: extracts 4 coefficients from a 24-bit field.
 * Each coefficient is (popcount(a) - popcount(b)) where a,b are 3-bit fields.
 * Output range: [-3, 3]. Used by ML-KEM-512.
 */
void masked_poly_cbd3(masked_poly_t *r, const uint8_t *buf)
{
    for (unsigned int i = 0; i < MLKEM_N; i += 4) {
        /* CBD3 uses 3 bytes = 24 bits per 4 coefficients */
        uint32_t word = (uint32_t)buf[3*(i/4)]
                      | ((uint32_t)buf[3*(i/4)+1] << 8)
                      | ((uint32_t)buf[3*(i/4)+2] << 16);
        int32_t coeff;

        SCA_CBD3_1(coeff, word); r->coeffs[0][i+0] = (int16_t)coeff;
        SCA_CBD3_2(coeff, word); r->coeffs[0][i+1] = (int16_t)coeff;
        SCA_CBD3_3(coeff, word); r->coeffs[0][i+2] = (int16_t)coeff;
        SCA_CBD3_4(coeff, word); r->coeffs[0][i+3] = (int16_t)coeff;

        r->coeffs[1][i+0] = 0; r->coeffs[1][i+1] = 0;
        r->coeffs[1][i+2] = 0; r->coeffs[1][i+3] = 0;
    }
}

/* =========================================================================
 * Mask / Unmask Conversions
 * ========================================================================= */

void mask_poly_kem(masked_poly_t *mp, const int16_t *plain)
{
    for (unsigned int i = 0; i < MLKEM_N; i++) {
        uint32_t r;
        int16_t  mask;
        do {
            SCA_GET_RANDOM(r);
            uint32_t cand = r & 0x0FFFu;
            if (cand < (uint32_t)MLKEM_Q) { mask = (int16_t)cand; break; }
            cand = (r >> 12) & 0x0FFFu;
            if (cand < (uint32_t)MLKEM_Q) { mask = (int16_t)cand; break; }
        } while (1);

        int32_t s0 = (int32_t)plain[i] + (int32_t)mask;
        int32_t tmp;
        SCA_BARRETT_K(tmp, s0);
        mp->coeffs[0][i] = (int16_t)tmp;
        mp->coeffs[1][i] = (int16_t)(-mask);
    }
}

/*
 * Unmask: reconstruct plain[i] = share0[i] + share1[i] mod Q.
 * Result is in [0, Q-1].
 */
void unmask_poly_kem(int16_t *plain, const masked_poly_t *mp)
{
    for (unsigned int i = 0; i < MLKEM_N; i++) {
        int32_t sum = (int32_t)mp->coeffs[0][i] + (int32_t)mp->coeffs[1][i];
        int32_t tmp;
        SCA_BARRETT_K(tmp, sum);
        SCA_CADDQ(tmp, tmp);
        plain[i] = (int16_t)tmp;
    }
}

/* Mask/unmask for ML-DSA (32-bit coefficients).
 * Rejection-sampled into Z_Q (Q=8380417 ≈ 2^23, so the 23-bit slot is reused
 * with 0.999... acceptance rate -- essentially never rejects, but the test
 * is kept so the distribution is exactly uniform). */
void mask_poly_dsa(masked_poly32_t *mp, const int32_t *plain)
{
    for (unsigned int i = 0; i < MLDSA_N; i++) {
        uint32_t r;
        int32_t  mask;
        do {
            SCA_GET_RANDOM(r);
            uint32_t cand = r & 0x007FFFFFu;             /* 23-bit field */
            if (cand < (uint32_t)MLDSA_Q) { mask = (int32_t)cand; break; }
        } while (1);

        int32_t s0 = plain[i] + mask;
        int32_t tmp;
        SCA_BARRETT_D(tmp, s0);
        mp->coeffs[0][i] = tmp;
        mp->coeffs[1][i] = -mask;
    }
}

void unmask_poly_dsa(int32_t *plain, const masked_poly32_t *mp)
{
    for (unsigned int i = 0; i < MLDSA_N; i++) {
        int32_t sum = mp->coeffs[0][i] + mp->coeffs[1][i];
        int32_t tmp;
        SCA_BARRETT_D(tmp, sum);
        /* Conditional add Q for DSA: ensure positive */
        if (tmp < 0)
            tmp += MLDSA_Q;
        plain[i] = tmp;
    }
}

/* =========================================================================
 * A2B / B2A Domain Conversion (via OBI MMIO peripheral)
 *
 * Used for operations that cross arithmetic/boolean domains:
 *   - Compression (needs comparison, which is boolean)
 *   - Message decoding
 *   - Ciphertext comparison in FO transform
 * ========================================================================= */

void masked_poly_a2b_kem(masked_poly_t *mp)
{
    for (unsigned int i = 0; i < MLKEM_N; i++) {
        uint32_t r;
        SCA_GET_RANDOM(r);

        uint32_t bool_s0, bool_s1;
        a2b_convert((uint32_t)(uint16_t)mp->coeffs[0][i],
                    (uint32_t)(uint16_t)mp->coeffs[1][i],
                    r, 0 /* 12-bit mode for ML-KEM */,
                    &bool_s0, &bool_s1);

        mp->coeffs[0][i] = (int16_t)bool_s0;
        mp->coeffs[1][i] = (int16_t)bool_s1;
    }
}

void masked_poly_b2a_kem(masked_poly_t *mp)
{
    for (unsigned int i = 0; i < MLKEM_N; i++) {
        uint32_t r;
        SCA_GET_RANDOM(r);

        uint32_t arith_s0, arith_s1;
        b2a_convert((uint32_t)(uint16_t)mp->coeffs[0][i],
                    (uint32_t)(uint16_t)mp->coeffs[1][i],
                    r, 0 /* 12-bit mode */,
                    &arith_s0, &arith_s1);

        mp->coeffs[0][i] = (int16_t)arith_s0;
        mp->coeffs[1][i] = (int16_t)arith_s1;
    }
}

/* Constant-time conditional add of Q for ML-DSA shares.  The X2X A2B core
 * expects each share in [0, Q); our masked NTT pipeline produces shares in
 * (-Q, Q) (Barrett-centred).  Per-share CADDQ brings each share into the
 * required range without altering the sum-invariant mod Q.  The branch is
 * on a single share's sign, which is uniformly distributed (50/50 by design
 * of mask_poly_dsa), so this leaks no information about the secret. */
static inline uint32_t caddq_dsa(int32_t x)
{
    int32_t m = x >> 31;             /* -1 if x < 0, else 0 */
    return (uint32_t)(x + (m & (int32_t)MLDSA_Q));
}

/* =========================================================================
 * Software Montgomery reduction for ML-DSA, mirroring pq-crystals exactly.
 * Used by the streaming masked DOM below in place of SCA_MONT_D_MUL when
 * the silicon MONT_D ISE opcode is unavailable / unverified.  Output range
 * matches the C reference: r ≡ a · 2^{-32} mod Q with -Q < r < Q. */
static inline int32_t sw_montgomery_reduce_dsa(int64_t a)
{
    const int32_t QINV = 58728449;          /* Q^{-1} mod 2^32  (vlspqc) */
    int32_t t = (int32_t)((int32_t)a * QINV);
    return (int32_t)((a - (int64_t)t * MLDSA_Q) >> 32);
}

/* =========================================================================
 * Streaming masked-secret × public-cp pointwise multiply (ML-DSA-44).
 *
 * Per-coefficient inline DOM (no buffer scratch):
 *   1. Rejection-sample fresh mask m_i ∈ Z_Q.
 *   2. s0 = (secret[i] + m_i) mod Q  (Barrett, centred).  s1 = -m_i.
 *   3. cp is public, trivially shared as (cp[i], 0).
 *   4. DOM-paired Mont-D multiply:
 *        u00 = MONT_D_MUL (s0,  cp[i])    -- (s0*cp)/R
 *        u11 = 0                          -- s1·0 = 0 trivially
 *        v01 = SECMUL_LATCH_D(s0, 0) → blinded with fresh r_blind
 *        v10 = SECMUL_REUSE_D(s1, cp[i]) → (s1*cp - r_blind)/R
 *      share0 = u00 + v01;  share1 = v10
 *      sum    = (s*cp)/R    -- standard Mont-multiply output
 *   5. Recentre and store.
 *
 * Stack: ~80 bytes per call iteration; freed after each coefficient.
 * BSS:   0 bytes.
 *
 * The sign loop calls this 4× (one per ML-DSA-44 polyvecl element),
 * three times per signing attempt (cp·s1, cp·s2, cp·t0).
 * ========================================================================= */
void masked_poly_pointwise_sxp_streaming_dsa(int32_t       r_out[MLDSA_N],
                                              const int32_t cp_pub[MLDSA_N],
                                              const int32_t secret[MLDSA_N])
{
    /* Software-Montgomery streaming DOM gadget for ML-DSA-44.  The
     * Gross-Mangard +r/-r masking is preserved (per-coefficient fresh
     * random from the ISE PRNG), but the Montgomery reductions are done
     * in software, NOT through SCA_MONT_D_MUL or SCA_SECMUL_*_D.  This
     * avoids the silicon MONT_D opcode failure mode observed under
     * heavy call volume in sign.  ISE in use: BARRETT_D (for centred
     * reductions) and GET_RANDOM (fresh blinding random per coeff).
     *
     * KAT correctness preserved because Mont reduction is linear modulo R:
     *     mont(s0·c) + mont(r) + mont(s1·c − r) = mont((s0+s1)·c) = mont(s·c).
     *
     * Each call: 1 GET_RANDOM (rejection-sampled) + 2 BARRETT_D + 3 software
     * Mont reductions + 4 int64 multiplications.  ~120 cycles per coeff at
     * 20 MHz => masked DSA-44 sign ≈ 250-400 ms wall-clock.
     */
    for (int i = 0; i < MLDSA_N; i++) {
        /* Step 1: rejection-sample fresh blinding random r ∈ Z_Q. */
        int32_t r_blind;
        do {
            uint32_t rr;
            SCA_GET_RANDOM(rr);
            uint32_t cand = rr & 0x007FFFFFu;
            if (cand < (uint32_t)MLDSA_Q) { r_blind = (int32_t)cand; break; }
        } while (1);

        /* Step 2: rejection-sample fresh per-coefficient mask m ∈ Z_Q. */
        int32_t m;
        do {
            uint32_t rr;
            SCA_GET_RANDOM(rr);
            uint32_t cand = rr & 0x007FFFFFu;
            if (cand < (uint32_t)MLDSA_Q) { m = (int32_t)cand; break; }
        } while (1);

        /* Step 3: arithmetic shares of secret[i].  s0 = barrett(secret + m),
         * s1 = -m.  share-sum invariant: s0 + s1 ≡ secret (mod Q). */
        int32_t s_plus = secret[i] + m;
        int32_t s0;
        SCA_BARRETT_D(s0, s_plus);
        int32_t cmask0 = (MLDSA_Q/2 - s0) >> 31;     /* re-centre */
        s0 = s0 - (cmask0 & MLDSA_Q);
        int32_t s1 = -m;

        int32_t c0 = cp_pub[i];     /* public; trivially shared as (c0, 0) */

        /* Step 4: Gross-Mangard DOM cross-term gadget in software.
         *   u00 = mont(s0 · c0)
         *   v01 = mont(0 + r_blind)         (s0 · c1 = s0 · 0 = 0; +r blinding)
         *   v10 = mont(s1 · c0 − r_blind)   (s1 · c0 with -r cancellation)
         * Sum invariant: u00 + v01 + v10 = mont(s · c) = (s · c)/R mod Q. */
        int64_t p00 = (int64_t)s0 * c0;
        int32_t u00 = sw_montgomery_reduce_dsa(p00);

        int64_t p01 = (int64_t)r_blind;          /* s0·c1=0, plus +r */
        int32_t v01 = sw_montgomery_reduce_dsa(p01);

        int64_t p10 = (int64_t)s1 * c0 - r_blind; /* s1·c0 minus the same r */
        int32_t v10 = sw_montgomery_reduce_dsa(p10);

        /* Step 5: recombine and centre. */
        int32_t share0 = u00 + v01;
        int32_t share1 = v10;
        int32_t sum    = share0 + share1;
        int32_t reduced;
        SCA_BARRETT_D(reduced, sum);
        int32_t cmask1 = (MLDSA_Q/2 - reduced) >> 31;
        r_out[i] = reduced - (cmask1 & MLDSA_Q);
    }
}

void masked_poly_power2round_dsa(int32_t          t1_pub[MLDSA_N],
                                  masked_poly32_t *t0_masked,
                                  const masked_poly32_t *r_masked)
{
    const int32_t D = 13;
    const int32_t TWO_D = 1 << D;        /* 8192 */
    const int32_t HALF_D_M1 = (1 << (D - 1)) - 1;  /* 4095 */

    for (unsigned int i = 0; i < MLDSA_N; i++) {
        /* Step 1: per-share CADDQ to bring shares into [0, Q) (X2X core
         * input convention), then A2B convert as a "secure unmask" path. */
        uint32_t s0 = caddq_dsa(r_masked->coeffs[0][i]);
        uint32_t s1 = caddq_dsa(r_masked->coeffs[1][i]);
        uint32_t fresh;
        SCA_GET_RANDOM(fresh);
        uint32_t b0, b1;
        a2b_convert(s0, s1, fresh, /*wide=*/1, &b0, &b1);

        /* Step 2: reconstruct r in clear (one-cycle leak window). */
        int32_t r = (int32_t)(b0 ^ b1);

        /* Step 3: pq-crystals Power2Round in the clear. */
        int32_t t1 = (r + HALF_D_M1) >> D;
        int32_t t0 = r - (t1 << D);
        t1_pub[i] = t1;

        /* Step 4: re-mask t0 over Z_Q with fresh randomness. */
        uint32_t rr;
        int32_t  refresh_mask;
        do {
            SCA_GET_RANDOM(rr);
            uint32_t cand = rr & 0x007FFFFFu;
            if (cand < (uint32_t)MLDSA_Q) { refresh_mask = (int32_t)cand; break; }
        } while (1);
        t0_masked->coeffs[0][i] = t0 + refresh_mask;
        t0_masked->coeffs[1][i] = -refresh_mask;

        (void)TWO_D;  /* used in design comment, retained for SNI-future stage */
    }
}

/* =========================================================================
 * Software masked ripple-carry adder of a public constant.
 *
 *   sum = (a + k) mod 2^WIDTH   where  a is boolean-shared (a0, a1).
 *
 * Public k means a AND k and a XOR k are per-share linear; the only
 * cross-share AND in the carry chain is (a XOR k) AND cy_in, handled by
 * the paired DOM gadget AND_LATCH/REUSE.  This is the W=24 building block
 * used by masked_poly_power2round_dsa_boolean.
 *
 * Cost: WIDTH iterations × 1 AND_LATCH + 1 AND_REUSE + 4 trivial ANDs per
 * iteration.  For WIDTH=24 (ML-DSA): 48 ISE-LATCH/REUSE ops per coefficient.
 * The loop body is straight-line; no data-dependent branches.
 * ========================================================================= */
static void masked_bool_add_const_w(uint32_t a0, uint32_t a1, uint32_t k,
                                     int width,
                                     uint32_t *s0_out, uint32_t *s1_out)
{
    uint32_t cy0 = 0, cy1 = 0;
    uint32_t s0 = 0, s1 = 0;
    for (int i = 0; i < width; i++) {
        uint32_t a0_i = (a0 >> i) & 1u;
        uint32_t a1_i = (a1 >> i) & 1u;
        uint32_t k_i  = (k  >> i) & 1u;

        /* sum bit = a0 ^ a1 ^ k ^ cy0 ^ cy1, split per share. */
        s0 |= ((a0_i ^ k_i ^ cy0) & 1u) << i;
        s1 |= ((a1_i ^ cy1) & 1u) << i;

        /* New carry: cy' = (a AND k) XOR (cy AND (a XOR k)).
         *   a AND k       : per-share linear (k public)
         *   a XOR k       : per-share linear
         *   cy AND (a^k)  : DOM-AND  (cross-share)               */
        uint32_t aandk0 = a0_i & k_i;
        uint32_t aandk1 = a1_i & k_i;
        uint32_t axk0   = a0_i ^ k_i;
        uint32_t axk1   = a1_i;

        /* Inner products (same-share AND): per-share linear. */
        uint32_t v00 = axk0 & cy0;
        uint32_t v11 = axk1 & cy1;

        /* Cross-share ANDs via paired DOM gadget. */
        uint32_t v01_blind, v10_blind;
        SCA_DOM_REGION_BEGIN();
        SCA_AND_LATCH(v01_blind, axk0, cy1);   /* (axk0 & cy1) ^ r */
        SCA_AND_REUSE(v10_blind, axk1, cy0);   /* (axk1 & cy0) ^ r */
        SCA_DOM_REGION_END();

        uint32_t and_s0 = (v00 ^ (v01_blind & 1u)) & 1u;
        uint32_t and_s1 = (v11 ^ (v10_blind & 1u)) & 1u;

        cy0 = (aandk0 ^ and_s0) & 1u;
        cy1 = (aandk1 ^ and_s1) & 1u;
    }
    *s0_out = s0;
    *s1_out = s1;
}

/* =========================================================================
 * Fully-masked Power2Round (no clear-r window).
 *
 *   t1 = (r + 4095) >> D     (D = 13)
 *   t0 = r - t1 · 2^D
 *
 * 1. Per-share CADDQ (shares -> [0, Q)).
 * 2. A2B(r) -> 24-bit boolean shares (b0, b1) such that b0 ^ b1 = r.
 * 3. masked_bool_add_const_w(b0, b1, 4095, 24) -> (s0, s1) boolean
 *    shares of (r + 4095) in 24-bit space.  No mod-Q reduction; the
 *    carry chain handles overflow naturally.
 * 4. Per-share right-shift by D: (s0 >> 13, s1 >> 13) are boolean shares
 *    of t1.  XOR-fold to publish t1 (PUBLIC per FIPS-204).
 * 5. Recover t0 in arithmetic shares: per-share subtract of t1·2^D from
 *    the original arithmetic share-0 of r.  Sum invariant preserved.
 * 6. Refresh t0 with fresh randomness.
 *
 * The architectural-secret r is NEVER reconstructed in a single register.
 * The only public output is t1, which is a function of r intentionally
 * published in pk per FIPS-204.
 * ========================================================================= */
void masked_poly_power2round_dsa_boolean(int32_t          t1_pub[MLDSA_N],
                                          masked_poly32_t *t0_masked,
                                          const masked_poly32_t *r_masked)
{
    const int32_t D = 13;
    const int32_t TWO_D = 1 << D;
    const uint32_t HALF_D_M1 = (1u << (D - 1)) - 1u;  /* 4095 */

    for (unsigned int i = 0; i < MLDSA_N; i++) {
        uint32_t s_a0 = caddq_dsa(r_masked->coeffs[0][i]);
        uint32_t s_a1 = caddq_dsa(r_masked->coeffs[1][i]);

        /* Step 2: A2B -> boolean shares of r in [0, Q). */
        uint32_t fresh;
        SCA_GET_RANDOM(fresh);
        uint32_t b0, b1;
        a2b_convert(s_a0, s_a1, fresh, /*wide=*/1, &b0, &b1);

        /* Step 3: boolean masked add of HALF_D_M1 over 24 bits. */
        uint32_t s0, s1;
        masked_bool_add_const_w(b0, b1, HALF_D_M1, /*width=*/24, &s0, &s1);

        /* Step 4: per-share right-shift by D, then unmask t1 (PUBLIC). */
        int32_t t1 = (int32_t)((s0 >> D) ^ (s1 >> D));
        t1_pub[i] = t1;

        /* Step 5: t0 = r - t1·2^D in arithmetic shares (per-share). */
        int32_t t0_s0 = r_masked->coeffs[0][i] - t1 * TWO_D;
        int32_t t0_s1 = r_masked->coeffs[1][i];

        /* Step 6: fresh refresh over Z_Q. */
        uint32_t rr;
        int32_t  refresh_mask;
        do {
            SCA_GET_RANDOM(rr);
            uint32_t cand = rr & 0x007FFFFFu;
            if (cand < (uint32_t)MLDSA_Q) { refresh_mask = (int32_t)cand; break; }
        } while (1);
        t0_masked->coeffs[0][i] = t0_s0 + refresh_mask;
        t0_masked->coeffs[1][i] = t0_s1 - refresh_mask;
    }
}

void masked_poly_decompose_dsa(int32_t          r1_pub[MLDSA_N],
                                masked_poly32_t *r0_masked,
                                const masked_poly32_t *r_masked,
                                int32_t          alpha)
{
    const int32_t half_alpha = alpha >> 1;

    for (unsigned int i = 0; i < MLDSA_N; i++) {
        /* Per-share CADDQ (shares in [0, Q) for X2X core), then A2B as a
         * "secure unmask" path. */
        uint32_t s0 = caddq_dsa(r_masked->coeffs[0][i]);
        uint32_t s1 = caddq_dsa(r_masked->coeffs[1][i]);
        uint32_t fresh;
        SCA_GET_RANDOM(fresh);
        uint32_t b0, b1;
        a2b_convert(s0, s1, fresh, /*wide=*/1, &b0, &b1);
        int32_t r = (int32_t)(b0 ^ b1);    /* <-- one-cycle clear-r leakage window */

        /* FIPS-204 Algorithm 36 in the clear. */
        int32_t r0;
        r0  = r % alpha;
        if (r0 > half_alpha)  r0 -= alpha;
        int32_t r1;
        if ((r - r0) == (MLDSA_Q - 1)) {
            r1 = 0;
            r0 -= 1;
        } else {
            r1 = (r - r0) / alpha;
        }
        r1_pub[i] = r1;

        /* Re-mask r0 with fresh randomness over Z_Q. */
        uint32_t rr;
        int32_t  refresh_mask;
        do {
            SCA_GET_RANDOM(rr);
            uint32_t cand = rr & 0x007FFFFFu;
            if (cand < (uint32_t)MLDSA_Q) { refresh_mask = (int32_t)cand; break; }
        } while (1);
        r0_masked->coeffs[0][i] = r0 + refresh_mask;
        r0_masked->coeffs[1][i] = -refresh_mask;
    }
}

/* =========================================================================
 * System Initialization
 *
 * Seeds both the ISE-internal PRNG (via SEED_PRNG instruction) and the
 * OBI A2B/B2A peripheral PRNG (via TRNG MMIO) with true random entropy.
 * Must be called once before any masked operations.
 * ========================================================================= */
void sca_pqc_init(void)
{
    uint32_t seed, dummy;

    /* Enable TRNG with von Neumann post-processing */
    trng_enable(TRNG_CTRL_PP_VN);

    /* Wait for TRNG health tests to pass (may take ~10K cycles for BIST) */
    while (trng_health_status() != 0)
        ;

    /* Seed ISE PRNG with 4 words from TRNG.
     * SEED_PRNG(rd, rs1, rs2): rs1 = seed data, rs2 = word index.
     * rs2 must be a register, so we use a variable. */
    for (uint32_t idx = 0; idx < 4; idx++) {
        seed = trng_read();
        SCA_SEED_PRNG(dummy, seed, idx);
    }
    (void)dummy;

    /* Seed OBI A2B/B2A peripheral PRNG via TRNG */
    trng_seed_prng();
}
