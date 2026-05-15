#include <stdint.h>
#include "params.h"
#include "cbd.h"

#include "mlkem1024_instructions.h"
#include "sca_pqc_ise.h"

/*************************************************
* Name:        load32_littleendian
*
* Description: load 4 bytes into a 32-bit integer
*              in little-endian order
*
* Arguments:   - const uint8_t *x: pointer to input byte array
*
* Returns 32-bit unsigned integer loaded from x
**************************************************/
static uint32_t load32_littleendian(const uint8_t x[4])
{
  uint32_t r;
  r  = (uint32_t)x[0];
  r |= (uint32_t)x[1] << 8;
  r |= (uint32_t)x[2] << 16;
  r |= (uint32_t)x[3] << 24;
  return r;
}

/*************************************************
* Name:        load24_littleendian
*
* Description: load 3 bytes into a 32-bit integer
*              in little-endian order.
*              This function is only needed for Kyber-512
*
* Arguments:   - const uint8_t *x: pointer to input byte array
*
* Returns 32-bit unsigned integer loaded from x (most significant byte is zero)
**************************************************/
#if KYBER_ETA1 == 3
static uint32_t load24_littleendian(const uint8_t x[3])
{
  uint32_t r;
  r  = (uint32_t)x[0];
  r |= (uint32_t)x[1] << 8;
  r |= (uint32_t)x[2] << 16;
  return r;
}
#endif


/*************************************************
* Name:        cbd2
*
* Description: Given an array of uniformly random bytes, compute
*              polynomial with coefficients distributed according to
*              a centered binomial distribution with parameter eta=2
*
* Arguments:   - poly *r: pointer to output polynomial
*              - const uint8_t *buf: pointer to input byte array
**************************************************/
static void cbd2(poly *r, const uint8_t buf[2*KYBER_N/4])
{
  unsigned int i,j;
  uint32_t t,d;
  int16_t a,b;
  for(i=0;i<KYBER_N/8;i++) {
    t  = load32_littleendian(buf+4*i);
    d  = t & 0x55555555;
    d += (t>>1) & 0x55555555;

        #if ENABLE_KYBER_CBD
            /* SCA_CBD2_* extract coefficient j from raw 32-bit input.
             * The HW does its own popcount on the 4-bit slice at offset
             * j*4, so we pass `t` (NOT the precomputed `d`). */
            uint32_t c0,c1,c2,c3,c4,c5,c6,c7;
            SCA_CBD2_1(c0, t); r->coeffs[8*i+0] = (int16_t)(int32_t)c0;
            SCA_CBD2_2(c1, t); r->coeffs[8*i+1] = (int16_t)(int32_t)c1;
            SCA_CBD2_3(c2, t); r->coeffs[8*i+2] = (int16_t)(int32_t)c2;
            SCA_CBD2_4(c3, t); r->coeffs[8*i+3] = (int16_t)(int32_t)c3;
            SCA_CBD2_5(c4, t); r->coeffs[8*i+4] = (int16_t)(int32_t)c4;
            SCA_CBD2_6(c5, t); r->coeffs[8*i+5] = (int16_t)(int32_t)c5;
            SCA_CBD2_7(c6, t); r->coeffs[8*i+6] = (int16_t)(int32_t)c6;
            SCA_CBD2_8(c7, t); r->coeffs[8*i+7] = (int16_t)(int32_t)c7;
        #else
            for (j = 0; j < 8; j++) {
                a = (d >> (4 * j + 0)) & 0x3;
                b = (d >> (4 * j + 2)) & 0x3;
                r->coeffs[8 * i + j] = a - b;
            }

        #endif
  }
}

/*************************************************
* Name:        cbd3
*
* Description: Given an array of uniformly random bytes, compute
*              polynomial with coefficients distributed according to
*              a centered binomial distribution with parameter eta=3.
*              This function is only needed for Kyber-512
*
* Arguments:   - poly *r: pointer to output polynomial
*              - const uint8_t *buf: pointer to input byte array
**************************************************/
#if KYBER_ETA1 == 3
static void cbd3(poly *r, const uint8_t buf[3*KYBER_N/4])
{
  unsigned int i,j;
  uint32_t t,d;
  int16_t a,b;

  for(i=0;i<KYBER_N/4;i++) {
    t  = load24_littleendian(buf+3*i);
    d  = t & 0x00249249;
    d += (t>>1) & 0x00249249;
    d += (t>>2) & 0x00249249;

    for(j=0;j<4;j++) {
      a = (d >> (6*j+0)) & 0x7;
      b = (d >> (6*j+3)) & 0x7;
      r->coeffs[4*i+j] = a - b;
    }
  }
}
#endif

void poly_cbd_eta1(poly *r, const uint8_t buf[KYBER_ETA1*KYBER_N/4])
{
#if KYBER_ETA1 == 2
  cbd2(r, buf);
#elif KYBER_ETA1 == 3
  cbd3(r, buf);
#else
#error "This implementation requires eta1 in {2,3}"
#endif
}

void poly_cbd_eta2(poly *r, const uint8_t buf[KYBER_ETA2*KYBER_N/4])
{
#if KYBER_ETA2 == 2
  cbd2(r, buf);
#else
#error "This implementation requires eta2 = 2"
#endif
}
