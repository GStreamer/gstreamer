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

static gboolean kernel_ready = FALSE;
static float gauss_kernel[7];

static void
gst_gl_effects_glow_step_one (gint width, gint height, guint texture,
    gpointer data)
{
  GstGLShader *shader;
  GstGLEffects *effects = GST_GL_EFFECTS (data);
  GstGLFilter *filter = GST_GL_FILTER (effects);
  GstGLContext *context = GST_GL_BASE_FILTER (filter)->context;
  GstGLFuncs *gl = context->gl_vtable;

  shader = gst_gl_effects_get_fragment_shader (effects, "luma_threshold",
      luma_threshold_fragment_source_gles2,
      luma_threshold_fragment_source_opengl);

  if (!shader)
    return;

#if GST_GL_HAVE_OPENGL
  if (USING_OPENGL (context)) {
    gl->MatrixMode (GL_PROJECTION);
    gl->LoadIdentity ();
  }
#endif

  gst_gl_shader_use (shader);

  gl->ActiveTexture (GL_TEXTURE0);
  gl->BindTexture (GL_TEXTURE_2D, texture);

  gst_gl_shader_set_uniform_1i (shader, "tex", 0);

  gst_gl_filter_draw_texture (filter, texture, width, height);
}

static void
gst_gl_effects_glow_step_two (gint width, gint height, guint texture,
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
    fill_gaussian_kernel (gauss_kernel, 7, 10.0);
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
  gst_gl_shader_set_uniform_1fv (shader, "kernel", 7, gauss_kernel);
  gst_gl_shader_set_uniform_1f (shader, "gauss_width", width);

  gst_gl_filter_draw_texture (filter, texture, width, height);
}

static void
gst_gl_effects_glow_step_three (gint width, gint height, guint texture,
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
  gst_gl_shader_set_uniform_1fv (shader, "kernel", 7, gauss_kernel);
  gst_gl_shader_set_uniform_1f (shader, "gauss_height", height);

  gst_gl_filter_draw_texture (filter, texture, width, height);
}

static void
gst_gl_effects_glow_step_four (gint width, gint height, guint texture,
    gpointer data)
{
  GstGLShader *shader;
  GstGLEffects *effects = GST_GL_EFFECTS (data);
  GstGLFilter *filter = GST_GL_FILTER (effects);
  GstGLContext *context = GST_GL_BASE_FILTER (filter)->context;
  GstGLFuncs *gl = context->gl_vtable;

  shader = gst_gl_effects_get_fragment_shader (effects, "sum",
      sum_fragment_source_gles2, sum_fragment_source_opengl);

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
  gl->BindTexture (GL_TEXTURE_2D, effects->intexture);

  gst_gl_shader_set_uniform_1f (shader, "alpha", 1.0f);
  gst_gl_shader_set_uniform_1i (shader, "base", 2);

  gl->ActiveTexture (GL_TEXTURE1);
  gl->BindTexture (GL_TEXTURE_2D, texture);

  gst_gl_shader_set_uniform_1f (shader, "beta", (gfloat) 1 / 3.5f);
  gst_gl_shader_set_uniform_1i (shader, "blend", 1);

  gst_gl_filter_draw_texture (filter, texture, width, height);
}

void
gst_gl_effects_glow (GstGLEffects * effects)
{
  GstGLFilter *filter = GST_GL_FILTER (effects);

  /* threshold */
  gst_gl_filter_render_to_target (filter, TRUE, effects->intexture,
      effects->midtexture[0], gst_gl_effects_glow_step_one, effects);

  /* blur */
  gst_gl_filter_render_to_target (filter, FALSE, effects->midtexture[0],
      effects->midtexture[1], gst_gl_effects_glow_step_two, effects);

  gst_gl_filter_render_to_target (filter, FALSE, effects->midtexture[1],
      effects->midtexture[2], gst_gl_effects_glow_step_three, effects);

  /* add blurred luma to intexture */
  gst_gl_filter_render_to_target (filter, FALSE, effects->midtexture[2],
      effects->outtexture, gst_gl_effects_glow_step_four, effects);
}
