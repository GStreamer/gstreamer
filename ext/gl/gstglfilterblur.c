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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-glfilterblur
 *
 * Blur with 9x9 separable convolution.
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch videotestsrc ! glupload ! glfilterblur ! glimagesink
 * ]|
 * FBO (Frame Buffer Object) and GLSL (OpenGL Shading Language) are required.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglfilterblur.h"
#include "effects/gstgleffectssources.h"

#define GST_CAT_DEFAULT gst_gl_filterblur_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_filterblur_debug, "glfilterblur", 0, "glfilterblur element");

G_DEFINE_TYPE_WITH_CODE (GstGLFilterBlur, gst_gl_filterblur,
    GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_filterblur_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_filterblur_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_gl_filter_filterblur_reset (GstGLFilter * filter);

static gboolean gst_gl_filterblur_init_shader (GstGLFilter * filter);
static gboolean gst_gl_filterblur_filter_texture (GstGLFilter * filter,
    guint in_tex, guint out_tex);
static void gst_gl_filterblur_hcallback (gint width, gint height, guint texture,
    gpointer stuff);
static void gst_gl_filterblur_vcallback (gint width, gint height, guint texture,
    gpointer stuff);


static void
gst_gl_filterblur_init_resources (GstGLFilter * filter)
{
  GstGLFilterBlur *filterblur = GST_GL_FILTERBLUR (filter);
  GstGLFuncs *gl = filter->context->gl_vtable;

  gl->GenTextures (1, &filterblur->midtexture);
  gl->BindTexture (GL_TEXTURE_2D, filterblur->midtexture);
  gl->TexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8,
      GST_VIDEO_INFO_WIDTH (&filter->out_info),
      GST_VIDEO_INFO_HEIGHT (&filter->out_info),
      0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static void
gst_gl_filterblur_reset_resources (GstGLFilter * filter)
{
  GstGLFilterBlur *filterblur = GST_GL_FILTERBLUR (filter);
  GstGLFuncs *gl = filter->context->gl_vtable;

  gl->DeleteTextures (1, &filterblur->midtexture);
}

static void
gst_gl_filterblur_class_init (GstGLFilterBlurClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_gl_filterblur_set_property;
  gobject_class->get_property = gst_gl_filterblur_get_property;

  gst_element_class_set_metadata (element_class, "Gstreamer OpenGL Blur",
      "Filter/Effect/Video", "Blur with 9x9 separable convolution",
      "Filippo Argiolas <filippo.argiolas@gmail.com>");

  GST_GL_FILTER_CLASS (klass)->filter_texture =
      gst_gl_filterblur_filter_texture;
  GST_GL_FILTER_CLASS (klass)->display_init_cb =
      gst_gl_filterblur_init_resources;
  GST_GL_FILTER_CLASS (klass)->display_reset_cb =
      gst_gl_filterblur_reset_resources;
  GST_GL_FILTER_CLASS (klass)->onInitFBO = gst_gl_filterblur_init_shader;
  GST_GL_FILTER_CLASS (klass)->onReset = gst_gl_filter_filterblur_reset;
}

static void
gst_gl_filterblur_init (GstGLFilterBlur * filterblur)
{
  filterblur->shader0 = NULL;
  filterblur->shader1 = NULL;
  filterblur->midtexture = 0;
  /* gaussian kernel (well, actually vector), size 9, standard
   * deviation 3.0 */
  /* FIXME: eventually make this a runtime property */
  fill_gaussian_kernel (filterblur->gauss_kernel, 7, 3.0);
}

static void
gst_gl_filter_filterblur_reset (GstGLFilter * filter)
{
  GstGLFilterBlur *filterblur = GST_GL_FILTERBLUR (filter);

  //blocking call, wait the opengl thread has destroyed the shader
  if (filterblur->shader0)
    gst_gl_context_del_shader (filter->context, filterblur->shader0);
  filterblur->shader0 = NULL;

  //blocking call, wait the opengl thread has destroyed the shader
  if (filterblur->shader1)
    gst_gl_context_del_shader (filter->context, filterblur->shader1);
  filterblur->shader1 = NULL;
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

static gboolean
gst_gl_filterblur_init_shader (GstGLFilter * filter)
{
  GstGLFilterBlur *blur_filter = GST_GL_FILTERBLUR (filter);

  //blocking call, wait the opengl thread has compiled the shader
  if (!gst_gl_context_gen_shader (filter->context, 0, hconv7_fragment_source,
          &blur_filter->shader0))
    return FALSE;

  //blocking call, wait the opengl thread has compiled the shader
  if (!gst_gl_context_gen_shader (filter->context, 0, vconv7_fragment_source,
          &blur_filter->shader1))
    return FALSE;

  return TRUE;
}

static gboolean
gst_gl_filterblur_filter_texture (GstGLFilter * filter, guint in_tex,
    guint out_tex)
{
  GstGLFilterBlur *filterblur = GST_GL_FILTERBLUR (filter);

  gst_gl_filter_render_to_target (filter, TRUE, in_tex,
      filterblur->midtexture, gst_gl_filterblur_hcallback, filterblur);

  gst_gl_filter_render_to_target (filter, FALSE, filterblur->midtexture,
      out_tex, gst_gl_filterblur_vcallback, filterblur);

  return TRUE;
}

static void
gst_gl_filterblur_hcallback (gint width, gint height, guint texture,
    gpointer stuff)
{
  GstGLFilter *filter = GST_GL_FILTER (stuff);
  GstGLFilterBlur *filterblur = GST_GL_FILTERBLUR (filter);
  GstGLFuncs *gl = filter->context->gl_vtable;

  gl->MatrixMode (GL_PROJECTION);
  gl->LoadIdentity ();

  gst_gl_shader_use (filterblur->shader0);

  gl->ActiveTexture (GL_TEXTURE1);
  gl->Enable (GL_TEXTURE_2D);
  gl->BindTexture (GL_TEXTURE_2D, texture);
  gl->Disable (GL_TEXTURE_2D);

  gst_gl_shader_set_uniform_1i (filterblur->shader0, "tex", 1);
  gst_gl_shader_set_uniform_1fv (filterblur->shader0, "kernel", 7,
      filterblur->gauss_kernel);
  gst_gl_shader_set_uniform_1f (filterblur->shader0, "width", width);

  gst_gl_filter_draw_texture (filter, texture, width, height);
}


static void
gst_gl_filterblur_vcallback (gint width, gint height, guint texture,
    gpointer stuff)
{
  GstGLFilter *filter = GST_GL_FILTER (stuff);
  GstGLFilterBlur *filterblur = GST_GL_FILTERBLUR (filter);
  GstGLFuncs *gl = filter->context->gl_vtable;

  gl->MatrixMode (GL_PROJECTION);
  gl->LoadIdentity ();

  gst_gl_shader_use (filterblur->shader1);

  gl->ActiveTexture (GL_TEXTURE1);
  gl->Enable (GL_TEXTURE_2D);
  gl->BindTexture (GL_TEXTURE_2D, texture);
  gl->Disable (GL_TEXTURE_2D);

  gst_gl_shader_set_uniform_1i (filterblur->shader1, "tex", 1);
  gst_gl_shader_set_uniform_1fv (filterblur->shader1, "kernel", 7,
      filterblur->gauss_kernel);
  gst_gl_shader_set_uniform_1f (filterblur->shader1, "height", height);

  gst_gl_filter_draw_texture (filter, texture, width, height);
}
