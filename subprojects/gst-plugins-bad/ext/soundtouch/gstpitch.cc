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

#include <SoundTouch.h>

#include <gst/gst.h>
#include <gst/audio/audio.h>

#include "gstpitch.hh"

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
  ARG_OUTPUT_RATE,
  ARG_RATE,
  ARG_TEMPO,
  ARG_PITCH
};

/* For soundtouch 1.4 */
#if defined(INTEGER_SAMPLES)
#define SOUNDTOUCH_INTEGER_SAMPLES 1
#elif defined(FLOAT_SAMPLES)
#define SOUNDTOUCH_FLOAT_SAMPLES 1
#endif

#if defined(SOUNDTOUCH_FLOAT_SAMPLES)
#define SUPPORTED_CAPS \
    "audio/x-raw, " \
      "format = (string) " GST_AUDIO_NE (F32) ", " \
      "rate = (int) [ 8000, MAX ], " \
      "channels = (int) [ 1, MAX ], " \
      "layout = (string) interleaved"
#elif defined(SOUNDTOUCH_INTEGER_SAMPLES)
#define SUPPORTED_CAPS \
    "audio/x-raw, " \
      "format = (string) " GST_AUDIO_NE (S16) ", " \
      "rate = (int) [ 8000, MAX ], " \
      "channels = (int) [ 1, MAX ]", \
      "layout = (string) interleaved"
#else
#error "Only integer or float samples are supported"
#endif

static GstStaticPadTemplate gst_pitch_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SUPPORTED_CAPS));

static GstStaticPadTemplate gst_pitch_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SUPPORTED_CAPS));

static void gst_pitch_dispose (GObject * object);
static void gst_pitch_finalize (GObject * object);
static void gst_pitch_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_pitch_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);


static gboolean gst_pitch_setcaps (GstPitch * pitch, GstCaps * caps);
static GstFlowReturn gst_pitch_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static GstStateChangeReturn gst_pitch_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_pitch_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_pitch_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static gboolean gst_pitch_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

#define gst_pitch_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstPitch, gst_pitch, GST_TYPE_ELEMENT);
GST_ELEMENT_REGISTER_DEFINE (pitch, "pitch", GST_RANK_NONE, GST_TYPE_PITCH);

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
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_pitch_finalize);

  g_object_class_install_property (gobject_class, ARG_PITCH,
      g_param_spec_float ("pitch", "Pitch",
          "Audio stream pitch", 0.1, 10.0, 1.0,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, ARG_TEMPO,
      g_param_spec_float ("tempo", "Tempo",
          "Audio stream tempo", 0.1, 10.0, 1.0,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, ARG_RATE,
      g_param_spec_float ("rate", "Rate",
          "Audio stream rate", 0.1, 10.0, 1.0,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, ARG_OUTPUT_RATE,
      g_param_spec_float ("output-rate", "Output Rate",
          "Output rate on downstream segment events", 0.1, 10.0, 1.0,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_pitch_change_state);

  gst_element_class_add_static_pad_template (element_class,
      &gst_pitch_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_pitch_sink_template);

  gst_element_class_set_static_metadata (element_class, "Pitch controller",
      "Filter/Effect/Audio", "Control the pitch of an audio stream",
      "Wouter Paesen <wouter@blue-gate.be>");
}

static void
gst_pitch_init (GstPitch * pitch)
{
  GstPitchPrivate *priv =
      (GstPitchPrivate *) gst_pitch_get_instance_private (pitch);
  GST_PITCH_GET_PRIVATE (pitch) = priv;

  pitch->sinkpad =
      gst_pad_new_from_static_template (&gst_pitch_sink_template, "sink");
  gst_pad_set_chain_function (pitch->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pitch_chain));
  gst_pad_set_event_function (pitch->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pitch_sink_event));
  GST_PAD_SET_PROXY_CAPS (pitch->sinkpad);
  gst_element_add_pad (GST_ELEMENT (pitch), pitch->sinkpad);

  pitch->srcpad =
      gst_pad_new_from_static_template (&gst_pitch_src_template, "src");
  gst_pad_set_event_function (pitch->srcpad,
      GST_DEBUG_FUNCPTR (gst_pitch_src_event));
  gst_pad_set_query_function (pitch->srcpad,
      GST_DEBUG_FUNCPTR (gst_pitch_src_query));
  GST_PAD_SET_PROXY_CAPS (pitch->sinkpad);
  gst_element_add_pad (GST_ELEMENT (pitch), pitch->srcpad);

  priv->st = new soundtouch::SoundTouch ();

  pitch->tempo = 1.0;
  pitch->rate = 1.0;
  pitch->output_rate = 1.0;
  pitch->segment_applied_rate = 1.0;
  pitch->pitch = 1.0;
  pitch->next_buffer_time = GST_CLOCK_TIME_NONE;
  pitch->next_buffer_offset = 0;

  priv->st->setRate (pitch->rate);
  priv->st->setTempo (pitch->tempo * pitch->segment_applied_rate);
  priv->st->setPitch (pitch->pitch);

  priv->stream_time_ratio = 1.0;
  pitch->min_latency = pitch->max_latency = 0;
}


static void
gst_pitch_dispose (GObject * object)
{
  GstPitch *pitch = GST_PITCH (object);
  GstPitchPrivate *priv = GST_PITCH_GET_PRIVATE (pitch);

  gst_clear_event (&priv->pending_segment);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_pitch_finalize (GObject * object)
{
  GstPitch *pitch = GST_PITCH (object);
  GstPitchPrivate *priv = GST_PITCH_GET_PRIVATE (pitch);

  if (priv->st) {
    delete priv->st;
    priv->st = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_pitch_update_duration (GstPitch * pitch)
{
  GstMessage *m;

  m = gst_message_new_duration_changed (GST_OBJECT (pitch));
  gst_element_post_message (GST_ELEMENT (pitch), m);
}

static void
gst_pitch_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPitch *pitch = GST_PITCH (object);
  GstPitchPrivate *priv = GST_PITCH_GET_PRIVATE (pitch);

  switch (prop_id) {
    case ARG_TEMPO:
      GST_OBJECT_LOCK (pitch);
      pitch->tempo = g_value_get_float (value);
      priv->stream_time_ratio =
          pitch->tempo * pitch->rate * ABS (pitch->segment_applied_rate);
      priv->st->setTempo (pitch->tempo * ABS (pitch->segment_applied_rate));
      GST_OBJECT_UNLOCK (pitch);
      gst_pitch_update_duration (pitch);
      break;
    case ARG_RATE:
      GST_OBJECT_LOCK (pitch);
      pitch->rate = g_value_get_float (value);
      priv->stream_time_ratio =
          pitch->tempo * pitch->rate * ABS (pitch->segment_applied_rate);
      priv->st->setRate (pitch->rate);
      GST_OBJECT_UNLOCK (pitch);
      gst_pitch_update_duration (pitch);
      break;
    case ARG_OUTPUT_RATE:
      /* Has no effect until the next input segment */
      GST_OBJECT_LOCK (pitch);
      pitch->output_rate = g_value_get_float (value);
      GST_OBJECT_UNLOCK (pitch);
      break;
    case ARG_PITCH:
      GST_OBJECT_LOCK (pitch);
      pitch->pitch = g_value_get_float (value);
      priv->st->setPitch (pitch->pitch);
      GST_OBJECT_UNLOCK (pitch);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_pitch_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstPitch *pitch = GST_PITCH (object);

  switch (prop_id) {
    case ARG_TEMPO:
      GST_OBJECT_LOCK (pitch);
      g_value_set_float (value, pitch->tempo);
      GST_OBJECT_UNLOCK (pitch);
      break;
    case ARG_RATE:
      GST_OBJECT_LOCK (pitch);
      g_value_set_float (value, pitch->rate);
      GST_OBJECT_UNLOCK (pitch);
      break;
    case ARG_OUTPUT_RATE:
      GST_OBJECT_LOCK (pitch);
      g_value_set_float (value, pitch->output_rate);
      GST_OBJECT_UNLOCK (pitch);
      break;
    case ARG_PITCH:
      GST_OBJECT_LOCK (pitch);
      g_value_set_float (value, pitch->pitch);
      GST_OBJECT_UNLOCK (pitch);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_pitch_setcaps (GstPitch * pitch, GstCaps * caps)
{
  GstAudioInfo info;
  if (!gst_audio_info_from_caps (&info, caps))
    return FALSE;

  GST_OBJECT_LOCK (pitch);
  pitch->info = info;

  /* notify the soundtouch instance of this change */
  GstPitchPrivate *priv = GST_PITCH_GET_PRIVATE (pitch);
  priv->st->setSampleRate (pitch->info.rate);
  priv->st->setChannels (pitch->info.channels);
  GST_OBJECT_UNLOCK (pitch);

  return TRUE;
}

/* send a buffer out */
static GstFlowReturn
gst_pitch_forward_buffer (GstPitch * pitch, GstBuffer * buffer)
{
  guint64 samples = GST_BUFFER_OFFSET (buffer);

  GST_OBJECT_LOCK (pitch);

  if (pitch->segment_applied_rate >= 0) {
    GST_BUFFER_PTS (buffer) = pitch->next_buffer_time;
    pitch->next_buffer_time += GST_BUFFER_DURATION (buffer);

    GST_BUFFER_OFFSET (buffer) = pitch->next_buffer_offset;
    pitch->next_buffer_offset += samples;
    GST_BUFFER_OFFSET_END (buffer) = pitch->next_buffer_offset;
  } else {
    if (pitch->next_buffer_time > GST_BUFFER_DURATION (buffer))
      pitch->next_buffer_time -= GST_BUFFER_DURATION (buffer);
    else
      pitch->next_buffer_time = 0;
    GST_BUFFER_PTS (buffer) = pitch->next_buffer_time;

    GST_BUFFER_OFFSET_END (buffer) = pitch->next_buffer_offset;
    if (pitch->next_buffer_offset > samples)
      pitch->next_buffer_offset -= samples;
    else
      pitch->next_buffer_offset = 0;
    GST_BUFFER_OFFSET (buffer) = pitch->next_buffer_offset;
  }

  GST_OBJECT_UNLOCK (pitch);

  GST_LOG ("pushing buffer pts: %" GST_TIME_FORMAT ", duration: %"
      GST_TIME_FORMAT ", samples: %" G_GUINT64_FORMAT, GST_TIME_ARGS (GST_BUFFER_PTS (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)), samples);

  return gst_pad_push (pitch->srcpad, buffer);
}

/* extract a buffer from soundtouch */
static GstBuffer *
gst_pitch_prepare_buffer (GstPitch * pitch)
{
  GstMapInfo info;
  GstBuffer *buffer = NULL;
  GST_LOG_OBJECT (pitch, "preparing buffer");

  GST_OBJECT_LOCK (pitch);
  gboolean reverse_playback = (pitch->segment_applied_rate < 0);
  gint rate = pitch->info.rate;
  gint bytes_per_frame = pitch->info.bpf;

  GstPitchPrivate *priv = GST_PITCH_GET_PRIVATE (pitch);
  guint samples = priv->st->numSamples ();
  if (samples > 0) {
    buffer = gst_buffer_new_and_alloc (samples * bytes_per_frame);
    gst_buffer_map (buffer, &info, (GstMapFlags) GST_MAP_READWRITE);

    samples =
        priv->st->receiveSamples ((soundtouch::SAMPLETYPE *) info.data,
        samples);

    if (samples <= 0) {
      gst_buffer_unmap (buffer, &info);
      gst_buffer_unref (buffer);
      buffer = NULL;
    }
  }
  GST_OBJECT_UNLOCK (pitch);

  if (buffer) {
    if (reverse_playback && samples > 1) {
      // If playing backwards, we need to inverse the order of all received samples
      guint8 *dest = info.data;
      guint8 val = 0;
      guint half_samples = samples >> 1;
      for (guint i = samples - 1; i >= half_samples; --i) {
        guint8 *src = info.data + i * bytes_per_frame;
        if (src == dest)
          break;

        for (gint j = 0; j < bytes_per_frame; ++j) {
          val = *dest;
          *dest++ = *src;
          *src++ = val;
        }
      }
    }
    gst_buffer_unmap (buffer, &info);

    GST_BUFFER_DURATION (buffer) =
        gst_util_uint64_scale (samples, GST_SECOND, rate);
    /* temporary store samples here, to avoid having to recalculate this */
    GST_BUFFER_OFFSET (buffer) = (gint64) samples;
  }

  return buffer;
}

/* process the last samples, in a later stage we should make sure no more
 * samples are sent out here as strictly necessary, because soundtouch could
 * append zero samples, which could disturb looping.  */
static GstFlowReturn
gst_pitch_flush_buffer (GstPitch * pitch, gboolean send)
{
  GstBuffer *buffer;
  GstPitchPrivate *priv = GST_PITCH_GET_PRIVATE (pitch);

  GST_OBJECT_LOCK (pitch);
  if (priv->st->numUnprocessedSamples () != 0) {
    GST_DEBUG_OBJECT (pitch, "flushing SoundTouch buffer");
    priv->st->flush ();
  }
  GST_OBJECT_UNLOCK (pitch);

  if (!send)
    return GST_FLOW_OK;

  buffer = gst_pitch_prepare_buffer (pitch);

  if (!buffer)
    return GST_FLOW_OK;

  return gst_pitch_forward_buffer (pitch, buffer);
}

static gboolean
gst_pitch_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstPitch *pitch;
  gboolean res;

  pitch = GST_PITCH (parent);

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
      guint32 seqnum;

      GstPitchPrivate *priv = GST_PITCH_GET_PRIVATE (pitch);

      GST_OBJECT_LOCK (pitch);
      stream_time_ratio = priv->stream_time_ratio;
      GST_OBJECT_UNLOCK (pitch);

      gst_event_parse_seek (event, &rate, &format, &flags,
          &cur_type, &cur, &stop_type, &stop);

      seqnum = gst_event_get_seqnum (event);

      gst_event_unref (event);

      if (format == GST_FORMAT_TIME || format == GST_FORMAT_DEFAULT) {
        cur = (gint64) (cur * stream_time_ratio);
        if (stop != -1)
          stop = (gint64) (stop * stream_time_ratio);

        event = gst_event_new_seek (rate, format, flags,
            cur_type, cur, stop_type, stop);
        gst_event_set_seqnum (event, seqnum);
        res = gst_pad_event_default (pad, parent, event);
      } else {
        GST_WARNING_OBJECT (pitch,
            "Seeking only supported in TIME or DEFAULT format");
        res = FALSE;
      }
      break;
    }
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }
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
  sample_size = pitch->info.bpf;
  samplerate = pitch->info.rate;
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

static gboolean
gst_pitch_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstPitch *pitch;
  gboolean res = FALSE;
  gfloat stream_time_ratio;
  guint64 next_buffer_offset;
  GstClockTime next_buffer_time;
  GstClockTimeDiff min_latency, max_latency;

  pitch = GST_PITCH (parent);
  GstPitchPrivate *priv = GST_PITCH_GET_PRIVATE (pitch);

  GST_LOG ("%s query", GST_QUERY_TYPE_NAME (query));

  GST_OBJECT_LOCK (pitch);
  stream_time_ratio = priv->stream_time_ratio;
  next_buffer_time = pitch->next_buffer_time;
  next_buffer_offset = pitch->next_buffer_offset;
  min_latency = pitch->min_latency;
  max_latency = pitch->max_latency;
  GST_OBJECT_UNLOCK (pitch);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:{
      GstFormat format;
      gint64 duration;

      if (!gst_pad_query_default (pad, parent, query)) {
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
              GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

          min += min_latency;
          if (max != GST_CLOCK_TIME_NONE)
            max += max_latency;

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
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}

/* this function returns FALSE if not enough data is known to transform the
 * segment into proper downstream values.  If the function does return false
 * the segment should be stalled until enough information is available.
 * If the function returns TRUE, event will be replaced by the new downstream
 * compatible event.
 */
static gboolean
gst_pitch_process_segment (GstPitch * pitch, GstEvent ** event)
{
  gint seqnum;
  gdouble output_rate, segment_applied_rate;
  gfloat stream_time_ratio;
  GstSegment seg;

  g_return_val_if_fail (event, FALSE);
  g_return_val_if_fail (*event, FALSE);

  GstPitchPrivate *priv = GST_PITCH_GET_PRIVATE (pitch);

  GST_OBJECT_LOCK (pitch);
  stream_time_ratio = priv->stream_time_ratio;
  output_rate = pitch->output_rate;
  GST_OBJECT_UNLOCK (pitch);

  gst_event_copy_segment (*event, &seg);

  if (seg.format != GST_FORMAT_TIME && seg.format != GST_FORMAT_DEFAULT) {
    GST_WARNING_OBJECT (pitch,
        "Only NEWSEGMENT in TIME or DEFAULT format supported, sending"
        "open ended NEWSEGMENT in TIME format.");
    seg.format = GST_FORMAT_TIME;
    seg.start = 0;
    seg.stop = -1;
    seg.time = 0;
    seg.position = 0;
    seg.duration = -1;
  }

  /* Figure out how much of the incoming 'rate' we'll apply ourselves */
  segment_applied_rate = seg.rate / output_rate;
  /* update the output rate variables */
  seg.rate = (segment_applied_rate < 0.0) ? -output_rate : output_rate;
  seg.applied_rate *= ABS (segment_applied_rate);

  GST_LOG_OBJECT (pitch->sinkpad, "in segment %" GST_SEGMENT_FORMAT, &seg);

  stream_time_ratio = pitch->tempo * pitch->rate * ABS (segment_applied_rate);

  if (stream_time_ratio == 0) {
    GST_LOG_OBJECT (pitch->sinkpad, "stream_time_ratio is zero");
    return FALSE;
  }

  /* Update the playback rate */
  GST_OBJECT_LOCK (pitch);
  pitch->segment_applied_rate = segment_applied_rate;
  priv->stream_time_ratio = stream_time_ratio;
  priv->st->setTempo (pitch->tempo * ABS (pitch->segment_applied_rate));
  GST_OBJECT_UNLOCK (pitch);

  seg.start = (gint64) (seg.start / stream_time_ratio);
  if (seg.stop != (guint64) - 1)
    seg.stop = (gint64) (seg.stop / stream_time_ratio);
  seg.time = (gint64) (seg.time / stream_time_ratio);
  seg.position = (gint64) (seg.position / stream_time_ratio);
  if (seg.duration != (guint64) - 1)
    seg.duration = (gint64) (seg.duration / stream_time_ratio);

  GST_LOG_OBJECT (pitch->sinkpad, "out segment %" GST_SEGMENT_FORMAT, &seg);

  seqnum = gst_event_get_seqnum (*event);
  gst_event_unref (*event);
  *event = gst_event_new_segment (&seg);
  gst_event_set_seqnum (*event, seqnum);

  return TRUE;
}

static gboolean
gst_pitch_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res = TRUE;
  GstPitch *pitch;

  pitch = GST_PITCH (parent);
  GstPitchPrivate *priv = GST_PITCH_GET_PRIVATE (pitch);

  GST_LOG_OBJECT (pad, "received %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_pitch_flush_buffer (pitch, FALSE);
      GST_OBJECT_LOCK (pitch);
      priv->st->clear ();
      pitch->next_buffer_offset = 0;
      pitch->next_buffer_time = GST_CLOCK_TIME_NONE;
      pitch->min_latency = pitch->max_latency = 0;
      GST_OBJECT_UNLOCK (pitch);
      break;
    case GST_EVENT_EOS:
      gst_pitch_flush_buffer (pitch, TRUE);
      GST_OBJECT_LOCK (pitch);
      priv->st->clear ();
      pitch->min_latency = pitch->max_latency = 0;
      GST_OBJECT_UNLOCK (pitch);
      break;
    case GST_EVENT_SEGMENT:
      if (!gst_pitch_process_segment (pitch, &event)) {
        GST_LOG_OBJECT (pad, "not enough data known, stalling segment");

        GST_OBJECT_LOCK (pitch);
        if (priv->pending_segment)
          gst_event_unref (priv->pending_segment);
        priv->pending_segment = event;
        GST_OBJECT_UNLOCK (pitch);

        event = NULL;
      }

      GST_OBJECT_LOCK (pitch);
      priv->st->clear ();
      pitch->min_latency = pitch->max_latency = 0;
      GST_OBJECT_UNLOCK (pitch);
      break;
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      res = gst_pitch_setcaps (pitch, caps);
      if (!res) {
        gst_event_unref (event);
        goto done;
      }
    }
    default:
      break;
  }

  /* and forward it */
  if (event)
    res = gst_pad_event_default (pad, parent, event);

done:
  return res;
}

static void
gst_pitch_update_latency (GstPitch * pitch, GstClockTime timestamp)
{
  GstClockTimeDiff current_latency, min_latency, max_latency;
  GstPitchPrivate *priv = GST_PITCH_GET_PRIVATE (pitch);
  gboolean post_new_latency_message = FALSE;

  GST_OBJECT_LOCK (pitch);
  current_latency =
      (GstClockTimeDiff) (timestamp / priv->stream_time_ratio) -
      pitch->next_buffer_time;

  min_latency = MIN (pitch->min_latency, ABS (current_latency));
  max_latency = MAX (pitch->max_latency, ABS (current_latency));

  if (pitch->min_latency != min_latency || pitch->max_latency != max_latency) {
    pitch->min_latency = min_latency;
    pitch->max_latency = max_latency;
    post_new_latency_message = TRUE;
  }
  GST_OBJECT_UNLOCK (pitch);

  if (post_new_latency_message) {
    /* FIXME: what about the LATENCY event? It only has
     * one latency value, should it be current, min or max?
     * Should it include upstream latencies?
     */
    gst_element_post_message (GST_ELEMENT (pitch),
        gst_message_new_latency (GST_OBJECT (pitch)));
  }
}

static GstFlowReturn
gst_pitch_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstPitch *pitch = GST_PITCH (parent);
  GstPitchPrivate *priv = GST_PITCH_GET_PRIVATE (pitch);

  GstClockTime timestamp = GST_BUFFER_PTS (buffer);

  GST_OBJECT_LOCK (pitch);

  gint bytes_per_frame = pitch->info.bpf;
  GST_LOG_OBJECT (pitch, "incoming buffer (%d samples) %" GST_TIME_FORMAT,
      (gint) (gst_buffer_get_size (buffer) / bytes_per_frame),
      GST_TIME_ARGS (timestamp));

  gboolean reverse_playback = (pitch->segment_applied_rate < 0);
  if (reverse_playback) {
    // In reverse playback we need to take the end of the buffer as the
    // reference timestamp as the output buffer produced by libsoundtouch may
    // have a duration different from this current input buffer.
    timestamp += GST_BUFFER_DURATION (buffer);
  }

  gfloat stream_time_ratio = priv->stream_time_ratio;
  GstClockTime next_buffer_time = pitch->next_buffer_time;

  if (!GST_CLOCK_TIME_IS_VALID (next_buffer_time)) {
    next_buffer_time = timestamp / stream_time_ratio;
    pitch->next_buffer_time = next_buffer_time;
    pitch->next_buffer_offset =
        gst_util_uint64_scale_int (next_buffer_time, pitch->info.rate,
        GST_SECOND);
  }

  GstEvent *pending_segment = priv->pending_segment;
  priv->pending_segment = NULL;

  GST_OBJECT_UNLOCK (pitch);

  gst_object_sync_values (GST_OBJECT (pitch), next_buffer_time);

  /* push the received samples on the soundtouch buffer */
  if (pending_segment) {
    GST_LOG_OBJECT (pitch, "processing stalled segment");

    if (!gst_pitch_process_segment (pitch, &pending_segment)) {
      gst_buffer_unref (buffer);
      gst_event_unref (pending_segment);
      return GST_FLOW_ERROR;
    }

    if (!gst_pad_event_default (pitch->sinkpad, parent, pending_segment)) {
      gst_buffer_unref (buffer);
      return GST_FLOW_ERROR;
    }
  }

  GstMapInfo info;
  gst_buffer_map (buffer, &info, GST_MAP_READ);
  guint8 *data = info.data;

  gboolean free_data = FALSE;
  if (reverse_playback && (gint) info.size > bytes_per_frame) {
    // If playing backwards, we need to inverse the order of all samples before
    // sending them to the libsoundtouch library.
    data = (guint8 *) g_malloc (info.size);
    free_data = TRUE;

    guint8 *p = data;
    for (gint i = info.size - bytes_per_frame; i >= 0; i -= bytes_per_frame) {
      memcpy (p, info.data + i, bytes_per_frame);
      p += bytes_per_frame;
    }
  }

  GST_OBJECT_LOCK (pitch);
  priv->st->putSamples ((soundtouch::SAMPLETYPE *) data,
      info.size / bytes_per_frame);
  gboolean has_output_samples_available = !priv->st->isEmpty ();
  GST_OBJECT_UNLOCK (pitch);

  if (free_data)
    g_free (data);

  gst_buffer_unmap (buffer, &info);
  gst_buffer_unref (buffer);

  /* Calculate latency */
  gst_pitch_update_latency (pitch, timestamp);

  /* and try to extract some samples from the soundtouch buffer */
  if (has_output_samples_available) {
    GstBuffer *out_buffer = gst_pitch_prepare_buffer (pitch);
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
  GstPitchPrivate *priv = GST_PITCH_GET_PRIVATE (pitch);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_OBJECT_LOCK (pitch);
      pitch->next_buffer_time = GST_CLOCK_TIME_NONE;
      pitch->next_buffer_offset = 0;
      priv->st->clear ();
      pitch->min_latency = pitch->max_latency = 0;
      GST_OBJECT_UNLOCK (pitch);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_OBJECT_LOCK (pitch);
      if (priv->pending_segment) {
        gst_event_unref (priv->pending_segment);
        priv->pending_segment = NULL;
      }
      GST_OBJECT_UNLOCK (pitch);
      break;
    default:
      break;
  }

  return ret;
}
