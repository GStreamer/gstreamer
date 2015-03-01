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
  gint i, j;
  static GstVideoFormat formats[] = {
    GST_VIDEO_FORMAT_RGBA, GST_VIDEO_FORMAT_RGB,
    GST_VIDEO_FORMAT_YUY2, GST_VIDEO_FORMAT_I420
  };

  gl_allocator = gst_allocator_find (GST_GL_MEMORY_ALLOCATOR);
  fail_if (gl_allocator == NULL);

  /* test allocator creation */
  ASSERT_WARNING (mem = gst_allocator_alloc (gl_allocator, 0, NULL));

  for (i = 0; i < G_N_ELEMENTS (formats); i++) {
    GstVideoInfo v_info;
    gsize width = 320, height = 240;

    gst_video_info_set_format (&v_info, formats[i], width, height);

    for (j = 0; j < GST_VIDEO_INFO_N_PLANES (&v_info); j++) {
      mem = gst_gl_memory_alloc (context, NULL, &v_info, j, NULL);
      fail_if (mem == NULL);
      gl_mem = (GstGLMemory *) mem;

      /* test init params */
      fail_if (gst_video_info_is_equal (&v_info, &gl_mem->info) == FALSE);
      fail_if (gl_mem->context != context);
      fail_if (gl_mem->tex_id == 0);

      /* copy the memory */
      mem2 = gst_memory_copy (mem, 0, -1);
      fail_if (mem2 == NULL);
      gl_mem2 = (GstGLMemory *) mem2;

      /* test params */
      fail_if (gst_video_info_is_equal (&gl_mem2->info,
              &gl_mem->info) == FALSE);
      fail_if (gl_mem->context != gl_mem2->context);

      if (gst_gl_context_get_error ())
        printf ("%s\n", gst_gl_context_get_error ());
      fail_if (gst_gl_context_get_error () != NULL);

      gst_memory_unref (mem);
      gst_memory_unref (mem2);
    }
  }

  gst_object_unref (gl_allocator);
}

GST_END_TEST;

/* one red rgba pixel */
static gchar rgba_pixel[] = {
  0xff, 0x00, 0x00, 0xff,
};

GST_START_TEST (test_transfer)
{
  GstAllocator *gl_allocator;
  GstVideoInfo v_info;
  GstMemory *mem, *mem2, *mem3;
  GstMapInfo map_info;

  gl_allocator = gst_allocator_find (GST_GL_MEMORY_ALLOCATOR);
  fail_if (gl_allocator == NULL);

  gst_video_info_set_format (&v_info, GST_VIDEO_FORMAT_RGBA, 1, 1);

  /* texture creation */
  mem = (GstMemory *) gst_gl_memory_alloc (context, NULL, &v_info, 0, NULL);
  fail_unless (!GST_GL_MEMORY_FLAG_IS_SET (mem,
          GST_GL_MEMORY_FLAG_NEED_UPLOAD));
  fail_unless (!GST_GL_MEMORY_FLAG_IS_SET (mem,
          GST_GL_MEMORY_FLAG_NEED_DOWNLOAD));

  /* test wrapping raw data */
  mem2 =
      (GstMemory *) gst_gl_memory_wrapped (context, &v_info, 0, NULL,
      rgba_pixel, NULL, NULL);
  fail_if (mem == NULL);

  fail_unless (GST_GL_MEMORY_FLAG_IS_SET (mem2,
          GST_GL_MEMORY_FLAG_NEED_UPLOAD));
  fail_unless (!GST_GL_MEMORY_FLAG_IS_SET (mem2,
          GST_GL_MEMORY_FLAG_NEED_DOWNLOAD));

  /* wrapped texture creation */
  mem3 = (GstMemory *) gst_gl_memory_wrapped_texture (context,
      ((GstGLMemory *) mem)->tex_id, GL_TEXTURE_2D, &v_info, 0, NULL, NULL,
      NULL);
  fail_unless (!GST_GL_MEMORY_FLAG_IS_SET (mem3,
          GST_GL_MEMORY_FLAG_NEED_UPLOAD));
  fail_unless (GST_GL_MEMORY_FLAG_IS_SET (mem3,
          GST_GL_MEMORY_FLAG_NEED_DOWNLOAD));

  /* check data/flags are correct */
  fail_unless (gst_memory_map (mem2, &map_info, GST_MAP_READ));

  fail_unless (GST_GL_MEMORY_FLAG_IS_SET (mem2,
          GST_GL_MEMORY_FLAG_NEED_UPLOAD));
  fail_unless (!GST_GL_MEMORY_FLAG_IS_SET (mem2,
          GST_GL_MEMORY_FLAG_NEED_DOWNLOAD));

  fail_unless (((gchar *) map_info.data)[0] == rgba_pixel[0]);
  fail_unless (((gchar *) map_info.data)[1] == rgba_pixel[1]);
  fail_unless (((gchar *) map_info.data)[2] == rgba_pixel[2]);
  fail_unless (((gchar *) map_info.data)[3] == rgba_pixel[3]);

  gst_memory_unmap (mem2, &map_info);

  fail_unless (GST_GL_MEMORY_FLAG_IS_SET (mem2,
          GST_GL_MEMORY_FLAG_NEED_UPLOAD));
  fail_unless (!GST_GL_MEMORY_FLAG_IS_SET (mem2,
          GST_GL_MEMORY_FLAG_NEED_DOWNLOAD));

  fail_unless (gst_memory_map (mem2, &map_info, GST_MAP_READ | GST_MAP_GL));

  fail_unless (!GST_GL_MEMORY_FLAG_IS_SET (mem2,
          GST_GL_MEMORY_FLAG_NEED_UPLOAD));
  fail_unless (!GST_GL_MEMORY_FLAG_IS_SET (mem2,
          GST_GL_MEMORY_FLAG_NEED_DOWNLOAD));

  /* test texture copy */
  fail_unless (gst_gl_memory_copy_into_texture ((GstGLMemory *) mem2,
          ((GstGLMemory *) mem)->tex_id, GST_VIDEO_GL_TEXTURE_TYPE_RGBA, 1, 1,
          4, FALSE));
  GST_GL_MEMORY_FLAG_SET (mem, GST_GL_MEMORY_FLAG_NEED_DOWNLOAD);

  fail_unless (!GST_GL_MEMORY_FLAG_IS_SET (mem2,
          GST_GL_MEMORY_FLAG_NEED_UPLOAD));
  fail_unless (!GST_GL_MEMORY_FLAG_IS_SET (mem2,
          GST_GL_MEMORY_FLAG_NEED_DOWNLOAD));
  fail_unless (!GST_GL_MEMORY_FLAG_IS_SET (mem,
          GST_GL_MEMORY_FLAG_NEED_UPLOAD));
  fail_unless (GST_GL_MEMORY_FLAG_IS_SET (mem,
          GST_GL_MEMORY_FLAG_NEED_DOWNLOAD));

  gst_memory_unmap (mem2, &map_info);

  /* test download of copied texture */
  fail_unless (gst_memory_map (mem, &map_info, GST_MAP_READ));

  fail_unless (((gchar *) map_info.data)[0] == rgba_pixel[0]);
  fail_unless (((gchar *) map_info.data)[1] == rgba_pixel[1]);
  fail_unless (((gchar *) map_info.data)[2] == rgba_pixel[2]);
  fail_unless (((gchar *) map_info.data)[3] == rgba_pixel[3]);

  gst_memory_unmap (mem, &map_info);

  /* test download of wrapped copied texture */
  fail_unless (gst_memory_map (mem3, &map_info, GST_MAP_READ));

  fail_unless (!GST_GL_MEMORY_FLAG_IS_SET (mem,
          GST_GL_MEMORY_FLAG_NEED_UPLOAD));
  fail_unless (!GST_GL_MEMORY_FLAG_IS_SET (mem,
          GST_GL_MEMORY_FLAG_NEED_DOWNLOAD));

  fail_unless (((gchar *) map_info.data)[0] == rgba_pixel[0]);
  fail_unless (((gchar *) map_info.data)[1] == rgba_pixel[1]);
  fail_unless (((gchar *) map_info.data)[2] == rgba_pixel[2]);
  fail_unless (((gchar *) map_info.data)[3] == rgba_pixel[3]);

  gst_memory_unmap (mem3, &map_info);

  /* test upload flag */
  fail_unless (gst_memory_map (mem3, &map_info, GST_MAP_WRITE));
  gst_memory_unmap (mem3, &map_info);

  fail_unless (GST_GL_MEMORY_FLAG_IS_SET (mem3,
          GST_GL_MEMORY_FLAG_NEED_UPLOAD));
  fail_unless (!GST_GL_MEMORY_FLAG_IS_SET (mem3,
          GST_GL_MEMORY_FLAG_NEED_DOWNLOAD));

  /* test download flag */
  fail_unless (gst_memory_map (mem3, &map_info, GST_MAP_WRITE | GST_MAP_GL));
  gst_memory_unmap (mem3, &map_info);

  fail_unless (!GST_GL_MEMORY_FLAG_IS_SET (mem3,
          GST_GL_MEMORY_FLAG_NEED_UPLOAD));
  fail_unless (GST_GL_MEMORY_FLAG_IS_SET (mem3,
          GST_GL_MEMORY_FLAG_NEED_DOWNLOAD));

  if (gst_gl_context_get_error ())
    printf ("%s\n", gst_gl_context_get_error ());
  fail_if (gst_gl_context_get_error () != NULL);

  gst_memory_unref (mem);
  gst_memory_unref (mem2);
  gst_memory_unref (mem3);
  gst_object_unref (gl_allocator);
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
  tcase_add_test (tc_chain, test_transfer);

  return s;
}

GST_CHECK_MAIN (gst_gl_memory);
