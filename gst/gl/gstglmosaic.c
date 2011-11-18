/*
 * GStreamer
 * Copyright (C) 2009 Julien Isorce <julien.isorce@gmail.com>
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
 * SECTION:element-glmosaic
 *
 * glmixer sub element. N gl sink pads to 1 source pad.
 * N + 1 OpenGL contexts shared together.
 * N <= 6 because the rendering is more a like a cube than a mosaic
 * Each opengl input stream is rendered on a cube face
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch-0.10 videotestsrc ! "video/x-raw-yuv, format=(fourcc)YUY2" ! glupload ! queue ! glmosaic name=m ! glimagesink videotestsrc pattern=12 ! "video/x-raw-yuv, format=(fourcc)I420, framerate=(fraction)5/1, width=100, height=200" ! glupload ! queue ! m. videotestsrc ! "video/x-raw-rgb, framerate=(fraction)15/1, width=1500, height=1500" ! glupload ! gleffects effect=3 ! queue ! m. videotestsrc ! glupload ! gleffects effect=2 ! queue ! m.  videotestsrc ! glupload ! glfiltercube ! queue ! m. videotestsrc ! glupload ! gleffects effect=6 ! queue ! m.
 * ]|
 * FBO (Frame Buffer Object) is required.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglmosaic.h"

#define GST_CAT_DEFAULT gst_gl_mosaic_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

enum
{
  PROP_0,
};

#define DEBUG_INIT(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_gl_mosaic_debug, "glmosaic", 0, "glmosaic element");

GST_BOILERPLATE_FULL (GstGLMosaic, gst_gl_mosaic, GstGLMixer,
    GST_TYPE_GL_MIXER, DEBUG_INIT);

static void gst_gl_mosaic_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_mosaic_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_gl_mosaic_reset (GstGLMixer * mixer);
static gboolean gst_gl_mosaic_init_shader (GstGLMixer * mixer,
    GstCaps * outcaps);

static gboolean gst_gl_mosaic_proc (GstGLMixer * mixer,
    GPtrArray * buffers, GstBuffer * outbuf);
static void gst_gl_mosaic_callback (gpointer stuff);

//vertex source
static const gchar *mosaic_v_src =
    "#extension GL_ARB_texture_rectangle : enable\n"
    "uniform mat4 u_matrix;                                       \n"
    "uniform float xrot_degree, yrot_degree, zrot_degree;         \n"
    "attribute vec4 a_position;                                   \n"
    "attribute vec2 a_texCoord;                                   \n"
    "varying vec2 v_texCoord;                                     \n"
    "void main()                                                  \n"
    "{                                                            \n"
    "   float PI = 3.14159265;                                    \n"
    "   float xrot = xrot_degree*2.0*PI/360.0;                    \n"
    "   float yrot = yrot_degree*2.0*PI/360.0;                    \n"
    "   float zrot = zrot_degree*2.0*PI/360.0;                    \n"
    "   mat4 matX = mat4 (                                        \n"
    "            1.0,        0.0,        0.0, 0.0,                \n"
    "            0.0,  cos(xrot),  sin(xrot), 0.0,                \n"
    "            0.0, -sin(xrot),  cos(xrot), 0.0,                \n"
    "            0.0,        0.0,        0.0, 1.0 );              \n"
    "   mat4 matY = mat4 (                                        \n"
    "      cos(yrot),        0.0, -sin(yrot), 0.0,                \n"
    "            0.0,        1.0,        0.0, 0.0,                \n"
    "      sin(yrot),        0.0,  cos(yrot), 0.0,                \n"
    "            0.0,        0.0,       0.0,  1.0 );              \n"
    "   mat4 matZ = mat4 (                                        \n"
    "      cos(zrot),  sin(zrot),        0.0, 0.0,                \n"
    "     -sin(zrot),  cos(zrot),        0.0, 0.0,                \n"
    "            0.0,        0.0,        1.0, 0.0,                \n"
    "            0.0,        0.0,        0.0, 1.0 );              \n"
    "   gl_Position = u_matrix * matZ * matY * matX * a_position; \n"
    "   v_texCoord = a_texCoord;                                  \n"
    "}                                                            \n";

//fragment source
static const gchar *mosaic_f_src =
    "#extension GL_ARB_texture_rectangle : enable\n"
    "uniform sampler2DRect s_texture;                    \n"
    "varying vec2 v_texCoord;                            \n"
    "void main()                                         \n"
    "{                                                   \n"
    //"  gl_FragColor = vec4( 1.0, 0.5, 1.0, 1.0 );\n"
    "  gl_FragColor = texture2DRect( s_texture, v_texCoord );\n"
    "}                                                   \n";

static void
gst_gl_mosaic_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class, "OpenGL mosaic",
      "Filter/Effect", "OpenGL mosaic",
      "Julien Isorce <julien.isorce@gmail.com>");
}

static void
gst_gl_mosaic_class_init (GstGLMosaicClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_gl_mosaic_set_property;
  gobject_class->get_property = gst_gl_mosaic_get_property;

  GST_GL_MIXER_CLASS (klass)->set_caps = gst_gl_mosaic_init_shader;
  GST_GL_MIXER_CLASS (klass)->reset = gst_gl_mosaic_reset;
  GST_GL_MIXER_CLASS (klass)->process_buffers = gst_gl_mosaic_proc;
}

static void
gst_gl_mosaic_init (GstGLMosaic * mosaic, GstGLMosaicClass * klass)
{
  mosaic->shader = NULL;
  mosaic->input_gl_buffers = NULL;
}

static void
gst_gl_mosaic_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  //GstGLMosaic *mixer = GST_GL_MOSAIC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_mosaic_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  //GstGLMosaic* mixer = GST_GL_MOSAIC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_mosaic_reset (GstGLMixer * mixer)
{
  GstGLMosaic *mosaic = GST_GL_MOSAIC (mixer);

  mosaic->input_gl_buffers = NULL;

  //blocking call, wait the opengl thread has destroyed the shader
  gst_gl_display_del_shader (mixer->display, mosaic->shader);
}

static gboolean
gst_gl_mosaic_init_shader (GstGLMixer * mixer, GstCaps * outcaps)
{
  GstGLMosaic *mosaic = GST_GL_MOSAIC (mixer);

  //blocking call, wait the opengl thread has compiled the shader
  gst_gl_display_gen_shader (mixer->display, mosaic_v_src, mosaic_f_src,
      &mosaic->shader);

  return TRUE;
}

static gboolean
gst_gl_mosaic_proc (GstGLMixer * mix, GPtrArray * buffers, GstBuffer * outbuf)
{
  GstGLMosaic *mosaic = GST_GL_MOSAIC (mix);
  GstGLBuffer *gl_out_buffer = GST_GL_BUFFER (outbuf);

  mosaic->input_gl_buffers = buffers;

  //blocking call, use a FBO
  gst_gl_display_use_fbo_v2 (mix->display, mix->width, mix->height,
      mix->fbo, mix->depthbuffer, gl_out_buffer->texture,
      gst_gl_mosaic_callback, (gpointer) mosaic);

  return TRUE;
}

//opengl scene, params: input texture (not the output mixer->texture)
static void
gst_gl_mosaic_callback (gpointer stuff)
{
  GstGLMosaic *mosaic = GST_GL_MOSAIC (stuff);

  static GLfloat xrot = 0;
  static GLfloat yrot = 0;
  static GLfloat zrot = 0;

  GLint attr_position_loc = 0;
  GLint attr_texture_loc = 0;

  const GLfloat matrix[] = {
    0.5f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.5f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.5f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
  };

  const GLushort indices[] = {
    0, 1, 2,
    0, 2, 3
  };

  guint count = 0;
  gboolean do_next = TRUE;

  gst_gl_shader_use (NULL);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);
  glDisable (GL_TEXTURE_RECTANGLE_ARB);

  glEnable (GL_DEPTH_TEST);

  glClearColor (0.0, 0.0, 0.0, 0.0);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  gst_gl_shader_use (mosaic->shader);

  attr_position_loc =
      gst_gl_shader_get_attribute_location (mosaic->shader, "a_position");
  attr_texture_loc =
      gst_gl_shader_get_attribute_location (mosaic->shader, "a_texCoord");

  while (do_next && count < mosaic->input_gl_buffers->len && count < 6) {
    GstGLBuffer *gl_in_buffer =
        g_ptr_array_index (mosaic->input_gl_buffers, count);

    if (gl_in_buffer && gl_in_buffer->texture) {
      GLuint texture = gl_in_buffer->texture;
      GLfloat width = (GLfloat) gl_in_buffer->width;
      GLfloat height = (GLfloat) gl_in_buffer->height;

      const GLfloat v_vertices[] = {

        //front face
        1.0f, 1.0f, -1.0f,
        width, 0.0f,
        1.0f, -1.0f, -1.0f,
        width, height,
        -1.0f, -1.0f, -1.0f,
        0.0f, height,
        -1.0f, 1.0f, -1.0f,
        0.0f, 0.0f,
        //right face
        1.0f, 1.0f, 1.0f,
        width, 0.0f,
        1.0f, -1.0f, 1.0f,
        0.0f, 0.0f,
        1.0f, -1.0f, -1.0f,
        0.0f, height,
        1.0f, 1.0f, -1.0f,
        width, height,
        //left face
        -1.0f, 1.0f, 1.0f,
        width, 0.0f,
        -1.0f, 1.0f, -1.0f,
        width, height,
        -1.0f, -1.0f, -1.0f,
        0.0f, height,
        -1.0f, -1.0f, 1.0f,
        0.0f, 0.0f,
        //top face
        1.0f, -1.0f, 1.0f,
        width, 0.0f,
        -1.0f, -1.0f, 1.0f,
        0.0f, 0.0f,
        -1.0f, -1.0f, -1.0f,
        0.0f, height,
        1.0f, -1.0f, -1.0f,
        width, height,
        //bottom face
        1.0f, 1.0f, 1.0f,
        width, 0.0f,
        1.0f, 1.0f, -1.0f,
        width, height,
        -1.0f, 1.0f, -1.0f,
        0.0f, height,
        -1.0f, 1.0f, 1.0f,
        0.0f, 0.0f,
        //back face
        1.0f, 1.0f, 1.0f,
        width, 0.0f,
        -1.0f, 1.0f, 1.0f,
        0.0f, 0.0f,
        -1.0f, -1.0f, 1.0f,
        0.0f, height,
        1.0f, -1.0f, 1.0f,
        width, height
      };

      glVertexAttribPointerARB (attr_position_loc, 3, GL_FLOAT,
          GL_FALSE, 5 * sizeof (GLfloat), &v_vertices[5 * 4 * count]);

      glVertexAttribPointerARB (attr_texture_loc, 2, GL_FLOAT,
          GL_FALSE, 5 * sizeof (GLfloat), &v_vertices[5 * 4 * count + 3]);

      glEnableVertexAttribArrayARB (attr_position_loc);
      glEnableVertexAttribArrayARB (attr_texture_loc);

      glActiveTextureARB (GL_TEXTURE0_ARB);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
      gst_gl_shader_set_uniform_1i (mosaic->shader, "s_texture", 0);
      gst_gl_shader_set_uniform_1f (mosaic->shader, "xrot_degree", xrot);
      gst_gl_shader_set_uniform_1f (mosaic->shader, "yrot_degree", yrot);
      gst_gl_shader_set_uniform_1f (mosaic->shader, "zrot_degree", zrot);
      gst_gl_shader_set_uniform_matrix_4fv (mosaic->shader, "u_matrix", 1,
          GL_FALSE, matrix);

      glDrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);

      ++count;
    } else {
      do_next = FALSE;
    }
  }

  glDisableVertexAttribArrayARB (attr_position_loc);
  glDisableVertexAttribArrayARB (attr_texture_loc);

  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);

  glDisable (GL_DEPTH_TEST);

  gst_gl_shader_use (NULL);

  xrot += 0.6f;
  yrot += 0.4f;
  zrot += 0.8f;
}
