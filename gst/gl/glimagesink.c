/* GStreamer
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
 * Copyright (C) 2005,2006,2007 David A. Schleef <ds@schleef.org>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#include <gst/gst.h>
#include <gst/interfaces/xoverlay.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/video.h>
#include "gstglbuffer.h"

#include <string.h>

#include <glimagesink.h>

GST_DEBUG_CATEGORY (gst_debug_glimage_sink);
#define GST_CAT_DEFAULT gst_debug_glimage_sink

static void gst_glimage_sink_init_interfaces (GType type);

static void gst_glimage_sink_finalize (GObject * object);
static void gst_glimage_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * param_spec);
static void gst_glimage_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * param_spec);

static GstStateChangeReturn
gst_glimage_sink_change_state (GstElement * element, GstStateChange transition);

static void gst_glimage_sink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end);
static GstCaps *gst_glimage_sink_get_caps (GstBaseSink * bsink);
static gboolean gst_glimage_sink_set_caps (GstBaseSink * bsink, GstCaps * caps);
static GstFlowReturn gst_glimage_sink_render (GstBaseSink * bsink,
    GstBuffer * buf);
static gboolean gst_glimage_sink_start (GstBaseSink * bsink);
static gboolean gst_glimage_sink_stop (GstBaseSink * bsink);
static gboolean gst_glimage_sink_unlock (GstBaseSink * bsink);

static void gst_glimage_sink_xoverlay_init (GstXOverlayClass * iface);
static void gst_glimage_sink_set_xwindow_id (GstXOverlay * overlay,
    XID window_id);
static void gst_glimage_sink_expose (GstXOverlay * overlay);
static void gst_glimage_sink_set_event_handling (GstXOverlay * overlay,
    gboolean handle_events);

static gboolean gst_glimage_sink_interface_supported (GstImplementsInterface *
    iface, GType type);
static void gst_glimage_sink_implements_init (GstImplementsInterfaceClass *
    klass);

//static void gst_glimage_sink_update_caps (GstGLImageSink * glimage_sink);

static const GstElementDetails gst_glimage_sink_details =
GST_ELEMENT_DETAILS ("OpenGL video sink",
    "Sink/Video",
    "A videosink based on OpenGL",
    "David Schleef <ds@schleef.org>");

#ifdef GL_YCBCR_MESA
#define YUV_CAPS ";" GST_VIDEO_CAPS_YUV ("{ AYUV, UYVY, YUY2 }")
#else
#define YUV_CAPS
#endif
static GstStaticPadTemplate gst_glimage_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_VIDEO_CAPS)
    );

//GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_BGRx ";"
//GST_VIDEO_CAPS_xRGB ";" GST_VIDEO_CAPS_xBGR YUV_CAPS)

enum
{
  ARG_0,
  ARG_DISPLAY
};

GST_BOILERPLATE_FULL (GstGLImageSink, gst_glimage_sink, GstVideoSink,
    GST_TYPE_VIDEO_SINK, gst_glimage_sink_init_interfaces);

static void
gst_glimage_sink_init_interfaces (GType type)
{
  static const GInterfaceInfo overlay_info = {
    (GInterfaceInitFunc) gst_glimage_sink_xoverlay_init,
    NULL,
    NULL
  };
  static const GInterfaceInfo implements_info = {
    (GInterfaceInitFunc) gst_glimage_sink_implements_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (type, GST_TYPE_IMPLEMENTS_INTERFACE,
      &implements_info);
  g_type_add_interface_static (type, GST_TYPE_X_OVERLAY, &overlay_info);
}

static void
gst_glimage_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_glimage_sink_details);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_glimage_sink_template));

}

static void
gst_glimage_sink_class_init (GstGLImageSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  gobject_class->set_property = gst_glimage_sink_set_property;
  gobject_class->get_property = gst_glimage_sink_get_property;

  g_object_class_install_property (gobject_class, ARG_DISPLAY,
      g_param_spec_string ("display", "Display", "X Display name",
          NULL, G_PARAM_READWRITE));

  gobject_class->finalize = gst_glimage_sink_finalize;

  gstelement_class->change_state = gst_glimage_sink_change_state;

  if (0)
    gstbasesink_class->get_caps = gst_glimage_sink_get_caps;
  gstbasesink_class->set_caps = gst_glimage_sink_set_caps;
  gstbasesink_class->get_times = gst_glimage_sink_get_times;
  gstbasesink_class->preroll = gst_glimage_sink_render;
  gstbasesink_class->render = gst_glimage_sink_render;
  gstbasesink_class->start = gst_glimage_sink_start;
  gstbasesink_class->stop = gst_glimage_sink_stop;
  gstbasesink_class->unlock = gst_glimage_sink_unlock;
}

static void
gst_glimage_sink_init (GstGLImageSink * glimage_sink,
    GstGLImageSinkClass * glimage_sink_class)
{

  glimage_sink->display_name = NULL;
  //gst_glimage_sink_update_caps (glimage_sink);
}

static void
gst_glimage_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLImageSink *glimage_sink;

  g_return_if_fail (GST_IS_GLIMAGE_SINK (object));

  glimage_sink = GST_GLIMAGE_SINK (object);

  switch (prop_id) {
    case ARG_DISPLAY:
      g_free (glimage_sink->display_name);
      glimage_sink->display_name = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_glimage_sink_finalize (GObject * object)
{
  GstGLImageSink *glimage_sink;

  g_return_if_fail (GST_IS_GLIMAGE_SINK (object));

  glimage_sink = GST_GLIMAGE_SINK (object);

  if (glimage_sink->caps) {
    gst_caps_unref (glimage_sink->caps);
  }
  g_free (glimage_sink->display_name);
}

static void
gst_glimage_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLImageSink *glimage_sink;

  g_return_if_fail (GST_IS_GLIMAGE_SINK (object));

  glimage_sink = GST_GLIMAGE_SINK (object);

  switch (prop_id) {
    case ARG_DISPLAY:
      g_value_set_string (value, glimage_sink->display_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/*
 * GstElement methods
 */

static GstStateChangeReturn
gst_glimage_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstGLImageSink *glimage_sink;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG ("change state");

  glimage_sink = GST_GLIMAGE_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
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
      /* FIXME clear window */
      glimage_sink->fps_n = 0;
      glimage_sink->fps_d = 1;
      GST_VIDEO_SINK_WIDTH (glimage_sink) = 0;
      GST_VIDEO_SINK_HEIGHT (glimage_sink) = 0;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      /* FIXME dispose of window */
      break;
    default:
      break;
  }

  return ret;
}

/*
 * GstBaseSink methods
 */

static gboolean
gst_glimage_sink_start (GstBaseSink * bsink)
{
  GstGLImageSink *glimage_sink;

  //gboolean ret;

  GST_DEBUG ("start");

  glimage_sink = GST_GLIMAGE_SINK (bsink);

#if 0
  glimage_sink->display = gst_gl_display_new ();
  ret = gst_gl_display_connect (glimage_sink->display,
      glimage_sink->display_name);
  if (!ret) {
    GST_ERROR ("failed to open display");
    return FALSE;
  }
#endif

  if (glimage_sink->display && glimage_sink->window_id) {
    gst_gl_display_set_window (glimage_sink->display, glimage_sink->window_id);
  }

  GST_DEBUG ("start done");

  return TRUE;
}

static gboolean
gst_glimage_sink_stop (GstBaseSink * bsink)
{
  GstGLImageSink *glimage_sink;

  GST_DEBUG ("stop");

  glimage_sink = GST_GLIMAGE_SINK (bsink);

  if (glimage_sink->display) {
    g_object_unref (glimage_sink->display);
    glimage_sink->display = NULL;
  }

  return TRUE;
}

static gboolean
gst_glimage_sink_unlock (GstBaseSink * bsink)
{
  //GstGLImageSink *glimage_sink;

  GST_DEBUG ("unlock");

  //glimage_sink = GST_GLIMAGE_SINK (bsink);

  /* FIXME */

  return TRUE;
}

static void
gst_glimage_sink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end)
{
  GstGLImageSink *glimagesink;

  glimagesink = GST_GLIMAGE_SINK (bsink);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    *start = GST_BUFFER_TIMESTAMP (buf);
    if (GST_BUFFER_DURATION_IS_VALID (buf)) {
      *end = *start + GST_BUFFER_DURATION (buf);
    } else {
      if (glimagesink->fps_n > 0) {
        *end = *start +
            gst_util_uint64_scale_int (GST_SECOND, glimagesink->fps_d,
            glimagesink->fps_n);
      }
    }
  }


}

static GstCaps *
gst_glimage_sink_get_caps (GstBaseSink * bsink)
{
  GstGLImageSink *glimage_sink;

  glimage_sink = GST_GLIMAGE_SINK (bsink);

  GST_DEBUG ("get caps returning %" GST_PTR_FORMAT, glimage_sink->caps);

  return gst_caps_ref (glimage_sink->caps);
}

static gboolean
gst_glimage_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstGLImageSink *glimage_sink;
  int width;
  int height;
  gboolean ok;
  int fps_n, fps_d;
  int par_n, par_d;
  GstVideoFormat format;
  GstStructure *structure;

  GST_DEBUG ("set caps with %" GST_PTR_FORMAT, caps);

  glimage_sink = GST_GLIMAGE_SINK (bsink);

  structure = gst_caps_get_structure (caps, 0);
  if (0) {
    ok = gst_video_format_parse_caps (caps, &format, &width, &height);
    ok &= gst_video_parse_caps_framerate (caps, &fps_n, &fps_d);
    ok &= gst_video_parse_caps_pixel_aspect_ratio (caps, &par_n, &par_d);
  } else {
    ok = gst_structure_get_int (structure, "width", &width);
    ok &= gst_structure_get_int (structure, "height", &height);
    ok &= gst_video_parse_caps_framerate (caps, &fps_n, &fps_d);
    ok &= gst_video_parse_caps_pixel_aspect_ratio (caps, &par_n, &par_d);
  }

  if (!ok)
    return FALSE;

  GST_VIDEO_SINK_WIDTH (glimage_sink) = width;
  GST_VIDEO_SINK_HEIGHT (glimage_sink) = height;
  //glimage_sink->format = format;
  glimage_sink->fps_n = fps_n;
  glimage_sink->fps_d = fps_d;
  glimage_sink->par_n = par_n;
  glimage_sink->par_d = par_d;

  return TRUE;
}

static GstFlowReturn
gst_glimage_sink_render (GstBaseSink * bsink, GstBuffer * buf)
{
  GstGLImageSink *glimage_sink;
  GstGLBuffer *gl_buffer = GST_GL_BUFFER (buf);
  GstGLDisplay *display;

  GST_DEBUG ("render");

  glimage_sink = GST_GLIMAGE_SINK (bsink);

  if (glimage_sink->display == NULL) {
    glimage_sink->display = g_object_ref (gl_buffer->display);
#if 1
    if (glimage_sink->window_id) {
      gst_gl_display_set_window (glimage_sink->display,
          glimage_sink->window_id);
    }
#endif
  } else {
    g_assert (gl_buffer->display == glimage_sink->display);
  }

  display = gl_buffer->display;

  /* FIXME polling causes a round-trip delay.  This should be changed
   * to catch structure events */
  gst_gl_display_update_attributes (display);

  gst_gl_display_lock (display);
  glViewport (0, 0, display->win_width, display->win_height);

  glClearColor (0.3, 0.3, 0.3, 1.0);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();

  glDisable (GL_CULL_FACE);
  glEnableClientState (GL_TEXTURE_COORD_ARRAY);

  glColor4f (1, 1, 1, 1);

  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, gl_buffer->texture);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP);
  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  glColor4f (1, 0, 1, 1);

  glBegin (GL_QUADS);
  glNormal3f (0, 0, -1);
  glTexCoord2f (gl_buffer->width, 0);
  glVertex3f (1.0, 1.0, 0);
  glTexCoord2f (0, 0);
  glVertex3f (-1.0, 1.0, 0);
  glTexCoord2f (0, gl_buffer->height);
  glVertex3f (-1.0, -1.0, 0);
  glTexCoord2f (gl_buffer->width, gl_buffer->height);
  glVertex3f (1.0, -1.0, 0);
  glEnd ();

  glXSwapBuffers (display->display, display->window);

  gst_gl_display_unlock (display);

  return GST_FLOW_OK;
}

/*
 * XOverlay
 */
static void
gst_glimage_sink_xoverlay_init (GstXOverlayClass * iface)
{
  iface->set_xwindow_id = gst_glimage_sink_set_xwindow_id;
  iface->expose = gst_glimage_sink_expose;
  iface->handle_events = gst_glimage_sink_set_event_handling;
}

static void
gst_glimage_sink_set_xwindow_id (GstXOverlay * overlay, XID window_id)
{
  GstGLImageSink *glimage_sink;

  g_return_if_fail (GST_IS_GLIMAGE_SINK (overlay));

  GST_DEBUG ("set_xwindow_id %ld", window_id);

  glimage_sink = GST_GLIMAGE_SINK (overlay);

  if (glimage_sink->window_id == window_id) {
    return;
  }
  glimage_sink->window_id = window_id;
  if (glimage_sink->display) {
    gst_gl_display_set_window (glimage_sink->display, glimage_sink->window_id);
  }
}

static void
gst_glimage_sink_expose (GstXOverlay * overlay)
{
  /* FIXME */
  GST_DEBUG ("expose");
}

static void
gst_glimage_sink_set_event_handling (GstXOverlay * overlay,
    gboolean handle_events)
{
  /* FIXME */
  GST_DEBUG ("handle_events %d", handle_events);
}

/*
 * GstImplementsInterface
 */
static gboolean
gst_glimage_sink_interface_supported (GstImplementsInterface * iface,
    GType type)
{
  return TRUE;
}

static void
gst_glimage_sink_implements_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_glimage_sink_interface_supported;
}


/*
 * helper functions
 */

#if 0
static void
gst_caps_set_all (GstCaps * caps, char *field, ...)
{
  GstStructure *structure;
  va_list var_args;
  int i;

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    structure = gst_caps_get_structure (caps, i);

    va_start (var_args, field);
    gst_structure_set_valist (structure, field, var_args);
    va_end (var_args);
  }
}
#endif

#if 0
static void
gst_glimage_sink_update_caps (GstGLImageSink * glimage_sink)
{
  GstCaps *caps;
  int max_size;

  if (glimage_sink->display == NULL) {
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD
            (glimage_sink)));
    gst_caps_replace (&glimage_sink->caps, caps);
    return;
  }

  caps =
      gst_caps_from_string (GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_BGRx ";"
      GST_VIDEO_CAPS_xRGB ";" GST_VIDEO_CAPS_xBGR);
#ifdef GL_YCBCR_MESA
  if (glimage_sink->display->have_ycbcr_texture) {
    GstCaps *ycaps =
        gst_caps_from_string (GST_VIDEO_CAPS_YUV ("{ AYUV, UYVY, YUY2 }"));
    gst_caps_append (ycaps, caps);
    caps = ycaps;
  }
#endif

  max_size = glimage_sink->display->max_texture_size;
  if (max_size == 0) {
    max_size = 1024;
  }

  gst_caps_set_all (caps,
      "width", GST_TYPE_INT_RANGE, 16, max_size,
      "height", GST_TYPE_INT_RANGE, 16, max_size, NULL);

  gst_caps_replace (&glimage_sink->caps, caps);
}
#endif
