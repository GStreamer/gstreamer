/*
 *  Copyright 2009 Nokia Corporation <multimedia@maemo.org>
 *            2006 Zeeshan Ali <zeeshan.ali@nokia.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 *
 * |[
 * gst-launch videotestsrc ! fpsdisplaysink
 * gst-launch videotestsrc ! fpsdisplaysink text-overlay=false
 * gst-launch filesrc location=video.avi ! decodebin2 name=d ! queue ! fpsdisplaysink d. ! queue ! fakesink sync=true
 * ]|
 */
/* FIXME:
 * - do we need to proxy xoverlay stuff?
 * - can we avoid the 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fpsdisplaysink.h"
#include <gst/interfaces/xoverlay.h>

#define FPS_DISPLAY_INTERVAL_MS 500     /* 500 ms */
#define DEFAULT_FONT "20"

static GstElementDetails fps_display_sink_details = {
  "Wrapper for xvimagesink's frame-rate display",
  "Sink/Video",
  "Shows the current frame-rate and drop-rate of the xvimagesink on the xvimagesink's video output",
  "Zeeshan Ali <zeeshan.ali@nokia.com>, Stefan Kost <stefan.kost@nokia.com>"
};

/* generic templates */
static GstStaticPadTemplate fps_display_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (fps_display_sink_debug);
#define GST_CAT_DEFAULT fps_display_sink_debug

struct _FPSDisplaySinkPrivate
{
  GstElement *text_overlay;
  GstElement *xvimagesink;

  guint64 frames_rendered, last_frames_rendered;
  guint64 frames_dropped, last_frames_dropped;
  GstClockTime last_ts;
  GstClockTime next_ts;

  guint timeout_id;

  gchar *font;
  gboolean sync;
  gboolean use_text_overlay;
};

enum
{
  ARG_0,
  ARG_SYNC,
  ARG_TEXT_OVERLAY,
  /* FILL ME */
};

static GstBinClass *parent_class = NULL;

static GstStateChangeReturn fps_display_sink_change_state (GstElement * element,
    GstStateChange transition);
static void fps_display_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void fps_display_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void fps_display_sink_dispose (GObject * object);

static void
fps_display_sink_class_init (FPSDisplaySinkClass * klass)
{
  GObjectClass *gobject_klass = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_klass = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_klass->set_property = fps_display_sink_set_property;
  gobject_klass->get_property = fps_display_sink_get_property;
  gobject_klass->dispose = fps_display_sink_dispose;

  g_object_class_install_property (gobject_klass, ARG_SYNC,
      g_param_spec_boolean ("sync",
          "Sync", "Sync on the clock", FALSE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_klass, ARG_TEXT_OVERLAY,
      g_param_spec_boolean ("text-overlay",
          "text-overlay",
          "Wether to use text-overlay", TRUE, G_PARAM_READWRITE));

  gstelement_klass->change_state = fps_display_sink_change_state;

  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&fps_display_sink_template));

  gst_element_class_set_details (gstelement_klass, &fps_display_sink_details);

  GST_DEBUG_CATEGORY_INIT (fps_display_sink_debug, "fpsdisplaysink", 0,
      "FPS Display Sink");

  g_type_class_add_private (klass, sizeof (FPSDisplaySinkPrivate));
}

static void
fps_display_sink_base_init (gpointer klass)
{
}

#if 0
static void
on_xvimagesink_caps_negotiated (GstPad * pad, GParamSpec * arg,
    gpointer user_data)
{
  GstCaps *caps = gst_pad_get_negotiated_caps (pad);

  g_print ("caps negotiated on xvimagesink = %s\n", gst_caps_to_string (caps));
  gst_caps_unref (caps);
}
#endif

static gboolean
on_xvimagesink_data_flow (GstPad * pad, GstMiniObject * mini_obj,
    gpointer user_data)
{
  FPSDisplaySink *self = FPS_DISPLAY_SINK (user_data);

#if 0
  if (GST_IS_BUFFER (mini_obj)) {
    GstBuffer *buf = GST_BUFFER_CAST (mini_obj);

    if (GST_CLOCK_TIME_IS_VALID (self->priv->next_ts)) {
      if (GST_BUFFER_TIMESTAMP (buf) <= self->priv->next_ts) {
        self->priv->frames_rendered++;
      } else {
        g_debug ("dropping frame : ts %" GST_TIME_FORMAT " < expected_ts %"
            GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
            GST_TIME_ARGS (self->priv->next_ts));
        self->priv->frames_dropped++;
      }
    } else {
      self->priv->frames_rendered++;
    }
  } else
#endif
  if (GST_IS_EVENT (mini_obj)) {
    GstEvent *ev = GST_EVENT_CAST (mini_obj);

    if (GST_EVENT_TYPE (ev) == GST_EVENT_QOS) {
      GstClockTimeDiff diff;
      GstClockTime ts;

      gst_event_parse_qos (ev, NULL, &diff, &ts);
      self->priv->next_ts = ts + diff;
      if (diff <= 0.0) {
        self->priv->frames_rendered++;
      } else {
        self->priv->frames_dropped++;
      }
    }
  }
  return TRUE;
}

static void
fps_display_sink_init (FPSDisplaySink * self, FPSDisplaySinkClass * g_class)
{
  GstPad *sink_pad, *target_pad;

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, FPS_TYPE_DISPLAY_SINK,
      FPSDisplaySinkPrivate);

  self->priv->font = DEFAULT_FONT;
  self->priv->sync = FALSE;
  self->priv->use_text_overlay = TRUE;

  self->priv->xvimagesink =
      gst_element_factory_make ("xvimagesink", "fps_display_xvimagesink");
  self->priv->text_overlay =
      gst_element_factory_make ("textoverlay", "text-overlay");

  if (!self->priv->xvimagesink || !self->priv->text_overlay) {
    g_debug ("element could not be created");
    return;
  }

  g_object_set (self->priv->xvimagesink, "sync", self->priv->sync, NULL);
  g_object_set (self->priv->text_overlay, "font-desc", self->priv->font, NULL);

  /* take a ref before bin takes the ownership */
  gst_object_ref (self->priv->text_overlay);
  gst_object_ref (self->priv->xvimagesink);

  gst_bin_add_many (GST_BIN (self),
      self->priv->text_overlay, self->priv->xvimagesink, NULL);

  if (!gst_element_link_many (self->priv->text_overlay, self->priv->xvimagesink,
          NULL)) {
    g_error ("Could not link elements");
  }

  target_pad =
      gst_element_get_static_pad (self->priv->text_overlay, "video_sink");
  sink_pad = gst_ghost_pad_new ("sink_pad", target_pad);
  gst_object_unref (target_pad);
  gst_element_add_pad (GST_ELEMENT (self), sink_pad);

  sink_pad = gst_element_get_static_pad (self->priv->xvimagesink, "sink");
#if 0
  g_signal_connect (sink_pad, "notify::caps",
      G_CALLBACK (on_xvimagesink_caps_negotiated), NULL);
#endif
  gst_pad_add_data_probe (sink_pad, G_CALLBACK (on_xvimagesink_data_flow),
      (gpointer) self);

  gst_object_unref (sink_pad);
}

static gboolean
display_current_fps (gpointer data)
{
  FPSDisplaySink *self = FPS_DISPLAY_SINK (data);
  gint64 current_ts;
#if 0
  guint64 frames_rendered, frames_dropped;
#endif
  GstQuery *query;

#if 0
  query = gst_query_new_qos ();
  gst_element_query (self->priv->xvimagesink, query);
  gst_query_parse_qos (query, &frames_rendered, &frames_dropped);
  gst_query_unref (query);
#endif

  query = gst_query_new_position (GST_FORMAT_TIME);
  gst_element_query (self->priv->xvimagesink, query);
  gst_query_parse_position (query, NULL, &current_ts);
  gst_query_unref (query);

  if (GST_CLOCK_TIME_IS_VALID (self->priv->last_ts)) {
    gdouble rr, dr, average_fps;
    gchar fps_message[256];
    gdouble time_diff =
        (gdouble) (current_ts - self->priv->last_ts) / GST_SECOND;

    rr = (gdouble) (self->priv->frames_rendered -
        self->priv->last_frames_rendered) / time_diff;
    dr = (gdouble) (self->priv->frames_dropped -
        self->priv->last_frames_dropped) / time_diff;

    average_fps =
        self->priv->frames_rendered / (gdouble) (current_ts / GST_SECOND);

    if (dr == 0.0) {
      sprintf (fps_message, "current: %.2f\naverage: %.2f", rr, average_fps);
    } else {
      sprintf (fps_message, "fps: %.2f\ndrop rate: %.2f", rr, dr);
    }

    if (self->priv->use_text_overlay) {
      g_object_set (self->priv->text_overlay, "text", fps_message, NULL);
    } else {
      g_print ("%s\n", fps_message);
    }
  }

  self->priv->last_frames_rendered = self->priv->frames_rendered;
  self->priv->last_frames_dropped = self->priv->frames_dropped;
  self->priv->last_ts = current_ts;

  return TRUE;
}

static void
fps_display_sink_start_the_show (FPSDisplaySink * self)
{
  /* Init counters */
  self->priv->next_ts = GST_CLOCK_TIME_NONE;
  self->priv->last_ts = GST_CLOCK_TIME_NONE;
  self->priv->frames_rendered = G_GUINT64_CONSTANT (0);
  self->priv->frames_dropped = G_GUINT64_CONSTANT (0);

  /* Set a timeout for the fps display */
  self->priv->timeout_id =
      g_timeout_add (FPS_DISPLAY_INTERVAL_MS,
      display_current_fps, (gpointer) self);
}

static void
fps_display_sink_stop_the_show (FPSDisplaySink * self)
{
  /* remove the timeout */
  if (self->priv->timeout_id) {
    g_source_remove (self->priv->timeout_id);
    self->priv->timeout_id = 0;
  }
}

static void
fps_display_sink_dispose (GObject * object)
{
  FPSDisplaySink *self = FPS_DISPLAY_SINK (object);

  if (self->priv->xvimagesink) {
    gst_object_unref (self->priv->xvimagesink);
    self->priv->xvimagesink = NULL;
  }

  if (self->priv->text_overlay) {
    gst_object_unref (self->priv->text_overlay);
    self->priv->text_overlay = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
fps_display_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  FPSDisplaySink *self = FPS_DISPLAY_SINK (object);

  switch (prop_id) {
    case ARG_SYNC:
      self->priv->sync = g_value_get_boolean (value);
      g_object_set (self->priv->xvimagesink, "sync", self->priv->sync, NULL);
      break;
    case ARG_TEXT_OVERLAY:
      self->priv->use_text_overlay = g_value_get_boolean (value);

      if (!(self->priv->use_text_overlay)) {
        g_print ("text-overlay set to false\n");
        g_object_set (self->priv->text_overlay, "text", "", "silent", TRUE,
            NULL);
      } else {
        g_print ("text-overlay set to true\n");
        g_object_set (self->priv->text_overlay, "silent", FALSE, NULL);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fps_display_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  FPSDisplaySink *self = FPS_DISPLAY_SINK (object);

  switch (prop_id) {
    case ARG_SYNC:
      g_value_set_boolean (value, self->priv->sync);
      break;
    case ARG_TEXT_OVERLAY:
      g_value_set_boolean (value, self->priv->use_text_overlay);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
fps_display_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  FPSDisplaySink *self = FPS_DISPLAY_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      fps_display_sink_start_the_show (self);
      break;
    default:
      break;
  }

  ret = GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS, change_state,
      (element, transition), GST_STATE_CHANGE_SUCCESS);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      fps_display_sink_stop_the_show (self);
      break;
    default:
      break;
  }

  return ret;
}

GType
fps_display_sink_get_type (void)
{
  static GType fps_display_sink_type = 0;

  if (!fps_display_sink_type) {
    static const GTypeInfo fps_display_sink_info = {
      sizeof (FPSDisplaySinkClass),
      fps_display_sink_base_init,
      NULL,
      (GClassInitFunc) fps_display_sink_class_init,
      NULL,
      NULL,
      sizeof (FPSDisplaySink),
      0,
      (GInstanceInitFunc) fps_display_sink_init,
    };

    fps_display_sink_type = g_type_register_static (GST_TYPE_BIN,
        "FPSDisplaySink", &fps_display_sink_info, 0);
  }

  return fps_display_sink_type;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "fpsdisplaysink",
      GST_RANK_NONE, FPS_TYPE_DISPLAY_SINK);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "fpsdisplaysink",
    "A custom sink that show the current FPS of the sink on the video screen",
    plugin_init, VERSION, "LGPL", "Nokia Real-time Communications", "")
