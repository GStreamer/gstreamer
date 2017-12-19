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

#include <gst/gl/gl.h>

#include <stdio.h>

static GstGLDisplay *display;
static GstGLContext *context;

static void
setup (void)
{
  display = gst_gl_display_new ();
  context = gst_gl_context_new (display);
  gst_gl_context_create (context, 0, NULL);
  gst_gl_memory_init_once ();
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
  GstGLBaseMemoryAllocator *base_mem_alloc;
  gint i, j;
  static GstVideoFormat formats[] = {
    GST_VIDEO_FORMAT_RGBA, GST_VIDEO_FORMAT_RGB,
    GST_VIDEO_FORMAT_YUY2, GST_VIDEO_FORMAT_I420
  };

  gl_allocator = gst_allocator_find (GST_GL_MEMORY_ALLOCATOR_NAME);
  fail_if (gl_allocator == NULL);
  base_mem_alloc = GST_GL_BASE_MEMORY_ALLOCATOR (gl_allocator);

  /* test allocator creation */
  ASSERT_WARNING (mem = gst_allocator_alloc (gl_allocator, 0, NULL));

  for (i = 0; i < G_N_ELEMENTS (formats); i++) {
    GstVideoInfo v_info;
    gsize width = 320, height = 240;

    gst_video_info_set_format (&v_info, formats[i], width, height);

    for (j = 0; j < GST_VIDEO_INFO_N_PLANES (&v_info); j++) {
      GstGLFormat tex_format = gst_gl_format_from_video_info (context,
          &v_info, j);
      GstGLVideoAllocationParams *params;

      params = gst_gl_video_allocation_params_new (context, NULL, &v_info, j,
          NULL, GST_GL_TEXTURE_TARGET_2D, tex_format);

      mem = (GstMemory *) gst_gl_base_memory_alloc (base_mem_alloc,
          (GstGLAllocationParams *) params);
      fail_if (mem == NULL);
      gl_mem = (GstGLMemory *) mem;

      /* test init params */
      fail_if (gst_video_info_is_equal (&v_info, &gl_mem->info) == FALSE);
      fail_if (gl_mem->mem.context != context);
      fail_if (gl_mem->tex_id == 0);

      /* copy the memory */
      mem2 = gst_memory_copy (mem, 0, -1);
      fail_if (mem2 == NULL);
      gl_mem2 = (GstGLMemory *) mem2;

      /* test params */
      fail_if (gst_video_info_is_equal (&gl_mem2->info,
              &gl_mem->info) == FALSE);
      fail_if (gl_mem->mem.context != gl_mem2->mem.context);

      gst_gl_allocation_params_free ((GstGLAllocationParams *) params);
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

static void
test_transfer_allocator (const gchar * allocator_name)
{
  GstAllocator *gl_allocator;
  GstGLBaseMemoryAllocator *base_mem_alloc;
  GstVideoInfo v_info;
  GstMemory *mem, *mem2, *mem3;
  GstMapInfo map_info;
  GstGLVideoAllocationParams *params;

  gl_allocator = gst_allocator_find (allocator_name);
  fail_if (gl_allocator == NULL);
  base_mem_alloc = GST_GL_BASE_MEMORY_ALLOCATOR (gl_allocator);

  gst_video_info_set_format (&v_info, GST_VIDEO_FORMAT_RGBA, 1, 1);

  params = gst_gl_video_allocation_params_new (context, NULL, &v_info, 0,
      NULL, GST_GL_TEXTURE_TARGET_2D, GST_GL_RGBA);

  /* texture creation */
  mem = (GstMemory *) gst_gl_base_memory_alloc (base_mem_alloc,
      (GstGLAllocationParams *) params);
  gst_gl_allocation_params_free ((GstGLAllocationParams *) params);
  fail_unless (!GST_MEMORY_FLAG_IS_SET (mem,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_UPLOAD));
  fail_unless (!GST_MEMORY_FLAG_IS_SET (mem,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_DOWNLOAD));

  /* test wrapping raw data */
  params = gst_gl_video_allocation_params_new_wrapped_data (context, NULL,
      &v_info, 0, NULL, GST_GL_TEXTURE_TARGET_2D,
      GST_GL_RGBA, rgba_pixel, NULL, NULL);
  mem2 =
      (GstMemory *) gst_gl_base_memory_alloc (base_mem_alloc,
      (GstGLAllocationParams *) params);
  gst_gl_allocation_params_free ((GstGLAllocationParams *) params);
  fail_if (mem == NULL);

  fail_unless (GST_MEMORY_FLAG_IS_SET (mem2,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_UPLOAD));
  fail_unless (!GST_MEMORY_FLAG_IS_SET (mem2,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_DOWNLOAD));

  /* wrapped texture creation */
  params = gst_gl_video_allocation_params_new_wrapped_texture (context, NULL,
      &v_info, 0, NULL, GST_GL_TEXTURE_TARGET_2D,
      GST_GL_RGBA, ((GstGLMemory *) mem)->tex_id, NULL, NULL);
  mem3 =
      (GstMemory *) gst_gl_base_memory_alloc (base_mem_alloc,
      (GstGLAllocationParams *) params);
  gst_gl_allocation_params_free ((GstGLAllocationParams *) params);
  fail_unless (!GST_MEMORY_FLAG_IS_SET (mem3,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_UPLOAD));
  fail_unless (GST_MEMORY_FLAG_IS_SET (mem3,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_DOWNLOAD));

  /* check data/flags are correct */
  fail_unless (gst_memory_map (mem2, &map_info, GST_MAP_READ));

  fail_unless (GST_MEMORY_FLAG_IS_SET (mem2,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_UPLOAD));
  fail_unless (!GST_MEMORY_FLAG_IS_SET (mem2,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_DOWNLOAD));

  fail_unless (memcmp (map_info.data, rgba_pixel,
          G_N_ELEMENTS (rgba_pixel)) == 0,
      "0x%02x%02x%02x%02x != 0x%02x%02x%02x%02x", map_info.data[0],
      map_info.data[1], map_info.data[2], map_info.data[3],
      (guint8) rgba_pixel[0], (guint8) rgba_pixel[1], (guint8) rgba_pixel[2],
      (guint8) rgba_pixel[3]);

  gst_memory_unmap (mem2, &map_info);

  fail_unless (GST_MEMORY_FLAG_IS_SET (mem2,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_UPLOAD));
  fail_unless (!GST_MEMORY_FLAG_IS_SET (mem2,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_DOWNLOAD));

  fail_unless (gst_memory_map (mem2, &map_info, GST_MAP_READ | GST_MAP_GL));

  fail_unless (!GST_MEMORY_FLAG_IS_SET (mem2,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_UPLOAD));
  fail_unless (!GST_MEMORY_FLAG_IS_SET (mem2,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_DOWNLOAD));

  /* test texture copy */
  fail_unless (gst_gl_memory_copy_into ((GstGLMemory *) mem2,
          ((GstGLMemory *) mem)->tex_id, GST_GL_TEXTURE_TARGET_2D,
          GST_GL_RGBA, 1, 1));
  GST_MINI_OBJECT_FLAG_SET (mem, GST_GL_BASE_MEMORY_TRANSFER_NEED_DOWNLOAD);

  fail_unless (!GST_MEMORY_FLAG_IS_SET (mem2,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_UPLOAD));
  fail_unless (!GST_MEMORY_FLAG_IS_SET (mem2,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_DOWNLOAD));
  fail_unless (!GST_MEMORY_FLAG_IS_SET (mem,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_UPLOAD));
  fail_unless (GST_MEMORY_FLAG_IS_SET (mem,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_DOWNLOAD));

  gst_memory_unmap (mem2, &map_info);

  /* test download of copied texture */
  fail_unless (gst_memory_map (mem, &map_info, GST_MAP_READ));

  fail_unless (memcmp (map_info.data, rgba_pixel,
          G_N_ELEMENTS (rgba_pixel)) == 0,
      "0x%02x%02x%02x%02x != 0x%02x%02x%02x%02x", (guint8) map_info.data[0],
      (guint8) map_info.data[1], (guint8) map_info.data[2],
      (guint8) map_info.data[3], (guint8) rgba_pixel[0], (guint8) rgba_pixel[1],
      (guint8) rgba_pixel[2], (guint8) rgba_pixel[3]);

  gst_memory_unmap (mem, &map_info);

  /* test download of wrapped copied texture */
  fail_unless (gst_memory_map (mem3, &map_info, GST_MAP_READ));

  fail_unless (!GST_MEMORY_FLAG_IS_SET (mem,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_UPLOAD));
  fail_unless (!GST_MEMORY_FLAG_IS_SET (mem,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_DOWNLOAD));

  fail_unless (memcmp (map_info.data, rgba_pixel,
          G_N_ELEMENTS (rgba_pixel)) == 0,
      "0x%02x%02x%02x%02x != 0x%02x%02x%02x%02x", (guint8) map_info.data[0],
      (guint8) map_info.data[1], (guint8) map_info.data[2],
      (guint8) map_info.data[3], (guint8) rgba_pixel[0], (guint8) rgba_pixel[1],
      (guint8) rgba_pixel[2], (guint8) rgba_pixel[3]);

  gst_memory_unmap (mem3, &map_info);

  /* test upload flag */
  fail_unless (gst_memory_map (mem3, &map_info, GST_MAP_WRITE));
  gst_memory_unmap (mem3, &map_info);

  fail_unless (GST_MEMORY_FLAG_IS_SET (mem3,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_UPLOAD));
  fail_unless (!GST_MEMORY_FLAG_IS_SET (mem3,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_DOWNLOAD));

  /* test download flag */
  fail_unless (gst_memory_map (mem3, &map_info, GST_MAP_WRITE | GST_MAP_GL));
  gst_memory_unmap (mem3, &map_info);

  fail_unless (!GST_MEMORY_FLAG_IS_SET (mem3,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_UPLOAD));
  fail_unless (GST_MEMORY_FLAG_IS_SET (mem3,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_DOWNLOAD));

  gst_memory_unref (mem);
  gst_memory_unref (mem2);
  gst_memory_unref (mem3);
  gst_object_unref (gl_allocator);
}


GST_START_TEST (test_transfer)
{
  test_transfer_allocator (GST_GL_MEMORY_ALLOCATOR_NAME);
  test_transfer_allocator (GST_GL_MEMORY_PBO_ALLOCATOR_NAME);
}

GST_END_TEST;

GST_START_TEST (test_separate_transfer)
{
  GstGLBaseMemoryAllocator *base_mem_alloc;
  GstGLVideoAllocationParams *params;
  GstAllocator *gl_allocator;
  GstVideoInfo v_info;
  GstMemory *mem;
  GstMapInfo info;

  gl_allocator = gst_allocator_find (GST_GL_MEMORY_PBO_ALLOCATOR_NAME);
  fail_if (gl_allocator == NULL);
  base_mem_alloc = GST_GL_BASE_MEMORY_ALLOCATOR (gl_allocator);

  gst_video_info_set_format (&v_info, GST_VIDEO_FORMAT_RGBA, 1, 1);

  params = gst_gl_video_allocation_params_new_wrapped_data (context, NULL,
      &v_info, 0, NULL, GST_GL_TEXTURE_TARGET_2D,
      GST_GL_RGBA, rgba_pixel, NULL, NULL);
  mem =
      (GstMemory *) gst_gl_base_memory_alloc (base_mem_alloc,
      (GstGLAllocationParams *) params);
  gst_gl_allocation_params_free ((GstGLAllocationParams *) params);
  fail_if (mem == NULL);
  fail_unless (!GST_MEMORY_FLAG_IS_SET (mem,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_DOWNLOAD));

  gst_gl_memory_pbo_upload_transfer ((GstGLMemoryPBO *) mem);

  fail_unless (!GST_MEMORY_FLAG_IS_SET (mem,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_DOWNLOAD));

  fail_unless (gst_memory_map (mem, &info, GST_MAP_READ));

  fail_unless (memcmp (info.data, rgba_pixel, G_N_ELEMENTS (rgba_pixel)) == 0,
      "0x%02x%02x%02x%02x != 0x%02x%02x%02x%02x", (guint8) info.data[0],
      (guint8) info.data[1], (guint8) info.data[2],
      (guint8) info.data[3], (guint8) rgba_pixel[0], (guint8) rgba_pixel[1],
      (guint8) rgba_pixel[2], (guint8) rgba_pixel[3]);

  gst_memory_unmap (mem, &info);

  /* FIXME: add download transfer */

  gst_memory_unref (mem);
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
  tcase_add_test (tc_chain, test_separate_transfer);

  return s;
}

GST_CHECK_MAIN (gst_gl_memory);
