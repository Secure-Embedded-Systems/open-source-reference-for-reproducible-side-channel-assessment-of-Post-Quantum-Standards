/* ============================================================================
 * ML-KEM-1024 (FIPS-203) pure-software v2-widetrig CCA-PC TVLA firmware on
 * CW305 (CV32E40X, RV32IMC). No Custom-3 ISE. mlkem-sw upstream is
 * pq-code-package/mlkem-native, patched only at indcpa.c to bracket the
 * mlk_poly_tomsg call with MBOX(1) writes (TIO4 trigger pulse).
 *
 * Mirrors main_saarinen_naive_widetrig.c so the share-recombine-window
 * comparison is apples-to-apples against the ISE-accelerated naive baseline:
 *   - HW-matched 10-CT_B pool from saarinen_ct_pool_1024_v2.h
 *   - LFSR-driven Set-A / Set-B coin per trace
 *   - MBOX[3] upper bits = explicit pool label so the host does not infer A/B
 *
 * Mailbox layout (firmware -> host):
 *   MBOX[0]  0xCAFE0010 sticky boot marker
 *   MBOX[1]  trigger bit (pulsed inside mlk_poly_tomsg call site)
 *   MBOX[2]  trace_idx running counter
 *   MBOX[3]  [31:24] = B-pool index (1..10 when pool=1, 0 when pool=0)
 *            [16]    = pool A/B (0=A, 1=B)
 *            [15: 0] = 0x0021 trace-done sentinel
 * ============================================================================ */

#include <stdint.h>
#include <stddef.h>

#include "mlkem_native.h"
#include "test_vectors_1024.h"
#include "saarinen_ct_pool_1024_v2.h"

#define MBOX(n) (*(volatile uint32_t *)(0x50000200 + (n) * 4))

/* libc shims (same as performance wrapper, -nostdlib build). */
void *memcpy(void *dst, const void *src, size_t n)
{ uint8_t *d=dst; const uint8_t *s=src; while(n--) *d++=*s++; return dst; }
void *memset(void *s, int c, size_t n)
{ uint8_t *p=s; while(n--) *p++=(uint8_t)c; return s; }
int memcmp(const void *a, const void *b, size_t n)
{ const uint8_t *pa=a, *pb=b; for(size_t i=0;i<n;i++) if(pa[i]!=pb[i]) return pa[i]-pb[i]; return 0; }
void *memmove(void *dst, const void *src, size_t n)
{ uint8_t *d=dst; const uint8_t *s=src;
  if(d<s){ while(n--) *d++=*s++; } else { d+=n; s+=n; while(n--) *--d=*--s; } return dst; }
int randombytes(uint8_t *out, size_t outlen)
{ static uint32_t s=0xDEADBEEFu;
  for(size_t i=0;i<outlen;i++){ s=s*1664525u+1013904223u; out[i]=(uint8_t)(s>>24); } return 0; }

static uint8_t s_pk[CRYPTO_PUBLICKEYBYTES];
static uint8_t s_sk[CRYPTO_SECRETKEYBYTES];
static uint8_t s_ss[CRYPTO_BYTES];

static inline uint32_t lfsr_step(uint32_t s)
{
    /* Galois LFSR, taps 32,22,2,1. Per-trace A/B coin only -- non-cryptographic. */
    return (s >> 1) ^ (((s & 1u) ? 0xD0000001u : 0u));
}

static inline void inter_trace_pause(void)
{
    for (volatile uint32_t i = 0; i < 50000; i++) { /* spin */ }
}

void main(void)
{
    /* Early-boot diagnostic markers: write BEFORE the expensive KeyGen so the
     * host can distinguish "firmware never started" from "still in KeyGen". */
    MBOX(0) = 0xCAFE00B0u;   /* phase 0: main entered, about to KeyGen */
    MBOX(2) = 0u;
    MBOX(3) = 0x000000B0u;

    /* One-time keypair generation against the ACVP fixture (so sk matches what
     * SAARINEN_CT_A was generated to decrypt cleanly). Pure-SW KeyGen on
     * RV32IMC at 16 MHz takes ~1-2 s, well inside the 120 s host boot deadline. */
    crypto_kem_keypair_derand(s_pk, s_sk, TVEC_IN_KEM_KEYPAIR);

    /* Boot-ready markers expected by capture_cw305_saarinen.py::wait_boot(). */
    MBOX(0) = 0xCAFE0010u;
    MBOX(2) = 0u;
    MBOX(3) = 0x00000020u;

    /* Seed LFSR from mcycle so each programming cycle picks a fresh A/B order. */
    uint32_t mc;
    __asm__ volatile ("csrr %0, mcycle" : "=r"(mc));
    uint32_t prng = mc ? mc : 0xACE1u;

    uint32_t trace_idx = 0u;
    for (;;) {
        prng = lfsr_step(prng);
        uint32_t coin = prng & 0xFu;
        uint32_t pool = (coin & 0x1u);
        uint32_t b_ix = (prng >> 4) % SAARINEN_CT_B_POOL_SIZE;

        const uint8_t *ct = pool ? SAARINEN_CT_B_POOL[b_ix] : SAARINEN_CT_A;
        uint32_t label = pool ? (0x10000u | ((b_ix + 1) << 24)) : 0u;

        MBOX(2) = trace_idx;
        /* MBOX(1) is pulsed inside mlk_poly_tomsg via the indcpa.c patch
         * (trigger bracket is exactly that one call, tight to the
         * secret-bit-extraction operation in pure-software ML-KEM). */
        (void)crypto_kem_dec(s_ss, ct, s_sk);
        MBOX(3) = label | 0x0021u;
        trace_idx++;
        inter_trace_pause();
    }
}
