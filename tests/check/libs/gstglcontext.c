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

#if GST_GL_HAVE_GLES2
/* *INDENT-OFF* */
static const gchar *vertex_shader_str_gles2 =
      "attribute vec4 a_position;   \n"
      "attribute vec2 a_texCoord;   \n"
      "varying vec2 v_texCoord;     \n"
      "void main()                  \n"
      "{                            \n"
      "   gl_Position = a_position; \n"
      "   v_texCoord = a_texCoord;  \n"
      "}                            \n";

static const gchar *fragment_shader_str_gles2 =
      "precision mediump float;                            \n"
      "varying vec2 v_texCoord;                            \n"
      "uniform sampler2D s_texture;                        \n"
      "void main()                                         \n"
      "{                                                   \n"
      "  gl_FragColor = texture2D( s_texture, v_texCoord );\n"
      "}                                                   \n";
/* *INDENT-ON* */
#endif

static GstGLDisplay *display;

static void
setup (void)
{
  display = gst_gl_display_new ();
}

static void
teardown (void)
{
  gst_object_unref (display);
}

static GLuint fbo_id, rbo, tex;
static GstGLFramebuffer *fbo;
#if GST_GL_HAVE_GLES2
static GError *error;
static GstGLShader *shader;
static GLint shader_attr_position_loc;
static GLint shader_attr_texture_loc;
#endif

static void
init (gpointer data)
{
  GstGLContext *context = data;

  /* has to be called in the thread that is going to use the framebuffer */
  fbo = gst_gl_framebuffer_new (context);

  gst_gl_framebuffer_generate (fbo, 320, 240, &fbo_id, &rbo);
  fail_if (fbo == NULL || fbo_id == 0, "failed to create framebuffer object");

  gst_gl_context_gen_texture (context, &tex, GST_VIDEO_FORMAT_RGBA, 320, 240);
  fail_if (tex == 0, "failed to create texture");

#if GST_GL_HAVE_GLES2
  if (gst_gl_context_get_gl_api (context) & GST_GL_API_GLES2) {
    shader = gst_gl_shader_new (context);
    fail_if (shader == NULL, "failed to create shader object");

    gst_gl_shader_set_vertex_source (shader, vertex_shader_str_gles2);
    gst_gl_shader_set_fragment_source (shader, fragment_shader_str_gles2);

    error = NULL;
    gst_gl_shader_compile (shader, &error);
    fail_if (error != NULL, "Error compiling shader %s\n",
        error ? error->message : "Unknown Error");

    shader_attr_position_loc =
        gst_gl_shader_get_attribute_location (shader, "a_position");
    shader_attr_texture_loc =
        gst_gl_shader_get_attribute_location (shader, "a_texCoord");
  }
#endif
}

static void
deinit (gpointer data)
{
  GstGLContext *context = data;
  GstGLFuncs *gl = context->gl_vtable;
  gl->DeleteTextures (1, &tex);;
  gst_object_unref (fbo);
#if GST_GL_HAVE_GLES2
  if (gst_gl_context_get_gl_api (context) & GST_GL_API_GLES2)
    gst_object_unref (shader);
#endif
}

static void
clear_tex (gpointer data)
{
  GstGLContext *context = data;
  GstGLFuncs *gl = context->gl_vtable;
  static gfloat r = 0.0, g = 0.0, b = 0.0;

  gl->ClearColor (r, g, b, 1.0);
  gl->Clear (GL_COLOR_BUFFER_BIT);

  r = r > 1.0 ? 0.0 : r + 0.03;
  g = g > 1.0 ? 0.0 : g + 0.01;
  b = b > 1.0 ? 0.0 : b + 0.015;
}

static void
draw_tex (gpointer data)
{
  gst_gl_framebuffer_use_v2 (fbo, 320, 240, fbo_id, rbo, tex,
      (GLCB_V2) clear_tex, data);
}

static void
draw_render (gpointer data)
{
  GstGLContext *context = data;
  GstGLContextClass *context_class = GST_GL_CONTEXT_GET_CLASS (context);
  const GstGLFuncs *gl = context->gl_vtable;

  /* redraw the texture into the system provided framebuffer */

#if GST_GL_HAVE_OPENGL
  if (gst_gl_context_get_gl_api (context) & GST_GL_API_OPENGL) {
    GLfloat verts[8] = { 1.0f, 1.0f,
      -1.0f, 1.0f,
      -1.0f, -1.0f,
      1.0f, -1.0f
    };
    GLfloat texcoords[8] = { 1.0f, 0.0f,
      0.0f, 0.0f,
      0.0f, 1.0f,
      1.0f, 1.0f
    };

    gl->Viewport (0, 0, 320, 240);

    gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    gl->MatrixMode (GL_PROJECTION);
    gl->LoadIdentity ();

    gl->Enable (GL_TEXTURE_2D);
    gl->BindTexture (GL_TEXTURE_2D, tex);

    gl->EnableClientState (GL_VERTEX_ARRAY);
    gl->EnableClientState (GL_TEXTURE_COORD_ARRAY);
    gl->VertexPointer (2, GL_FLOAT, 0, &verts);
    gl->TexCoordPointer (2, GL_FLOAT, 0, &texcoords);

    gl->DrawArrays (GL_TRIANGLE_FAN, 0, 4);

    gl->DisableClientState (GL_VERTEX_ARRAY);
    gl->DisableClientState (GL_TEXTURE_COORD_ARRAY);

    gl->Disable (GL_TEXTURE_2D);
  }
#endif
#if GST_GL_HAVE_GLES2
  if (gst_gl_context_get_gl_api (context) & GST_GL_API_GLES2) {
    const GLfloat vVertices[] = { 1.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      -1.0f, 1.0f, 0.0f,
      0.0f, 0.0f,
      -1.0f, -1.0f, 0.0f,
      0.0f, 1.0f,
      1.0f, -1.0f, 0.0f,
      1.0f, 1.0f
    };

    GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

    gl->Clear (GL_COLOR_BUFFER_BIT);

    gst_gl_shader_use (shader);

    /* Load the vertex position */
    gl->VertexAttribPointer (shader_attr_position_loc, 3,
        GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), vVertices);

    /* Load the texture coordinate */
    gl->VertexAttribPointer (shader_attr_texture_loc, 2,
        GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), &vVertices[3]);

    gl->EnableVertexAttribArray (shader_attr_position_loc);
    gl->EnableVertexAttribArray (shader_attr_texture_loc);

    gl->ActiveTexture (GL_TEXTURE0);
    gl->BindTexture (GL_TEXTURE_2D, tex);
    gst_gl_shader_set_uniform_1i (shader, "s_texture", 0);

    gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
  }
#endif

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

  context = gst_gl_context_new (display);

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

  gst_gl_window_send_message (other_window, GST_GL_WINDOW_CB (deinit), context);

  gst_object_unref (window);
  gst_object_unref (other_window);
  gst_object_unref (other_context);
  gst_object_unref (context);
}

GST_END_TEST;

GST_START_TEST (test_wrapped_context)
{
  GstGLContext *context, *other_context, *wrapped_context;
  GstGLWindow *window, *other_window;
  GError *error = NULL;
  gint i = 0;
  guintptr handle;
  GstGLPlatform platform;
  GstGLAPI apis;

  context = gst_gl_context_new (display);

  window = gst_gl_window_new (display);
  gst_gl_context_set_window (context, window);

  gst_gl_context_create (context, 0, &error);

  fail_if (error != NULL, "Error creating master context %s\n",
      error ? error->message : "Unknown Error");

  handle = gst_gl_context_get_gl_context (context);
  platform = gst_gl_context_get_gl_platform (context);
  apis = gst_gl_context_get_gl_api (context);

  wrapped_context =
      gst_gl_context_new_wrapped (display, handle, platform, apis);

  other_context = gst_gl_context_new (display);
  other_window = gst_gl_window_new (display);
  gst_gl_context_set_window (other_context, other_window);

  gst_gl_context_create (other_context, wrapped_context, &error);

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

  gst_gl_window_send_message (other_window, GST_GL_WINDOW_CB (deinit), context);

  gst_object_unref (window);
  gst_object_unref (other_window);
  gst_object_unref (other_context);
  gst_object_unref (context);
  gst_object_unref (wrapped_context);
}

GST_END_TEST;


static Suite *
gst_gl_context_suite (void)
{
  Suite *s = suite_create ("GstGLContext");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_checked_fixture (tc_chain, setup, teardown);
  tcase_add_test (tc_chain, test_share);
  tcase_add_test (tc_chain, test_wrapped_context);

  return s;
}

GST_CHECK_MAIN (gst_gl_context);
