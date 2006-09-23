/* GStreamer
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
 * Copyright (C) 2005,2006 David A. Schleef <ds@schleef.org>
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

#include <string.h>

#include <GL/glx.h>
#include <GL/gl.h>

GST_DEBUG_CATEGORY_STATIC (gst_debug_glimage_sink);
#define GST_CAT_DEFAULT gst_debug_glimage_sink

#define GST_TYPE_GLIMAGE_SINK \
    (gst_glimage_sink_get_type())
#define GST_GLIMAGE_SINK(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GLIMAGE_SINK,GstGLImageSink))
#define GST_GLIMAGE_SINK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GLIMAGE_SINK,GstGLImageSinkClass))
#define GST_IS_GLIMAGE_SINK(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GLIMAGE_SINK))
#define GST_IS_GLIMAGE_SINK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GLIMAGE_SINK))

typedef struct _GstGLImageSink GstGLImageSink;
typedef struct _GstGLImageSinkClass GstGLImageSinkClass;

struct _GstGLImageSink
{
  GstVideoSink video_sink;

  /* properties */
  char *display_name;

  /* caps */
  GstCaps *caps;
  int fps_n, fps_d;
  int par_n, par_d;
  int height, width;

  Window window;
  Window parent_window;
  XVisualInfo *visinfo;

  Display *display;
  GLXContext context;

  int max_texture_size;
  gboolean have_yuv;

  gboolean use_rgb;
  gboolean use_rgbx;
  gboolean use_yuy2;
};

struct _GstGLImageSinkClass
{
  GstVideoSinkClass video_sink_class;

};

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

static void gst_glimage_sink_create_window (GstGLImageSink * glimage_sink);
static gboolean gst_glimage_sink_init_display (GstGLImageSink * glimage_sink);
static void gst_glimage_sink_update_caps (GstGLImageSink * glimage_sink);
static void gst_glimage_sink_push_image (GstGLImageSink * glimage_sink,
    GstBuffer * buf);

static const GstElementDetails gst_glimage_sink_details =
GST_ELEMENT_DETAILS ("OpenGL video sink",
    "Sink/Video",
    "A videosink based on OpenGL",
    "David Schleef <ds@schleef.org>");

#ifdef GL_YCBCR_MESA
#define YUV_CAPS ";" GST_VIDEO_CAPS_YUV ("{ UYVY, YUY2 }")
#else
#define YUV_CAPS
#endif
static GstStaticPadTemplate gst_glimage_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_BGRx YUV_CAPS)
    );

enum
{
  ARG_0,
  ARG_DISPLAY
};

GST_BOILERPLATE (GstGLImageSink, gst_glimage_sink, GstVideoSink,
    GST_TYPE_VIDEO_SINK);

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

  gstbasesink_class->get_caps = gst_glimage_sink_get_caps;
  gstbasesink_class->set_caps = gst_glimage_sink_set_caps;
  gstbasesink_class->get_times = gst_glimage_sink_get_times;
  gstbasesink_class->preroll = gst_glimage_sink_render;
  gstbasesink_class->render = gst_glimage_sink_render;
}

static void
gst_glimage_sink_init (GstGLImageSink * glimage_sink,
    GstGLImageSinkClass * glimage_sink_class)
{
  int screen;

  glimage_sink->display = XOpenDisplay (NULL);
  if (glimage_sink->display) {
    screen = DefaultScreen (glimage_sink->display);

    XSynchronize (glimage_sink->display, True);
  }
  /* XSetErrorHandler (error_handler); */
  glimage_sink->width = 400;
  glimage_sink->height = 400;

  gst_glimage_sink_update_caps (glimage_sink);
  glimage_sink->display_name = g_strdup ("");
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
      /* FIXME this should close/reopen display */
      if (glimage_sink->display_name) {
        g_free (glimage_sink->display_name);
      }
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

  if (glimage_sink->display) {
    XCloseDisplay (glimage_sink->display);
  }
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
      if (!gst_glimage_sink_init_display (glimage_sink)) {
        GST_ELEMENT_ERROR (glimage_sink, RESOURCE, WRITE, (NULL),
            ("Could not initialize OpenGL"));
        return GST_STATE_CHANGE_FAILURE;
      }
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

static void
gst_glimage_sink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end)
{

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
  GstCaps *intersection;
  GstStructure *structure;
  int width;
  int height;
  gboolean ret;
  const GValue *fps;
  const GValue *par;

  GST_DEBUG ("set caps with %" GST_PTR_FORMAT, caps);

  glimage_sink = GST_GLIMAGE_SINK (bsink);

  intersection = gst_caps_intersect (glimage_sink->caps, caps);

  if (gst_caps_is_empty (intersection)) {
    return FALSE;
  }

  gst_caps_unref (intersection);

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width", &width);
  ret &= gst_structure_get_int (structure, "height", &height);
  fps = gst_structure_get_value (structure, "framerate");
  ret &= (fps != NULL);
  par = gst_structure_get_value (structure, "pixel-aspect-ratio");

  if (!ret)
    return FALSE;

  glimage_sink->width = width;
  glimage_sink->height = height;
  glimage_sink->fps_n = gst_value_get_fraction_numerator (fps);
  glimage_sink->fps_d = gst_value_get_fraction_denominator (fps);
  if (par) {
    glimage_sink->par_n = gst_value_get_fraction_numerator (par);
    glimage_sink->par_d = gst_value_get_fraction_denominator (par);
  } else {
    glimage_sink->par_n = 1;
    glimage_sink->par_d = 1;
  }

  GST_VIDEO_SINK_WIDTH (glimage_sink) = width;
  GST_VIDEO_SINK_HEIGHT (glimage_sink) = height;

  if (strcmp (gst_structure_get_name (structure), "video/x-raw-rgb") == 0) {
    int red_mask;

    GST_DEBUG ("using RGB");
    glimage_sink->use_rgb = TRUE;
    gst_structure_get_int (structure, "red_mask", &red_mask);

    if (red_mask == 0xff000000) {
      glimage_sink->use_rgbx = TRUE;
    } else {
      glimage_sink->use_rgbx = FALSE;
    }
  } else {
    unsigned int fourcc;

    GST_DEBUG ("using YUV");
    glimage_sink->use_rgb = FALSE;

    gst_structure_get_fourcc (structure, "format", &fourcc);
    if (fourcc == GST_MAKE_FOURCC ('Y', 'U', 'Y', '2')) {
      glimage_sink->use_yuy2 = TRUE;
    } else {
      glimage_sink->use_yuy2 = FALSE;
    }
  }

#if 0
  if (!glimage_sink->window) {
    gst_x_overlay_prepare_xwindow_id (GST_X_OVERLAY (glimage_sink));
  }
#endif

  if (!glimage_sink->window) {
    gst_glimage_sink_create_window (glimage_sink);
  }

  return TRUE;
}

static GstFlowReturn
gst_glimage_sink_render (GstBaseSink * bsink, GstBuffer * buf)
{
  GstGLImageSink *glimage_sink;

  glimage_sink = GST_GLIMAGE_SINK (bsink);

  gst_glimage_sink_push_image (glimage_sink, buf);

  return GST_FLOW_OK;
}


/*
 * helper functions
 */

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

static void
gst_glimage_sink_update_caps (GstGLImageSink * glimage_sink)
{
  GstCaps *caps;
  int max_size;

  if (glimage_sink->caps) {
    gst_caps_unref (glimage_sink->caps);
  }

  if (glimage_sink->display == NULL) {
    glimage_sink->caps =
        gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD
            (glimage_sink)));
    return;
  }

  caps = gst_caps_from_string (GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_BGRx);
#ifdef GL_YCBCR_MESA
  if (glimage_sink->have_yuv) {
    GstCaps *ycaps =
        gst_caps_from_string (GST_VIDEO_CAPS_YUV ("{ UYVY, YUY2 }"));
    gst_caps_append (ycaps, caps);
    caps = ycaps;
  }
#endif

  max_size = glimage_sink->max_texture_size;
  if (max_size == 0) {
    max_size = 1024;
  }

  gst_caps_set_all (caps,
      "width", GST_TYPE_INT_RANGE, 16, max_size,
      "height", GST_TYPE_INT_RANGE, 16, max_size, NULL);

  glimage_sink->caps = caps;
}

static void
gst_glimage_sink_create_window (GstGLImageSink * glimage_sink)
{
  gboolean ret;
  Window root;
  XSetWindowAttributes attr;
  Screen *screen;
  int scrnum;
  int mask;
  int width, height;

  screen = XDefaultScreenOfDisplay (glimage_sink->display);
  scrnum = XScreenNumberOfScreen (screen);
  root = XRootWindow (glimage_sink->display, scrnum);

  if (glimage_sink->parent_window) {
    XWindowAttributes pattr;

    XGetWindowAttributes (glimage_sink->display, glimage_sink->parent_window,
        &pattr);
    width = pattr.width;
    height = pattr.height;
  } else {
    width = GST_VIDEO_SINK (glimage_sink)->width;
    height = GST_VIDEO_SINK (glimage_sink)->height;
  }
  attr.background_pixel = 0;
  attr.border_pixel = 0;
  attr.colormap = XCreateColormap (glimage_sink->display, root,
      glimage_sink->visinfo->visual, AllocNone);
  if (glimage_sink->parent_window) {
    attr.override_redirect = True;
  } else {
    attr.override_redirect = False;
  }

  mask = CWBackPixel | CWBorderPixel | CWColormap | CWOverrideRedirect;

  GST_DEBUG ("creating window with size %d x %d", width, height);

  glimage_sink->window = XCreateWindow (glimage_sink->display, root, 0, 0,
      width, height,
      0, glimage_sink->visinfo->depth, InputOutput,
      glimage_sink->visinfo->visual, mask, &attr);

  if (glimage_sink->parent_window) {
    ret = XReparentWindow (glimage_sink->display, glimage_sink->window,
        glimage_sink->parent_window, 0, 0);
    XMapWindow (glimage_sink->display, glimage_sink->window);
  } else {
    XMapWindow (glimage_sink->display, glimage_sink->window);
  }

  glXMakeCurrent (glimage_sink->display, glimage_sink->window,
      glimage_sink->context);

  glDepthFunc (GL_LESS);
  glEnable (GL_DEPTH_TEST);
  glClearColor (0.2, 0.2, 0.2, 1.0);
  glViewport (0, 0, width, height);
}


static gboolean
gst_glimage_sink_init_display (GstGLImageSink * glimage_sink)
{
  gboolean ret;
  XVisualInfo *visinfo;
  Screen *screen;
  Window root;
  int scrnum;
  int attrib[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8,
    GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None
  };
  XSetWindowAttributes attr;
  int error_base;
  int event_base;
  int mask;
  const char *extstring;
  Window window;

  GST_LOG_OBJECT (glimage_sink, "initializing display");

  glimage_sink->display = XOpenDisplay (NULL);
  if (glimage_sink->display == NULL) {
    GST_DEBUG_OBJECT (glimage_sink, "Could not open display");
    return FALSE;
  }

  screen = XDefaultScreenOfDisplay (glimage_sink->display);
  scrnum = XScreenNumberOfScreen (screen);
  root = XRootWindow (glimage_sink->display, scrnum);

  ret = glXQueryExtension (glimage_sink->display, &error_base, &event_base);
  if (!ret) {
    GST_LOG_OBJECT (glimage_sink, "No GLX extension");
    return FALSE;
  }

  visinfo = glXChooseVisual (glimage_sink->display, scrnum, attrib);
  if (visinfo == NULL) {
    GST_LOG_OBJECT (glimage_sink, "No usable visual");
    return FALSE;
  }

  glimage_sink->visinfo = visinfo;

  glimage_sink->context = glXCreateContext (glimage_sink->display,
      visinfo, NULL, True);

  attr.background_pixel = 0;
  attr.border_pixel = 0;
  attr.colormap = XCreateColormap (glimage_sink->display, root,
      visinfo->visual, AllocNone);
  attr.event_mask = StructureNotifyMask | ExposureMask;
  attr.override_redirect = True;

  //mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;
  mask = CWBackPixel | CWBorderPixel | CWColormap | CWOverrideRedirect;

  window = XCreateWindow (glimage_sink->display, root, 0, 0,
      100, 100, 0, visinfo->depth, InputOutput, visinfo->visual, mask, &attr);

  glXMakeCurrent (glimage_sink->display, window, glimage_sink->context);

  glGetIntegerv (GL_MAX_TEXTURE_SIZE, &glimage_sink->max_texture_size);

  extstring = (const char *) glGetString (GL_EXTENSIONS);
#ifdef GL_YCBCR_MESA
  if (strstr (extstring, "GL_MESA_ycbcr_texture")) {
    glimage_sink->have_yuv = TRUE;
  } else {
    glimage_sink->have_yuv = FALSE;
  }
#else
  glimage_sink->have_yuv = FALSE;
#endif

  glXMakeCurrent (glimage_sink->display, None, NULL);
  XDestroyWindow (glimage_sink->display, window);

  return TRUE;
}

static void
gst_glimage_sink_push_image (GstGLImageSink * glimage_sink, GstBuffer * buf)
{
  int texture_size;
  XWindowAttributes attr;

  g_return_if_fail (buf != NULL);

  if (glimage_sink->display == NULL || glimage_sink->window == 0) {
    g_warning ("display or window not set up\n");
  }

  glXMakeCurrent (glimage_sink->display, glimage_sink->window,
      glimage_sink->context);

  if (glimage_sink->parent_window) {
    XGetWindowAttributes (glimage_sink->display, glimage_sink->parent_window,
        &attr);
    //gst_glimage_sink_set_window_size (glimage_sink, attr.width, attr.height);
  } else {
    XGetWindowAttributes (glimage_sink->display, glimage_sink->window, &attr);
    glViewport (0, 0, attr.width, attr.height);
  }

  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();

  glDisable (GL_CULL_FACE);
  glEnable (GL_TEXTURE_2D);
  glEnableClientState (GL_TEXTURE_COORD_ARRAY);

  glColor4f (1, 1, 1, 1);

#define TEXID 1000
  glBindTexture (GL_TEXTURE_2D, TEXID);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  for (texture_size = 64;
      (texture_size < GST_VIDEO_SINK (glimage_sink)->width ||
          texture_size < GST_VIDEO_SINK (glimage_sink)->height) &&
      (texture_size > 0); texture_size <<= 1);

  if (glimage_sink->use_rgb) {
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, texture_size,
        texture_size, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    if (glimage_sink->use_rgbx) {
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0,
          GST_VIDEO_SINK (glimage_sink)->width,
          GST_VIDEO_SINK (glimage_sink)->height,
          GL_RGBA, GL_UNSIGNED_BYTE, GST_BUFFER_DATA (buf));
    } else {
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0,
          GST_VIDEO_SINK (glimage_sink)->width,
          GST_VIDEO_SINK (glimage_sink)->height,
          GL_BGRA, GL_UNSIGNED_BYTE, GST_BUFFER_DATA (buf));
    }
  } else {
#ifdef GL_YCBCR_MESA
    glTexImage2D (GL_TEXTURE_2D, 0, GL_YCBCR_MESA, texture_size,
        texture_size, 0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, NULL);

    if (glimage_sink->use_yuy2) {
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0,
          GST_VIDEO_SINK (glimage_sink)->width,
          GST_VIDEO_SINK (glimage_sink)->height,
          GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, GST_BUFFER_DATA (buf));
    } else {
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0,
          GST_VIDEO_SINK (glimage_sink)->width,
          GST_VIDEO_SINK (glimage_sink)->height,
          GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, GST_BUFFER_DATA (buf));
    }
#else
    g_assert_not_reached ();
#endif
  }

  glColor4f (1, 0, 1, 1);
  glBegin (GL_QUADS);

  glNormal3f (0, 0, -1);

  {
    double xmax = GST_VIDEO_SINK (glimage_sink)->width / (double) texture_size;
    double ymax = GST_VIDEO_SINK (glimage_sink)->height / (double) texture_size;

    glTexCoord2f (xmax, 0);
    glVertex3f (1.0, 1.0, 0);
    glTexCoord2f (0, 0);
    glVertex3f (-1.0, 1.0, 0);
    glTexCoord2f (0, ymax);
    glVertex3f (-1.0, -1.0, 0);
    glTexCoord2f (xmax, ymax);
    glVertex3f (1.0, -1.0, 0);
    glEnd ();
  }

  glFlush ();
  glXSwapBuffers (glimage_sink->display, glimage_sink->window);
}


#ifdef unused


static void gst_glimage_sink_set_window_size (GstGLImageSink * glimage_sink,
    int width, int height);



static void
gst_glimage_sink_set_window_size (GstGLImageSink * glimage_sink,
    int width, int height)
{
  GST_DEBUG ("resizing to %d x %d",
      GST_VIDEO_SINK_WIDTH (glimage_sink),
      GST_VIDEO_SINK_HEIGHT (glimage_sink));

  if (glimage_sink->display && glimage_sink->window) {
    XResizeWindow (glimage_sink->display, glimage_sink->window, width, height);
    XSync (glimage_sink->display, False);
    glViewport (0, 0, width, height);
  }
}



static void
gst_glimage_sink_set_xwindow_id (GstXOverlay * overlay, XID xwindow_id)
{
  GstGLImageSink *glimage_sink = GST_GLIMAGE_SINK (overlay);

  GST_DEBUG ("set_xwindow_id %ld", xwindow_id);

  g_return_if_fail (GST_IS_GLIMAGE_SINK (glimage_sink));

  /* If the element has not initialized the X11 context try to do so */
  if (!glimage_sink->display) {
    g_warning ("X display not inited\n");
  }

  if (glimage_sink->parent_window == xwindow_id)
    return;

  glimage_sink->parent_window = xwindow_id;

  XSync (glimage_sink->display, False);
  gst_glimage_sink_create_window (glimage_sink);
}

#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "glimagesink",
          GST_RANK_NONE, GST_TYPE_GLIMAGE_SINK))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_debug_glimage_sink, "glimagesink", 0,
      "glimagesink element");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "glimagesink",
    "OpenGL video output plugin",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
