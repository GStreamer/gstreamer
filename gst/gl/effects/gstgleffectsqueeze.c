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

static void
gst_gl_effects_squeeze_callback (gint width, gint height, guint texture,
    gpointer data)
{
  GstGLEffects *effects = GST_GL_EFFECTS (data);

  GstGLShader *shader;

  shader = g_hash_table_lookup (effects->shaderstable, "squeeze0");

  if (!shader) {
    shader = gst_gl_shader_new ();
    g_hash_table_insert (effects->shaderstable, "squeeze0", shader);

#ifdef OPENGL_ES2
    if (shader) {
      GError *error = NULL;
      gst_gl_shader_set_vertex_source (shader, vertex_shader_source);
      gst_gl_shader_set_fragment_source (shader, squeeze_fragment_source);

      gst_gl_shader_compile (shader, &error);
      if (error) {
        GstGLFilter *filter = GST_GL_FILTER (effects);
        gst_gl_display_set_error (filter->display,
            "Failed to initialize squeeze shader, %s", error->message);
        g_error_free (error);
        error = NULL;
        gst_gl_shader_use (NULL);
        GST_ELEMENT_ERROR (effects, RESOURCE, NOT_FOUND,
            GST_GL_DISPLAY_ERR_MSG (GST_GL_FILTER (effects)->display), (NULL));
      } else {
        effects->draw_attr_position_loc =
            gst_gl_shader_get_attribute_location (shader, "a_position");
        effects->draw_attr_texture_loc =
            gst_gl_shader_get_attribute_location (shader, "a_texCoord");
      }
    }
#endif
  }
#ifndef OPENGL_ES2
  if (!gst_gl_shader_compile_and_check (shader,
          squeeze_fragment_source, GST_GL_SHADER_FRAGMENT_SOURCE)) {
    gst_gl_display_set_error (GST_GL_FILTER (effects)->display,
        "Failed to initialize squeeze shader");
    GST_ELEMENT_ERROR (effects, RESOURCE, NOT_FOUND,
        GST_GL_DISPLAY_ERR_MSG (GST_GL_FILTER (effects)->display), (NULL));
    return;
  }
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
#endif

  gst_gl_shader_use (shader);

  glActiveTexture (GL_TEXTURE0);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);

  gst_gl_shader_set_uniform_1i (shader, "tex", 0);

#ifndef OPENGL_ES2
  gst_gl_shader_set_uniform_1f (shader, "width", (gfloat) width / 2.0f);
  gst_gl_shader_set_uniform_1f (shader, "height", (gfloat) height / 2.0f);
#endif

  gst_gl_effects_draw_texture (effects, texture, width, height);
}

void
gst_gl_effects_squeeze (GstGLEffects * effects)
{
  GstGLFilter *filter = GST_GL_FILTER (effects);

  gst_gl_filter_render_to_target (filter, TRUE, effects->intexture,
      effects->outtexture, gst_gl_effects_squeeze_callback, effects);
}
