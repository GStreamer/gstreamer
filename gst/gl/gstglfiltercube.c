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

/**
 * SECTION:element-glfiltercube
 *
 * The resize and redraw callbacks can be set from a client code.
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch -v videotestsrc ! glupload ! glfiltercube ! glimagesink
 * ]| A pipeline to mpa textures on the 6 cube faces..
 * FBO is required.
 * |[
 * gst-launch -v videotestsrc ! glupload ! glfiltercube ! video/x-raw-gl, width=640, height=480 ! glimagesink
 * ]| Resize scene after drawing the cube.
 * The scene size is greater than the input video size.
  |[
 * gst-launch -v videotestsrc ! glupload ! video/x-raw-gl, width=640, height=480  ! glfiltercube ! glimagesink
 * ]| Resize scene before drawing the cube.
 * The scene size is greater than the input video size.
 * </refsect2>
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
    "Map input texture on the 6 cube faces",
    "Julien Isorce <julien.isorce@gmail.com>");

enum
{
  PROP_0,
  PROP_RED,
  PROP_GREEN,
  PROP_BLUE,
  PROP_FOVY,
  PROP_ASPECT,
  PROP_ZNEAR,
  PROP_ZFAR
};

#define DEBUG_INIT(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_gl_filter_cube_debug, "glfiltercube", 0, "glfiltercube element");

GST_BOILERPLATE_FULL (GstGLFilterCube, gst_gl_filter_cube, GstGLFilter,
    GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_filter_cube_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_filter_cube_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gl_filter_cube_set_caps (GstGLFilter * filter,
    GstCaps * incaps, GstCaps * outcaps);
static gboolean gst_gl_filter_cube_filter (GstGLFilter * filter,
    GstGLBuffer * inbuf, GstGLBuffer * outbuf);
static void gst_gl_filter_cube_callback (gint width, gint height, guint texture,
    gpointer stuff);


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

  GST_GL_FILTER_CLASS (klass)->set_caps = gst_gl_filter_cube_set_caps;
  GST_GL_FILTER_CLASS (klass)->filter = gst_gl_filter_cube_filter;

  g_object_class_install_property (gobject_class, PROP_RED,
      g_param_spec_float ("red", "Red", "Background red color",
          0.0f, 1.0f, 0.0f, G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, PROP_GREEN,
      g_param_spec_float ("green", "Green", "Background reen color",
          0.0f, 1.0f, 0.0f, G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, PROP_BLUE,
      g_param_spec_float ("blue", "Blue", "Background blue color",
          0.0f, 1.0f, 0.0f, G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, PROP_FOVY,
      g_param_spec_double ("fovy", "Fovy", "Field of view angle in degrees",
          0.0, 180.0, 45.0, G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, PROP_ASPECT,
      g_param_spec_double ("aspect", "Aspect",
          "Field of view in the x direction", 0.0, 100, 0.0, G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, PROP_ZNEAR,
      g_param_spec_double ("znear", "Znear",
          "Specifies the	distance from the viewer to the	near clipping plane",
          0.0, 100.0, 0.1, G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, PROP_ZFAR,
      g_param_spec_double ("zfar", "Zfar",
          "Specifies the	distance from the viewer to the	far clipping plane",
          0.0, 1000.0, 100.0, G_PARAM_WRITABLE));
}

static void
gst_gl_filter_cube_init (GstGLFilterCube * filter, GstGLFilterCubeClass * klass)
{
  filter->fovy = 45;
  filter->aspect = 0;
  filter->znear = 0.1;
  filter->zfar = 100;
}

static void
gst_gl_filter_cube_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLFilterCube *filter = GST_GL_FILTER_CUBE (object);

  switch (prop_id) {
    case PROP_RED:
      filter->red = g_value_get_float (value);
      break;
    case PROP_GREEN:
      filter->green = g_value_get_float (value);
      break;
    case PROP_BLUE:
      filter->blue = g_value_get_float (value);
      break;
    case PROP_FOVY:
      filter->fovy = g_value_get_double (value);
      break;
    case PROP_ASPECT:
      filter->aspect = g_value_get_double (value);
      break;
    case PROP_ZNEAR:
      filter->znear = g_value_get_double (value);
      break;
    case PROP_ZFAR:
      filter->zfar = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_filter_cube_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  //GstGLFilterCube* filter = GST_GL_FILTER_CUBE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_filter_cube_set_caps (GstGLFilter * filter, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstGLFilterCube *cube_filter = GST_GL_FILTER_CUBE (filter);

  if (cube_filter->aspect == 0)
    cube_filter->aspect = (gdouble) filter->width / (gdouble) filter->height;

  return TRUE;
}

static gboolean
gst_gl_filter_cube_filter (GstGLFilter * filter, GstGLBuffer * inbuf,
    GstGLBuffer * outbuf)
{
  GstGLFilterCube *cube_filter = GST_GL_FILTER_CUBE (filter);

  //blocking call, use a FBO
  gst_gl_display_use_fbo (filter->display, filter->width, filter->height,
      filter->fbo, filter->depthbuffer, outbuf->texture,
      gst_gl_filter_cube_callback, inbuf->width, inbuf->height, inbuf->texture,
      cube_filter->fovy, cube_filter->aspect, cube_filter->znear,
      cube_filter->zfar, GST_GL_DISPLAY_PROJECTION_PERSPECIVE,
      (gpointer) cube_filter);

  return TRUE;
}

//opengl scene, params: input texture (not the output filter->texture)
static void
gst_gl_filter_cube_callback (gint width, gint height, guint texture,
    gpointer stuff)
{
  static GLfloat xrot = 0;
  static GLfloat yrot = 0;
  static GLfloat zrot = 0;

  GstGLFilterCube *cube_filter = GST_GL_FILTER_CUBE (stuff);

  glEnable (GL_DEPTH_TEST);

  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
      GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
      GL_CLAMP_TO_EDGE);
  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  glClearColor (cube_filter->red, cube_filter->green, cube_filter->blue, 0.0);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();

  glTranslatef (0.0f, 0.0f, -5.0f);

  glRotatef (xrot, 1.0f, 0.0f, 0.0f);
  glRotatef (yrot, 0.0f, 1.0f, 0.0f);
  glRotatef (zrot, 0.0f, 0.0f, 1.0f);

  glBegin (GL_QUADS);
  // Front Face
  glTexCoord2f ((gfloat) width, 0.0f);
  glVertex3f (-1.0f, -1.0f, 1.0f);
  glTexCoord2f (0.0f, 0.0f);
  glVertex3f (1.0f, -1.0f, 1.0f);
  glTexCoord2f (0.0f, (gfloat) height);
  glVertex3f (1.0f, 1.0f, 1.0f);
  glTexCoord2f ((gfloat) width, (gfloat) height);
  glVertex3f (-1.0f, 1.0f, 1.0f);
  // Back Face
  glTexCoord2f (0.0f, 0.0f);
  glVertex3f (-1.0f, -1.0f, -1.0f);
  glTexCoord2f (0.0f, (gfloat) height);
  glVertex3f (-1.0f, 1.0f, -1.0f);
  glTexCoord2f ((gfloat) width, (gfloat) height);
  glVertex3f (1.0f, 1.0f, -1.0f);
  glTexCoord2f ((gfloat) width, 0.0f);
  glVertex3f (1.0f, -1.0f, -1.0f);
  // Top Face
  glTexCoord2f ((gfloat) width, (gfloat) height);
  glVertex3f (-1.0f, 1.0f, -1.0f);
  glTexCoord2f ((gfloat) width, 0.0f);
  glVertex3f (-1.0f, 1.0f, 1.0f);
  glTexCoord2f (0.0f, 0.0f);
  glVertex3f (1.0f, 1.0f, 1.0f);
  glTexCoord2f (0.0f, (gfloat) height);
  glVertex3f (1.0f, 1.0f, -1.0f);
  // Bottom Face
  glTexCoord2f ((gfloat) width, 0.0f);
  glVertex3f (-1.0f, -1.0f, -1.0f);
  glTexCoord2f (0.0f, 0.0f);
  glVertex3f (1.0f, -1.0f, -1.0f);
  glTexCoord2f (0.0f, (gfloat) height);
  glVertex3f (1.0f, -1.0f, 1.0f);
  glTexCoord2f ((gfloat) width, (gfloat) height);
  glVertex3f (-1.0f, -1.0f, 1.0f);
  // Right face
  glTexCoord2f (0.0f, 0.0f);
  glVertex3f (1.0f, -1.0f, -1.0f);
  glTexCoord2f (0.0f, (gfloat) height);
  glVertex3f (1.0f, 1.0f, -1.0f);
  glTexCoord2f ((gfloat) width, (gfloat) height);
  glVertex3f (1.0f, 1.0f, 1.0f);
  glTexCoord2f ((gfloat) width, 0.0f);
  glVertex3f (1.0f, -1.0f, 1.0f);
  // Left Face
  glTexCoord2f ((gfloat) width, 0.0f);
  glVertex3f (-1.0f, -1.0f, -1.0f);
  glTexCoord2f (0.0f, 0.0f);
  glVertex3f (-1.0f, -1.0f, 1.0f);
  glTexCoord2f (0.0f, (gfloat) height);
  glVertex3f (-1.0f, 1.0f, 1.0f);
  glTexCoord2f ((gfloat) width, (gfloat) height);
  glVertex3f (-1.0f, 1.0f, -1.0f);
  glEnd ();

  xrot += 0.3f;
  yrot += 0.2f;
  zrot += 0.4f;

  glDisable (GL_DEPTH_TEST);
}
