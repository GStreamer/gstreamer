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

/* int to float conversion: int2float(i) = 1 / (2^31-1) * i */
#define INT2FLOAT(i) (4.6566128752457969e-10 * ((gfloat)i))

/* sign bit in the intermediate format */
#define SIGNED	(1<<31)

/*** 
 * unpack code
 */
#define MAKE_UNPACK_FUNC_NAME(name)					\
audio_convert_unpack_##name

#define MAKE_UNPACK_FUNC(name, stride, sign, READ_FUNC)			\
static void								\
MAKE_UNPACK_FUNC_NAME (name) (gpointer src, gint32 *dst,  		\
	gint scale, gint count)						\
{									\
  guint8* p = (guint8 *) src;						\
  for (;count; count--) {						\
    *dst++ = (((gint32) READ_FUNC (p)) << scale) ^ (sign);		\
    p+=stride;								\
  }									\
}

/* special unpack code for float */
static void
MAKE_UNPACK_FUNC_NAME (float) (gpointer src, gint32 * dst,
    gint scale, gint count)
{
  gfloat *p = (gfloat *) src;
  gfloat temp;

  for (; count; count--) {
    temp = *p++ * 2147483647.0f + .5;
    *dst++ = (gint32) CLAMP (temp, G_MININT, G_MAXINT);
  }
}

#define READ8(p)          GST_READ_UINT8(p)
#define READ16_FROM_LE(p) GST_READ_UINT16_LE (p)
#define READ16_FROM_BE(p) GST_READ_UINT16_BE (p)
#define READ24_FROM_LE(p) (p[0] | (p[1] << 8) | (p[2] << 16))
#define READ24_FROM_BE(p) (p[2] | (p[1] << 8) | (p[0] << 16))
#define READ32_FROM_LE(p) GST_READ_UINT32_LE (p)
#define READ32_FROM_BE(p) GST_READ_UINT32_BE (p)

MAKE_UNPACK_FUNC (u8, 1, SIGNED, READ8);
MAKE_UNPACK_FUNC (s8, 1, 0, READ8);
MAKE_UNPACK_FUNC (u16_le, 2, SIGNED, READ16_FROM_LE);
MAKE_UNPACK_FUNC (s16_le, 2, 0, READ16_FROM_LE);
MAKE_UNPACK_FUNC (u16_be, 2, SIGNED, READ16_FROM_BE);
MAKE_UNPACK_FUNC (s16_be, 2, 0, READ16_FROM_BE);
MAKE_UNPACK_FUNC (u24_le, 3, SIGNED, READ24_FROM_LE);
MAKE_UNPACK_FUNC (s24_le, 3, 0, READ24_FROM_LE);
MAKE_UNPACK_FUNC (u24_be, 3, SIGNED, READ24_FROM_BE);
MAKE_UNPACK_FUNC (s24_be, 3, 0, READ24_FROM_BE);
MAKE_UNPACK_FUNC (u32_le, 4, SIGNED, READ32_FROM_LE);
MAKE_UNPACK_FUNC (s32_le, 4, 0, READ32_FROM_LE);
MAKE_UNPACK_FUNC (u32_be, 4, SIGNED, READ32_FROM_BE);
MAKE_UNPACK_FUNC (s32_be, 4, 0, READ32_FROM_BE);

/*** 
 * packing code
 */
#define MAKE_PACK_FUNC_NAME(name)					\
audio_convert_pack_##name

#define MAKE_PACK_FUNC(name, stride, sign, WRITE_FUNC)			\
static void								\
MAKE_PACK_FUNC_NAME (name) (gint32 *src, gpointer dst,	 		\
	gint scale, gint count)						\
{									\
  guint8 *p = (guint8 *)dst;						\
  guint32 tmp;								\
  for (;count; count--) {						\
    tmp = (*src++ ^ (sign)) >> scale;					\
    WRITE_FUNC (p, tmp);						\
    p+=stride;								\
  }									\
}

/* special float pack function */
static void
MAKE_PACK_FUNC_NAME (float) (gint32 * src, gpointer dst, gint scale, gint count)
{
  gfloat *p = (gfloat *) dst;

  for (; count; count--) {
    *p++ = INT2FLOAT (*src++);
  }
}

#define WRITE8(p, v)       GST_WRITE_UINT8 (p, v)
#define WRITE16_TO_LE(p,v) GST_WRITE_UINT16_LE (p, (guint16)(v))
#define WRITE16_TO_BE(p,v) GST_WRITE_UINT16_BE (p, (guint16)(v))
#define WRITE24_TO_LE(p,v) p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff
#define WRITE24_TO_BE(p,v) p[2] = v & 0xff; p[1] = (v >> 8) & 0xff; p[0] = (v >> 16) & 0xff
#define WRITE32_TO_LE(p,v) GST_WRITE_UINT32_LE (p, (guint32)(v))
#define WRITE32_TO_BE(p,v) GST_WRITE_UINT32_BE (p, (guint32)(v))

MAKE_PACK_FUNC (u8, 1, SIGNED, WRITE8);
MAKE_PACK_FUNC (s8, 1, 0, WRITE8);
MAKE_PACK_FUNC (u16_le, 2, SIGNED, WRITE16_TO_LE);
MAKE_PACK_FUNC (s16_le, 2, 0, WRITE16_TO_LE);
MAKE_PACK_FUNC (u16_be, 2, SIGNED, WRITE16_TO_BE);
MAKE_PACK_FUNC (s16_be, 2, 0, WRITE16_TO_BE);
MAKE_PACK_FUNC (u24_le, 3, SIGNED, WRITE24_TO_LE);
MAKE_PACK_FUNC (s24_le, 3, 0, WRITE24_TO_LE);
MAKE_PACK_FUNC (u24_be, 3, SIGNED, WRITE24_TO_BE);
MAKE_PACK_FUNC (s24_be, 3, 0, WRITE24_TO_BE);
MAKE_PACK_FUNC (u32_le, 4, SIGNED, WRITE32_TO_LE);
MAKE_PACK_FUNC (s32_le, 4, 0, WRITE32_TO_LE);
MAKE_PACK_FUNC (u32_be, 4, SIGNED, WRITE32_TO_BE);
MAKE_PACK_FUNC (s32_be, 4, 0, WRITE32_TO_BE);

static AudioConvertUnpack unpack_funcs[] = {
  MAKE_UNPACK_FUNC_NAME (u8),
  MAKE_UNPACK_FUNC_NAME (s8),
  MAKE_UNPACK_FUNC_NAME (u8),
  MAKE_UNPACK_FUNC_NAME (s8),
  MAKE_UNPACK_FUNC_NAME (u16_le),
  MAKE_UNPACK_FUNC_NAME (s16_le),
  MAKE_UNPACK_FUNC_NAME (u16_be),
  MAKE_UNPACK_FUNC_NAME (s16_be),
  MAKE_UNPACK_FUNC_NAME (u24_le),
  MAKE_UNPACK_FUNC_NAME (s24_le),
  MAKE_UNPACK_FUNC_NAME (u24_be),
  MAKE_UNPACK_FUNC_NAME (s24_be),
  MAKE_UNPACK_FUNC_NAME (u32_le),
  MAKE_UNPACK_FUNC_NAME (s32_le),
  MAKE_UNPACK_FUNC_NAME (u32_be),
  MAKE_UNPACK_FUNC_NAME (s32_be),
  MAKE_UNPACK_FUNC_NAME (float),
};

static AudioConvertPack pack_funcs[] = {
  MAKE_PACK_FUNC_NAME (u8),
  MAKE_PACK_FUNC_NAME (s8),
  MAKE_PACK_FUNC_NAME (u8),
  MAKE_PACK_FUNC_NAME (s8),
  MAKE_PACK_FUNC_NAME (u16_le),
  MAKE_PACK_FUNC_NAME (s16_le),
  MAKE_PACK_FUNC_NAME (u16_be),
  MAKE_PACK_FUNC_NAME (s16_be),
  MAKE_PACK_FUNC_NAME (u24_le),
  MAKE_PACK_FUNC_NAME (s24_le),
  MAKE_PACK_FUNC_NAME (u24_be),
  MAKE_PACK_FUNC_NAME (s24_be),
  MAKE_PACK_FUNC_NAME (u32_le),
  MAKE_PACK_FUNC_NAME (s32_le),
  MAKE_PACK_FUNC_NAME (u32_be),
  MAKE_PACK_FUNC_NAME (s32_be),
  MAKE_PACK_FUNC_NAME (float),
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
    index = 16;
  }
  return index;
}

static gboolean
check_default (AudioConvertFmt * fmt)
{
  return (fmt->width == 32 && fmt->depth == 32 &&
      fmt->endianness == G_BYTE_ORDER && fmt->sign == TRUE);
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
  gint idx;

  g_return_val_if_fail (ctx != NULL, FALSE);
  g_return_val_if_fail (in != NULL, FALSE);
  g_return_val_if_fail (out != NULL, FALSE);

  /* first clean the existing context */
  audio_convert_clean_context (ctx);

  ctx->in = *in;
  ctx->out = *out;

  gst_channel_mix_setup_matrix (ctx);

  idx = audio_convert_get_func_index (in);
  if (!(ctx->unpack = unpack_funcs[idx]))
    goto not_supported;

  idx = audio_convert_get_func_index (out);
  if (!(ctx->pack = pack_funcs[idx]))
    goto not_supported;

  /* check if input is in default format */
  ctx->in_default = check_default (in);
  /* check if channel mixer is passthrough */
  ctx->mix_passthrough = gst_channel_mix_passthrough (ctx);
  /* check if output is in default format */
  ctx->out_default = check_default (out);

  ctx->in_scale = 32 - in->depth;
  ctx->out_scale = 32 - out->depth;

  return TRUE;

not_supported:
  {
    return FALSE;
  }
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
  gint insize, outsize;
  gpointer outbuf, tmpbuf;
  gint biggest = 0;

  g_return_val_if_fail (ctx != NULL, FALSE);
  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (dst != NULL, FALSE);
  g_return_val_if_fail (samples >= 0, FALSE);

  if (samples == 0)
    return TRUE;

  insize = ctx->in.unit_size * samples;
  outsize = ctx->out.unit_size * samples;

  /* find biggest temp buffer size */
  if (!ctx->in_default)
    biggest = insize * 32 / ctx->in.width;
  if (!ctx->mix_passthrough)
    biggest = MAX (biggest, outsize * 32 / ctx->out.width);

  /* see if one of the buffers can be used as temp */
  if (outsize >= biggest)
    tmpbuf = dst;
  else if (insize >= biggest && src_writable)
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
    gst_channel_mix_mix (ctx, src, outbuf, samples);

    src = outbuf;
  }

  if (!ctx->out_default) {
    /* pack default format into dst */
    ctx->pack (src, dst, ctx->out_scale, samples * ctx->out.channels);
  }

  return TRUE;
}
