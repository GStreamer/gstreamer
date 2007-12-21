/* 
 * GStreamer
 * Copyright (C) 2007 David Schleef <ds@schleef.org>
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
#include <gst/video/video.h>
#include <gstglbuffer.h>

#define GST_CAT_DEFAULT gst_gl_filter_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define GST_TYPE_GL_FILTER            (gst_gl_filter_get_type())
#define GST_GL_FILTER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_FILTER,GstGLFilter))
#define GST_IS_GL_FILTER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_FILTER))
#define GST_GL_FILTER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_GL_FILTER,GstGLFilterClass))
#define GST_IS_GL_FILTER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_GL_FILTER))
#define GST_GL_FILTER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_GL_FILTER,GstGLFilterClass))
typedef struct _GstGLFilter GstGLFilter;
typedef struct _GstGLFilterClass GstGLFilterClass;

typedef void (*GstGLFilterProcessFunc) (GstGLFilter *, guint8 *, guint);

struct _GstGLFilter
{
  GstElement element;

  GstPad *srcpad;
  GstPad *sinkpad;

  /* < private > */

  GstGLDisplay *display;
  GstVideoFormat format;
  int width;
  int height;
};

struct _GstGLFilterClass
{
  GstElementClass element_class;
};

static const GstElementDetails element_details = GST_ELEMENT_DETAILS ("FIXME",
    "Filter/Effect",
    "FIXME example filter",
    "FIXME <fixme@fixme.com>");

#define GST_GL_VIDEO_CAPS "video/x-raw-gl"

static GstStaticPadTemplate gst_gl_filter_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_VIDEO_CAPS)
    );

static GstStaticPadTemplate gst_gl_filter_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_VIDEO_CAPS)
    );

enum
{
  PROP_0
};

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_gl_filter_debug, "glfilter", 0, "glfilter element");

GST_BOILERPLATE_FULL (GstGLFilter, gst_gl_filter, GstElement,
    GST_TYPE_ELEMENT, DEBUG_INIT);

static void gst_gl_filter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_filter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_gl_filter_chain (GstPad * pad, GstBuffer * buf);
static void gst_gl_filter_reset (GstGLFilter * filter);
static GstStateChangeReturn
gst_gl_filter_change_state (GstElement * element, GstStateChange transition);
static gboolean gst_gl_filter_sink_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_gl_filter_transform (GstGLBuffer * outbuf,
    GstGLBuffer * inbuf);


static void
gst_gl_filter_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &element_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_filter_src_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_filter_sink_pad_template));
}

static void
gst_gl_filter_class_init (GstGLFilterClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_gl_filter_set_property;
  gobject_class->get_property = gst_gl_filter_get_property;

  GST_ELEMENT_CLASS (klass)->change_state = gst_gl_filter_change_state;
}

static void
gst_gl_filter_init (GstGLFilter * filter, GstGLFilterClass * klass)
{
  gst_element_create_all_pads (GST_ELEMENT (filter));

  filter->sinkpad = gst_element_get_static_pad (GST_ELEMENT (filter), "sink");
  filter->srcpad = gst_element_get_static_pad (GST_ELEMENT (filter), "src");

  gst_pad_set_setcaps_function (filter->sinkpad, gst_gl_filter_sink_setcaps);
  gst_pad_set_chain_function (filter->sinkpad, gst_gl_filter_chain);

  gst_gl_filter_reset (filter);
}

static void
gst_gl_filter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  //GstGLFilter *filter = GST_GL_FILTER (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_filter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  //GstGLFilter *filter = GST_GL_FILTER (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_filter_reset (GstGLFilter * filter)
{
  if (filter->display) {
    g_object_unref (filter->display);
    filter->display = NULL;
  }
  filter->format = GST_VIDEO_FORMAT_BGRx;
}

static gboolean
gst_gl_filter_start (GstGLFilter * filter)
{
  gboolean ret;

  filter->format = GST_VIDEO_FORMAT_BGRx;
  filter->display = gst_gl_display_new ();
  ret = gst_gl_display_connect (filter->display, NULL);

  return ret;
}

static gboolean
gst_gl_filter_stop (GstGLFilter * filter)
{
  gst_gl_filter_reset (filter);

  return TRUE;
}

static gboolean
gst_gl_filter_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstGLFilter *filter;
  gboolean ret;
  GstStructure *structure;

  filter = GST_GL_FILTER (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  ret = gst_structure_get_int (structure, "width", &filter->width);
  ret &= gst_structure_get_int (structure, "height", &filter->height);
  if (!ret)
    return FALSE;

  GST_ERROR ("setcaps %d %d", filter->width, filter->height);

  ret = gst_pad_set_caps (filter->srcpad, caps);

  return ret;
}

static GstFlowReturn
gst_gl_filter_chain (GstPad * pad, GstBuffer * buf)
{
  GstGLFilter *filter;
  GstGLBuffer *inbuf;
  GstGLBuffer *outbuf;

  filter = GST_GL_FILTER (gst_pad_get_parent (pad));
  inbuf = GST_GL_BUFFER (buf);

  outbuf = gst_gl_buffer_new (inbuf->display, filter->format,
      filter->width, filter->height);

  gst_buffer_copy_metadata (GST_BUFFER (outbuf), buf,
      GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_FLAGS);
  gst_buffer_set_caps (GST_BUFFER (outbuf), GST_PAD_CAPS (filter->srcpad));

  gst_gl_filter_transform (outbuf, inbuf);

  gst_pad_push (filter->srcpad, GST_BUFFER (outbuf));

  gst_object_unref (filter);
  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_gl_filter_change_state (GstElement * element, GstStateChange transition)
{
  GstGLFilter *filter;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG ("change state");

  filter = GST_GL_FILTER (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_gl_filter_start (filter);
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
      gst_gl_filter_stop (filter);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

void
dump_fbconfigs (Display * display)
{
  GLXFBConfig *fbconfigs;
  int n;
  int i;
  int j;
  int ret;
  int value;
  struct
  {
    int attr;
    char *name;
  } list[] = {
    {
    GLX_DRAWABLE_TYPE, "drawable type"}, {
    GLX_BIND_TO_TEXTURE_TARGETS_EXT, "bind to texture targets"}, {
    GLX_BIND_TO_TEXTURE_RGBA_EXT, "bind to texture rgba"}, {
    GLX_MAX_PBUFFER_WIDTH, "max pbuffer width"}, {
    GLX_MAX_PBUFFER_HEIGHT, "max pbuffer height"}, {
    GLX_MAX_PBUFFER_PIXELS, "max pbuffer pixels"}, {
    GLX_RENDER_TYPE, "render type"}, {
    0, 0}
  };

  g_print ("screen count: %d\n", ScreenCount (display));

  fbconfigs = glXGetFBConfigs (display, 0, &n);
  for (i = 0; i < n; i++) {
    g_print ("%d:\n", i);
    for (j = 0; list[j].attr; j++) {
      ret = glXGetFBConfigAttrib (display, fbconfigs[i], list[j].attr, &value);
      if (ret != Success) {
        g_print ("%s: failed\n", list[j].name);
      } else {
        g_print ("%s: %d\n", list[j].name, value);
      }
    }
  }

}

static gboolean
gst_gl_filter_transform (GstGLBuffer * outbuf, GstGLBuffer * inbuf)
{
  GstGLDisplay *display;

#if 0
  int pixmapAttribs[] = {
    GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_RECTANGLE_EXT,
    GLX_TEXTURE_FORMAT_EXT, GLX_TEXTURE_FORMAT_RGBA_EXT,
    None
  };
#endif
  GLXFBConfig *fbconfigs;
  int n;
  int i;
  GLXDrawable glxpixmap;
  GLXContext context = 0;
  int fb_index = 0;
  int attrib[] = { GLX_RGBA, GLX_RED_SIZE, 8,
    GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None
  };
  XVisualInfo *visinfo;

  display = outbuf->display;
  gst_gl_display_lock (display);

  //context = glXCreateContext (display->display, visinfo, NULL, True);

  dump_fbconfigs (display->display);

  fbconfigs = glXGetFBConfigs (display->display, display->screen_num, &n);
  for (i = 0; i < n; i++) {
    XVisualInfo *visinfo;
    int value;

    GST_DEBUG ("fbconfig %d", i);

    visinfo = glXGetVisualFromFBConfig (display->display, fbconfigs[i]);
    GST_DEBUG ("visinfo %p", visinfo);

    glXGetFBConfigAttrib (display->display, fbconfigs[i],
        GLX_DRAWABLE_TYPE, &value);
    if (!(value & GLX_WINDOW_BIT)) {
      GST_DEBUG ("GLX_DRAWABLE_TYPE doesn't have GLX_WINDOW_BIT set");
      continue;
    }

    glXGetFBConfigAttrib (display->display, fbconfigs[i],
        GLX_BIND_TO_TEXTURE_TARGETS_EXT, &value);
    if (!(value & GLX_TEXTURE_2D_BIT_EXT)) {
      GST_DEBUG
          ("GLX_BIND_TO_TEXTURE_TARGETS_EXT doesn't have GLX_TEXTURE_2D_BIT_EXT set");
      continue;
    }

    glXGetFBConfigAttrib (display->display, fbconfigs[i],
        GLX_BIND_TO_TEXTURE_RGBA_EXT, &value);
    GST_DEBUG ("GLX_BIND_TO_TEXTURE_RGBA_EXT %d", value);

  }

  fb_index = 0;

#if 0
  {
    pb = glXCreatePbuffer (display->display, fbconfigs[fb_index], attribs);
  }
#endif

  XSync (display->display, False);
  visinfo = glXChooseVisual (display->display, 0, attrib);
  glxpixmap = glXCreateGLXPixmap (display->display, visinfo, outbuf->pixmap);

  XSync (display->display, False);

  glXMakeCurrent (display->display, glxpixmap, context);

  glXMakeCurrent (display->display, None, NULL);
  gst_gl_display_unlock (display);

  return TRUE;
}
