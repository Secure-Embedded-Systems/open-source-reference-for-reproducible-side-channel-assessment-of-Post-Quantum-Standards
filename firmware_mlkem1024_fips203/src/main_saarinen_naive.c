/* Naive (unmasked) Saarinen CCA-PC TVLA capture firmware; trigger around poly_tomsg. */

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

#define MBOX(n) (*(volatile uint32_t *)(0x50000200 + (n) * 4))

static uint8_t s_pk[KYBER_PUBLICKEYBYTES];
static uint8_t s_sk[KYBER_SECRETKEYBYTES];
static uint8_t s_ss[KYBER_SSBYTES];
static uint8_t s_ct2[KYBER_CIPHERTEXTBYTES];

/* polyvec_frombytes is in polyvec.c; forward declare so we can inline indcpa_dec here */
extern void polyvec_frombytes(polyvec *r, const uint8_t a[KYBER_POLYVECBYTES]);
extern void polyvec_decompress(polyvec *r, const uint8_t a[KYBER_POLYVECCOMPRESSEDBYTES]);
extern void poly_decompress(poly *r, const uint8_t a[KYBER_POLYCOMPRESSEDBYTES]);

/* v2-widetrig: inlined naive indcpa_dec with the trigger placed tight on
 * poly_tomsg ONLY, so the naive window contains the same single operation
 * (secret-bit extraction) that the masked window contains in addition to
 * unmask_poly_kem.  The difference between the two TVLA t-curves is then
 * exactly the share-recombine event in the masked path. */
static void naive_indcpa_dec_widetrig(uint8_t m[KYBER_INDCPA_MSGBYTES],
                                       const uint8_t c[KYBER_INDCPA_BYTES],
                                       const uint8_t sk[KYBER_INDCPA_SECRETKEYBYTES])
{
    polyvec b, skpv;
    poly v, mp;
    polyvec_decompress(&b, c);
    poly_decompress(&v, c + KYBER_POLYVECCOMPRESSEDBYTES);
    polyvec_frombytes(&skpv, sk);
    polyvec_ntt(&b);
    polyvec_basemul_acc_montgomery(&mp, &skpv, &b);
    poly_invntt_tomont(&mp);
    poly_sub(&mp, &v, &mp);
    poly_reduce(&mp);

    MBOX(1) = 1u;   /* trigger HIGH: bracket poly_tomsg only (matches masked) */
    poly_tomsg(m, &mp);
    MBOX(1) = 0u;
}

static int naive_kem_dec(uint8_t ss[KYBER_SSBYTES],
                         const uint8_t ct[KYBER_CIPHERTEXTBYTES],
                         const uint8_t sk[KYBER_SECRETKEYBYTES])
{
    uint8_t buf[2 * KYBER_SYMBYTES];
    uint8_t kr [2 * KYBER_SYMBYTES];
    const uint8_t *pk_in = sk + KYBER_INDCPA_SECRETKEYBYTES;

    naive_indcpa_dec_widetrig(buf, ct, sk);
    memcpy(buf + KYBER_SYMBYTES,
           sk + KYBER_SECRETKEYBYTES - 2 * KYBER_SYMBYTES, KYBER_SYMBYTES);
    hash_g(kr, buf, 2 * KYBER_SYMBYTES);
    indcpa_enc(s_ct2, buf, pk_in, kr + KYBER_SYMBYTES);

    int fail = verify(ct, s_ct2, KYBER_CIPHERTEXTBYTES);

    rkprf(ss, sk + KYBER_SECRETKEYBYTES - KYBER_SYMBYTES, ct);
    cmov(ss, kr, KYBER_SYMBYTES, !fail);
    return 0;
}

static inline void inter_trace_pause(void) {
    for (volatile uint32_t i = 0; i < 50000; i++) { /* ~2.5 ms @ 20 MHz */ }
}

/* Galois LFSR (taps 32,22,2,1) -- non-cryptographic but fine for randomising
 * the A/B label across the campaign so any slow drift averages out. */
static inline uint32_t lfsr_step(uint32_t s) {
    return (s >> 1) ^ (((s & 1u) ? 0xD0000001u : 0u));
}

void main(void)
{
    crypto_kem_keypair_derand(s_pk, s_sk, TVEC_IN_KEM_KEYPAIR);

    MBOX(0) = 0xCAFE0010u;
    MBOX(2) = 0u;
    MBOX(3) = 0x00000020u;   /* boot sentinel before first iteration */

    /* Seed PRNG from mcycle so each programming cycle has a fresh order. */
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
        /* MBOX[3] layout: [31:24]=B-index (1..10), [16]=pool A/B, [15:0]=sentinel */

        MBOX(2) = trace_idx;
        (void)naive_kem_dec(s_ss, ct, s_sk);
        MBOX(3) = label | 0x0021u;
        trace_idx++;
        inter_trace_pause();
    }
}
