/* GStreamer unit tests for the RTSP support library
 * Copyright (C) 2010 Andy Wingo <wingo@oblong.com>
 * Copyright (C) 2015 Tim-Philipp Müller <tim@centricular.com>
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

#include <glib/gstdio.h>
#include <gst/check/gstcheck.h>

#include <fcntl.h>
#include <gst/allocators/gstahardwarebuffer.h>
#include <gst/allocators/gstdmabuf.h>
#include <gst/allocators/gstfdmemory.h>
#include <gst/allocators/gstshmallocator.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define FILE_SIZE 4096


GST_START_TEST (test_dmabuf)
{
  char tmpfilename[] = "/tmp/dmabuf-test.XXXXXX";
  int fd;
  GstMemory *mem;
  GstAllocator *alloc;
  GstMapInfo info;

  fd = mkstemp (tmpfilename);
  fail_unless (fd > 0);
  fail_unless (g_unlink (tmpfilename) == 0);

  alloc = gst_dmabuf_allocator_new ();

  mem = gst_dmabuf_allocator_alloc (alloc, fd, FILE_SIZE);

  fail_unless (gst_memory_map (mem, &info, GST_MAP_READWRITE));
  fail_unless (info.flags == GST_MAP_READWRITE);
  fail_unless (info.data != NULL);
  fail_unless (info.size == FILE_SIZE);
  fail_unless (info.maxsize == FILE_SIZE);
  gst_memory_unmap (mem, &info);

  gst_memory_unref (mem);
  g_object_unref (alloc);
}

GST_END_TEST;

GST_START_TEST (test_ahardware_buffer_formats)
{
#define AHB_FORMAT(name) { GST_AHARDWARE_BUFFER_FORMAT_ ## name, #name }

  static const struct
  {
    guint32 value;
    const gchar *name;
  } known[] = {
    AHB_FORMAT (R8G8B8A8_UNORM),
    AHB_FORMAT (R8G8B8X8_UNORM),
    AHB_FORMAT (R8G8B8_UNORM),
    AHB_FORMAT (R5G6B5_UNORM),
    AHB_FORMAT (R16G16B16A16_FLOAT),
    AHB_FORMAT (BLOB),
    AHB_FORMAT (Y8Cb8Cr8_420),
    AHB_FORMAT (R10G10B10A2_UNORM),
    AHB_FORMAT (D16_UNORM),
    AHB_FORMAT (D24_UNORM),
    AHB_FORMAT (D24_UNORM_S8_UINT),
    AHB_FORMAT (D32_FLOAT),
    AHB_FORMAT (D32_FLOAT_S8_UINT),
    AHB_FORMAT (S8_UINT),
    AHB_FORMAT (YCbCr_P010),
    AHB_FORMAT (R8_UNORM),
    AHB_FORMAT (R16_UINT),
    AHB_FORMAT (R16G16_UINT),
    AHB_FORMAT (R10G10B10A10_UNORM),
    AHB_FORMAT (YCbCr_P210),
    AHB_FORMAT (R12_UINT),
    AHB_FORMAT (R14_UINT),
    AHB_FORMAT (R12G12_UINT),
    AHB_FORMAT (R14G14_UINT),
    AHB_FORMAT (R12G12B12A12_UINT),
    AHB_FORMAT (R14G14B14A14_UINT),
    AHB_FORMAT (B10G10R10A2_UNORM),
    AHB_FORMAT (B10G10R10X2_UNORM),
  };
  guint32 value;
  gchar *str;

  for (guint i = 0; i < G_N_ELEMENTS (known); i++) {
    str = gst_ahardware_buffer_format_to_caps_string (known[i].value);
    fail_unless_equals_string (str, known[i].name);
    fail_unless (gst_ahardware_buffer_format_from_caps_string (str, &value));
    fail_unless_equals_int (value, known[i].value);
    g_free (str);
  }

  str = gst_ahardware_buffer_format_to_caps_string (0x32315659);
  fail_unless_equals_string (str, "0x32315659");
  fail_unless (gst_ahardware_buffer_format_from_caps_string (str, &value));
  fail_unless_equals_int (value, 0x32315659);
  g_free (str);

  fail_if (gst_ahardware_buffer_format_from_caps_string ("0X32315659", &value));
  fail_if (gst_ahardware_buffer_format_from_caps_string ("0x123", &value));
  fail_if (gst_ahardware_buffer_format_from_caps_string ("0xABCDEF01", &value));
  fail_if (gst_ahardware_buffer_format_from_caps_string ("YUV_VENDOR", &value));

#undef AHB_FORMAT
}

GST_END_TEST;

GST_START_TEST (test_fdmem)
{
  GstAllocator *alloc;
  GstMemory *mem;
  GstMapInfo info;
  GError *error = NULL;
  int fd;
  const char *data = "0123456789";

  fd = g_file_open_tmp (NULL, NULL, &error);
  fail_if (error);
  fail_unless (write (fd, data, 10) == 10);

  alloc = gst_fd_allocator_new ();
  fail_unless (alloc);
  mem = gst_fd_allocator_alloc (alloc, fd, 10, GST_FD_MEMORY_FLAG_KEEP_MAPPED);

  fail_unless (gst_memory_map (mem, &info, GST_MAP_READ));
  fail_unless (info.data[5] == '5');
  gst_memory_unmap (mem, &info);

  fail_unless (gst_memory_map (mem, &info, GST_MAP_WRITE));
  info.data[5] = 'X';
  gst_memory_unmap (mem, &info);

  fail_unless (gst_memory_map (mem, &info, GST_MAP_READ));
  fail_unless (info.data[5] == 'X');
  gst_memory_unmap (mem, &info);

  gst_memory_unref (mem);
  gst_object_unref (alloc);
}

GST_END_TEST;

GST_START_TEST (test_fdmem_dont_close)
{
  GstAllocator *alloc;
  GstMemory *mem;
  GstMapInfo info;
  GError *error = NULL;
  int fd;
  const char *data = "0123456789";

  fd = g_file_open_tmp (NULL, NULL, &error);
  fail_if (error);
  fail_unless (write (fd, data, 10) == 10);

  alloc = gst_fd_allocator_new ();
  fail_unless (alloc);
  mem = gst_fd_allocator_alloc (alloc, fd, 10,
      GST_FD_MEMORY_FLAG_KEEP_MAPPED | GST_FD_MEMORY_FLAG_DONT_CLOSE);

  fail_unless (gst_memory_map (mem, &info, GST_MAP_READ));
  fail_unless (info.data[5] == '5');
  gst_memory_unmap (mem, &info);

  fail_unless (gst_memory_map (mem, &info, GST_MAP_WRITE));
  info.data[5] = 'X';
  gst_memory_unmap (mem, &info);

  fail_unless (gst_memory_map (mem, &info, GST_MAP_READ));
  fail_unless (info.data[5] == 'X');
  gst_memory_unmap (mem, &info);

  gst_memory_unref (mem);
  fail_unless (g_close (fd, NULL));
  gst_object_unref (alloc);
}

GST_END_TEST;

GST_START_TEST (test_shm_alloc_page_aligned)
{
  GstAllocator *alloc;
  GstMemory *mem;
  long page_size = sysconf (_SC_PAGESIZE);

  gst_shm_allocator_init_once ();
  alloc = gst_shm_allocator_get ();
  fail_unless (alloc != NULL);

  /* Small allocation: maxsize should be rounded up to page size */
  mem = gst_allocator_alloc (alloc, 100, NULL);
  fail_unless (mem != NULL);
  fail_unless (mem->maxsize >= (gsize) page_size,
      "maxsize %" G_GSIZE_FORMAT " should be >= page_size %ld",
      mem->maxsize, page_size);
  gst_memory_unref (mem);

  /* Allocation crossing page boundary */
  mem = gst_allocator_alloc (alloc, 5000, NULL);
  fail_unless (mem != NULL);
  fail_unless (mem->maxsize >= 2 * (gsize) page_size,
      "maxsize %" G_GSIZE_FORMAT " should be >= 2 * page_size %ld",
      mem->maxsize, page_size);
  gst_memory_unref (mem);

  /* Exact page size allocation */
  mem = gst_allocator_alloc (alloc, page_size, NULL);
  fail_unless (mem != NULL);
  fail_unless (mem->maxsize >= (gsize) page_size);
  gst_memory_unref (mem);

  gst_object_unref (alloc);
}

GST_END_TEST;

static Suite *
allocators_suite (void)
{
  Suite *s = suite_create ("allocators");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_dmabuf);
  tcase_add_test (tc_chain, test_fdmem);
  tcase_add_test (tc_chain, test_fdmem_dont_close);
  tcase_add_test (tc_chain, test_shm_alloc_page_aligned);
  tcase_add_test (tc_chain, test_ahardware_buffer_formats);

  return s;
}

GST_CHECK_MAIN (allocators);
