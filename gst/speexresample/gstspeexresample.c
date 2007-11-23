/* GStreamer
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2003,2004 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2007 Sebastian Dröge <slomo@circular-chaos.org>
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
 * SECTION:element-speexresample
 *
 * <refsect2>
 * speexresample resamples raw audio buffers to different sample rates using
 * a configurable windowing function to enhance quality.
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch -v filesrc location=sine.ogg ! oggdemux ! vorbisdec ! audioconvert ! speexresample ! audio/x-raw-int, rate=8000 ! alsasink
 * </programlisting>
 * Decode an Ogg/Vorbis downsample to 8Khz and play sound through alsa. 
 * To create the Ogg/Vorbis file refer to the documentation of vorbisenc.
 * </para>
 * </refsect2>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <math.h>

#include "gstspeexresample.h"
#include <gst/audio/audio.h>
#include <gst/base/gstbasetransform.h>

GST_DEBUG_CATEGORY (speex_resample_debug);
#define GST_CAT_DEFAULT speex_resample_debug

enum
{
  PROP_0,
  PROP_QUALITY
};

#define SUPPORTED_CAPS \
GST_STATIC_CAPS ( \
    "audio/x-raw-float, " \
      "rate = (int) [ 1, MAX ], "	\
      "channels = (int) [ 1, MAX ], " \
      "endianness = (int) BYTE_ORDER, " \
      "width = (int) 32; " \
    "audio/x-raw-int, " \
      "rate = (int) [ 1, MAX ], " \
      "channels = (int) [ 1, MAX ], " \
      "endianness = (int) BYTE_ORDER, " \
      "width = (int) 16, " \
      "depth = (int) 16, " \
      "signed = (boolean) true" \
)

static GstStaticPadTemplate gst_speex_resample_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK, GST_PAD_ALWAYS, SUPPORTED_CAPS);

static GstStaticPadTemplate gst_speex_resample_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC, GST_PAD_ALWAYS, SUPPORTED_CAPS);

static void gst_speex_resample_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_speex_resample_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

/* vmethods */
static gboolean gst_speex_resample_get_unit_size (GstBaseTransform * base,
    GstCaps * caps, guint * size);
static GstCaps *gst_speex_resample_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps);
static gboolean gst_speex_resample_transform_size (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * incaps, guint insize,
    GstCaps * outcaps, guint * outsize);
static gboolean gst_speex_resample_set_caps (GstBaseTransform * base,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_speex_resample_transform (GstBaseTransform * base,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_speex_resample_event (GstBaseTransform * base,
    GstEvent * event);
static gboolean gst_speex_resample_start (GstBaseTransform * base);
static gboolean gst_speex_resample_stop (GstBaseTransform * base);
static gboolean gst_speex_resample_query (GstPad * pad, GstQuery * query);
static const GstQueryType *gst_speex_resample_query_type (GstPad * pad);

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (speex_resample_debug, "speex_resample", 0, "audio resampling element");

GST_BOILERPLATE_FULL (GstSpeexResample, gst_speex_resample, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void
gst_speex_resample_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_speex_resample_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_speex_resample_sink_template));

  gst_element_class_set_details_simple (gstelement_class, "Audio resampler",
      "Filter/Converter/Audio", "Resamples audio",
      "Sebastian Dröge <slomo@circular-chaos.org>");
}

static void
gst_speex_resample_class_init (GstSpeexResampleClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_speex_resample_set_property;
  gobject_class->get_property = gst_speex_resample_get_property;

  g_object_class_install_property (gobject_class, PROP_QUALITY,
      g_param_spec_int ("quality", "Quality", "Resample quality with 0 being "
          "the lowest and 10 being the best",
          SPEEX_RESAMPLER_QUALITY_MIN, SPEEX_RESAMPLER_QUALITY_MAX,
          SPEEX_RESAMPLER_QUALITY_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  GST_BASE_TRANSFORM_CLASS (klass)->start =
      GST_DEBUG_FUNCPTR (gst_speex_resample_start);
  GST_BASE_TRANSFORM_CLASS (klass)->stop =
      GST_DEBUG_FUNCPTR (gst_speex_resample_stop);
  GST_BASE_TRANSFORM_CLASS (klass)->transform_size =
      GST_DEBUG_FUNCPTR (gst_speex_resample_transform_size);
  GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_speex_resample_get_unit_size);
  GST_BASE_TRANSFORM_CLASS (klass)->transform_caps =
      GST_DEBUG_FUNCPTR (gst_speex_resample_transform_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->set_caps =
      GST_DEBUG_FUNCPTR (gst_speex_resample_set_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->transform =
      GST_DEBUG_FUNCPTR (gst_speex_resample_transform);
  GST_BASE_TRANSFORM_CLASS (klass)->event =
      GST_DEBUG_FUNCPTR (gst_speex_resample_event);

  GST_BASE_TRANSFORM_CLASS (klass)->passthrough_on_same_caps = TRUE;
}

static void
gst_speex_resample_init (GstSpeexResample * resample,
    GstSpeexResampleClass * klass)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM (resample);

  resample->quality = SPEEX_RESAMPLER_QUALITY_DEFAULT;

  resample->need_discont = FALSE;

  gst_pad_set_query_function (trans->srcpad, gst_speex_resample_query);
  gst_pad_set_query_type_function (trans->srcpad,
      gst_speex_resample_query_type);
}

/* vmethods */
static gboolean
gst_speex_resample_start (GstBaseTransform * base)
{
  GstSpeexResample *resample = GST_SPEEX_RESAMPLE (base);

  resample->ts_offset = -1;
  resample->offset = -1;
  resample->next_ts = -1;

  return TRUE;
}

static gboolean
gst_speex_resample_stop (GstBaseTransform * base)
{
  GstSpeexResample *resample = GST_SPEEX_RESAMPLE (base);

  if (resample->state) {
    resample_resampler_destroy (resample->state);
    resample->state = NULL;
  }

  gst_caps_replace (&resample->sinkcaps, NULL);
  gst_caps_replace (&resample->srccaps, NULL);

  return TRUE;
}

static gboolean
gst_speex_resample_get_unit_size (GstBaseTransform * base, GstCaps * caps,
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
  g_return_val_if_fail (ret, FALSE);

  *size = width * channels / 8;

  return TRUE;
}

static GstCaps *
gst_speex_resample_transform_caps (GstBaseTransform * base,
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

static SpeexResamplerState *
gst_speex_resample_init_state (guint channels, guint inrate, guint outrate,
    guint quality, gboolean fp)
{
  SpeexResamplerState *ret = NULL;
  gint err = RESAMPLER_ERR_SUCCESS;

  if (fp)
    ret =
        resample_float_resampler_init (channels, inrate, outrate, quality,
        &err);
  else
    ret =
        resample_int_resampler_init (channels, inrate, outrate, quality, &err);

  if (err != RESAMPLER_ERR_SUCCESS) {
    GST_ERROR ("Failed to create resampler state: %s",
        resample_resampler_strerror (err));
    return NULL;
  }

  if (fp)
    resample_float_resampler_skip_zeros (ret);
  else
    resample_int_resampler_skip_zeros (ret);

  return ret;
}

static gboolean
gst_speex_resample_update_state (GstSpeexResample * resample, gint channels,
    gint inrate, gint outrate, gint quality, gboolean fp)
{
  gboolean ret = TRUE;
  gboolean updated_latency = FALSE;

  updated_latency = (resample->inrate != inrate
      || quality != resample->quality);

  if (resample->state == NULL) {
    ret = TRUE;
  } else if (resample->channels != channels || fp != resample->fp) {
    resample_resampler_destroy (resample->state);
    resample->state =
        gst_speex_resample_init_state (channels, inrate, outrate, quality, fp);

    ret = (resample->state != NULL);
  } else if (resample->inrate != inrate || resample->outrate != outrate) {
    gint err = RESAMPLER_ERR_SUCCESS;

    if (fp)
      err =
          resample_float_resampler_set_rate (resample->state, inrate, outrate);
    else
      err = resample_int_resampler_set_rate (resample->state, inrate, outrate);

    if (err != RESAMPLER_ERR_SUCCESS)
      GST_ERROR ("Failed to update rate: %s",
          resample_resampler_strerror (err));

    ret = (err == RESAMPLER_ERR_SUCCESS);
  } else if (quality != resample->quality) {
    gint err = RESAMPLER_ERR_SUCCESS;

    if (fp)
      err = resample_float_resampler_set_quality (resample->state, quality);
    else
      err = resample_int_resampler_set_quality (resample->state, quality);

    if (err != RESAMPLER_ERR_SUCCESS)
      GST_ERROR ("Failed to update quality: %s",
          resample_resampler_strerror (err));

    ret = (err == RESAMPLER_ERR_SUCCESS);
  }

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
gst_speex_resample_reset_state (GstSpeexResample * resample)
{
  if (resample->state && resample->fp)
    resample_float_resampler_reset_mem (resample->state);
  else if (resample->state && !resample->fp)
    resample_int_resampler_reset_mem (resample->state);
}

static gboolean
gst_speex_resample_parse_caps (GstCaps * incaps,
    GstCaps * outcaps, gint * channels, gint * inrate, gint * outrate,
    gboolean * fp)
{
  GstStructure *structure;
  gboolean ret;
  gint myinrate, myoutrate, mychannels;
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
  if (!ret)
    goto no_in_rate_channels;

  structure = gst_caps_get_structure (outcaps, 0);
  ret = gst_structure_get_int (structure, "rate", &myoutrate);
  if (!ret)
    goto no_out_rate;

  if (channels)
    *channels = mychannels;
  if (inrate)
    *inrate = myinrate;
  if (outrate)
    *outrate = myoutrate;

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

static gboolean
gst_speex_resample_transform_size (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, guint size, GstCaps * othercaps,
    guint * othersize)
{
  GstSpeexResample *resample = GST_SPEEX_RESAMPLE (base);
  SpeexResamplerState *state;
  GstCaps *srccaps, *sinkcaps;
  gboolean use_internal = FALSE;        /* whether we use the internal state */
  gboolean ret = TRUE;
  guint32 ratio_den, ratio_num;
  gboolean fp;

  GST_LOG ("asked to transform size %d in direction %s",
      size, direction == GST_PAD_SINK ? "SINK" : "SRC");
  if (direction == GST_PAD_SINK) {
    sinkcaps = caps;
    srccaps = othercaps;
  } else {
    sinkcaps = othercaps;
    srccaps = caps;
  }

  /* if the caps are the ones that _set_caps got called with; we can use
   * our own state; otherwise we'll have to create a state */
  if (resample->state && gst_caps_is_equal (sinkcaps, resample->sinkcaps) &&
      gst_caps_is_equal (srccaps, resample->srccaps)) {
    use_internal = TRUE;
    state = resample->state;
    fp = resample->fp;
  } else {
    gint inrate, outrate, channels;

    GST_DEBUG ("Can't use internal state, creating state");

    ret =
        gst_speex_resample_parse_caps (caps, othercaps, &channels, &inrate,
        &outrate, &fp);

    if (!ret) {
      GST_ERROR ("Wrong caps");
      return FALSE;
    }

    state = gst_speex_resample_init_state (channels, inrate, outrate, 0, TRUE);
    if (!state)
      return FALSE;
  }

  if (resample->fp || use_internal)
    resample_float_resampler_get_ratio (state, &ratio_num, &ratio_den);
  else
    resample_int_resampler_get_ratio (state, &ratio_num, &ratio_den);

  if (direction == GST_PAD_SINK) {
    gint fac = (fp) ? 4 : 2;

    /* asked to convert size of an incoming buffer */
    size /= fac;
    *othersize = (size * ratio_den + (ratio_num >> 1)) / ratio_num;
    *othersize *= fac;
    size *= fac;
  } else {
    gint fac = (fp) ? 4 : 2;

    /* asked to convert size of an outgoing buffer */
    size /= fac;
    *othersize = (size * ratio_num + (ratio_den >> 1)) / ratio_den;
    *othersize *= fac;
    size *= fac;
  }

  GST_LOG ("transformed size %d to %d", size, *othersize);

  if (!use_internal)
    resample_resampler_destroy (state);

  return ret;
}

static gboolean
gst_speex_resample_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  gboolean ret;
  gint inrate = 0, outrate = 0, channels = 0;
  gboolean fp = FALSE;
  GstSpeexResample *resample = GST_SPEEX_RESAMPLE (base);

  GST_LOG ("incaps %" GST_PTR_FORMAT ", outcaps %"
      GST_PTR_FORMAT, incaps, outcaps);

  ret = gst_speex_resample_parse_caps (incaps, outcaps,
      &channels, &inrate, &outrate, &fp);

  g_return_val_if_fail (ret, FALSE);

  ret =
      gst_speex_resample_update_state (resample, channels, inrate, outrate,
      resample->quality, fp);

  g_return_val_if_fail (ret, FALSE);

  /* save caps so we can short-circuit in the size_transform if the caps
   * are the same */
  gst_caps_replace (&resample->sinkcaps, incaps);
  gst_caps_replace (&resample->srccaps, outcaps);

  return TRUE;
}

static void
gst_speex_resample_push_drain (GstSpeexResample * resample)
{
  GstBuffer *buf;
  GstBaseTransform *trans = GST_BASE_TRANSFORM (resample);
  GstFlowReturn res;
  gint outsize;
  guint out_len, out_processed;
  gint err;

  if (!resample->state)
    return;

  if (resample->fp) {
    guint num, den;

    resample_float_resampler_get_ratio (resample->state, &num, &den);

    out_len = resample_float_resampler_get_latency (resample->state);
    out_len = out_processed = (out_len * den + (num >> 1)) / num;
    outsize = 4 * out_len * resample->channels;
  } else {
    guint num, den;

    resample_int_resampler_get_ratio (resample->state, &num, &den);

    out_len = resample_int_resampler_get_latency (resample->state);
    out_len = out_processed = (out_len * den + (num >> 1)) / num;
    outsize = 2 * out_len * resample->channels;
  }

  res = gst_pad_alloc_buffer (trans->srcpad, GST_BUFFER_OFFSET_NONE, outsize,
      GST_PAD_CAPS (trans->srcpad), &buf);

  if (G_UNLIKELY (res != GST_FLOW_OK)) {
    GST_WARNING ("failed allocating buffer of %d bytes", outsize);
    return;
  }

  if (resample->fp)
    err = resample_float_resampler_drain_interleaved_float (resample->state,
        (gfloat *) GST_BUFFER_DATA (buf), &out_processed);
  else
    err = resample_int_resampler_drain_interleaved_int (resample->state,
        (gint16 *) GST_BUFFER_DATA (buf), &out_processed);

  if (err != RESAMPLER_ERR_SUCCESS) {
    GST_WARNING ("Failed to process drain: %s",
        resample_resampler_strerror (err));
    gst_buffer_unref (buf);
    return;
  }

  if (out_processed == 0) {
    GST_WARNING ("Failed to get drain, dropping buffer");
    gst_buffer_unref (buf);
    return;
  }

  GST_BUFFER_OFFSET (buf) = resample->offset;
  GST_BUFFER_TIMESTAMP (buf) = resample->next_ts;
  GST_BUFFER_SIZE (buf) =
      out_processed * resample->channels * ((resample->fp) ? 4 : 2);

  if (resample->ts_offset != -1) {
    resample->offset += out_processed;
    resample->ts_offset += out_processed;
    resample->next_ts =
        GST_FRAMES_TO_CLOCK_TIME (resample->ts_offset, resample->outrate);
    GST_BUFFER_OFFSET_END (buf) = resample->offset;

    /* we calculate DURATION as the difference between "next" timestamp
     * and current timestamp so we ensure a contiguous stream, instead of
     * having rounding errors. */
    GST_BUFFER_DURATION (buf) = resample->next_ts - GST_BUFFER_TIMESTAMP (buf);
  } else {
    /* no valid offset know, we can still sortof calculate the duration though */
    GST_BUFFER_DURATION (buf) =
        GST_FRAMES_TO_CLOCK_TIME (out_processed, resample->outrate);
  }

  GST_LOG ("Pushing drain buffer of %ld bytes with timestamp %" GST_TIME_FORMAT
      " duration %" GST_TIME_FORMAT " offset %lld offset_end %lld",
      GST_BUFFER_SIZE (buf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)),
      GST_BUFFER_OFFSET (buf), GST_BUFFER_OFFSET_END (buf));

  res = gst_pad_push (trans->srcpad, buf);

  if (res != GST_FLOW_OK)
    GST_WARNING ("Failed to push drain");

  return;
}

static gboolean
gst_speex_resample_event (GstBaseTransform * base, GstEvent * event)
{
  GstSpeexResample *resample = GST_SPEEX_RESAMPLE (base);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_speex_resample_reset_state (resample);
      resample->ts_offset = -1;
      resample->next_ts = -1;
      resample->offset = -1;
    case GST_EVENT_NEWSEGMENT:
      gst_speex_resample_push_drain (resample);
      gst_speex_resample_reset_state (resample);
      resample->ts_offset = -1;
      resample->next_ts = -1;
      resample->offset = -1;
      break;
    case GST_EVENT_EOS:{
      gst_speex_resample_push_drain (resample);
      gst_speex_resample_reset_state (resample);
      break;
    }
    default:
      break;
  }
  parent_class->event (base, event);

  return TRUE;
}

static gboolean
gst_speex_resample_check_discont (GstSpeexResample * resample,
    GstClockTime timestamp)
{
  if (timestamp != GST_CLOCK_TIME_NONE &&
      resample->prev_ts != GST_CLOCK_TIME_NONE &&
      resample->prev_duration != GST_CLOCK_TIME_NONE &&
      timestamp != resample->prev_ts + resample->prev_duration) {
    /* Potentially a discontinuous buffer. However, it turns out that many
     * elements generate imperfect streams due to rounding errors, so we permit
     * a small error (up to one sample) without triggering a filter 
     * flush/restart (if triggered incorrectly, this will be audible) */
    GstClockTimeDiff diff = timestamp -
        (resample->prev_ts + resample->prev_duration);

    if (ABS (diff) > GST_SECOND / resample->inrate) {
      GST_WARNING ("encountered timestamp discontinuity of %" G_GINT64_FORMAT,
          diff);
      return TRUE;
    }
  }

  return FALSE;
}

static void
gst_speex_fix_output_buffer (GstSpeexResample * resample, GstBuffer * outbuf,
    guint diff)
{
  GstClockTime timediff = GST_FRAMES_TO_CLOCK_TIME (diff, resample->outrate);

  GST_LOG ("Adjusting buffer by %d samples", diff);

  GST_BUFFER_DURATION (outbuf) -= timediff;
  GST_BUFFER_SIZE (outbuf) -=
      diff * ((resample->fp) ? 4 : 2) * resample->channels;

  if (resample->ts_offset != -1) {
    GST_BUFFER_OFFSET_END (outbuf) -= diff;
    resample->offset -= diff;
    resample->ts_offset -= diff;
    resample->next_ts =
        GST_FRAMES_TO_CLOCK_TIME (resample->ts_offset, resample->outrate);
  }
}

static GstFlowReturn
gst_speex_resample_process (GstSpeexResample * resample, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  guint32 in_len, in_processed;
  guint32 out_len, out_processed;
  gint err = RESAMPLER_ERR_SUCCESS;

  in_len = GST_BUFFER_SIZE (inbuf) / resample->channels;
  out_len = GST_BUFFER_SIZE (outbuf) / resample->channels;

  if (resample->fp) {
    in_len /= 4;
    out_len /= 4;
  } else {
    in_len /= 2;
    out_len /= 2;
  }

  in_processed = in_len;
  out_processed = out_len;

  if (resample->fp)
    err = resample_float_resampler_process_interleaved_float (resample->state,
        (const gfloat *) GST_BUFFER_DATA (inbuf), &in_processed,
        (gfloat *) GST_BUFFER_DATA (outbuf), &out_processed);
  else
    err = resample_int_resampler_process_interleaved_int (resample->state,
        (const gint16 *) GST_BUFFER_DATA (inbuf), &in_processed,
        (gint16 *) GST_BUFFER_DATA (outbuf), &out_processed);

  if (in_len != in_processed)
    GST_WARNING ("Converted %d of %d input samples", in_processed, in_len);

  if (out_len != out_processed) {
    /* One sample difference is allowed as this will happen
     * because of rounding errors */
    if (out_processed == 0) {
      GST_DEBUG ("Converted to 0 samples, buffer dropped");

      if (resample->ts_offset != -1) {
        GST_BUFFER_OFFSET_END (outbuf) -= out_len;
        resample->offset -= out_len;
        resample->ts_offset -= out_len;
        resample->next_ts =
            GST_FRAMES_TO_CLOCK_TIME (resample->ts_offset, resample->outrate);
      }

      return GST_BASE_TRANSFORM_FLOW_DROPPED;
    } else if (out_len - out_processed != 1)
      GST_WARNING ("Converted to %d instead of %d output samples",
          out_processed, out_len);
    if (out_len > out_processed) {
      gst_speex_fix_output_buffer (resample, outbuf, out_len - out_processed);
    } else {
      GST_ERROR ("Wrote more output than allocated!");
      return GST_FLOW_ERROR;
    }
  }

  if (err != RESAMPLER_ERR_SUCCESS) {
    GST_ERROR ("Failed to convert data: %s", resample_resampler_strerror (err));
    return GST_FLOW_ERROR;
  } else {
    GST_LOG ("Converted to buffer of %ld bytes with timestamp %" GST_TIME_FORMAT
        ", duration %" GST_TIME_FORMAT ", offset %lld, offset_end %lld",
        GST_BUFFER_SIZE (outbuf),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)),
        GST_BUFFER_OFFSET (outbuf), GST_BUFFER_OFFSET_END (outbuf));
    return GST_FLOW_OK;
  }
}

static GstFlowReturn
gst_speex_resample_transform (GstBaseTransform * base, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstSpeexResample *resample = GST_SPEEX_RESAMPLE (base);
  guint8 *data;
  gulong size;
  GstClockTime timestamp;
  gint outsamples;

  if (resample->state == NULL)
    if (!(resample->state = gst_speex_resample_init_state (resample->channels,
                resample->inrate, resample->outrate, resample->quality,
                resample->fp)))
      return GST_FLOW_ERROR;

  data = GST_BUFFER_DATA (inbuf);
  size = GST_BUFFER_SIZE (inbuf);
  timestamp = GST_BUFFER_TIMESTAMP (inbuf);

  GST_LOG ("transforming buffer of %ld bytes, ts %"
      GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT ", offset %"
      G_GINT64_FORMAT ", offset_end %" G_GINT64_FORMAT,
      size, GST_TIME_ARGS (timestamp),
      GST_TIME_ARGS (GST_BUFFER_DURATION (inbuf)),
      GST_BUFFER_OFFSET (inbuf), GST_BUFFER_OFFSET_END (inbuf));

  /* check for timestamp discontinuities and flush/reset if needed */
  if (G_UNLIKELY (gst_speex_resample_check_discont (resample, timestamp)
          || GST_BUFFER_IS_DISCONT (inbuf))) {
    /* Flush internal samples */
    gst_speex_resample_reset_state (resample);
    /* Inform downstream element about discontinuity */
    resample->need_discont = TRUE;
    /* We want to recalculate the offset */
    resample->ts_offset = -1;
  }

  outsamples = GST_BUFFER_SIZE (outbuf) / resample->channels;
  outsamples /= (resample->fp) ? 4 : 2;

  if (resample->ts_offset == -1) {
    /* if we don't know the initial offset yet, calculate it based on the 
     * input timestamp. */
    if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
      GstClockTime stime;

      /* offset used to calculate the timestamps. We use the sample offset for
       * this to make it more accurate. We want the first buffer to have the
       * same timestamp as the incoming timestamp. */
      resample->next_ts = timestamp;
      resample->ts_offset =
          GST_CLOCK_TIME_TO_FRAMES (timestamp, resample->outrate);
      /* offset used to set as the buffer offset, this offset is always
       * relative to the stream time, note that timestamp is not... */
      stime = (timestamp - base->segment.start) + base->segment.time;
      resample->offset = GST_CLOCK_TIME_TO_FRAMES (stime, resample->outrate);
    }
  }
  resample->prev_ts = timestamp;
  resample->prev_duration = GST_BUFFER_DURATION (inbuf);

  GST_BUFFER_OFFSET (outbuf) = resample->offset;
  GST_BUFFER_TIMESTAMP (outbuf) = resample->next_ts;

  if (resample->ts_offset != -1) {
    resample->offset += outsamples;
    resample->ts_offset += outsamples;
    resample->next_ts =
        GST_FRAMES_TO_CLOCK_TIME (resample->ts_offset, resample->outrate);
    GST_BUFFER_OFFSET_END (outbuf) = resample->offset;

    /* we calculate DURATION as the difference between "next" timestamp
     * and current timestamp so we ensure a contiguous stream, instead of
     * having rounding errors. */
    GST_BUFFER_DURATION (outbuf) = resample->next_ts -
        GST_BUFFER_TIMESTAMP (outbuf);
  } else {
    /* no valid offset know, we can still sortof calculate the duration though */
    GST_BUFFER_DURATION (outbuf) =
        GST_FRAMES_TO_CLOCK_TIME (outsamples, resample->outrate);
  }

  if (G_UNLIKELY (resample->need_discont)) {
    GST_DEBUG ("marking this buffer with the DISCONT flag");
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
    resample->need_discont = FALSE;
  }

  return gst_speex_resample_process (resample, inbuf, outbuf);
}

static gboolean
gst_speex_resample_query (GstPad * pad, GstQuery * query)
{
  GstSpeexResample *resample = GST_SPEEX_RESAMPLE (gst_pad_get_parent (pad));
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

      if (resample->state && resample->fp)
        resampler_latency =
            resample_float_resampler_get_latency (resample->state);
      else if (resample->state && !resample->fp)
        resampler_latency =
            resample_int_resampler_get_latency (resample->state);
      else
        resampler_latency = 0;

      if (gst_base_transform_is_passthrough (trans))
        resampler_latency = 0;

      if ((peer = gst_pad_get_peer (trans->sinkpad))) {
        if ((res = gst_pad_query (peer, query))) {
          gst_query_parse_latency (query, &live, &min, &max);

          GST_DEBUG ("Peer latency: min %"
              GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
              GST_TIME_ARGS (min), GST_TIME_ARGS (max));

          /* add our own latency */
          if (rate != 0 && resampler_latency != 0)
            latency =
                gst_util_uint64_scale (resampler_latency, GST_SECOND, rate);
          else
            latency = 0;

          GST_DEBUG ("Our latency: %" GST_TIME_FORMAT, GST_TIME_ARGS (latency));

          min += latency;
          if (max != GST_CLOCK_TIME_NONE)
            max += latency;

          GST_DEBUG ("Calculated total latency : min %"
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
gst_speex_resample_query_type (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_LATENCY,
    0
  };

  return types;
}

static void
gst_speex_resample_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSpeexResample *resample;

  resample = GST_SPEEX_RESAMPLE (object);

  switch (prop_id) {
    case PROP_QUALITY:
      resample->quality = g_value_get_int (value);
      GST_DEBUG ("new quality %d", resample->quality);

      gst_speex_resample_update_state (resample, resample->channels,
          resample->inrate, resample->outrate, resample->quality, resample->fp);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_speex_resample_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSpeexResample *resample;

  resample = GST_SPEEX_RESAMPLE (object);

  switch (prop_id) {
    case PROP_QUALITY:
      g_value_set_int (value, resample->quality);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "speexresample", GST_RANK_NONE,
          GST_TYPE_SPEEX_RESAMPLE)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "speexresample",
    "Resamples audio", plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN);
