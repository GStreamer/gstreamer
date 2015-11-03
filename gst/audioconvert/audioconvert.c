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
 *                           int/int    int/float  float/int float/float
 *
 *  unpack                     S32          S32         F64       F64
 *  convert                               S32->F64
 *  channel mix                S32          F64         F64       F64
 *  convert                                           F64->S32
 *  quantize                   S32                      S32
 *  pack                       S32          F64         S32       F64
 */
gboolean
audio_convert_prepare_context (AudioConvertCtx * ctx, GstAudioInfo * in,
    GstAudioInfo * out, GstAudioDitherMethod dither,
    GstAudioNoiseShapingMethod ns)
{
  gint in_depth, out_depth;
  GstChannelMixFlags flags;
  gboolean in_int, out_int;
  GstAudioFormat format;

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

  GST_INFO ("unitsizes: %d -> %d", in->bpf, out->bpf);

  in_depth = GST_AUDIO_FORMAT_INFO_DEPTH (in->finfo);
  out_depth = GST_AUDIO_FORMAT_INFO_DEPTH (out->finfo);

  GST_INFO ("depth in %d, out %d", in_depth, out_depth);

  in_int = GST_AUDIO_FORMAT_INFO_IS_INTEGER (in->finfo);
  out_int = GST_AUDIO_FORMAT_INFO_IS_INTEGER (out->finfo);

  flags =
      GST_AUDIO_INFO_IS_UNPOSITIONED (in) ?
      GST_CHANNEL_MIX_FLAGS_UNPOSITIONED_IN : 0;
  flags |=
      GST_AUDIO_INFO_IS_UNPOSITIONED (out) ?
      GST_CHANNEL_MIX_FLAGS_UNPOSITIONED_OUT : 0;


  /* step 1, unpack */
  format = in->finfo->unpack_format;
  ctx->in_default = in->finfo->unpack_format == in->finfo->format;
  GST_INFO ("unpack format %s to %s",
      gst_audio_format_to_string (in->finfo->format),
      gst_audio_format_to_string (format));

  /* step 2, optional convert from S32 to F64 for channel mix */
  if (in_int && !out_int) {
    GST_INFO ("convert S32 to F64");
    ctx->convert_in = (AudioConvertFunc) audio_convert_orc_s32_to_double;
    format = GST_AUDIO_FORMAT_F64;
  }

  /* step 3, channel mix */
  ctx->mix_format = format;
  ctx->mix = gst_channel_mix_new (flags, in->channels, in->position,
      out->channels, out->position);
  ctx->mix_passthrough = gst_channel_mix_is_passthrough (ctx->mix);
  GST_INFO ("mix format %s, passthrough %d, in_channels %d, out_channels %d",
      gst_audio_format_to_string (format), ctx->mix_passthrough,
      in->channels, out->channels);

  /* step 4, optional convert for quantize */
  if (!in_int && out_int) {
    GST_INFO ("convert F64 to S32");
    ctx->convert_out = (AudioConvertFunc) audio_convert_orc_double_to_s32;
    format = GST_AUDIO_FORMAT_S32;
  }
  /* step 5, optional quantize */
  /* Don't dither or apply noise shaping if target depth is bigger than 20 bits
   * as DA converters only can do a SNR up to 20 bits in reality.
   * Also don't dither or apply noise shaping if target depth is larger than
   * source depth. */
  if (out_depth > 20 || (in_int && out_depth >= in_depth)) {
    dither = GST_AUDIO_DITHER_NONE;
    ns = GST_AUDIO_NOISE_SHAPING_NONE;
    GST_INFO ("using no dither and noise shaping");
  } else {
    GST_INFO ("using dither %d and noise shaping %d", dither, ns);
    /* Use simple error feedback when output sample rate is smaller than
     * 32000 as the other methods might move the noise to audible ranges */
    if (ns > GST_AUDIO_NOISE_SHAPING_ERROR_FEEDBACK && out->rate < 32000)
      ns = GST_AUDIO_NOISE_SHAPING_ERROR_FEEDBACK;
  }
  /* we still want to run the quantization step when reducing bits to get
   * the rounding correct */
  if (out_int && out_depth < 32) {
    GST_INFO ("quantize to %d bits, dither %d, ns %d", out_depth, dither, ns);
    ctx->quant = gst_audio_quantize_new (dither, ns, 0, format,
        out->channels, 1U << (32 - out_depth));
  }
  /* step 6, pack */
  g_assert (out->finfo->unpack_format == format);
  ctx->out_default = format == out->finfo->format;
  GST_INFO ("pack format %s to %s", gst_audio_format_to_string (format),
      gst_audio_format_to_string (out->finfo->format));

  /* optimize */
  if (out->finfo->format == in->finfo->format && ctx->mix_passthrough) {
    GST_INFO ("same formats and passthrough mixing -> passthrough");
    ctx->passthrough = TRUE;
  }

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

  if (ctx->quant)
    gst_audio_quantize_free (ctx->quant);
  ctx->quant = NULL;
  if (ctx->mix)
    gst_channel_mix_free (ctx->mix);
  ctx->mix = NULL;
  gst_audio_info_init (&ctx->in);
  gst_audio_info_init (&ctx->out);
  ctx->convert_in = NULL;
  ctx->convert_out = NULL;

  g_free (ctx->tmpbuf);
  g_free (ctx->tmpbuf2);
  ctx->tmpbuf = NULL;
  ctx->tmpbuf2 = NULL;
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
  guint size;
  gpointer outbuf, tmpbuf, tmpbuf2;

  g_return_val_if_fail (ctx != NULL, FALSE);
  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (dst != NULL, FALSE);
  g_return_val_if_fail (samples >= 0, FALSE);

  if (samples == 0)
    return TRUE;

  if (ctx->passthrough) {
    memcpy (dst, src, samples * ctx->in.bpf);
    return TRUE;
  }

  size = sizeof (gdouble) * samples * MAX (ctx->in.channels, ctx->out.channels);

  if (size > ctx->tmpbufsize) {
    ctx->tmpbuf = g_realloc (ctx->tmpbuf, size);
    ctx->tmpbuf2 = g_realloc (ctx->tmpbuf2, size);
    ctx->tmpbufsize = size;
  }
  tmpbuf = ctx->tmpbuf;
  tmpbuf2 = ctx->tmpbuf2;

  /* 1. unpack */
  if (!ctx->in_default) {
    if (!ctx->convert_in && ctx->mix_passthrough && !ctx->convert_out
        && !ctx->quant && ctx->out_default)
      outbuf = dst;
    else
      outbuf = tmpbuf;

    ctx->in.finfo->unpack_func (ctx->in.finfo,
        GST_AUDIO_PACK_FLAG_TRUNCATE_RANGE, outbuf, src,
        samples * ctx->in.channels);
    src = outbuf;
  }

  /* 2. optionally convert for mixing */
  if (ctx->convert_in) {
    if (ctx->mix_passthrough && !ctx->convert_out && !ctx->quant
        && ctx->out_default)
      outbuf = dst;
    else if (src == tmpbuf)
      outbuf = tmpbuf2;
    else
      outbuf = tmpbuf;

    ctx->convert_in (outbuf, src, samples * ctx->in.channels);
    src = outbuf;
  }

  /* step 3, channel mix if not passthrough */
  if (!ctx->mix_passthrough) {
    if (!ctx->convert_out && !ctx->quant && ctx->out_default)
      outbuf = dst;
    else
      outbuf = tmpbuf;

    gst_channel_mix_mix (ctx->mix, ctx->mix_format, ctx->in.layout, src, outbuf,
        samples);
    src = outbuf;
  }
  /* step 4, optional convert F64 -> S32 for quantize */
  if (ctx->convert_out) {
    if (!ctx->quant && ctx->out_default)
      outbuf = dst;
    else
      outbuf = tmpbuf;

    ctx->convert_out (outbuf, src, samples * ctx->out.channels);
    src = outbuf;
  }

  /* step 5, optional quantize */
  if (ctx->quant) {
    if (ctx->out_default)
      outbuf = dst;
    else
      outbuf = tmpbuf;

    gst_audio_quantize_samples (ctx->quant, outbuf, src, samples);
    src = outbuf;
  }

  /* step 6, pack */
  if (!ctx->out_default) {
    ctx->out.finfo->pack_func (ctx->out.finfo, 0, src, dst,
        samples * ctx->out.channels);
  }

  return TRUE;
}
