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

#include <gstgleffects.h>

#define USING_OPENGL(display) (gst_gl_display_get_gl_api_unlocked (display) & GST_GL_API_OPENGL)
#define USING_OPENGL3(display) (gst_gl_display_get_gl_api_unlocked (display) & GST_GL_API_OPENGL3)
#define USING_GLES(display) (gst_gl_display_get_gl_api_unlocked (display) & GST_GL_API_GLES)
#define USING_GLES2(display) (gst_gl_display_get_gl_api_unlocked (display) & GST_GL_API_GLES2)
#define USING_GLES3(display) (gst_gl_display_get_gl_api_unlocked (display) & GST_GL_API_GLES3)

static void
gst_gl_effects_identity_callback (gint width, gint height, guint texture,
    gpointer data)
{
  GstGLEffects *effects = GST_GL_EFFECTS (data);
  GstGLFilter *filter = GST_GL_FILTER (effects);

#if HAVE_OPENGL
  if (USING_OPENGL (filter->display)) {
    glMatrixMode (GL_PROJECTION);
    glLoadIdentity ();
  }
#endif
#if HAVE_GLES2
  if (USING_GLES2 (filter->display)) {
    GstGLShader *shader =
        g_hash_table_lookup (effects->shaderstable, "identity0");

    if (!shader) {
      shader = gst_gl_shader_new (filter->display);
      g_hash_table_insert (effects->shaderstable, "identity0", shader);

      if (shader) {
        GError *error = NULL;
        gst_gl_shader_set_vertex_source (shader, vertex_shader_source);
        gst_gl_shader_set_fragment_source (shader, identity_fragment_source);

        gst_gl_shader_compile (shader, &error);
        if (error) {
          GST_ERROR ("%s", error->message);
          g_error_free (error);
          error = NULL;
          gst_gl_shader_use (NULL);
        } else {
          effects->draw_attr_position_loc =
              gst_gl_shader_get_attribute_location (shader, "a_position");
          effects->draw_attr_texture_loc =
              gst_gl_shader_get_attribute_location (shader, "a_texCoord");
        }
      }
    }
    gst_gl_shader_use (shader);

    glActiveTexture (GL_TEXTURE0);
    glEnable (GL_TEXTURE_RECTANGLE_ARB);
    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);

    gst_gl_shader_set_uniform_1i (shader, "tex", 0);
  }
#endif

  gst_gl_effects_draw_texture (effects, texture, width, height);
}

void
gst_gl_effects_identity (GstGLEffects * effects)
{
  GstGLFilter *filter = GST_GL_FILTER (effects);

  gst_gl_filter_render_to_target (filter, TRUE, effects->intexture,
      effects->outtexture, gst_gl_effects_identity_callback, effects);
}
