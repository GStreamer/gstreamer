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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../gstgleffects.h"
#include "gstgleffectscurves.h"
#include "gstgleffectlumatocurve.h"

static gboolean kernel_ready = FALSE;
static float gauss_kernel[7];

static void
gst_gl_effects_xray_step_one (gint width, gint height, guint texture,
    gpointer data)
{
  GstGLEffects *effects = GST_GL_EFFECTS (data);

  gst_gl_effects_luma_to_curve (effects, &xray_curve, GST_GL_EFFECTS_CURVE_XRAY,
      width, height, texture);
}

static void
gst_gl_effects_xray_step_two (gint width, gint height, guint texture,
    gpointer data)
{
  GstGLShader *shader;
  GstGLEffects *effects = GST_GL_EFFECTS (data);
  GstGLFilter *filter = GST_GL_FILTER (effects);
  GstGLContext *context = GST_GL_BASE_FILTER (filter)->context;
  GstGLFuncs *gl = context->gl_vtable;

  shader = gst_gl_effects_get_fragment_shader (effects, "hconv7",
      hconv7_fragment_source_gles2, hconv7_fragment_source_opengl);

  if (!shader)
    return;

  if (!kernel_ready) {
    fill_gaussian_kernel (gauss_kernel, 7, 1.5);
    kernel_ready = TRUE;
  }
#if GST_GL_HAVE_OPENGL
  if (USING_OPENGL (context)) {
    gl->MatrixMode (GL_PROJECTION);
    gl->LoadIdentity ();
  }
#endif

  gst_gl_shader_use (shader);

  gl->ActiveTexture (GL_TEXTURE1);
  gl->BindTexture (GL_TEXTURE_2D, texture);

  gst_gl_shader_set_uniform_1i (shader, "tex", 1);
  gst_gl_shader_set_uniform_1fv (shader, "kernel", 9, gauss_kernel);
  gst_gl_shader_set_uniform_1f (shader, "gauss_width", width);

  gst_gl_filter_draw_texture (filter, texture, width, height);
}

static void
gst_gl_effects_xray_step_three (gint width, gint height, guint texture,
    gpointer data)
{
  GstGLShader *shader;
  GstGLEffects *effects = GST_GL_EFFECTS (data);
  GstGLFilter *filter = GST_GL_FILTER (effects);
  GstGLContext *context = GST_GL_BASE_FILTER (filter)->context;
  GstGLFuncs *gl = context->gl_vtable;

  shader = gst_gl_effects_get_fragment_shader (effects, "vconv7",
      vconv7_fragment_source_gles2, vconv7_fragment_source_opengl);

  if (!shader)
    return;

#if GST_GL_HAVE_OPENGL
  if (USING_OPENGL (context)) {
    gl->MatrixMode (GL_PROJECTION);
    gl->LoadIdentity ();
  }
#endif

  gst_gl_shader_use (shader);

  gl->ActiveTexture (GL_TEXTURE1);
  gl->BindTexture (GL_TEXTURE_2D, texture);

  gst_gl_shader_set_uniform_1i (shader, "tex", 1);
  gst_gl_shader_set_uniform_1fv (shader, "kernel", 9, gauss_kernel);
  gst_gl_shader_set_uniform_1f (shader, "gauss_height", height);

  gst_gl_filter_draw_texture (filter, texture, width, height);
}

/* multipass separable sobel */
static void
gst_gl_effects_xray_desaturate (gint width, gint height, guint texture,
    gpointer data)
{
  GstGLShader *shader;
  GstGLEffects *effects = GST_GL_EFFECTS (data);
  GstGLFilter *filter = GST_GL_FILTER (effects);
  GstGLContext *context = GST_GL_BASE_FILTER (filter)->context;
  GstGLFuncs *gl = context->gl_vtable;

  shader = gst_gl_effects_get_fragment_shader (effects, "desaturate",
      desaturate_fragment_source_gles2, desaturate_fragment_source_opengl);

  if (!shader)
    return;

#if GST_GL_HAVE_OPENGL
  if (USING_OPENGL (context)) {
    gl->MatrixMode (GL_PROJECTION);
    gl->LoadIdentity ();
  }
#endif

  gst_gl_shader_use (shader);

  gl->ActiveTexture (GL_TEXTURE1);
  gl->BindTexture (GL_TEXTURE_2D, texture);

  gst_gl_shader_set_uniform_1i (shader, "tex", 1);
  gst_gl_filter_draw_texture (filter, texture, width, height);
}

static void
gst_gl_effects_xray_sobel_hconv (gint width, gint height, guint texture,
    gpointer data)
{
  GstGLShader *shader;
  GstGLEffects *effects = GST_GL_EFFECTS (data);
  GstGLFilter *filter = GST_GL_FILTER (effects);
  GstGLContext *context = GST_GL_BASE_FILTER (filter)->context;
  GstGLFuncs *gl = context->gl_vtable;

  shader = gst_gl_effects_get_fragment_shader (effects, "sobel_hconv3",
      sep_sobel_hconv3_fragment_source_gles2,
      sep_sobel_hconv3_fragment_source_opengl);

  if (!shader)
    return;

#if GST_GL_HAVE_OPENGL
  if (USING_OPENGL (context)) {
    gl->MatrixMode (GL_PROJECTION);
    gl->LoadIdentity ();
  }
#endif

  gst_gl_shader_use (shader);

  gl->ActiveTexture (GL_TEXTURE1);
  gl->BindTexture (GL_TEXTURE_2D, texture);

  gst_gl_shader_set_uniform_1i (shader, "tex", 1);
  gst_gl_shader_set_uniform_1f (shader, "width", width);

  gst_gl_filter_draw_texture (filter, texture, width, height);
}

static void
gst_gl_effects_xray_sobel_vconv (gint width, gint height, guint texture,
    gpointer data)
{
  GstGLShader *shader;
  GstGLEffects *effects = GST_GL_EFFECTS (data);
  GstGLFilter *filter = GST_GL_FILTER (effects);
  GstGLContext *context = GST_GL_BASE_FILTER (filter)->context;
  GstGLFuncs *gl = context->gl_vtable;

  shader = gst_gl_effects_get_fragment_shader (effects, "sobel_vconv3",
      sep_sobel_vconv3_fragment_source_gles2,
      sep_sobel_vconv3_fragment_source_opengl);

  if (!shader)
    return;

#if GST_GL_HAVE_OPENGL
  if (USING_OPENGL (context)) {
    gl->MatrixMode (GL_PROJECTION);
    gl->LoadIdentity ();
  }
#endif

  gst_gl_shader_use (shader);

  gl->ActiveTexture (GL_TEXTURE1);
  gl->BindTexture (GL_TEXTURE_2D, texture);

  gst_gl_shader_set_uniform_1i (shader, "tex", 1);
  gst_gl_shader_set_uniform_1f (shader, "height", height);

  gst_gl_filter_draw_texture (filter, texture, width, height);
}

static void
gst_gl_effects_xray_sobel_length (gint width, gint height, guint texture,
    gpointer data)
{
  GstGLShader *shader;
  GstGLEffects *effects = GST_GL_EFFECTS (data);
  GstGLFilter *filter = GST_GL_FILTER (effects);
  GstGLContext *context = GST_GL_BASE_FILTER (filter)->context;
  GstGLFuncs *gl = context->gl_vtable;

  shader = gst_gl_effects_get_fragment_shader (effects, "sobel_length",
      sep_sobel_length_fragment_source_gles2,
      sep_sobel_length_fragment_source_opengl);

  if (!shader)
    return;

#if GST_GL_HAVE_OPENGL
  if (USING_OPENGL (context)) {
    gl->MatrixMode (GL_PROJECTION);
    gl->LoadIdentity ();
  }
#endif

  gst_gl_shader_use (shader);

  gl->ActiveTexture (GL_TEXTURE1);
  gl->BindTexture (GL_TEXTURE_2D, texture);

  gst_gl_shader_set_uniform_1i (shader, "tex", 1);
  gst_gl_shader_set_uniform_1i (shader, "invert", TRUE);
  gst_gl_filter_draw_texture (filter, texture, width, height);
}

/* end of sobel passes */

static void
gst_gl_effects_xray_step_five (gint width, gint height, guint texture,
    gpointer data)
{
  GstGLShader *shader;
  GstGLEffects *effects = GST_GL_EFFECTS (data);
  GstGLFilter *filter = GST_GL_FILTER (effects);
  GstGLContext *context = GST_GL_BASE_FILTER (filter)->context;
  GstGLFuncs *gl = context->gl_vtable;

  shader = gst_gl_effects_get_fragment_shader (effects, "multiply",
      multiply_fragment_source_gles2, multiply_fragment_source_opengl);

  if (!shader)
    return;

#if GST_GL_HAVE_OPENGL
  if (USING_OPENGL (context)) {
    gl->MatrixMode (GL_PROJECTION);
    gl->LoadIdentity ();
  }
#endif

  gst_gl_shader_use (shader);

  gl->ActiveTexture (GL_TEXTURE2);
  gl->BindTexture (GL_TEXTURE_2D, effects->midtexture[2]);

  gst_gl_shader_set_uniform_1i (shader, "base", 2);

  gl->ActiveTexture (GL_TEXTURE1);
  gl->BindTexture (GL_TEXTURE_2D, texture);

  gst_gl_shader_set_uniform_1f (shader, "alpha", (gfloat) 0.5f);
  gst_gl_shader_set_uniform_1i (shader, "blend", 1);

  gst_gl_filter_draw_texture (filter, texture, width, height);
}

void
gst_gl_effects_xray (GstGLEffects * effects)
{
  GstGLFilter *filter = GST_GL_FILTER (effects);

  /* map luma to xray curve */
  gst_gl_filter_render_to_target (filter, TRUE, effects->intexture,
      effects->midtexture[0], gst_gl_effects_xray_step_one, effects);
  /* horizontal blur */
  gst_gl_filter_render_to_target (filter, FALSE, effects->midtexture[0],
      effects->midtexture[1], gst_gl_effects_xray_step_two, effects);
  /* vertical blur */
  gst_gl_filter_render_to_target (filter, FALSE, effects->midtexture[1],
      effects->midtexture[2], gst_gl_effects_xray_step_three, effects);
  /* detect edges with Sobel */
  /* the old version used edges from the blurred texture, this uses
   * the ones from original texture, still not sure what I like
   * more. This one gives better edges obviously but behaves badly
   * with noise */
  /* desaturate */
  gst_gl_filter_render_to_target (filter, TRUE, effects->intexture,
      effects->midtexture[3], gst_gl_effects_xray_desaturate, effects);
  /* horizonal convolution */
  gst_gl_filter_render_to_target (filter, FALSE, effects->midtexture[3],
      effects->midtexture[4], gst_gl_effects_xray_sobel_hconv, effects);
  /* vertical convolution */
  gst_gl_filter_render_to_target (filter, FALSE, effects->midtexture[4],
      effects->midtexture[3], gst_gl_effects_xray_sobel_vconv, effects);
  /* gradient length */
  gst_gl_filter_render_to_target (filter, FALSE, effects->midtexture[3],
      effects->midtexture[4], gst_gl_effects_xray_sobel_length, effects);
  /* multiply edges with the blurred image */
  gst_gl_filter_render_to_target (filter, FALSE, effects->midtexture[4],
      effects->outtexture, gst_gl_effects_xray_step_five, effects);
}
