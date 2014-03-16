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
  GstGLContext *context = filter->context;
  GstGLFuncs *gl = context->gl_vtable;

  shader = g_hash_table_lookup (effects->shaderstable, "glow0");

  if (!shader) {
    shader = gst_gl_shader_new (context);
    g_hash_table_insert (effects->shaderstable, (gchar *) "glow0", shader);
  }

  if (!gst_gl_shader_compile_and_check (shader,
          luma_threshold_fragment_source, GST_GL_SHADER_FRAGMENT_SOURCE)) {
    gst_gl_context_set_error (context,
        "Failed to initialize luma threshold shader");
    GST_ELEMENT_ERROR (effects, RESOURCE, NOT_FOUND,
        ("%s", gst_gl_context_get_error ()), (NULL));
    return;
  }

  gl->MatrixMode (GL_PROJECTION);
  gl->LoadIdentity ();

  gst_gl_shader_use (shader);

  gl->ActiveTexture (GL_TEXTURE0);
  gl->Enable (GL_TEXTURE_2D);
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
  GstGLContext *context = filter->context;
  GstGLFuncs *gl = context->gl_vtable;

  shader = g_hash_table_lookup (effects->shaderstable, "glow1");

  if (!shader) {
    shader = gst_gl_shader_new (context);
    g_hash_table_insert (effects->shaderstable, (gchar *) "glow1", shader);
  }

  if (!kernel_ready) {
    fill_gaussian_kernel (gauss_kernel, 7, 10.0);
    kernel_ready = TRUE;
  }

  if (!gst_gl_shader_compile_and_check (shader,
          hconv7_fragment_source, GST_GL_SHADER_FRAGMENT_SOURCE)) {
    gst_gl_context_set_error (context, "Failed to initialize hconv7 shader");
    GST_ELEMENT_ERROR (effects, RESOURCE, NOT_FOUND,
        ("%s", gst_gl_context_get_error ()), (NULL));
    return;
  }

  gl->MatrixMode (GL_PROJECTION);
  gl->LoadIdentity ();

  gst_gl_shader_use (shader);

  gl->ActiveTexture (GL_TEXTURE1);
  gl->Enable (GL_TEXTURE_2D);
  gl->BindTexture (GL_TEXTURE_2D, texture);
  gl->Disable (GL_TEXTURE_2D);

  gst_gl_shader_set_uniform_1i (shader, "tex", 1);
  gst_gl_shader_set_uniform_1fv (shader, "kernel", 7, gauss_kernel);
  gst_gl_shader_set_uniform_1f (shader, "height", height);

  gst_gl_filter_draw_texture (filter, texture, width, height);
}

static void
gst_gl_effects_glow_step_three (gint width, gint height, guint texture,
    gpointer data)
{
  GstGLShader *shader;
  GstGLEffects *effects = GST_GL_EFFECTS (data);
  GstGLFilter *filter = GST_GL_FILTER (effects);
  GstGLContext *context = filter->context;
  GstGLFuncs *gl = context->gl_vtable;

  shader = g_hash_table_lookup (effects->shaderstable, "glow2");

  if (!shader) {
    shader = gst_gl_shader_new (context);
    g_hash_table_insert (effects->shaderstable, (gchar *) "glow2", shader);
  }

  if (!gst_gl_shader_compile_and_check (shader,
          vconv7_fragment_source, GST_GL_SHADER_FRAGMENT_SOURCE)) {
    gst_gl_context_set_error (context, "Failed to initialize vcon7 shader");
    GST_ELEMENT_ERROR (effects, RESOURCE, NOT_FOUND,
        ("%s", gst_gl_context_get_error ()), (NULL));
    return;
  }

  gl->MatrixMode (GL_PROJECTION);
  gl->LoadIdentity ();

  gst_gl_shader_use (shader);

  gl->ActiveTexture (GL_TEXTURE1);
  gl->Enable (GL_TEXTURE_2D);
  gl->BindTexture (GL_TEXTURE_2D, texture);
  gl->Disable (GL_TEXTURE_2D);

  gst_gl_shader_set_uniform_1i (shader, "tex", 1);
  gst_gl_shader_set_uniform_1fv (shader, "kernel", 7, gauss_kernel);
  gst_gl_shader_set_uniform_1f (shader, "width", width);

  gst_gl_filter_draw_texture (filter, texture, width, height);
}

static void
gst_gl_effects_glow_step_four (gint width, gint height, guint texture,
    gpointer data)
{
  GstGLShader *shader;
  GstGLEffects *effects = GST_GL_EFFECTS (data);
  GstGLFilter *filter = GST_GL_FILTER (effects);
  GstGLContext *context = filter->context;
  GstGLFuncs *gl = context->gl_vtable;

  shader = g_hash_table_lookup (effects->shaderstable, "glow3");

  if (!shader) {
    shader = gst_gl_shader_new (context);
    g_hash_table_insert (effects->shaderstable, (gchar *) "glow3", shader);
  }

  if (!gst_gl_shader_compile_and_check (shader,
          sum_fragment_source, GST_GL_SHADER_FRAGMENT_SOURCE)) {
    gst_gl_context_set_error (context, "Failed to initialize sum shader");
    GST_ELEMENT_ERROR (effects, RESOURCE, NOT_FOUND,
        ("%s", gst_gl_context_get_error ()), (NULL));
    return;
  }

  gl->MatrixMode (GL_PROJECTION);
  gl->LoadIdentity ();

  gst_gl_shader_use (shader);

  gl->ActiveTexture (GL_TEXTURE2);
  gl->Enable (GL_TEXTURE_2D);
  gl->BindTexture (GL_TEXTURE_2D, effects->intexture);
  gl->Disable (GL_TEXTURE_2D);

  gst_gl_shader_set_uniform_1f (shader, "alpha", 1.0);
  gst_gl_shader_set_uniform_1i (shader, "base", 2);

  gl->ActiveTexture (GL_TEXTURE1);
  gl->Enable (GL_TEXTURE_2D);
  gl->BindTexture (GL_TEXTURE_2D, texture);
  gl->Disable (GL_TEXTURE_2D);

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
