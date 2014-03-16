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

static void
gst_gl_effects_stretch_callback (gint width, gint height, guint texture,
    gpointer data)
{
  GstGLShader *shader;
  GstGLEffects *effects = GST_GL_EFFECTS (data);
  GstGLFilter *filter = GST_GL_FILTER (effects);
  GstGLContext *context = filter->context;
  GstGLFuncs *gl = context->gl_vtable;

  shader = g_hash_table_lookup (effects->shaderstable, "stretch0");

  if (!shader) {
    shader = gst_gl_shader_new (context);
    g_hash_table_insert (effects->shaderstable, (gchar *) "stretch0", shader);
  }

  if (!gst_gl_shader_compile_and_check (shader,
          stretch_fragment_source, GST_GL_SHADER_FRAGMENT_SOURCE)) {
    gst_gl_context_set_error (context, "Failed to initialize stretch shader");
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

  gst_gl_shader_set_uniform_1f (shader, "width", (gfloat) width / 2.0f);
  gst_gl_shader_set_uniform_1f (shader, "height", (gfloat) height / 2.0f);

  gst_gl_filter_draw_texture (filter, texture, width, height);
}

void
gst_gl_effects_stretch (GstGLEffects * effects)
{
  GstGLFilter *filter = GST_GL_FILTER (effects);

  gst_gl_filter_render_to_target (filter, TRUE, effects->intexture,
      effects->outtexture, gst_gl_effects_stretch_callback, effects);
}
