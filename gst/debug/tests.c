/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <otte@gnome.org>
 *
 * includes code based on glibc 2.2.3's crypt/md5.c,
 * Copyright (C) 1995, 1996, 1997, 1999, 2000 Free Software Foundation, Inc. 
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "tests.h"
#include <stdlib.h>
#include <string.h>


/*
 *** LENGTH ***
 */

typedef struct
{
  gint64 value;
}
LengthTest;

static GParamSpec *
length_get_spec (const GstTestInfo * info, gboolean compare_value)
{
  if (compare_value) {
    return g_param_spec_int64 ("expected-length", "expected length",
        "expected length of stream", -1, G_MAXINT64, -1,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
  } else {
    return g_param_spec_int64 ("length", "length", "length of stream",
        -1, G_MAXINT64, -1, G_PARAM_READABLE);
  }
}

static gpointer
length_new (const GstTestInfo * info)
{
  return g_new0 (LengthTest, 1);
}

static void
length_add (gpointer test, GstBuffer * buffer)
{
  LengthTest *t = test;

  t->value += GST_BUFFER_SIZE (buffer);
}

static gboolean
length_finish (gpointer test, GValue * value)
{
  LengthTest *t = test;

  if (g_value_get_int64 (value) == -1)
    return TRUE;

  return t->value == g_value_get_int64 (value);
}

static void
length_get_value (gpointer test, GValue * value)
{
  LengthTest *t = test;

  g_value_set_int64 (value, t ? t->value : -1);
}

/*
 *** BUFFER COUNT ***
 */

static GParamSpec *
buffer_count_get_spec (const GstTestInfo * info, gboolean compare_value)
{
  if (compare_value) {
    return g_param_spec_int64 ("expected-buffer-count", "expected buffer count",
        "expected number of buffers in stream",
        -1, G_MAXINT64, -1, G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
  } else {
    return g_param_spec_int64 ("buffer-count", "buffer count",
        "number of buffers in stream", -1, G_MAXINT64, -1, G_PARAM_READABLE);
  }
}

static void
buffer_count_add (gpointer test, GstBuffer * buffer)
{
  LengthTest *t = test;

  t->value++;
}

/*
 *** TIMESTAMP / DURATION MATCHING ***
 */

typedef struct
{
  guint64 diff;
  guint count;
  GstClockTime expected;
}
TimeDurTest;

static GParamSpec *
timedur_get_spec (const GstTestInfo * info, gboolean compare_value)
{
  if (compare_value) {
    return g_param_spec_int64 ("allowed-timestamp-deviation",
        "allowed timestamp deviation",
        "allowed average difference in usec between timestamp of next buffer "
        "and expected timestamp from analyzing last buffer",
        -1, G_MAXINT64, -1, G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
  } else {
    return g_param_spec_int64 ("timestamp-deviation",
        "timestamp deviation",
        "average difference in usec between timestamp of next buffer "
        "and expected timestamp from analyzing last buffer",
        -1, G_MAXINT64, -1, G_PARAM_READABLE);
  }
}

static gpointer
timedur_new (const GstTestInfo * info)
{
  TimeDurTest *ret = g_new0 (TimeDurTest, 1);

  ret->expected = GST_CLOCK_TIME_NONE;

  return ret;
}

static void
timedur_add (gpointer test, GstBuffer * buffer)
{
  TimeDurTest *t = test;

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer) &&
      GST_CLOCK_TIME_IS_VALID (t->expected)) {
    t->diff += labs (GST_BUFFER_TIMESTAMP (buffer) - t->expected);
    t->count++;
  }
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer) &&
      GST_BUFFER_DURATION_IS_VALID (buffer)) {
    t->expected = GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer);
  } else {
    t->expected = GST_CLOCK_TIME_NONE;
  }
}

static gboolean
timedur_finish (gpointer test, GValue * value)
{
  TimeDurTest *t = test;

  if (g_value_get_int64 (value) == -1)
    return TRUE;

  return (t->diff / MAX (1, t->count)) <= g_value_get_int64 (value);
}

static void
timedur_get_value (gpointer test, GValue * value)
{
  TimeDurTest *t = test;

  g_value_set_int64 (value, t ? (t->diff / MAX (1, t->count)) : -1);
}

/*
 *** MD5 ***
 */

typedef struct
{
  /* md5 information */
  guint32 A;
  guint32 B;
  guint32 C;
  guint32 D;

  guint32 total[2];
  guint32 buflen;
  gchar buffer[128];

  gchar result[33];
}
MD5Test;

static void md5_process_block (const void *buffer, size_t len, MD5Test * ctx);
static void md5_read_ctx (MD5Test * ctx, gchar * result);

static GParamSpec *
md5_get_spec (const GstTestInfo * info, gboolean compare_value)
{
  if (compare_value) {
    return g_param_spec_string ("expected-md5", "expected md5",
        "expected md5 of processing the whole data",
        "---", G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
  } else {
    return g_param_spec_string ("md5", "md5",
        "md5 of processing the whole data", "---", G_PARAM_READABLE);
  }
}

/* This array contains the bytes used to pad the buffer to the next
   64-byte boundary.  (RFC 1321, 3.1: Step 1)  */
static const guchar fillbuf[64] = { 0x80, 0 /* , 0, 0, ...  */  };

/* MD5 functions */
/* Initialize structure containing state of computation.
   (RFC 1321, 3.3: Step 3)  */
static gpointer
md5_new (const GstTestInfo * info)
{
  MD5Test *ctx = g_new (MD5Test, 1);

  ctx->A = 0x67452301;
  ctx->B = 0xefcdab89;
  ctx->C = 0x98badcfe;
  ctx->D = 0x10325476;

  ctx->total[0] = ctx->total[1] = 0;
  ctx->buflen = 0;

  memset (ctx->result, 0, 33);

  return ctx;
}

/* Process the remaining bytes in the internal buffer and the usual
   prolog according to the standard and write the result to RESBUF.

   IMPORTANT: On some systems it is required that RESBUF is correctly
   aligned for a 32 bits value.  */
static gboolean
md5_finish (gpointer test, GValue * value)
{
  MD5Test *ctx = test;
  const gchar *str_val = g_value_get_string (value);

  /* Take yet unprocessed bytes into account.  */
  guint32 bytes = ctx->buflen;
  size_t pad;

  /* Now count remaining bytes.  */
  ctx->total[0] += bytes;
  if (ctx->total[0] < bytes)
    ++ctx->total[1];

  pad = bytes >= 56 ? 64 + 56 - bytes : 56 - bytes;
  memcpy (&ctx->buffer[bytes], fillbuf, pad);

  /* Put the 64-bit file length in *bits* at the end of the buffer.  */
  *(guint32 *) & ctx->buffer[bytes + pad] = GUINT32_TO_LE (ctx->total[0] << 3);
  *(guint32 *) & ctx->buffer[bytes + pad + 4] =
      GUINT32_TO_LE ((ctx->total[1] << 3) | (ctx->total[0] >> 29));

  /* Process last bytes.  */
  md5_process_block (ctx->buffer, bytes + pad + 8, ctx);

  md5_read_ctx (ctx, ctx->result);
  if (g_str_equal (str_val, "---"))
    return TRUE;
  if (g_str_equal (str_val, ctx->result))
    return TRUE;
  return FALSE;
}

/* Put result from CTX in first 16 bytes following RESBUF.  The result
   must be in little endian byte order.

   IMPORTANT: On some systems it is required that RESBUF is correctly
   aligned for a 32 bits value.  */
static void
md5_read_ctx (MD5Test * ctx, gchar * result)
{
  guint32 resbuf[4];
  guint i;

  resbuf[0] = GUINT32_TO_LE (ctx->A);
  resbuf[1] = GUINT32_TO_LE (ctx->B);
  resbuf[2] = GUINT32_TO_LE (ctx->C);
  resbuf[3] = GUINT32_TO_LE (ctx->D);

  for (i = 0; i < 16; i++)
    sprintf (result + i * 2, "%02x", ((guchar *) resbuf)[i]);
}

static void
md5_add (gpointer test, GstBuffer * gstbuffer)
{
  const void *buffer = GST_BUFFER_DATA (gstbuffer);
  gsize len = GST_BUFFER_SIZE (gstbuffer);
  MD5Test *ctx = test;

  /*const void aligned_buffer = buffer; */

  /* When we already have some bits in our internal buffer concatenate
     both inputs first.  */
  if (ctx->buflen != 0) {
    gsize left_over = ctx->buflen;
    gsize add = 128 - left_over > len ? len : 128 - left_over;

    /* Only put full words in the buffer.  */
    /* Forcing alignment here appears to be only an optimization.
     * The glibc source uses __alignof__, which seems to be a
     * gratuitous usage of a GCC extension, when sizeof() will
     * work fine.  (And don't question the sanity of using
     * sizeof(guint32) instead of 4. */
    /* add -= add % __alignof__ (guint32); */
    add -= add % sizeof (guint32);

    memcpy (&ctx->buffer[left_over], buffer, add);
    ctx->buflen += add;

    if (ctx->buflen > 64) {
      md5_process_block (ctx->buffer, ctx->buflen & ~63, ctx);

      ctx->buflen &= 63;
      /* The regions in the following copy operation cannot overlap.  */
      memcpy (ctx->buffer, &ctx->buffer[(left_over + add) & ~63], ctx->buflen);
    }

    buffer = (const char *) buffer + add;
    len -= add;
  }

  /* Process available complete blocks.  */
  if (len > 64) {
    md5_process_block (buffer, len & ~63, ctx);
    buffer = (const char *) buffer + (len & ~63);
    len &= 63;
  }

  /* Move remaining bytes in internal buffer.  */
  if (len > 0) {
    size_t left_over = ctx->buflen;

    memcpy (&ctx->buffer[left_over], buffer, len);
    left_over += len;
    if (left_over >= 64) {
      md5_process_block (ctx->buffer, 64, ctx);
      left_over -= 64;
      memcpy (ctx->buffer, &ctx->buffer[64], left_over);
    }
    ctx->buflen = left_over;
  }
}


/* These are the four functions used in the four steps of the MD5 algorithm
   and defined in the RFC 1321.  The first function is a little bit optimized
   (as found in Colin Plumbs public domain implementation).  */
/* #define FF(b, c, d) ((b & c) | (~b & d)) */
#define FF(b, c, d) (d ^ (b & (c ^ d)))
#define FG(b, c, d) FF (d, b, c)
#define FH(b, c, d) (b ^ c ^ d)
#define FI(b, c, d) (c ^ (b | ~d))

static void
md5_process_block (const void *buffer, size_t len, MD5Test * ctx)
{
/* Process LEN bytes of BUFFER, accumulating context into CTX.
   It is assumed that LEN % 64 == 0.  */
  guint32 correct_words[16];
  const guint32 *words = buffer;
  size_t nwords = len / sizeof (guint32);
  const guint32 *endp = words + nwords;
  guint32 A = ctx->A;
  guint32 B = ctx->B;
  guint32 C = ctx->C;
  guint32 D = ctx->D;

  /* First increment the byte count.  RFC 1321 specifies the possible
     length of the file up to 2^64 bits.  Here we only compute the
     number of bytes.  Do a double word increment.  */
  ctx->total[0] += len;
  if (ctx->total[0] < len)
    ++ctx->total[1];

  /* Process all bytes in the buffer with 64 bytes in each round of
     the loop.  */
  while (words < endp) {
    guint32 *cwp = correct_words;
    guint32 A_save = A;
    guint32 B_save = B;
    guint32 C_save = C;
    guint32 D_save = D;

    /* First round: using the given function, the context and a constant
       the next context is computed.  Because the algorithms processing
       unit is a 32-bit word and it is determined to work on words in
       little endian byte order we perhaps have to change the byte order
       before the computation.  To reduce the work for the next steps
       we store the swapped words in the array CORRECT_WORDS.  */

#define OP(a, b, c, d, s, T)						\
      do								\
        {								\
	  a += FF (b, c, d) + (*cwp++ = GUINT32_TO_LE (*words)) + T;		\
	  ++words;							\
	  CYCLIC (a, s);						\
	  a += b;							\
        }								\
      while (0)

    /* It is unfortunate that C does not provide an operator for
       cyclic rotation.  Hope the C compiler is smart enough.  */
#define CYCLIC(w, s) (w = (w << s) | (w >> (32 - s)))

    /* Before we start, one word to the strange constants.
       They are defined in RFC 1321 as

       T[i] = (int) (4294967296.0 * fabs (sin (i))), i=1..64
     */

    /* Round 1.  */
    OP (A, B, C, D, 7, 0xd76aa478);
    OP (D, A, B, C, 12, 0xe8c7b756);
    OP (C, D, A, B, 17, 0x242070db);
    OP (B, C, D, A, 22, 0xc1bdceee);
    OP (A, B, C, D, 7, 0xf57c0faf);
    OP (D, A, B, C, 12, 0x4787c62a);
    OP (C, D, A, B, 17, 0xa8304613);
    OP (B, C, D, A, 22, 0xfd469501);
    OP (A, B, C, D, 7, 0x698098d8);
    OP (D, A, B, C, 12, 0x8b44f7af);
    OP (C, D, A, B, 17, 0xffff5bb1);
    OP (B, C, D, A, 22, 0x895cd7be);
    OP (A, B, C, D, 7, 0x6b901122);
    OP (D, A, B, C, 12, 0xfd987193);
    OP (C, D, A, B, 17, 0xa679438e);
    OP (B, C, D, A, 22, 0x49b40821);

    /* For the second to fourth round we have the possibly swapped words
       in CORRECT_WORDS.  Redefine the macro to take an additional first
       argument specifying the function to use.  */
#undef OP
#define OP(f, a, b, c, d, k, s, T)					\
      do 								\
	{								\
	  a += f (b, c, d) + correct_words[k] + T;			\
	  CYCLIC (a, s);						\
	  a += b;							\
	}								\
      while (0)

    /* Round 2.  */
    OP (FG, A, B, C, D, 1, 5, 0xf61e2562);
    OP (FG, D, A, B, C, 6, 9, 0xc040b340);
    OP (FG, C, D, A, B, 11, 14, 0x265e5a51);
    OP (FG, B, C, D, A, 0, 20, 0xe9b6c7aa);
    OP (FG, A, B, C, D, 5, 5, 0xd62f105d);
    OP (FG, D, A, B, C, 10, 9, 0x02441453);
    OP (FG, C, D, A, B, 15, 14, 0xd8a1e681);
    OP (FG, B, C, D, A, 4, 20, 0xe7d3fbc8);
    OP (FG, A, B, C, D, 9, 5, 0x21e1cde6);
    OP (FG, D, A, B, C, 14, 9, 0xc33707d6);
    OP (FG, C, D, A, B, 3, 14, 0xf4d50d87);
    OP (FG, B, C, D, A, 8, 20, 0x455a14ed);
    OP (FG, A, B, C, D, 13, 5, 0xa9e3e905);
    OP (FG, D, A, B, C, 2, 9, 0xfcefa3f8);
    OP (FG, C, D, A, B, 7, 14, 0x676f02d9);
    OP (FG, B, C, D, A, 12, 20, 0x8d2a4c8a);

    /* Round 3.  */
    OP (FH, A, B, C, D, 5, 4, 0xfffa3942);
    OP (FH, D, A, B, C, 8, 11, 0x8771f681);
    OP (FH, C, D, A, B, 11, 16, 0x6d9d6122);
    OP (FH, B, C, D, A, 14, 23, 0xfde5380c);
    OP (FH, A, B, C, D, 1, 4, 0xa4beea44);
    OP (FH, D, A, B, C, 4, 11, 0x4bdecfa9);
    OP (FH, C, D, A, B, 7, 16, 0xf6bb4b60);
    OP (FH, B, C, D, A, 10, 23, 0xbebfbc70);
    OP (FH, A, B, C, D, 13, 4, 0x289b7ec6);
    OP (FH, D, A, B, C, 0, 11, 0xeaa127fa);
    OP (FH, C, D, A, B, 3, 16, 0xd4ef3085);
    OP (FH, B, C, D, A, 6, 23, 0x04881d05);
    OP (FH, A, B, C, D, 9, 4, 0xd9d4d039);
    OP (FH, D, A, B, C, 12, 11, 0xe6db99e5);
    OP (FH, C, D, A, B, 15, 16, 0x1fa27cf8);
    OP (FH, B, C, D, A, 2, 23, 0xc4ac5665);

    /* Round 4.  */
    OP (FI, A, B, C, D, 0, 6, 0xf4292244);
    OP (FI, D, A, B, C, 7, 10, 0x432aff97);
    OP (FI, C, D, A, B, 14, 15, 0xab9423a7);
    OP (FI, B, C, D, A, 5, 21, 0xfc93a039);
    OP (FI, A, B, C, D, 12, 6, 0x655b59c3);
    OP (FI, D, A, B, C, 3, 10, 0x8f0ccc92);
    OP (FI, C, D, A, B, 10, 15, 0xffeff47d);
    OP (FI, B, C, D, A, 1, 21, 0x85845dd1);
    OP (FI, A, B, C, D, 8, 6, 0x6fa87e4f);
    OP (FI, D, A, B, C, 15, 10, 0xfe2ce6e0);
    OP (FI, C, D, A, B, 6, 15, 0xa3014314);
    OP (FI, B, C, D, A, 13, 21, 0x4e0811a1);
    OP (FI, A, B, C, D, 4, 6, 0xf7537e82);
    OP (FI, D, A, B, C, 11, 10, 0xbd3af235);
    OP (FI, C, D, A, B, 2, 15, 0x2ad7d2bb);
    OP (FI, B, C, D, A, 9, 21, 0xeb86d391);

    /* Add the starting values of the context.  */
    A += A_save;
    B += B_save;
    C += C_save;
    D += D_save;
  }

  /* Put checksum in context given as argument.  */
  ctx->A = A;
  ctx->B = B;
  ctx->C = C;
  ctx->D = D;
}

static void
md5_get_value (gpointer test, GValue * value)
{
  MD5Test *ctx = test;

  if (ctx->result[0] == 0) {
    gchar *str = g_new (gchar, 33);

    str[32] = 0;
    md5_read_ctx (ctx, str);
    g_value_set_string_take_ownership (value, str);
  } else {
    g_value_set_string (value, ctx->result);
  }
}

/*
 *** TESTINFO ***
 */

const GstTestInfo tests[] = {
  {length_get_spec, length_new, length_add,
      length_finish, length_get_value, g_free},
  {buffer_count_get_spec, length_new, buffer_count_add,
      length_finish, length_get_value, g_free},
  {timedur_get_spec, timedur_new, timedur_add,
      timedur_finish, timedur_get_value, g_free},
  {md5_get_spec, md5_new, md5_add,
      md5_finish, md5_get_value, g_free}
};
