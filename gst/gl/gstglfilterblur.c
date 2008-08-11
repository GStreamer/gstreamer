/*
 * GStreamer
 * Copyright (C) 2008 Filippo Argiolas <filippo.argiolas@gmail.com>
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

#include <gstglfilter.h>

#define GST_TYPE_GL_FILTERBLUR            (gst_gl_filterblur_get_type())
#define GST_GL_FILTERBLUR(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_FILTERBLUR,GstGLFilterBlur))
#define GST_IS_GL_FILTERBLUR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_FILTERBLUR))
#define GST_GL_FILTERBLUR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_GL_FILTERBLUR,GstGLFilterBlurClass))
#define GST_IS_GL_FILTERBLUR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_GL_FILTERBLUR))
#define GST_GL_FILTERBLUR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_GL_FILTERBLUR,GstGLFilterBlurClass))

typedef struct _GstGLFilterBlur GstGLFilterBlur;
typedef struct _GstGLFilterBlurClass GstGLFilterBlurClass;

struct _GstGLFilterBlur
{
  GstGLFilter filter;
  GstGLShader *shader0;
  GstGLShader *shader1;

  GLuint midtexture;
};

struct _GstGLFilterBlurClass
{
  GstGLFilterClass filter_class;
};

GType gst_gl_glfilterblur_get_type (void);

/* horizontal convolution */
static const gchar *hconv9_fragment_source =
"#extension GL_ARB_texture_rectangle : enable\n"
"uniform sampler2DRect tex;"
"uniform float norm_const;"
"uniform float norm_offset;"
"uniform float kernel[9];"
"void main () {"
"  float offset[9] = float[9] (-4.0, -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0, 4.0);"
"  vec2 texturecoord = gl_TexCoord[0].st;"
"  int i;"
"  vec4 sum = vec4 (0.0);"
"  for (i = 0; i < 9; i++) { "
"    if (kernel[i] != 0.0) {"
"        vec4 neighbor = texture2DRect(tex, vec2(texturecoord.s+offset[i], texturecoord.t)); "
"        sum += neighbor * kernel[i]/norm_const; "
"      }"
"  }"
"  gl_FragColor = sum + norm_offset;"
"}";

/* vertical convolution */
static const gchar *vconv9_fragment_source =
"#extension GL_ARB_texture_rectangle : enable\n"
"uniform sampler2DRect tex;"
"uniform float norm_const;"
"uniform float norm_offset;"
"uniform float kernel[9];"
"void main () {"
"  float offset[9] = float[9] (-4.0, -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0, 4.0);"
"  vec2 texturecoord = gl_TexCoord[0].st;"
"  int i;"
"  vec4 sum = vec4 (0.0);"
"  for (i = 0; i < 9; i++) { "
"    if (kernel[i] != 0.0) {"
"        vec4 neighbor = texture2DRect(tex, vec2(texturecoord.s, texturecoord.t+offset[i])); "
"        sum += neighbor * kernel[i]/norm_const; "
"      }"
"  }"
"  gl_FragColor = sum + norm_offset;"
"}";

#define GST_CAT_DEFAULT gst_gl_filterblur_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static const GstElementDetails element_details =
GST_ELEMENT_DETAILS ("Gstreamer OpenGL Blur",
    "Filter/Effect",
    "Blur with 9x9 separable convolution",
    "Filippo Argiolas <filippo.argiolas@gmail.com>");

#define DEBUG_INIT(bla)							\
  GST_DEBUG_CATEGORY_INIT (gst_gl_filterblur_debug, "glfilterblur", 0, "glfilterblur element");

GST_BOILERPLATE_FULL (GstGLFilterBlur, gst_gl_filterblur, GstGLFilter,
		      GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_filterblur_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_filterblur_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_gl_filter_filterblur_reset (GstGLFilter* filter);
static void gst_gl_filterblur_draw_texture (GstGLFilterBlur * filterblur, GLuint tex);

static void gst_gl_filterblur_init_shader (GstGLFilter* filter);
static gboolean gst_gl_filterblur_filter (GstGLFilter * filter,
                                           GstGLBuffer * inbuf, GstGLBuffer * outbuf);
static void gst_gl_filterblur_hcallback (gint width, gint height, guint texture, gpointer stuff);
static void gst_gl_filterblur_vcallback (gint width, gint height, guint texture, gpointer stuff);


static void
gst_gl_filterblur_init_resources (GstGLFilter *filter)
{
//  GstGLFilterBlur *filterblur = GST_GL_FILTERBLUR (filter);

  /* dummy gl call to test if we really are in a  gl context */
  g_print ("\nStart!!!: %s\n\n", glGetString (GL_VERSION));
}

static void
gst_gl_filterblur_reset_resources (GstGLFilter *filter)
{
//  GstGLFilterBlur *filterblur = GST_GL_FILTERBLUR (filter);

  /* dummy gl call to test if we really are in a  gl context */
  g_print ("\nStop!!!: %s\n\n", glGetString (GL_VENDOR));
}

static void
gst_gl_filterblur_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &element_details);
}

static void
gst_gl_filterblur_class_init (GstGLFilterBlurClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_gl_filterblur_set_property;
  gobject_class->get_property = gst_gl_filterblur_get_property;

  GST_GL_FILTER_CLASS (klass)->filter = gst_gl_filterblur_filter;
  GST_GL_FILTER_CLASS (klass)->display_init_cb = gst_gl_filterblur_init_resources;
  GST_GL_FILTER_CLASS (klass)->display_reset_cb = gst_gl_filterblur_reset_resources;
  GST_GL_FILTER_CLASS (klass)->onInitFBO = gst_gl_filterblur_init_shader;
  GST_GL_FILTER_CLASS (klass)->onReset = gst_gl_filter_filterblur_reset;
}

static void
gst_gl_filterblur_init (GstGLFilterBlur * filterblur, GstGLFilterBlurClass * klass)
{
  filterblur->shader0 = NULL;
  filterblur->shader1 = NULL;
  filterblur->midtexture = 0;
}

static void
gst_gl_filter_filterblur_reset (GstGLFilter* filter)
{
  GstGLFilterBlur* filterblur = GST_GL_FILTERBLUR(filter);

  //blocking call, wait the opengl thread has destroyed the shader
  gst_gl_display_del_shader (filter->display, filterblur->shader0);

  //blocking call, wait the opengl thread has destroyed the shader
  gst_gl_display_del_shader (filter->display, filterblur->shader1);

  //blocking call, put the texture in the pool
  gst_gl_display_del_texture (filter->display, filterblur->midtexture, filter->width, filter->height);
}

static void
gst_gl_filterblur_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  /* GstGLFilterBlur *filterblur = GST_GL_FILTERBLUR (object); */

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_filterblur_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  /* GstGLFilterBlur *filterblur = GST_GL_FILTERBLUR (object); */

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_filterblur_init_shader (GstGLFilter* filter)
{
  GstGLFilterBlur* blur_filter = GST_GL_FILTERBLUR (filter);

  //blocking call, generate a texture using the pool
  gst_gl_display_gen_texture (filter->display, &blur_filter->midtexture, filter->width, filter->height) ;

  //blocking call, wait the opengl thread has compiled the shader
  gst_gl_display_gen_shader (filter->display, hconv9_fragment_source, &blur_filter->shader0);

  //blocking call, wait the opengl thread has compiled the shader
  gst_gl_display_gen_shader (filter->display, vconv9_fragment_source, &blur_filter->shader1);
}

static void
gst_gl_filterblur_draw_texture (GstGLFilterBlur * filterblur, GLuint tex)
{
  GstGLFilter *filter = GST_GL_FILTER (filterblur);

  glActiveTexture (GL_TEXTURE0);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, tex);

  glBegin (GL_QUADS);

  glTexCoord2f (0.0, 0.0);
  glVertex2f (-1.0, -1.0);
  glTexCoord2f (filter->width, 0.0);
  glVertex2f (1.0, -1.0);
  glTexCoord2f (filter->width, filter->height);
  glVertex2f (1.0, 1.0);
  glTexCoord2f (0.0, filter->height);
  glVertex2f (-1.0, 1.0);

  glEnd ();
}

static void
change_view (GstGLDisplay *display, gpointer data)
{
//  GstGLFilterBlur *filterblur = GST_GL_FILTERBLUR (data);

  const double mirrormatrix[16] = {
    -1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.0, 0.0, 0.0, 1.0
  };

  glMatrixMode (GL_MODELVIEW);
  glLoadMatrixd (mirrormatrix);
}

static gboolean
gst_gl_filterblur_filter (GstGLFilter* filter, GstGLBuffer* inbuf,
				GstGLBuffer* outbuf)
{
  GstGLFilterBlur* filterblur = GST_GL_FILTERBLUR(filter);

  gst_gl_filter_render_to_target (filter, inbuf->texture, filterblur->midtexture,
				  gst_gl_filterblur_hcallback, filterblur);

  gst_gl_display_thread_add (filter->display, change_view, filterblur);

  gst_gl_filter_render_to_target (filter, filterblur->midtexture, outbuf->texture,
				  gst_gl_filterblur_vcallback, filterblur);

  return TRUE;
}

static void
gst_gl_filterblur_hcallback (gint width, gint height, guint texture, gpointer stuff)
{
  GstGLFilterBlur* filterblur = GST_GL_FILTERBLUR (stuff);

  /* hard coded kernel, it could be easily generated at runtime with a
   * property to change standard deviation */
  gfloat gauss_kernel[9] = {
    0.026995, 0.064759, 0.120985,
    0.176033, 0.199471, 0.176033,
    0.120985, 0.064759, 0.026995 };

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  gst_gl_shader_use (filterblur->shader0);

  glActiveTexture (GL_TEXTURE1);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
  glDisable (GL_TEXTURE_RECTANGLE_ARB);

  gst_gl_shader_set_uniform_1i (filterblur->shader0, "tex", 1);

  gst_gl_shader_set_uniform_1fv (filterblur->shader0, "kernel", 9, gauss_kernel);
  gst_gl_shader_set_uniform_1f (filterblur->shader0, "norm_const", 0.977016);
  gst_gl_shader_set_uniform_1f (filterblur->shader0, "norm_offset", 0.0);

  gst_gl_filterblur_draw_texture (filterblur, texture);
}


static void
gst_gl_filterblur_vcallback (gint width, gint height, guint texture, gpointer stuff)
{
  GstGLFilterBlur* filterblur = GST_GL_FILTERBLUR (stuff);

  /* hard coded kernel, it could be easily generated at runtime with a
   * property to change standard deviation */
  gfloat gauss_kernel[9] = {
    0.026995, 0.064759, 0.120985,
    0.176033, 0.199471, 0.176033,
    0.120985, 0.064759, 0.026995 };

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  gst_gl_shader_use (filterblur->shader1);

  glActiveTexture (GL_TEXTURE1);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
  glDisable (GL_TEXTURE_RECTANGLE_ARB);

  gst_gl_shader_set_uniform_1i (filterblur->shader1, "tex", 1);

  gst_gl_shader_set_uniform_1fv (filterblur->shader1, "kernel", 9, gauss_kernel);
  gst_gl_shader_set_uniform_1f (filterblur->shader1, "norm_const", 0.977016);
  gst_gl_shader_set_uniform_1f (filterblur->shader1, "norm_offset", 0.0);

  gst_gl_filterblur_draw_texture (filterblur, texture);
}
