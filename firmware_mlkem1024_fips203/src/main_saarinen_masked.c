/* Masked (DOM d=1) Saarinen CCA-PC TVLA capture firmware; trigger around unmask_poly_kem + poly_tomsg. */

#include <stdint.h>
#include <stddef.h>
#include "string.h"
#include "params.h"
#include "kem.h"
#include "indcpa.h"
#include "verify.h"
#include "symmetric.h"
#include "polyvec.h"
#include "poly.h"
#include "ntt.h"
#include "test_vectors_1024.h"
#include "saarinen_ct_pool_1024_v2.h"

#include "sca_pqc_masked.h"
#include "sca_pqc_ise.h"
#include "sca_pqc_mmio.h"

#include "sca/firmware_hooks/sca_trigger.h"

#define MBOX(n) (*(volatile uint32_t *)(0x50000200 + (n) * 4))

static uint8_t s_pk[KYBER_PUBLICKEYBYTES];
static uint8_t s_sk[KYBER_SECRETKEYBYTES];
static uint8_t s_ss[KYBER_SSBYTES];

static masked_poly_t s_skpv_masked[KYBER_K];
static polyvec       s_b_pub;
static poly          s_v_pub;
static masked_poly_t s_mp_masked;
static uint8_t       s_ct2[KYBER_CIPHERTEXTBYTES];

extern void polyvec_decompress(polyvec *r, const uint8_t a[KYBER_POLYVECCOMPRESSEDBYTES]);
extern void poly_decompress(poly *r, const uint8_t a[KYBER_POLYCOMPRESSEDBYTES]);
extern void polyvec_frombytes(polyvec *r, const uint8_t a[KYBER_POLYVECBYTES]);

static void masked_polyvec_basemul_acc_kem(masked_poly_t *r_m,
                                           const masked_poly_t skpv_m[KYBER_K],
                                           const polyvec *b_pub)
{
    masked_poly_t b_triv, tmp_m;
    for (int i = 0; i < KYBER_N; i++) {
        b_triv.coeffs[0][i] = b_pub->vec[0].coeffs[i];
        b_triv.coeffs[1][i] = 0;
    }
    masked_poly_basemul_kem(r_m, &skpv_m[0], &b_triv);
    for (int k = 1; k < KYBER_K; k++) {
        for (int i = 0; i < KYBER_N; i++) {
            b_triv.coeffs[0][i] = b_pub->vec[k].coeffs[i];
            b_triv.coeffs[1][i] = 0;
        }
        masked_poly_basemul_kem(&tmp_m, &skpv_m[k], &b_triv);
        for (int i = 0; i < KYBER_N; i++) {
            r_m->coeffs[0][i] += tmp_m.coeffs[0][i];
            r_m->coeffs[1][i] += tmp_m.coeffs[1][i];
        }
    }
    masked_poly_reduce_kem(r_m);
}

static void masked_indcpa_dec(uint8_t m[KYBER_INDCPA_MSGBYTES],
                               const uint8_t c[KYBER_INDCPA_BYTES],
                               const uint8_t sk_pke[KYBER_INDCPA_SECRETKEYBYTES])
{
    polyvec skpv_clear;
    polyvec_decompress(&s_b_pub, c);
    poly_decompress(&s_v_pub, c + KYBER_POLYVECCOMPRESSEDBYTES);
    polyvec_frombytes(&skpv_clear, sk_pke);

    for (int k = 0; k < KYBER_K; k++)
        mask_poly_kem(&s_skpv_masked[k], skpv_clear.vec[k].coeffs);
    memset(&skpv_clear, 0, sizeof(skpv_clear));

    polyvec_ntt(&s_b_pub);
    masked_polyvec_basemul_acc_kem(&s_mp_masked, s_skpv_masked, &s_b_pub);
    masked_invntt_kem(&s_mp_masked);

    for (int i = 0; i < KYBER_N; i++) {
        s_mp_masked.coeffs[0][i] = (int16_t)((int32_t)s_v_pub.coeffs[i] -
                                              (int32_t)s_mp_masked.coeffs[0][i]);
        s_mp_masked.coeffs[1][i] = (int16_t)(-(int32_t)s_mp_masked.coeffs[1][i]);
    }
    masked_poly_reduce_kem(&s_mp_masked);

    int16_t plain[KYBER_N];
    poly mp_clear;

    /* v2-widetrig: trigger encloses the share-recombine boundary
     * (unmask_poly_kem) and the secret-bit extraction (poly_tomsg).
     * This is the textbook 2nd-order TVLA site for the d=1 DOM scheme. */
    MBOX(1) = 1u;
    unmask_poly_kem(plain, &s_mp_masked);
    for (int i = 0; i < KYBER_N; i++) mp_clear.coeffs[i] = plain[i];
    poly_tomsg(m, &mp_clear);
    MBOX(1) = 0u;
}

static int masked_kem_dec(uint8_t ss_out[KYBER_SSBYTES],
                          const uint8_t c[KYBER_CIPHERTEXTBYTES],
                          const uint8_t sk_full[KYBER_SECRETKEYBYTES])
{
    int fail;
    uint8_t buf[2 * KYBER_SYMBYTES];
    uint8_t kr [2 * KYBER_SYMBYTES];
    const uint8_t *pk_in = sk_full + KYBER_INDCPA_SECRETKEYBYTES;

    masked_indcpa_dec(buf, c, sk_full);
    /* trigger is now inside masked_indcpa_dec, around unmask_poly_kem+poly_tomsg */

    memcpy(buf + KYBER_SYMBYTES,
           sk_full + KYBER_SECRETKEYBYTES - 2 * KYBER_SYMBYTES, KYBER_SYMBYTES);
    hash_g(kr, buf, 2 * KYBER_SYMBYTES);
    indcpa_enc(s_ct2, buf, pk_in, kr + KYBER_SYMBYTES);

    fail = verify(c, s_ct2, KYBER_CIPHERTEXTBYTES);

    rkprf(ss_out, sk_full + KYBER_SECRETKEYBYTES - KYBER_SYMBYTES, c);
    cmov(ss_out, kr, KYBER_SYMBYTES, !fail);
    return 0;
}

static inline void inter_trace_pause(void) {
    for (volatile uint32_t i = 0; i < 50000; i++) { /* spin */ }
}

/* Galois LFSR (taps 32,22,2,1) -- non-cryptographic but fine for randomising
 * the A/B label across the campaign so any slow drift averages out. */
static inline uint32_t lfsr_step(uint32_t s) {
    return (s >> 1) ^ (((s & 1u) ? 0xD0000001u : 0u));
}

void main(void)
{
    {
        uint32_t dummy;
        SCA_SEED_PRNG(dummy, 0xC0FFEE01u, 0u);
        SCA_SEED_PRNG(dummy, 0xDEADBEEFu, 1u);
        SCA_SEED_PRNG(dummy, 0x12345678u, 2u);
        SCA_SEED_PRNG(dummy, 0xA5A5A5A5u, 3u);
        (void)dummy;
    }
    crypto_kem_keypair_derand(s_pk, s_sk, TVEC_IN_KEM_KEYPAIR);

    MBOX(0) = 0xCAFE0010u;
    MBOX(2) = 0u;
    MBOX(3) = 0x00000020u;   /* boot sentinel before first iteration */

    /* Seed LFSR from mcycle so each programming cycle has a fresh order.
     * The SCA mask-PRNG above is intentionally seeded separately -- it
     * randomises the share split, not the A/B campaign label. */
    uint32_t mc;
    __asm__ volatile ("csrr %0, mcycle" : "=r"(mc));
    uint32_t prng = mc ? mc : 0xACE1u;

    uint32_t trace_idx = 0u;
    for (;;) {
        prng = lfsr_step(prng);
        uint32_t coin = prng & 0xFu;       /* 4 random bits */
        uint32_t pool = (coin & 0x1u);     /* bit 0: 0=A, 1=B */
        uint32_t b_ix = (prng >> 4) % SAARINEN_CT_B_POOL_SIZE;

        const uint8_t *ct = pool ? SAARINEN_CT_B_POOL[b_ix] : SAARINEN_CT_A;
        uint32_t label = pool ? (0x10000u | ((b_ix + 1) << 24)) : 0u;

        MBOX(2) = trace_idx;
        (void)masked_kem_dec(s_ss, ct, s_sk);
        MBOX(3) = label | 0x0021u;
        trace_idx++;
        inter_trace_pause();
    }
}
