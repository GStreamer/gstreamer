/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:gtkgstsink
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstgtksink.h"

GST_DEBUG_CATEGORY (gst_debug_gtk_sink);
#define GST_CAT_DEFAULT gst_debug_gtk_sink

static void gst_gtk_sink_finalize (GObject * object);
static void gst_gtk_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * param_spec);
static void gst_gtk_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * param_spec);

static gboolean gst_gtk_sink_stop (GstBaseSink * bsink);

static gboolean gst_gtk_sink_query (GstBaseSink * bsink, GstQuery * query);

static GstStateChangeReturn
gst_gtk_sink_change_state (GstElement * element, GstStateChange transition);

static void gst_gtk_sink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end);
static gboolean gst_gtk_sink_set_caps (GstBaseSink * bsink, GstCaps * caps);
static GstFlowReturn gst_gtk_sink_show_frame (GstVideoSink * bsink,
    GstBuffer * buf);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define FORMATS "{ BGRx, BGRA }"
#else
#define FORMATS "{ xRGB, ARGB }"
#endif

static GstStaticPadTemplate gst_gtk_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (FORMATS))
    );

#define DEFAULT_FORCE_ASPECT_RATIO  TRUE
#define DEFAULT_PAR_N               0
#define DEFAULT_PAR_D               1
#define DEFAULT_IGNORE_ALPHA        TRUE

enum
{
  PROP_0,
  PROP_WIDGET,
  PROP_FORCE_ASPECT_RATIO,
  PROP_PIXEL_ASPECT_RATIO,
  PROP_IGNORE_ALPHA,
};

enum
{
  SIGNAL_0,
  LAST_SIGNAL
};

#define gst_gtk_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGtkSink, gst_gtk_sink,
    GST_TYPE_VIDEO_SINK, GST_DEBUG_CATEGORY_INIT (gst_debug_gtk_sink, "gtksink",
        0, "Gtk Video Sink"));

static void
gst_gtk_sink_class_init (GstGtkSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstVideoSinkClass *gstvideosink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstvideosink_class = (GstVideoSinkClass *) klass;

  gobject_class->set_property = gst_gtk_sink_set_property;
  gobject_class->get_property = gst_gtk_sink_get_property;

  gst_element_class_set_metadata (gstelement_class, "Gtk Video Sink",
      "Sink/Video", "A video sink that renders to a GtkWidget",
      "Matthew Waters <matthew@centricular.com>");

  g_object_class_install_property (gobject_class, PROP_WIDGET,
      g_param_spec_object ("widget", "Gtk Widget",
          "The GtkWidget to place in the widget heirachy",
          GTK_TYPE_WIDGET, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio",
          "Force aspect ratio",
          "When enabled, scaling will respect original aspect ratio",
          DEFAULT_FORCE_ASPECT_RATIO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PIXEL_ASPECT_RATIO,
      gst_param_spec_fraction ("pixel-aspect-ratio", "Pixel Aspect Ratio",
          "The pixel aspect ratio of the device", DEFAULT_PAR_N, DEFAULT_PAR_D,
          G_MAXINT, 1, 1, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_IGNORE_ALPHA,
      g_param_spec_boolean ("ignore-alpha", "Ignore Alpha",
          "When enabled, alpha will be ignored and converted to black",
          DEFAULT_IGNORE_ALPHA, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_gtk_sink_template));

  gobject_class->finalize = gst_gtk_sink_finalize;

  gstelement_class->change_state = gst_gtk_sink_change_state;
  gstbasesink_class->query = gst_gtk_sink_query;
  gstbasesink_class->set_caps = gst_gtk_sink_set_caps;
  gstbasesink_class->get_times = gst_gtk_sink_get_times;
  gstbasesink_class->stop = gst_gtk_sink_stop;

  gstvideosink_class->show_frame = gst_gtk_sink_show_frame;
}

static void
gst_gtk_sink_init (GstGtkSink * gtk_sink)
{
  gtk_sink->force_aspect_ratio = DEFAULT_FORCE_ASPECT_RATIO;
  gtk_sink->par_n = DEFAULT_PAR_N;
  gtk_sink->par_d = DEFAULT_PAR_D;
  gtk_sink->ignore_alpha = DEFAULT_IGNORE_ALPHA;
}

static void
gst_gtk_sink_finalize (GObject * object)
{
  GstGtkSink *gtk_sink = GST_GTK_SINK (object);;

  g_object_unref (gtk_sink->widget);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GtkGstWidget *
gst_gtk_sink_get_widget (GstGtkSink * gtk_sink)
{
  if (gtk_sink->widget != NULL)
    return gtk_sink->widget;

  /* Ensure GTK is initialized, this has no side effect if it was already
   * initialized. Also, we do that lazily, so the application can be first */
  if (!gtk_init_check (NULL, NULL)) {
    GST_ERROR_OBJECT (gtk_sink, "Could not ensure GTK initialization.");
    return NULL;
  }

  gtk_sink->widget = (GtkGstWidget *) gtk_gst_widget_new ();
  gtk_sink->bind_aspect_ratio =
      g_object_bind_property (gtk_sink, "force-aspect-ratio", gtk_sink->widget,
      "force-aspect-ratio", G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_sink->bind_pixel_aspect_ratio =
      g_object_bind_property (gtk_sink, "pixel-aspect-ratio", gtk_sink->widget,
      "pixel-aspect-ratio", G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  gtk_sink->bind_ignore_alpha =
      g_object_bind_property (gtk_sink, "ignore-alpha", gtk_sink->widget,
      "ignore-alpha", G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

  /* Take the floating ref, other wise the destruction of the container will
   * make this widget disapear possibly before we are done. */
  gst_object_ref_sink (gtk_sink->widget);

  return gtk_sink->widget;
}

static void
gst_gtk_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGtkSink *gtk_sink = GST_GTK_SINK (object);

  switch (prop_id) {
    case PROP_WIDGET:
      g_value_set_object (value, gst_gtk_sink_get_widget (gtk_sink));
      break;
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, gtk_sink->force_aspect_ratio);
      break;
    case PROP_PIXEL_ASPECT_RATIO:
      gst_value_set_fraction (value, gtk_sink->par_n, gtk_sink->par_d);
      break;
    case PROP_IGNORE_ALPHA:
      g_value_set_boolean (value, gtk_sink->ignore_alpha);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gtk_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGtkSink *gtk_sink = GST_GTK_SINK (object);

  switch (prop_id) {
    case PROP_FORCE_ASPECT_RATIO:
      gtk_sink->force_aspect_ratio = g_value_get_boolean (value);
      break;
    case PROP_PIXEL_ASPECT_RATIO:
      gtk_sink->par_n = gst_value_get_fraction_numerator (value);
      gtk_sink->par_d = gst_value_get_fraction_denominator (value);
      break;
    case PROP_IGNORE_ALPHA:
      gtk_sink->ignore_alpha = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gtk_sink_query (GstBaseSink * bsink, GstQuery * query)
{
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    default:
      res = GST_BASE_SINK_CLASS (parent_class)->query (bsink, query);
      break;
  }

  return res;
}

static gboolean
gst_gtk_sink_stop (GstBaseSink * bsink)
{
  return TRUE;
}

static GstStateChangeReturn
gst_gtk_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstGtkSink *gtk_sink = GST_GTK_SINK (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG ("changing state: %s => %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (gst_gtk_sink_get_widget (gtk_sink) == NULL)
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gtk_gst_widget_set_buffer (gtk_sink->widget, NULL);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_gtk_sink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end)
{
  GstGtkSink *gtk_sink;

  gtk_sink = GST_GTK_SINK (bsink);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    *start = GST_BUFFER_TIMESTAMP (buf);
    if (GST_BUFFER_DURATION_IS_VALID (buf))
      *end = *start + GST_BUFFER_DURATION (buf);
    else {
      if (GST_VIDEO_INFO_FPS_N (&gtk_sink->v_info) > 0) {
        *end = *start +
            gst_util_uint64_scale_int (GST_SECOND,
            GST_VIDEO_INFO_FPS_D (&gtk_sink->v_info),
            GST_VIDEO_INFO_FPS_N (&gtk_sink->v_info));
      }
    }
  }
}

gboolean
gst_gtk_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstGtkSink *gtk_sink = GST_GTK_SINK (bsink);

  GST_DEBUG ("set caps with %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&gtk_sink->v_info, caps))
    return FALSE;

  if (!gtk_gst_widget_set_caps (gtk_sink->widget, caps))
    return FALSE;

  return TRUE;
}

static GstFlowReturn
gst_gtk_sink_show_frame (GstVideoSink * vsink, GstBuffer * buf)
{
  GstGtkSink *gtk_sink;

  GST_TRACE ("rendering buffer:%p", buf);

  gtk_sink = GST_GTK_SINK (vsink);

  if (gtk_sink->widget)
    gtk_gst_widget_set_buffer (gtk_sink->widget, buf);

  return GST_FLOW_OK;
}
