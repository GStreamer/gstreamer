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

#define UNPACK_CODE(type, corr, E_FUNC)					\
  type* p = (type *) src;						\
  gint64 tmp;								\
  for (;count; count--) {						\
    tmp = ((gint64) E_FUNC (*p) - corr) * scale;			\
    *dst++ = CLAMP (tmp, -((gint64) 1 << 32), (gint64) 0x7FFFFFFF);	\
    p++;								\
  }

#define MAKE_UNPACK_FUNC_NAME(name)					\
audio_convert_unpack_##name

/* unsigned case */
#define MAKE_UNPACK_FUNC_U(name, type, E_FUNC)				\
static void								\
MAKE_UNPACK_FUNC_NAME (name) (gpointer src, gint32 *dst,  		\
	gint64 scale, gint count)					\
{									\
  UNPACK_CODE(type, (1 << (sizeof (type) * 8 - 1)), E_FUNC);		\
}

/* signed case */
#define MAKE_UNPACK_FUNC_S(name, type, E_FUNC)				\
static void								\
MAKE_UNPACK_FUNC_NAME (name) (gpointer src, gint32 *dst,  		\
	gint64 scale, gint count)					\
{									\
  UNPACK_CODE(type, 0, E_FUNC);						\
}

MAKE_UNPACK_FUNC_U (u8, guint8, /* nothing */ );
MAKE_UNPACK_FUNC_S (s8, gint8, /* nothing */ );
MAKE_UNPACK_FUNC_U (u16_le, guint16, GUINT16_FROM_LE);
MAKE_UNPACK_FUNC_S (s16_le, gint16, GINT16_FROM_LE);
MAKE_UNPACK_FUNC_U (u16_be, guint16, GUINT16_FROM_BE);
MAKE_UNPACK_FUNC_S (s16_be, gint16, GINT16_FROM_BE);
MAKE_UNPACK_FUNC_U (u32_le, guint32, GUINT32_FROM_LE);
MAKE_UNPACK_FUNC_S (s32_le, gint32, GINT32_FROM_LE);
MAKE_UNPACK_FUNC_U (u32_be, guint32, GUINT32_FROM_BE);
MAKE_UNPACK_FUNC_S (s32_be, gint32, GINT32_FROM_BE);

/* FIXME 24 bits */
#if 0
gint64 cur = 0;

        /* FIXME */

        /* Read 24-bits LE/BE into signed 64 host-endian */
if (this->sinkcaps.endianness == G_LITTLE_ENDIAN) {
  cur = src[0] | (src[1] << 8) | (src[2] << 16);
} else {
  cur = src[2] | (src[1] << 8) | (src[0] << 16);
}

        /* Sign extend */
if ((this->sinkcaps.sign)
    && (cur & (1 << (this->sinkcaps.depth - 1))))
  cur |= ((gint64) (-1)) ^ ((1 << this->sinkcaps.depth) - 1);

src -= 3;
#endif

static void
MAKE_UNPACK_FUNC_NAME (float) (gpointer src, gint32 * dst,
    gint64 scale, gint count)
{
  gfloat *p = (gfloat *) src;
  gfloat temp;

  for (; count; count--) {
    temp = *p++ * 2147483647.0f + .5;
    *dst++ = (gint32) CLAMP (temp, -2147483648ll, 2147483647ll);
  }
}

#define PACK_CODE(type, corr, E_FUNC)					\
  type* p = (type *) dst;						\
  gint32 scale = (32 - depth);						\
  for (;count; count--) {						\
    *p = E_FUNC ((type)((*src) >> scale) + corr);			\
    p++; src++;								\
  }

#define MAKE_PACK_FUNC_NAME(name)					\
audio_convert_pack_##name

#define MAKE_PACK_FUNC_U(name, type, E_FUNC)				\
static void								\
MAKE_PACK_FUNC_NAME (name) (gint32 *src, gpointer dst,	 		\
	gint depth, gint count)						\
{									\
  PACK_CODE (type, (1 << (depth - 1)), E_FUNC);				\
}

#define MAKE_PACK_FUNC_S(name, type, E_FUNC)				\
static void								\
MAKE_PACK_FUNC_NAME (name) (gint32 *src, gpointer dst, 			\
	gint depth, gint count)						\
{									\
  PACK_CODE (type, 0, E_FUNC);						\
}

MAKE_PACK_FUNC_U (u8, guint8, /* nothing */ );
MAKE_PACK_FUNC_S (s8, gint8, /* nothing */ );
MAKE_PACK_FUNC_U (u16_le, guint16, GUINT16_TO_LE);
MAKE_PACK_FUNC_S (s16_le, gint16, GINT16_TO_LE);
MAKE_PACK_FUNC_U (u16_be, guint16, GUINT16_TO_BE);
MAKE_PACK_FUNC_S (s16_be, gint16, GINT16_TO_BE);
MAKE_PACK_FUNC_U (u32_le, guint32, GUINT32_TO_LE);
MAKE_PACK_FUNC_S (s32_le, gint32, GINT32_TO_LE);
MAKE_PACK_FUNC_U (u32_be, guint32, GUINT32_TO_BE);
MAKE_PACK_FUNC_S (s32_be, gint32, GINT32_TO_BE);

static void
MAKE_PACK_FUNC_NAME (float) (gint32 * src, gpointer dst, gint depth, gint count)
{
  gfloat *p = (gfloat *) dst;

  for (; count; count--) {
    *p++ = INT2FLOAT (*src++);
  }
}

static AudioConvertUnpack unpack_funcs[] = {
  MAKE_UNPACK_FUNC_NAME (u8),
  MAKE_UNPACK_FUNC_NAME (s8),
  MAKE_UNPACK_FUNC_NAME (u8),
  MAKE_UNPACK_FUNC_NAME (s8),
  MAKE_UNPACK_FUNC_NAME (u16_le),
  MAKE_UNPACK_FUNC_NAME (s16_le),
  MAKE_UNPACK_FUNC_NAME (u16_be),
  MAKE_UNPACK_FUNC_NAME (s16_be),
  NULL,
  NULL,
  NULL,
  NULL,
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
  NULL,
  NULL,
  NULL,
  NULL,
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

  ctx->scale = ((gint64) 1 << (32 - in->depth));
  ctx->depth = out->depth;

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
    ctx->unpack (src, outbuf, ctx->scale, samples * ctx->in.channels);

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
    ctx->pack (src, dst, ctx->depth, samples * ctx->out.channels);
  }

  return TRUE;
}
