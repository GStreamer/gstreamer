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

#include "gstbaseaudiosink.h"

GST_DEBUG_CATEGORY_STATIC (gst_baseaudiosink_debug);
#define GST_CAT_DEFAULT gst_baseaudiosink_debug

/* BaseAudioSink signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_BUFFER	-1
#define DEFAULT_LATENCY	-1
enum
{
  PROP_0,
  PROP_BUFFER,
  PROP_LATENCY,
};

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_baseaudiosink_debug, "baseaudiosink", 0, "baseaudiosink element");

GST_BOILERPLATE_FULL (GstBaseAudioSink, gst_baseaudiosink, GstBaseSink,
    GST_TYPE_BASESINK, _do_init);

static void gst_baseaudiosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_baseaudiosink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementStateReturn gst_baseaudiosink_change_state (GstElement *
    element);

static GstFlowReturn gst_baseaudiosink_preroll (GstBaseSink * bsink,
    GstBuffer * buffer);
static GstFlowReturn gst_baseaudiosink_render (GstBaseSink * bsink,
    GstBuffer * buffer);
static void gst_baseaudiosink_event (GstBaseSink * bsink, GstEvent * event);
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

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BUFFER,
      g_param_spec_uint64 ("buffer", "Buffer",
          "Size of audio buffer in nanoseconds (-1 = default)",
          0, G_MAXUINT64, DEFAULT_BUFFER, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LATENCY,
      g_param_spec_uint64 ("latency", "Latency",
          "Audio latency in nanoseconds (-1 = default)",
          0, G_MAXUINT64, DEFAULT_LATENCY, G_PARAM_READWRITE));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_baseaudiosink_change_state);

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
  baseaudiosink->buffer = DEFAULT_BUFFER;
  baseaudiosink->latency = DEFAULT_LATENCY;
}

static void
gst_baseaudiosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseAudioSink *sink;

  sink = GST_BASEAUDIOSINK (object);

  switch (prop_id) {
    case PROP_BUFFER:
      break;
    case PROP_LATENCY:
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
    case PROP_BUFFER:
      break;
    case PROP_LATENCY:
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

  gst_caps_replace (&spec->caps, caps);
  spec->buffersize = sink->buffer;
  spec->latency = sink->latency;

  spec->segtotal = 0x7fff;
  spec->segsize = 0x2048;

  gst_ringbuffer_release (sink->ringbuffer);
  gst_ringbuffer_acquire (sink->ringbuffer, spec);

  return TRUE;
}

static void
gst_baseaudiosink_get_times (GstBaseSink * bsink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  *start = GST_CLOCK_TIME_NONE;
  *end = GST_CLOCK_TIME_NONE;
}

static void
gst_baseaudiosink_event (GstBaseSink * bsink, GstEvent * event)
{
}

static GstFlowReturn
gst_baseaudiosink_preroll (GstBaseSink * bsink, GstBuffer * buffer)
{
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_baseaudiosink_render (GstBaseSink * bsink, GstBuffer * buf)
{
  GstBaseAudioSink *sink = GST_BASEAUDIOSINK (bsink);

  gst_ringbuffer_commit (sink->ringbuffer, 0,
      GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

  return GST_FLOW_OK;
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
gst_baseaudiosink_callback (GstRingBuffer * rbuf, guint advance, gpointer data)
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
      gst_ringbuffer_stop (sink->ringbuffer);
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_ringbuffer_release (sink->ringbuffer);
      gst_object_unref (GST_OBJECT (sink->ringbuffer));
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}
