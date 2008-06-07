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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglfiltercube.h"

#define GST_CAT_DEFAULT gst_gl_filter_cube_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static const GstElementDetails element_details = 
    GST_ELEMENT_DETAILS ("OpenGL cube filter",
        "Filter/Effect",
        "Put input texture on the cube faces",
        "Julien Isorce <julien.isorce@gmail.com>");

enum
{
  PROP_0
};

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_gl_filter_cube_debug, "glfiltercube", 0, "glfiltercube element");

GST_BOILERPLATE_FULL (GstGLFilterCube, gst_gl_filter_cube, GstGLFilter,
    GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_filter_cube_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_filter_cube_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gl_filter_cube_filter (GstGLFilter * filter,
    GstGLBuffer * inbuf, GstGLBuffer * outbuf);


static void
gst_gl_filter_cube_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &element_details);
}

static void
gst_gl_filter_cube_class_init (GstGLFilterCubeClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_gl_filter_cube_set_property;
  gobject_class->get_property = gst_gl_filter_cube_get_property;

  GST_GL_FILTER_CLASS (klass)->filter = gst_gl_filter_cube_filter;
}

static void
gst_gl_filter_cube_init (GstGLFilterCube * filter,
    GstGLFilterCubeClass * klass)
{
}

static void
gst_gl_filter_cube_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  //GstGLFilterCube *filter = GST_GL_FILTER_CUBE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_filter_cube_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  //GstGLFilterCube *filter = GST_GL_FILTER_CUBE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_filter_cube_filter (GstGLFilter * filter, GstGLBuffer * inbuf,
    GstGLBuffer * outbuf)
{
  GstGLFilterCube* cube = GST_GL_FILTER_CUBE(filter);

  g_print ("gstglfiltercube: gst_gl_filter_cube_filter\n");
  /*int i, j;
  double *vertex_x, *vertex_y;

  glDisable (GL_CULL_FACE);
  glEnableClientState (GL_TEXTURE_COORD_ARRAY);

  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP);
  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  glColor4f (1, 0, 1, 1);



    glMatrixMode (GL_COLOR);
    glLoadMatrixd (matrix);
    glPixelTransferf (GL_POST_COLOR_MATRIX_RED_BIAS, (1 - GAIN) / 2);
    glPixelTransferf (GL_POST_COLOR_MATRIX_GREEN_BIAS, (1 - GAIN) / 2);
    glPixelTransferf (GL_POST_COLOR_MATRIX_BLUE_BIAS, (1 - GAIN) / 2);
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
  glPixelTransferf (GL_POST_COLOR_MATRIX_BLUE_BIAS, 0);*/

  return TRUE;
}
