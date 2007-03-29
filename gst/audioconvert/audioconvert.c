/* GStreamer
 * Copyright (C) 2005 Wim Taymans <wim at fluendo dot com>
 *
 * audioconvert.c: Convert audio to different audio formats automatically
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

#include <string.h>

#include "gstchannelmix.h"
#include "audioconvert.h"
#include "gst/floatcast/floatcast.h"

/* int to float/double conversion: int2xxx(i) = 1 / (2^31-1) * i */
#define INT2FLOAT(i) (4.6566128752457969e-10 * ((gfloat)i))
#define INT2DOUBLE(i) (4.6566128752457969e-10 * ((gdouble)i))

/* sign bit in the intermediate format */
#define SIGNED  (1U<<31)

/*** 
 * unpack code
 */
#define MAKE_UNPACK_FUNC_NAME(name)                                     \
audio_convert_unpack_##name

/* unpack from integer to signed integer 32 */
#define MAKE_UNPACK_FUNC_II(name, stride, sign, READ_FUNC)                 \
static void                                                             \
MAKE_UNPACK_FUNC_NAME (name) (guint8 *src, gint32 *dst,                 \
        gint scale, gint count)                                         \
{                                                                       \
  for (;count; count--) {                                               \
    *dst++ = (((gint32) READ_FUNC (src)) << scale) ^ (sign);            \
    src+=stride;                                                        \
  }                                                                     \
}

/* unpack from float to signed integer 32 */
#define MAKE_UNPACK_FUNC_FI(name, type, READ_FUNC)                            \
static void                                                                   \
MAKE_UNPACK_FUNC_NAME (name) (type * src, gint32 * dst, gint s, gint count)   \
{                                                                             \
  gdouble temp;                                                               \
                                                                              \
  for (; count; count--) {                                                    \
    /* blow up to 32 bit */                                                   \
    temp = (READ_FUNC (*src++) * 2147483647.0) + 0.5;                         \
    *dst++ = (gint32) CLAMP (temp, G_MININT32, G_MAXINT32);                   \
  }                                                                           \
}

/* unpack from float to float 64 (double) */
#define MAKE_UNPACK_FUNC_FF(name, type, FUNC)                                 \
static void                                                                   \
MAKE_UNPACK_FUNC_NAME (name) (type * src, gdouble * dst, gint s,              \
    gint count)                                                               \
{                                                                             \
  for (; count; count--)                                                      \
    *dst++ = (gdouble) FUNC (*src++);                                         \
}

#define READ8(p)          GST_READ_UINT8(p)
#define READ16_FROM_LE(p) GST_READ_UINT16_LE (p)
#define READ16_FROM_BE(p) GST_READ_UINT16_BE (p)
#define READ24_FROM_LE(p) (p[0] | (p[1] << 8) | (p[2] << 16))
#define READ24_FROM_BE(p) (p[2] | (p[1] << 8) | (p[0] << 16))
#define READ32_FROM_LE(p) GST_READ_UINT32_LE (p)
#define READ32_FROM_BE(p) GST_READ_UINT32_BE (p)

MAKE_UNPACK_FUNC_II (u8, 1, SIGNED, READ8);
MAKE_UNPACK_FUNC_II (s8, 1, 0, READ8);
MAKE_UNPACK_FUNC_II (u16_le, 2, SIGNED, READ16_FROM_LE);
MAKE_UNPACK_FUNC_II (s16_le, 2, 0, READ16_FROM_LE);
MAKE_UNPACK_FUNC_II (u16_be, 2, SIGNED, READ16_FROM_BE);
MAKE_UNPACK_FUNC_II (s16_be, 2, 0, READ16_FROM_BE);
MAKE_UNPACK_FUNC_II (u24_le, 3, SIGNED, READ24_FROM_LE);
MAKE_UNPACK_FUNC_II (s24_le, 3, 0, READ24_FROM_LE);
MAKE_UNPACK_FUNC_II (u24_be, 3, SIGNED, READ24_FROM_BE);
MAKE_UNPACK_FUNC_II (s24_be, 3, 0, READ24_FROM_BE);
MAKE_UNPACK_FUNC_II (u32_le, 4, SIGNED, READ32_FROM_LE);
MAKE_UNPACK_FUNC_II (s32_le, 4, 0, READ32_FROM_LE);
MAKE_UNPACK_FUNC_II (u32_be, 4, SIGNED, READ32_FROM_BE);
MAKE_UNPACK_FUNC_II (s32_be, 4, 0, READ32_FROM_BE);
MAKE_UNPACK_FUNC_FI (float_le, gfloat, GFLOAT_FROM_LE);
MAKE_UNPACK_FUNC_FI (float_be, gfloat, GFLOAT_FROM_BE);
MAKE_UNPACK_FUNC_FI (double_le, gdouble, GDOUBLE_FROM_LE);
MAKE_UNPACK_FUNC_FI (double_be, gdouble, GDOUBLE_FROM_BE);
MAKE_UNPACK_FUNC_FF (float_hq_le, gfloat, GFLOAT_FROM_LE);
MAKE_UNPACK_FUNC_FF (float_hq_be, gfloat, GFLOAT_FROM_BE);
MAKE_UNPACK_FUNC_FF (double_hq_le, gdouble, GDOUBLE_FROM_LE);
MAKE_UNPACK_FUNC_FF (double_hq_be, gdouble, GDOUBLE_FROM_BE);

/* One of the double_hq_* functions generated above is ineffecient, but it's
 * never used anyway.  The same is true for one of the s32_* functions. */

/*** 
 * packing code
 */
#define MAKE_PACK_FUNC_NAME(name)                                       \
audio_convert_pack_##name

/*
 * These functions convert the signed 32 bit integers to the
 * target format. For this to work the following steps are done:
 *
 * 1) If the output format is smaller than 32 bit we add 0.5LSB of
 *    the target format (i.e. 1<<(scale-1)) to get proper rounding.
 *    Shifting will result in rounding towards negative infinity (for
 *    signed values) or zero (for unsigned values). As we might overflow
 *    an overflow check is performed.
 *    Additionally, if our target format is signed and the value is smaller
 *    than zero we decrease it by one to round -X.5 downwards.
 *    This leads to the following rounding:
 *    -1.2 => -1    1.2 => 1
 *    -1.5 => -2    1.5 => 2
 *    -1.7 => -2    1.7 => 2
 * 2) If the output format is unsigned we will XOR the sign bit. This
 *    will do the same as if we add 1<<31.
 * 3) Afterwards we shift to the target depth. It's necessary to left-shift
 *    on signed values here to get arithmetical shifting.
 * 4) This is then written into our target array by the corresponding write
 *    function for the target width.
 */

/* pack from signed integer 32 to integer */
#define MAKE_PACK_FUNC_II(name, stride, sign, WRITE_FUNC)               \
static void                                                             \
MAKE_PACK_FUNC_NAME (name) (gint32 *src, gpointer dst,                  \
        gint scale, gint count)                                         \
{                                                                       \
  guint8 *p = (guint8 *)dst;                                            \
  gint32 tmp;                                                           \
  if (scale > 0) {                                                      \
    guint32 bias = 1 << (scale - 1);                                    \
    for (;count; count--) {                                             \
      tmp = *src++;                                                     \
      if (tmp > 0 && G_MAXINT32 - tmp < bias)                           \
        tmp = G_MAXINT32;                                               \
      else                                                              \
        tmp += bias;                                                    \
      if (sign == 0 && tmp < 0)                                         \
        tmp--;                                                          \
      tmp = ((tmp) ^ (sign)) >> scale;                                  \
      WRITE_FUNC (p, tmp);                                              \
      p+=stride;                                                        \
    }                                                                   \
  } else {                                                              \
    for (;count; count--) {                                             \
      tmp = (*src++ ^ (sign));                                          \
      WRITE_FUNC (p, tmp);                                              \
      p+=stride;                                                        \
    }                                                                   \
  }                                                                     \
}

/* pack from signed integer 32 to float */
#define MAKE_PACK_FUNC_IF(name, type, FUNC, FUNC2)                      \
static void                                                             \
MAKE_PACK_FUNC_NAME (name) (gint32 * src, type * dst, gint scale,       \
    gint count)                                                         \
{                                                                       \
  for (; count; count--)                                                \
    *dst++ = FUNC (FUNC2 (*src++));                                     \
}

/* pack from float 64 (double) to float */
#define MAKE_PACK_FUNC_FF(name, type, FUNC)                             \
static void                                                             \
MAKE_PACK_FUNC_NAME (name) (gdouble * src, type * dst, gint s,          \
    gint count)                                                         \
{                                                                       \
  for (; count; count--)                                                \
    *dst++ = FUNC ((type) (*src++));                                    \
}

#define WRITE8(p, v)       GST_WRITE_UINT8 (p, v)
#define WRITE16_TO_LE(p,v) GST_WRITE_UINT16_LE (p, (guint16)(v))
#define WRITE16_TO_BE(p,v) GST_WRITE_UINT16_BE (p, (guint16)(v))
#define WRITE24_TO_LE(p,v) p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff
#define WRITE24_TO_BE(p,v) p[2] = v & 0xff; p[1] = (v >> 8) & 0xff; p[0] = (v >> 16) & 0xff
#define WRITE32_TO_LE(p,v) GST_WRITE_UINT32_LE (p, (guint32)(v))
#define WRITE32_TO_BE(p,v) GST_WRITE_UINT32_BE (p, (guint32)(v))

MAKE_PACK_FUNC_II (u8, 1, SIGNED, WRITE8);
MAKE_PACK_FUNC_II (s8, 1, 0, WRITE8);
MAKE_PACK_FUNC_II (u16_le, 2, SIGNED, WRITE16_TO_LE);
MAKE_PACK_FUNC_II (s16_le, 2, 0, WRITE16_TO_LE);
MAKE_PACK_FUNC_II (u16_be, 2, SIGNED, WRITE16_TO_BE);
MAKE_PACK_FUNC_II (s16_be, 2, 0, WRITE16_TO_BE);
MAKE_PACK_FUNC_II (u24_le, 3, SIGNED, WRITE24_TO_LE);
MAKE_PACK_FUNC_II (s24_le, 3, 0, WRITE24_TO_LE);
MAKE_PACK_FUNC_II (u24_be, 3, SIGNED, WRITE24_TO_BE);
MAKE_PACK_FUNC_II (s24_be, 3, 0, WRITE24_TO_BE);
MAKE_PACK_FUNC_II (u32_le, 4, SIGNED, WRITE32_TO_LE);
MAKE_PACK_FUNC_II (s32_le, 4, 0, WRITE32_TO_LE);
MAKE_PACK_FUNC_II (u32_be, 4, SIGNED, WRITE32_TO_BE);
MAKE_PACK_FUNC_II (s32_be, 4, 0, WRITE32_TO_BE);
MAKE_PACK_FUNC_IF (float_le, gfloat, GFLOAT_TO_LE, INT2FLOAT);
MAKE_PACK_FUNC_IF (float_be, gfloat, GFLOAT_TO_BE, INT2FLOAT);
MAKE_PACK_FUNC_IF (double_le, gdouble, GDOUBLE_TO_LE, INT2DOUBLE);
MAKE_PACK_FUNC_IF (double_be, gdouble, GDOUBLE_TO_BE, INT2DOUBLE);
MAKE_PACK_FUNC_FF (float_hq_le, gfloat, GFLOAT_TO_LE);
MAKE_PACK_FUNC_FF (float_hq_be, gfloat, GFLOAT_TO_BE);
/* For double_hq, packing and unpacking is the same, so we reuse the unpacking
 * functions here. */
#define audio_convert_pack_double_hq_le MAKE_UNPACK_FUNC_NAME (double_hq_le)
#define audio_convert_pack_double_hq_be MAKE_UNPACK_FUNC_NAME (double_hq_be)

static AudioConvertUnpack unpack_funcs[] = {
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (u8),
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (s8),
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (u8),
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (s8),
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (u16_le),
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (s16_le),
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (u16_be),
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (s16_be),
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (u24_le),
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (s24_le),
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (u24_be),
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (s24_be),
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (u32_le),
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (s32_le),
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (u32_be),
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (s32_be),
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (float_le),
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (float_be),
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (double_le),
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (double_be),
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (float_hq_le),
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (float_hq_be),
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (double_hq_le),
  (AudioConvertUnpack) MAKE_UNPACK_FUNC_NAME (double_hq_be),
};

static AudioConvertPack pack_funcs[] = {
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (u8),
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (s8),
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (u8),
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (s8),
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (u16_le),
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (s16_le),
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (u16_be),
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (s16_be),
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (u24_le),
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (s24_le),
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (u24_be),
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (s24_be),
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (u32_le),
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (s32_le),
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (u32_be),
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (s32_be),
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (float_le),
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (float_be),
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (double_le),
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (double_be),
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (float_hq_le),
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (float_hq_be),
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (double_hq_le),
  (AudioConvertPack) MAKE_PACK_FUNC_NAME (double_hq_be),
};

static gint
audio_convert_get_func_index (AudioConvertFmt * fmt)
{
  gint index = 0;

  if (fmt->is_int) {
    index += (fmt->width / 8 - 1) * 4;
    index += fmt->endianness == G_LITTLE_ENDIAN ? 0 : 2;
    index += fmt->sign ? 1 : 0;
  } else {
    /* this is float/double */
    index = 16;
    index += (fmt->width == 32) ? 0 : 2;
    index += (fmt->endianness == G_LITTLE_ENDIAN) ? 0 : 1;
  }
  return index;
}

static gboolean
check_default (AudioConvertCtx * ctx, AudioConvertFmt * fmt)
{
  if (ctx->in.is_int || ctx->out.is_int) {
    return (fmt->width == 32 && fmt->depth == 32 &&
        fmt->endianness == G_BYTE_ORDER && fmt->sign == TRUE);
  } else {
    return (fmt->width == 64 && fmt->endianness == G_BYTE_ORDER);
  }
}

gboolean
audio_convert_clean_fmt (AudioConvertFmt * fmt)
{
  g_return_val_if_fail (fmt != NULL, FALSE);

  g_free (fmt->pos);
  fmt->pos = NULL;

  return TRUE;
}


gboolean
audio_convert_prepare_context (AudioConvertCtx * ctx, AudioConvertFmt * in,
    AudioConvertFmt * out)
{
  gint idx_in, idx_out;

  g_return_val_if_fail (ctx != NULL, FALSE);
  g_return_val_if_fail (in != NULL, FALSE);
  g_return_val_if_fail (out != NULL, FALSE);

  /* first clean the existing context */
  audio_convert_clean_context (ctx);

  ctx->in = *in;
  ctx->out = *out;

  gst_channel_mix_setup_matrix (ctx);

  idx_in = audio_convert_get_func_index (in);
  ctx->unpack = unpack_funcs[idx_in];

  idx_out = audio_convert_get_func_index (out);
  ctx->pack = pack_funcs[idx_out];

  /* if both formats are float/double use double as intermediate format and
   * and switch mixing */
  if (in->is_int || out->is_int) {
    GST_INFO ("use int mixing");
    ctx->channel_mix = (AudioConvertMix) gst_channel_mix_mix_int;
  } else {
    GST_INFO ("use float mixing");
    ctx->channel_mix = (AudioConvertMix) gst_channel_mix_mix_float;
    /* Bump the pack/unpack function indices by 4 to use double as intermediary
     * format (float_hq_*, double_hq_* functions).*/
    ctx->unpack = unpack_funcs[idx_in + 4];
    ctx->pack = pack_funcs[idx_out + 4];
  }
  GST_INFO ("unitsizes: %d -> %d", in->unit_size, out->unit_size);

  /* check if input is in default format */
  ctx->in_default = check_default (ctx, in);
  /* check if channel mixer is passthrough */
  ctx->mix_passthrough = gst_channel_mix_passthrough (ctx);
  /* check if output is in default format */
  ctx->out_default = check_default (ctx, out);

  GST_INFO ("in default %d, mix passthrough %d, out default %d",
      ctx->in_default, ctx->mix_passthrough, ctx->out_default);

  ctx->in_scale = (in->is_int) ? (32 - in->depth) : 0;
  ctx->out_scale = (out->is_int) ? (32 - out->depth) : 0;

  return TRUE;
}

gboolean
audio_convert_clean_context (AudioConvertCtx * ctx)
{
  g_return_val_if_fail (ctx != NULL, FALSE);

  audio_convert_clean_fmt (&ctx->in);
  audio_convert_clean_fmt (&ctx->out);
  gst_channel_mix_unset_matrix (ctx);

  g_free (ctx->tmpbuf);
  ctx->tmpbuf = NULL;
  ctx->tmpbufsize = 0;

  return TRUE;
}

gboolean
audio_convert_get_sizes (AudioConvertCtx * ctx, gint samples, gint * srcsize,
    gint * dstsize)
{
  g_return_val_if_fail (ctx != NULL, FALSE);

  if (srcsize)
    *srcsize = samples * ctx->in.unit_size;
  if (dstsize)
    *dstsize = samples * ctx->out.unit_size;

  return TRUE;
}

gboolean
audio_convert_convert (AudioConvertCtx * ctx, gpointer src,
    gpointer dst, gint samples, gboolean src_writable)
{
  gint insize, outsize, size;
  gpointer outbuf, tmpbuf;
  gint intemp = 0, outtemp = 0, biggest;

  g_return_val_if_fail (ctx != NULL, FALSE);
  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (dst != NULL, FALSE);
  g_return_val_if_fail (samples >= 0, FALSE);

  if (samples == 0)
    return TRUE;

  insize = ctx->in.unit_size * samples;
  outsize = ctx->out.unit_size * samples;

  /* find biggest temp buffer size */
  size = (ctx->in.is_int || ctx->out.is_int) ?
      sizeof (gint32) : sizeof (gdouble);

  if (!ctx->in_default)
    intemp = insize * size * 8 / ctx->in.width;
  if (!ctx->mix_passthrough)
    outtemp = outsize * size * 8 / ctx->out.width;
  biggest = MAX (intemp, outtemp);

  /* see if one of the buffers can be used as temp */
  if ((outsize >= biggest) && (ctx->out.unit_size <= size))
    tmpbuf = dst;
  else if ((insize >= biggest) && src_writable && (ctx->in.unit_size >= size))
    tmpbuf = src;
  else {
    if (biggest > ctx->tmpbufsize) {
      ctx->tmpbuf = g_realloc (ctx->tmpbuf, biggest);
      ctx->tmpbufsize = biggest;
    }
    tmpbuf = ctx->tmpbuf;
  }

  /* start conversion */
  if (!ctx->in_default) {
    /* check if final conversion */
    if (!(ctx->out_default && ctx->mix_passthrough))
      outbuf = tmpbuf;
    else
      outbuf = dst;

    /* unpack to default format */
    ctx->unpack (src, outbuf, ctx->in_scale, samples * ctx->in.channels);

    src = outbuf;
  }

  if (!ctx->mix_passthrough) {
    /* check if final conversion */
    if (!ctx->out_default)
      outbuf = tmpbuf;
    else
      outbuf = dst;

    /* convert channels */
    ctx->channel_mix (ctx, src, outbuf, samples);

    src = outbuf;
  }

  if (!ctx->out_default) {
    /* pack default format into dst */
    ctx->pack (src, dst, ctx->out_scale, samples * ctx->out.channels);
  }

  return TRUE;
}
