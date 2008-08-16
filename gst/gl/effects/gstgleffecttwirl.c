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
gst_gl_effects_twirl_callback (gint width, gint height, guint texture, gpointer data)
{
  GstGLEffects* effects = GST_GL_EFFECTS (data);

  GstGLShader *shader;
          
  shader = g_hash_table_lookup (effects->shaderstable, "twirl0");
  
  if (!shader) {
    shader = gst_gl_shader_new ();
    g_hash_table_insert (effects->shaderstable, "twirl0", shader);
  }
  
  g_return_if_fail (
    gst_gl_shader_compile_and_check (shader, twirl_fragment_source,
				     GST_GL_SHADER_FRAGMENT_SOURCE));

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  
  gst_gl_shader_use (shader);

  glActiveTexture (GL_TEXTURE0);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
     
  gst_gl_shader_set_uniform_1i (shader, "tex", 0);
  
  gst_gl_shader_set_uniform_1f (shader, "width", width / 2.0); 
  gst_gl_shader_set_uniform_1f (shader, "height", height / 2.0);
  
  gst_gl_effects_draw_texture (effects, texture);
}

void
gst_gl_effects_twirl (GstGLEffects *effects) {
  GstGLFilter *filter = GST_GL_FILTER (effects);

  gst_gl_filter_render_to_target (filter, effects->intexture, effects->outtexture,
				  gst_gl_effects_twirl_callback, effects);
}
