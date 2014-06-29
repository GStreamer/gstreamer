/* GStreamer
 *
 * Copyright (C) 2014 Matthew Waters <ystreet00@gmail.com>
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
#include <gst/gl/gstglupload.h>

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
static GstGLContext *context;
static GstGLWindow *window;
static GstGLUpload *upload;
static guint tex_id;
#if GST_GL_HAVE_GLES2
static GError *error;
static GstGLShader *shader;
static GLint shader_attr_position_loc;
static GLint shader_attr_texture_loc;
#endif


#define FORMAT GST_VIDEO_GL_TEXTURE_TYPE_RGBA
#define WIDTH 10
#define HEIGHT 10
#define RED 0xff, 0x00, 0x00, 0xff
#define GREEN 0x00, 0xff, 0x00, 0xff
#define BLUE 0x00, 0x00, 0xff, 0xff

static gchar rgba_data[] =
    { RED, GREEN, BLUE, RED, GREEN, BLUE, RED, GREEN, BLUE, RED,
  GREEN, BLUE, RED, GREEN, BLUE, RED, GREEN, BLUE, RED, GREEN,
  BLUE, RED, GREEN, BLUE, RED, GREEN, BLUE, RED, GREEN, BLUE,
  RED, RED, RED, RED, RED, RED, RED, RED, RED, RED,
  GREEN, GREEN, GREEN, GREEN, GREEN, GREEN, GREEN, GREEN, GREEN, GREEN,
  BLUE, BLUE, BLUE, BLUE, BLUE, BLUE, BLUE, BLUE, BLUE, BLUE,
  RED, GREEN, BLUE, RED, GREEN, BLUE, RED, GREEN, BLUE, RED,
  RED, GREEN, BLUE, RED, GREEN, BLUE, RED, GREEN, BLUE, RED,
  RED, GREEN, BLUE, RED, GREEN, BLUE, RED, GREEN, BLUE, RED,
  RED, GREEN, BLUE, RED, GREEN, BLUE, RED, GREEN, BLUE, RED
};

static void
setup (void)
{
  GError *error = NULL;

  display = gst_gl_display_new ();
  context = gst_gl_context_new (display);

  gst_gl_context_create (context, 0, &error);
  window = gst_gl_context_get_window (context);

  fail_if (error != NULL, "Error creating context: %s\n",
      error ? error->message : "Unknown Error");

  upload = gst_gl_upload_new (context);
}

static void
teardown (void)
{
  GLuint error = context->gl_vtable->GetError ();
  fail_if (error != GL_NONE, "GL error 0x%x encountered during processing\n",
      error);

  gst_object_unref (upload);
  gst_object_unref (window);
  gst_object_unref (context);
  gst_object_unref (display);
}

static void
init (gpointer data)
{
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

    gl->Viewport (0, 0, 10, 10);

    gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    gl->MatrixMode (GL_PROJECTION);
    gl->LoadIdentity ();

    gl->Enable (GL_TEXTURE_2D);
    gl->BindTexture (GL_TEXTURE_2D, tex_id);

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
    gl->BindTexture (GL_TEXTURE_2D, tex_id);
    gst_gl_shader_set_uniform_1i (shader, "s_texture", 0);

    gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
  }
#endif

  context_class->swap_buffers (context);
}

GST_START_TEST (test_upload_data)
{
  gpointer data[GST_VIDEO_MAX_PLANES] = { rgba_data, NULL, NULL, NULL };
  GstVideoInfo in_info;
  gboolean res;
  gint i = 0;

  gst_video_info_set_format (&in_info, GST_VIDEO_FORMAT_RGBA, WIDTH, HEIGHT);

  gst_gl_upload_set_format (upload, &in_info);

  res = gst_gl_upload_perform_with_data (upload, &tex_id, data);
  fail_if (res == FALSE, "Failed to upload buffer: %s\n",
      gst_gl_context_get_error ());

  gst_gl_window_draw (window, WIDTH, HEIGHT);

  gst_gl_window_send_message (window, GST_GL_WINDOW_CB (init), context);

  while (i < 2) {
    gst_gl_window_send_message (window, GST_GL_WINDOW_CB (draw_render),
        context);
    i++;
  }
}

GST_END_TEST;

GST_START_TEST (test_upload_buffer)
{
  GstBuffer *buffer;
  GstGLMemory *gl_mem;
  GstVideoInfo in_info;
  gint i = 0;
  gboolean res;

  gst_video_info_set_format (&in_info, GST_VIDEO_FORMAT_RGBA, WIDTH, HEIGHT);

  /* create GL buffer */
  buffer = gst_buffer_new ();
  gl_mem = gst_gl_memory_wrapped (context, FORMAT, WIDTH, HEIGHT, WIDTH * 4,
      rgba_data, NULL, NULL);
  gst_buffer_append_memory (buffer, (GstMemory *) gl_mem);

  gst_gl_upload_set_format (upload, &in_info);

  res = gst_gl_upload_perform_with_buffer (upload, buffer, &tex_id);
  fail_if (res == FALSE, "Failed to upload buffer: %s\n",
      gst_gl_context_get_error ());

  gst_gl_window_draw (window, WIDTH, HEIGHT);
  gst_gl_window_send_message (window, GST_GL_WINDOW_CB (init), context);

  while (i < 2) {
    gst_gl_window_send_message (window, GST_GL_WINDOW_CB (draw_render),
        context);
    i++;
  }

  gst_gl_upload_release_buffer (upload);
  gst_buffer_unref (buffer);
}

GST_END_TEST;

GST_START_TEST (test_upload_meta_producer)
{
  GstBuffer *buffer;
  GstGLMemory *gl_mem;
  GstVideoInfo in_info;
  GstVideoGLTextureUploadMeta *gl_upload_meta;
  guint tex_ids[] = { 0, 0, 0, 0 };
  GstGLUploadMeta *upload_meta;
  gboolean res;
  gint i = 0;

  gst_video_info_set_format (&in_info, GST_VIDEO_FORMAT_RGBA, WIDTH, HEIGHT);

  /* create GL buffer */
  buffer = gst_buffer_new ();
  gl_mem = gst_gl_memory_wrapped (context, FORMAT, WIDTH, HEIGHT, WIDTH * 4,
      rgba_data, NULL, NULL);
  gst_buffer_append_memory (buffer, (GstMemory *) gl_mem);

  gst_gl_context_gen_texture (context, &tex_ids[0], GST_VIDEO_FORMAT_RGBA,
      WIDTH, HEIGHT);

  upload_meta = gst_gl_upload_meta_new (context);
  gst_gl_upload_meta_set_format (upload_meta, &in_info);

  gst_gl_upload_set_format (upload, &in_info);
  gst_buffer_add_video_meta_full (buffer, 0, GST_VIDEO_FORMAT_RGBA, WIDTH,
      HEIGHT, 1, in_info.offset, in_info.stride);
  gst_gl_upload_meta_add_to_buffer (upload_meta, buffer);

  gl_upload_meta = gst_buffer_get_video_gl_texture_upload_meta (buffer);
  fail_if (gl_upload_meta == NULL, "Failed to add GstVideoGLTextureUploadMeta"
      " to buffer\n");

  res = gst_video_gl_texture_upload_meta_upload (gl_upload_meta, tex_ids);
  fail_if (res == FALSE, "Failed to upload GstVideoGLTextureUploadMeta\n");

  tex_id = tex_ids[0];
  gst_gl_window_draw (window, WIDTH, HEIGHT);
  gst_gl_window_send_message (window, GST_GL_WINDOW_CB (init), context);

  while (i < 2) {
    gst_gl_window_send_message (window, GST_GL_WINDOW_CB (draw_render),
        context);
    i++;
  }

  gst_object_unref (upload_meta);
  gst_gl_context_del_texture (context, &tex_ids[0]);
  gst_buffer_unref (buffer);
}

GST_END_TEST;


static Suite *
gst_gl_upload_suite (void)
{
  Suite *s = suite_create ("GstGLUpload");
  TCase *tc_chain = tcase_create ("upload");

  suite_add_tcase (s, tc_chain);
  tcase_add_checked_fixture (tc_chain, setup, teardown);
  tcase_add_test (tc_chain, test_upload_data);
  tcase_add_test (tc_chain, test_upload_buffer);
  tcase_add_test (tc_chain, test_upload_meta_producer);

  return s;
}

GST_CHECK_MAIN (gst_gl_upload);
