/* ============================================================================
 * ML-KEM-1024 (FIPS-203) cycle-count baseline on CW305.
 *
 * Build target: FW=mlkem_native (see integration/Makefile).
 * Library:      upstream pq-code-package/mlkem-native, portable-C backend.
 * Backend:      no native arithmetic, no native FIPS-202, no CV-X-IF Custom-3.
 *
 * This firmware exists to answer one question for the paper benchmark
 * table: how many CV32E40X cycles does ML-KEM-1024 cost when the CPU
 * runs plain software, with no custom instructions? The number it ships
 * is the "before" baseline that turns the ISE-accelerated 180 k figure
 * in oscar2026.tex into a measured speed-up.
 *
 * Mailbox protocol (firmware -> host, base 0x50000200, 4 x uint32).
 * Layout is shared with the patched FW=fips203 and FW=fips203_masked
 * firmwares so one host poll yields four numbers per configuration:
 *   MBOX[0]  KeyGen cycles  (mcycle delta, 32-bit)
 *   MBOX[1]  Encap   cycles
 *   MBOX[2]  Decap   cycles
 *   MBOX[3]  final sentinel
 *              0xCAFE0001  ALL PASS  (verify_mailbox.py polls this slot)
 *              0xCAFE0002  FAIL
 *              0xCAFE00BA  baseline booted, run incomplete (sticky early-write)
 *
 * Notes:
 *   - The mcycle CSR is 32-bit on CV32E40X here; we do not read mcycleh.
 *     ML-KEM-1024 on a 20 MHz core finishes in well under 2^32 cycles even
 *     for the slowest plain-C compile.
 *   - randombytes() is provided as a deterministic LCG. ML-KEM Decaps does
 *     not call it; KeyGen/Encap use *_derand variants seeded from the KAT
 *     fixture, so the LCG is never actually invoked for these traces. The
 *     definition is required as an external symbol by mlkem-native.
 *   - No code from the ISE-accelerated tree is linked here. The Custom-3
 *     opcode 0x7B must not appear in the resulting ELF (objdump check at
 *     end of the Makefile recipe).
 *
 * Source for KAT data: test_vectors_1024.h in the existing FIPS-203 tree.
 * That header is data-only; including it does not pull in any ISE code.
 * ============================================================================
 */

#include <stdint.h>
#include <stddef.h>

/* mlkem-native API. -Iupstream/mlkem makes this resolve to
 * upstream/mlkem/mlkem_native.h, which in turn exports
 * crypto_kem_keypair_derand / crypto_kem_enc_derand / crypto_kem_dec
 * once MLK_CONFIG_PARAMETER_SET=1024 is passed via CFLAGS. */
#include "mlkem_native.h"

/* Data-only header: only TVEC_* static const byte arrays. No ISE includes. */
#include "test_vectors_1024.h"

#define MBOX(n) (*(volatile uint32_t *)(0x50000200 + (n) * 4))

/* mcycle CSR read (RV32IMC). CV32E40X exposes the standard machine cycle
 * counter; we never need mcycleh in this range. */
static inline uint32_t read_mcycle(void)
{
    uint32_t v;
    __asm__ volatile ("csrr %0, mcycle" : "=r"(v));
    return v;
}

/* Minimal libc shims: -nostdlib builds do not pull in memcpy/memset/memcmp,
 * and mlkem-native uses them freely. The existing FIPS-203 tree has a
 * static-inline string.h in mlkem1024_fips203/include/, but that header
 * also drags in unrelated declarations; we provide a self-contained set
 * here so the baseline tree has zero cross-dependence on the ISE tree
 * apart from the data-only test vector. */
void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *s, int c, size_t n)
{
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

/* mlkem-native requires the consumer to provide randombytes(). Decaps does
 * not invoke it on the FIPS-203 hot path (the derand entrypoints take their
 * coins as arguments), but the symbol must resolve at link time, and a
 * fallback path inside indcpa.c can route through randombytes if a future
 * mlkem-native release changes its internals. Returning 0 = success. */
int randombytes(uint8_t *out, size_t outlen)
{
    static uint32_t s = 0xDEADBEEFu;
    for (size_t i = 0; i < outlen; i++) {
        s = s * 1664525u + 1013904223u;
        out[i] = (uint8_t)(s >> 24);
    }
    return 0;
}

static int byte_eq(const uint8_t *a, const uint8_t *b, unsigned int n)
{
    for (unsigned int i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

/* Large buffers in .bss so we do not blow the stack. ML-KEM-1024 sizes:
 *   pk = 1568, sk = 3168, ct = 1568, ss = 32. */
static uint8_t pk[CRYPTO_PUBLICKEYBYTES];
static uint8_t sk[CRYPTO_SECRETKEYBYTES];
static uint8_t ct[CRYPTO_CIPHERTEXTBYTES];
static uint8_t ss_e[CRYPTO_BYTES];
static uint8_t ss_d[CRYPTO_BYTES];

void main(void)
{
    uint32_t c0, c1;
    uint32_t keygen_cy = 0, encap_cy = 0, decap_cy = 0;
    uint32_t flags = 0;

    MBOX(0) = 0;
    MBOX(1) = 0;
    MBOX(2) = 0;
    MBOX(3) = 0xCAFE00BAu;   /* baseline booted, no result yet */

    /* Per-phase debug marker so if we hang inside any mlkem-native call
     * the host can read the last successful phase from MBOX[3]. */
    MBOX(3) = 0xCAFE00B0u;   /* phase 0: about to call KeyGen */

    /* ---- KeyGen (derand, seeds drawn from KAT fixture) ---- */
    c0 = read_mcycle();
    crypto_kem_keypair_derand(pk, sk, TVEC_IN_KEM_KEYPAIR);
    c1 = read_mcycle();
    keygen_cy = c1 - c0;
    MBOX(0) = keygen_cy;
    MBOX(3) = 0xCAFE00B1u;   /* phase 1: KeyGen done */

    /* Functional sanity: keypair must match the ACVP reference vector.
     * If mlkem-native is FIPS-203 conformant on this CPU, both pk and
     * the implementation-specific sk encoding match byte-exact. */
    if (byte_eq(pk, TVEC_OUT_PK, CRYPTO_PUBLICKEYBYTES)) flags |= 1u << 0;
    if (byte_eq(sk, TVEC_OUT_SK, CRYPTO_SECRETKEYBYTES)) flags |= 1u << 1;

    /* ---- Encap (derand, using the ACVP-supplied ek and m) ---- */
    c0 = read_mcycle();
    crypto_kem_enc_derand(ct, ss_e, TVEC_IN_KEM_ENC_EK, TVEC_IN_KEM_ENC);
    c1 = read_mcycle();
    encap_cy = c1 - c0;
    MBOX(1) = encap_cy;
    MBOX(3) = 0xCAFE00B2u;   /* phase 2: Encap done */

    if (byte_eq(ct,   TVEC_OUT_CT, CRYPTO_CIPHERTEXTBYTES)) flags |= 1u << 2;
    if (byte_eq(ss_e, TVEC_OUT_SS, CRYPTO_BYTES))           flags |= 1u << 3;

    /* ---- Decap (against the ACVP reference ct + our own sk) ----
     * Headline number: this is the measurement the professor asked for. */
    c0 = read_mcycle();
    crypto_kem_dec(ss_d, TVEC_OUT_CT, sk);
    c1 = read_mcycle();
    decap_cy = c1 - c0;
    MBOX(2) = decap_cy;
    MBOX(3) = 0xCAFE00B3u;   /* phase 3: Decap done */

    if (byte_eq(ss_d, TVEC_OUT_SS, CRYPTO_BYTES)) flags |= 1u << 4;

    /* Self-consistency round-trip: encap with our own pk, decap with our
     * own sk, the two shared secrets must agree. Independent of the KAT. */
    {
        uint8_t ct2[CRYPTO_CIPHERTEXTBYTES];
        uint8_t ssa[CRYPTO_BYTES];
        uint8_t ssb[CRYPTO_BYTES];
        crypto_kem_enc_derand(ct2, ssa, pk, TVEC_IN_KEM_ENC);
        crypto_kem_dec(ssb, ct2, sk);
        if (byte_eq(ssa, ssb, CRYPTO_BYTES)) flags |= 1u << 5;
    }

    /* Republish cycle counts so a host poll that arrives after the run
     * sees consistent numbers regardless of write race. */
    MBOX(0) = keygen_cy;
    MBOX(1) = encap_cy;
    MBOX(2) = decap_cy;

    /* required_mask matches the existing ISE-accelerated main.c
     * (pk, sk, ct, ss_e from KeyGen+Encap, plus self-consistency
     * round-trip). Bit 4 (ss_d byte-match against TVEC_OUT_SS) is
     * informational only and intentionally excluded because the ACVP
     * Encap test vector encrypts against a different ek than the one
     * we derive at KeyGen, so the FO implicit-rejection path produces
     * a deterministic-but-unrelated ss_d. See test_vectors_1024.h
     * header comment and main.c:91 for the canonical statement. */
    uint32_t required_mask = (1u<<0)|(1u<<1)|(1u<<2)|(1u<<3)|(1u<<5);
    MBOX(3) = ((flags & required_mask) == required_mask) ? 0xCAFE0001u
                                                         : 0xCAFE0002u;

    while (1) { __asm__ volatile ("wfi"); }
}
