#include "getbits.h"

unsigned long _gst_getbits_int_cb (gst_getbits_t * gb, unsigned long bits);
unsigned long _gst_get1bit_int (gst_getbits_t * gb, unsigned long bits);
unsigned long _gst_getbits_int (gst_getbits_t * gb, unsigned long bits);
unsigned long _gst_getbits_fast_int (gst_getbits_t * gb, unsigned long bits);
unsigned long _gst_showbits_int (gst_getbits_t * gb, unsigned long bits);
void _gst_flushbits_int (gst_getbits_t * gb, unsigned long bits);
void _gst_getbits_back_int (gst_getbits_t * gb, unsigned long bits);


unsigned long
_gst_getbits_int_cb (gst_getbits_t * gb, unsigned long bits)
{
  int result;
  int bitsleft;

  /*printf("gst_getbits%lu %ld %p %08x\n", bits, gb->bits, gb->ptr, gb->dword); */

  if (!bits)
    return 0;

  gb->bits -= bits;
  result = gb->dword >> (32 - bits);

  if (gb->bits < 0) {

    gb->ptr += 4;

    bitsleft = (gb->endptr - gb->ptr) * 8;
    bits = -gb->bits;
    gb->bits += (bitsleft > 32 ? 32 : bitsleft);

    if (gb->endptr <= gb->ptr) {
      (gb->callback) (gb, gb->data);
      gb->bits -= bits;
    }
    gb->dword = swab32 (*((unsigned long *) (gb->ptr)));

    result |= (gb->dword >> (32 - bits));
  }
  gb->dword <<= bits;

  return result;
}

unsigned long
_gst_get1bit_int (gst_getbits_t * gb, unsigned long bits)
{
  unsigned char rval;

  rval = *gb->ptr << gb->bits;

  gb->bits++;
  gb->ptr += (gb->bits >> 3);
  gb->bits &= 0x7;

  GST_DEBUG ("getbits%ld, %08x", bits, rval);
  return rval >> 7;
}

unsigned long
_gst_getbits_int (gst_getbits_t * gb, unsigned long bits)
{
  unsigned long rval;

  if (bits == 0)
    return 0;

  rval = swab32 (*((unsigned long *) (gb->ptr)));
  rval <<= gb->bits;

  gb->bits += bits;

  rval >>= (32 - bits);
  gb->ptr += (gb->bits >> 3);
  gb->bits &= 0x7;

  GST_DEBUG ("getbits%ld, %08lx", bits, rval);
  return rval;
}

unsigned long
_gst_getbits_fast_int (gst_getbits_t * gb, unsigned long bits)
{
  unsigned long rval;

  rval = (unsigned char) (gb->ptr[0] << gb->bits);
  rval |= ((unsigned int) gb->ptr[1] << gb->bits) >> 8;
  rval <<= bits;
  rval >>= 8;

  gb->bits += bits;
  gb->ptr += (gb->bits >> 3);
  gb->bits &= 0x7;

  GST_DEBUG ("getbits%ld, %08lx", bits, rval);
  return rval;
}

unsigned long
_gst_showbits_int (gst_getbits_t * gb, unsigned long bits)
{
  unsigned long rval;

  if (bits == 0)
    return 0;

  rval = swab32 (*((unsigned long *) (gb->ptr)));
  rval <<= gb->bits;
  rval >>= (32 - bits);

  GST_DEBUG ("showbits%ld, %08lx", bits, rval);
  return rval;
}

void
_gst_flushbits_int (gst_getbits_t * gb, unsigned long bits)
{
  gb->bits += bits;
  gb->ptr += (gb->bits >> 3);
  gb->bits &= 0x7;
  GST_DEBUG ("flushbits%ld", bits);
}

void
_gst_getbits_back_int (gst_getbits_t * gb, unsigned long bits)
{
  gb->bits -= bits;
  gb->ptr += (gb->bits >> 3);
  gb->bits &= 0x7;
}
