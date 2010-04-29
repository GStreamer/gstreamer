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

#include <gstgleffects.h>

static gboolean kernel_ready = FALSE;
static float gauss_kernel[7];

static void
gst_gl_effects_glow_step_one (gint width, gint height, guint texture,
    gpointer data)
{
  GstGLEffects *effects = GST_GL_EFFECTS (data);

  GstGLShader *shader;

  shader = g_hash_table_lookup (effects->shaderstable, "glow0");

  if (!shader) {
    shader = gst_gl_shader_new ();
    g_hash_table_insert (effects->shaderstable, "glow0", shader);
  }

  g_return_if_fail (gst_gl_shader_compile_and_check (shader,
          luma_threshold_fragment_source, GST_GL_SHADER_FRAGMENT_SOURCE));

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  gst_gl_shader_use (shader);

  glActiveTexture (GL_TEXTURE0);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);

  gst_gl_shader_set_uniform_1i (shader, "tex", 0);

  gst_gl_effects_draw_texture (effects, texture);
}

static void
gst_gl_effects_glow_step_two (gint width, gint height, guint texture,
    gpointer stuff)
{
  GstGLEffects *effects = GST_GL_EFFECTS (stuff);
  GstGLShader *shader;

  shader = g_hash_table_lookup (effects->shaderstable, "glow1");

  if (!shader) {
    shader = gst_gl_shader_new ();
    g_hash_table_insert (effects->shaderstable, "glow1", shader);
  }

  if (!kernel_ready) {
    fill_gaussian_kernel (gauss_kernel, 7, 10.0);
    kernel_ready = TRUE;
  }

  g_return_if_fail (gst_gl_shader_compile_and_check (shader,
          hconv7_fragment_source, GST_GL_SHADER_FRAGMENT_SOURCE));

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  gst_gl_shader_use (shader);

  glActiveTexture (GL_TEXTURE1);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
  glDisable (GL_TEXTURE_RECTANGLE_ARB);

  gst_gl_shader_set_uniform_1i (shader, "tex", 1);
  gst_gl_shader_set_uniform_1fv (shader, "kernel", 7, gauss_kernel);

  gst_gl_effects_draw_texture (effects, texture);
}

void
gst_gl_effects_glow_step_three (gint width, gint height, guint texture,
    gpointer stuff)
{
  GstGLEffects *effects = GST_GL_EFFECTS (stuff);
  GstGLShader *shader;

  shader = g_hash_table_lookup (effects->shaderstable, "glow2");

  if (!shader) {
    shader = gst_gl_shader_new ();
    g_hash_table_insert (effects->shaderstable, "glow2", shader);
  }

  g_return_if_fail (gst_gl_shader_compile_and_check (shader,
          vconv7_fragment_source, GST_GL_SHADER_FRAGMENT_SOURCE));

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  gst_gl_shader_use (shader);

  glActiveTexture (GL_TEXTURE1);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
  glDisable (GL_TEXTURE_RECTANGLE_ARB);

  gst_gl_shader_set_uniform_1i (shader, "tex", 1);
  gst_gl_shader_set_uniform_1fv (shader, "kernel", 7, gauss_kernel);

  gst_gl_effects_draw_texture (effects, texture);
}

void
gst_gl_effects_glow_step_four (gint width, gint height, guint texture,
    gpointer stuff)
{
  GstGLEffects *effects = GST_GL_EFFECTS (stuff);
  GstGLShader *shader;

  shader = g_hash_table_lookup (effects->shaderstable, "glow3");

  if (!shader) {
    shader = gst_gl_shader_new ();
    g_hash_table_insert (effects->shaderstable, "glow3", shader);
  }

  g_return_if_fail (gst_gl_shader_compile_and_check (shader,
          sum_fragment_source, GST_GL_SHADER_FRAGMENT_SOURCE));

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  gst_gl_shader_use (shader);

  glActiveTexture (GL_TEXTURE2);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, effects->intexture);
  glDisable (GL_TEXTURE_RECTANGLE_ARB);

  gst_gl_shader_set_uniform_1f (shader, "alpha", 1.0);
  gst_gl_shader_set_uniform_1i (shader, "base", 2);

  glActiveTexture (GL_TEXTURE1);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
  glDisable (GL_TEXTURE_RECTANGLE_ARB);

  gst_gl_shader_set_uniform_1f (shader, "beta", (gfloat) 1 / 3.5f);
  gst_gl_shader_set_uniform_1i (shader, "blend", 1);

  gst_gl_effects_draw_texture (effects, texture);
}

void
gst_gl_effects_glow (GstGLEffects * effects)
{
  GstGLFilter *filter = GST_GL_FILTER (effects);

  /* threshold */
  gst_gl_filter_render_to_target (filter, effects->intexture,
      effects->midtexture[0], gst_gl_effects_glow_step_one, effects);
  /* blur */
  gst_gl_filter_render_to_target (filter, effects->midtexture[0],
      effects->midtexture[1], gst_gl_effects_glow_step_two, effects);
  gst_gl_filter_render_to_target (filter, effects->midtexture[1],
      effects->midtexture[2], gst_gl_effects_glow_step_three, effects);
  /* add blurred luma to intexture */
  gst_gl_filter_render_to_target (filter, effects->midtexture[2],
      effects->outtexture, gst_gl_effects_glow_step_four, effects);
}
