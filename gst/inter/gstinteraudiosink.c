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
 * The interaudiosink element is an audio sink element.  It is used
 * in connection with a interaudiosrc element in a different pipeline,
 * similar to intervideosink and intervideosrc.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v audiotestsrc ! queue ! interaudiosink
 * ]|
 *
 * The interaudiosink element cannot be used effectively with gst-launch,
 * as it requires a second pipeline in the application to receive the
 * audio.
 * See the gstintertest.c example in the gst-plugins-bad source code for
 * more details.
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
static void gst_inter_audio_sink_finalize (GObject * object);

static void gst_inter_audio_sink_get_times (GstBaseSink * sink,
    GstBuffer * buffer, GstClockTime * start, GstClockTime * end);
static gboolean gst_inter_audio_sink_start (GstBaseSink * sink);
static gboolean gst_inter_audio_sink_stop (GstBaseSink * sink);
static GstFlowReturn gst_inter_audio_sink_render (GstBaseSink * sink,
    GstBuffer * buffer);

enum
{
  PROP_0,
  PROP_CHANNEL
};

/* pad templates */

static GstStaticPadTemplate gst_inter_audio_sink_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, format = (string) " GST_AUDIO_NE (S16) ", "
        "rate = (int) 48000, channels = (int) 2")
    );


/* class initialization */


G_DEFINE_TYPE (GstInterAudioSink, gst_inter_audio_sink, GST_TYPE_BASE_SINK);

static void
gst_inter_audio_sink_class_init (GstInterAudioSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *base_sink_class = GST_BASE_SINK_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_inter_audio_sink_debug_category,
      "interaudiosink", 0, "debug category for interaudiosink element");
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_inter_audio_sink_sink_template));

  gst_element_class_set_static_metadata (element_class,
      "Internal audio sink",
      "Sink/Audio",
      "Virtual audio sink for internal process communication",
      "David Schleef <ds@schleef.org>");

  gobject_class->set_property = gst_inter_audio_sink_set_property;
  gobject_class->get_property = gst_inter_audio_sink_get_property;
  gobject_class->finalize = gst_inter_audio_sink_finalize;
  base_sink_class->get_times =
      GST_DEBUG_FUNCPTR (gst_inter_audio_sink_get_times);
  base_sink_class->start = GST_DEBUG_FUNCPTR (gst_inter_audio_sink_start);
  base_sink_class->stop = GST_DEBUG_FUNCPTR (gst_inter_audio_sink_stop);
  base_sink_class->render = GST_DEBUG_FUNCPTR (gst_inter_audio_sink_render);

  g_object_class_install_property (gobject_class, PROP_CHANNEL,
      g_param_spec_string ("channel", "Channel",
          "Channel name to match inter src and sink elements",
          "default", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_inter_audio_sink_init (GstInterAudioSink * interaudiosink)
{
  interaudiosink->channel = g_strdup ("default");
}

void
gst_inter_audio_sink_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstInterAudioSink *interaudiosink = GST_INTER_AUDIO_SINK (object);

  switch (property_id) {
    case PROP_CHANNEL:
      g_free (interaudiosink->channel);
      interaudiosink->channel = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_inter_audio_sink_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstInterAudioSink *interaudiosink = GST_INTER_AUDIO_SINK (object);

  switch (property_id) {
    case PROP_CHANNEL:
      g_value_set_string (value, interaudiosink->channel);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_inter_audio_sink_finalize (GObject * object)
{
  GstInterAudioSink *interaudiosink = GST_INTER_AUDIO_SINK (object);

  /* clean up object here */
  g_free (interaudiosink->channel);

  G_OBJECT_CLASS (gst_inter_audio_sink_parent_class)->finalize (object);
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
  GstInterAudioSink *interaudiosink = GST_INTER_AUDIO_SINK (sink);

  GST_DEBUG ("start");

  interaudiosink->surface = gst_inter_surface_get (interaudiosink->channel);

  return TRUE;
}

static gboolean
gst_inter_audio_sink_stop (GstBaseSink * sink)
{
  GstInterAudioSink *interaudiosink = GST_INTER_AUDIO_SINK (sink);

  GST_DEBUG ("stop");

  g_mutex_lock (&interaudiosink->surface->mutex);
  gst_adapter_clear (interaudiosink->surface->audio_adapter);
  g_mutex_unlock (&interaudiosink->surface->mutex);

  gst_inter_surface_unref (interaudiosink->surface);
  interaudiosink->surface = NULL;

  return TRUE;
}

static GstFlowReturn
gst_inter_audio_sink_render (GstBaseSink * sink, GstBuffer * buffer)
{
  GstInterAudioSink *interaudiosink = GST_INTER_AUDIO_SINK (sink);
  int n;

  GST_DEBUG ("render %" G_GSIZE_FORMAT, gst_buffer_get_size (buffer));

  g_mutex_lock (&interaudiosink->surface->mutex);
  n = gst_adapter_available (interaudiosink->surface->audio_adapter) / 4;
#define SIZE 1600
  if (n > (SIZE * 3)) {
    int n_chunks = (n / (SIZE / 2)) - 4;
    GST_WARNING ("flushing %d samples", n_chunks * 800);
    gst_adapter_flush (interaudiosink->surface->audio_adapter,
        n_chunks * (SIZE / 2) * 4);
  }
  gst_adapter_push (interaudiosink->surface->audio_adapter,
      gst_buffer_ref (buffer));
  g_mutex_unlock (&interaudiosink->surface->mutex);

  return GST_FLOW_OK;
}
