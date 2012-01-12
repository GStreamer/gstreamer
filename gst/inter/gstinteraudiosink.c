/* GStreamer
 * Copyright (C) 2011 David A. Schleef <ds@schleef.org>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gstinteraudiosink
 *
 * The interaudiosink element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v fakesrc ! interaudiosink ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include <gst/audio/audio.h>
#include "gstinteraudiosink.h"
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_inter_audio_sink_debug_category);
#define GST_CAT_DEFAULT gst_inter_audio_sink_debug_category

/* prototypes */


static void gst_inter_audio_sink_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_inter_audio_sink_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_inter_audio_sink_dispose (GObject * object);
static void gst_inter_audio_sink_finalize (GObject * object);

static GstCaps *gst_inter_audio_sink_get_caps (GstBaseSink * sink);
static gboolean gst_inter_audio_sink_set_caps (GstBaseSink * sink,
    GstCaps * caps);
static GstFlowReturn gst_inter_audio_sink_buffer_alloc (GstBaseSink * sink,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);
static void gst_inter_audio_sink_get_times (GstBaseSink * sink,
    GstBuffer * buffer, GstClockTime * start, GstClockTime * end);
static gboolean gst_inter_audio_sink_start (GstBaseSink * sink);
static gboolean gst_inter_audio_sink_stop (GstBaseSink * sink);
static gboolean gst_inter_audio_sink_unlock (GstBaseSink * sink);
static gboolean gst_inter_audio_sink_event (GstBaseSink * sink,
    GstEvent * event);
static GstFlowReturn gst_inter_audio_sink_preroll (GstBaseSink * sink,
    GstBuffer * buffer);
static GstFlowReturn gst_inter_audio_sink_render (GstBaseSink * sink,
    GstBuffer * buffer);
static GstStateChangeReturn gst_inter_audio_sink_async_play (GstBaseSink *
    sink);
static gboolean gst_inter_audio_sink_activate_pull (GstBaseSink * sink,
    gboolean active);
static gboolean gst_inter_audio_sink_unlock_stop (GstBaseSink * sink);

enum
{
  PROP_0
};

/* pad templates */

static GstStaticPadTemplate gst_inter_audio_sink_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) true, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]")
    );


/* class initialization */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_inter_audio_sink_debug_category, "interaudiosink", 0, \
      "debug category for interaudiosink element");

GST_BOILERPLATE_FULL (GstInterAudioSink, gst_inter_audio_sink, GstBaseSink,
    GST_TYPE_BASE_SINK, DEBUG_INIT);

static void
gst_inter_audio_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_inter_audio_sink_sink_template);

  gst_element_class_set_details_simple (element_class, "FIXME Long name",
      "Generic", "FIXME Description", "FIXME <fixme@example.com>");
}

static void
gst_inter_audio_sink_class_init (GstInterAudioSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *base_sink_class = GST_BASE_SINK_CLASS (klass);

  gobject_class->set_property = gst_inter_audio_sink_set_property;
  gobject_class->get_property = gst_inter_audio_sink_get_property;
  gobject_class->dispose = gst_inter_audio_sink_dispose;
  gobject_class->finalize = gst_inter_audio_sink_finalize;
  base_sink_class->get_caps = GST_DEBUG_FUNCPTR (gst_inter_audio_sink_get_caps);
  base_sink_class->set_caps = GST_DEBUG_FUNCPTR (gst_inter_audio_sink_set_caps);
  if (0)
    base_sink_class->buffer_alloc =
        GST_DEBUG_FUNCPTR (gst_inter_audio_sink_buffer_alloc);
  base_sink_class->get_times =
      GST_DEBUG_FUNCPTR (gst_inter_audio_sink_get_times);
  base_sink_class->start = GST_DEBUG_FUNCPTR (gst_inter_audio_sink_start);
  base_sink_class->stop = GST_DEBUG_FUNCPTR (gst_inter_audio_sink_stop);
  base_sink_class->unlock = GST_DEBUG_FUNCPTR (gst_inter_audio_sink_unlock);
  if (0)
    base_sink_class->event = GST_DEBUG_FUNCPTR (gst_inter_audio_sink_event);
  //if (0)
  base_sink_class->preroll = GST_DEBUG_FUNCPTR (gst_inter_audio_sink_preroll);
  base_sink_class->render = GST_DEBUG_FUNCPTR (gst_inter_audio_sink_render);
  if (0)
    base_sink_class->async_play =
        GST_DEBUG_FUNCPTR (gst_inter_audio_sink_async_play);
  if (0)
    base_sink_class->activate_pull =
        GST_DEBUG_FUNCPTR (gst_inter_audio_sink_activate_pull);
  base_sink_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_inter_audio_sink_unlock_stop);

}

static void
gst_inter_audio_sink_init (GstInterAudioSink * interaudiosink,
    GstInterAudioSinkClass * interaudiosink_class)
{
  interaudiosink->surface = gst_inter_surface_get ("default");
}

void
gst_inter_audio_sink_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  /* GstInterAudioSink *interaudiosink = GST_INTER_AUDIO_SINK (object); */

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_inter_audio_sink_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  /* GstInterAudioSink *interaudiosink = GST_INTER_AUDIO_SINK (object); */

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_inter_audio_sink_dispose (GObject * object)
{
  /* GstInterAudioSink *interaudiosink = GST_INTER_AUDIO_SINK (object); */

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_inter_audio_sink_finalize (GObject * object)
{
  /* GstInterAudioSink *interaudiosink = GST_INTER_AUDIO_SINK (object); */

  /* clean up object here */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}



static GstCaps *
gst_inter_audio_sink_get_caps (GstBaseSink * sink)
{

  return NULL;
}

static gboolean
gst_inter_audio_sink_set_caps (GstBaseSink * sink, GstCaps * caps)
{

  return TRUE;
}

static GstFlowReturn
gst_inter_audio_sink_buffer_alloc (GstBaseSink * sink, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf)
{

  return GST_FLOW_ERROR;
}

static void
gst_inter_audio_sink_get_times (GstBaseSink * sink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  GstInterAudioSink *interaudiosink = GST_INTER_AUDIO_SINK (sink);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer)) {
    *start = GST_BUFFER_TIMESTAMP (buffer);
    if (GST_BUFFER_DURATION_IS_VALID (buffer)) {
      *end = *start + GST_BUFFER_DURATION (buffer);
    } else {
      if (interaudiosink->fps_n > 0) {
        *end = *start +
            gst_util_uint64_scale_int (GST_SECOND, interaudiosink->fps_d,
            interaudiosink->fps_n);
      }
    }
  }


}

static gboolean
gst_inter_audio_sink_start (GstBaseSink * sink)
{

  return TRUE;
}

static gboolean
gst_inter_audio_sink_stop (GstBaseSink * sink)
{
  GstInterAudioSink *interaudiosink = GST_INTER_AUDIO_SINK (sink);

  GST_DEBUG ("stop");

  g_mutex_lock (interaudiosink->surface->mutex);
  gst_adapter_clear (interaudiosink->surface->audio_adapter);
  g_mutex_unlock (interaudiosink->surface->mutex);

  return TRUE;
}

static gboolean
gst_inter_audio_sink_unlock (GstBaseSink * sink)
{

  return TRUE;
}

static gboolean
gst_inter_audio_sink_event (GstBaseSink * sink, GstEvent * event)
{

  return TRUE;
}

static GstFlowReturn
gst_inter_audio_sink_preroll (GstBaseSink * sink, GstBuffer * buffer)
{

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_inter_audio_sink_render (GstBaseSink * sink, GstBuffer * buffer)
{
  GstInterAudioSink *interaudiosink = GST_INTER_AUDIO_SINK (sink);
  int n;

  GST_DEBUG ("render %d", GST_BUFFER_SIZE (buffer));

  g_mutex_lock (interaudiosink->surface->mutex);
  n = gst_adapter_available (interaudiosink->surface->audio_adapter) / 4;
  if (n > (800 * 2 * 2)) {
    GST_INFO ("flushing 800 samples");
    gst_adapter_flush (interaudiosink->surface->audio_adapter, 800 * 4);
    n -= 800;
  }
  gst_adapter_push (interaudiosink->surface->audio_adapter,
      gst_buffer_ref (buffer));
  g_mutex_unlock (interaudiosink->surface->mutex);

  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_inter_audio_sink_async_play (GstBaseSink * sink)
{

  return GST_STATE_CHANGE_SUCCESS;
}

static gboolean
gst_inter_audio_sink_activate_pull (GstBaseSink * sink, gboolean active)
{

  return TRUE;
}

static gboolean
gst_inter_audio_sink_unlock_stop (GstBaseSink * sink)
{

  return TRUE;
}
