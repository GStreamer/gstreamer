/* GStreamer
 * Copyright (C) 2002 Erik Walthinsen <omega@cse.ogi.edu>
 *               2002 Wim Taymans <wim.taymans@chello.be>
 *
 * gstmd5sink.c: A sink computing an md5 checksum from a stream
 *
 * The md5 code was taken from glibc-2.2.3/crypt/md5.c and slightly
 * modified.
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

#include <string.h>

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>
#include "gstmd5sink.h"

GST_DEBUG_CATEGORY_STATIC (gst_md5sink_debug);
#define GST_CAT_DEFAULT gst_md5sink_debug

GstElementDetails gst_md5sink_details = GST_ELEMENT_DETAILS ("MD5 Sink",
    "Sink",
    "compute MD5 for incoming data",
    "Benjamin Otte <in7y118@public.uni-hamburg.de>");

/* MD5Sink signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_MD5,
  /* FILL ME */
};

GstStaticPadTemplate md5_sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_md5sink_debug, "md5sink", 0, "md5sink element");

GST_BOILERPLATE_FULL (GstMD5Sink, gst_md5sink, GstElement, GST_TYPE_ELEMENT,
    _do_init);

/* GObject stuff */
/*static void			gst_md5sink_set_property	(GObject *object, guint prop_id, 
							 const GValue *value, GParamSpec *pspec);*/
static void gst_md5sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_md5sink_chain (GstPad * pad, GstData * _data);
static GstElementStateReturn gst_md5sink_change_state (GstElement * element);


/* MD5 stuff */
static void md5_init_ctx (GstMD5Sink * ctx);
static gpointer md5_read_ctx (GstMD5Sink * ctx, gpointer resbuf);
static gpointer md5_finish_ctx (GstMD5Sink * ctx, gpointer resbuf);
static void md5_process_bytes (const void *buffer, size_t len,
    GstMD5Sink * ctx);
static void md5_process_block (const void *buffer, size_t len,
    GstMD5Sink * ctx);

/* This array contains the bytes used to pad the buffer to the next
   64-byte boundary.  (RFC 1321, 3.1: Step 1)  */
static const guchar fillbuf[64] = { 0x80, 0 /* , 0, 0, ...  */  };

/* MD5 functions */
/* Initialize structure containing state of computation.
   (RFC 1321, 3.3: Step 3)  */
static void
md5_init_ctx (GstMD5Sink * ctx)
{
  ctx->A = 0x67452301;
  ctx->B = 0xefcdab89;
  ctx->C = 0x98badcfe;
  ctx->D = 0x10325476;

  ctx->total[0] = ctx->total[1] = 0;
  ctx->buflen = 0;
}

/* Process the remaining bytes in the internal buffer and the usual
   prolog according to the standard and write the result to RESBUF.

   IMPORTANT: On some systems it is required that RESBUF is correctly
   aligned for a 32 bits value.  */
static gpointer
md5_finish_ctx (GstMD5Sink * ctx, gpointer resbuf)
{
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

  return md5_read_ctx (ctx, resbuf);
}

/* Put result from CTX in first 16 bytes following RESBUF.  The result
   must be in little endian byte order.

   IMPORTANT: On some systems it is required that RESBUF is correctly
   aligned for a 32 bits value.  */
static gpointer
md5_read_ctx (GstMD5Sink * ctx, gpointer resbuf)
{
  ((guint32 *) resbuf)[0] = GUINT32_TO_LE (ctx->A);
  ((guint32 *) resbuf)[1] = GUINT32_TO_LE (ctx->B);
  ((guint32 *) resbuf)[2] = GUINT32_TO_LE (ctx->C);
  ((guint32 *) resbuf)[3] = GUINT32_TO_LE (ctx->D);

  return resbuf;
}

static void
md5_process_bytes (const void *buffer, size_t len, GstMD5Sink * ctx)
{
  /*const void aligned_buffer = buffer; */

  /* When we already have some bits in our internal buffer concatenate
     both inputs first.  */
  if (ctx->buflen != 0) {
    size_t left_over = ctx->buflen;
    size_t add = 128 - left_over > len ? len : 128 - left_over;

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

/* Process LEN bytes of BUFFER, accumulating context into CTX.
   It is assumed that LEN % 64 == 0.  */
static void
md5_process_block (const void *buffer, size_t len, GstMD5Sink * ctx)
{
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
gst_md5sink_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstelement_class, &gst_md5sink_details);
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&md5_sink_template));
}

static void
gst_md5sink_class_init (GstMD5SinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;


  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_md5sink_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MD5,
      g_param_spec_string ("md5", "md5", "current value of the md5 sum",
          "", G_PARAM_READABLE));

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_md5sink_change_state);
}

static void
gst_md5sink_init (GstMD5Sink * md5sink)
{
  GstPad *pad;

  pad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&md5_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (md5sink), pad);
  gst_pad_set_chain_function (pad, GST_DEBUG_FUNCPTR (gst_md5sink_chain));

  md5_init_ctx (md5sink);
}

static GstElementStateReturn
gst_md5sink_change_state (GstElement * element)
{
  GstMD5Sink *sink;

  /* element check */
  sink = GST_MD5SINK (element);

  g_return_val_if_fail (GST_IS_MD5SINK (sink), GST_STATE_FAILURE);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      md5_init_ctx (sink);
      g_object_notify (G_OBJECT (element), "md5");
      break;
    case GST_STATE_PAUSED_TO_READY:
      md5_finish_ctx (sink, sink->md5);
      g_object_notify (G_OBJECT (element), "md5");
      break;
    default:
      break;
  }

  if ((GST_ELEMENT_CLASS (parent_class)->change_state))
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_md5sink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMD5Sink *sink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_MD5SINK (object));

  sink = GST_MD5SINK (object);

  switch (prop_id) {
    case ARG_MD5:
    {
      /* you could actually get a value for the current md5. 
       * This is currently disabled.
       * md5_read_ctx (sink, sink->md5); */
      /* md5 is a guchar[16] */
      int i;
      guchar *md5string = g_malloc0 (33);

      for (i = 0; i < 16; ++i)
        sprintf (md5string + i * 2, "%02x", sink->md5[i]);
      g_value_set_string (value, md5string);
      g_free (md5string);
    }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_md5sink_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstMD5Sink *md5sink;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  md5sink = GST_MD5SINK (gst_pad_get_parent (pad));

  if (GST_IS_BUFFER (buf)) {
    md5_process_bytes (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf), md5sink);
  }

  gst_buffer_unref (buf);
}
