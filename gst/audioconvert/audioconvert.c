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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <string.h>

#include "gstchannelmix.h"
#include "gstaudioquantize.h"
#include "audioconvert.h"
#include "gstaudioconvertorc.h"

/**
 * int -> int
 *  - unpack S32
 *  -                                      convert F64
 *  - (channel mix S32)                    (channel mix F64)
 *  - (quantize+dither S32)                quantize+dither+ns F64->S32
 *  - pack from S32
 *
 * int -> float
 *  - unpack S32
 *  - convert F64
 *  - (channel mix F64)
 *  - pack from F64
 *
 * float -> int
 *  - unpack F64
 *  - (channel mix F64)
 *  - quantize+dither+ns F64->S32
 *  - pack from S32
 *
 * float -> float
 *  - unpack F64
 *  - (channel mix F64)
 *  - pack from F64
 */
#define DOUBLE_INTERMEDIATE_FORMAT(ctx)                     \
    (!GST_AUDIO_FORMAT_INFO_IS_INTEGER (ctx->in.finfo) ||   \
     !GST_AUDIO_FORMAT_INFO_IS_INTEGER (ctx->out.finfo) ||  \
     (ctx->ns != NOISE_SHAPING_NONE))

static inline gboolean
check_default (AudioConvertCtx * ctx, const GstAudioFormatInfo * fmt)
{
  if (!DOUBLE_INTERMEDIATE_FORMAT (ctx)) {
    return GST_AUDIO_FORMAT_INFO_FORMAT (fmt) == GST_AUDIO_FORMAT_S32;
  } else {
    return GST_AUDIO_FORMAT_INFO_FORMAT (fmt) == GST_AUDIO_FORMAT_F64;
  }
}

gboolean
audio_convert_prepare_context (AudioConvertCtx * ctx, GstAudioInfo * in,
    GstAudioInfo * out, GstAudioConvertDithering dither,
    GstAudioConvertNoiseShaping ns)
{
  gint in_depth, out_depth;

  g_return_val_if_fail (ctx != NULL, FALSE);
  g_return_val_if_fail (in != NULL, FALSE);
  g_return_val_if_fail (out != NULL, FALSE);

  /* first clean the existing context */
  audio_convert_clean_context (ctx);
  if ((GST_AUDIO_INFO_CHANNELS (in) != GST_AUDIO_INFO_CHANNELS (out)) &&
      (GST_AUDIO_INFO_IS_UNPOSITIONED (in)
          || GST_AUDIO_INFO_IS_UNPOSITIONED (out)))
    goto unpositioned;

  ctx->in = *in;
  ctx->out = *out;

  in_depth = GST_AUDIO_FORMAT_INFO_DEPTH (in->finfo);
  out_depth = GST_AUDIO_FORMAT_INFO_DEPTH (out->finfo);

  GST_INFO ("depth in %d, out %d", in_depth, out_depth);

  /* Don't dither or apply noise shaping if target depth is bigger than 20 bits
   * as DA converters only can do a SNR up to 20 bits in reality.
   * Also don't dither or apply noise shaping if target depth is larger than
   * source depth. */
  if (out_depth <= 20 && (!GST_AUDIO_FORMAT_INFO_IS_INTEGER (in->finfo)
          || in_depth >= out_depth)) {
    ctx->dither = dither;
    ctx->ns = ns;
    GST_INFO ("using dither %d and noise shaping %d", dither, ns);
  } else {
    ctx->dither = DITHER_NONE;
    ctx->ns = NOISE_SHAPING_NONE;
    GST_INFO ("using no dither and noise shaping");
  }

  /* Use simple error feedback when output sample rate is smaller than
   * 32000 as the other methods might move the noise to audible ranges */
  if (ctx->ns > NOISE_SHAPING_ERROR_FEEDBACK && out->rate < 32000)
    ctx->ns = NOISE_SHAPING_ERROR_FEEDBACK;

  gst_channel_mix_setup_matrix (ctx);

  /* if one formats is float/double or we use noise shaping use double as
   * intermediate format and switch mixing */
  if (DOUBLE_INTERMEDIATE_FORMAT (ctx)) {
    GST_INFO ("use float mixing");
    ctx->channel_mix = (AudioConvertMix) gst_channel_mix_mix_float;

    if (ctx->in.finfo->unpack_format != GST_AUDIO_FORMAT_F64) {
      ctx->convert = audio_convert_orc_s32_to_double;
      GST_INFO ("convert input to F64");
    }
    /* check if input needs to be unpacked to intermediate format */
    ctx->in_default =
        GST_AUDIO_FORMAT_INFO_FORMAT (in->finfo) == GST_AUDIO_FORMAT_F64;

    if (GST_AUDIO_FORMAT_INFO_IS_INTEGER (out->finfo)) {
      /* quantization will convert to s32, check if this is our final output format */
      ctx->out_default =
          GST_AUDIO_FORMAT_INFO_FORMAT (out->finfo) == GST_AUDIO_FORMAT_S32;
    } else {
      ctx->out_default =
          GST_AUDIO_FORMAT_INFO_FORMAT (out->finfo) == GST_AUDIO_FORMAT_F64;
    }
  } else {
    GST_INFO ("use int mixing");
    ctx->channel_mix = (AudioConvertMix) gst_channel_mix_mix_int;
    /* check if input needs to be unpacked to intermediate format */
    ctx->in_default =
        GST_AUDIO_FORMAT_INFO_FORMAT (in->finfo) == GST_AUDIO_FORMAT_S32;
    /* check if output is in default format */
    ctx->out_default =
        GST_AUDIO_FORMAT_INFO_FORMAT (out->finfo) == GST_AUDIO_FORMAT_S32;
  }

  GST_INFO ("unitsizes: %d -> %d", in->bpf, out->bpf);

  /* check if channel mixer is passthrough */
  ctx->mix_passthrough = gst_channel_mix_passthrough (ctx);
  ctx->quant_default = check_default (ctx, out->finfo);

  GST_INFO ("in default %d, mix passthrough %d, out default %d",
      ctx->in_default, ctx->mix_passthrough, ctx->out_default);

  ctx->out_scale =
      GST_AUDIO_FORMAT_INFO_IS_INTEGER (out->finfo) ? (32 - out_depth) : 0;

  GST_INFO ("scale out %d", ctx->out_scale);

  gst_audio_quantize_setup (ctx);

  return TRUE;

  /* ERRORS */
unpositioned:
  {
    GST_WARNING ("unpositioned channels");
    return FALSE;
  }
}

gboolean
audio_convert_clean_context (AudioConvertCtx * ctx)
{
  g_return_val_if_fail (ctx != NULL, FALSE);

  gst_audio_quantize_free (ctx);
  gst_channel_mix_unset_matrix (ctx);
  gst_audio_info_init (&ctx->in);
  gst_audio_info_init (&ctx->out);
  ctx->convert = NULL;

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
    *srcsize = samples * ctx->in.bpf;
  if (dstsize)
    *dstsize = samples * ctx->out.bpf;

  return TRUE;
}

gboolean
audio_convert_convert (AudioConvertCtx * ctx, gpointer src,
    gpointer dst, gint samples, gboolean src_writable)
{
  guint insize, outsize, size;
  gpointer outbuf, tmpbuf;
  guint intemp = 0, outtemp = 0, biggest;
  gint in_width, out_width;

  g_return_val_if_fail (ctx != NULL, FALSE);
  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (dst != NULL, FALSE);
  g_return_val_if_fail (samples >= 0, FALSE);

  if (samples == 0)
    return TRUE;

  insize = ctx->in.bpf * samples;
  outsize = ctx->out.bpf * samples;

  in_width = GST_AUDIO_FORMAT_INFO_WIDTH (ctx->in.finfo);
  out_width = GST_AUDIO_FORMAT_INFO_WIDTH (ctx->out.finfo);

  /* find biggest temp buffer size */
  size = (DOUBLE_INTERMEDIATE_FORMAT (ctx)) ? sizeof (gdouble)
      : sizeof (gint32);

  if (!ctx->in_default)
    intemp = gst_util_uint64_scale (insize, size * 8, in_width);
  if (!ctx->mix_passthrough || !ctx->quant_default)
    outtemp = gst_util_uint64_scale (outsize, size * 8, out_width);
  biggest = MAX (intemp, outtemp);

  /* see if one of the buffers can be used as temp */
  if ((outsize >= biggest) && (ctx->out.bpf <= size))
    tmpbuf = dst;
  else if ((insize >= biggest) && src_writable && (ctx->in.bpf >= size))
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
    gpointer t;

    /* check if final conversion */
    if (!(ctx->quant_default && ctx->mix_passthrough))
      outbuf = tmpbuf;
    else
      outbuf = dst;

    /* move samples to the middle of the array so that we can
     * convert them in-place */
    if (ctx->convert)
      t = ((gint32 *) outbuf) + (samples * ctx->in.channels);
    else
      t = outbuf;

    /* unpack to default format */
    ctx->in.finfo->unpack_func (ctx->in.finfo, 0, t, src,
        samples * ctx->in.channels);

    if (ctx->convert)
      ctx->convert (outbuf, t, samples * ctx->in.channels);

    src = outbuf;
  }

  if (!ctx->mix_passthrough) {
    /* check if final conversion */
    if (ctx->quant_default)
      outbuf = dst;
    else
      outbuf = tmpbuf;

    /* convert channels */
    ctx->channel_mix (ctx, src, outbuf, samples);

    src = outbuf;
  }

  /* we only need to quantize if output format is int */
  if (GST_AUDIO_FORMAT_INFO_IS_INTEGER (ctx->out.finfo)) {
    if (ctx->out_default)
      outbuf = dst;
    else
      outbuf = tmpbuf;

    ctx->quantize (ctx, src, outbuf, samples);
  }

  if (!ctx->out_default) {
    /* pack default format into dst */
    ctx->out.finfo->pack_func (ctx->out.finfo, 0, src, dst,
        samples * ctx->out.channels);
  }

  return TRUE;
}
