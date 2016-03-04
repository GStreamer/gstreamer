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

typedef struct _AudioChain AudioChain;

typedef void (*AudioConvertFunc) (gpointer dst, const gpointer src, gint count);
typedef gboolean (*AudioConvertSamplesFunc) (GstAudioConverter * convert,
    GstAudioConverterFlags flags, gpointer in[], gsize in_frames,
    gpointer out[], gsize out_frames);

/*                           int/int    int/float  float/int float/float
 *
 *  unpack                     S32          S32         F64       F64
 *  convert                               S32->F64
 *  channel mix                S32          F64         F64       F64
 *  convert                                           F64->S32
 *  quantize                   S32                      S32
 *  pack                       S32          F64         S32       F64
 *
 *
 *  interleave
 *  deinterleave
 *  resample
 */
struct _GstAudioConverter
{
  GstAudioInfo in;
  GstAudioInfo out;

  GstStructure *config;

  GstAudioConverterFlags flags;
  GstAudioFormat current_format;
  GstAudioLayout current_layout;
  gint current_channels;

  gboolean in_writable;
  gpointer *in_data;
  gsize in_frames;
  gpointer *out_data;
  gsize out_frames;

  /* unpack */
  gboolean in_default;
  gboolean unpack_ip;
  AudioChain *unpack_chain;

  /* convert in */
  AudioConvertFunc convert_in;
  AudioChain *convert_in_chain;

  /* channel mix */
  gboolean mix_passthrough;
  GstAudioChannelMixer *mix;
  AudioChain *mix_chain;

  /* resample */
  GstAudioResampler *resampler;
  AudioChain *resample_chain;

  /* convert out */
  AudioConvertFunc convert_out;
  AudioChain *convert_out_chain;

  /* quant */
  GstAudioQuantize *quant;
  AudioChain *quant_chain;

  /* pack */
  gboolean out_default;
  AudioChain *pack_chain;

  AudioConvertSamplesFunc convert;
};

typedef gboolean (*AudioChainFunc) (AudioChain * chain, gpointer user_data);
typedef gpointer *(*AudioChainAllocFunc) (AudioChain * chain, gsize num_samples,
    gpointer user_data);

struct _AudioChain
{
  AudioChain *prev;

  AudioChainFunc make_func;
  gpointer make_func_data;
  GDestroyNotify make_func_notify;

  const GstAudioFormatInfo *finfo;
  gint stride;
  gint inc;
  gint blocks;

  gboolean pass_alloc;
  gboolean allow_ip;

  AudioChainAllocFunc alloc_func;
  gpointer alloc_data;

  gpointer *tmp;
  gsize allocated_samples;

  gpointer *samples;
  gsize num_samples;
};

static AudioChain *
audio_chain_new (AudioChain * prev, GstAudioConverter * convert)
{
  AudioChain *chain;

  chain = g_slice_new0 (AudioChain);
  chain->prev = prev;

  if (convert->current_layout == GST_AUDIO_LAYOUT_NON_INTERLEAVED) {
    chain->inc = 1;
    chain->blocks = convert->current_channels;
  } else {
    chain->inc = convert->current_channels;
    chain->blocks = 1;
  }
  chain->finfo = gst_audio_format_get_info (convert->current_format);
  chain->stride = (chain->finfo->width * chain->inc) / 8;

  return chain;
}

static void
audio_chain_set_make_func (AudioChain * chain,
    AudioChainFunc make_func, gpointer user_data, GDestroyNotify notify)
{
  chain->make_func = make_func;
  chain->make_func_data = user_data;
  chain->make_func_notify = notify;
}

static void
audio_chain_free (AudioChain * chain)
{
  GST_LOG ("free chain %p", chain);
  if (chain->make_func_notify)
    chain->make_func_notify (chain->make_func_data);
  g_free (chain->tmp);
  g_slice_free (AudioChain, chain);
}

static gpointer *
audio_chain_alloc_samples (AudioChain * chain, gsize num_samples)
{
  return chain->alloc_func (chain, num_samples, chain->alloc_data);
}

static void
audio_chain_set_samples (AudioChain * chain, gpointer * samples,
    gsize num_samples)
{
  GST_LOG ("set samples %p %" G_GSIZE_FORMAT, samples, num_samples);

  chain->samples = samples;
  chain->num_samples = num_samples;
}

static gpointer *
audio_chain_get_samples (AudioChain * chain, gsize * avail)
{
  gpointer *res;

  while (!chain->samples)
    chain->make_func (chain, chain->make_func_data);

  res = chain->samples;
  *avail = chain->num_samples;
  chain->samples = NULL;

  return res;
}

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

#define DEFAULT_OPT_RESAMPLER_METHOD GST_AUDIO_RESAMPLER_METHOD_BLACKMAN_NUTTALL
#define DEFAULT_OPT_DITHER_METHOD GST_AUDIO_DITHER_NONE
#define DEFAULT_OPT_NOISE_SHAPING_METHOD GST_AUDIO_NOISE_SHAPING_NONE
#define DEFAULT_OPT_QUANTIZATION 1

#define GET_OPT_RESAMPLER_METHOD(c) get_opt_enum(c, \
    GST_AUDIO_CONVERTER_OPT_RESAMPLER_METHOD, GST_TYPE_AUDIO_RESAMPLER_METHOD, \
    DEFAULT_OPT_RESAMPLER_METHOD)
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
 * gst_audio_converter_update_config:
 * @convert: a #GstAudioConverter
 * @in_rate: input rate
 * @out_rate: output rate
 * @config: (transfer full) (allow-none): a #GstStructure or %NULL
 *
 * Set @in_rate, @out_rate and @config as extra configuration for @convert.
 *
 * @in_rate and @out_rate specify the new sample rates of input and output
 * formats. A value of 0 leaves the sample rate unchanged.
 *
 * @config can be %NULL, in which case, the current configuration is not
 * changed.
 *
 * If the parameters in @config can not be set exactly, this function returns
 * %FALSE and will try to update as much state as possible. The new state can
 * then be retrieved and refined with gst_audio_converter_get_config().
 *
 * Look at the #GST_AUDIO_CONVERTER_OPT_* fields to check valid configuration
 * option and values.
 *
 * Returns: %TRUE when the new parameters could be set
 */
gboolean
gst_audio_converter_update_config (GstAudioConverter * convert,
    gint in_rate, gint out_rate, GstStructure * config)
{
  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail ((in_rate == 0 && out_rate == 0) ||
      convert->flags & GST_AUDIO_CONVERTER_FLAG_VARIABLE_RATE, FALSE);

  GST_LOG ("new rate %d -> %d", in_rate, out_rate);

  if (in_rate <= 0)
    in_rate = convert->in.rate;
  if (out_rate <= 0)
    out_rate = convert->out.rate;

  convert->in.rate = in_rate;
  convert->out.rate = out_rate;

  if (convert->resampler)
    gst_audio_resampler_update (convert->resampler, in_rate, out_rate, config);

  if (config) {
    gst_structure_foreach (config, copy_config, convert);
    gst_structure_free (config);
  }

  return TRUE;
}

/**
 * gst_audio_converter_get_config:
 * @convert: a #GstAudioConverter
 * @in_rate: result input rate
 * @out_rate: result output rate
 *
 * Get the current configuration of @convert.
 *
 * Returns: a #GstStructure that remains valid for as long as @convert is valid
 *   or until gst_audio_converter_update_config() is called.
 */
const GstStructure *
gst_audio_converter_get_config (GstAudioConverter * convert,
    gint * in_rate, gint * out_rate)
{
  g_return_val_if_fail (convert != NULL, NULL);

  if (in_rate)
    *in_rate = convert->in.rate;
  if (out_rate)
    *out_rate = convert->out.rate;

  return convert->config;
}

static gpointer *
get_output_samples (AudioChain * chain, gsize num_samples, gpointer user_data)
{
  GstAudioConverter *convert = user_data;

  GST_LOG ("output samples %p %" G_GSIZE_FORMAT, convert->out_data,
      num_samples);

  return convert->out_data;
}

#define MEM_ALIGN(m,a) ((gint8 *)((guintptr)((gint8 *)(m) + ((a)-1)) & ~((a)-1)))
#define ALIGN 16

static gpointer *
get_temp_samples (AudioChain * chain, gsize num_samples, gpointer user_data)
{
  if (num_samples > chain->allocated_samples) {
    gint i;
    gint8 *s;
    gsize stride = GST_ROUND_UP_N (num_samples * chain->stride, ALIGN);
    /* first part contains the pointers, second part the data, add some extra bytes
     * for alignement */
    gsize needed = (stride + sizeof (gpointer)) * chain->blocks + ALIGN - 1;

    GST_DEBUG ("alloc samples %d %" G_GSIZE_FORMAT " %" G_GSIZE_FORMAT,
        chain->stride, num_samples, needed);
    chain->tmp = g_realloc (chain->tmp, needed);
    chain->allocated_samples = num_samples;

    /* pointer to the data, make sure it's 16 bytes aligned */
    s = MEM_ALIGN (&chain->tmp[chain->blocks], ALIGN);

    /* set up the pointers */
    for (i = 0; i < chain->blocks; i++)
      chain->tmp[i] = s + i * stride;
  }
  GST_LOG ("temp samples %p %" G_GSIZE_FORMAT, chain->tmp, num_samples);

  return chain->tmp;
}

static gboolean
do_unpack (AudioChain * chain, gpointer user_data)
{
  GstAudioConverter *convert = user_data;
  gsize num_samples;
  gpointer *tmp;
  gboolean in_writable;

  in_writable = convert->in_writable;
  num_samples = convert->in_frames;

  if (!chain->allow_ip || !in_writable || !convert->in_default) {
    gint i;

    if (in_writable && chain->allow_ip) {
      tmp = convert->in_data;
      GST_LOG ("unpack in-place %p, %" G_GSIZE_FORMAT, tmp, num_samples);
    } else {
      tmp = audio_chain_alloc_samples (chain, num_samples);
      GST_LOG ("unpack to tmp %p, %" G_GSIZE_FORMAT, tmp, num_samples);
    }

    if (convert->in_data) {
      for (i = 0; i < chain->blocks; i++) {
        if (convert->in_default) {
          GST_LOG ("copy %p, %p, %" G_GSIZE_FORMAT, tmp[i], convert->in_data[i],
              num_samples);
          memcpy (tmp[i], convert->in_data[i], num_samples * chain->stride);
        } else {
          GST_LOG ("unpack %p, %p, %" G_GSIZE_FORMAT, tmp[i],
              convert->in_data[i], num_samples);
          convert->in.finfo->unpack_func (convert->in.finfo,
              GST_AUDIO_PACK_FLAG_TRUNCATE_RANGE, tmp[i], convert->in_data[i],
              num_samples * chain->inc);
        }
      }
    } else {
      for (i = 0; i < chain->blocks; i++) {
        gst_audio_format_fill_silence (chain->finfo, tmp[i],
            num_samples * chain->inc);
      }
    }
  } else {
    tmp = convert->in_data;
    GST_LOG ("get in samples %p", tmp);
  }
  audio_chain_set_samples (chain, tmp, num_samples);

  return TRUE;
}

static gboolean
do_convert_in (AudioChain * chain, gpointer user_data)
{
  gsize num_samples;
  GstAudioConverter *convert = user_data;
  gpointer *in, *out;
  gint i;

  in = audio_chain_get_samples (chain->prev, &num_samples);
  out = (chain->allow_ip ? in : audio_chain_alloc_samples (chain, num_samples));
  GST_LOG ("convert in %p, %p, %" G_GSIZE_FORMAT, in, out, num_samples);

  for (i = 0; i < chain->blocks; i++)
    convert->convert_in (out[i], in[i], num_samples * chain->inc);

  audio_chain_set_samples (chain, out, num_samples);

  return TRUE;
}

static gboolean
do_mix (AudioChain * chain, gpointer user_data)
{
  gsize num_samples;
  GstAudioConverter *convert = user_data;
  gpointer *in, *out;

  in = audio_chain_get_samples (chain->prev, &num_samples);
  out = (chain->allow_ip ? in : audio_chain_alloc_samples (chain, num_samples));
  GST_LOG ("mix %p, %p, %" G_GSIZE_FORMAT, in, out, num_samples);

  gst_audio_channel_mixer_samples (convert->mix, in, out, num_samples);

  audio_chain_set_samples (chain, out, num_samples);

  return TRUE;
}

static gboolean
do_resample (AudioChain * chain, gpointer user_data)
{
  GstAudioConverter *convert = user_data;
  gpointer *in, *out;
  gsize in_frames, out_frames;

  in = audio_chain_get_samples (chain->prev, &in_frames);
  out_frames = convert->out_frames;
  out = (chain->allow_ip ? in : audio_chain_alloc_samples (chain, out_frames));

  GST_LOG ("resample %p %p,%" G_GSIZE_FORMAT " %" G_GSIZE_FORMAT, in,
      out, in_frames, out_frames);

  gst_audio_resampler_resample (convert->resampler, in, in_frames, out,
      out_frames);

  audio_chain_set_samples (chain, out, out_frames);

  return TRUE;
}

static gboolean
do_convert_out (AudioChain * chain, gpointer user_data)
{
  GstAudioConverter *convert = user_data;
  gsize num_samples;
  gpointer *in, *out;
  gint i;

  in = audio_chain_get_samples (chain->prev, &num_samples);
  out = (chain->allow_ip ? in : audio_chain_alloc_samples (chain, num_samples));
  GST_LOG ("convert out %p, %p %" G_GSIZE_FORMAT, in, out, num_samples);

  for (i = 0; i < chain->blocks; i++)
    convert->convert_out (out[i], in[i], num_samples * chain->inc);

  audio_chain_set_samples (chain, out, num_samples);

  return TRUE;
}

static gboolean
do_quantize (AudioChain * chain, gpointer user_data)
{
  GstAudioConverter *convert = user_data;
  gsize num_samples;
  gpointer *in, *out;

  in = audio_chain_get_samples (chain->prev, &num_samples);
  out = (chain->allow_ip ? in : audio_chain_alloc_samples (chain, num_samples));
  GST_LOG ("quantize %p, %p %" G_GSIZE_FORMAT, in, out, num_samples);

  gst_audio_quantize_samples (convert->quant, in, out, num_samples);

  audio_chain_set_samples (chain, out, num_samples);

  return TRUE;
}

static gboolean
is_intermediate_format (GstAudioFormat format)
{
  return (format == GST_AUDIO_FORMAT_S16 ||
      format == GST_AUDIO_FORMAT_S32 ||
      format == GST_AUDIO_FORMAT_F32 || format == GST_AUDIO_FORMAT_F64);
}

static AudioChain *
chain_unpack (GstAudioConverter * convert)
{
  AudioChain *prev;
  GstAudioInfo *in = &convert->in;
  GstAudioInfo *out = &convert->out;
  gboolean same_format;

  same_format = in->finfo->format == out->finfo->format;

  /* do not unpack if we have the same input format as the output format
   * and it is a possible intermediate format */
  if (same_format && is_intermediate_format (in->finfo->format)) {
    convert->current_format = in->finfo->format;
  } else {
    convert->current_format = in->finfo->unpack_format;
  }
  convert->current_layout = in->layout;
  convert->current_channels = in->channels;

  convert->in_default = convert->current_format == in->finfo->format;

  GST_INFO ("unpack format %s to %s",
      gst_audio_format_to_string (in->finfo->format),
      gst_audio_format_to_string (convert->current_format));

  prev = convert->unpack_chain = audio_chain_new (NULL, convert);
  prev->allow_ip = prev->finfo->width <= in->finfo->width;
  prev->pass_alloc = FALSE;
  audio_chain_set_make_func (prev, do_unpack, convert, NULL);

  return prev;
}

static AudioChain *
chain_convert_in (GstAudioConverter * convert, AudioChain * prev)
{
  gboolean in_int, out_int;
  GstAudioInfo *in = &convert->in;
  GstAudioInfo *out = &convert->out;

  in_int = GST_AUDIO_FORMAT_INFO_IS_INTEGER (in->finfo);
  out_int = GST_AUDIO_FORMAT_INFO_IS_INTEGER (out->finfo);

  if (in_int && !out_int) {
    GST_INFO ("convert S32 to F64");
    convert->convert_in = (AudioConvertFunc) audio_orc_s32_to_double;
    convert->current_format = GST_AUDIO_FORMAT_F64;

    prev = convert->convert_in_chain = audio_chain_new (prev, convert);
    prev->allow_ip = FALSE;
    prev->pass_alloc = FALSE;
    audio_chain_set_make_func (prev, do_convert_in, convert, NULL);
  }
  return prev;
}

static AudioChain *
chain_mix (GstAudioConverter * convert, AudioChain * prev)
{
  GstAudioChannelMixerFlags flags;
  GstAudioInfo *in = &convert->in;
  GstAudioInfo *out = &convert->out;
  GstAudioFormat format = convert->current_format;

  flags =
      GST_AUDIO_INFO_IS_UNPOSITIONED (in) ?
      GST_AUDIO_CHANNEL_MIXER_FLAGS_UNPOSITIONED_IN : 0;
  flags |=
      GST_AUDIO_INFO_IS_UNPOSITIONED (out) ?
      GST_AUDIO_CHANNEL_MIXER_FLAGS_UNPOSITIONED_OUT : 0;

  convert->current_channels = out->channels;

  convert->mix =
      gst_audio_channel_mixer_new (flags, format, in->channels, in->position,
      out->channels, out->position);
  convert->mix_passthrough =
      gst_audio_channel_mixer_is_passthrough (convert->mix);
  GST_INFO ("mix format %s, passthrough %d, in_channels %d, out_channels %d",
      gst_audio_format_to_string (format), convert->mix_passthrough,
      in->channels, out->channels);

  if (!convert->mix_passthrough) {
    prev = convert->mix_chain = audio_chain_new (prev, convert);
    prev->allow_ip = FALSE;
    prev->pass_alloc = FALSE;
    audio_chain_set_make_func (prev, do_mix, convert, NULL);
  }
  return prev;
}

static AudioChain *
chain_resample (GstAudioConverter * convert, AudioChain * prev)
{
  GstAudioInfo *in = &convert->in;
  GstAudioInfo *out = &convert->out;
  GstAudioResamplerMethod method;
  GstAudioResamplerFlags flags;
  GstAudioFormat format = convert->current_format;
  gint channels = convert->current_channels;
  gboolean variable_rate;

  variable_rate = convert->flags & GST_AUDIO_CONVERTER_FLAG_VARIABLE_RATE;

  if (in->rate != out->rate || variable_rate) {
    method = GET_OPT_RESAMPLER_METHOD (convert);

    flags = 0;
    if (convert->current_layout == GST_AUDIO_LAYOUT_NON_INTERLEAVED) {
      flags |= GST_AUDIO_RESAMPLER_FLAG_NON_INTERLEAVED_IN;
      flags |= GST_AUDIO_RESAMPLER_FLAG_NON_INTERLEAVED_OUT;
    }
    if (variable_rate)
      flags |= GST_AUDIO_RESAMPLER_FLAG_VARIABLE_RATE;

    convert->resampler =
        gst_audio_resampler_new (method, flags, format, channels, in->rate,
        out->rate, convert->config);

    prev = convert->resample_chain = audio_chain_new (prev, convert);
    prev->allow_ip = FALSE;
    prev->pass_alloc = FALSE;
    audio_chain_set_make_func (prev, do_resample, convert, NULL);
  }
  return prev;
}

static AudioChain *
chain_convert_out (GstAudioConverter * convert, AudioChain * prev)
{
  gboolean in_int, out_int;
  GstAudioInfo *in = &convert->in;
  GstAudioInfo *out = &convert->out;

  in_int = GST_AUDIO_FORMAT_INFO_IS_INTEGER (in->finfo);
  out_int = GST_AUDIO_FORMAT_INFO_IS_INTEGER (out->finfo);

  if (!in_int && out_int) {
    convert->convert_out = (AudioConvertFunc) audio_orc_double_to_s32;
    convert->current_format = GST_AUDIO_FORMAT_S32;

    GST_INFO ("convert F64 to S32");
    prev = convert->convert_out_chain = audio_chain_new (prev, convert);
    prev->allow_ip = TRUE;
    prev->pass_alloc = FALSE;
    audio_chain_set_make_func (prev, do_convert_out, convert, NULL);
  }
  return prev;
}

static AudioChain *
chain_quantize (GstAudioConverter * convert, AudioChain * prev)
{
  const GstAudioFormatInfo *cur_finfo;
  GstAudioInfo *out = &convert->out;
  gint in_depth, out_depth;
  gboolean in_int, out_int;
  GstAudioDitherMethod dither;
  GstAudioNoiseShapingMethod ns;

  dither = GET_OPT_DITHER_METHOD (convert);
  ns = GET_OPT_NOISE_SHAPING_METHOD (convert);

  cur_finfo = gst_audio_format_get_info (convert->current_format);

  in_depth = GST_AUDIO_FORMAT_INFO_DEPTH (cur_finfo);
  out_depth = GST_AUDIO_FORMAT_INFO_DEPTH (out->finfo);
  GST_INFO ("depth in %d, out %d", in_depth, out_depth);

  in_int = GST_AUDIO_FORMAT_INFO_IS_INTEGER (cur_finfo);
  out_int = GST_AUDIO_FORMAT_INFO_IS_INTEGER (out->finfo);

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
  if (out_int && out_depth < 32
      && convert->current_format == GST_AUDIO_FORMAT_S32) {
    GST_INFO ("quantize to %d bits, dither %d, ns %d", out_depth, dither, ns);
    convert->quant =
        gst_audio_quantize_new (dither, ns, 0, convert->current_format,
        out->channels, 1U << (32 - out_depth));

    prev = convert->quant_chain = audio_chain_new (prev, convert);
    prev->allow_ip = TRUE;
    prev->pass_alloc = TRUE;
    audio_chain_set_make_func (prev, do_quantize, convert, NULL);
  }
  return prev;
}

static AudioChain *
chain_pack (GstAudioConverter * convert, AudioChain * prev)
{
  GstAudioInfo *out = &convert->out;
  GstAudioFormat format = convert->current_format;

  convert->current_format = out->finfo->format;

  convert->out_default = format == out->finfo->format;
  GST_INFO ("pack format %s to %s", gst_audio_format_to_string (format),
      gst_audio_format_to_string (out->finfo->format));

  return prev;
}

static void
setup_allocators (GstAudioConverter * convert)
{
  AudioChain *chain;
  AudioChainAllocFunc alloc_func;
  gboolean allow_ip;

  /* start with using dest if we can directly write into it */
  if (convert->out_default) {
    alloc_func = get_output_samples;
    allow_ip = FALSE;
  } else {
    alloc_func = get_temp_samples;
    allow_ip = TRUE;
  }
  /* now walk backwards, we try to write into the dest samples directly
   * and keep track if the source needs to be writable */
  for (chain = convert->pack_chain; chain; chain = chain->prev) {
    chain->alloc_func = alloc_func;
    chain->alloc_data = convert;
    chain->allow_ip = allow_ip && chain->allow_ip;
    GST_LOG ("chain %p: %d %d", chain, allow_ip, chain->allow_ip);

    if (!chain->pass_alloc) {
      /* can't pass allocator, make new temp line allocator */
      alloc_func = get_temp_samples;
      allow_ip = TRUE;
    }
  }
}

static gboolean
converter_passthrough (GstAudioConverter * convert,
    GstAudioConverterFlags flags, gpointer in[], gsize in_frames,
    gpointer out[], gsize out_frames)
{
  gint i;
  AudioChain *chain;
  gsize samples;

  chain = convert->pack_chain;

  samples = in_frames * chain->inc;

  GST_LOG ("passthrough: %" G_GSIZE_FORMAT " / %" G_GSIZE_FORMAT " samples",
      in_frames, samples);

  if (in) {
    gsize bytes;

    bytes = samples * (convert->in.bpf / convert->in.channels);

    for (i = 0; i < chain->blocks; i++)
      memcpy (out[i], in[i], bytes);
  } else {
    for (i = 0; i < chain->blocks; i++)
      gst_audio_format_fill_silence (convert->in.finfo, out[i], samples);
  }
  return TRUE;
}

static gboolean
converter_generic (GstAudioConverter * convert,
    GstAudioConverterFlags flags, gpointer in[], gsize in_frames,
    gpointer out[], gsize out_frames)
{
  AudioChain *chain;
  gpointer *tmp;
  gint i;
  gsize produced;

  chain = convert->pack_chain;

  convert->in_writable = flags & GST_AUDIO_CONVERTER_FLAG_IN_WRITABLE;
  convert->in_data = in;
  convert->in_frames = in_frames;
  convert->out_data = out;
  convert->out_frames = out_frames;

  /* get frames to pack */
  tmp = audio_chain_get_samples (chain, &produced);

  if (!convert->out_default) {
    GST_LOG ("pack %p, %p %" G_GSIZE_FORMAT, tmp, out, produced);
    /* and pack if needed */
    for (i = 0; i < chain->blocks; i++)
      convert->out.finfo->pack_func (convert->out.finfo, 0, tmp[i], out[i],
          produced * chain->inc);
  }
  return TRUE;
}

static gboolean
converter_resample (GstAudioConverter * convert,
    GstAudioConverterFlags flags, gpointer in[], gsize in_frames,
    gpointer out[], gsize out_frames)
{
  gst_audio_resampler_resample (convert->resampler, in, in_frames, out,
      out_frames);

  return TRUE;
}

/**
 * gst_audio_converter_new: (skip)
 * @flags: extra #GstAudioConverterFlags
 * @in_info: a source #GstAudioInfo
 * @out_info: a destination #GstAudioInfo
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
gst_audio_converter_new (GstAudioConverterFlags flags, GstAudioInfo * in_info,
    GstAudioInfo * out_info, GstStructure * config)
{
  GstAudioConverter *convert;
  AudioChain *prev;

  g_return_val_if_fail (in_info != NULL, FALSE);
  g_return_val_if_fail (out_info != NULL, FALSE);
  g_return_val_if_fail (in_info->layout == GST_AUDIO_LAYOUT_INTERLEAVED, FALSE);
  g_return_val_if_fail (in_info->layout == out_info->layout, FALSE);

  if ((GST_AUDIO_INFO_CHANNELS (in_info) != GST_AUDIO_INFO_CHANNELS (out_info))
      && (GST_AUDIO_INFO_IS_UNPOSITIONED (in_info)
          || GST_AUDIO_INFO_IS_UNPOSITIONED (out_info)))
    goto unpositioned;

  convert = g_slice_new0 (GstAudioConverter);

  convert->flags = flags;
  convert->in = *in_info;
  convert->out = *out_info;

  /* default config */
  convert->config = gst_structure_new_empty ("GstAudioConverter");
  if (config)
    gst_audio_converter_update_config (convert, 0, 0, config);

  GST_INFO ("unitsizes: %d -> %d", in_info->bpf, out_info->bpf);

  /* step 1, unpack */
  prev = chain_unpack (convert);
  /* step 2, optional convert from S32 to F64 for channel mix */
  prev = chain_convert_in (convert, prev);
  /* step 3, channel mix */
  prev = chain_mix (convert, prev);
  /* step 4, resample */
  prev = chain_resample (convert, prev);
  /* step 5, optional convert for quantize */
  prev = chain_convert_out (convert, prev);
  /* step 6, optional quantize */
  prev = chain_quantize (convert, prev);
  /* step 7, pack */
  convert->pack_chain = chain_pack (convert, prev);

  convert->convert = converter_generic;

  /* optimize */
  if (out_info->finfo->format == in_info->finfo->format
      && convert->mix_passthrough) {
    if (convert->resampler == NULL) {
      GST_INFO
          ("same formats, no resampler and passthrough mixing -> passthrough");
      convert->convert = converter_passthrough;
    } else {
      if (is_intermediate_format (in_info->finfo->format)) {
        GST_INFO ("same formats, and passthrough mixing -> only resampling");
        convert->convert = converter_resample;
      }
    }
  }

  setup_allocators (convert);

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

  if (convert->unpack_chain)
    audio_chain_free (convert->unpack_chain);
  if (convert->convert_in_chain)
    audio_chain_free (convert->convert_in_chain);
  if (convert->mix_chain)
    audio_chain_free (convert->mix_chain);
  if (convert->resample_chain)
    audio_chain_free (convert->resample_chain);
  if (convert->convert_out_chain)
    audio_chain_free (convert->convert_out_chain);
  if (convert->quant_chain)
    audio_chain_free (convert->quant_chain);
  if (convert->quant)
    gst_audio_quantize_free (convert->quant);
  if (convert->mix)
    gst_audio_channel_mixer_free (convert->mix);
  if (convert->resampler)
    gst_audio_resampler_free (convert->resampler);
  gst_audio_info_init (&convert->in);
  gst_audio_info_init (&convert->out);

  gst_structure_free (convert->config);

  g_slice_free (GstAudioConverter, convert);
}

/**
 * gst_audio_converter_get_out_frames:
 * @convert: a #GstAudioConverter
 * @in_frames: number of input frames
 *
 * Calculate how many output frames can be produced when @in_frames input
 * frames are given to @convert.
 *
 * Returns: the number of output frames
 */
gsize
gst_audio_converter_get_out_frames (GstAudioConverter * convert,
    gsize in_frames)
{
  if (convert->resampler)
    return gst_audio_resampler_get_out_frames (convert->resampler, in_frames);
  else
    return in_frames;
}

/**
 * gst_audio_converter_get_in_frames:
 * @convert: a #GstAudioConverter
 * @out_frames: number of output frames
 *
 * Calculate how many input frames are currently needed by @convert to produce
 * @out_frames of output frames.
 *
 * Returns: the number of input frames
 */
gsize
gst_audio_converter_get_in_frames (GstAudioConverter * convert,
    gsize out_frames)
{
  if (convert->resampler)
    return gst_audio_resampler_get_in_frames (convert->resampler, out_frames);
  else
    return out_frames;
}

/**
 * gst_audio_converter_get_max_latency:
 * @convert: a #GstAudioConverter
 *
 * Get the maximum number of input frames that the converter would
 * need before producing output.
 *
 * Returns: the latency of @convert as expressed in the number of
 * frames.
 */
gsize
gst_audio_converter_get_max_latency (GstAudioConverter * convert)
{
  if (convert->resampler)
    return gst_audio_resampler_get_max_latency (convert->resampler);
  else
    return 0;
}

/**
 * gst_audio_converter_reset:
 * @convert: a #GstAudioConverter
 *
 * Reset @convert to the state it was when it was first created, clearing
 * any history it might currently have.
 */
void
gst_audio_converter_reset (GstAudioConverter * convert)
{
  if (convert->resampler)
    gst_audio_resampler_reset (convert->resampler);
  if (convert->quant)
    gst_audio_quantize_reset (convert->quant);
}

/**
 * gst_audio_converter_samples:
 * @convert: a #GstAudioConverter
 * @flags: extra #GstAudioConverterFlags
 * @in: input frames
 * @in_frames: number of input frames
 * @out: output frames
 * @out_frames: number of output frames
 *
 * Perform the conversion with @in_frames in @in to @out_frames in @out
 * using @convert.
 *
 * In case the samples are interleaved, @in and @out must point to an
 * array with a single element pointing to a block of interleaved samples.
 *
 * If non-interleaved samples are used, @in and @out must point to an
 * array with pointers to memory blocks, one for each channel.
 *
 * @in may be %NULL, in which case @in_frames of silence samples are processed
 * by the converter.
 *
 * This function always produces @out_frames of output and consumes @in_frames of
 * input. Use gst_audio_converter_get_out_frames() and
 * gst_audio_converter_get_in_frames() to make sure @in_frames and @out_frames
 * are matching and @in and @out point to enough memory.
 *
 * Returns: %TRUE is the conversion could be performed.
 */
gboolean
gst_audio_converter_samples (GstAudioConverter * convert,
    GstAudioConverterFlags flags, gpointer in[], gsize in_frames,
    gpointer out[], gsize out_frames)
{
  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail (out != NULL, FALSE);

  if (in_frames == 0) {
    GST_LOG ("skipping empty buffer");
    return TRUE;
  }
  return convert->convert (convert, flags, in, in_frames, out, out_frames);
}
