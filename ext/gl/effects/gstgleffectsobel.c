/*
 * GStreamer
 * Copyright (C) 2008-2010 Filippo Argiolas <filippo.argiolas@gmail.com>
 * Copyright (C) 2015 Michał Dębski <debski.mi.zd@gmail.com>
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
gst_gl_effects_sobel_callback_desaturate (gint width, gint height,
    guint texture, gpointer data)
{
  GstGLShader *shader = NULL;
  GstGLEffects *effects = GST_GL_EFFECTS (data);
  GstGLFilter *filter = GST_GL_FILTER (effects);

  if (NULL != (shader = gst_gl_effects_get_fragment_shader (effects, "desat0",
              desaturate_fragment_source_gles2,
              desaturate_fragment_source_opengl))) {
    GstGLFuncs *gl = GST_GL_BASE_FILTER (filter)->context->gl_vtable;

#if GST_GL_HAVE_OPENGL
    if (USING_OPENGL (GST_GL_BASE_FILTER (filter)->context)) {
      gl->MatrixMode (GL_PROJECTION);
      gl->LoadIdentity ();
    }
#endif

    gst_gl_shader_use (shader);

    gl->ActiveTexture (GL_TEXTURE0);
    gl->Enable (GL_TEXTURE_2D);
    gl->BindTexture (GL_TEXTURE_2D, texture);
    gl->Disable (GL_TEXTURE_2D);

    gst_gl_shader_set_uniform_1i (shader, "tex", 0);

    gst_gl_filter_draw_texture (filter, texture, width, height);
  }
}

static void
gst_gl_effects_sobel_callback_hconv (gint width, gint height, guint texture,
    gpointer data)
{
  GstGLShader *shader = NULL;
  GstGLEffects *effects = GST_GL_EFFECTS (data);
  GstGLFilter *filter = GST_GL_FILTER (effects);

  if (NULL != (shader = gst_gl_effects_get_fragment_shader (effects, "hconv0",
              sep_sobel_hconv3_fragment_source_gles2,
              sep_sobel_hconv3_fragment_source_opengl))) {
    GstGLFuncs *gl = GST_GL_BASE_FILTER (filter)->context->gl_vtable;

#if GST_GL_HAVE_OPENGL
    if (USING_OPENGL (GST_GL_BASE_FILTER (filter)->context)) {
      gl->MatrixMode (GL_PROJECTION);
      gl->LoadIdentity ();
    }
#endif

    gst_gl_shader_use (shader);

    gl->ActiveTexture (GL_TEXTURE0);
    gl->Enable (GL_TEXTURE_2D);
    gl->BindTexture (GL_TEXTURE_2D, texture);
    gl->Disable (GL_TEXTURE_2D);

    gst_gl_shader_set_uniform_1i (shader, "tex", 0);
    gst_gl_shader_set_uniform_1f (shader, "width", width);

    gst_gl_filter_draw_texture (filter, texture, width, height);
  }
}

static void
gst_gl_effects_sobel_callback_vconv (gint width, gint height, guint texture,
    gpointer data)
{
  GstGLShader *shader = NULL;
  GstGLEffects *effects = GST_GL_EFFECTS (data);
  GstGLFilter *filter = GST_GL_FILTER (effects);

  if (NULL != (shader = gst_gl_effects_get_fragment_shader (effects, "vconv0",
              sep_sobel_vconv3_fragment_source_gles2,
              sep_sobel_vconv3_fragment_source_opengl))) {
    GstGLFuncs *gl = GST_GL_BASE_FILTER (filter)->context->gl_vtable;

#if GST_GL_HAVE_OPENGL
    if (USING_OPENGL (GST_GL_BASE_FILTER (filter)->context)) {
      gl->MatrixMode (GL_PROJECTION);
      gl->LoadIdentity ();
    }
#endif

    gst_gl_shader_use (shader);

    gl->ActiveTexture (GL_TEXTURE0);
    gl->Enable (GL_TEXTURE_2D);
    gl->BindTexture (GL_TEXTURE_2D, texture);
    gl->Disable (GL_TEXTURE_2D);

    gst_gl_shader_set_uniform_1i (shader, "tex", 0);
    gst_gl_shader_set_uniform_1f (shader, "height", height);

    gst_gl_filter_draw_texture (filter, texture, width, height);
  }
}

static void
gst_gl_effects_sobel_callback_length (gint width, gint height, guint texture,
    gpointer data)
{
  GstGLShader *shader = NULL;
  GstGLEffects *effects = GST_GL_EFFECTS (data);
  GstGLFilter *filter = GST_GL_FILTER (effects);

  if (NULL != (shader = gst_gl_effects_get_fragment_shader (effects, "len0",
              sep_sobel_length_fragment_source_gles2,
              sep_sobel_length_fragment_source_opengl))) {
    GstGLFuncs *gl = GST_GL_BASE_FILTER (filter)->context->gl_vtable;

#if GST_GL_HAVE_OPENGL
    if (USING_OPENGL (GST_GL_BASE_FILTER (filter)->context)) {
      gl->MatrixMode (GL_PROJECTION);
      gl->LoadIdentity ();
    }
#endif

    gst_gl_shader_use (shader);

    gl->ActiveTexture (GL_TEXTURE0);
    gl->Enable (GL_TEXTURE_2D);
    gl->BindTexture (GL_TEXTURE_2D, texture);
    gl->Disable (GL_TEXTURE_2D);

    gst_gl_shader_set_uniform_1i (shader, "tex", 0);
    gst_gl_shader_set_uniform_1i (shader, "invert", effects->invert);

    gst_gl_filter_draw_texture (filter, texture, width, height);
  }
}

void
gst_gl_effects_sobel (GstGLEffects * effects)
{
  GstGLFilter *filter = GST_GL_FILTER (effects);

  gst_gl_filter_render_to_target (filter, TRUE,
      effects->intexture, effects->midtexture[0],
      gst_gl_effects_sobel_callback_desaturate, effects);
  gst_gl_filter_render_to_target (filter, FALSE,
      effects->midtexture[0], effects->midtexture[1],
      gst_gl_effects_sobel_callback_hconv, effects);
  gst_gl_filter_render_to_target (filter, FALSE,
      effects->midtexture[1], effects->midtexture[0],
      gst_gl_effects_sobel_callback_vconv, effects);
  gst_gl_filter_render_to_target (filter, FALSE,
      effects->midtexture[0], effects->outtexture,
      gst_gl_effects_sobel_callback_length, effects);
}
