/*
 * GStreamer
 * Copyright (C) 2008-2010 Filippo Argiolas <filippo.argiolas@gmail.com>
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
 * SECTION:element-glfilterlaplacian
 *
 * Laplacian Convolution Demo Filter.
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch videotestsrc ! glupload ! glfilterlaplacian ! glimagesink
 * ]|
 * FBO (Frame Buffer Object) and GLSL (OpenGL Shading Language) are required.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglfilterlaplacian.h"

#define GST_CAT_DEFAULT gst_gl_filter_laplacian_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

enum
{
  PROP_0
};

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_filter_laplacian_debug, "glfilterlaplacian", 0, "glfilterlaplacian element");

G_DEFINE_TYPE_WITH_CODE (GstGLFilterLaplacian, gst_gl_filter_laplacian,
    GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_filter_laplacian_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_gl_filter_laplacian_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_gl_filter_laplacian_reset (GstGLFilter * filter);
static gboolean gst_gl_filter_laplacian_init_shader (GstGLFilter * filter);
static gboolean gst_gl_filter_laplacian_filter_texture (GstGLFilter * filter,
    guint in_tex, guint out_tex);
static void gst_gl_filter_laplacian_callback (gint width, gint height,
    guint texture, gpointer stuff);

/* *INDENT-OFF* */

/* This filter is meant as a demo of gst-plugins-gl + glsl
   capabilities. So I'm keeping this shader readable enough. If and
   when this shader will be used in production be careful to hard code
   kernel into the shader and remove unneeded zero multiplications in
   the convolution */
static const gchar *convolution_fragment_source =
  "uniform sampler2D tex;"
  "uniform float kernel[9];"
  "uniform float width, height;"
  "void main () {"
  "  float w = 1.0 / width;"
  "  float h = 1.0 / height;"
  "  vec2 texturecoord[9];"
  "  texturecoord[4] = gl_TexCoord[0].st;"                /*  0  0 */
  "  texturecoord[5] = texturecoord[4] + vec2(w,   0.0);" /*  1  0 */
  "  texturecoord[2] = texturecoord[5] - vec2(0.0, h);" /*  1 -1 */
  "  texturecoord[1] = texturecoord[2] - vec2(w,   0.0);" /*  0 -1 */
  "  texturecoord[0] = texturecoord[1] - vec2(w,   0.0);" /* -1 -1 */
  "  texturecoord[3] = texturecoord[0] + vec2(0.0, h);" /* -1  0 */
  "  texturecoord[6] = texturecoord[3] + vec2(0.0, h);" /* -1  1 */
  "  texturecoord[7] = texturecoord[6] + vec2(w,   0.0);" /*  0  1 */
  "  texturecoord[8] = texturecoord[7] + vec2(w,   0.0);" /*  1  1 */
  "  int i;"
  "  vec4 sum = vec4 (0.0);"
  "  for (i = 0; i < 9; i++) { "
  "    vec4 neighbor = texture2D(tex, texturecoord[i]);"
  "    sum += neighbor * kernel[i];"
  "  }"
  "  gl_FragColor = sum;"
  "}";
/* *INDENT-ON* */

static void
gst_gl_filter_laplacian_class_init (GstGLFilterLaplacianClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_gl_filter_laplacian_set_property;
  gobject_class->get_property = gst_gl_filter_laplacian_get_property;

  gst_element_class_set_metadata (element_class,
      "OpenGL laplacian filter", "Filter/Effect/Video",
      "Laplacian Convolution Demo Filter",
      "Filippo Argiolas <filippo.argiolas@gmail.com>");

  GST_GL_FILTER_CLASS (klass)->filter_texture =
      gst_gl_filter_laplacian_filter_texture;
  GST_GL_FILTER_CLASS (klass)->onInitFBO = gst_gl_filter_laplacian_init_shader;
  GST_GL_FILTER_CLASS (klass)->onReset = gst_gl_filter_laplacian_reset;
}

static void
gst_gl_filter_laplacian_init (GstGLFilterLaplacian * filter)
{
  filter->shader = NULL;
}

static void
gst_gl_filter_laplacian_reset (GstGLFilter * filter)
{
  GstGLFilterLaplacian *laplacian_filter = GST_GL_FILTER_LAPLACIAN (filter);

  //blocking call, wait the opengl thread has destroyed the shader
  if (laplacian_filter->shader)
    gst_gl_context_del_shader (filter->context, laplacian_filter->shader);
  laplacian_filter->shader = NULL;
}

static void
gst_gl_filter_laplacian_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  //GstGLFilterLaplacian *filter = GST_GL_FILTER_LAPLACIAN (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_filter_laplacian_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  //GstGLFilterLaplacian *filter = GST_GL_FILTER_LAPLACIAN (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_filter_laplacian_init_shader (GstGLFilter * filter)
{
  GstGLFilterLaplacian *laplacian_filter = GST_GL_FILTER_LAPLACIAN (filter);

  //blocking call, wait the opengl thread has compiled the shader
  return gst_gl_context_gen_shader (filter->context, 0,
      convolution_fragment_source, &laplacian_filter->shader);
}

static gboolean
gst_gl_filter_laplacian_filter_texture (GstGLFilter * filter, guint in_tex,
    guint out_tex)
{
  gpointer laplacian_filter = GST_GL_FILTER_LAPLACIAN (filter);


  //blocking call, use a FBO
  gst_gl_filter_render_to_target (filter, TRUE, in_tex, out_tex,
      gst_gl_filter_laplacian_callback, laplacian_filter);

  return TRUE;
}

//opengl scene, params: input texture (not the output filter->texture)
static void
gst_gl_filter_laplacian_callback (gint width, gint height, guint texture,
    gpointer stuff)
{
  GstGLFilter *filter = GST_GL_FILTER (stuff);
  GstGLFilterLaplacian *laplacian_filter = GST_GL_FILTER_LAPLACIAN (filter);
  GstGLFuncs *gl = filter->context->gl_vtable;

  gfloat kernel[9] = { 0.0, -1.0, 0.0,
    -1.0, 4.0, -1.0,
    0.0, -1.0, 0.0
  };

  gl->MatrixMode (GL_PROJECTION);
  gl->LoadIdentity ();

  gst_gl_shader_use (laplacian_filter->shader);

  gl->ActiveTexture (GL_TEXTURE0);
  gl->Enable (GL_TEXTURE_2D);
  gl->BindTexture (GL_TEXTURE_2D, texture);

  gst_gl_shader_set_uniform_1i (laplacian_filter->shader, "tex", 0);
  gst_gl_shader_set_uniform_1fv (laplacian_filter->shader, "kernel", 9, kernel);
  gst_gl_shader_set_uniform_1f (laplacian_filter->shader, "width",
      (gfloat) width);
  gst_gl_shader_set_uniform_1f (laplacian_filter->shader, "height",
      (gfloat) height);

  gst_gl_filter_draw_texture (filter, texture, width, height);
}
