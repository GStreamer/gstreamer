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

static GstGLDisplay *display;
static GstGLContext *context;
static GstGLWindow *window;
static GstGLUpload *upload;
static guint tex_id;
static GstGLShader *shader;
static GLint shader_attr_position_loc;
static GLint shader_attr_texture_loc;

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
  GError *error = NULL;

  shader = gst_gl_shader_new_default (context, &error);
  fail_if (shader == NULL, "failed to create shader object %s", error->message);

  shader_attr_position_loc =
      gst_gl_shader_get_attribute_location (shader, "a_position");
  shader_attr_texture_loc =
      gst_gl_shader_get_attribute_location (shader, "a_texCoord");
}

static void
draw_render (gpointer data)
{
  GstGLContext *context = data;
  GstGLContextClass *context_class = GST_GL_CONTEXT_GET_CLASS (context);
  const GstGLFuncs *gl = context->gl_vtable;
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

  context_class->swap_buffers (context);
}

GST_START_TEST (test_upload_data)
{
  GstCaps *in_caps, *out_caps;
  GstBuffer *inbuf, *outbuf;
  GstMapInfo map_info;
  gboolean res;
  gint i = 0;

  in_caps = gst_caps_from_string ("video/x-raw,format=RGBA,"
      "width=10,height=10");
  out_caps = gst_caps_from_string ("video/x-raw(memory:GLMemory),"
      "format=RGBA,width=10,height=10");

  gst_gl_upload_set_caps (upload, in_caps, out_caps);

  inbuf = gst_buffer_new_wrapped_full (0, rgba_data, WIDTH * HEIGHT * 4,
      0, WIDTH * HEIGHT * 4, NULL, NULL);

  res = gst_gl_upload_perform_with_buffer (upload, inbuf, &outbuf);
  fail_if (res == FALSE, "Failed to upload buffer: %s\n",
      gst_gl_context_get_error ());
  fail_unless (GST_IS_BUFFER (outbuf));

  res = gst_buffer_map (outbuf, &map_info, GST_MAP_READ | GST_MAP_GL);
  fail_if (res == FALSE, "Failed to map gl memory");

  tex_id = *(guint *) map_info.data;

  gst_buffer_unmap (outbuf, &map_info);

  gst_gl_window_set_preferred_size (window, WIDTH, HEIGHT);
  gst_gl_window_draw (window);

  gst_gl_window_send_message (window, GST_GL_WINDOW_CB (init), context);

  while (i < 2) {
    gst_gl_window_send_message (window, GST_GL_WINDOW_CB (draw_render),
        context);
    i++;
  }

  gst_caps_unref (in_caps);
  gst_caps_unref (out_caps);
  gst_buffer_unref (inbuf);
  gst_buffer_unref (outbuf);
}

GST_END_TEST;

GST_START_TEST (test_upload_buffer)
{
  GstGLBaseMemoryAllocator *base_mem_alloc;
  GstGLVideoAllocationParams *params;
  GstBuffer *buffer, *outbuf;
  GstGLMemory *gl_mem;
  GstCaps *in_caps, *out_caps;
  GstVideoInfo in_info;
  GstMapInfo map_info;
  gint i = 0;
  gboolean res;

  base_mem_alloc =
      GST_GL_BASE_MEMORY_ALLOCATOR (gst_allocator_find
      (GST_GL_MEMORY_ALLOCATOR_NAME));

  in_caps = gst_caps_from_string ("video/x-raw,format=RGBA,width=10,height=10");
  gst_video_info_from_caps (&in_info, in_caps);

  /* create GL buffer */
  buffer = gst_buffer_new ();
  params = gst_gl_video_allocation_params_new_wrapped_data (context, NULL,
      &in_info, 0, NULL, GST_GL_TEXTURE_TARGET_2D, rgba_data, NULL, NULL);
  gl_mem = (GstGLMemory *) gst_gl_base_memory_alloc (base_mem_alloc,
      (GstGLAllocationParams *) params);
  gst_gl_allocation_params_free ((GstGLAllocationParams *) params);

  res =
      gst_memory_map ((GstMemory *) gl_mem, &map_info,
      GST_MAP_READ | GST_MAP_GL);
  fail_if (res == FALSE, "Failed to map gl memory\n");
  tex_id = *(guint *) map_info.data;
  gst_memory_unmap ((GstMemory *) gl_mem, &map_info);

  gst_buffer_append_memory (buffer, (GstMemory *) gl_mem);

  out_caps = gst_caps_from_string ("video/x-raw(memory:GLMemory),"
      "format=RGBA,width=10,height=10");

  gst_gl_upload_set_caps (upload, in_caps, out_caps);

  res = gst_gl_upload_perform_with_buffer (upload, buffer, &outbuf);
  fail_if (res == FALSE, "Failed to upload buffer: %s\n",
      gst_gl_context_get_error ());
  fail_unless (GST_IS_BUFFER (outbuf));

  gst_gl_window_set_preferred_size (window, WIDTH, HEIGHT);
  gst_gl_window_draw (window);
  gst_gl_window_send_message (window, GST_GL_WINDOW_CB (init), context);

  while (i < 2) {
    gst_gl_window_send_message (window, GST_GL_WINDOW_CB (draw_render),
        context);
    i++;
  }

  gst_caps_unref (in_caps);
  gst_caps_unref (out_caps);
  gst_buffer_unref (buffer);
  gst_buffer_unref (outbuf);
  gst_object_unref (base_mem_alloc);
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

  return s;
}

GST_CHECK_MAIN (gst_gl_upload);
