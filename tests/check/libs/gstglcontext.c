/* GStreamer
 *
 * Copyright (C) 2013 Matthew Waters <ystreet00@gmail.com>
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
#  include "config.h"
#endif

#include <gst/check/gstcheck.h>

#include <gst/gl/gstglcontext.h>

#include <stdio.h>

static GstGLDisplay *display;

void
setup (void)
{
  display = gst_gl_display_new ();
}

void
teardown (void)
{
  gst_object_unref (display);
}

static GLuint fbo_id, rbo, tex;
static GstGLFramebuffer *fbo;

void
init (gpointer data)
{
  /* has to be called in the thread that is going to use the framebuffer */
  fbo = gst_gl_framebuffer_new (display);

  gst_gl_framebuffer_generate (fbo, 320, 240, &fbo_id, &rbo);
  fail_if (fbo == NULL || fbo_id == 0, "failed to create framebuffer object");

  gst_gl_display_gen_texture (display, &tex, GST_VIDEO_FORMAT_RGBA, 320, 240);
  fail_if (tex == 0, "failed to create texture");
}

void
clear_tex (gpointer data)
{
  static gfloat r = 0.0, g = 0.0, b = 0.0;
  GstGLFuncs *gl = display->gl_vtable;

  gl->ClearColor (r, g, b, 1.0);
  gl->Clear (GL_COLOR_BUFFER_BIT);

  r = r > 1.0 ? 0.0 : r + 0.03;
  g = g > 1.0 ? 0.0 : g + 0.01;
  b = b > 1.0 ? 0.0 : b + 0.015;
}

void
draw_tex (gpointer data)
{
  gst_gl_framebuffer_use_v2 (fbo, 320, 240, fbo_id, rbo, tex,
      (GLCB_V2) clear_tex, data);
}

void
draw_render (gpointer data)
{
  GstGLFuncs *gl = display->gl_vtable;
  GstGLContext *context = data;
  GstGLContextClass *context_class = GST_GL_CONTEXT_GET_CLASS (context);

  /* redraw the texture into the system provided framebuffer */

  GLfloat verts[8] = { 1.0f, 1.0f,
    -1.0f, 1.0f,
    -1.0f, -1.0f,
    1.0f, -1.0f
  };
  GLfloat texcoords[8] = { 320.0f, 0.0f,
    0.0f, 0.0f,
    0.0f, 240.0f,
    320.0f, 240.0f
  };

  gl->Viewport (0, 0, 320, 240);

  gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  gl->MatrixMode (GL_PROJECTION);
  gl->LoadIdentity ();

  gl->Enable (GL_TEXTURE_RECTANGLE_ARB);
  gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, tex);

  gl->EnableClientState (GL_VERTEX_ARRAY);
  gl->EnableClientState (GL_TEXTURE_COORD_ARRAY);
  gl->VertexPointer (2, GL_FLOAT, 0, &verts);
  gl->TexCoordPointer (2, GL_FLOAT, 0, &texcoords);

  gl->DrawArrays (GL_TRIANGLE_FAN, 0, 4);

  gl->DisableClientState (GL_VERTEX_ARRAY);
  gl->DisableClientState (GL_TEXTURE_COORD_ARRAY);

  gl->Disable (GL_TEXTURE_RECTANGLE_ARB);

  context_class->swap_buffers (context);
}

GST_START_TEST (test_share)
{
  GstGLContext *context;
  GstGLWindow *window;
  GstGLContext *other_context;
  GstGLWindow *other_window;
  GError *error = NULL;
  gint i = 0;

  display = gst_gl_display_new ();
  context = gst_gl_context_new (display);
  gst_gl_display_set_context (display, context);

  window = gst_gl_window_new (display);
  gst_gl_context_set_window (context, window);

  gst_gl_context_create (context, 0, &error);

  fail_if (error != NULL, "Error creating master context %s\n",
      error ? error->message : "Unknown Error");

  other_window = gst_gl_window_new (display);

  other_context = gst_gl_context_new (display);
  gst_gl_context_set_window (other_context, other_window);

  gst_gl_context_create (other_context, context, &error);

  fail_if (error != NULL, "Error creating secondary context %s\n",
      error ? error->message : "Unknown Error");

  /* make the window visible */
  gst_gl_window_draw (window, 320, 240);

  gst_gl_window_send_message (other_window, GST_GL_WINDOW_CB (init), context);

  while (i < 10) {
    gst_gl_window_send_message (other_window, GST_GL_WINDOW_CB (draw_tex),
        context);
    gst_gl_window_send_message (window, GST_GL_WINDOW_CB (draw_render),
        context);
    i++;
  }

  gst_object_unref (window);
  gst_object_unref (other_window);
  gst_object_unref (other_context);
  gst_object_unref (context);
}

GST_END_TEST;


Suite *
gst_gl_memory_suite (void)
{
  Suite *s = suite_create ("GstGLContext");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_checked_fixture (tc_chain, setup, teardown);
  tcase_add_test (tc_chain, test_share);

  return s;
}

GST_CHECK_MAIN (gst_gl_memory);
