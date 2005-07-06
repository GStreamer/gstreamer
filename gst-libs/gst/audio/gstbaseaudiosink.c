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

GST_DEBUG_CATEGORY_STATIC (gst_baseaudiosink_debug);
#define GST_CAT_DEFAULT gst_baseaudiosink_debug

/* BaseAudioSink signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_BUFFER_TIME	500 * GST_USECOND
#define DEFAULT_LATENCY_TIME	10 * GST_USECOND
enum
{
  PROP_0,
  PROP_BUFFER_TIME,
  PROP_LATENCY_TIME,
};

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_baseaudiosink_debug, "baseaudiosink", 0, "baseaudiosink element");

GST_BOILERPLATE_FULL (GstBaseAudioSink, gst_baseaudiosink, GstBaseSink,
    GST_TYPE_BASESINK, _do_init);

static void gst_baseaudiosink_dispose (GObject * object);

static void gst_baseaudiosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_baseaudiosink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementStateReturn gst_baseaudiosink_change_state (GstElement *
    element);

static GstClock *gst_baseaudiosink_get_clock (GstElement * elem);
static GstClockTime gst_baseaudiosink_get_time (GstClock * clock,
    GstBaseAudioSink * sink);

static GstFlowReturn gst_baseaudiosink_preroll (GstBaseSink * bsink,
    GstBuffer * buffer);
static GstFlowReturn gst_baseaudiosink_render (GstBaseSink * bsink,
    GstBuffer * buffer);
static gboolean gst_baseaudiosink_event (GstBaseSink * bsink, GstEvent * event);
static void gst_baseaudiosink_get_times (GstBaseSink * bsink,
    GstBuffer * buffer, GstClockTime * start, GstClockTime * end);
static gboolean gst_baseaudiosink_setcaps (GstBaseSink * bsink, GstCaps * caps);

//static guint gst_baseaudiosink_signals[LAST_SIGNAL] = { 0 };

static void
gst_baseaudiosink_base_init (gpointer g_class)
{
}

static void
gst_baseaudiosink_class_init (GstBaseAudioSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_baseaudiosink_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_baseaudiosink_get_property);
  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_baseaudiosink_dispose);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BUFFER_TIME,
      g_param_spec_int64 ("buffer-time", "Buffer Time",
          "Size of audio buffer in milliseconds (-1 = default)",
          -1, G_MAXINT64, DEFAULT_BUFFER_TIME, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LATENCY_TIME,
      g_param_spec_int64 ("latency-time", "Latency Time",
          "Audio latency in milliseconds (-1 = default)",
          -1, G_MAXINT64, DEFAULT_LATENCY_TIME, G_PARAM_READWRITE));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_baseaudiosink_change_state);
  gstelement_class->get_clock = GST_DEBUG_FUNCPTR (gst_baseaudiosink_get_clock);

  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_baseaudiosink_event);
  gstbasesink_class->preroll = GST_DEBUG_FUNCPTR (gst_baseaudiosink_preroll);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_baseaudiosink_render);
  gstbasesink_class->get_times =
      GST_DEBUG_FUNCPTR (gst_baseaudiosink_get_times);
  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_baseaudiosink_setcaps);
}

static void
gst_baseaudiosink_init (GstBaseAudioSink * baseaudiosink)
{
  baseaudiosink->buffer_time = DEFAULT_BUFFER_TIME;
  baseaudiosink->latency_time = DEFAULT_LATENCY_TIME;

  baseaudiosink->clock = gst_audio_clock_new ("clock",
      (GstAudioClockGetTimeFunc) gst_baseaudiosink_get_time, baseaudiosink);
}

static void
gst_baseaudiosink_dispose (GObject * object)
{
  GstBaseAudioSink *sink;

  sink = GST_BASEAUDIOSINK (object);

  if (sink->clock)
    gst_object_unref (sink->clock);
  sink->clock = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstClock *
gst_baseaudiosink_get_clock (GstElement * elem)
{
  GstBaseAudioSink *sink;

  sink = GST_BASEAUDIOSINK (elem);

  return GST_CLOCK (gst_object_ref (sink->clock));
}

static GstClockTime
gst_baseaudiosink_get_time (GstClock * clock, GstBaseAudioSink * sink)
{
  guint64 samples;
  GstClockTime result;

  if (sink->ringbuffer == NULL || sink->ringbuffer->spec.rate == 0)
    return 0;

  samples = gst_ringbuffer_samples_done (sink->ringbuffer);

  result = samples * GST_SECOND / sink->ringbuffer->spec.rate;
  result += GST_ELEMENT (sink)->base_time;

  return result;
}

static void
gst_baseaudiosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseAudioSink *sink;

  sink = GST_BASEAUDIOSINK (object);

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
gst_baseaudiosink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstBaseAudioSink *sink;

  sink = GST_BASEAUDIOSINK (object);

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
gst_baseaudiosink_setcaps (GstBaseSink * bsink, GstCaps * caps)
{
  GstBaseAudioSink *sink = GST_BASEAUDIOSINK (bsink);
  GstRingBufferSpec *spec;

  spec = &sink->ringbuffer->spec;

  GST_DEBUG ("release old ringbuffer");

  /* release old ringbuffer */
  gst_ringbuffer_release (sink->ringbuffer);

  GST_DEBUG ("parse caps");

  spec->buffer_time = sink->buffer_time;
  spec->latency_time = sink->latency_time;

  /* parse new caps */
  if (!gst_ringbuffer_parse_caps (spec, caps))
    goto parse_error;

  gst_ringbuffer_debug_spec_buff (spec);

  GST_DEBUG ("acquire new ringbuffer");

  if (!gst_ringbuffer_acquire (sink->ringbuffer, spec))
    goto acquire_error;

  /* calculate actual latency and buffer times */
  spec->latency_time =
      spec->segsize * GST_MSECOND / (spec->rate * spec->bytes_per_sample);
  spec->buffer_time =
      spec->segtotal * spec->segsize * GST_MSECOND / (spec->rate *
      spec->bytes_per_sample);

  gst_ringbuffer_debug_spec_buff (spec);

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
gst_baseaudiosink_get_times (GstBaseSink * bsink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  /* ne need to sync to a clock here, we schedule the samples based
   * on our own clock for the moment. FIXME, implement this when
   * we are not using our own clock */
  *start = GST_CLOCK_TIME_NONE;
  *end = GST_CLOCK_TIME_NONE;
}

static gboolean
gst_baseaudiosink_event (GstBaseSink * bsink, GstEvent * event)
{
  GstBaseAudioSink *sink = GST_BASEAUDIOSINK (bsink);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH:
      if (GST_EVENT_FLUSH_DONE (event)) {
      } else {
        gst_ringbuffer_pause (sink->ringbuffer);
      }
      break;
    case GST_EVENT_DISCONTINUOUS:
    {
      gint64 time, sample;

      if (gst_event_discont_get_value (event, GST_FORMAT_DEFAULT, &sample,
              NULL))
        goto have_value;
      if (gst_event_discont_get_value (event, GST_FORMAT_TIME, &time, NULL)) {
        sample = time * sink->ringbuffer->spec.rate / GST_SECOND;
        goto have_value;
      }
      g_warning ("discont without valid timestamp");
      sample = 0;

    have_value:
      GST_DEBUG ("discont now at %lld", sample);
      gst_ringbuffer_set_sample (sink->ringbuffer, sample);
      break;
    }
    default:
      break;
  }
  return TRUE;
}

static GstFlowReturn
gst_baseaudiosink_preroll (GstBaseSink * bsink, GstBuffer * buffer)
{
  GstBaseAudioSink *sink = GST_BASEAUDIOSINK (bsink);

  if (!gst_ringbuffer_is_acquired (sink->ringbuffer))
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
gst_baseaudiosink_render (GstBaseSink * bsink, GstBuffer * buf)
{
  guint64 offset;
  GstBaseAudioSink *sink = GST_BASEAUDIOSINK (bsink);

  offset = GST_BUFFER_OFFSET (buf);

  GST_DEBUG ("in offset %llu, time %" GST_TIME_FORMAT, offset,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));
  if (!gst_ringbuffer_is_acquired (sink->ringbuffer))
    goto wrong_state;

  gst_ringbuffer_commit (sink->ringbuffer, offset,
      GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

  return GST_FLOW_OK;

wrong_state:
  {
    GST_DEBUG ("ringbuffer in wrong state");
    GST_ELEMENT_ERROR (sink, RESOURCE, NOT_FOUND,
        ("sink not negotiated."), (NULL));
    return GST_FLOW_ERROR;
  }
}

GstRingBuffer *
gst_baseaudiosink_create_ringbuffer (GstBaseAudioSink * sink)
{
  GstBaseAudioSinkClass *bclass;
  GstRingBuffer *buffer = NULL;

  bclass = GST_BASEAUDIOSINK_GET_CLASS (sink);
  if (bclass->create_ringbuffer)
    buffer = bclass->create_ringbuffer (sink);

  if (buffer) {
    gst_object_set_parent (GST_OBJECT (buffer), GST_OBJECT (sink));
  }

  return buffer;
}

void
gst_baseaudiosink_callback (GstRingBuffer * rbuf, guint8 * data, guint len,
    gpointer user_data)
{
  //GstBaseAudioSink *sink = GST_BASEAUDIOSINK (data);
}

static GstElementStateReturn
gst_baseaudiosink_change_state (GstElement * element)
{
  GstElementStateReturn ret = GST_STATE_SUCCESS;
  GstBaseAudioSink *sink = GST_BASEAUDIOSINK (element);
  GstElementState transition = GST_STATE_TRANSITION (element);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      sink->ringbuffer = gst_baseaudiosink_create_ringbuffer (sink);
      gst_ringbuffer_set_callback (sink->ringbuffer, gst_baseaudiosink_callback,
          sink);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);

  switch (transition) {
    case GST_STATE_PLAYING_TO_PAUSED:
      gst_ringbuffer_pause (sink->ringbuffer);
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_ringbuffer_stop (sink->ringbuffer);
      gst_ringbuffer_release (sink->ringbuffer);
      gst_object_unref (sink->ringbuffer);
      sink->ringbuffer = NULL;
      gst_pad_set_caps (GST_BASESINK_PAD (sink), NULL);
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}
