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
#include <string.h>

#define GST_CAT_DEFAULT gst_gl_filter_example_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define GST_TYPE_GL_FILTER_EXAMPLE            (gst_gl_filter_example_get_type())
#define GST_GL_FILTER_EXAMPLE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_FILTER_EXAMPLE,GstGLFilterExample))
#define GST_IS_GL_FILTER_EXAMPLE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_FILTER_EXAMPLE))
#define GST_GL_FILTER_EXAMPLE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_GL_FILTER_EXAMPLE,GstGLFilterExampleClass))
#define GST_IS_GL_FILTER_EXAMPLE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_GL_FILTER_EXAMPLE))
#define GST_GL_FILTER_EXAMPLE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_GL_FILTER_EXAMPLE,GstGLFilterExampleClass))
typedef struct _GstGLFilterExample GstGLFilterExample;
typedef struct _GstGLFilterExampleClass GstGLFilterExampleClass;

struct _GstGLFilterExample
{
  GstGLFilter filter;

  /* < private > */

};

struct _GstGLFilterExampleClass
{
  GstGLFilterClass filter_class;
};

static const GstElementDetails element_details = GST_ELEMENT_DETAILS ("FIXME",
    "Filter/Effect",
    "FIXME example filter",
    "FIXME <fixme@fixme.com>");

#if 0
#define GST_GL_VIDEO_CAPS "video/x-raw-gl"

static GstStaticPadTemplate gst_gl_filter_example_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_VIDEO_CAPS)
    );

static GstStaticPadTemplate gst_gl_filter_example_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_VIDEO_CAPS)
    );
#endif

enum
{
  PROP_0
};

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_gl_filter_example_debug, "glfilterexample", 0, "glfilterexample element");

GST_BOILERPLATE_FULL (GstGLFilterExample, gst_gl_filter_example, GstGLFilter,
    GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_filter_example_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_filter_example_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_gl_filter_example_reset (GstGLFilterExample * filter);
static gboolean gst_gl_filter_example_transform (GstGLFilter * filter,
    GstGLBuffer * outbuf, GstGLBuffer * inbuf);
static gboolean gst_gl_filter_example_start (GstGLFilter * filter);
static gboolean gst_gl_filter_example_stop (GstGLFilter * filter);


static void
gst_gl_filter_example_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &element_details);

#if 0
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_filter_example_src_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_filter_example_sink_pad_template));
#endif
}

static void
gst_gl_filter_example_class_init (GstGLFilterExampleClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_gl_filter_example_set_property;
  gobject_class->get_property = gst_gl_filter_example_get_property;

  GST_GL_FILTER_CLASS (klass)->transform = gst_gl_filter_example_transform;
  GST_GL_FILTER_CLASS (klass)->start = gst_gl_filter_example_start;
  GST_GL_FILTER_CLASS (klass)->stop = gst_gl_filter_example_stop;
}

static void
gst_gl_filter_example_init (GstGLFilterExample * filter,
    GstGLFilterExampleClass * klass)
{
  gst_gl_filter_example_reset (filter);
}

static void
gst_gl_filter_example_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  //GstGLFilterExample *filter = GST_GL_FILTER_EXAMPLE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_filter_example_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  //GstGLFilterExample *filter = GST_GL_FILTER_EXAMPLE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_filter_example_reset (GstGLFilterExample * filter)
{

}

static gboolean
gst_gl_filter_example_start (GstGLFilter * _filter)
{
  //GstGLFilterExample *filter = GST_GL_FILTER_EXAMPLE(_filter);

  return TRUE;
}

static gboolean
gst_gl_filter_example_stop (GstGLFilter * _filter)
{
  GstGLFilterExample *filter = GST_GL_FILTER_EXAMPLE (_filter);

  gst_gl_filter_example_reset (filter);
  return TRUE;
}

static gboolean
gst_gl_filter_example_transform (GstGLFilter * filter, GstGLBuffer * outbuf,
    GstGLBuffer * inbuf)
{
  //GstGLFilterExample *example = GST_GL_FILTER_EXAMPLE(filter);
  int i, j;
  double *vertex_x, *vertex_y;

  glDisable (GL_CULL_FACE);
  glEnableClientState (GL_TEXTURE_COORD_ARRAY);

  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP);
  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  glColor4f (1, 0, 1, 1);

#define GAIN 0.5
  /* just for fun.  swap red and blue components.  Doesn't work on my
   * driver. */
  {
    const double matrix[16] = {
      0, 0, 1.0, 0,
      0, 1.0, 0, 0,
      1.0, 0, 0, 0,
      0, 0, 0, 1
    };

    glMatrixMode (GL_COLOR);
    glLoadMatrixd (matrix);
    glPixelTransferf (GL_POST_COLOR_MATRIX_RED_BIAS, (1 - GAIN) / 2);
    glPixelTransferf (GL_POST_COLOR_MATRIX_GREEN_BIAS, (1 - GAIN) / 2);
    glPixelTransferf (GL_POST_COLOR_MATRIX_BLUE_BIAS, (1 - GAIN) / 2);
  }

  /* load raster-scanning matrix */
  {
    const double matrix[16] = {
      2.0, 0, 0, 0,
      0, 2.0, 0, 0,
      0, 0, 1, 0,
      -1, -1, 0, 1
    };
    glMatrixMode (GL_MODELVIEW);
    glLoadMatrixd (matrix);
  }
  /* load texture raster-scanning matrix */
  {
    double matrix[16] = {
      1.0, 0, 0, 0,
      0, 1.0, 0, 0,
      0, 0, 1, 0,
      -1, -1, 0, 1
    };
    matrix[0] = inbuf->width;
    matrix[5] = inbuf->height;
    glMatrixMode (GL_TEXTURE);
    glLoadMatrixd (matrix);
  }

#define N 10
#define SCALE (1.0/N)
#define NOISE() (0.1*SCALE*g_random_double_range(-1,1))
  vertex_x = malloc (sizeof (double) * (N + 1) * (N + 1));
  vertex_y = malloc (sizeof (double) * (N + 1) * (N + 1));
  for (j = 0; j < N + 1; j++) {
    for (i = 0; i < N + 1; i++) {
      vertex_x[j * (N + 1) + i] = i * SCALE + NOISE ();
      vertex_y[j * (N + 1) + i] = j * SCALE + NOISE ();
    }
  }
  for (j = 0; j < N; j++) {
    for (i = 0; i < N; i++) {
      glBegin (GL_QUADS);
      glNormal3f (0, 0, -1);
      glTexCoord2f (i * SCALE, j * SCALE);
      glVertex3f (vertex_x[j * (N + 1) + i], vertex_y[j * (N + 1) + i], 0);
      glTexCoord2f ((i + 1) * SCALE, j * SCALE);
      glVertex3f (vertex_x[j * (N + 1) + (i + 1)],
          vertex_y[j * (N + 1) + (i + 1)], 0);
      glTexCoord2f ((i + 1) * SCALE, (j + 1) * SCALE);
      glVertex3f (vertex_x[(j + 1) * (N + 1) + (i + 1)],
          vertex_y[(j + 1) * (N + 1) + (i + 1)], 0);
      glTexCoord2f (i * SCALE, (j + 1) * SCALE);
      glVertex3f (vertex_x[(j + 1) * (N + 1) + i],
          vertex_y[(j + 1) * (N + 1) + i], 0);
      glEnd ();
    }
  }
  free (vertex_x);
  free (vertex_y);


  glFlush ();

  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();
  glMatrixMode (GL_TEXTURE);
  glLoadIdentity ();
  glMatrixMode (GL_COLOR);
  glLoadIdentity ();
  glPixelTransferf (GL_POST_COLOR_MATRIX_RED_SCALE, 1.0);
  glPixelTransferf (GL_POST_COLOR_MATRIX_RED_BIAS, 0);
  glPixelTransferf (GL_POST_COLOR_MATRIX_GREEN_BIAS, 0);
  glPixelTransferf (GL_POST_COLOR_MATRIX_BLUE_BIAS, 0);

  return TRUE;
}
