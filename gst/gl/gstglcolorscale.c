/*
 * GStreamer
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
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
 * SECTION:element-glcolorscale
 *
 * video frame scaling and colorspace conversion.
 *
 * <refsect2>
 * <title>Scaling and Color space conversion</title>
 * <para>
 * Equivalent to glupload ! gldownload.
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch -v videotestsrc ! "video/x-raw-yuv" ! glcolorscale ! ximagesink
 * ]| A pipeline to test colorspace conversion.
 * FBO is required.
  |[
 * gst-launch -v videotestsrc ! "video/x-raw-yuv, width=640, height=480, format=(fourcc)AYUV" ! glcolorscale ! \
 *   "video/x-raw-yuv, width=320, height=240, format=(fourcc)YV12" ! autovideosink
 * ]| A pipeline to test hardware scaling and colorspace conversion.
 * FBO and GLSL are required.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglcolorscale.h"


#define GST_CAT_DEFAULT gst_gl_colorscale_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

/* Properties */
enum
{
  PROP_0
};

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_colorscale_debug, "glcolorscale", 0, "glcolorscale element");

G_DEFINE_TYPE_WITH_CODE (GstGLColorscale, gst_gl_colorscale,
    GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_colorscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_colorscale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gl_colorscale_filter_texture (GstGLFilter * filter,
    guint in_tex, guint out_tex);
static void gst_gl_colorscale_callback (gint width, gint height,
    guint texture, gpointer stuff);

static void
gst_gl_colorscale_class_init (GstGLColorscaleClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstGLFilterClass *filter_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);
  filter_class = GST_GL_FILTER_CLASS (klass);

  gobject_class->set_property = gst_gl_colorscale_set_property;
  gobject_class->get_property = gst_gl_colorscale_get_property;

  gst_element_class_set_metadata (element_class, "OpenGL color scale",
      "Filter/Effect", "Colorspace converter and video scaler",
      "Julien Isorce <julien.isorce@gmail.com>");

  filter_class->filter_texture = gst_gl_colorscale_filter_texture;
}

static void
gst_gl_colorscale_init (GstGLColorscale * colorscale)
{
}

static void
gst_gl_colorscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_colorscale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_colorscale_filter_texture (GstGLFilter * filter, guint in_tex,
    guint out_tex)
{
  GstGLColorscale *colorscale;

  colorscale = GST_GL_COLORSCALE (filter);

  gst_gl_filter_render_to_target (filter, TRUE, in_tex, out_tex,
      gst_gl_colorscale_callback, colorscale);

  return TRUE;
}

static void
gst_gl_colorscale_callback (gint width, gint height, guint texture,
    gpointer stuff)
{
  GstGLFilter *filter = GST_GL_FILTER (stuff);

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  gst_gl_filter_draw_texture (filter, texture, width, height);
}
