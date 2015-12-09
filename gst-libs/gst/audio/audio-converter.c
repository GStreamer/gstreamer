/* GStreamer
 * Copyright (C) 2005 Wim Taymans <wim at fluendo dot com>
 *           (C) 2015 Wim Taymans <wim.taymans@gmail.com>
 *
 * audioconverter.c: Convert audio to different audio formats automatically
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

#include "audio-converter.h"
#include "gstaudiopack.h"

/**
 * SECTION:audioconverter
 * @short_description: Generic audio conversion
 *
 * <refsect2>
 * <para>
 * This object is used to convert audio samples from one format to another.
 * The object can perform conversion of:
 * <itemizedlist>
 *  <listitem><para>
 *    audio format with optional dithering and noise shaping
 *  </para></listitem>
 *  <listitem><para>
 *    audio samplerate
 *  </para></listitem>
 *  <listitem><para>
 *    audio channels and channel layout
 *  </para></listitem>
 * </para>
 * </refsect2>
 */

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static gsize cat_gonce = 0;

  if (g_once_init_enter (&cat_gonce)) {
    gsize cat_done;

    cat_done = (gsize) _gst_debug_category_new ("audio-converter", 0,
        "audio-converter object");

    g_once_init_leave (&cat_gonce, cat_done);
  }

  return (GstDebugCategory *) cat_gonce;
}
#else
#define ensure_debug_category() /* NOOP */
#endif /* GST_DISABLE_GST_DEBUG */

typedef void (*AudioConvertFunc) (gpointer dst, const gpointer src, gint count);

/*                           int/int    int/float  float/int float/float
 *
 *  unpack                     S32          S32         F64       F64
 *  convert                               S32->F64
 *  channel mix                S32          F64         F64       F64
 *  convert                                           F64->S32
 *  quantize                   S32                      S32
 *  pack                       S32          F64         S32       F64
 */
struct _GstAudioConverter
{
  GstAudioInfo in;
  GstAudioInfo out;

  GstStructure *config;

  gboolean in_default;

  AudioConvertFunc convert_in;

  gboolean mix_passthrough;
  GstAudioChannelMix *mix;

  AudioConvertFunc convert_out;

  GstAudioQuantize *quant;

  gboolean out_default;

  gboolean passthrough;

  gpointer tmpbuf;
  gpointer tmpbuf2;
  gint tmpbufsize;
};

/*
static guint
get_opt_uint (GstAudioConverter * convert, const gchar * opt, guint def)
{
  guint res;
  if (!gst_structure_get_uint (convert->config, opt, &res))
    res = def;
  return res;
}
*/

static gint
get_opt_enum (GstAudioConverter * convert, const gchar * opt, GType type,
    gint def)
{
  gint res;
  if (!gst_structure_get_enum (convert->config, opt, type, &res))
    res = def;
  return res;
}

#define DEFAULT_OPT_DITHER_METHOD GST_AUDIO_DITHER_NONE
#define DEFAULT_OPT_NOISE_SHAPING_METHOD GST_AUDIO_NOISE_SHAPING_NONE
#define DEFAULT_OPT_QUANTIZATION 1

#define GET_OPT_DITHER_METHOD(c) get_opt_enum(c, \
    GST_AUDIO_CONVERTER_OPT_DITHER_METHOD, GST_TYPE_AUDIO_DITHER_METHOD, \
    DEFAULT_OPT_DITHER_METHOD)
#define GET_OPT_NOISE_SHAPING_METHOD(c) get_opt_enum(c, \
    GST_AUDIO_CONVERTER_OPT_NOISE_SHAPING_METHOD, GST_TYPE_AUDIO_NOISE_SHAPING_METHOD, \
    DEFAULT_OPT_NOISE_SHAPING_METHOD)
#define GET_OPT_QUANTIZATION(c) get_opt_uint(c, \
    GST_AUDIO_CONVERTER_OPT_QUANTIZATION, DEFAULT_OPT_QUANTIZATION)

static gboolean
copy_config (GQuark field_id, const GValue * value, gpointer user_data)
{
  GstAudioConverter *convert = user_data;

  gst_structure_id_set_value (convert->config, field_id, value);

  return TRUE;
}

/**
 * gst_audio_converter_set_config:
 * @convert: a #GstAudioConverter
 * @config: (transfer full): a #GstStructure
 *
 * Set @config as extra configuraion for @convert.
 *
 * If the parameters in @config can not be set exactly, this function returns
 * %FALSE and will try to update as much state as possible. The new state can
 * then be retrieved and refined with gst_audio_converter_get_config().
 *
 * Look at the #GST_AUDIO_CONVERTER_OPT_* fields to check valid configuration
 * option and values.
 *
 * Returns: %TRUE when @config could be set.
 */
gboolean
gst_audio_converter_set_config (GstAudioConverter * convert,
    GstStructure * config)
{
  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail (config != NULL, FALSE);

  gst_structure_foreach (config, copy_config, convert);
  gst_structure_free (config);

  return TRUE;
}

/**
 * gst_audio_converter_get_config:
 * @convert: a #GstAudioConverter
 *
 * Get the current configuration of @convert.
 *
 * Returns: a #GstStructure that remains valid for as long as @convert is valid
 *   or until gst_audio_converter_set_config() is called.
 */
const GstStructure *
gst_audio_converter_get_config (GstAudioConverter * convert)
{
  g_return_val_if_fail (convert != NULL, NULL);

  return convert->config;
}

/**
 * gst_audio_converter_new: (skip)
 * @in: a source #GstAudioInfo
 * @out: a destination #GstAudioInfo
 * @config: (transfer full): a #GstStructure with configuration options
 *
 * Create a new #GstAudioConverter that is able to convert between @in and @out
 * audio formats.
 *
 * @config contains extra configuration options, see #GST_VIDEO_CONVERTER_OPT_*
 * parameters for details about the options and values.
 *
 * Returns: a #GstAudioConverter or %NULL if conversion is not possible.
 */
GstAudioConverter *
gst_audio_converter_new (GstAudioInfo * in, GstAudioInfo * out,
    GstStructure * config)
{
  GstAudioConverter *convert;
  gint in_depth, out_depth;
  GstAudioChannelMixFlags flags;
  gboolean in_int, out_int;
  GstAudioFormat format;
  GstAudioDitherMethod dither;
  GstAudioNoiseShapingMethod ns;

  g_return_val_if_fail (in != NULL, FALSE);
  g_return_val_if_fail (out != NULL, FALSE);
  g_return_val_if_fail (in->rate == out->rate, FALSE);
  g_return_val_if_fail (in->layout == GST_AUDIO_LAYOUT_INTERLEAVED, FALSE);
  g_return_val_if_fail (in->layout == out->layout, FALSE);

  if ((GST_AUDIO_INFO_CHANNELS (in) != GST_AUDIO_INFO_CHANNELS (out)) &&
      (GST_AUDIO_INFO_IS_UNPOSITIONED (in)
          || GST_AUDIO_INFO_IS_UNPOSITIONED (out)))
    goto unpositioned;

  convert = g_slice_new0 (GstAudioConverter);

  convert->in = *in;
  convert->out = *out;

  /* default config */
  convert->config = gst_structure_new_empty ("GstAudioConverter");
  if (config)
    gst_audio_converter_set_config (convert, config);

  dither = GET_OPT_DITHER_METHOD (convert);
  ns = GET_OPT_NOISE_SHAPING_METHOD (convert);

  GST_INFO ("unitsizes: %d -> %d", in->bpf, out->bpf);

  in_depth = GST_AUDIO_FORMAT_INFO_DEPTH (in->finfo);
  out_depth = GST_AUDIO_FORMAT_INFO_DEPTH (out->finfo);

  GST_INFO ("depth in %d, out %d", in_depth, out_depth);

  in_int = GST_AUDIO_FORMAT_INFO_IS_INTEGER (in->finfo);
  out_int = GST_AUDIO_FORMAT_INFO_IS_INTEGER (out->finfo);

  flags =
      GST_AUDIO_INFO_IS_UNPOSITIONED (in) ?
      GST_AUDIO_CHANNEL_MIX_FLAGS_UNPOSITIONED_IN : 0;
  flags |=
      GST_AUDIO_INFO_IS_UNPOSITIONED (out) ?
      GST_AUDIO_CHANNEL_MIX_FLAGS_UNPOSITIONED_OUT : 0;


  /* step 1, unpack */
  format = in->finfo->unpack_format;
  convert->in_default = in->finfo->unpack_format == in->finfo->format;
  GST_INFO ("unpack format %s to %s",
      gst_audio_format_to_string (in->finfo->format),
      gst_audio_format_to_string (format));

  /* step 2, optional convert from S32 to F64 for channel mix */
  if (in_int && !out_int) {
    GST_INFO ("convert S32 to F64");
    convert->convert_in = (AudioConvertFunc) audio_orc_s32_to_double;
    format = GST_AUDIO_FORMAT_F64;
  }

  /* step 3, channel mix */
  convert->mix =
      gst_audio_channel_mix_new (flags, format, in->channels, in->position,
      out->channels, out->position);
  convert->mix_passthrough =
      gst_audio_channel_mix_is_passthrough (convert->mix);
  GST_INFO ("mix format %s, passthrough %d, in_channels %d, out_channels %d",
      gst_audio_format_to_string (format), convert->mix_passthrough,
      in->channels, out->channels);

  /* step 4, optional convert for quantize */
  if (!in_int && out_int) {
    GST_INFO ("convert F64 to S32");
    convert->convert_out = (AudioConvertFunc) audio_orc_double_to_s32;
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
    convert->quant = gst_audio_quantize_new (dither, ns, 0, format,
        out->channels, 1U << (32 - out_depth));
  }
  /* step 6, pack */
  g_assert (out->finfo->unpack_format == format);
  convert->out_default = format == out->finfo->format;
  GST_INFO ("pack format %s to %s", gst_audio_format_to_string (format),
      gst_audio_format_to_string (out->finfo->format));

  /* optimize */
  if (out->finfo->format == in->finfo->format && convert->mix_passthrough) {
    GST_INFO ("same formats and passthrough mixing -> passthrough");
    convert->passthrough = TRUE;
  }

  return convert;

  /* ERRORS */
unpositioned:
  {
    GST_WARNING ("unpositioned channels");
    return NULL;
  }
}

/**
 * gst_audio_converter_free:
 * @convert: a #GstAudioConverter
 *
 * Free a previously allocated @convert instance.
 */
void
gst_audio_converter_free (GstAudioConverter * convert)
{
  g_return_if_fail (convert != NULL);

  if (convert->quant)
    gst_audio_quantize_free (convert->quant);
  if (convert->mix)
    gst_audio_channel_mix_free (convert->mix);
  gst_audio_info_init (&convert->in);
  gst_audio_info_init (&convert->out);

  g_free (convert->tmpbuf);
  g_free (convert->tmpbuf2);
  gst_structure_free (convert->config);

  g_slice_free (GstAudioConverter, convert);
}

/**
 * gst_audio_converter_samples:
 * @convert: a #GstAudioConverter
 * @flags: extra #GstAudioConverterFlags
 * @in: input samples
 * @in_samples: number of input samples
 * @out: output samples
 * @out_samples: number of output samples
 * @in_consumed: number of input samples consumed
 * @out_produced: number of output samples produced
 *
 * Perform the conversion with @in_samples in @in to @out_samples in @out
 * using @convert.
 *
 * The actual number of samples used from @in is returned in @in_consumed and
 * can be less than @in_samples. The actual number of samples produced is
 * returned in @out_produced and can be less than @out_samples.
 *
 * Returns: %TRUE is the conversion could be performed.
 */
gboolean
gst_audio_converter_samples (GstAudioConverter * convert,
    GstAudioConverterFlags flags, gpointer in, gsize in_samples,
    gpointer out, gsize out_samples, gsize * in_consumed, gsize * out_produced)
{
  guint size;
  gpointer outbuf, tmpbuf, tmpbuf2;

  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail (in != NULL, FALSE);
  g_return_val_if_fail (out != NULL, FALSE);
  g_return_val_if_fail (in_consumed != NULL, FALSE);
  g_return_val_if_fail (out_produced != NULL, FALSE);

  in_samples = MIN (in_samples, out_samples);

  if (in_samples == 0) {
    *in_consumed = 0;
    *out_produced = 0;
    return TRUE;
  }

  if (convert->passthrough) {
    memcpy (out, in, in_samples * convert->in.bpf);
    *out_produced = in_samples;
    *in_consumed = in_samples;
    return TRUE;
  }

  size =
      sizeof (gdouble) * in_samples * MAX (convert->in.channels,
      convert->out.channels);

  if (size > convert->tmpbufsize) {
    convert->tmpbuf = g_realloc (convert->tmpbuf, size);
    convert->tmpbuf2 = g_realloc (convert->tmpbuf2, size);
    convert->tmpbufsize = size;
  }
  tmpbuf = convert->tmpbuf;
  tmpbuf2 = convert->tmpbuf2;

  /* 1. unpack */
  if (!convert->in_default) {
    if (!convert->convert_in && convert->mix_passthrough
        && !convert->convert_out && !convert->quant && convert->out_default)
      outbuf = out;
    else
      outbuf = tmpbuf;

    convert->in.finfo->unpack_func (convert->in.finfo,
        GST_AUDIO_PACK_FLAG_TRUNCATE_RANGE, outbuf, in,
        in_samples * convert->in.channels);
    in = outbuf;
  }

  /* 2. optionally convert for mixing */
  if (convert->convert_in) {
    if (convert->mix_passthrough && !convert->convert_out && !convert->quant
        && convert->out_default)
      outbuf = out;
    else if (in == tmpbuf)
      outbuf = tmpbuf2;
    else
      outbuf = tmpbuf;

    convert->convert_in (outbuf, in, in_samples * convert->in.channels);
    in = outbuf;
  }

  /* step 3, channel mix if not passthrough */
  if (!convert->mix_passthrough) {
    if (!convert->convert_out && !convert->quant && convert->out_default)
      outbuf = out;
    else
      outbuf = tmpbuf;

    gst_audio_channel_mix_samples (convert->mix, in, outbuf, in_samples);
    in = outbuf;
  }
  /* step 4, optional convert F64 -> S32 for quantize */
  if (convert->convert_out) {
    if (!convert->quant && convert->out_default)
      outbuf = out;
    else
      outbuf = tmpbuf;

    convert->convert_out (outbuf, in, in_samples * convert->out.channels);
    in = outbuf;
  }

  /* step 5, optional quantize */
  if (convert->quant) {
    if (convert->out_default)
      outbuf = out;
    else
      outbuf = tmpbuf;

    gst_audio_quantize_samples (convert->quant, outbuf, in, in_samples);
    in = outbuf;
  }

  /* step 6, pack */
  if (!convert->out_default) {
    convert->out.finfo->pack_func (convert->out.finfo, 0, in, out,
        in_samples * convert->out.channels);
  }
  *out_produced = in_samples;
  *in_consumed = in_samples;

  return TRUE;
}
