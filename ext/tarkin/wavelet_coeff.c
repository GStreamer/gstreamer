#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mem.h"
#include "wavelet.h"
#include "rle.h"

#define printf(args...)

#define GRAY_CODES 1

#if defined(GRAY_CODES)
static inline uint16_t
binary_to_gray (uint16_t x)
{
  return x ^ (x >> 1);
}

static inline uint16_t
gray_to_binary (uint16_t x)
{
  int i;

  for (i = 1; i < 16; i += i)
    x ^= x >> i;
  return x;
}
#endif


static inline void
encode_coeff (ENTROPY_CODER significand_bitstream[],
    ENTROPY_CODER insignificand_bitstream[], TYPE coeff)
{
  int sign = (coeff >> (8 * sizeof (TYPE) - 1)) & 1;

#if defined(GRAY_CODES)
  TYPE significance = binary_to_gray (coeff);
#else
  static TYPE mask[2] = { 0, ~0 };
  TYPE significance = coeff ^ mask[sign];
#endif
  int i = TYPE_BITS;

  do {
    i--;
    OUTPUT_BIT (&significand_bitstream[i], (significance >> i) & 1);
  } while (!((significance >> i) & 1) && i > 0);

  OUTPUT_BIT (&significand_bitstream[i], sign);

  while (--i >= 0)
    OUTPUT_BIT (&insignificand_bitstream[i], (significance >> i) & 1);
}



static inline TYPE
decode_coeff (ENTROPY_CODER significand_bitstream[],
    ENTROPY_CODER insignificand_bitstream[])
{
#if !defined(GRAY_CODES)
  static TYPE mask[2] = { 0, ~0 };
#endif
  TYPE significance = 0;
  int sign;
  int i = TYPE_BITS;

  do {
    i--;
    significance |= INPUT_BIT (&significand_bitstream[i]) << i;
/*    if (ENTROPY_CODER_EOS(&significand_bitstream[i])) */
/*       return 0; */
  } while (!significance && i > 0);

  sign = INPUT_BIT (&significand_bitstream[i]);
/* if (ENTROPY_CODER_EOS(&significand_bitstream[i])) */
/*    return 0; */

  while (--i >= 0)
    significance |= INPUT_BIT (&insignificand_bitstream[i]) << i;

#if defined(GRAY_CODES)
  significance |= sign << (8 * sizeof (TYPE) - 1);
  return gray_to_binary (significance);
#else
  return (significance ^ mask[sign]);
#endif
}


static inline uint32_t
skip_0coeffs (Wavelet3DBuf * buf,
    ENTROPY_CODER s_stream[], ENTROPY_CODER i_stream[], uint32_t limit)
{
  int i;
  uint32_t skip = limit;

  for (i = 0; i < TYPE_BITS; i++) {
    if (ENTROPY_CODER_SYMBOL (&s_stream[i]) != 0) {
      return 0;
    } else {
      uint32_t runlength = ENTROPY_CODER_RUNLENGTH (&s_stream[i]);

      if (i == 0)
	runlength /= 2;		/* sign bits are in this bitplane ... */
      if (skip > runlength)
	skip = runlength;
      if (skip <= 2)
	return 0;
    }
  }

  ENTROPY_CODER_SKIP (&s_stream[0], 2 * skip);	/* kill sign+significance bits */

  for (i = 1; i < TYPE_BITS; i++)
    ENTROPY_CODER_SKIP (&s_stream[i], skip);

  return skip;
}



#if 1
static inline void
encode_quadrant (const Wavelet3DBuf * buf,
    int level, int quadrant, uint32_t w, uint32_t h, uint32_t f,
    ENTROPY_CODER significand_bitstream[],
    ENTROPY_CODER insignificand_bitstream[])
{
  uint32_t x, y, z;

  for (z = 0; z < f; z++) {
    for (y = 0; y < h; y++) {
      for (x = 0; x < w; x++) {
	unsigned int index = buf->offset[level][quadrant]
	    + z * buf->width * buf->height + y * buf->width + x;

	encode_coeff (significand_bitstream, insignificand_bitstream,
	    buf->data[index]);
      }
    }
  }
}


static void
encode_coefficients (const Wavelet3DBuf * buf,
    ENTROPY_CODER s_stream[], ENTROPY_CODER i_stream[])
{
  int level;

  encode_coeff (s_stream, i_stream, buf->data[0]);

  for (level = 0; level < buf->scales - 1; level++) {
    uint32_t w, h, f, w1, h1, f1;

    w = buf->w[level];
    h = buf->h[level];
    f = buf->f[level];
    w1 = buf->w[level + 1] - w;
    h1 = buf->h[level + 1] - h;
    f1 = buf->f[level + 1] - f;

    if (w1 > 0)
      encode_quadrant (buf, level, 1, w1, h, f, s_stream, i_stream);
    if (h1 > 0)
      encode_quadrant (buf, level, 2, w, h1, f, s_stream, i_stream);
    if (f1 > 0)
      encode_quadrant (buf, level, 3, w, h, f1, s_stream, i_stream);
    if (w1 > 0 && h1 > 0)
      encode_quadrant (buf, level, 4, w1, h1, f, s_stream, i_stream);
    if (w1 > 0 && f1 > 0)
      encode_quadrant (buf, level, 5, w1, h, f1, s_stream, i_stream);
    if (h1 > 0 && f1 > 0)
      encode_quadrant (buf, level, 6, w, h1, f1, s_stream, i_stream);
    if (h1 > 0 && f1 > 0 && f1 > 0)
      encode_quadrant (buf, level, 7, w1, h1, f1, s_stream, i_stream);
  }
}


static inline void
decode_quadrant (Wavelet3DBuf * buf,
    int level, int quadrant, uint32_t w, uint32_t h, uint32_t f,
    ENTROPY_CODER s_stream[], ENTROPY_CODER i_stream[])
{
  uint32_t x, y, z;

  z = 0;
  do {
    y = 0;
    do {
      x = 0;
      do {
	uint32_t skip;
	uint32_t index = buf->offset[level][quadrant]
	    + z * buf->width * buf->height + y * buf->width + x;

	buf->data[index] = decode_coeff (s_stream, i_stream);

	skip = skip_0coeffs (buf, s_stream, i_stream,
	    (w - x - 1) + (h - y - 1) * w + (f - z - 1) * w * h);
	if (skip > 0) {
	  x += skip;
	  while (x >= w) {
	    y++;
	    x -= w;
	    while (y >= h) {
	      z++;
	      y -= h;
	      if (z >= f)
		return;
	    }
	  }
	}
	x++;
      } while (x < w);
      y++;
    } while (y < h);
    z++;
  } while (z < f);
}


static void
decode_coefficients (Wavelet3DBuf * buf,
    ENTROPY_CODER s_stream[], ENTROPY_CODER i_stream[])
{
  int level;

  buf->data[0] = decode_coeff (s_stream, i_stream);

  for (level = 0; level < buf->scales - 1; level++) {
    uint32_t w, h, f, w1, h1, f1;

    w = buf->w[level];
    h = buf->h[level];
    f = buf->f[level];
    w1 = buf->w[level + 1] - w;
    h1 = buf->h[level + 1] - h;
    f1 = buf->f[level + 1] - f;

    if (w1 > 0)
      decode_quadrant (buf, level, 1, w1, h, f, s_stream, i_stream);
    if (h1 > 0)
      decode_quadrant (buf, level, 2, w, h1, f, s_stream, i_stream);
    if (f1 > 0)
      decode_quadrant (buf, level, 3, w, h, f1, s_stream, i_stream);
    if (w1 > 0 && h1 > 0)
      decode_quadrant (buf, level, 4, w1, h1, f, s_stream, i_stream);
    if (w1 > 0 && f1 > 0)
      decode_quadrant (buf, level, 5, w1, h, f1, s_stream, i_stream);
    if (h1 > 0 && f1 > 0)
      decode_quadrant (buf, level, 6, w, h1, f1, s_stream, i_stream);
    if (h1 > 0 && f1 > 0 && f1 > 0)
      decode_quadrant (buf, level, 7, w1, h1, f1, s_stream, i_stream);
  }
}
#else

static void
encode_coefficients (const Wavelet3DBuf * buf,
    ENTROPY_CODER s_stream[], ENTROPY_CODER i_stream[])
{
  uint32_t i;

  for (i = 0; i < buf->width * buf->height * buf->frames; i++)
    encode_coeff (s_stream, i_stream, buf->data[i]);
}




static void
decode_coefficients (Wavelet3DBuf * buf,
    ENTROPY_CODER s_stream[], ENTROPY_CODER i_stream[])
{
  uint32_t i;

  for (i = 0; i < buf->width * buf->height * buf->frames; i++) {
    uint32_t skip;

    buf->data[i] = decode_coeff (s_stream, i_stream);

    skip = skip_0coeffs (buf, s_stream, i_stream,
	buf->width * buf->height * buf->frames - i);
    i += skip;
  }
}
#endif



static uint32_t
setup_limittabs (ENTROPY_CODER significand_bitstream[],
    ENTROPY_CODER insignificand_bitstream[],
    uint32_t significand_limittab[],
    uint32_t insignificand_limittab[], uint32_t limit)
{
  uint32_t significand_limit;
  uint32_t insignificand_limit;
  uint32_t byte_count;
  int i;

  assert (limit > 2 * TYPE_BITS * sizeof (uint32_t));	/* limit too small */

  printf ("%s: limit == %u\n", __FUNCTION__, limit);
  byte_count = 2 * TYPE_BITS * sizeof (uint32_t);	/* 2 binary coded limittabs */
  limit -= byte_count;
  printf ("%s: rem. limit == %u\n", __FUNCTION__, limit);

  significand_limit = limit * 7 / 8;
  insignificand_limit = limit - significand_limit;

  printf ("%s: limit == %u\n", __FUNCTION__, limit);
  printf ("significand limit == %u\n", significand_limit);
  printf ("insignificand limit == %u\n", insignificand_limit);

  for (i = TYPE_BITS - 1; i >= 0; i--) {
    uint32_t s_bytes, i_bytes;

    if (i > 0) {
      significand_limittab[i] = (significand_limit + 1) / 2;
      insignificand_limittab[i] = (insignificand_limit + 1) / 2;
    } else {
      significand_limittab[0] = significand_limit;
      insignificand_limittab[0] = insignificand_limit;
    }

    s_bytes = ENTROPY_ENCODER_FLUSH (&significand_bitstream[i]);
    i_bytes = ENTROPY_ENCODER_FLUSH (&insignificand_bitstream[i]);

    if (s_bytes < significand_limittab[i])
      significand_limittab[i] = s_bytes;

    if (i_bytes < insignificand_limittab[i])
      insignificand_limittab[i] = i_bytes;

    byte_count += significand_limittab[i];
    byte_count += insignificand_limittab[i];

    printf ("insignificand_limittab[%i]  == %u / %u\n",
	i, insignificand_limittab[i], i_bytes);
    printf ("  significand_limittab[%i]  == %u / %u\n",
	i, significand_limittab[i], s_bytes);

    significand_limit -= significand_limittab[i];
    insignificand_limit -= insignificand_limittab[i];
  }

  printf ("byte_count == %u\n", byte_count);

  return byte_count;
}


/**
 *  write 'em binary for now, should be easy to compress ...
 */
static uint8_t *
write_limittabs (uint8_t * bitstream,
    uint32_t significand_limittab[], uint32_t insignificand_limittab[])
{
  int i;

  for (i = 0; i < TYPE_BITS; i++) {
    *(uint32_t *) bitstream = significand_limittab[i];
    bitstream += 4;
  }

  for (i = 0; i < TYPE_BITS; i++) {
    *(uint32_t *) bitstream = insignificand_limittab[i];
    bitstream += 4;
  }

  return bitstream;
}


static uint8_t *
read_limittabs (uint8_t * bitstream,
    uint32_t significand_limittab[], uint32_t insignificand_limittab[])
{
  int i;

  for (i = 0; i < TYPE_BITS; i++) {
    significand_limittab[i] = *(uint32_t *) bitstream;
    printf ("significand_limittab[%i]  == %u\n", i, significand_limittab[i]);
    bitstream += 4;
  }

  for (i = 0; i < TYPE_BITS; i++) {
    insignificand_limittab[i] = *(uint32_t *) bitstream;
    printf ("insignificand_limittab[%i]  == %u\n", i,
	insignificand_limittab[i]);
    bitstream += 4;
  }

  return bitstream;
}


/**
 *  concatenate entropy coder bitstreams
 */
static void
merge_bitstreams (uint8_t * bitstream,
    ENTROPY_CODER significand_bitstream[],
    ENTROPY_CODER insignificand_bitstream[],
    uint32_t significand_limittab[], uint32_t insignificand_limittab[])
{
  int i;

  for (i = TYPE_BITS - 1; i >= 0; i--) {
    memcpy (bitstream,
	ENTROPY_CODER_BITSTREAM (&significand_bitstream[i]),
	significand_limittab[i]);

    bitstream += significand_limittab[i];
  }

  for (i = TYPE_BITS - 1; i >= 0; i--) {
    memcpy (bitstream,
	ENTROPY_CODER_BITSTREAM (&insignificand_bitstream[i]),
	insignificand_limittab[i]);

    bitstream += insignificand_limittab[i];
  }
}


static void
split_bitstreams (uint8_t * bitstream,
    ENTROPY_CODER significand_bitstream[],
    ENTROPY_CODER insignificand_bitstream[],
    uint32_t significand_limittab[], uint32_t insignificand_limittab[])
{
  uint32_t byte_count;
  int i;

  for (i = TYPE_BITS - 1; i >= 0; i--) {
    byte_count = significand_limittab[i];
    ENTROPY_DECODER_INIT (&significand_bitstream[i], bitstream, byte_count);
    bitstream += byte_count;
  }

  for (i = TYPE_BITS - 1; i >= 0; i--) {
    byte_count = insignificand_limittab[i];
    ENTROPY_DECODER_INIT (&insignificand_bitstream[i], bitstream, byte_count);
    bitstream += byte_count;
  }
}


int
wavelet_3d_buf_encode_coeff (const Wavelet3DBuf * buf,
    uint8_t * bitstream, uint32_t limit)
{
  ENTROPY_CODER significand_bitstream[TYPE_BITS];
  ENTROPY_CODER insignificand_bitstream[TYPE_BITS];
  uint32_t significand_limittab[TYPE_BITS];
  uint32_t insignificand_limittab[TYPE_BITS];
  uint32_t byte_count;
  int i;

  for (i = 0; i < TYPE_BITS; i++)
    ENTROPY_ENCODER_INIT (&significand_bitstream[i], limit);
  for (i = 0; i < TYPE_BITS; i++)
    ENTROPY_ENCODER_INIT (&insignificand_bitstream[i], limit);

  encode_coefficients (buf, significand_bitstream, insignificand_bitstream);

  byte_count = setup_limittabs (significand_bitstream, insignificand_bitstream,
      significand_limittab, insignificand_limittab, limit);

  bitstream = write_limittabs (bitstream,
      significand_limittab, insignificand_limittab);

  merge_bitstreams (bitstream, significand_bitstream, insignificand_bitstream,
      significand_limittab, insignificand_limittab);

  for (i = 0; i < TYPE_BITS; i++) {
    ENTROPY_ENCODER_DONE (&significand_bitstream[i]);
    ENTROPY_ENCODER_DONE (&insignificand_bitstream[i]);
  }

  return byte_count;
}


void
wavelet_3d_buf_decode_coeff (Wavelet3DBuf * buf,
    uint8_t * bitstream, uint32_t byte_count)
{
  ENTROPY_CODER significand_bitstream[TYPE_BITS];
  ENTROPY_CODER insignificand_bitstream[TYPE_BITS];
  uint32_t significand_limittab[TYPE_BITS];
  uint32_t insignificand_limittab[TYPE_BITS];
  int i;

  memset (buf->data, 0, buf->width * buf->height * buf->frames * sizeof (TYPE));

  bitstream = read_limittabs (bitstream,
      significand_limittab, insignificand_limittab);

  split_bitstreams (bitstream, significand_bitstream, insignificand_bitstream,
      significand_limittab, insignificand_limittab);

  decode_coefficients (buf, significand_bitstream, insignificand_bitstream);

  for (i = 0; i < TYPE_BITS; i++) {
    ENTROPY_DECODER_DONE (&significand_bitstream[i]);
    ENTROPY_DECODER_DONE (&insignificand_bitstream[i]);
  }
}
