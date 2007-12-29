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
#include <gstglfilter.h>
#include "glextensions.h"

#define GST_CAT_DEFAULT gst_gl_convert_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define GST_TYPE_GL_CONVERT            (gst_gl_convert_get_type())
#define GST_GL_CONVERT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_CONVERT,GstGLConvert))
#define GST_IS_GL_CONVERT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_CONVERT))
#define GST_GL_CONVERT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_GL_CONVERT,GstGLConvertClass))
#define GST_IS_GL_CONVERT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_GL_CONVERT))
#define GST_GL_CONVERT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_GL_CONVERT,GstGLConvertClass))
typedef struct _GstGLConvert GstGLConvert;
typedef struct _GstGLConvertClass GstGLConvertClass;

struct _GstGLConvert
{
  GstGLFilter filter;

  /* < private > */

};

struct _GstGLConvertClass
{
  GstGLFilterClass filter_class;
};

static const GstElementDetails element_details = GST_ELEMENT_DETAILS ("FIXME",
    "Filter/Effect",
    "FIXME GL conversion filter",
    "FIXME <fixme@fixme.com>");

enum
{
  PROP_0
};

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_gl_convert_debug, "glconvert", 0, "glconvert element");

GST_BOILERPLATE_FULL (GstGLConvert, gst_gl_convert, GstGLFilter,
    GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_convert_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_convert_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gl_convert_filter (GstGLFilter * filter,
    GstGLBuffer * inbuf, GstGLBuffer * outbuf);


static void
gst_gl_convert_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &element_details);
}

static void
gst_gl_convert_class_init (GstGLConvertClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_gl_convert_set_property;
  gobject_class->get_property = gst_gl_convert_get_property;

  GST_GL_FILTER_CLASS (klass)->filter = gst_gl_convert_filter;
}

static void
gst_gl_convert_init (GstGLConvert * filter, GstGLConvertClass * klass)
{
}

static void
gst_gl_convert_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  //GstGLConvert *filter = GST_GL_CONVERT (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_convert_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  //GstGLConvert *filter = GST_GL_CONVERT (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_convert_filter (GstGLFilter * filter, GstGLBuffer * inbuf,
    GstGLBuffer * outbuf)
{
  //GstGLConvert *convert = GST_GL_CONVERT(filter);

  glDisable (GL_CULL_FACE);
  glEnableClientState (GL_TEXTURE_COORD_ARRAY);

  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP);
  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  if (inbuf->is_yuv) {
#ifdef GL_POST_COLOR_MATRIX_RED_BIAS
    const double matrix[16] = {
      1.16438, 1.6321, -0.00107909, 0,
      1.13839, -0.813005, -0.39126, 0,
      1.13839, 0.00112726, 2.01741, 0,
      0, 0, 0, 1
    };

    GST_DEBUG ("applying YUV->RGB conversion");

    glMatrixMode (GL_COLOR);
    glLoadMatrixd (matrix);

    /* same */
    glPixelTransferf (GL_POST_COLOR_MATRIX_RED_BIAS, -0.873494);
    glPixelTransferf (GL_POST_COLOR_MATRIX_GREEN_BIAS, 0.531435);
    glPixelTransferf (GL_POST_COLOR_MATRIX_BLUE_BIAS, -1.08629);
#else
    g_assert_not_reached ();
#endif
  }

  glColor4f (1, 0, 1, 1);

  glBegin (GL_QUADS);
  glNormal3f (0, 0, -1);
  glTexCoord2f (inbuf->width, 0);
  glVertex3f (1.0, -1.0, 0);
  glTexCoord2f (0, 0);
  glVertex3f (-1.0, -1.0, 0);
  glTexCoord2f (0, inbuf->height);
  glVertex3f (-1.0, 1.0, 0);
  glTexCoord2f (inbuf->width, inbuf->height);
  glVertex3f (1.0, 1.0, 0);
  glEnd ();

  if (inbuf->is_yuv) {
#ifdef GL_POST_COLOR_MATRIX_RED_BIAS
    glMatrixMode (GL_COLOR);
    glLoadIdentity ();

    glPixelTransferf (GL_POST_COLOR_MATRIX_RED_BIAS, 0);
    glPixelTransferf (GL_POST_COLOR_MATRIX_GREEN_BIAS, 0);
    glPixelTransferf (GL_POST_COLOR_MATRIX_BLUE_BIAS, 0);
#else
    g_assert_not_reached ();
#endif
  }

  return TRUE;
}
