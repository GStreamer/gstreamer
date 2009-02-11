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
gst_gl_effects_identity_callback (gint width, gint height, guint texture,
    gpointer data)
{
  GstGLEffects *effects = GST_GL_EFFECTS (data);

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  gst_gl_effects_draw_texture (effects, texture);
}

void
gst_gl_effects_identity (GstGLEffects * effects)
{
  GstGLFilter *filter = GST_GL_FILTER (effects);

  gst_gl_filter_render_to_target (filter, effects->intexture,
      effects->outtexture, gst_gl_effects_identity_callback, effects);
}
