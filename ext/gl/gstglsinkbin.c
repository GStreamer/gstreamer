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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/videooverlay.h>
#include <gst/video/navigation.h>

#include "gstglsinkbin.h"

GST_DEBUG_CATEGORY (gst_debug_gl_sink_bin);
#define GST_CAT_DEFAULT gst_debug_gl_sink_bin

static void gst_gl_sink_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * param_spec);
static void gst_gl_sink_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * param_spec);

static GstStateChangeReturn gst_gl_sink_bin_change_state (GstElement * element,
    GstStateChange transition);

static void gst_gl_sink_bin_video_overlay_init (gpointer g_iface,
    gpointer g_iface_data);
static void gst_gl_sink_bin_navigation_interface_init (gpointer g_iface,
    gpointer g_iface_data);

static GstStaticPadTemplate gst_gl_sink_bin_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(ANY)"));

enum
{
  PROP_0,
  PROP_FORCE_ASPECT_RATIO,
  PROP_SINK,
};

enum
{
  SIGNAL_0,
  SIGNAL_CREATE_ELEMENT,
  SIGNAL_LAST,
};

static guint gst_gl_sink_bin_signals[SIGNAL_LAST] = { 0, };

#define gst_gl_sink_bin_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGLSinkBin, gst_gl_sink_bin,
    GST_TYPE_BIN, G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_OVERLAY,
        gst_gl_sink_bin_video_overlay_init);
    G_IMPLEMENT_INTERFACE (GST_TYPE_NAVIGATION,
        gst_gl_sink_bin_navigation_interface_init);
    GST_DEBUG_CATEGORY_INIT (gst_debug_gl_sink_bin, "glimagesink", 0,
        "OpenGL Video Sink Bin"));

static void
gst_gl_sink_bin_class_init (GstGLSinkBinClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  element_class->change_state = gst_gl_sink_bin_change_state;

  gobject_class->set_property = gst_gl_sink_bin_set_property;
  gobject_class->get_property = gst_gl_sink_bin_get_property;

  g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio",
          "Force aspect ratio",
          "When enabled, scaling will respect original aspect ratio", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SINK,
      g_param_spec_object ("sink",
          "GL sink element",
          "The GL sink chain to use",
          GST_TYPE_ELEMENT,
          GST_PARAM_MUTABLE_READY | G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS));

  /**
   * GstGLSinkBin::create-element:
   * @object: the #GstGLSinkBin
   *
   * Will be emitted when we need the processing element/s that this bin will use
   *
   * Returns: a new #GstElement
   */
  gst_gl_sink_bin_signals[SIGNAL_CREATE_ELEMENT] =
      g_signal_new ("create-element", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      GST_TYPE_ELEMENT, 0);

  gst_element_class_set_metadata (element_class,
      "GL Sink Bin", "Sink/Video",
      "Infrastructure to process GL textures",
      "Matthew Waters <matthew@centricular.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_sink_bin_template));
}

static void
gst_gl_sink_bin_init (GstGLSinkBin * self)
{
  gboolean res = TRUE;
  GstPad *pad;

  self->upload = gst_element_factory_make ("glupload", NULL);
  self->convert = gst_element_factory_make ("glcolorconvert", NULL);

  res &= gst_bin_add (GST_BIN (self), self->upload);
  res &= gst_bin_add (GST_BIN (self), self->convert);

  res &= gst_element_link_pads (self->upload, "src", self->convert, "sink");

  pad = gst_element_get_static_pad (self->upload, "sink");
  if (!pad) {
    res = FALSE;
  } else {
    GST_DEBUG_OBJECT (self, "setting target sink pad %" GST_PTR_FORMAT, pad);
    self->sinkpad = gst_ghost_pad_new ("sink", pad);
    gst_element_add_pad (GST_ELEMENT_CAST (self), self->sinkpad);
    gst_object_unref (pad);
  }

  if (!res) {
    GST_WARNING_OBJECT (self, "Failed to add/connect the necessary machinery");
  }
}

static gboolean
_connect_sink_element (GstGLSinkBin * self)
{
  gboolean res = TRUE;

  gst_object_set_name (GST_OBJECT (self->sink), "sink");
  res &= gst_bin_add (GST_BIN (self), self->sink);

  res &= gst_element_link_pads (self->convert, "src", self->sink, "sink");

  if (!res)
    GST_ERROR_OBJECT (self, "Failed to link sink element into the pipeline");

  return res;
}

void
gst_gl_sink_bin_finish_init_with_element (GstGLSinkBin * self,
    GstElement * element)
{
  g_return_if_fail (GST_IS_ELEMENT (element));

  self->sink = element;

  if (!_connect_sink_element (self)) {
    gst_object_unref (self->sink);
    self->sink = NULL;
  }
}

void
gst_gl_sink_bin_finish_init (GstGLSinkBin * self)
{
  GstGLSinkBinClass *klass = GST_GL_SINK_BIN_GET_CLASS (self);
  GstElement *element = NULL;

  if (klass->create_element)
    element = klass->create_element ();

  if (element)
    gst_gl_sink_bin_finish_init_with_element (self, element);
}

static void
gst_gl_sink_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLSinkBin *self = GST_GL_SINK_BIN (object);

  switch (prop_id) {
    case PROP_SINK:
    {
      GstElement *sink = g_value_get_object (value);
      if (self->sink)
        gst_bin_remove (GST_BIN (self), self->sink);
      self->sink = sink;
      if (sink)
        _connect_sink_element (self);
      break;
    }
    case PROP_FORCE_ASPECT_RATIO:
      if (self->sink)
        g_object_set_property (G_OBJECT (self->sink), pspec->name, value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_sink_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLSinkBin *self = GST_GL_SINK_BIN (object);

  switch (prop_id) {
    case PROP_SINK:
      g_value_set_object (value, self->sink);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      if (self->sink)
        g_object_get_property (G_OBJECT (self->sink), pspec->name, value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_gl_sink_bin_change_state (GstElement * element, GstStateChange transition)
{
  GstGLSinkBin *self = GST_GL_SINK_BIN (element);
  GstGLSinkBinClass *klass = GST_GL_SINK_BIN_GET_CLASS (self);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG ("changing state: %s => %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!self->sink) {
        if (klass->create_element)
          self->sink = klass->create_element ();

        if (!self->sink)
          g_signal_emit (element,
              gst_gl_sink_bin_signals[SIGNAL_CREATE_ELEMENT], 0, &self->sink);

        if (!self->sink) {
          GST_ERROR_OBJECT (element, "Failed to retreive element");
          return GST_STATE_CHANGE_FAILURE;
        }
        if (!_connect_sink_element (self))
          return GST_STATE_CHANGE_FAILURE;
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    default:
      break;
  }

  return ret;
}

static void
gst_gl_sink_bin_navigation_send_event (GstNavigation * navigation, GstStructure
    * structure)
{
  GstGLSinkBin *self = GST_GL_SINK_BIN (navigation);
  GstElement *nav =
      gst_bin_get_by_interface (GST_BIN (self), GST_TYPE_NAVIGATION);

  if (nav) {
    gst_navigation_send_event (GST_NAVIGATION (nav), structure);
    structure = NULL;
    gst_object_unref (nav);
  } else {
    GstEvent *event = gst_event_new_navigation (structure);
    structure = NULL;
    gst_element_send_event (GST_ELEMENT (self), event);
  }
}

static void
gst_gl_sink_bin_navigation_interface_init (gpointer g_iface,
    gpointer g_iface_data)
{
  GstNavigationInterface *iface = (GstNavigationInterface *) g_iface;
  iface->send_event = gst_gl_sink_bin_navigation_send_event;
}

static void
gst_gl_sink_bin_overlay_expose (GstVideoOverlay * overlay)
{
  GstGLSinkBin *self = GST_GL_SINK_BIN (overlay);
  GstVideoOverlay *overlay_element = NULL;

  overlay_element =
      GST_VIDEO_OVERLAY (gst_bin_get_by_interface (GST_BIN (self),
          GST_TYPE_VIDEO_OVERLAY));

  if (overlay_element) {
    gst_video_overlay_expose (overlay_element);
    gst_object_unref (overlay_element);
  }
}

static void
gst_gl_sink_bin_overlay_handle_events (GstVideoOverlay * overlay,
    gboolean handle_events)
{
  GstGLSinkBin *self = GST_GL_SINK_BIN (overlay);
  GstVideoOverlay *overlay_element = NULL;

  overlay_element =
      GST_VIDEO_OVERLAY (gst_bin_get_by_interface (GST_BIN (self),
          GST_TYPE_VIDEO_OVERLAY));

  if (overlay_element) {
    gst_video_overlay_handle_events (overlay_element, handle_events);
    gst_object_unref (overlay_element);
  }
}

static void
gst_gl_sink_bin_overlay_set_render_rectangle (GstVideoOverlay * overlay, gint x,
    gint y, gint width, gint height)
{
  GstGLSinkBin *self = GST_GL_SINK_BIN (overlay);
  GstVideoOverlay *overlay_element = NULL;

  overlay_element =
      GST_VIDEO_OVERLAY (gst_bin_get_by_interface (GST_BIN (self),
          GST_TYPE_VIDEO_OVERLAY));

  if (overlay_element) {
    gst_video_overlay_set_render_rectangle (overlay_element, x, y, width,
        height);
    gst_object_unref (overlay_element);
  }
}

static void
gst_gl_sink_bin_overlay_set_window_handle (GstVideoOverlay * overlay,
    guintptr handle)
{
  GstGLSinkBin *self = GST_GL_SINK_BIN (overlay);
  GstVideoOverlay *overlay_element = NULL;

  overlay_element =
      GST_VIDEO_OVERLAY (gst_bin_get_by_interface (GST_BIN (self),
          GST_TYPE_VIDEO_OVERLAY));

  if (overlay_element) {
    gst_video_overlay_set_window_handle (overlay_element, handle);
    gst_object_unref (overlay_element);
  }
}

static void
gst_gl_sink_bin_video_overlay_init (gpointer g_iface, gpointer g_iface_data)
{
  GstVideoOverlayInterface *iface = (GstVideoOverlayInterface *) g_iface;
  iface->expose = gst_gl_sink_bin_overlay_expose;
  iface->handle_events = gst_gl_sink_bin_overlay_handle_events;
  iface->set_render_rectangle = gst_gl_sink_bin_overlay_set_render_rectangle;
  iface->set_window_handle = gst_gl_sink_bin_overlay_set_window_handle;
}
