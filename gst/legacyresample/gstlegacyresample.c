/* GStreamer
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2003,2004 David A. Schleef <ds@schleef.org>
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
/* Element-Checklist-Version: 5 */

/**
 * SECTION:element-legacyresample
 *
 * legacyresample resamples raw audio buffers to different sample rates using
 * a configurable windowing function to enhance quality.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v filesrc location=sine.ogg ! oggdemux ! vorbisdec ! audioconvert ! legacyresample ! audio/x-raw-int, rate=8000 ! alsasink
 * ]| Decode an Ogg/Vorbis downsample to 8Khz and play sound through alsa. 
 * To create the Ogg/Vorbis file refer to the documentation of vorbisenc.
 * </refsect2>
 *
 * Last reviewed on 2006-03-02 (0.10.4)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <math.h>

/*#define DEBUG_ENABLED */
#include "gstlegacyresample.h"
#include <gst/audio/audio.h>
#include <gst/base/gstbasetransform.h>

GST_DEBUG_CATEGORY_STATIC (legacyresample_debug);
#define GST_CAT_DEFAULT legacyresample_debug

#define DEFAULT_FILTERLEN       16

enum
{
  PROP_0,
  PROP_FILTERLEN
};

#define SUPPORTED_CAPS \
GST_STATIC_CAPS ( \
    "audio/x-raw-int, " \
      "rate = (int) [ 1, MAX ], " \
      "channels = (int) [ 1, MAX ], " \
      "endianness = (int) BYTE_ORDER, " \
      "width = (int) 16, " \
      "depth = (int) 16, " \
      "signed = (boolean) true;" \
    "audio/x-raw-int, " \
      "rate = (int) [ 1, MAX ], " \
      "channels = (int) [ 1, MAX ], " \
      "endianness = (int) BYTE_ORDER, " \
      "width = (int) 32, " \
      "depth = (int) 32, " \
      "signed = (boolean) true;" \
    "audio/x-raw-float, " \
      "rate = (int) [ 1, MAX ], "	\
      "channels = (int) [ 1, MAX ], " \
      "endianness = (int) BYTE_ORDER, " \
      "width = (int) 32; " \
    "audio/x-raw-float, " \
      "rate = (int) [ 1, MAX ], "	\
      "channels = (int) [ 1, MAX ], " \
      "endianness = (int) BYTE_ORDER, " \
      "width = (int) 64" \
)

static GstStaticPadTemplate gst_legacyresample_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK, GST_PAD_ALWAYS, SUPPORTED_CAPS);

static GstStaticPadTemplate gst_legacyresample_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC, GST_PAD_ALWAYS, SUPPORTED_CAPS);

static void gst_legacyresample_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_legacyresample_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

/* vmethods */
static gboolean legacyresample_get_unit_size (GstBaseTransform * base,
    GstCaps * caps, guint * size);
static GstCaps *legacyresample_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps);
static void legacyresample_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean legacyresample_transform_size (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * incaps, guint insize,
    GstCaps * outcaps, guint * outsize);
static gboolean legacyresample_set_caps (GstBaseTransform * base,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn legacyresample_pushthrough (GstLegacyresample *
    legacyresample);
static GstFlowReturn legacyresample_transform (GstBaseTransform * base,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean legacyresample_event (GstBaseTransform * base,
    GstEvent * event);
static gboolean legacyresample_start (GstBaseTransform * base);
static gboolean legacyresample_stop (GstBaseTransform * base);

static gboolean legacyresample_query (GstPad * pad, GstQuery * query);
static const GstQueryType *legacyresample_query_type (GstPad * pad);

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (legacyresample_debug, "legacyresample", 0, "audio resampling element");

GST_BOILERPLATE_FULL (GstLegacyresample, gst_legacyresample, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void
gst_legacyresample_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_legacyresample_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_legacyresample_sink_template);

  gst_element_class_set_details_simple (gstelement_class, "Audio scaler",
      "Filter/Converter/Audio",
      "Resample audio", "David Schleef <ds@schleef.org>");
}

static void
gst_legacyresample_class_init (GstLegacyresampleClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_legacyresample_set_property;
  gobject_class->get_property = gst_legacyresample_get_property;

  g_object_class_install_property (gobject_class, PROP_FILTERLEN,
      g_param_spec_int ("filter-length", "filter length",
          "Length of the resample filter", 0, G_MAXINT, DEFAULT_FILTERLEN,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  GST_BASE_TRANSFORM_CLASS (klass)->start =
      GST_DEBUG_FUNCPTR (legacyresample_start);
  GST_BASE_TRANSFORM_CLASS (klass)->stop =
      GST_DEBUG_FUNCPTR (legacyresample_stop);
  GST_BASE_TRANSFORM_CLASS (klass)->transform_size =
      GST_DEBUG_FUNCPTR (legacyresample_transform_size);
  GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size =
      GST_DEBUG_FUNCPTR (legacyresample_get_unit_size);
  GST_BASE_TRANSFORM_CLASS (klass)->transform_caps =
      GST_DEBUG_FUNCPTR (legacyresample_transform_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->fixate_caps =
      GST_DEBUG_FUNCPTR (legacyresample_fixate_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->set_caps =
      GST_DEBUG_FUNCPTR (legacyresample_set_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->transform =
      GST_DEBUG_FUNCPTR (legacyresample_transform);
  GST_BASE_TRANSFORM_CLASS (klass)->event =
      GST_DEBUG_FUNCPTR (legacyresample_event);

  GST_BASE_TRANSFORM_CLASS (klass)->passthrough_on_same_caps = TRUE;
}

static void
gst_legacyresample_init (GstLegacyresample * legacyresample,
    GstLegacyresampleClass * klass)
{
  GstBaseTransform *trans;

  trans = GST_BASE_TRANSFORM (legacyresample);

  /* buffer alloc passthrough is too impossible. FIXME, it
   * is trivial in the passthrough case. */
  gst_pad_set_bufferalloc_function (trans->sinkpad, NULL);

  legacyresample->filter_length = DEFAULT_FILTERLEN;

  legacyresample->need_discont = FALSE;

  gst_pad_set_query_function (trans->srcpad, legacyresample_query);
  gst_pad_set_query_type_function (trans->srcpad, legacyresample_query_type);
}

/* vmethods */
static gboolean
legacyresample_start (GstBaseTransform * base)
{
  GstLegacyresample *legacyresample = GST_LEGACYRESAMPLE (base);

  legacyresample->resample = resample_new ();
  legacyresample->ts_offset = -1;
  legacyresample->offset = -1;
  legacyresample->next_ts = -1;

  resample_set_filter_length (legacyresample->resample,
      legacyresample->filter_length);

  return TRUE;
}

static gboolean
legacyresample_stop (GstBaseTransform * base)
{
  GstLegacyresample *legacyresample = GST_LEGACYRESAMPLE (base);

  if (legacyresample->resample) {
    resample_free (legacyresample->resample);
    legacyresample->resample = NULL;
  }

  gst_caps_replace (&legacyresample->sinkcaps, NULL);
  gst_caps_replace (&legacyresample->srccaps, NULL);

  return TRUE;
}

static gboolean
legacyresample_get_unit_size (GstBaseTransform * base, GstCaps * caps,
    guint * size)
{
  gint width, channels;
  GstStructure *structure;
  gboolean ret;

  g_assert (size);

  /* this works for both float and int */
  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width", &width);
  ret &= gst_structure_get_int (structure, "channels", &channels);
  g_return_val_if_fail (ret, FALSE);

  *size = width * channels / 8;

  return TRUE;
}

static GstCaps *
legacyresample_transform_caps (GstBaseTransform * base,
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
legacyresample_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstStructure *s;
  gint rate;

  s = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (s, "rate", &rate))
    return;

  s = gst_caps_get_structure (othercaps, 0);
  gst_structure_fixate_field_nearest_int (s, "rate", rate);
}

static gboolean
resample_set_state_from_caps (ResampleState * state, GstCaps * incaps,
    GstCaps * outcaps, gint * channels, gint * inrate, gint * outrate)
{
  GstStructure *structure;
  gboolean ret;
  gint myinrate, myoutrate;
  int mychannels;
  gint width, depth;
  ResampleFormat format;

  GST_DEBUG ("incaps %" GST_PTR_FORMAT ", outcaps %"
      GST_PTR_FORMAT, incaps, outcaps);

  structure = gst_caps_get_structure (incaps, 0);

  /* get width */
  ret = gst_structure_get_int (structure, "width", &width);
  if (!ret)
    goto no_width;

  /* figure out the format */
  if (g_str_equal (gst_structure_get_name (structure), "audio/x-raw-float")) {
    if (width == 32)
      format = RESAMPLE_FORMAT_F32;
    else if (width == 64)
      format = RESAMPLE_FORMAT_F64;
    else
      goto wrong_depth;
  } else {
    /* for int, depth and width must be the same */
    ret = gst_structure_get_int (structure, "depth", &depth);
    if (!ret || width != depth)
      goto not_equal;

    if (width == 16)
      format = RESAMPLE_FORMAT_S16;
    else if (width == 32)
      format = RESAMPLE_FORMAT_S32;
    else
      goto wrong_depth;
  }
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

  resample_set_format (state, format);
  resample_set_n_channels (state, mychannels);
  resample_set_input_rate (state, myinrate);
  resample_set_output_rate (state, myoutrate);

  return TRUE;

  /* ERRORS */
no_width:
  {
    GST_DEBUG ("failed to get width from caps");
    return FALSE;
  }
not_equal:
  {
    GST_DEBUG ("width %d and depth %d must be the same", width, depth);
    return FALSE;
  }
wrong_depth:
  {
    GST_DEBUG ("unknown depth %d found", depth);
    return FALSE;
  }
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
legacyresample_transform_size (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, guint size, GstCaps * othercaps,
    guint * othersize)
{
  GstLegacyresample *legacyresample = GST_LEGACYRESAMPLE (base);
  ResampleState *state;
  GstCaps *srccaps, *sinkcaps;
  gboolean use_internal = FALSE;        /* whether we use the internal state */
  gboolean ret = TRUE;

  GST_LOG_OBJECT (base, "asked to transform size %d in direction %s",
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
  if (gst_caps_is_equal (sinkcaps, legacyresample->sinkcaps) &&
      gst_caps_is_equal (srccaps, legacyresample->srccaps)) {
    use_internal = TRUE;
    state = legacyresample->resample;
  } else {
    GST_DEBUG_OBJECT (legacyresample,
        "caps are not the set caps, creating state");
    state = resample_new ();
    resample_set_filter_length (state, legacyresample->filter_length);
    resample_set_state_from_caps (state, sinkcaps, srccaps, NULL, NULL, NULL);
  }

  if (direction == GST_PAD_SINK) {
    /* asked to convert size of an incoming buffer */
    *othersize = resample_get_output_size_for_input (state, size);
  } else {
    /* asked to convert size of an outgoing buffer */
    *othersize = resample_get_input_size_for_output (state, size);
  }
  g_assert (*othersize % state->sample_size == 0);

  /* we make room for one extra sample, given that the resampling filter
   * can output an extra one for non-integral i_rate/o_rate */
  GST_LOG_OBJECT (base, "transformed size %d to %d", size, *othersize);

  if (!use_internal) {
    resample_free (state);
  }

  return ret;
}

static gboolean
legacyresample_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  gboolean ret;
  gint inrate, outrate;
  int channels;
  GstLegacyresample *legacyresample = GST_LEGACYRESAMPLE (base);

  GST_DEBUG_OBJECT (base, "incaps %" GST_PTR_FORMAT ", outcaps %"
      GST_PTR_FORMAT, incaps, outcaps);

  ret = resample_set_state_from_caps (legacyresample->resample, incaps, outcaps,
      &channels, &inrate, &outrate);

  g_return_val_if_fail (ret, FALSE);

  legacyresample->channels = channels;
  GST_DEBUG_OBJECT (legacyresample, "set channels to %d", channels);
  legacyresample->i_rate = inrate;
  GST_DEBUG_OBJECT (legacyresample, "set i_rate to %d", inrate);
  legacyresample->o_rate = outrate;
  GST_DEBUG_OBJECT (legacyresample, "set o_rate to %d", outrate);

  /* save caps so we can short-circuit in the size_transform if the caps
   * are the same */
  gst_caps_replace (&legacyresample->sinkcaps, incaps);
  gst_caps_replace (&legacyresample->srccaps, outcaps);

  return TRUE;
}

static gboolean
legacyresample_event (GstBaseTransform * base, GstEvent * event)
{
  GstLegacyresample *legacyresample;

  legacyresample = GST_LEGACYRESAMPLE (base);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      break;
    case GST_EVENT_FLUSH_STOP:
      if (legacyresample->resample)
        resample_input_flush (legacyresample->resample);
      legacyresample->ts_offset = -1;
      legacyresample->next_ts = -1;
      legacyresample->offset = -1;
      break;
    case GST_EVENT_NEWSEGMENT:
      resample_input_pushthrough (legacyresample->resample);
      legacyresample_pushthrough (legacyresample);
      legacyresample->ts_offset = -1;
      legacyresample->next_ts = -1;
      legacyresample->offset = -1;
      break;
    case GST_EVENT_EOS:
      resample_input_eos (legacyresample->resample);
      legacyresample_pushthrough (legacyresample);
      break;
    default:
      break;
  }
  return parent_class->event (base, event);
}

static GstFlowReturn
legacyresample_do_output (GstLegacyresample * legacyresample,
    GstBuffer * outbuf)
{
  int outsize;
  int outsamples;
  ResampleState *r;

  r = legacyresample->resample;

  outsize = resample_get_output_size (r);
  GST_LOG_OBJECT (legacyresample, "legacyresample can give me %d bytes",
      outsize);

  /* protect against mem corruption */
  if (outsize > GST_BUFFER_SIZE (outbuf)) {
    GST_WARNING_OBJECT (legacyresample,
        "overriding legacyresample's outsize %d with outbuffer's size %d",
        outsize, GST_BUFFER_SIZE (outbuf));
    outsize = GST_BUFFER_SIZE (outbuf);
  }
  /* catch possibly wrong size differences */
  if (GST_BUFFER_SIZE (outbuf) - outsize > r->sample_size) {
    GST_WARNING_OBJECT (legacyresample,
        "legacyresample's outsize %d too far from outbuffer's size %d",
        outsize, GST_BUFFER_SIZE (outbuf));
  }

  outsize = resample_get_output_data (r, GST_BUFFER_DATA (outbuf), outsize);
  outsamples = outsize / r->sample_size;
  GST_LOG_OBJECT (legacyresample, "resample gave me %d bytes or %d samples",
      outsize, outsamples);

  GST_BUFFER_OFFSET (outbuf) = legacyresample->offset;
  GST_BUFFER_TIMESTAMP (outbuf) = legacyresample->next_ts;

  if (legacyresample->ts_offset != -1) {
    legacyresample->offset += outsamples;
    legacyresample->ts_offset += outsamples;
    legacyresample->next_ts =
        gst_util_uint64_scale_int (legacyresample->ts_offset, GST_SECOND,
        legacyresample->o_rate);
    GST_BUFFER_OFFSET_END (outbuf) = legacyresample->offset;

    /* we calculate DURATION as the difference between "next" timestamp
     * and current timestamp so we ensure a contiguous stream, instead of
     * having rounding errors. */
    GST_BUFFER_DURATION (outbuf) = legacyresample->next_ts -
        GST_BUFFER_TIMESTAMP (outbuf);
  } else {
    /* no valid offset know, we can still sortof calculate the duration though */
    GST_BUFFER_DURATION (outbuf) =
        gst_util_uint64_scale_int (outsamples, GST_SECOND,
        legacyresample->o_rate);
  }

  /* check for possible mem corruption */
  if (outsize > GST_BUFFER_SIZE (outbuf)) {
    /* this is an error that when it happens, would need fixing in the
     * resample library; we told it we wanted only GST_BUFFER_SIZE (outbuf),
     * and it gave us more ! */
    GST_WARNING_OBJECT (legacyresample,
        "legacyresample, you memory corrupting bastard. "
        "you gave me outsize %d while my buffer was size %d",
        outsize, GST_BUFFER_SIZE (outbuf));
    return GST_FLOW_ERROR;
  }
  /* catch possibly wrong size differences */
  if (GST_BUFFER_SIZE (outbuf) - outsize > r->sample_size) {
    GST_WARNING_OBJECT (legacyresample,
        "legacyresample's written outsize %d too far from outbuffer's size %d",
        outsize, GST_BUFFER_SIZE (outbuf));
  }
  GST_BUFFER_SIZE (outbuf) = outsize;

  if (G_UNLIKELY (legacyresample->need_discont)) {
    GST_DEBUG_OBJECT (legacyresample,
        "marking this buffer with the DISCONT flag");
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
    legacyresample->need_discont = FALSE;
  }

  GST_LOG_OBJECT (legacyresample, "transformed to buffer of %d bytes, ts %"
      GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT ", offset %"
      G_GINT64_FORMAT ", offset_end %" G_GINT64_FORMAT,
      outsize, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)),
      GST_BUFFER_OFFSET (outbuf), GST_BUFFER_OFFSET_END (outbuf));


  return GST_FLOW_OK;
}

static gboolean
legacyresample_check_discont (GstLegacyresample * legacyresample,
    GstClockTime timestamp)
{
  if (timestamp != GST_CLOCK_TIME_NONE &&
      legacyresample->prev_ts != GST_CLOCK_TIME_NONE &&
      legacyresample->prev_duration != GST_CLOCK_TIME_NONE &&
      timestamp != legacyresample->prev_ts + legacyresample->prev_duration) {
    /* Potentially a discontinuous buffer. However, it turns out that many
     * elements generate imperfect streams due to rounding errors, so we permit
     * a small error (up to one sample) without triggering a filter 
     * flush/restart (if triggered incorrectly, this will be audible) */
    GstClockTimeDiff diff = timestamp -
        (legacyresample->prev_ts + legacyresample->prev_duration);

    if (ABS (diff) > GST_SECOND / legacyresample->i_rate) {
      GST_WARNING_OBJECT (legacyresample,
          "encountered timestamp discontinuity of %" G_GINT64_FORMAT, diff);
      return TRUE;
    }
  }

  return FALSE;
}

static GstFlowReturn
legacyresample_transform (GstBaseTransform * base, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstLegacyresample *legacyresample;
  ResampleState *r;
  guchar *data, *datacopy;
  gulong size;
  GstClockTime timestamp;

  legacyresample = GST_LEGACYRESAMPLE (base);
  r = legacyresample->resample;

  data = GST_BUFFER_DATA (inbuf);
  size = GST_BUFFER_SIZE (inbuf);
  timestamp = GST_BUFFER_TIMESTAMP (inbuf);

  GST_LOG_OBJECT (legacyresample, "transforming buffer of %ld bytes, ts %"
      GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT ", offset %"
      G_GINT64_FORMAT ", offset_end %" G_GINT64_FORMAT,
      size, GST_TIME_ARGS (timestamp),
      GST_TIME_ARGS (GST_BUFFER_DURATION (inbuf)),
      GST_BUFFER_OFFSET (inbuf), GST_BUFFER_OFFSET_END (inbuf));

  /* check for timestamp discontinuities and flush/reset if needed */
  if (G_UNLIKELY (legacyresample_check_discont (legacyresample, timestamp))) {
    /* Flush internal samples */
    legacyresample_pushthrough (legacyresample);
    /* Inform downstream element about discontinuity */
    legacyresample->need_discont = TRUE;
    /* We want to recalculate the offset */
    legacyresample->ts_offset = -1;
  }

  if (legacyresample->ts_offset == -1) {
    /* if we don't know the initial offset yet, calculate it based on the 
     * input timestamp. */
    if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
      GstClockTime stime;

      /* offset used to calculate the timestamps. We use the sample offset for
       * this to make it more accurate. We want the first buffer to have the
       * same timestamp as the incoming timestamp. */
      legacyresample->next_ts = timestamp;
      legacyresample->ts_offset =
          gst_util_uint64_scale_int (timestamp, r->o_rate, GST_SECOND);
      /* offset used to set as the buffer offset, this offset is always
       * relative to the stream time, note that timestamp is not... */
      stime = (timestamp - base->segment.start) + base->segment.time;
      legacyresample->offset =
          gst_util_uint64_scale_int (stime, r->o_rate, GST_SECOND);
    }
  }
  legacyresample->prev_ts = timestamp;
  legacyresample->prev_duration = GST_BUFFER_DURATION (inbuf);

  /* need to memdup, resample takes ownership. */
  datacopy = g_memdup (data, size);
  resample_add_input_data (r, datacopy, size, g_free, datacopy);

  return legacyresample_do_output (legacyresample, outbuf);
}

/* push remaining data in the buffers out */
static GstFlowReturn
legacyresample_pushthrough (GstLegacyresample * legacyresample)
{
  int outsize;
  ResampleState *r;
  GstBuffer *outbuf;
  GstFlowReturn res = GST_FLOW_OK;
  GstBaseTransform *trans;

  r = legacyresample->resample;

  outsize = resample_get_output_size (r);
  if (outsize == 0) {
    GST_DEBUG_OBJECT (legacyresample, "no internal buffers needing flush");
    goto done;
  }

  trans = GST_BASE_TRANSFORM (legacyresample);

  res = gst_pad_alloc_buffer (trans->srcpad, GST_BUFFER_OFFSET_NONE, outsize,
      GST_PAD_CAPS (trans->srcpad), &outbuf);
  if (G_UNLIKELY (res != GST_FLOW_OK)) {
    GST_WARNING_OBJECT (legacyresample, "failed allocating buffer of %d bytes",
        outsize);
    goto done;
  }

  res = legacyresample_do_output (legacyresample, outbuf);
  if (G_UNLIKELY (res != GST_FLOW_OK))
    goto done;

  res = gst_pad_push (trans->srcpad, outbuf);

done:
  return res;
}

static gboolean
legacyresample_query (GstPad * pad, GstQuery * query)
{
  GstLegacyresample *legacyresample =
      GST_LEGACYRESAMPLE (gst_pad_get_parent (pad));
  GstBaseTransform *trans = GST_BASE_TRANSFORM (legacyresample);
  gboolean res = TRUE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GstClockTime min, max;
      gboolean live;
      guint64 latency;
      GstPad *peer;
      gint rate = legacyresample->i_rate;
      gint resampler_latency = legacyresample->filter_length / 2;

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
  gst_object_unref (legacyresample);
  return res;
}

static const GstQueryType *
legacyresample_query_type (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_LATENCY,
    0
  };

  return types;
}

static void
gst_legacyresample_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstLegacyresample *legacyresample;

  legacyresample = GST_LEGACYRESAMPLE (object);

  switch (prop_id) {
    case PROP_FILTERLEN:
      legacyresample->filter_length = g_value_get_int (value);
      GST_DEBUG_OBJECT (GST_ELEMENT (legacyresample), "new filter length %d",
          legacyresample->filter_length);
      if (legacyresample->resample) {
        resample_set_filter_length (legacyresample->resample,
            legacyresample->filter_length);
        gst_element_post_message (GST_ELEMENT (legacyresample),
            gst_message_new_latency (GST_OBJECT (legacyresample)));
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_legacyresample_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstLegacyresample *legacyresample;

  legacyresample = GST_LEGACYRESAMPLE (object);

  switch (prop_id) {
    case PROP_FILTERLEN:
      g_value_set_int (value, legacyresample->filter_length);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  resample_init ();

  if (!gst_element_register (plugin, "legacyresample", GST_RANK_MARGINAL,
          GST_TYPE_LEGACYRESAMPLE)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "legacyresample",
    "Resamples audio", plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN);
