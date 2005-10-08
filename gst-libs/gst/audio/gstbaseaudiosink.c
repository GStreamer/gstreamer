/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gstbaseaudiosink.c: 
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

#include <string.h>

#include "gstbaseaudiosink.h"

GST_DEBUG_CATEGORY_STATIC (gst_base_audio_sink_debug);
#define GST_CAT_DEFAULT gst_base_audio_sink_debug

/* BaseAudioSink signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

/* we tollerate a 10th of a second diff before we start resyncing. This
 * should be enough to compensate for various rounding errors in the timestamp
 * and sample offset position. */
#define DIFF_TOLERANCE	10

#define DEFAULT_BUFFER_TIME	500 * GST_USECOND
#define DEFAULT_LATENCY_TIME	10 * GST_USECOND
enum
{
  PROP_0,
  PROP_BUFFER_TIME,
  PROP_LATENCY_TIME,
};

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_base_audio_sink_debug, "baseaudiosink", 0, "baseaudiosink element");

GST_BOILERPLATE_FULL (GstBaseAudioSink, gst_base_audio_sink, GstBaseSink,
    GST_TYPE_BASE_SINK, _do_init);

static void gst_base_audio_sink_dispose (GObject * object);

static void gst_base_audio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_base_audio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_base_audio_sink_change_state (GstElement *
    element, GstStateChange transition);

static GstClock *gst_base_audio_sink_provide_clock (GstElement * elem);
static GstClockTime gst_base_audio_sink_get_time (GstClock * clock,
    GstBaseAudioSink * sink);
static void gst_base_audio_sink_callback (GstRingBuffer * rbuf, guint8 * data,
    guint len, gpointer user_data);

static GstFlowReturn gst_base_audio_sink_preroll (GstBaseSink * bsink,
    GstBuffer * buffer);
static GstFlowReturn gst_base_audio_sink_render (GstBaseSink * bsink,
    GstBuffer * buffer);
static gboolean gst_base_audio_sink_event (GstBaseSink * bsink,
    GstEvent * event);
static void gst_base_audio_sink_get_times (GstBaseSink * bsink,
    GstBuffer * buffer, GstClockTime * start, GstClockTime * end);
static gboolean gst_base_audio_sink_setcaps (GstBaseSink * bsink,
    GstCaps * caps);

//static guint gst_base_audio_sink_signals[LAST_SIGNAL] = { 0 };

static void
gst_base_audio_sink_base_init (gpointer g_class)
{
}

static void
gst_base_audio_sink_class_init (GstBaseAudioSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_base_audio_sink_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_base_audio_sink_get_property);
  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_base_audio_sink_dispose);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BUFFER_TIME,
      g_param_spec_int64 ("buffer-time", "Buffer Time",
          "Size of audio buffer in milliseconds (-1 = default)",
          -1, G_MAXINT64, DEFAULT_BUFFER_TIME, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LATENCY_TIME,
      g_param_spec_int64 ("latency-time", "Latency Time",
          "Audio latency in milliseconds (-1 = default)",
          -1, G_MAXINT64, DEFAULT_LATENCY_TIME, G_PARAM_READWRITE));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_base_audio_sink_change_state);
  gstelement_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_base_audio_sink_provide_clock);

  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_base_audio_sink_event);
  gstbasesink_class->preroll = GST_DEBUG_FUNCPTR (gst_base_audio_sink_preroll);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_base_audio_sink_render);
  gstbasesink_class->get_times =
      GST_DEBUG_FUNCPTR (gst_base_audio_sink_get_times);
  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_base_audio_sink_setcaps);
}

static void
gst_base_audio_sink_init (GstBaseAudioSink * baseaudiosink,
    GstBaseAudioSinkClass * g_class)
{
  baseaudiosink->buffer_time = DEFAULT_BUFFER_TIME;
  baseaudiosink->latency_time = DEFAULT_LATENCY_TIME;


  baseaudiosink->clock = gst_audio_clock_new ("clock",
      (GstAudioClockGetTimeFunc) gst_base_audio_sink_get_time, baseaudiosink);

}

static void
gst_base_audio_sink_dispose (GObject * object)
{
  GstBaseAudioSink *sink;

  sink = GST_BASE_AUDIO_SINK (object);

  if (sink->clock)
    gst_object_unref (sink->clock);
  sink->clock = NULL;

  if (sink->ringbuffer)
    gst_object_unref (sink->ringbuffer);
  sink->ringbuffer = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstClock *
gst_base_audio_sink_provide_clock (GstElement * elem)
{
  GstBaseAudioSink *sink;

  sink = GST_BASE_AUDIO_SINK (elem);

  return GST_CLOCK (gst_object_ref (sink->clock));
}

static GstClockTime
gst_base_audio_sink_get_time (GstClock * clock, GstBaseAudioSink * sink)
{
  guint64 samples;
  GstClockTime result;

  if (sink->ringbuffer == NULL || sink->ringbuffer->spec.rate == 0)
    return GST_CLOCK_TIME_NONE;

  /* our processed samples are always increasing */
  samples = gst_ring_buffer_samples_done (sink->ringbuffer);

  result = samples * GST_SECOND / sink->ringbuffer->spec.rate;

  return result;
}

static void
gst_base_audio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseAudioSink *sink;

  sink = GST_BASE_AUDIO_SINK (object);

  switch (prop_id) {
    case PROP_BUFFER_TIME:
      sink->buffer_time = g_value_get_int64 (value);
      break;
    case PROP_LATENCY_TIME:
      sink->latency_time = g_value_get_int64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_base_audio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseAudioSink *sink;

  sink = GST_BASE_AUDIO_SINK (object);

  switch (prop_id) {
    case PROP_BUFFER_TIME:
      g_value_set_int64 (value, sink->buffer_time);
      break;
    case PROP_LATENCY_TIME:
      g_value_set_int64 (value, sink->latency_time);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_base_audio_sink_setcaps (GstBaseSink * bsink, GstCaps * caps)
{
  GstBaseAudioSink *sink = GST_BASE_AUDIO_SINK (bsink);
  GstRingBufferSpec *spec;

  spec = &sink->ringbuffer->spec;

  GST_DEBUG ("release old ringbuffer");

  /* release old ringbuffer */
  gst_ring_buffer_release (sink->ringbuffer);

  GST_DEBUG ("parse caps");

  spec->buffer_time = sink->buffer_time;
  spec->latency_time = sink->latency_time;

  /* parse new caps */
  if (!gst_ring_buffer_parse_caps (spec, caps))
    goto parse_error;

  gst_ring_buffer_debug_spec_buff (spec);

  GST_DEBUG ("acquire new ringbuffer");

  if (!gst_ring_buffer_acquire (sink->ringbuffer, spec))
    goto acquire_error;

  /* calculate actual latency and buffer times */
  spec->latency_time =
      spec->segsize * GST_MSECOND / (spec->rate * spec->bytes_per_sample);
  spec->buffer_time =
      spec->segtotal * spec->segsize * GST_MSECOND / (spec->rate *
      spec->bytes_per_sample);

  gst_ring_buffer_debug_spec_buff (spec);

  return TRUE;

  /* ERRORS */
parse_error:
  {
    GST_DEBUG ("could not parse caps");
    return FALSE;
  }
acquire_error:
  {
    GST_DEBUG ("could not acquire ringbuffer");
    return FALSE;
  }
}

static void
gst_base_audio_sink_get_times (GstBaseSink * bsink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  /* our clock sync is a bit too much for the base class to handle so
   * we implement it ourselves. */
  *start = GST_CLOCK_TIME_NONE;
  *end = GST_CLOCK_TIME_NONE;
}

static gboolean
gst_base_audio_sink_event (GstBaseSink * bsink, GstEvent * event)
{
  GstBaseAudioSink *sink = GST_BASE_AUDIO_SINK (bsink);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      gst_ring_buffer_pause (sink->ringbuffer);
      gst_ring_buffer_clear_all (sink->ringbuffer);
      break;
    case GST_EVENT_FLUSH_STOP:
      /* always resync on sample after a flush */
      sink->next_sample = -1;
      gst_ring_buffer_clear_all (sink->ringbuffer);
      break;
    case GST_EVENT_EOS:
      gst_ring_buffer_start (sink->ringbuffer);
      break;
    default:
      break;
  }
  return TRUE;
}

static GstFlowReturn
gst_base_audio_sink_preroll (GstBaseSink * bsink, GstBuffer * buffer)
{
  GstBaseAudioSink *sink = GST_BASE_AUDIO_SINK (bsink);

  if (!gst_ring_buffer_is_acquired (sink->ringbuffer))
    goto wrong_state;

  /* we don't really do anything when prerolling. We could make a
   * property to play this buffer to have some sort of scrubbing
   * support. */
  return GST_FLOW_OK;

wrong_state:
  {
    GST_DEBUG ("ringbuffer in wrong state");
    GST_ELEMENT_ERROR (sink, RESOURCE, NOT_FOUND,
        ("sink not negotiated."), (NULL));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_base_audio_sink_render (GstBaseSink * bsink, GstBuffer * buf)
{
  guint64 render_offset, in_offset;
  GstClockTime time, render_time;
  GstClockTimeDiff render_diff;
  GstBaseAudioSink *sink = GST_BASE_AUDIO_SINK (bsink);
  gint64 diff;
  guint8 *data;
  guint size;

  /* can't do anything when we don't have the device */
  if (!gst_ring_buffer_is_acquired (sink->ringbuffer))
    goto wrong_state;

  in_offset = GST_BUFFER_OFFSET (buf);
  time = GST_BUFFER_TIMESTAMP (buf);
  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  GST_DEBUG ("time %" GST_TIME_FORMAT ", offset %llu, start %" GST_TIME_FORMAT,
      GST_TIME_ARGS (time), in_offset, GST_TIME_ARGS (bsink->segment_start));

  render_diff = time - bsink->segment_start;
  /* samples should be rendered based on their timestamp. All samples
   * arriving before the segment_start are to be thrown away */
  /* FIXME, for now we drop the sample completely, we should
   * in fact clip the sample. Same for the segment_stop, actually. */
  if (render_diff < 0)
    return GST_FLOW_OK;

  /* bring buffer timestamp to stream time */
  render_time = render_diff;
  /* add base time to get absolute clock time */
  render_time += gst_element_get_base_time (GST_ELEMENT (bsink));
  /* and bring the time to the offset in the buffer */
  render_offset = render_time * sink->ringbuffer->spec.rate / GST_SECOND;

  /* roundoff errors in timestamp conversion */
  if (sink->next_sample != -1)
    diff = ABS ((gint64) render_offset - (gint64) sink->next_sample);
  else
    diff = sink->ringbuffer->spec.rate;

  GST_DEBUG ("render time %" GST_TIME_FORMAT
      ", render offset %llu, diff %lld, size %lu", GST_TIME_ARGS (render_time),
      render_offset, diff, size);

  /* we tollerate a 10th of a second diff before we start resyncing. This
   * should be enough to compensate for various rounding errors in the timestamp
   * and sample offset position. */
  if (diff < sink->ringbuffer->spec.rate / DIFF_TOLERANCE) {
    GST_DEBUG ("align with prev sample, %" G_GINT64_FORMAT " < %lu", diff,
        sink->ringbuffer->spec.rate / DIFF_TOLERANCE);
    /* just align with previous sample then */
    render_offset = sink->next_sample;
  } else {
    GST_DEBUG ("resync");
  }
  gst_ring_buffer_commit (sink->ringbuffer, render_offset, data, size);

  /* the next sample should be current sample and its length */
  sink->next_sample =
      render_offset + size / sink->ringbuffer->spec.bytes_per_sample;

  return GST_FLOW_OK;

wrong_state:
  {
    GST_DEBUG ("ringbuffer not negotiated");
    GST_ELEMENT_ERROR (sink, RESOURCE, NOT_FOUND,
        ("sink not negotiated."), ("sink not negotiated."));
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

GstRingBuffer *
gst_base_audio_sink_create_ringbuffer (GstBaseAudioSink * sink)
{
  GstBaseAudioSinkClass *bclass;
  GstRingBuffer *buffer = NULL;

  bclass = GST_BASE_AUDIO_SINK_GET_CLASS (sink);
  if (bclass->create_ringbuffer)
    buffer = bclass->create_ringbuffer (sink);

  if (buffer)
    gst_object_set_parent (GST_OBJECT (buffer), GST_OBJECT (sink));

  return buffer;
}

static void
gst_base_audio_sink_callback (GstRingBuffer * rbuf, guint8 * data, guint len,
    gpointer user_data)
{
  //GstBaseAudioSink *sink = GST_BASE_AUDIO_SINK (data);
}

static GstStateChangeReturn
gst_base_audio_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstBaseAudioSink *sink = GST_BASE_AUDIO_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (sink->ringbuffer == NULL) {
        sink->ringbuffer = gst_base_audio_sink_create_ringbuffer (sink);
        gst_ring_buffer_set_callback (sink->ringbuffer,
            gst_base_audio_sink_callback, sink);
      }
      if (!gst_ring_buffer_open_device (sink->ringbuffer))
        return GST_STATE_CHANGE_FAILURE;
      sink->next_sample = 0;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      gst_ring_buffer_pause (sink->ringbuffer);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_ring_buffer_stop (sink->ringbuffer);
      gst_pad_set_caps (GST_BASE_SINK_PAD (sink), NULL);
      gst_ring_buffer_release (sink->ringbuffer);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_ring_buffer_close_device (sink->ringbuffer);
      break;
    default:
      break;
  }

  return ret;
}
