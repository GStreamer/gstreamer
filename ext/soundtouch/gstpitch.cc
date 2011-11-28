/* GStreamer pitch controller element
 * Copyright (C) 2006 Wouter Paesen <wouter@blue-gate.be>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* FIXME: workaround for SoundTouch.h of version 1.3.1 defining those
 * variables while it shouldn't. */
#undef VERSION
#undef PACKAGE_VERSION
#undef PACKAGE_TARNAME
#undef PACKAGE_STRING
#undef PACKAGE_NAME
#undef PACKAGE_BUGREPORT
#undef PACKAGE

#define FLOAT_SAMPLES 1
#include <soundtouch/SoundTouch.h>

#include <gst/gst.h>
#include <gst/controller/gstcontroller.h>
#include "gstpitch.hh"
#include <math.h>

GST_DEBUG_CATEGORY_STATIC (pitch_debug);
#define GST_CAT_DEFAULT pitch_debug

#define GST_PITCH_GET_PRIVATE(o) (o->priv)
struct _GstPitchPrivate
{
  gfloat stream_time_ratio;

  GstEvent *pending_segment;

    soundtouch::SoundTouch * st;
};

enum
{
  ARG_0,
  ARG_OUT_RATE,
  ARG_RATE,
  ARG_TEMPO,
  ARG_PITCH
};

#define SUPPORTED_CAPS \
GST_STATIC_CAPS( \
  "audio/x-raw-float, " \
    "rate = (int) [ 8000, MAX ], " \
    "channels = (int) [ 1, 2 ], " \
    "endianness = (int) BYTE_ORDER, " \
    "width = (int) 32" \
)

static GstStaticPadTemplate gst_pitch_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    SUPPORTED_CAPS);

static GstStaticPadTemplate gst_pitch_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    SUPPORTED_CAPS);

static void gst_pitch_dispose (GObject * object);
static void gst_pitch_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_pitch_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);


static gboolean gst_pitch_sink_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_pitch_chain (GstPad * pad, GstBuffer * buffer);
static GstStateChangeReturn gst_pitch_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_pitch_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_pitch_src_event (GstPad * pad, GstEvent * event);

static gboolean gst_pitch_src_query (GstPad * pad, GstQuery * query);
static const GstQueryType *gst_pitch_get_query_types (GstPad * pad);

GST_BOILERPLATE (GstPitch, gst_pitch, GstElement, GST_TYPE_ELEMENT);

static void
gst_pitch_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_pitch_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_pitch_sink_template);

  gst_element_class_set_details_simple (gstelement_class, "Pitch controller",
      "Filter/Converter/Audio", "Control the pitch of an audio stream",
      "Wouter Paesen <wouter@blue-gate.be>");
}

static void
gst_pitch_class_init (GstPitchClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (pitch_debug, "pitch", 0,
      "audio pitch control element");

  gobject_class->set_property = gst_pitch_set_property;
  gobject_class->get_property = gst_pitch_get_property;
  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_pitch_dispose);
  element_class->change_state = GST_DEBUG_FUNCPTR (gst_pitch_change_state);

  g_object_class_install_property (gobject_class, ARG_PITCH,
      g_param_spec_float ("pitch", "Pitch",
          "Audio stream pitch", 0.1, 10.0, 1.0,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, ARG_TEMPO,
      g_param_spec_float ("tempo", "Tempo",
          "Audio stream tempo", 0.1, 10.0, 1.0,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, ARG_RATE,
      g_param_spec_float ("rate", "Rate",
          "Audio stream rate", 0.1, 10.0, 1.0,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, ARG_OUT_RATE,
      g_param_spec_float ("output-rate", "Output Rate",
          "Output rate on downstream segment events", 0.1, 10.0, 1.0,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS)));

  g_type_class_add_private (gobject_class, sizeof (GstPitchPrivate));
}

static void
gst_pitch_init (GstPitch * pitch, GstPitchClass * pitch_class)
{
  pitch->priv =
      G_TYPE_INSTANCE_GET_PRIVATE ((pitch), GST_TYPE_PITCH, GstPitchPrivate);

  pitch->sinkpad =
      gst_pad_new_from_static_template (&gst_pitch_sink_template, "sink");
  gst_pad_set_chain_function (pitch->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pitch_chain));
  gst_pad_set_event_function (pitch->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pitch_sink_event));
  gst_pad_set_setcaps_function (pitch->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pitch_sink_setcaps));
  gst_pad_set_getcaps_function (pitch->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_element_add_pad (GST_ELEMENT (pitch), pitch->sinkpad);

  pitch->srcpad =
      gst_pad_new_from_static_template (&gst_pitch_src_template, "src");
  gst_pad_set_event_function (pitch->srcpad,
      GST_DEBUG_FUNCPTR (gst_pitch_src_event));
  gst_pad_set_query_type_function (pitch->srcpad,
      GST_DEBUG_FUNCPTR (gst_pitch_get_query_types));
  gst_pad_set_query_function (pitch->srcpad,
      GST_DEBUG_FUNCPTR (gst_pitch_src_query));
  gst_pad_set_setcaps_function (pitch->srcpad,
      GST_DEBUG_FUNCPTR (gst_pitch_sink_setcaps));
  gst_pad_set_getcaps_function (pitch->srcpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_element_add_pad (GST_ELEMENT (pitch), pitch->srcpad);

  pitch->priv->st = new soundtouch::SoundTouch ();

  pitch->tempo = 1.0;
  pitch->rate = 1.0;
  pitch->out_seg_rate = 1.0;
  pitch->seg_arate = 1.0;
  pitch->pitch = 1.0;
  pitch->next_buffer_time = 0;
  pitch->next_buffer_offset = 0;

  pitch->priv->st->setRate (pitch->rate);
  pitch->priv->st->setTempo (pitch->tempo * pitch->seg_arate);
  pitch->priv->st->setPitch (pitch->pitch);

  pitch->priv->stream_time_ratio = 1.0;
  pitch->min_latency = pitch->max_latency = 0;
}


static void
gst_pitch_dispose (GObject * object)
{
  GstPitch *pitch = GST_PITCH (object);

  if (pitch->priv->st) {
    delete pitch->priv->st;

    pitch->priv->st = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_pitch_update_duration (GstPitch * pitch)
{
  GstMessage *m;

  m = gst_message_new_duration (GST_OBJECT (pitch), GST_FORMAT_TIME, -1);
  gst_element_post_message (GST_ELEMENT (pitch), m);
}

static void
gst_pitch_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPitch *pitch = GST_PITCH (object);

  GST_OBJECT_LOCK (pitch);
  switch (prop_id) {
    case ARG_TEMPO:
      pitch->tempo = g_value_get_float (value);
      pitch->priv->stream_time_ratio = pitch->tempo * pitch->rate * pitch->seg_arate;
      pitch->priv->st->setTempo (pitch->tempo * pitch->seg_arate);
      GST_OBJECT_UNLOCK (pitch);
      gst_pitch_update_duration (pitch);
      break;
    case ARG_RATE:
      pitch->rate = g_value_get_float (value);
      pitch->priv->stream_time_ratio = pitch->tempo * pitch->rate * pitch->seg_arate;
      pitch->priv->st->setRate (pitch->rate);
      GST_OBJECT_UNLOCK (pitch);
      gst_pitch_update_duration (pitch);
      break;
    case ARG_OUT_RATE:
      /* Has no effect until the next input segment */
      pitch->out_seg_rate = g_value_get_float (value);
      GST_OBJECT_UNLOCK (pitch);
    case ARG_PITCH:
      pitch->pitch = g_value_get_float (value);
      pitch->priv->st->setPitch (pitch->pitch);
      GST_OBJECT_UNLOCK (pitch);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      GST_OBJECT_UNLOCK (pitch);
      break;
  }
}

static void
gst_pitch_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstPitch *pitch = GST_PITCH (object);

  GST_OBJECT_LOCK (pitch);
  switch (prop_id) {
    case ARG_TEMPO:
      g_value_set_float (value, pitch->tempo);
      break;
    case ARG_RATE:
      g_value_set_float (value, pitch->rate);
      break;
    case ARG_OUT_RATE:
      g_value_set_float (value, pitch->out_seg_rate);
      break;
    case ARG_PITCH:
      g_value_set_float (value, pitch->pitch);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (pitch);
}

static gboolean
gst_pitch_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstPitch *pitch;
  GstPitchPrivate *priv;
  GstStructure *structure;
  GstPad *otherpad;
  gint rate, channels;

  pitch = GST_PITCH (GST_PAD_PARENT (pad));
  priv = GST_PITCH_GET_PRIVATE (pitch);

  otherpad = (pad == pitch->srcpad) ? pitch->sinkpad : pitch->srcpad;

  if (!gst_pad_set_caps (otherpad, caps))
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "rate", &rate) ||
      !gst_structure_get_int (structure, "channels", &channels)) {
    return FALSE;
  }

  GST_OBJECT_LOCK (pitch);

  pitch->samplerate = rate;
  pitch->channels = channels;

  /* notify the soundtouch instance of this change */
  priv->st->setSampleRate (rate);
  priv->st->setChannels (channels);

  /* calculate sample size */
  pitch->sample_size = (sizeof (gfloat) * channels);

  GST_OBJECT_UNLOCK (pitch);

  return TRUE;
}

/* send a buffer out */
static GstFlowReturn
gst_pitch_forward_buffer (GstPitch * pitch, GstBuffer * buffer)
{
  gint samples;

  GST_BUFFER_TIMESTAMP (buffer) = pitch->next_buffer_time;
  pitch->next_buffer_time += GST_BUFFER_DURATION (buffer);

  samples = GST_BUFFER_OFFSET (buffer);
  GST_BUFFER_OFFSET (buffer) = pitch->next_buffer_offset;
  pitch->next_buffer_offset += samples;
  GST_BUFFER_OFFSET_END (buffer) = pitch->next_buffer_offset;

  GST_LOG ("pushing buffer [%" GST_TIME_FORMAT "]-[%" GST_TIME_FORMAT
      "] (%d samples)", GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (pitch->next_buffer_time), samples);

  return gst_pad_push (pitch->srcpad, buffer);
}

/* extract a buffer from soundtouch */
static GstBuffer *
gst_pitch_prepare_buffer (GstPitch * pitch)
{
  GstPitchPrivate *priv;
  guint samples;
  GstBuffer *buffer;

  priv = GST_PITCH_GET_PRIVATE (pitch);

  GST_LOG_OBJECT (pitch, "preparing buffer");

  samples = pitch->priv->st->numSamples ();
  if (samples == 0)
    return NULL;

  if (gst_pad_alloc_buffer_and_set_caps (pitch->srcpad, GST_BUFFER_OFFSET_NONE,
          samples * pitch->sample_size, GST_PAD_CAPS (pitch->srcpad), &buffer)
      != GST_FLOW_OK) {
    buffer = gst_buffer_new_and_alloc (samples * pitch->sample_size);
    gst_buffer_set_caps (buffer, GST_PAD_CAPS (pitch->srcpad));
  }

  samples =
      priv->st->receiveSamples ((gfloat *) GST_BUFFER_DATA (buffer), samples);

  if (samples <= 0) {
    gst_buffer_unref (buffer);
    return NULL;
  }

  GST_BUFFER_DURATION (buffer) =
      gst_util_uint64_scale (samples, GST_SECOND, pitch->samplerate);
  /* temporary store samples here, to avoid having to recalculate this */
  GST_BUFFER_OFFSET (buffer) = (gint64) samples;

  return buffer;
}

/* process the last samples, in a later stage we should make sure no more
 * samples are sent out here as strictly necessary, because soundtouch could
 * append zero samples, which could disturb looping.  */
static GstFlowReturn
gst_pitch_flush_buffer (GstPitch * pitch, gboolean send)
{
  GstBuffer *buffer;

  GST_DEBUG_OBJECT (pitch, "flushing buffer");

  if (pitch->next_buffer_offset == 0)
    return GST_FLOW_OK;

  pitch->priv->st->flush ();
  if (!send)
    return GST_FLOW_OK;

  buffer = gst_pitch_prepare_buffer (pitch);

  if (!buffer)
    return GST_FLOW_OK;

  return gst_pitch_forward_buffer (pitch, buffer);
}

static gboolean
gst_pitch_src_event (GstPad * pad, GstEvent * event)
{
  GstPitch *pitch;
  gboolean res;

  pitch = GST_PITCH (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (pad, "received %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      /* transform the event upstream, according to the playback rate */
      gdouble rate;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType cur_type, stop_type;
      gint64 cur, stop;
      gfloat stream_time_ratio;

      GST_OBJECT_LOCK (pitch);
      stream_time_ratio = pitch->priv->stream_time_ratio;
      GST_OBJECT_UNLOCK (pitch);

      gst_event_parse_seek (event, &rate, &format, &flags,
          &cur_type, &cur, &stop_type, &stop);

      gst_event_unref (event);

      if (format == GST_FORMAT_TIME || format == GST_FORMAT_DEFAULT) {
        cur = (gint64) (cur * stream_time_ratio);
        if (stop != -1)
          stop = (gint64) (stop * stream_time_ratio);

        event = gst_event_new_seek (rate, format, flags,
            cur_type, cur, stop_type, stop);
        res = gst_pad_event_default (pad, event);
      } else {
        GST_WARNING_OBJECT (pitch,
            "Seeking only supported in TIME or DEFAULT format");
        res = FALSE;
      }

      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (pitch);
  return res;
}

/* generic convert function based on caps, no rate 
 * used here 
 */
static gboolean
gst_pitch_convert (GstPitch * pitch,
    GstFormat src_format, gint64 src_value,
    GstFormat * dst_format, gint64 * dst_value)
{
  gboolean res = TRUE;
  guint sample_size;
  gint samplerate;

  g_return_val_if_fail (dst_format && dst_value, FALSE);

  GST_OBJECT_LOCK (pitch);
  sample_size = pitch->sample_size;
  samplerate = pitch->samplerate;
  GST_OBJECT_UNLOCK (pitch);

  if (sample_size == 0 || samplerate == 0) {
    return FALSE;
  }

  if (src_format == *dst_format || src_value == -1) {
    *dst_value = src_value;
    return TRUE;
  }

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dst_format) {
        case GST_FORMAT_TIME:
          *dst_value =
              gst_util_uint64_scale_int (src_value, GST_SECOND,
              sample_size * samplerate);
          break;
        case GST_FORMAT_DEFAULT:
          *dst_value = gst_util_uint64_scale_int (src_value, 1, sample_size);
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dst_format) {
        case GST_FORMAT_BYTES:
          *dst_value =
              gst_util_uint64_scale_int (src_value, samplerate * sample_size,
              GST_SECOND);
          break;
        case GST_FORMAT_DEFAULT:
          *dst_value =
              gst_util_uint64_scale_int (src_value, samplerate, GST_SECOND);
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dst_format) {
        case GST_FORMAT_BYTES:
          *dst_value = gst_util_uint64_scale_int (src_value, sample_size, 1);
          break;
        case GST_FORMAT_TIME:
          *dst_value =
              gst_util_uint64_scale_int (src_value, GST_SECOND, samplerate);
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    default:
      res = FALSE;
      break;
  }

  return res;
}

static const GstQueryType *
gst_pitch_get_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    GST_QUERY_LATENCY,
    GST_QUERY_NONE
  };

  return types;
}

static gboolean
gst_pitch_src_query (GstPad * pad, GstQuery * query)
{
  GstPitch *pitch;
  gboolean res = FALSE;
  gfloat stream_time_ratio;
  gint64 next_buffer_offset;
  GstClockTime next_buffer_time;

  pitch = GST_PITCH (gst_pad_get_parent (pad));
  GST_LOG ("%s query", GST_QUERY_TYPE_NAME (query));
  GST_OBJECT_LOCK (pitch);
  stream_time_ratio = pitch->priv->stream_time_ratio;
  next_buffer_time = pitch->next_buffer_time;
  next_buffer_offset = pitch->next_buffer_offset;
  GST_OBJECT_UNLOCK (pitch);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:{
      GstFormat format;
      gint64 duration;

      if (!gst_pad_query_default (pad, query)) {
        GST_DEBUG_OBJECT (pitch, "upstream provided no duration");
        break;
      }

      gst_query_parse_duration (query, &format, &duration);

      if (format != GST_FORMAT_TIME && format != GST_FORMAT_DEFAULT) {
        GST_DEBUG_OBJECT (pitch, "not TIME or DEFAULT format");
        break;
      }
      GST_LOG_OBJECT (pitch, "upstream duration: %" G_GINT64_FORMAT, duration);
      duration = (gint64) (duration / stream_time_ratio);
      GST_LOG_OBJECT (pitch, "our duration: %" G_GINT64_FORMAT, duration);
      gst_query_set_duration (query, format, duration);
      res = TRUE;
      break;
    }
    case GST_QUERY_POSITION:{
      GstFormat dst_format;
      gint64 dst_value;

      gst_query_parse_position (query, &dst_format, &dst_value);

      if (dst_format != GST_FORMAT_TIME && dst_format != GST_FORMAT_DEFAULT) {
        GST_DEBUG_OBJECT (pitch, "not TIME or DEFAULT format");
        break;
      }

      if (dst_format == GST_FORMAT_TIME) {
        dst_value = next_buffer_time;
        res = TRUE;
      } else {
        dst_value = next_buffer_offset;
        res = TRUE;
      }

      if (res) {
        GST_LOG_OBJECT (pitch, "our position: %" G_GINT64_FORMAT, dst_value);
        gst_query_set_position (query, dst_format, dst_value);
      }
      break;
    }
    case GST_QUERY_CONVERT:{
      GstFormat src_format, dst_format;
      gint64 src_value, dst_value;

      gst_query_parse_convert (query, &src_format, &src_value,
          &dst_format, NULL);

      res = gst_pitch_convert (pitch, src_format, src_value,
          &dst_format, &dst_value);

      if (res) {
        gst_query_set_convert (query, src_format, src_value,
            dst_format, dst_value);
      }
      break;
    }
    case GST_QUERY_LATENCY:
    {
      GstClockTime min, max;
      gboolean live;
      GstPad *peer;

      if ((peer = gst_pad_get_peer (pitch->sinkpad))) {
        if ((res = gst_pad_query (peer, query))) {
          gst_query_parse_latency (query, &live, &min, &max);

          GST_DEBUG ("Peer latency: min %"
              GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
              GST_TIME_ARGS (min), GST_TIME_ARGS (max));

          /* add our own latency */

          GST_DEBUG ("Our latency: min %" GST_TIME_FORMAT
              ", max %" GST_TIME_FORMAT,
              GST_TIME_ARGS (pitch->min_latency),
              GST_TIME_ARGS (pitch->max_latency));

          min += pitch->min_latency;
          if (max != GST_CLOCK_TIME_NONE)
            max += pitch->max_latency;
          else
            max = pitch->max_latency;

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

  gst_object_unref (pitch);
  return res;
}

/* this function returns FALSE if not enough data is known to transform the
 * segment into proper downstream values.  If the function does return false
 * the sgement should be stalled until enough information is available.
 * If the funtion returns TRUE, event will be replaced by the new downstream
 * compatible event.
 */
static gboolean
gst_pitch_process_segment (GstPitch * pitch, GstEvent ** event)
{
  GstFormat format, conv_format;
  gint64 start_value, stop_value, base;
  gint64 next_offset = 0, next_time = 0;
  gboolean update = FALSE;
  gdouble rate, out_seg_rate, arate, our_arate;
  gfloat stream_time_ratio;

  g_return_val_if_fail (event, FALSE);

  GST_OBJECT_LOCK (pitch);
  stream_time_ratio = pitch->priv->stream_time_ratio;
  out_seg_rate = pitch->out_seg_rate;
  GST_OBJECT_UNLOCK (pitch);

  gst_event_parse_new_segment_full (*event, &update, &rate, &arate, &format, &start_value,
      &stop_value, &base);

  if (format != GST_FORMAT_TIME && format != GST_FORMAT_DEFAULT) {
    GST_WARNING_OBJECT (pitch,
        "Only NEWSEGMENT in TIME or DEFAULT format supported, sending"
        "open ended NEWSEGMENT in TIME format.");
    gst_event_unref (*event);
    *event =
        gst_event_new_new_segment_full (update, rate, arate, GST_FORMAT_TIME, 0, -1, 0);
    start_value = 0;
    stop_value = -1;
    base = 0;
  }

  /* Figure out how much of the incoming 'rate' we'll apply ourselves */
  our_arate = rate / out_seg_rate;
  /* update the output rate variables */
  rate = out_seg_rate;
  arate *= our_arate;

  GST_LOG_OBJECT (pitch->sinkpad,
      "segment %" G_GINT64_FORMAT " - %" G_GINT64_FORMAT " (%d)", start_value,
      stop_value, format);

  stream_time_ratio = pitch->tempo * pitch->rate * pitch->seg_arate;

  if (stream_time_ratio == 0) {
    GST_LOG_OBJECT (pitch->sinkpad, "stream_time_ratio is zero");
    return FALSE;
  }

  /* Update the playback rate */
  GST_OBJECT_LOCK (pitch);
  pitch->seg_arate = our_arate;
  pitch->priv->stream_time_ratio = stream_time_ratio;
  pitch->priv->st->setTempo (pitch->tempo * pitch->seg_arate);
  GST_OBJECT_UNLOCK (pitch);

  start_value = (gint64) (start_value / stream_time_ratio);
  if (stop_value != -1)
    stop_value = (gint64) (stop_value / stream_time_ratio);
  base = (gint64) (base / stream_time_ratio);

  conv_format = GST_FORMAT_TIME;
  if (!gst_pitch_convert (pitch, format, start_value, &conv_format, &next_time)) {
    GST_LOG_OBJECT (pitch->sinkpad,
        "could not convert segment start value to time");
    return FALSE;
  }

  conv_format = GST_FORMAT_DEFAULT;
  if (!gst_pitch_convert (pitch, format, start_value, &conv_format,
          &next_offset)) {
    GST_LOG_OBJECT (pitch->sinkpad,
        "could not convert segment start value to offset");
    return FALSE;
  }

  pitch->next_buffer_time = next_time;
  pitch->next_buffer_offset = next_offset;

  gst_event_unref (*event);
  *event = gst_event_new_new_segment_full (update, rate, arate, format,
       start_value, stop_value, base);

  return TRUE;
}

static gboolean
gst_pitch_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstPitch *pitch;

  pitch = GST_PITCH (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (pad, "received %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_pitch_flush_buffer (pitch, FALSE);
      pitch->priv->st->clear ();
      pitch->next_buffer_offset = 0;
      pitch->next_buffer_time = 0;
      pitch->min_latency = pitch->max_latency = 0;
      break;
    case GST_EVENT_EOS:
      gst_pitch_flush_buffer (pitch, TRUE);
      pitch->priv->st->clear ();
      pitch->min_latency = pitch->max_latency = 0;
      break;
    case GST_EVENT_NEWSEGMENT:
      if (!gst_pitch_process_segment (pitch, &event)) {
        GST_LOG_OBJECT (pad, "not enough data known, stalling segment");
        if (GST_PITCH_GET_PRIVATE (pitch)->pending_segment)
          gst_event_unref (GST_PITCH_GET_PRIVATE (pitch)->pending_segment);
        GST_PITCH_GET_PRIVATE (pitch)->pending_segment = event;
        event = NULL;
      }
      pitch->priv->st->clear ();
      pitch->min_latency = pitch->max_latency = 0;
      break;
    default:
      break;
  }

  /* and forward it */
  if (event)
    res = gst_pad_event_default (pad, event);

  gst_object_unref (pitch);
  return res;
}

static void
gst_pitch_update_latency (GstPitch * pitch, GstClockTime timestamp)
{
  GstClockTimeDiff current_latency, min_latency, max_latency;

  current_latency =
      (GstClockTimeDiff) (timestamp / pitch->priv->stream_time_ratio) -
      pitch->next_buffer_time;

  min_latency = MIN (pitch->min_latency, current_latency);
  max_latency = MAX (pitch->max_latency, current_latency);

  if (pitch->min_latency != min_latency || pitch->max_latency != max_latency) {
    pitch->min_latency = min_latency;
    pitch->max_latency = max_latency;

    /* FIXME: what about the LATENCY event? It only has
     * one latency value, should it be current, min or max?
     * Should it include upstream latencies?
     */

    gst_element_post_message (GST_ELEMENT (pitch),
        gst_message_new_latency (GST_OBJECT (pitch)));
  }
}

static GstFlowReturn
gst_pitch_chain (GstPad * pad, GstBuffer * buffer)
{
  GstPitch *pitch;
  GstPitchPrivate *priv;
  GstClockTime timestamp;

  pitch = GST_PITCH (GST_PAD_PARENT (pad));
  priv = GST_PITCH_GET_PRIVATE (pitch);

  gst_object_sync_values (G_OBJECT (pitch), pitch->next_buffer_time);

  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  /* push the received samples on the soundtouch buffer */
  GST_LOG_OBJECT (pitch, "incoming buffer (%d samples)",
      (gint) (GST_BUFFER_SIZE (buffer) / pitch->sample_size));

  if (GST_PITCH_GET_PRIVATE (pitch)->pending_segment) {
    GstEvent *event =
        gst_event_copy (GST_PITCH_GET_PRIVATE (pitch)->pending_segment);

    GST_LOG_OBJECT (pitch, "processing stalled segment");
    if (!gst_pitch_process_segment (pitch, &event)) {
      gst_event_unref (event);
      return GST_FLOW_ERROR;
    }

    if (!gst_pad_event_default (pitch->sinkpad, event)) {
      gst_event_unref (event);
      return GST_FLOW_ERROR;
    }

    gst_event_unref (GST_PITCH_GET_PRIVATE (pitch)->pending_segment);
    GST_PITCH_GET_PRIVATE (pitch)->pending_segment = NULL;
  }

  priv->st->putSamples ((gfloat *) GST_BUFFER_DATA (buffer),
      GST_BUFFER_SIZE (buffer) / pitch->sample_size);
  gst_buffer_unref (buffer);

  /* Calculate latency */

  gst_pitch_update_latency (pitch, timestamp);

  /* and try to extract some samples from the soundtouch buffer */
  if (!priv->st->isEmpty ()) {
    GstBuffer *out_buffer;

    out_buffer = gst_pitch_prepare_buffer (pitch);
    if (out_buffer)
      return gst_pitch_forward_buffer (pitch, out_buffer);
  }

  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_pitch_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstPitch *pitch = GST_PITCH (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      pitch->next_buffer_time = 0;
      pitch->next_buffer_offset = 0;
      pitch->priv->st->clear ();
      pitch->min_latency = pitch->max_latency = 0;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (GST_PITCH_GET_PRIVATE (pitch)->pending_segment) {
        gst_event_unref (GST_PITCH_GET_PRIVATE (pitch)->pending_segment);
        GST_PITCH_GET_PRIVATE (pitch)->pending_segment = NULL;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
    default:
      break;
  }

  return ret;
}
