/* GStreamer
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2003,2004 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2007-2008 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

/**
 * SECTION:element-audioresample
 *
 * audioresample resamples raw audio buffers to different sample rates using
 * a configurable windowing function to enhance quality.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v filesrc location=sine.ogg ! oggdemux ! vorbisdec ! audioconvert ! audioresample ! audio/x-raw-int, rate=8000 ! alsasink
 * ]| Decode an Ogg/Vorbis downsample to 8Khz and play sound through alsa.
 * To create the Ogg/Vorbis file refer to the documentation of vorbisenc.
 * </refsect2>
 */

/* TODO:
 *  - Enable SSE/ARM optimizations and select at runtime
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <math.h>

#include "gstaudioresample.h"
#include <gst/gstutils.h>
#include <gst/audio/audio.h>
#include <gst/base/gstbasetransform.h>

#ifndef DISABLE_ORC
#include <orc/orc.h>
#include <orc-test/orctest.h>
#include <orc-test/orcprofile.h>
#endif

GST_DEBUG_CATEGORY (audio_resample_debug);
#define GST_CAT_DEFAULT audio_resample_debug

enum
{
  PROP_0,
  PROP_QUALITY,
  PROP_FILTER_LENGTH
};

#define SUPPORTED_CAPS \
GST_STATIC_CAPS ( \
    "audio/x-raw-float, " \
      "rate = (int) [ 1, MAX ], "	\
      "channels = (int) [ 1, MAX ], " \
      "endianness = (int) BYTE_ORDER, " \
      "width = (int) { 32, 64 }; " \
    "audio/x-raw-int, " \
      "rate = (int) [ 1, MAX ], " \
      "channels = (int) [ 1, MAX ], " \
      "endianness = (int) BYTE_ORDER, " \
      "width = (int) 32, " \
      "depth = (int) 32, " \
      "signed = (boolean) true; " \
    "audio/x-raw-int, " \
      "rate = (int) [ 1, MAX ], " \
      "channels = (int) [ 1, MAX ], " \
      "endianness = (int) BYTE_ORDER, " \
      "width = (int) 24, " \
      "depth = (int) 24, " \
      "signed = (boolean) true; " \
    "audio/x-raw-int, " \
      "rate = (int) [ 1, MAX ], " \
      "channels = (int) [ 1, MAX ], " \
      "endianness = (int) BYTE_ORDER, " \
      "width = (int) 16, " \
      "depth = (int) 16, " \
      "signed = (boolean) true; " \
    "audio/x-raw-int, " \
      "rate = (int) [ 1, MAX ], " \
      "channels = (int) [ 1, MAX ], " \
      "endianness = (int) BYTE_ORDER, " \
      "width = (int) 8, " \
      "depth = (int) 8, " \
      "signed = (boolean) true" \
)

/* If TRUE integer arithmetic resampling is faster and will be used if appropriate */
#if defined AUDIORESAMPLE_FORMAT_INT
static gboolean gst_audio_resample_use_int = TRUE;
#elif defined AUDIORESAMPLE_FORMAT_FLOAT
static gboolean gst_audio_resample_use_int = FALSE;
#else
static gboolean gst_audio_resample_use_int = FALSE;
#endif

static GstStaticPadTemplate gst_audio_resample_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK, GST_PAD_ALWAYS, SUPPORTED_CAPS);

static GstStaticPadTemplate gst_audio_resample_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC, GST_PAD_ALWAYS, SUPPORTED_CAPS);

static void gst_audio_resample_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_audio_resample_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

/* vmethods */
static gboolean gst_audio_resample_get_unit_size (GstBaseTransform * base,
    GstCaps * caps, guint * size);
static GstCaps *gst_audio_resample_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps);
static void gst_audio_resample_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_audio_resample_transform_size (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * incaps, guint insize,
    GstCaps * outcaps, guint * outsize);
static gboolean gst_audio_resample_set_caps (GstBaseTransform * base,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_audio_resample_transform (GstBaseTransform * base,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_audio_resample_event (GstBaseTransform * base,
    GstEvent * event);
static gboolean gst_audio_resample_start (GstBaseTransform * base);
static gboolean gst_audio_resample_stop (GstBaseTransform * base);
static gboolean gst_audio_resample_query (GstPad * pad, GstQuery * query);
static const GstQueryType *gst_audio_resample_query_type (GstPad * pad);

GST_BOILERPLATE (GstAudioResample, gst_audio_resample, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM);

static void
gst_audio_resample_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_audio_resample_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_audio_resample_sink_template);

  gst_element_class_set_details_simple (gstelement_class, "Audio resampler",
      "Filter/Converter/Audio", "Resamples audio",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_audio_resample_class_init (GstAudioResampleClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_audio_resample_set_property;
  gobject_class->get_property = gst_audio_resample_get_property;

  g_object_class_install_property (gobject_class, PROP_QUALITY,
      g_param_spec_int ("quality", "Quality", "Resample quality with 0 being "
          "the lowest and 10 being the best",
          SPEEX_RESAMPLER_QUALITY_MIN, SPEEX_RESAMPLER_QUALITY_MAX,
          SPEEX_RESAMPLER_QUALITY_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /* FIXME 0.11: Remove this property, it's just for compatibility
   * with old audioresample
   */
  /**
   * GstAudioResample:filter-length:
   *
   * Length of the resample filter
   *
   * Deprecated: Use #GstAudioResample:quality property instead
   */
  g_object_class_install_property (gobject_class, PROP_FILTER_LENGTH,
      g_param_spec_int ("filter-length", "Filter length",
          "Length of the resample filter", 0, G_MAXINT, 64,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_BASE_TRANSFORM_CLASS (klass)->start =
      GST_DEBUG_FUNCPTR (gst_audio_resample_start);
  GST_BASE_TRANSFORM_CLASS (klass)->stop =
      GST_DEBUG_FUNCPTR (gst_audio_resample_stop);
  GST_BASE_TRANSFORM_CLASS (klass)->transform_size =
      GST_DEBUG_FUNCPTR (gst_audio_resample_transform_size);
  GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_audio_resample_get_unit_size);
  GST_BASE_TRANSFORM_CLASS (klass)->transform_caps =
      GST_DEBUG_FUNCPTR (gst_audio_resample_transform_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_audio_resample_fixate_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->set_caps =
      GST_DEBUG_FUNCPTR (gst_audio_resample_set_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->transform =
      GST_DEBUG_FUNCPTR (gst_audio_resample_transform);
  GST_BASE_TRANSFORM_CLASS (klass)->event =
      GST_DEBUG_FUNCPTR (gst_audio_resample_event);

  GST_BASE_TRANSFORM_CLASS (klass)->passthrough_on_same_caps = TRUE;
}

static void
gst_audio_resample_init (GstAudioResample * resample,
    GstAudioResampleClass * klass)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM (resample);

  resample->quality = SPEEX_RESAMPLER_QUALITY_DEFAULT;

  gst_base_transform_set_gap_aware (trans, TRUE);
  gst_pad_set_query_function (trans->srcpad, gst_audio_resample_query);
  gst_pad_set_query_type_function (trans->srcpad,
      gst_audio_resample_query_type);
}

/* vmethods */
static gboolean
gst_audio_resample_start (GstBaseTransform * base)
{
  GstAudioResample *resample = GST_AUDIO_RESAMPLE (base);

  resample->need_discont = TRUE;

  resample->num_gap_samples = 0;
  resample->num_nongap_samples = 0;
  resample->t0 = GST_CLOCK_TIME_NONE;
  resample->in_offset0 = GST_BUFFER_OFFSET_NONE;
  resample->out_offset0 = GST_BUFFER_OFFSET_NONE;
  resample->samples_in = 0;
  resample->samples_out = 0;

  resample->tmp_in = NULL;
  resample->tmp_in_size = 0;
  resample->tmp_out = NULL;
  resample->tmp_out_size = 0;

  return TRUE;
}

static gboolean
gst_audio_resample_stop (GstBaseTransform * base)
{
  GstAudioResample *resample = GST_AUDIO_RESAMPLE (base);

  if (resample->state) {
    resample->funcs->destroy (resample->state);
    resample->state = NULL;
  }

  resample->funcs = NULL;

  g_free (resample->tmp_in);
  resample->tmp_in = NULL;
  resample->tmp_in_size = 0;

  g_free (resample->tmp_out);
  resample->tmp_out = NULL;
  resample->tmp_out_size = 0;

  gst_caps_replace (&resample->sinkcaps, NULL);
  gst_caps_replace (&resample->srccaps, NULL);

  return TRUE;
}

static gboolean
gst_audio_resample_get_unit_size (GstBaseTransform * base, GstCaps * caps,
    guint * size)
{
  gint width, channels;
  GstStructure *structure;
  gboolean ret;

  g_return_val_if_fail (size != NULL, FALSE);

  /* this works for both float and int */
  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width", &width);
  ret &= gst_structure_get_int (structure, "channels", &channels);

  if (G_UNLIKELY (!ret))
    return FALSE;

  *size = (width / 8) * channels;

  return TRUE;
}

static GstCaps *
gst_audio_resample_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps)
{
  const GValue *val;
  GstStructure *s;
  GstCaps *res;

  /* transform single caps into input_caps + input_caps with the rate
   * field set to our supported range. This ensures that upstream knows
   * about downstream's prefered rate(s) and can negotiate accordingly. */
  res = gst_caps_copy (caps);

  /* first, however, check if the caps contain a range for the rate field, in
   * which case that side isn't going to care much about the exact sample rate
   * chosen and we should just assume things will get fixated to something sane
   * and we may just as well offer our full range instead of the range in the
   * caps. If the rate is not an int range value, it's likely to express a
   * real preference or limitation and we should maintain that structure as
   * preference by putting it first into the transformed caps, and only add
   * our full rate range as second option  */
  s = gst_caps_get_structure (res, 0);
  val = gst_structure_get_value (s, "rate");
  if (val == NULL || GST_VALUE_HOLDS_INT_RANGE (val)) {
    /* overwrite existing range, or add field if it doesn't exist yet */
    gst_structure_set (s, "rate", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
  } else {
    /* append caps with full range to existing caps with non-range rate field */
    s = gst_structure_copy (s);
    gst_structure_set (s, "rate", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
    gst_caps_append_structure (res, s);
  }

  return res;
}

/* Fixate rate to the allowed rate that has the smallest difference */
static void
gst_audio_resample_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstStructure *s;
  gint rate;

  s = gst_caps_get_structure (caps, 0);
  if (G_UNLIKELY (!gst_structure_get_int (s, "rate", &rate)))
    return;

  s = gst_caps_get_structure (othercaps, 0);
  gst_structure_fixate_field_nearest_int (s, "rate", rate);
}

static const SpeexResampleFuncs *
gst_audio_resample_get_funcs (gint width, gboolean fp)
{
  const SpeexResampleFuncs *funcs = NULL;

  if (gst_audio_resample_use_int && (width == 8 || width == 16) && !fp)
    funcs = &int_funcs;
  else if ((!gst_audio_resample_use_int && (width == 8 || width == 16) && !fp)
      || (width == 32 && fp))
    funcs = &float_funcs;
  else if ((width == 64 && fp) || ((width == 32 || width == 24) && !fp))
    funcs = &double_funcs;
  else
    g_assert_not_reached ();

  return funcs;
}

static SpeexResamplerState *
gst_audio_resample_init_state (GstAudioResample * resample, gint width,
    gint channels, gint inrate, gint outrate, gint quality, gboolean fp)
{
  SpeexResamplerState *ret = NULL;
  gint err = RESAMPLER_ERR_SUCCESS;
  const SpeexResampleFuncs *funcs = gst_audio_resample_get_funcs (width, fp);

  ret = funcs->init (channels, inrate, outrate, quality, &err);

  if (G_UNLIKELY (err != RESAMPLER_ERR_SUCCESS)) {
    GST_ERROR_OBJECT (resample, "Failed to create resampler state: %s",
        funcs->strerror (err));
    return NULL;
  }

  funcs->skip_zeros (ret);

  return ret;
}

static gboolean
gst_audio_resample_update_state (GstAudioResample * resample, gint width,
    gint channels, gint inrate, gint outrate, gint quality, gboolean fp)
{
  gboolean ret = TRUE;
  gboolean updated_latency = FALSE;

  updated_latency = (resample->inrate != inrate
      || quality != resample->quality) && resample->state != NULL;

  if (resample->state == NULL) {
    ret = TRUE;
  } else if (resample->channels != channels || fp != resample->fp
      || width != resample->width) {
    resample->funcs->destroy (resample->state);
    resample->state =
        gst_audio_resample_init_state (resample, width, channels, inrate,
        outrate, quality, fp);

    resample->funcs = gst_audio_resample_get_funcs (width, fp);
    ret = (resample->state != NULL);
  } else if (resample->inrate != inrate || resample->outrate != outrate) {
    gint err = RESAMPLER_ERR_SUCCESS;

    err = resample->funcs->set_rate (resample->state, inrate, outrate);

    if (G_UNLIKELY (err != RESAMPLER_ERR_SUCCESS))
      GST_ERROR_OBJECT (resample, "Failed to update rate: %s",
          resample->funcs->strerror (err));

    ret = (err == RESAMPLER_ERR_SUCCESS);
  } else if (quality != resample->quality) {
    gint err = RESAMPLER_ERR_SUCCESS;

    err = resample->funcs->set_quality (resample->state, quality);

    if (G_UNLIKELY (err != RESAMPLER_ERR_SUCCESS))
      GST_ERROR_OBJECT (resample, "Failed to update quality: %s",
          resample->funcs->strerror (err));

    ret = (err == RESAMPLER_ERR_SUCCESS);
  }

  resample->width = width;
  resample->channels = channels;
  resample->fp = fp;
  resample->quality = quality;
  resample->inrate = inrate;
  resample->outrate = outrate;

  if (updated_latency)
    gst_element_post_message (GST_ELEMENT (resample),
        gst_message_new_latency (GST_OBJECT (resample)));

  return ret;
}

static void
gst_audio_resample_reset_state (GstAudioResample * resample)
{
  if (resample->state)
    resample->funcs->reset_mem (resample->state);
}

static gboolean
gst_audio_resample_parse_caps (GstCaps * incaps,
    GstCaps * outcaps, gint * width, gint * channels, gint * inrate,
    gint * outrate, gboolean * fp)
{
  GstStructure *structure;
  gboolean ret;
  gint mywidth, myinrate, myoutrate, mychannels;
  gboolean myfp;

  GST_DEBUG ("incaps %" GST_PTR_FORMAT ", outcaps %"
      GST_PTR_FORMAT, incaps, outcaps);

  structure = gst_caps_get_structure (incaps, 0);

  if (gst_structure_has_name (structure, "audio/x-raw-float"))
    myfp = TRUE;
  else
    myfp = FALSE;

  ret = gst_structure_get_int (structure, "rate", &myinrate);
  ret &= gst_structure_get_int (structure, "channels", &mychannels);
  ret &= gst_structure_get_int (structure, "width", &mywidth);
  if (G_UNLIKELY (!ret))
    goto no_in_rate_channels;

  structure = gst_caps_get_structure (outcaps, 0);
  ret = gst_structure_get_int (structure, "rate", &myoutrate);
  if (G_UNLIKELY (!ret))
    goto no_out_rate;

  if (channels)
    *channels = mychannels;
  if (inrate)
    *inrate = myinrate;
  if (outrate)
    *outrate = myoutrate;
  if (width)
    *width = mywidth;
  if (fp)
    *fp = myfp;

  return TRUE;

  /* ERRORS */
no_in_rate_channels:
  {
    GST_DEBUG ("could not get input rate and channels");
    return FALSE;
  }
no_out_rate:
  {
    GST_DEBUG ("could not get output rate");
    return FALSE;
  }
}

static gint
_gcd (gint a, gint b)
{
  while (b != 0) {
    int temp = a;

    a = b;
    b = temp % b;
  }

  return ABS (a);
}

static gboolean
gst_audio_resample_transform_size (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, guint size, GstCaps * othercaps,
    guint * othersize)
{
  gboolean ret = TRUE;
  guint32 ratio_den, ratio_num;
  gint inrate, outrate, gcd;
  gint bytes_per_samp, channels;

  GST_LOG_OBJECT (base, "asked to transform size %d in direction %s",
      size, direction == GST_PAD_SINK ? "SINK" : "SRC");

  /* Get sample width -> bytes_per_samp, channels, inrate, outrate */
  ret =
      gst_audio_resample_parse_caps (caps, othercaps, &bytes_per_samp,
      &channels, &inrate, &outrate, NULL);
  if (G_UNLIKELY (!ret)) {
    GST_ERROR_OBJECT (base, "Wrong caps");
    return FALSE;
  }
  /* Number of samples in either buffer is size / (width*channels) ->
   * calculate the factor */
  bytes_per_samp = bytes_per_samp * channels / 8;
  /* Convert source buffer size to samples */
  size /= bytes_per_samp;

  /* Simplify the conversion ratio factors */
  gcd = _gcd (inrate, outrate);
  ratio_num = inrate / gcd;
  ratio_den = outrate / gcd;

  if (direction == GST_PAD_SINK) {
    /* asked to convert size of an incoming buffer. Round up the output size */
    *othersize = gst_util_uint64_scale_int_ceil (size, ratio_den, ratio_num);
    *othersize *= bytes_per_samp;
  } else {
    /* asked to convert size of an outgoing buffer. Round down the input size */
    *othersize = gst_util_uint64_scale_int (size, ratio_num, ratio_den);
    *othersize *= bytes_per_samp;
  }

  GST_LOG_OBJECT (base, "transformed size %d to %d", size * bytes_per_samp,
      *othersize);

  return ret;
}

static gboolean
gst_audio_resample_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  gboolean ret;
  gint width = 0, inrate = 0, outrate = 0, channels = 0;
  gboolean fp;
  GstAudioResample *resample = GST_AUDIO_RESAMPLE (base);

  GST_LOG ("incaps %" GST_PTR_FORMAT ", outcaps %"
      GST_PTR_FORMAT, incaps, outcaps);

  ret = gst_audio_resample_parse_caps (incaps, outcaps,
      &width, &channels, &inrate, &outrate, &fp);

  if (G_UNLIKELY (!ret))
    return FALSE;

  ret =
      gst_audio_resample_update_state (resample, width, channels, inrate,
      outrate, resample->quality, fp);

  if (G_UNLIKELY (!ret))
    return FALSE;

  /* save caps so we can short-circuit in the size_transform if the caps
   * are the same */
  gst_caps_replace (&resample->sinkcaps, incaps);
  gst_caps_replace (&resample->srccaps, outcaps);

  return TRUE;
}

#define GST_MAXINT24 (8388607)
#define GST_MININT24 (-8388608)

#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
#define GST_READ_UINT24 GST_READ_UINT24_LE
#define GST_WRITE_UINT24 GST_WRITE_UINT24_LE
#else
#define GST_READ_UINT24 GST_READ_UINT24_BE
#define GST_WRITE_UINT24 GST_WRITE_UINT24_BE
#endif

static void
gst_audio_resample_convert_buffer (GstAudioResample * resample,
    const guint8 * in, guint8 * out, guint len, gboolean inverse)
{
  len *= resample->channels;

  if (inverse) {
    if (gst_audio_resample_use_int && resample->width == 8 && !resample->fp) {
      gint8 *o = (gint8 *) out;
      gint16 *i = (gint16 *) in;
      gint32 tmp;

      while (len) {
        tmp = *i + (G_MAXINT8 >> 1);
        *o = CLAMP (tmp >> 8, G_MININT8, G_MAXINT8);
        o++;
        i++;
        len--;
      }
    } else if (!gst_audio_resample_use_int && resample->width == 8
        && !resample->fp) {
      gint8 *o = (gint8 *) out;
      gfloat *i = (gfloat *) in;
      gfloat tmp;

      while (len) {
        tmp = *i;
        *o = (gint8) CLAMP (tmp * G_MAXINT8 + 0.5, G_MININT8, G_MAXINT8);
        o++;
        i++;
        len--;
      }
    } else if (!gst_audio_resample_use_int && resample->width == 16
        && !resample->fp) {
      gint16 *o = (gint16 *) out;
      gfloat *i = (gfloat *) in;
      gfloat tmp;

      while (len) {
        tmp = *i;
        *o = (gint16) CLAMP (tmp * G_MAXINT16 + 0.5, G_MININT16, G_MAXINT16);
        o++;
        i++;
        len--;
      }
    } else if (resample->width == 24 && !resample->fp) {
      guint8 *o = (guint8 *) out;
      gdouble *i = (gdouble *) in;
      gdouble tmp;

      while (len) {
        tmp = *i;
        GST_WRITE_UINT24 (o, (gint32) CLAMP (tmp * GST_MAXINT24 + 0.5,
                GST_MININT24, GST_MAXINT24));
        o += 3;
        i++;
        len--;
      }
    } else if (resample->width == 32 && !resample->fp) {
      gint32 *o = (gint32 *) out;
      gdouble *i = (gdouble *) in;
      gdouble tmp;

      while (len) {
        tmp = *i;
        *o = (gint32) CLAMP (tmp * G_MAXINT32 + 0.5, G_MININT32, G_MAXINT32);
        o++;
        i++;
        len--;
      }
    } else {
      g_assert_not_reached ();
    }
  } else {
    if (gst_audio_resample_use_int && resample->width == 8 && !resample->fp) {
      gint8 *i = (gint8 *) in;
      gint16 *o = (gint16 *) out;
      gint32 tmp;

      while (len) {
        tmp = *i;
        *o = tmp << 8;
        o++;
        i++;
        len--;
      }
    } else if (!gst_audio_resample_use_int && resample->width == 8
        && !resample->fp) {
      gint8 *i = (gint8 *) in;
      gfloat *o = (gfloat *) out;
      gfloat tmp;

      while (len) {
        tmp = *i;
        *o = tmp / G_MAXINT8;
        o++;
        i++;
        len--;
      }
    } else if (!gst_audio_resample_use_int && resample->width == 16
        && !resample->fp) {
      gint16 *i = (gint16 *) in;
      gfloat *o = (gfloat *) out;
      gfloat tmp;

      while (len) {
        tmp = *i;
        *o = tmp / G_MAXINT16;
        o++;
        i++;
        len--;
      }
    } else if (resample->width == 24 && !resample->fp) {
      guint8 *i = (guint8 *) in;
      gdouble *o = (gdouble *) out;
      gdouble tmp;
      guint32 tmp2;

      while (len) {
        tmp2 = GST_READ_UINT24 (i);
        if (tmp2 & 0x00800000)
          tmp2 |= 0xff000000;
        tmp = (gint32) tmp2;
        *o = tmp / GST_MAXINT24;
        o++;
        i += 3;
        len--;
      }
    } else if (resample->width == 32 && !resample->fp) {
      gint32 *i = (gint32 *) in;
      gdouble *o = (gdouble *) out;
      gdouble tmp;

      while (len) {
        tmp = *i;
        *o = tmp / G_MAXINT32;
        o++;
        i++;
        len--;
      }
    } else {
      g_assert_not_reached ();
    }
  }
}

static guint8 *
gst_audio_resample_workspace_realloc (guint8 ** workspace, guint * size,
    guint new_size)
{
  guint8 *new;
  if (new_size <= *size)
    /* no need to resize */
    return *workspace;
  new = g_realloc (*workspace, new_size);
  if (!new)
    /* failure (re)allocating memeory */
    return NULL;
  /* success */
  *workspace = new;
  *size = new_size;
  return *workspace;
}

/* Push history_len zeros into the filter, but discard the output. */
static void
gst_audio_resample_dump_drain (GstAudioResample * resample, guint history_len)
{
  gint outsize;
  guint in_len, in_processed;
  guint out_len, out_processed;
  guint num, den;
  gpointer buf;

  g_assert (resample->state != NULL);

  resample->funcs->get_ratio (resample->state, &num, &den);

  in_len = in_processed = history_len;
  out_processed = out_len =
      gst_util_uint64_scale_int_ceil (history_len, den, num);
  outsize = out_len * resample->channels * (resample->funcs->width / 8);

  if (out_len == 0)
    return;

  buf = g_malloc (outsize);
  resample->funcs->process (resample->state, NULL, &in_processed, buf,
      &out_processed);
  g_free (buf);

  g_assert (in_len == in_processed);
}

static void
gst_audio_resample_push_drain (GstAudioResample * resample, guint history_len)
{
  GstBuffer *outbuf;
  GstFlowReturn res;
  gint outsize;
  guint in_len, in_processed;
  guint out_len, out_processed;
  gint err;
  guint num, den;

  g_assert (resample->state != NULL);

  /* Don't drain samples if we were reset. */
  if (!GST_CLOCK_TIME_IS_VALID (resample->t0))
    return;

  resample->funcs->get_ratio (resample->state, &num, &den);

  in_len = in_processed = history_len;
  out_len = out_processed =
      gst_util_uint64_scale_int_ceil (history_len, den, num);
  outsize = out_len * resample->channels * (resample->width / 8);

  if (out_len == 0)
    return;

  res =
      gst_pad_alloc_buffer_and_set_caps (GST_BASE_TRANSFORM_SRC_PAD (resample),
      GST_BUFFER_OFFSET_NONE, outsize,
      GST_PAD_CAPS (GST_BASE_TRANSFORM_SRC_PAD (resample)), &outbuf);
  if (G_UNLIKELY (res != GST_FLOW_OK)) {
    GST_WARNING_OBJECT (resample, "failed allocating buffer of %d bytes",
        outsize);
    return;
  }

  if (resample->funcs->width != resample->width) {
    /* need to convert data format;  allocate workspace */
    if (!gst_audio_resample_workspace_realloc (&resample->tmp_out,
            &resample->tmp_out_size, (resample->funcs->width / 8) * out_len *
            resample->channels)) {
      GST_ERROR_OBJECT (resample, "failed to allocate workspace");
      return;
    }

    /* process */
    err = resample->funcs->process (resample->state, NULL, &in_processed,
        resample->tmp_out, &out_processed);

    /* convert output format */
    gst_audio_resample_convert_buffer (resample, resample->tmp_out,
        GST_BUFFER_DATA (outbuf), out_processed, TRUE);
  } else {
    /* don't need to convert data format;  process */
    err = resample->funcs->process (resample->state, NULL, &in_processed,
        GST_BUFFER_DATA (outbuf), &out_processed);
  }

  /* If we wrote more than allocated something is really wrong now
   * and we should better abort immediately */
  g_assert (out_len >= out_processed);

  if (G_UNLIKELY (err != RESAMPLER_ERR_SUCCESS)) {
    GST_WARNING_OBJECT (resample, "Failed to process drain: %s",
        resample->funcs->strerror (err));
    gst_buffer_unref (outbuf);
    return;
  }

  /* time */
  if (GST_CLOCK_TIME_IS_VALID (resample->t0)) {
    GST_BUFFER_TIMESTAMP (outbuf) = resample->t0 +
        gst_util_uint64_scale_int_round (resample->samples_out, GST_SECOND,
        resample->outrate);
    GST_BUFFER_DURATION (outbuf) = resample->t0 +
        gst_util_uint64_scale_int_round (resample->samples_out + out_processed,
        GST_SECOND, resample->outrate) - GST_BUFFER_TIMESTAMP (outbuf);
  } else {
    GST_BUFFER_TIMESTAMP (outbuf) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION (outbuf) = GST_CLOCK_TIME_NONE;
  }
  /* offset */
  if (resample->out_offset0 != GST_BUFFER_OFFSET_NONE) {
    GST_BUFFER_OFFSET (outbuf) = resample->out_offset0 + resample->samples_out;
    GST_BUFFER_OFFSET_END (outbuf) = GST_BUFFER_OFFSET (outbuf) + out_processed;
  } else {
    GST_BUFFER_OFFSET (outbuf) = GST_BUFFER_OFFSET_NONE;
    GST_BUFFER_OFFSET_END (outbuf) = GST_BUFFER_OFFSET_NONE;
  }
  /* move along */
  resample->samples_out += out_processed;
  resample->samples_in += history_len;

  if (G_UNLIKELY (out_processed == 0 && in_len * den > num)) {
    GST_WARNING_OBJECT (resample, "Failed to get drain, dropping buffer");
    gst_buffer_unref (outbuf);
    return;
  }

  GST_BUFFER_SIZE (outbuf) =
      out_processed * resample->channels * (resample->width / 8);

  GST_LOG_OBJECT (resample,
      "Pushing drain buffer of %u bytes with timestamp %" GST_TIME_FORMAT
      " duration %" GST_TIME_FORMAT " offset %" G_GUINT64_FORMAT " offset_end %"
      G_GUINT64_FORMAT, GST_BUFFER_SIZE (outbuf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)), GST_BUFFER_OFFSET (outbuf),
      GST_BUFFER_OFFSET_END (outbuf));

  res = gst_pad_push (GST_BASE_TRANSFORM_SRC_PAD (resample), outbuf);

  if (G_UNLIKELY (res != GST_FLOW_OK))
    GST_WARNING_OBJECT (resample, "Failed to push drain: %s",
        gst_flow_get_name (res));

  return;
}

static gboolean
gst_audio_resample_event (GstBaseTransform * base, GstEvent * event)
{
  GstAudioResample *resample = GST_AUDIO_RESAMPLE (base);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_audio_resample_reset_state (resample);
      if (resample->state)
        resample->funcs->skip_zeros (resample->state);
      resample->num_gap_samples = 0;
      resample->num_nongap_samples = 0;
      resample->t0 = GST_CLOCK_TIME_NONE;
      resample->in_offset0 = GST_BUFFER_OFFSET_NONE;
      resample->out_offset0 = GST_BUFFER_OFFSET_NONE;
      resample->samples_in = 0;
      resample->samples_out = 0;
      resample->need_discont = TRUE;
      break;
    case GST_EVENT_NEWSEGMENT:
      if (resample->state) {
        guint latency = resample->funcs->get_input_latency (resample->state);
        gst_audio_resample_push_drain (resample, latency);
      }
      gst_audio_resample_reset_state (resample);
      if (resample->state)
        resample->funcs->skip_zeros (resample->state);
      resample->num_gap_samples = 0;
      resample->num_nongap_samples = 0;
      resample->t0 = GST_CLOCK_TIME_NONE;
      resample->in_offset0 = GST_BUFFER_OFFSET_NONE;
      resample->out_offset0 = GST_BUFFER_OFFSET_NONE;
      resample->samples_in = 0;
      resample->samples_out = 0;
      resample->need_discont = TRUE;
      break;
    case GST_EVENT_EOS:
      if (resample->state) {
        guint latency = resample->funcs->get_input_latency (resample->state);
        gst_audio_resample_push_drain (resample, latency);
      }
      gst_audio_resample_reset_state (resample);
      break;
    default:
      break;
  }

  return parent_class->event (base, event);
}

static gboolean
gst_audio_resample_check_discont (GstAudioResample * resample, GstBuffer * buf)
{
  guint64 offset;
  guint64 delta;

  /* is the incoming buffer a discontinuity? */
  if (G_UNLIKELY (GST_BUFFER_IS_DISCONT (buf)))
    return TRUE;

  /* no valid timestamps or offsets to compare --> no discontinuity */
  if (G_UNLIKELY (!(GST_BUFFER_TIMESTAMP_IS_VALID (buf) &&
              GST_CLOCK_TIME_IS_VALID (resample->t0))))
    return FALSE;

  /* convert the inbound timestamp to an offset. */
  offset =
      gst_util_uint64_scale_int_round (GST_BUFFER_TIMESTAMP (buf) -
      resample->t0, resample->inrate, GST_SECOND);

  /* many elements generate imperfect streams due to rounding errors, so we
   * permit a small error (up to one sample) without triggering a filter
   * flush/restart (if triggered incorrectly, this will be audible) */
  /* allow even up to more samples, since sink is not so strict anyway,
   * so give that one a chance to handle this as configured */
  delta = ABS ((gint64) (offset - resample->samples_in));
  if (delta <= (resample->inrate >> 5))
    return FALSE;

  GST_WARNING_OBJECT (resample,
      "encountered timestamp discontinuity of %" G_GUINT64_FORMAT " samples = %"
      GST_TIME_FORMAT, delta,
      GST_TIME_ARGS (gst_util_uint64_scale_int_round (delta, GST_SECOND,
              resample->inrate)));
  return TRUE;
}

static GstFlowReturn
gst_audio_resample_process (GstAudioResample * resample, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  guint32 in_len, in_processed;
  guint32 out_len, out_processed;
  guint filt_len = resample->funcs->get_filt_len (resample->state);

  in_len = GST_BUFFER_SIZE (inbuf) / resample->channels;
  out_len = GST_BUFFER_SIZE (outbuf) / resample->channels;

  in_len /= (resample->width / 8);
  out_len /= (resample->width / 8);

  in_processed = in_len;
  out_processed = out_len;

  if (GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_GAP)) {
    resample->num_nongap_samples = 0;
    if (resample->num_gap_samples < filt_len) {
      guint zeros_to_push;
      if (in_len >= filt_len - resample->num_gap_samples)
        zeros_to_push = filt_len - resample->num_gap_samples;
      else
        zeros_to_push = in_len;

      gst_audio_resample_push_drain (resample, zeros_to_push);
      in_len -= zeros_to_push;
      resample->num_gap_samples += zeros_to_push;
    }

    {
      guint num, den;
      resample->funcs->get_ratio (resample->state, &num, &den);
      if (resample->samples_in + in_len >= filt_len / 2)
        out_processed =
            gst_util_uint64_scale_int_ceil (resample->samples_in + in_len -
            filt_len / 2, den, num) - resample->samples_out;
      else
        out_processed = 0;

      memset (GST_BUFFER_DATA (outbuf), 0, GST_BUFFER_SIZE (outbuf));
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_GAP);
      resample->num_gap_samples += in_len;
      in_processed = in_len;
    }
  } else {                      /* not a gap */

    gint err;

    if (resample->num_gap_samples > filt_len) {
      /* push in enough zeros to restore the filter to the right offset */
      guint num, den;
      resample->funcs->get_ratio (resample->state, &num, &den);
      gst_audio_resample_dump_drain (resample,
          (resample->num_gap_samples - filt_len) % num);
    }
    resample->num_gap_samples = 0;
    if (resample->num_nongap_samples < filt_len) {
      resample->num_nongap_samples += in_len;
      if (resample->num_nongap_samples > filt_len)
        resample->num_nongap_samples = filt_len;
    }

    if (resample->funcs->width != resample->width) {
      /* need to convert data format for processing;  ensure we have enough
       * workspace available */
      if (!gst_audio_resample_workspace_realloc (&resample->tmp_in,
              &resample->tmp_in_size, in_len * resample->channels *
              (resample->funcs->width / 8)) ||
          !gst_audio_resample_workspace_realloc (&resample->tmp_out,
              &resample->tmp_out_size, out_len * resample->channels *
              (resample->funcs->width / 8))) {
        GST_ERROR_OBJECT (resample, "failed to allocate workspace");
        return GST_FLOW_ERROR;
      }

      /* convert input */
      gst_audio_resample_convert_buffer (resample, GST_BUFFER_DATA (inbuf),
          resample->tmp_in, in_len, FALSE);

      /* process */
      err = resample->funcs->process (resample->state,
          resample->tmp_in, &in_processed, resample->tmp_out, &out_processed);

      /* convert output */
      gst_audio_resample_convert_buffer (resample, resample->tmp_out,
          GST_BUFFER_DATA (outbuf), out_processed, TRUE);
    } else {
      /* no format conversion required;  process */
      err = resample->funcs->process (resample->state,
          GST_BUFFER_DATA (inbuf), &in_processed,
          GST_BUFFER_DATA (outbuf), &out_processed);
    }

    if (G_UNLIKELY (err != RESAMPLER_ERR_SUCCESS)) {
      GST_ERROR_OBJECT (resample, "Failed to convert data: %s",
          resample->funcs->strerror (err));
      return GST_FLOW_ERROR;
    }
  }

  /* If we wrote more than allocated something is really wrong now and we
   * should better abort immediately */
  g_assert (out_len >= out_processed);

  if (G_UNLIKELY (in_len != in_processed)) {
    GST_WARNING_OBJECT (resample, "converted %d of %d input samples",
        in_processed, in_len);
  }

  /* time */
  if (GST_CLOCK_TIME_IS_VALID (resample->t0)) {
    GST_BUFFER_TIMESTAMP (outbuf) = resample->t0 +
        gst_util_uint64_scale_int_round (resample->samples_out, GST_SECOND,
        resample->outrate);
    GST_BUFFER_DURATION (outbuf) = resample->t0 +
        gst_util_uint64_scale_int_round (resample->samples_out + out_processed,
        GST_SECOND, resample->outrate) - GST_BUFFER_TIMESTAMP (outbuf);
  } else {
    GST_BUFFER_TIMESTAMP (outbuf) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION (outbuf) = GST_CLOCK_TIME_NONE;
  }
  /* offset */
  if (resample->out_offset0 != GST_BUFFER_OFFSET_NONE) {
    GST_BUFFER_OFFSET (outbuf) = resample->out_offset0 + resample->samples_out;
    GST_BUFFER_OFFSET_END (outbuf) = GST_BUFFER_OFFSET (outbuf) + out_processed;
  } else {
    GST_BUFFER_OFFSET (outbuf) = GST_BUFFER_OFFSET_NONE;
    GST_BUFFER_OFFSET_END (outbuf) = GST_BUFFER_OFFSET_NONE;
  }
  /* move along */
  resample->samples_out += out_processed;
  resample->samples_in += in_len;

  GST_BUFFER_SIZE (outbuf) =
      out_processed * resample->channels * (resample->width / 8);

  GST_LOG_OBJECT (resample,
      "Converted to buffer of %" G_GUINT32_FORMAT
      " samples (%u bytes) with timestamp %" GST_TIME_FORMAT ", duration %"
      GST_TIME_FORMAT ", offset %" G_GUINT64_FORMAT ", offset_end %"
      G_GUINT64_FORMAT, out_processed, GST_BUFFER_SIZE (outbuf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)),
      GST_BUFFER_OFFSET (outbuf), GST_BUFFER_OFFSET_END (outbuf));

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_audio_resample_transform (GstBaseTransform * base, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstAudioResample *resample = GST_AUDIO_RESAMPLE (base);
  gulong size;
  GstFlowReturn ret;

  if (resample->state == NULL) {
    if (G_UNLIKELY (!(resample->state =
                gst_audio_resample_init_state (resample, resample->width,
                    resample->channels, resample->inrate, resample->outrate,
                    resample->quality, resample->fp))))
      return GST_FLOW_ERROR;

    resample->funcs =
        gst_audio_resample_get_funcs (resample->width, resample->fp);
  }

  size = GST_BUFFER_SIZE (inbuf);

  GST_LOG_OBJECT (resample, "transforming buffer of %ld bytes, ts %"
      GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT ", offset %"
      G_GINT64_FORMAT ", offset_end %" G_GINT64_FORMAT,
      size, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (inbuf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (inbuf)),
      GST_BUFFER_OFFSET (inbuf), GST_BUFFER_OFFSET_END (inbuf));

  /* check for timestamp discontinuities;  flush/reset if needed, and set
   * flag to resync timestamp and offset counters and send event
   * downstream */
  if (G_UNLIKELY (gst_audio_resample_check_discont (resample, inbuf))) {
    gst_audio_resample_reset_state (resample);
    resample->need_discont = TRUE;
  }

  /* handle discontinuity */
  if (G_UNLIKELY (resample->need_discont)) {
    resample->funcs->skip_zeros (resample->state);
    resample->num_gap_samples = 0;
    resample->num_nongap_samples = 0;
    /* reset */
    resample->samples_in = 0;
    resample->samples_out = 0;
    GST_DEBUG_OBJECT (resample, "found discontinuity; resyncing");
    /* resync the timestamp and offset counters if possible */
    if (GST_BUFFER_TIMESTAMP_IS_VALID (inbuf)) {
      resample->t0 = GST_BUFFER_TIMESTAMP (inbuf);
    } else {
      GST_DEBUG_OBJECT (resample, "... but new timestamp is invalid");
      resample->t0 = GST_CLOCK_TIME_NONE;
    }
    if (GST_BUFFER_OFFSET_IS_VALID (inbuf)) {
      resample->in_offset0 = GST_BUFFER_OFFSET (inbuf);
      resample->out_offset0 =
          gst_util_uint64_scale_int_round (resample->in_offset0,
          resample->outrate, resample->inrate);
    } else {
      GST_DEBUG_OBJECT (resample, "... but new offset is invalid");
      resample->in_offset0 = GST_BUFFER_OFFSET_NONE;
      resample->out_offset0 = GST_BUFFER_OFFSET_NONE;
    }
    /* set DISCONT flag on output buffer */
    GST_DEBUG_OBJECT (resample, "marking this buffer with the DISCONT flag");
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
    resample->need_discont = FALSE;
  }

  ret = gst_audio_resample_process (resample, inbuf, outbuf);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    return ret;

  GST_DEBUG_OBJECT (resample, "input = samples [%" G_GUINT64_FORMAT ", %"
      G_GUINT64_FORMAT ") = [%" G_GUINT64_FORMAT ", %" G_GUINT64_FORMAT
      ") ns;  output = samples [%" G_GUINT64_FORMAT ", %" G_GUINT64_FORMAT
      ") = [%" G_GUINT64_FORMAT ", %" G_GUINT64_FORMAT ") ns",
      GST_BUFFER_OFFSET (inbuf), GST_BUFFER_OFFSET_END (inbuf),
      GST_BUFFER_TIMESTAMP (inbuf), GST_BUFFER_TIMESTAMP (inbuf) +
      GST_BUFFER_DURATION (inbuf), GST_BUFFER_OFFSET (outbuf),
      GST_BUFFER_OFFSET_END (outbuf), GST_BUFFER_TIMESTAMP (outbuf),
      GST_BUFFER_TIMESTAMP (outbuf) + GST_BUFFER_DURATION (outbuf));

  return GST_FLOW_OK;
}

static gboolean
gst_audio_resample_query (GstPad * pad, GstQuery * query)
{
  GstAudioResample *resample = GST_AUDIO_RESAMPLE (gst_pad_get_parent (pad));
  GstBaseTransform *trans;
  gboolean res = TRUE;
  if (G_UNLIKELY (resample == NULL))
    return FALSE;

  trans = GST_BASE_TRANSFORM (resample);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GstClockTime min, max;
      gboolean live;
      guint64 latency;
      GstPad *peer;
      gint rate = resample->inrate;
      gint resampler_latency;

      if (resample->state)
        resampler_latency =
            resample->funcs->get_input_latency (resample->state);
      else
        resampler_latency = 0;

      if (gst_base_transform_is_passthrough (trans))
        resampler_latency = 0;

      if ((peer = gst_pad_get_peer (GST_BASE_TRANSFORM_SINK_PAD (trans)))) {
        if ((res = gst_pad_query (peer, query))) {
          gst_query_parse_latency (query, &live, &min, &max);

          GST_DEBUG_OBJECT (resample, "Peer latency: min %"
              GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
              GST_TIME_ARGS (min), GST_TIME_ARGS (max));

          /* add our own latency */
          if (rate != 0 && resampler_latency != 0)
            latency = gst_util_uint64_scale_round (resampler_latency,
                GST_SECOND, rate);
          else
            latency = 0;

          GST_DEBUG_OBJECT (resample, "Our latency: %" GST_TIME_FORMAT,
              GST_TIME_ARGS (latency));

          min += latency;
          if (GST_CLOCK_TIME_IS_VALID (max))
            max += latency;

          GST_DEBUG_OBJECT (resample, "Calculated total latency : min %"
              GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
              GST_TIME_ARGS (min), GST_TIME_ARGS (max));

          gst_query_set_latency (query, live, min, max);
        }
        gst_object_unref (peer);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
  gst_object_unref (resample);
  return res;
}

static const GstQueryType *
gst_audio_resample_query_type (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_LATENCY,
    0
  };

  return types;
}

static void
gst_audio_resample_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAudioResample *resample;
  gint quality;

  resample = GST_AUDIO_RESAMPLE (object);

  switch (prop_id) {
    case PROP_QUALITY:
      GST_BASE_TRANSFORM_LOCK (resample);
      quality = g_value_get_int (value);
      GST_DEBUG_OBJECT (resample, "new quality %d", quality);

      gst_audio_resample_update_state (resample, resample->width,
          resample->channels, resample->inrate, resample->outrate,
          quality, resample->fp);
      GST_BASE_TRANSFORM_UNLOCK (resample);
      break;
    case PROP_FILTER_LENGTH:{
      gint filter_length = g_value_get_int (value);

      GST_BASE_TRANSFORM_LOCK (resample);
      if (filter_length <= 8)
        quality = 0;
      else if (filter_length <= 16)
        quality = 1;
      else if (filter_length <= 32)
        quality = 2;
      else if (filter_length <= 48)
        quality = 3;
      else if (filter_length <= 64)
        quality = 4;
      else if (filter_length <= 80)
        quality = 5;
      else if (filter_length <= 96)
        quality = 6;
      else if (filter_length <= 128)
        quality = 7;
      else if (filter_length <= 160)
        quality = 8;
      else if (filter_length <= 192)
        quality = 9;
      else
        quality = 10;

      GST_DEBUG_OBJECT (resample, "new quality %d", quality);

      gst_audio_resample_update_state (resample, resample->width,
          resample->channels, resample->inrate, resample->outrate,
          quality, resample->fp);
      GST_BASE_TRANSFORM_UNLOCK (resample);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audio_resample_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAudioResample *resample;

  resample = GST_AUDIO_RESAMPLE (object);

  switch (prop_id) {
    case PROP_QUALITY:
      g_value_set_int (value, resample->quality);
      break;
    case PROP_FILTER_LENGTH:
      switch (resample->quality) {
        case 0:
          g_value_set_int (value, 8);
          break;
        case 1:
          g_value_set_int (value, 16);
          break;
        case 2:
          g_value_set_int (value, 32);
          break;
        case 3:
          g_value_set_int (value, 48);
          break;
        case 4:
          g_value_set_int (value, 64);
          break;
        case 5:
          g_value_set_int (value, 80);
          break;
        case 6:
          g_value_set_int (value, 96);
          break;
        case 7:
          g_value_set_int (value, 128);
          break;
        case 8:
          g_value_set_int (value, 160);
          break;
        case 9:
          g_value_set_int (value, 192);
          break;
        case 10:
          g_value_set_int (value, 256);
          break;
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* FIXME: should have a benchmark fallback for the case where orc is disabled */
#if defined(AUDIORESAMPLE_FORMAT_AUTO) && !defined(DISABLE_ORC)

#define BENCHMARK_SIZE 512

static gboolean
_benchmark_int_float (SpeexResamplerState * st)
{
  gint16 in[BENCHMARK_SIZE] = { 0, }, G_GNUC_UNUSED out[BENCHMARK_SIZE / 2];
  gfloat in_tmp[BENCHMARK_SIZE], out_tmp[BENCHMARK_SIZE / 2];
  gint i;
  guint32 inlen = BENCHMARK_SIZE, outlen = BENCHMARK_SIZE / 2;

  for (i = 0; i < BENCHMARK_SIZE; i++) {
    gfloat tmp = in[i];
    in_tmp[i] = tmp / G_MAXINT16;
  }

  resample_float_resampler_process_interleaved_float (st,
      (const guint8 *) in_tmp, &inlen, (guint8 *) out_tmp, &outlen);

  if (outlen == 0) {
    GST_ERROR ("Failed to use float resampler");
    return FALSE;
  }

  for (i = 0; i < outlen; i++) {
    gfloat tmp = out_tmp[i];
    out[i] = CLAMP (tmp * G_MAXINT16 + 0.5, G_MININT16, G_MAXINT16);
  }

  return TRUE;
}

static gboolean
_benchmark_int_int (SpeexResamplerState * st)
{
  gint16 in[BENCHMARK_SIZE] = { 0, }, out[BENCHMARK_SIZE / 2];
  guint32 inlen = BENCHMARK_SIZE, outlen = BENCHMARK_SIZE / 2;

  resample_int_resampler_process_interleaved_int (st, (const guint8 *) in,
      &inlen, (guint8 *) out, &outlen);

  if (outlen == 0) {
    GST_ERROR ("Failed to use int resampler");
    return FALSE;
  }

  return TRUE;
}

static gboolean
_benchmark_integer_resampling (void)
{
  OrcProfile a, b;
  gdouble av, bv;
  SpeexResamplerState *sta, *stb;
  int i;

  orc_profile_init (&a);
  orc_profile_init (&b);

  sta = resample_float_resampler_init (1, 48000, 24000, 4, NULL);
  if (sta == NULL) {
    GST_ERROR ("Failed to create float resampler state");
    return FALSE;
  }

  stb = resample_int_resampler_init (1, 48000, 24000, 4, NULL);
  if (stb == NULL) {
    resample_float_resampler_destroy (sta);
    GST_ERROR ("Failed to create int resampler state");
    return FALSE;
  }

  /* Benchmark */
  for (i = 0; i < 10; i++) {
    orc_profile_start (&a);
    if (!_benchmark_int_float (sta))
      goto error;
    orc_profile_stop (&a);
  }

  /* Benchmark */
  for (i = 0; i < 10; i++) {
    orc_profile_start (&b);
    if (!_benchmark_int_int (stb))
      goto error;
    orc_profile_stop (&b);
  }

  /* Handle results */
  orc_profile_get_ave_std (&a, &av, NULL);
  orc_profile_get_ave_std (&b, &bv, NULL);

  /* Remember benchmark result in global variable */
  gst_audio_resample_use_int = (av > bv);
  resample_float_resampler_destroy (sta);
  resample_int_resampler_destroy (stb);

  if (av > bv)
    GST_INFO ("Using integer resampler if appropriate: %lf < %lf", bv, av);
  else
    GST_INFO ("Using float resampler for everything: %lf <= %lf", av, bv);

  return TRUE;

error:
  resample_float_resampler_destroy (sta);
  resample_int_resampler_destroy (stb);

  return FALSE;
}
#endif /* defined(AUDIORESAMPLE_FORMAT_AUTO) && !defined(DISABLE_ORC) */

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (audio_resample_debug, "audioresample", 0,
      "audio resampling element");

#if defined(AUDIORESAMPLE_FORMAT_AUTO) && !defined(DISABLE_ORC)
  if (!_benchmark_integer_resampling ())
    return FALSE;
#else
  GST_WARNING ("Orc disabled, can't benchmark int vs. float resampler");
  {
    GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);
    GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");
    GST_CAT_WARNING (GST_CAT_PERFORMANCE, "orc disabled, no benchmarking done");
  }
#endif

  if (!gst_element_register (plugin, "audioresample", GST_RANK_PRIMARY,
          GST_TYPE_AUDIO_RESAMPLE)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "audioresample",
    "Resamples audio", plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN);
