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
#include <gstgleffectlumatocurve.h>

void
gst_gl_effects_luma_to_curve (GstGLEffects * effects,
    GstGLEffectsCurve curve,
    gint curve_index, gint width, gint height, GLuint texture)
{
  GstGLShader *shader;

  shader = g_hash_table_lookup (effects->shaderstable, "lumamap0");

  if (!shader) {
    shader = gst_gl_shader_new ();
    g_hash_table_insert (effects->shaderstable, "lumamap0", shader);
  }

  if (!gst_gl_shader_compile_and_check (shader,
          luma_to_curve_fragment_source, GST_GL_SHADER_FRAGMENT_SOURCE)) {
    gst_gl_display_set_error (GST_GL_FILTER (effects)->display,
        "Failed to initialize luma to curve shader");
    GST_ELEMENT_ERROR (effects, RESOURCE, NOT_FOUND,
        GST_GL_DISPLAY_ERR_MSG (GST_GL_FILTER (effects)->display), (NULL));
    return;
  }

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  gst_gl_shader_use (shader);

  if (effects->curve[curve_index] == 0) {
    /* this parameters are needed to have a right, predictable, mapping */
    glGenTextures (1, &effects->curve[curve_index]);
    glEnable (GL_TEXTURE_1D);
    glBindTexture (GL_TEXTURE_1D, effects->curve[curve_index]);
    glTexParameteri (GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri (GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri (GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri (GL_TEXTURE_1D, GL_TEXTURE_WRAP_T, GL_CLAMP);

    glTexImage1D (GL_TEXTURE_1D, 0, curve.bytes_per_pixel,
        curve.width, 0, GL_RGB, GL_UNSIGNED_BYTE, curve.pixel_data);

    glDisable (GL_TEXTURE_1D);
  }

  glActiveTexture (GL_TEXTURE2);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);

  gst_gl_shader_set_uniform_1i (shader, "tex", 2);

  glDisable (GL_TEXTURE_RECTANGLE_ARB);

  glActiveTexture (GL_TEXTURE1);
  glEnable (GL_TEXTURE_1D);
  glBindTexture (GL_TEXTURE_1D, effects->curve[curve_index]);

  gst_gl_shader_set_uniform_1i (shader, "curve", 1);

  glDisable (GL_TEXTURE_1D);

  gst_gl_effects_draw_texture (effects, texture);
}

static void
gst_gl_effects_heat_callback (gint width, gint height, guint texture,
    gpointer data)
{
  GstGLEffects *effects = GST_GL_EFFECTS (data);

  gst_gl_effects_luma_to_curve (effects, heat_curve, GST_GL_EFFECTS_CURVE_HEAT,
      width, height, texture);
}

void
gst_gl_effects_heat (GstGLEffects * effects)
{
  GstGLFilter *filter = GST_GL_FILTER (effects);

  gst_gl_filter_render_to_target (filter, effects->intexture,
      effects->outtexture, gst_gl_effects_heat_callback, effects);
}

static void
gst_gl_effects_sepia_callback (gint width, gint height, guint texture,
    gpointer data)
{
  GstGLEffects *effects = GST_GL_EFFECTS (data);

  gst_gl_effects_luma_to_curve (effects, sepia_curve,
      GST_GL_EFFECTS_CURVE_SEPIA, width, height, texture);
}

void
gst_gl_effects_sepia (GstGLEffects * effects)
{
  GstGLFilter *filter = GST_GL_FILTER (effects);

  gst_gl_filter_render_to_target (filter, effects->intexture,
      effects->outtexture, gst_gl_effects_sepia_callback, effects);
}

static void
gst_gl_effects_luma_xpro_callback (gint width, gint height, guint texture,
    gpointer data)
{
  GstGLEffects *effects = GST_GL_EFFECTS (data);

  gst_gl_effects_luma_to_curve (effects, luma_xpro_curve,
      GST_GL_EFFECTS_CURVE_LUMA_XPRO, width, height, texture);
}

void
gst_gl_effects_luma_xpro (GstGLEffects * effects)
{
  GstGLFilter *filter = GST_GL_FILTER (effects);

  gst_gl_filter_render_to_target (filter, effects->intexture,
      effects->outtexture, gst_gl_effects_luma_xpro_callback, effects);
}
