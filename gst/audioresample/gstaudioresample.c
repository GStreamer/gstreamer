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
#include <gst/audio/audio.h>
#include <gst/base/gstbasetransform.h>

#if defined AUDIORESAMPLE_FORMAT_AUTO
#define OIL_ENABLE_UNSTABLE_API
#include <liboil/liboilprofile.h>
#include <liboil/liboil.h>
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

/* If TRUE integer arithmetic resampling is faster and will be used if appropiate */
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

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_audio_resample_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_audio_resample_sink_template));

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
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /* FIXME 0.11: Remove this property, it's just for compatibility
   * with old audioresample
   */
  g_object_class_install_property (gobject_class, PROP_FILTER_LENGTH,
      g_param_spec_int ("filter-length", "Filter length",
          "DEPRECATED, DON'T USE THIS! " "Length of the resample filter", 0,
          G_MAXINT, 64, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

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

  resample->need_discont = FALSE;

  gst_pad_set_query_function (trans->srcpad, gst_audio_resample_query);
  gst_pad_set_query_type_function (trans->srcpad,
      gst_audio_resample_query_type);
}

/* vmethods */
static gboolean
gst_audio_resample_start (GstBaseTransform * base)
{
  GstAudioResample *resample = GST_AUDIO_RESAMPLE (base);

  resample->next_offset = -1;
  resample->next_ts = -1;
  resample->next_upstream_ts = -1;

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
  GstCaps *res;
  GstStructure *structure;

  /* transform caps gives one single caps so we can just replace
   * the rate property with our range. */
  res = gst_caps_copy (caps);
  structure = gst_caps_get_structure (res, 0);
  gst_structure_set (structure, "rate", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

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

  if (g_str_equal (gst_structure_get_name (structure), "audio/x-raw-float"))
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
  GstAudioResample *resample = GST_AUDIO_RESAMPLE (base);
  GstCaps *srccaps, *sinkcaps;
  gboolean ret = TRUE;
  guint32 ratio_den, ratio_num;
  gint inrate, outrate, gcd;
  gint width;

  GST_LOG_OBJECT (resample, "asked to transform size %d in direction %s",
      size, direction == GST_PAD_SINK ? "SINK" : "SRC");
  if (direction == GST_PAD_SINK) {
    sinkcaps = caps;
    srccaps = othercaps;
  } else {
    sinkcaps = othercaps;
    srccaps = caps;
  }

  ret =
      gst_audio_resample_parse_caps (caps, othercaps, &width, NULL, &inrate,
      &outrate, NULL);
  if (G_UNLIKELY (!ret)) {
    GST_ERROR_OBJECT (resample, "Wrong caps");
    return FALSE;
  }

  gcd = _gcd (inrate, outrate);
  ratio_num = inrate / gcd;
  ratio_den = outrate / gcd;

  if (direction == GST_PAD_SINK) {
    gint fac = width / 8;

    /* asked to convert size of an incoming buffer */
    size /= fac;
    *othersize = (size * ratio_den + ratio_num - 1) / ratio_num;
    *othersize *= fac;
    size *= fac;
  } else {
    gint fac = width / 8;

    /* asked to convert size of an outgoing buffer */
    size /= fac;
    *othersize = (size * ratio_num + ratio_den - 1) / ratio_den;
    *othersize *= fac;
    size *= fac;
  }

  GST_LOG_OBJECT (resample, "transformed size %d to %d", size, *othersize);

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

static void
gst_audio_resample_push_drain (GstAudioResample * resample)
{
  GstBuffer *buf;
  GstBaseTransform *trans = GST_BASE_TRANSFORM (resample);
  GstFlowReturn res;
  gint outsize;
  guint out_len, out_processed;
  gint err;
  guint num, den, len;
  guint8 *outtmp = NULL;
  gboolean need_convert = FALSE;

  if (!resample->state)
    return;

  need_convert = (resample->funcs->width != resample->width);

  resample->funcs->get_ratio (resample->state, &num, &den);

  out_len = resample->funcs->get_input_latency (resample->state);
  out_len = out_processed = (out_len * den + num - 1) / num;
  outsize = (resample->width / 8) * out_len * resample->channels;

  if (need_convert) {
    guint outsize_tmp =
        (resample->funcs->width / 8) * out_len * resample->channels;
    if (outsize_tmp <= resample->tmp_out_size) {
      outtmp = resample->tmp_out;
    } else {
      resample->tmp_out_size = outsize_tmp;
      resample->tmp_out = outtmp = g_realloc (resample->tmp_out, outsize_tmp);
    }
  }

  res =
      gst_pad_alloc_buffer_and_set_caps (trans->srcpad, GST_BUFFER_OFFSET_NONE,
      outsize, GST_PAD_CAPS (trans->srcpad), &buf);

  if (G_UNLIKELY (res != GST_FLOW_OK)) {
    GST_WARNING_OBJECT (resample, "failed allocating buffer of %d bytes",
        outsize);
    return;
  }

  len = resample->funcs->get_input_latency (resample->state);

  err =
      resample->funcs->process (resample->state,
      NULL, &len, (need_convert) ? outtmp : GST_BUFFER_DATA (buf),
      &out_processed);

  if (G_UNLIKELY (err != RESAMPLER_ERR_SUCCESS)) {
    GST_WARNING_OBJECT (resample, "Failed to process drain: %s",
        resample->funcs->strerror (err));
    gst_buffer_unref (buf);
    return;
  }

  if (G_UNLIKELY (out_processed == 0)) {
    GST_WARNING_OBJECT (resample, "Failed to get drain, dropping buffer");
    gst_buffer_unref (buf);
    return;
  }

  /* If we wrote more than allocated something is really wrong now
   * and we should better abort immediately */
  g_assert (out_len >= out_processed);

  if (need_convert)
    gst_audio_resample_convert_buffer (resample, outtmp, GST_BUFFER_DATA (buf),
        out_processed, TRUE);

  GST_BUFFER_DURATION (buf) =
      GST_FRAMES_TO_CLOCK_TIME (out_processed, resample->outrate);
  GST_BUFFER_SIZE (buf) =
      out_processed * resample->channels * (resample->width / 8);

  if (GST_CLOCK_TIME_IS_VALID (resample->next_ts)) {
    GST_BUFFER_OFFSET (buf) = resample->next_offset;
    GST_BUFFER_OFFSET_END (buf) = resample->next_offset + out_processed;
    GST_BUFFER_TIMESTAMP (buf) = resample->next_ts;

    resample->next_ts += GST_BUFFER_DURATION (buf);
    resample->next_offset += out_processed;
  }

  GST_LOG_OBJECT (resample,
      "Pushing drain buffer of %u bytes with timestamp %" GST_TIME_FORMAT
      " duration %" GST_TIME_FORMAT " offset %" G_GUINT64_FORMAT " offset_end %"
      G_GUINT64_FORMAT, GST_BUFFER_SIZE (buf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)), GST_BUFFER_OFFSET (buf),
      GST_BUFFER_OFFSET_END (buf));

  res = gst_pad_push (trans->srcpad, buf);

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
    case GST_EVENT_FLUSH_START:
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_audio_resample_reset_state (resample);
      resample->next_offset = -1;
      resample->next_ts = -1;
      resample->next_upstream_ts = -1;
    case GST_EVENT_NEWSEGMENT:
      gst_audio_resample_push_drain (resample);
      gst_audio_resample_reset_state (resample);
      resample->next_offset = -1;
      resample->next_ts = -1;
      resample->next_upstream_ts = -1;
      break;
    case GST_EVENT_EOS:{
      gst_audio_resample_push_drain (resample);
      gst_audio_resample_reset_state (resample);
      break;
    }
    default:
      break;
  }

  return parent_class->event (base, event);
}

static gboolean
gst_audio_resample_check_discont (GstAudioResample * resample,
    GstClockTime timestamp)
{
  if (timestamp != GST_CLOCK_TIME_NONE &&
      resample->next_upstream_ts != GST_CLOCK_TIME_NONE &&
      timestamp != resample->next_upstream_ts) {
    /* Potentially a discontinuous buffer. However, it turns out that many
     * elements generate imperfect streams due to rounding errors, so we permit
     * a small error (up to one sample) without triggering a filter 
     * flush/restart (if triggered incorrectly, this will be audible) */
    GstClockTimeDiff diff = timestamp - resample->next_upstream_ts;

    if (ABS (diff) > (GST_SECOND + resample->inrate - 1) / resample->inrate) {
      GST_WARNING_OBJECT (resample,
          "encountered timestamp discontinuity of %s%" GST_TIME_FORMAT,
          (diff < 0) ? "-" : "", GST_TIME_ARGS ((GstClockTime) ABS (diff)));
      return TRUE;
    }
  }

  return FALSE;
}

static GstFlowReturn
gst_audio_resample_process (GstAudioResample * resample, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  guint32 in_len, in_processed;
  guint32 out_len, out_processed;
  gint err = RESAMPLER_ERR_SUCCESS;
  guint8 *in_tmp = NULL, *out_tmp = NULL;
  gboolean need_convert = (resample->funcs->width != resample->width);

  in_len = GST_BUFFER_SIZE (inbuf) / resample->channels;
  out_len = GST_BUFFER_SIZE (outbuf) / resample->channels;

  in_len /= (resample->width / 8);
  out_len /= (resample->width / 8);

  in_processed = in_len;
  out_processed = out_len;

  if (need_convert) {
    guint in_size_tmp =
        in_len * resample->channels * (resample->funcs->width / 8);
    guint out_size_tmp =
        out_len * resample->channels * (resample->funcs->width / 8);

    if (in_size_tmp <= resample->tmp_in_size) {
      in_tmp = resample->tmp_in;
    } else {
      resample->tmp_in = in_tmp = g_realloc (resample->tmp_in, in_size_tmp);
      resample->tmp_in_size = in_size_tmp;
    }

    gst_audio_resample_convert_buffer (resample, GST_BUFFER_DATA (inbuf),
        in_tmp, in_len, FALSE);

    if (out_size_tmp <= resample->tmp_out_size) {
      out_tmp = resample->tmp_out;
    } else {
      resample->tmp_out = out_tmp = g_realloc (resample->tmp_out, out_size_tmp);
      resample->tmp_out_size = out_size_tmp;
    }
  }

  if (need_convert) {
    err = resample->funcs->process (resample->state,
        in_tmp, &in_processed, out_tmp, &out_processed);
  } else {
    err = resample->funcs->process (resample->state,
        (const guint8 *) GST_BUFFER_DATA (inbuf), &in_processed,
        (guint8 *) GST_BUFFER_DATA (outbuf), &out_processed);
  }

  if (G_UNLIKELY (in_len != in_processed))
    GST_WARNING_OBJECT (resample, "Converted %d of %d input samples",
        in_processed, in_len);

  if (out_len != out_processed) {
    if (out_processed == 0) {
      GST_DEBUG_OBJECT (resample, "Converted to 0 samples, buffer dropped");

      return GST_BASE_TRANSFORM_FLOW_DROPPED;
    }

    /* If we wrote more than allocated something is really wrong now
     * and we should better abort immediately */
    g_assert (out_len >= out_processed);
  }

  if (G_UNLIKELY (err != RESAMPLER_ERR_SUCCESS)) {
    GST_ERROR_OBJECT (resample, "Failed to convert data: %s",
        resample->funcs->strerror (err));
    return GST_FLOW_ERROR;
  } else {

    if (need_convert)
      gst_audio_resample_convert_buffer (resample, out_tmp,
          GST_BUFFER_DATA (outbuf), out_processed, TRUE);

    GST_BUFFER_DURATION (outbuf) =
        GST_FRAMES_TO_CLOCK_TIME (out_processed, resample->outrate);
    GST_BUFFER_SIZE (outbuf) =
        out_processed * resample->channels * (resample->width / 8);

    if (GST_CLOCK_TIME_IS_VALID (resample->next_ts)) {
      GST_BUFFER_TIMESTAMP (outbuf) = resample->next_ts;
      GST_BUFFER_OFFSET (outbuf) = resample->next_offset;
      GST_BUFFER_OFFSET_END (outbuf) = resample->next_offset + out_processed;

      resample->next_ts += GST_BUFFER_DURATION (outbuf);
      resample->next_offset += out_processed;
    }

    GST_LOG_OBJECT (resample,
        "Converted to buffer of %u bytes with timestamp %" GST_TIME_FORMAT
        ", duration %" GST_TIME_FORMAT ", offset %" G_GUINT64_FORMAT
        ", offset_end %" G_GUINT64_FORMAT, GST_BUFFER_SIZE (outbuf),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)),
        GST_BUFFER_OFFSET (outbuf), GST_BUFFER_OFFSET_END (outbuf));

    return GST_FLOW_OK;
  }
}

static GstFlowReturn
gst_audio_resample_transform (GstBaseTransform * base, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstAudioResample *resample = GST_AUDIO_RESAMPLE (base);
  guint8 *data;
  gulong size;
  GstClockTime timestamp;
  guint outsamples, insamples;
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

  data = GST_BUFFER_DATA (inbuf);
  size = GST_BUFFER_SIZE (inbuf);
  timestamp = GST_BUFFER_TIMESTAMP (inbuf);

  GST_LOG_OBJECT (resample, "transforming buffer of %ld bytes, ts %"
      GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT ", offset %"
      G_GINT64_FORMAT ", offset_end %" G_GINT64_FORMAT,
      size, GST_TIME_ARGS (timestamp),
      GST_TIME_ARGS (GST_BUFFER_DURATION (inbuf)),
      GST_BUFFER_OFFSET (inbuf), GST_BUFFER_OFFSET_END (inbuf));

  /* check for timestamp discontinuities and flush/reset if needed */
  if (G_UNLIKELY (gst_audio_resample_check_discont (resample, timestamp)
          || GST_BUFFER_IS_DISCONT (inbuf))) {
    /* Flush internal samples */
    gst_audio_resample_reset_state (resample);
    /* Inform downstream element about discontinuity */
    resample->need_discont = TRUE;
    /* We want to recalculate the timestamps */
    resample->next_ts = -1;
    resample->next_upstream_ts = -1;
    resample->next_offset = -1;
  }

  insamples = GST_BUFFER_SIZE (inbuf) / resample->channels;
  insamples /= (resample->width / 8);

  outsamples = GST_BUFFER_SIZE (outbuf) / resample->channels;
  outsamples /= (resample->width / 8);

  if (GST_CLOCK_TIME_IS_VALID (timestamp)
      && !GST_CLOCK_TIME_IS_VALID (resample->next_ts)) {
    resample->next_ts = timestamp;
    resample->next_offset =
        GST_CLOCK_TIME_TO_FRAMES (timestamp, resample->outrate);
  }

  if (G_UNLIKELY (resample->need_discont)) {
    GST_DEBUG_OBJECT (resample, "marking this buffer with the DISCONT flag");
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
    resample->need_discont = FALSE;
  }

  ret = gst_audio_resample_process (resample, inbuf, outbuf);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    return ret;

  if (GST_CLOCK_TIME_IS_VALID (timestamp)
      && !GST_CLOCK_TIME_IS_VALID (resample->next_upstream_ts))
    resample->next_upstream_ts = timestamp;

  if (GST_CLOCK_TIME_IS_VALID (resample->next_upstream_ts))
    resample->next_upstream_ts +=
        GST_FRAMES_TO_CLOCK_TIME (insamples, resample->inrate);

  return GST_FLOW_OK;
}

static gboolean
gst_audio_resample_query (GstPad * pad, GstQuery * query)
{
  GstAudioResample *resample = GST_AUDIO_RESAMPLE (gst_pad_get_parent (pad));
  GstBaseTransform *trans = GST_BASE_TRANSFORM (resample);
  gboolean res = TRUE;

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

      if ((peer = gst_pad_get_peer (trans->sinkpad))) {
        if ((res = gst_pad_query (peer, query))) {
          gst_query_parse_latency (query, &live, &min, &max);

          GST_DEBUG_OBJECT (resample, "Peer latency: min %"
              GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
              GST_TIME_ARGS (min), GST_TIME_ARGS (max));

          /* add our own latency */
          if (rate != 0 && resampler_latency != 0)
            latency =
                gst_util_uint64_scale (resampler_latency, GST_SECOND, rate);
          else
            latency = 0;

          GST_DEBUG_OBJECT (resample, "Our latency: %" GST_TIME_FORMAT,
              GST_TIME_ARGS (latency));

          min += latency;
          if (max != GST_CLOCK_TIME_NONE)
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

  resample = GST_AUDIO_RESAMPLE (object);

  switch (prop_id) {
    case PROP_QUALITY:
      resample->quality = g_value_get_int (value);
      GST_DEBUG_OBJECT (resample, "new quality %d", resample->quality);

      gst_audio_resample_update_state (resample, resample->width,
          resample->channels, resample->inrate, resample->outrate,
          resample->quality, resample->fp);
      break;
    case PROP_FILTER_LENGTH:{
      gint filter_length = g_value_get_int (value);

      if (filter_length <= 8)
        resample->quality = 0;
      else if (filter_length <= 16)
        resample->quality = 1;
      else if (filter_length <= 32)
        resample->quality = 2;
      else if (filter_length <= 48)
        resample->quality = 3;
      else if (filter_length <= 64)
        resample->quality = 4;
      else if (filter_length <= 80)
        resample->quality = 5;
      else if (filter_length <= 96)
        resample->quality = 6;
      else if (filter_length <= 128)
        resample->quality = 7;
      else if (filter_length <= 160)
        resample->quality = 8;
      else if (filter_length <= 192)
        resample->quality = 9;
      else
        resample->quality = 10;

      GST_DEBUG_OBJECT (resample, "new quality %d", resample->quality);

      gst_audio_resample_update_state (resample, resample->width,
          resample->channels, resample->inrate, resample->outrate,
          resample->quality, resample->fp);
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

#if defined AUDIORESAMPLE_FORMAT_AUTO
#define BENCHMARK_SIZE 512

static gboolean
_benchmark_int_float (SpeexResamplerState * st)
{
  gint16 in[BENCHMARK_SIZE] = { 0, }, out[BENCHMARK_SIZE / 2];
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
  OilProfile a, b;
  gdouble av, bv;
  SpeexResamplerState *sta, *stb;

  oil_profile_init (&a);
  oil_profile_init (&b);

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

  /* Warm up cache */
  if (!_benchmark_int_float (sta))
    goto error;
  if (!_benchmark_int_float (sta))
    goto error;

  /* Benchmark */
  oil_profile_start (&a);
  if (!_benchmark_int_float (sta))
    goto error;
  oil_profile_stop (&a);

  /* Warm up cache */
  if (!_benchmark_int_int (stb))
    goto error;
  if (!_benchmark_int_int (stb))
    goto error;

  /* Benchmark */
  oil_profile_start (&b);
  if (!_benchmark_int_int (stb))
    goto error;
  oil_profile_stop (&b);

  /* Handle results */
  oil_profile_get_ave_std (&a, &av, NULL);
  oil_profile_get_ave_std (&b, &bv, NULL);

  /* Remember benchmark result in global variable */
  gst_audio_resample_use_int = (av > bv);
  resample_float_resampler_destroy (sta);
  resample_int_resampler_destroy (stb);

  if (av > bv)
    GST_INFO ("Using integer resampler if appropiate: %lf < %lf", bv, av);
  else
    GST_INFO ("Using float resampler for everything: %lf <= %lf", av, bv);

  return TRUE;

error:
  resample_float_resampler_destroy (sta);
  resample_int_resampler_destroy (stb);

  return FALSE;
}
#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (audio_resample_debug, "audioresample", 0,
      "audio resampling element");
#if defined AUDIORESAMPLE_FORMAT_AUTO
  oil_init ();

  if (!_benchmark_integer_resampling ())
    return FALSE;
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
