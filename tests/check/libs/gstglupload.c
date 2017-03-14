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
static guint vbo, vbo_indices, vao;
static GstGLFramebuffer *fbo;
static GstGLMemory *fbo_tex;

static const GLfloat vertices[] = {
  1.0f, 1.0f, 0.0f, 1.0f, 0.0f,
  -1.0f, 1.0f, 0.0f, 0.0f, 0.0f,
  -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,
  1.0f, -1.0f, 0.0f, 1.0f, 1.0f
};

static GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

#define FORMAT GST_GL_RGBA
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
_check_gl_error (GstGLContext * context, gpointer data)
{
  GLuint error = context->gl_vtable->GetError ();
  fail_if (error != GL_NONE, "GL error 0x%x encountered during processing\n",
      error);
}

static void
teardown (void)
{
  gst_object_unref (upload);
  gst_object_unref (window);

  gst_gl_context_thread_add (context, (GstGLContextThreadFunc) _check_gl_error,
      NULL);
  gst_object_unref (context);
  gst_object_unref (display);
  if (shader)
    gst_object_unref (shader);
}

static void
_bind_buffer (GstGLContext * context)
{
  const GstGLFuncs *gl = context->gl_vtable;

  gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, vbo_indices);
  gl->BindBuffer (GL_ARRAY_BUFFER, vbo);

  /* Load the vertex position */
  gl->VertexAttribPointer (shader_attr_position_loc, 3, GL_FLOAT, GL_FALSE,
      5 * sizeof (GLfloat), (void *) 0);

  /* Load the texture coordinate */
  gl->VertexAttribPointer (shader_attr_texture_loc, 2, GL_FLOAT, GL_FALSE,
      5 * sizeof (GLfloat), (void *) (3 * sizeof (GLfloat)));

  gl->EnableVertexAttribArray (shader_attr_position_loc);
  gl->EnableVertexAttribArray (shader_attr_texture_loc);
}

static void
_unbind_buffer (GstGLContext * context)
{
  const GstGLFuncs *gl = context->gl_vtable;

  gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
  gl->BindBuffer (GL_ARRAY_BUFFER, 0);

  gl->DisableVertexAttribArray (shader_attr_position_loc);
  gl->DisableVertexAttribArray (shader_attr_texture_loc);
}

static void
init (gpointer data)
{
  const GstGLFuncs *gl = context->gl_vtable;
  GError *error = NULL;

  shader = gst_gl_shader_new_default (context, &error);
  fail_if (shader == NULL, "failed to create shader object %s", error->message);

  shader_attr_position_loc =
      gst_gl_shader_get_attribute_location (shader, "a_position");
  shader_attr_texture_loc =
      gst_gl_shader_get_attribute_location (shader, "a_texcoord");

  fbo = gst_gl_framebuffer_new_with_default_depth (context, WIDTH, HEIGHT);

  {
    GstGLMemoryAllocator *allocator;
    GstGLVideoAllocationParams *params;
    GstVideoInfo v_info;

    allocator = gst_gl_memory_allocator_get_default (context);
    gst_video_info_set_format (&v_info, GST_VIDEO_FORMAT_RGBA, WIDTH, HEIGHT);
    params =
        gst_gl_video_allocation_params_new (context, NULL, &v_info, 0, NULL,
        GST_GL_TEXTURE_TARGET_2D, FORMAT);
    fbo_tex =
        (GstGLMemory *) gst_gl_base_memory_alloc ((GstGLBaseMemoryAllocator *)
        allocator, (GstGLAllocationParams *) params);
    gst_object_unref (allocator);
    gst_gl_allocation_params_free ((GstGLAllocationParams *) params);
  }

  if (!vbo) {
    if (gl->GenVertexArrays) {
      gl->GenVertexArrays (1, &vao);
      gl->BindVertexArray (vao);
    }

    gl->GenBuffers (1, &vbo);
    gl->BindBuffer (GL_ARRAY_BUFFER, vbo);
    gl->BufferData (GL_ARRAY_BUFFER, 4 * 5 * sizeof (GLfloat), vertices,
        GL_STATIC_DRAW);

    gl->GenBuffers (1, &vbo_indices);
    gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, vbo_indices);
    gl->BufferData (GL_ELEMENT_ARRAY_BUFFER, sizeof (indices), indices,
        GL_STATIC_DRAW);

    if (gl->GenVertexArrays) {
      _bind_buffer (context);
      gl->BindVertexArray (0);
    }

    gl->BindBuffer (GL_ARRAY_BUFFER, 0);
    gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
  }
}

static void
deinit (gpointer data)
{
  GstGLContext *context = data;
  const GstGLFuncs *gl = context->gl_vtable;

  if (vbo)
    gl->DeleteBuffers (1, &vbo);
  vbo = 0;
  if (vbo_indices)
    gl->DeleteBuffers (1, &vbo_indices);
  vbo_indices = 0;
  if (vao)
    gl->DeleteVertexArrays (1, &vao);
  vao = 0;

  if (fbo)
    gst_object_unref (fbo);
  fbo = NULL;

  if (fbo_tex)
    gst_memory_unref (GST_MEMORY_CAST (fbo_tex));
  fbo_tex = NULL;
}

static gboolean
blit_tex (gpointer data)
{
  GstGLContext *context = data;
  const GstGLFuncs *gl = context->gl_vtable;

  gl->Clear (GL_COLOR_BUFFER_BIT);

  gst_gl_shader_use (shader);

  if (gl->GenVertexArrays)
    gl->BindVertexArray (vao);
  _bind_buffer (context);

  gl->ActiveTexture (GL_TEXTURE0);
  gl->BindTexture (GL_TEXTURE_2D, tex_id);
  gst_gl_shader_set_uniform_1i (shader, "s_texture", 0);

  gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);

  if (gl->GenVertexArrays)
    gl->BindVertexArray (0);
  _unbind_buffer (context);

  return TRUE;
}

static void
draw_render (gpointer data)
{
  gst_gl_framebuffer_draw_to_texture (fbo, fbo_tex,
      (GstGLFramebufferFunc) blit_tex, data);
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
  fail_if (res == FALSE, "Failed to upload buffer");
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
  gst_gl_window_send_message (window, GST_GL_WINDOW_CB (deinit), context);

  gst_caps_unref (in_caps);
  gst_caps_unref (out_caps);
  gst_buffer_unref (inbuf);
  gst_buffer_unref (outbuf);
}

GST_END_TEST;

GST_START_TEST (test_upload_gl_memory)
{
  GstGLBaseMemoryAllocator *base_mem_alloc;
  GstGLVideoAllocationParams *params;
  GstBuffer *buffer, *outbuf;
  GstGLMemory *gl_mem;
  GstCaps *in_caps, *out_caps;
  GstStructure *out_s;
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
      &in_info, 0, NULL, GST_GL_TEXTURE_TARGET_2D,
      GST_GL_RGBA, rgba_data, NULL, NULL);
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

  /* at this point glupload hasn't received any buffers so can output anything */
  out_caps = gst_gl_upload_transform_caps (upload, context,
      GST_PAD_SINK, in_caps, NULL);
  out_s = gst_caps_get_structure (out_caps, 0);
  fail_unless (gst_structure_has_field_typed (out_s, "texture-target",
          GST_TYPE_LIST));
  gst_caps_unref (out_caps);

  /* set some output caps without setting texture-target: this should trigger RECONFIGURE */
  out_caps = gst_caps_from_string ("video/x-raw(memory:GLMemory),"
      "format=RGBA,width=10,height=10");

  /* set caps with texture-target not fixed. This should trigger RECONFIGURE. */
  gst_gl_upload_set_caps (upload, in_caps, out_caps);
  gst_caps_unref (out_caps);

  /* push a texture-target=2D buffer */
  res = gst_gl_upload_perform_with_buffer (upload, buffer, &outbuf);
  fail_unless (res == GST_GL_UPLOAD_RECONFIGURE);
  fail_if (outbuf);

  /* now glupload has seen a 2D buffer and so wants to transform to that */
  out_caps = gst_gl_upload_transform_caps (upload, context,
      GST_PAD_SINK, in_caps, NULL);
  out_s = gst_caps_get_structure (out_caps, 0);
  fail_unless_equals_string (gst_structure_get_string (out_s, "texture-target"),
      "2D");
  gst_caps_unref (out_caps);

  /* try setting the wrong type first tho */
  out_caps = gst_caps_from_string ("video/x-raw(memory:GLMemory),"
      "format=RGBA,width=10,height=10,texture-target=RECTANGLE");
  gst_gl_upload_set_caps (upload, in_caps, out_caps);
  gst_caps_unref (out_caps);

  res = gst_gl_upload_perform_with_buffer (upload, buffer, &outbuf);
  fail_unless (res == GST_GL_UPLOAD_RECONFIGURE);
  fail_if (outbuf);

  /* finally do set the correct texture-target */
  out_caps = gst_caps_from_string ("video/x-raw(memory:GLMemory),"
      "format=RGBA,width=10,height=10,texture-target=2D");
  gst_gl_upload_set_caps (upload, in_caps, out_caps);
  gst_caps_unref (out_caps);

  res = gst_gl_upload_perform_with_buffer (upload, buffer, &outbuf);
  fail_unless (res == GST_GL_UPLOAD_DONE, "Failed to upload buffer");
  fail_unless (GST_IS_BUFFER (outbuf));

  gst_gl_window_set_preferred_size (window, WIDTH, HEIGHT);
  gst_gl_window_draw (window);
  gst_gl_window_send_message (window, GST_GL_WINDOW_CB (init), context);

  while (i < 2) {
    gst_gl_window_send_message (window, GST_GL_WINDOW_CB (draw_render),
        context);
    i++;
  }
  gst_gl_window_send_message (window, GST_GL_WINDOW_CB (deinit), context);

  gst_caps_unref (in_caps);
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
  tcase_add_test (tc_chain, test_upload_gl_memory);

  return s;
}

GST_CHECK_MAIN (gst_gl_upload);
