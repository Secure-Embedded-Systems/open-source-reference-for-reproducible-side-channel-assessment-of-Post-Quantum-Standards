/* Masked forward NTT and inverse NTT with per-layer share refresh. */

#include "sca_pqc_masked.h"

/* =========================================================================
 * ML-KEM zetas in Montgomery domain, signed-centred (FIPS-203 / pq-crystals).
 *
 * Identical to integration/firmware/mlkem1024_fips203/src/ntt.c so the
 * masked NTT and the KAT-verified reference NTT compute exactly the same
 * butterflies (only the masking layer differs).  Loop indexing follows
 * pq-crystals: forward NTT reads k=1..127, inverse reads k=127..1.
 * zetas_kem[0] = -1044 is the "trivial" tmp[tree[0]] = mont(1); never used
 * by the loop, kept here only so the array index matches the reference.
 * ========================================================================= */
static const int16_t zetas_kem[128] = {
    -1044,  -758,  -359, -1517,  1493,  1422,   287,   202,
     -171,   622,  1577,   182,   962, -1202, -1474,  1468,
      573, -1325,   264,   383,  -829,  1458, -1602,  -130,
     -681,  1017,   732,   608, -1542,   411,  -205, -1571,
     1223,   652,  -552,  1015, -1293,  1491,  -282, -1544,
      516,    -8,  -320,  -666, -1618, -1162,   126,  1469,
     -853,   -90,  -271,   830,   107, -1421,  -247,  -951,
     -398,   961, -1508,  -725,   448, -1065,   677, -1275,
    -1103,   430,   555,   843, -1251,   871,  1550,   105,
      422,   587,   177,  -235,  -291,  -460,  1574,  1653,
     -246,   778,  1159,  -147,  -777,  1483,  -602,  1119,
    -1590,   644,  -872,   349,   418,   329,  -156,   -75,
      817,  1097,   603,   610,  1322, -1285, -1465,   384,
    -1215,  -136,  1218, -1335,  -874,   220, -1187, -1659,
    -1185, -1530, -1278,   794, -1510,  -854,  -870,   478,
     -108,  -308,   996,   991,   958, -1460,  1522,  1628
};

/* =========================================================================
 * Internal: Montgomery multiplication via ISE
 *
 * MONT_K takes two register operands: rd = montmul(rs1, rs2)
 * For twiddle multiply: rs1 = coefficient, rs2 = zeta
 * ========================================================================= */
static inline int16_t fqmul_kem(int16_t a, int16_t b)
{
    int32_t result;
    int32_t op_a = (int32_t)a;
    int32_t op_b = (int32_t)b;
    SCA_MONT_K(result, op_a, op_b);
    return (int16_t)result;
}

/* =========================================================================
 * Internal: Mask refresh on an entire polynomial
 *
 * For each coefficient, ISE MASK_REFRESH_A generates a fresh random r,
 * returns (share_in + r). Software subtracts r from the other share.
 *
 * ISE MASK_REFRESH_A: rd = rs1 + PRNG_random (arithmetic refresh)
 * The random value used is also added to an internal accumulator that
 * the software reads back via GET_RANDOM to adjust the complementary share.
 *
 * Simplified approach: use GET_RANDOM for fresh randomness, apply
 * arithmetic refresh manually. This avoids depending on internal
 * accumulator state.
 * ========================================================================= */
static void mask_refresh_kem(masked_poly_t *p)
{
    
    for (unsigned int i = 0; i < MLKEM_N; i++) {
        uint32_t r;
        int16_t  mask;
        do {
            SCA_GET_RANDOM(r);
            uint32_t cand = r & 0x0FFFu;             /* 12-bit field */
            if (cand < (uint32_t)MLKEM_Q) { mask = (int16_t)cand; break; }
            cand = (r >> 12) & 0x0FFFu;              /* second slot in the same word */
            if (cand < (uint32_t)MLKEM_Q) { mask = (int16_t)cand; break; }
        } while (1);

        /* Arithmetic refresh: share0 += mask, share1 -= mask  (sum invariant) */
        int32_t s0 = (int32_t)p->coeffs[0][i] + (int32_t)mask;
        int32_t s1 = (int32_t)p->coeffs[1][i] - (int32_t)mask;

        int32_t tmp;
        SCA_BARRETT_K(tmp, s0);
        p->coeffs[0][i] = (int16_t)tmp;
        SCA_BARRETT_K(tmp, s1);
        p->coeffs[1][i] = (int16_t)tmp;
    }
}

/* =========================================================================
 * Masked Forward NTT (ML-KEM)
 *
 * 7-layer Cooley-Tukey NTT (incomplete: stops at len=2).
 * Each share is transformed independently since all operations are linear
 * in the secret data. Mask refresh after each layer for SNI security.
 *
 * After NTT, polynomial is in NTT domain with 128 degree-1 blocks.
 * ========================================================================= */
void masked_ntt_kem(masked_poly_t *p)
{
    unsigned int len, start, j, k;
    int16_t zeta;

    k = 1;
    for (len = 128; len >= 2; len >>= 1) {
        for (start = 0; start < MLKEM_N; start = j + len) {
            zeta = zetas_kem[k++];
            for (j = start; j < start + len; j++) {
                int16_t t_s0, t_s1;

                /* Share 0: t = zeta * p[j+len] mod Q (Montgomery) */
                t_s0 = fqmul_kem(zeta, p->coeffs[0][j + len]);

                /* Share 1: t = zeta * p[j+len] mod Q (Montgomery) */
                t_s1 = fqmul_kem(zeta, p->coeffs[1][j + len]);

                /* Cooley-Tukey butterfly (linear, per share) */
                p->coeffs[0][j + len] = p->coeffs[0][j] - t_s0;
                p->coeffs[0][j]       = p->coeffs[0][j] + t_s0;

                p->coeffs[1][j + len] = p->coeffs[1][j] - t_s1;
                p->coeffs[1][j]       = p->coeffs[1][j] + t_s1;
            }
        }

        /* Mask refresh after each NTT layer to prevent leakage accumulation.
         * This ensures SNI composability: each layer's intermediate values
         * are re-randomized before the next layer processes them. */
        mask_refresh_kem(p);
    }
}

/* =========================================================================
 * Masked Inverse NTT (ML-KEM)
 *
 * 7-layer Gentleman-Sande inverse NTT.
 * Reverse zeta order, negate zetas, GS butterfly, then multiply
 * by n^{-1} = 128^{-1} mod 3329 = 3303 in normal domain.
 * In Montgomery domain: f = 1441 = mont(128^{-1}).
 * ========================================================================= */
void masked_invntt_kem(masked_poly_t *p)
{
    unsigned int len, start, j, k;
    int16_t zeta;

    /* Montgomery representation of 128^{-1} mod 3329 */
    const int16_t f = 1441;

    k = 127;
    for (len = 2; len <= 128; len <<= 1) {
        for (start = 0; start < MLKEM_N; start = j + len) {
            zeta = zetas_kem[k--];
            for (j = start; j < start + len; j++) {
                int16_t t_s0, t_s1;

                /* GS butterfly share 0 */
                t_s0 = p->coeffs[0][j];
                p->coeffs[0][j] = t_s0 + p->coeffs[0][j + len];
                p->coeffs[0][j + len] = t_s0 - p->coeffs[0][j + len];
                /* Multiply by -zeta (inverse uses negated twiddle) */
                p->coeffs[0][j + len] = fqmul_kem(-zeta, p->coeffs[0][j + len]);

                /* GS butterfly share 1 */
                t_s1 = p->coeffs[1][j];
                p->coeffs[1][j] = t_s1 + p->coeffs[1][j + len];
                p->coeffs[1][j + len] = t_s1 - p->coeffs[1][j + len];
                p->coeffs[1][j + len] = fqmul_kem(-zeta, p->coeffs[1][j + len]);
            }
        }

        /* Mask refresh after each INTT layer */
        mask_refresh_kem(p);
    }

    /* Multiply all coefficients by f = mont(n^{-1}) per share */
    for (j = 0; j < MLKEM_N; j++) {
        p->coeffs[0][j] = fqmul_kem(f, p->coeffs[0][j]);
        p->coeffs[1][j] = fqmul_kem(f, p->coeffs[1][j]);
    }
}

/* =========================================================================
 * ML-DSA zetas in Montgomery domain
 *
 * zetas[i] = brv(i) * R mod Q, where R = 2^32, Q = 8380417
 * Full 8-layer NTT for n=256.
 * ========================================================================= */
static const int32_t zetas_dsa[256] = {
         0,    25847, -2608894,  -518909,   237124,  -777960,  -876248,   466468,
   1826347,  2353451,  -359251, -2091905,  3119733, -2884855,  3111497,  2680103,
   2725464,  1024112, -1079900,  3585928,  -549488, -1119584,  2619752, -2108549,
  -2118186, -3859737, -1399561, -3277672,  1757237,   -19422,  4010497,   280005,
   2706023,    95776,  3077325,  3530437, -1661693, -3592148, -2537516,  3915439,
  -3861115, -3043716,  3574422, -2867647,  3539968,  -300467,  2348700,  -539299,
  -1699267, -1643818,  3505694, -3821735,  3507263, -2140649, -1600420,  3699596,
    811944,   531354,   954230,  3881043,  3900724, -2556880,  2071892, -2797779,
  -3930395, -1528703, -3677745, -3041255, -1452451,  3475950,  2176455, -1585221,
  -1257611,  1939314, -4083598, -1000202, -3190144, -3157330, -3632928,   126922,
   3412210,  -983419,  2147896,  2715295, -2967645, -3693493,  -411027, -2477047,
   -671102, -1228525,   -22981, -1308169,  -381987,  1349076,  1852771, -1430430,
  -3343383,   264944,   508951,  3097992,    44288, -1100098,   904516,  3958618,
  -3724342,    -8578,  1653064, -3249728,  2389356,  -210977,   759969, -1316856,
    189548, -3553272,  3159746, -1851402, -2409325,  -177440,  1315589,  1341330,
   1285669, -1584928,  -812732, -1439742, -3019102, -3881060, -3628969,  3839961,
   2091667,  3407706,  2316500,  3817976, -3342478,  2244091, -2446433, -3562462,
    266997,  2434439, -1235728,  3513181, -3520352, -3759364, -1197226, -3193378,
    900702,  1859098,   909542,   819034,   495491, -1613174,   -43260,  -522500,
   -655327, -3122442,  2031748,  3207046, -3556995,  -525098,  -768622, -3595838,
    342297,   286988, -2437823,  4108315,  3437287, -3342277,  1735879,   203044,
   2842341,  2691481, -2590150,  1265009,  4055324,  1247620,  2486353,  1595974,
  -3767016,  1250494,  2635921, -3548272, -2994039,  1869119,  1903435, -1050970,
  -1333058,  1237275, -3318210, -1430225,  -451100,  1312455,  3306115, -1962642,
  -1279661,  1917081, -2546312, -1374803,  1500165,   777191,  2235880,  3406031,
   -542412, -2831860, -1671176, -1846953, -2584293, -3724270,   594136, -3776993,
  -2013608,  2432395,  2454455,  -164721,  1957272,  3369112,   185531, -1207385,
  -3183426,   162844,  1616392,  3014001,   810149,  1652634, -3694233, -1799107,
  -3038916,  3523897,  3866901,   269760,  2213111,  -975884,  1717735,   472078,
   -426683,  1723600, -1803090,  1910376, -1667432, -1104333,  -260646, -3833893,
  -2939036, -2235985,  -420899, -2286327,   183443,  -976891,  1612842, -3545687,
   -554416,  3919660,   -48306, -1362209,  3937738,  1400424,  -846154,  1976782
};

static inline int32_t fqmul_dsa(int32_t a, int32_t b)
{
    int32_t result;
    SCA_MONT_D_MUL(result, a, b);
    return result;
}

/* Mask refresh for ML-DSA (32-bit coefficients), rejection-sampled into Z_Q. */
static void mask_refresh_dsa(masked_poly32_t *p)
{
    for (unsigned int i = 0; i < MLDSA_N; i++) {
        uint32_t r;
        int32_t  mask;
        do {
            SCA_GET_RANDOM(r);
            uint32_t cand = r & 0x007FFFFFu;
            if (cand < (uint32_t)MLDSA_Q) { mask = (int32_t)cand; break; }
        } while (1);

        int32_t s0 = p->coeffs[0][i] + mask;
        int32_t s1 = p->coeffs[1][i] - mask;

        int32_t tmp;
        SCA_BARRETT_D(tmp, s0);
        p->coeffs[0][i] = tmp;
        SCA_BARRETT_D(tmp, s1);
        p->coeffs[1][i] = tmp;
    }
}

/* =========================================================================
 * Masked Forward NTT (ML-DSA)
 *
 * Full 8-layer Cooley-Tukey NTT (n=256, Q=8380417).
 * ========================================================================= */
void masked_ntt_dsa(masked_poly32_t *p)
{
    unsigned int len, start, j, k;
    int32_t zeta;

    k = 0;
    for (len = 128; len >= 1; len >>= 1) {
        for (start = 0; start < MLDSA_N; start = j + len) {
            zeta = zetas_dsa[++k];
            for (j = start; j < start + len; j++) {
                int32_t t_s0, t_s1;

                t_s0 = fqmul_dsa(zeta, p->coeffs[0][j + len]);
                t_s1 = fqmul_dsa(zeta, p->coeffs[1][j + len]);

                p->coeffs[0][j + len] = p->coeffs[0][j] - t_s0;
                p->coeffs[0][j]       = p->coeffs[0][j] + t_s0;

                p->coeffs[1][j + len] = p->coeffs[1][j] - t_s1;
                p->coeffs[1][j]       = p->coeffs[1][j] + t_s1;
            }
        }

        mask_refresh_dsa(p);
    }
}

/* =========================================================================
 * Masked Inverse NTT (ML-DSA)
 *
 * Full 8-layer Gentleman-Sande INTT.
 * f = mont(256^{-1} mod Q) = 41978 (Montgomery form of n^{-1}).
 * ========================================================================= */
void masked_invntt_dsa(masked_poly32_t *p)
{
    unsigned int len, start, j, k;
    int32_t zeta;

    const int32_t f = 41978;  /* mont(256^{-1}) mod 8380417 */

    k = 256;
    for (len = 1; len <= 128; len <<= 1) {
        for (start = 0; start < MLDSA_N; start = j + len) {
            zeta = -zetas_dsa[--k];
            for (j = start; j < start + len; j++) {
                int32_t t_s0, t_s1;

                t_s0 = p->coeffs[0][j];
                p->coeffs[0][j] = t_s0 + p->coeffs[0][j + len];
                p->coeffs[0][j + len] = t_s0 - p->coeffs[0][j + len];
                p->coeffs[0][j + len] = fqmul_dsa(zeta, p->coeffs[0][j + len]);

                t_s1 = p->coeffs[1][j];
                p->coeffs[1][j] = t_s1 + p->coeffs[1][j + len];
                p->coeffs[1][j + len] = t_s1 - p->coeffs[1][j + len];
                p->coeffs[1][j + len] = fqmul_dsa(zeta, p->coeffs[1][j + len]);
            }
        }

        mask_refresh_dsa(p);
    }

    for (j = 0; j < MLDSA_N; j++) {
        p->coeffs[0][j] = fqmul_dsa(f, p->coeffs[0][j]);
        p->coeffs[1][j] = fqmul_dsa(f, p->coeffs[1][j]);
    }
}
