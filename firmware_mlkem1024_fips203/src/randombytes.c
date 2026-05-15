/* Bare-metal stub: randombytes is unused by KAT (we only call _derand
 * variants). Provide a deterministic byte fill so any accidental call
 * doesn't pull in libc. */
#include <stddef.h>
#include <stdint.h>
#include "randombytes.h"

void randombytes(uint8_t *out, size_t outlen)
{
    static uint32_t s = 0xDEADBEEF;
    for (size_t i = 0; i < outlen; i++) {
        s = s * 1664525u + 1013904223u;
        out[i] = (uint8_t)(s >> 24);
    }
}
