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

#include <gst/allocators/gstdmabuf.h>
#include <string.h>

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

static Suite *
allocators_suite (void)
{
  Suite *s = suite_create ("allocators");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_dmabuf);

  return s;
}

GST_CHECK_MAIN (allocators);
