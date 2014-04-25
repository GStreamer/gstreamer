/* GStreamer
 *
 * unit test for state changes on all elements
 *
 * Copyright (C) <2012> Matthew Waters <ystreet00@gmail.com>
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

#include <gst/gl/gstglmemory.h>

#include <stdio.h>

static GstGLDisplay *display;
static GstGLContext *context;

static void
setup (void)
{
  display = gst_gl_display_new ();
  context = gst_gl_context_new (display);
  gst_gl_context_create (context, 0, NULL);
  gst_gl_memory_init ();
}

static void
teardown (void)
{
  gst_object_unref (display);
  gst_object_unref (context);
}

GST_START_TEST (test_basic)
{
  GstMemory *mem, *mem2;
  GstGLMemory *gl_mem, *gl_mem2;
  GstAllocator *gl_allocator;
  gint i;
  static GstVideoGLTextureType formats[] = {
    GST_VIDEO_GL_TEXTURE_TYPE_RGBA, GST_VIDEO_GL_TEXTURE_TYPE_RGB,
    GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA,
    GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE
  };

  for (i = 0; i < G_N_ELEMENTS (formats); i++) {
    gsize width = 320, height = 240, stride = 324;

    gl_allocator = gst_allocator_find (GST_GL_MEMORY_ALLOCATOR);
    fail_if (gl_allocator == NULL);

    /* test allocator creation */
    ASSERT_WARNING (mem = gst_allocator_alloc (gl_allocator, 0, NULL););
    mem = gst_gl_memory_alloc (context, formats[i], width, height, stride);
    fail_if (mem == NULL);
    gl_mem = (GstGLMemory *) mem;

    /* test init params */
    fail_if (gl_mem->width != width);
    fail_if (gl_mem->height != height);
    fail_if (gl_mem->stride != stride);
    fail_if (gl_mem->context != context);
    fail_if (gl_mem->tex_id == 0);

    /* copy the memory */
    mem2 = gst_memory_copy (mem, 0, -1);
    fail_if (mem2 == NULL);
    gl_mem2 = (GstGLMemory *) mem2;

    /* test params */
    fail_if (gl_mem->tex_id == gl_mem2->tex_id);
    fail_if (gl_mem->tex_type != gl_mem2->tex_type);
    fail_if (gl_mem->width != gl_mem2->width);
    fail_if (gl_mem->stride != gl_mem2->stride);
    fail_if (gl_mem->context != gl_mem2->context);

    if (gst_gl_context_get_error ())
      printf ("%s\n", gst_gl_context_get_error ());
    fail_if (gst_gl_context_get_error () != NULL);

    gst_memory_unref (mem);
    gst_memory_unref (mem2);

    gst_object_unref (gl_allocator);
  }
}

GST_END_TEST;


static Suite *
gst_gl_memory_suite (void)
{
  Suite *s = suite_create ("GstGLMemory");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_checked_fixture (tc_chain, setup, teardown);
  tcase_add_test (tc_chain, test_basic);

  return s;
}

GST_CHECK_MAIN (gst_gl_memory);
