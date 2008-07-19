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

#include "gstglfilterlaplacian.h"

#define GST_CAT_DEFAULT gst_gl_filter_laplacian_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static const GstElementDetails element_details = 
  GST_ELEMENT_DETAILS ("OpenGL laplacian filter",
		       "Filter/Effect",
		       "Laplacian Convolution Demo Filter",
		       "Filippo Argiolas <filippo.argiolas@gmail.com>");

enum
{
  PROP_0
};

#define DEBUG_INIT(bla)							\
  GST_DEBUG_CATEGORY_INIT (gst_gl_filter_laplacian_debug, "glfilterlaplacian", 0, "glfilterlaplacian element");

GST_BOILERPLATE_FULL (GstGLFilterLaplacian, gst_gl_filter_laplacian, GstGLFilter,
		      GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_filter_laplacian_set_property (GObject * object, guint prop_id,
						  const GValue * value, GParamSpec * pspec);
static void gst_gl_filter_laplacian_get_property (GObject * object, guint prop_id,
						  GValue * value, GParamSpec * pspec);

static void gst_gl_filter_laplacian_reset (GstGLFilter* filter);
static void gst_gl_filter_laplacian_init_shader (GstGLFilter* filter);
static gboolean gst_gl_filter_laplacian_filter (GstGLFilter * filter,
						GstGLBuffer * inbuf, GstGLBuffer * outbuf);
static void gst_gl_filter_laplacian_callback (gint width, gint height, guint texture, gpointer stuff);

static const gchar *convolution_fragment_source = 
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect tex;"
  "uniform float norm_const;"
  "uniform float norm_offset;"
  "uniform float kernel[9];"
  "void main () {"
  "  vec2 offset[9] = vec2[9] ("
  "      vec2(-1.0,-1.0), vec2( 0.0,-1.0), vec2( 1.0,-1.0),"
  "      vec2(-1.0, 0.0), vec2( 0.0, 0.0), vec2( 1.0, 0.0),"
  "      vec2(-1.0, 1.0), vec2( 0.0, 1.0), vec2( 1.0, 1.0) );"
  "  vec2 texturecoord = gl_TexCoord[0].st;"
  "  int i;"
  "  vec4 sum = vec4 (0.0);"
  "  for (i = 0; i < 9; i++) { "
  "    if (kernel[i] != 0.0) {"
  "      vec4 neighbor = texture2DRect(tex, texturecoord + vec2(offset[i])); "
  "      sum += neighbor * kernel[i]/norm_const; "
  "    }"
  "  }"
  "  gl_FragColor = sum + norm_offset;"
  "}";

static void
gst_gl_filter_laplacian_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &element_details);
}

static void
gst_gl_filter_laplacian_class_init (GstGLFilterLaplacianClass* klass)
{
  GObjectClass* gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_gl_filter_laplacian_set_property;
  gobject_class->get_property = gst_gl_filter_laplacian_get_property;

  GST_GL_FILTER_CLASS (klass)->filter = gst_gl_filter_laplacian_filter;
  GST_GL_FILTER_CLASS (klass)->onInitFBO = gst_gl_filter_laplacian_init_shader;
  GST_GL_FILTER_CLASS (klass)->onReset = gst_gl_filter_laplacian_reset;
}

static void
gst_gl_filter_laplacian_init (GstGLFilterLaplacian* filter,
    GstGLFilterLaplacianClass* klass)
{
    filter->shader = NULL;
}

static void
gst_gl_filter_laplacian_reset (GstGLFilter* filter)
{
  GstGLFilterLaplacian* laplacian_filter = GST_GL_FILTER_LAPLACIAN(filter);

  //blocking call, wait the opengl thread has destroyed the shader
  gst_gl_display_del_shader (filter->display, laplacian_filter->shader);
}

static void
gst_gl_filter_laplacian_set_property (GObject* object, guint prop_id,
				      const GValue* value, GParamSpec* pspec)
{
  //GstGLFilterLaplacian *filter = GST_GL_FILTER_LAPLACIAN (object);

  switch (prop_id) 
  {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
gst_gl_filter_laplacian_get_property (GObject* object, guint prop_id,
				      GValue* value, GParamSpec* pspec)
{
  //GstGLFilterLaplacian *filter = GST_GL_FILTER_LAPLACIAN (object);

  switch (prop_id) 
  {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
gst_gl_filter_laplacian_init_shader (GstGLFilter* filter)
{
  GstGLFilterLaplacian* laplacian_filter = GST_GL_FILTER_LAPLACIAN (filter);
    
  //blocking call, wait the opengl thread has compiled the shader
  gst_gl_display_gen_shader (filter->display, convolution_fragment_source, &laplacian_filter->shader);
}

static gboolean
gst_gl_filter_laplacian_filter (GstGLFilter* filter, GstGLBuffer* inbuf,
				GstGLBuffer* outbuf)
{
  gpointer laplacian_filter = GST_GL_FILTER_LAPLACIAN (filter);
    
  //blocking call, generate a FBO
  gst_gl_display_use_fbo (filter->display, filter->width, filter->height,
			  filter->fbo, filter->depthbuffer, outbuf->texture, gst_gl_filter_laplacian_callback,
			  inbuf->width, inbuf->height, inbuf->texture,
			  0, filter->width, 0, filter->height,
			  GST_GL_DISPLAY_PROJECTION_ORTHO2D, laplacian_filter);

  return TRUE;
}

//opengl scene, params: input texture (not the output filter->texture)
static void
gst_gl_filter_laplacian_callback (gint width, gint height, guint texture, gpointer stuff)
{
  GstGLFilterLaplacian* laplacian_filter = GST_GL_FILTER_LAPLACIAN (stuff);

  gfloat kernel[9] = {  0.0, -1.0,  0.0,
		       -1.0,  4.0, -1.0,
		        0.0, -1.0,  0.0  };
    
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  gst_gl_shader_use (laplacian_filter->shader);

  glActiveTexture (GL_TEXTURE0);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);

  gst_gl_shader_set_uniform_1i  (laplacian_filter->shader, "tex", 0);
  gst_gl_shader_set_uniform_1fv (laplacian_filter->shader, "kernel", 9, kernel);
  gst_gl_shader_set_uniform_1f  (laplacian_filter->shader, "norm_const", 1.0);
  gst_gl_shader_set_uniform_1f  (laplacian_filter->shader, "norm_offset", 0.0); //set to 0.5 to preserve overall greylevel
  

  glBegin (GL_QUADS);
  glTexCoord2i (0, 0);
  glVertex2f (-1.0f, -1.0f);
  glTexCoord2i (width, 0);
  glVertex2f (1.0f, -1.0f);
  glTexCoord2i (width, height);
  glVertex2f (1.0f, 1.0f);
  glTexCoord2i (0, height);
  glVertex2f (-1.0f, 1.0f);     
  glEnd ();
}
