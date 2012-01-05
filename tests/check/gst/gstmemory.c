/* GStreamer
 *
 * unit test for GstMemory
 *
 * Copyright (C) <2012> Wim Taymans <wim.taymans at gmail.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_VALGRIND_H
# include <valgrind/valgrind.h>
#else
# define RUNNING_ON_VALGRIND FALSE
#endif

#include <gst/check/gstcheck.h>

GST_START_TEST (test_submemory)
{
  GstMemory *memory, *sub;
  gsize size, maxsize, ssize;
  guint8 *data, *sdata;

  memory = gst_allocator_alloc (NULL, 4, 0);

  /* check sizes, memory starts out empty */
  data = gst_memory_map (memory, &size, &maxsize, GST_MAP_WRITE);
  fail_unless (size == 4, "memory has wrong size");
  fail_unless (maxsize >= 4, "memory has wrong size");
  memset (data, 0, 4);
  gst_memory_unmap (memory, data, 4);

  data = gst_memory_map (memory, &size, NULL, GST_MAP_READ);

  sub = gst_memory_share (memory, 1, 2);
  fail_if (sub == NULL, "share of memory returned NULL");

  sdata = gst_memory_map (sub, &ssize, NULL, GST_MAP_READ);
  fail_unless (ssize == 2, "submemory has wrong size");
  fail_unless (memcmp (data + 1, sdata, 2) == 0,
      "submemory contains the wrong data");
  ASSERT_MEMORY_REFCOUNT (sub, "submemory", 1);
  gst_memory_unmap (sub, sdata, ssize);
  gst_memory_unref (sub);

  /* create a submemory of size 0 */
  sub = gst_memory_share (memory, 1, 0);
  fail_if (sub == NULL, "share memory returned NULL");
  sdata = gst_memory_map (sub, &ssize, NULL, GST_MAP_READ);
  fail_unless (ssize == 0, "submemory has wrong size");
  fail_unless (memcmp (data + 1, sdata, 0) == 0,
      "submemory contains the wrong data");
  ASSERT_MEMORY_REFCOUNT (sub, "submemory", 1);
  gst_memory_unmap (sub, sdata, ssize);
  gst_memory_unref (sub);

  /* test if metadata is coppied, not a complete memory copy so only the
   * timestamp and offset fields are copied. */
  sub = gst_memory_share (memory, 0, 1);
  fail_if (sub == NULL, "share of memory returned NULL");
  fail_unless (gst_memory_get_sizes (sub, NULL, NULL) == 1,
      "submemory has wrong size");
  gst_memory_unref (sub);

  /* test if metadata is coppied, a complete memory is copied so all the timing
   * fields should be copied. */
  sub = gst_memory_share (memory, 0, 4);
  fail_if (sub == NULL, "share of memory returned NULL");
  fail_unless (gst_memory_get_sizes (sub, NULL, NULL) == 4,
      "submemory has wrong size");

  /* clean up */
  gst_memory_unref (sub);

  gst_memory_unmap (memory, data, size);
  gst_memory_unref (memory);
}

GST_END_TEST;

GST_START_TEST (test_is_span)
{
  GstMemory *memory, *sub1, *sub2;

  memory = gst_allocator_alloc (NULL, 4, 0);

  sub1 = gst_memory_share (memory, 0, 2);
  fail_if (sub1 == NULL, "share of memory returned NULL");

  sub2 = gst_memory_share (memory, 2, 2);
  fail_if (sub2 == NULL, "share of memory returned NULL");

  fail_if (gst_memory_is_span (memory, sub2, NULL) == TRUE,
      "a parent memory can't be span");

  fail_if (gst_memory_is_span (sub1, memory, NULL) == TRUE,
      "a parent memory can't be span");

  fail_if (gst_memory_is_span (sub1, sub2, NULL) == FALSE,
      "two submemorys next to each other should be span");

  /* clean up */
  gst_memory_unref (sub1);
  gst_memory_unref (sub2);
  gst_memory_unref (memory);
}

GST_END_TEST;

static const char ro_memory[] = "abcdefghijklmnopqrstuvwxyz";

static GstMemory *
create_read_only_memory (void)
{
  GstMemory *mem;

  /* assign some read-only data to the new memory */
  mem = gst_memory_new_wrapped (GST_MEMORY_FLAG_READONLY,
      (gpointer) ro_memory, NULL, sizeof (ro_memory), 0, sizeof (ro_memory));
  fail_if (GST_MEMORY_IS_WRITABLE (mem));

  return mem;
}

GST_START_TEST (test_writable)
{
  GstMemory *mem, *mem2;
  guint8 *data;
  gsize size;

  /* create read-only memory and try to write */
  mem = create_read_only_memory ();

  ASSERT_CRITICAL (gst_memory_map (mem, &size, NULL, GST_MAP_WRITE));
  fail_if (GST_MEMORY_IS_WRITABLE (mem));

  mem2 = gst_memory_copy (mem, 0, -1);
  fail_if (GST_MEMORY_IS_WRITABLE (mem));
  fail_unless (GST_MEMORY_IS_WRITABLE (mem2));

  data = gst_memory_map (mem2, &size, NULL, GST_MAP_WRITE);
  data[4] = 'a';
  gst_memory_unmap (mem2, data, size);
  gst_memory_unref (mem2);

  gst_memory_unref (mem);
}

GST_END_TEST;

GST_START_TEST (test_submemory_writable)
{
  GstMemory *mem, *sub_mem;
  gsize size;

  /* create sub-memory of read-only memory and make it writable */
  mem = create_read_only_memory ();

  sub_mem = gst_memory_share (mem, 0, 8);
  fail_if (GST_MEMORY_IS_WRITABLE (sub_mem));

  ASSERT_CRITICAL (gst_memory_map (sub_mem, &size, NULL, GST_MAP_WRITE));

  gst_memory_unref (sub_mem);
  gst_memory_unref (mem);
}

GST_END_TEST;

GST_START_TEST (test_copy)
{
  GstMemory *memory, *copy;
  gsize size, ssize;
  guint8 *data, *sdata;

  memory = gst_allocator_alloc (NULL, 4, 0);
  ASSERT_MEMORY_REFCOUNT (memory, "memory", 1);

  copy = gst_memory_copy (memory, 0, -1);
  ASSERT_MEMORY_REFCOUNT (memory, "memory", 1);
  ASSERT_MEMORY_REFCOUNT (copy, "copy", 1);
  /* memorys are copied and must point to different memory */
  fail_if (memory == copy);

  data = gst_memory_map (memory, &size, NULL, GST_MAP_READ);
  sdata = gst_memory_map (copy, &ssize, NULL, GST_MAP_READ);

  /* NOTE that data is refcounted */
  fail_unless (size == ssize);

  gst_memory_unmap (copy, sdata, ssize);
  gst_memory_unmap (memory, data, size);

  gst_memory_unref (copy);
  gst_memory_unref (memory);

  memory = gst_allocator_alloc (NULL, 0, 0);
  data = gst_memory_map (memory, &size, NULL, GST_MAP_READ);
  fail_unless (size == 0);
  gst_memory_unmap (memory, data, size);

  /* copying a 0-sized memory should not crash */
  copy = gst_memory_copy (memory, 0, -1);
  data = gst_memory_map (copy, &size, NULL, GST_MAP_READ);
  fail_unless (size == 0);
  gst_memory_unmap (copy, data, size);

  gst_memory_unref (copy);
  gst_memory_unref (memory);
}

GST_END_TEST;

GST_START_TEST (test_try_new_and_alloc)
{
  GstMemory *mem;
  gsize size;
  guint8 *data;

  mem = gst_allocator_alloc (NULL, 0, 0);
  fail_unless (mem != NULL);
  data = gst_memory_map (mem, &size, NULL, GST_MAP_READ);
  fail_unless (size == 0);
  gst_memory_unmap (mem, data, size);
  gst_memory_unref (mem);

  /* normal alloc should still work */
  mem = gst_allocator_alloc (NULL, 640 * 480 * 4, 0);
  fail_unless (mem != NULL);
  data = gst_memory_map (mem, &size, NULL, GST_MAP_WRITE);
  fail_unless (data != NULL);
  fail_unless (size == (640 * 480 * 4));
  data[640 * 479 * 4 + 479] = 0xff;
  gst_memory_unmap (mem, data, size);

  gst_memory_unref (mem);

#if 0
  /* Disabled this part of the test, because it happily succeeds on 64-bit
   * machines that have enough memory+swap, because the address space is large
   * enough. There's not really any way to test the failure case except by 
   * allocating chunks of memory until it fails, which would suck. */

  /* now this better fail (don't run in valgrind, it will abort
   * or warn when passing silly arguments to malloc) */
  if (!RUNNING_ON_VALGRIND) {
    mem = gst_allocator_alloc (NULL, (guint) - 1, 0);
    fail_unless (mem == NULL);
  }
#endif
}

GST_END_TEST;

GST_START_TEST (test_resize)
{
  GstMemory *mem;
  gsize maxalloc;
  gsize size, maxsize, offset;

  /* one memory block */
  mem = gst_allocator_alloc (NULL, 100, 0);

  size = gst_memory_get_sizes (mem, &offset, &maxalloc);
  fail_unless (size == 100);
  fail_unless (offset == 0);
  fail_unless (maxalloc >= 100);

  ASSERT_CRITICAL (gst_memory_resize (mem, 200, 50));
  ASSERT_CRITICAL (gst_memory_resize (mem, 0, 150));
  ASSERT_CRITICAL (gst_memory_resize (mem, 1, maxalloc));
  ASSERT_CRITICAL (gst_memory_resize (mem, maxalloc, 1));

  /* this does nothing */
  gst_memory_resize (mem, 0, 100);

  /* nothing should have changed */
  size = gst_memory_get_sizes (mem, &offset, &maxsize);
  fail_unless (size == 100);
  fail_unless (offset == 0);
  fail_unless (maxsize == maxalloc);

  gst_memory_resize (mem, 0, 50);
  size = gst_memory_get_sizes (mem, &offset, &maxsize);
  fail_unless (size == 50);
  fail_unless (offset == 0);
  fail_unless (maxsize == maxalloc);

  gst_memory_resize (mem, 0, 100);
  size = gst_memory_get_sizes (mem, &offset, &maxsize);
  fail_unless (size == 100);
  fail_unless (offset == 0);
  fail_unless (maxsize == maxalloc);

  gst_memory_resize (mem, 1, 99);
  size = gst_memory_get_sizes (mem, &offset, &maxsize);
  fail_unless (size == 99);
  fail_unless (offset == 1);
  fail_unless (maxsize == maxalloc);

  ASSERT_CRITICAL (gst_memory_resize (mem, 1, maxalloc - 1));

  gst_memory_resize (mem, 0, 99);
  size = gst_memory_get_sizes (mem, &offset, &maxsize);
  fail_unless (size == 99);
  fail_unless (offset == 1);
  fail_unless (maxsize == maxalloc);

  gst_memory_resize (mem, -1, 100);
  size = gst_memory_get_sizes (mem, &offset, &maxsize);
  fail_unless (size == 100);
  fail_unless (offset == 0);
  fail_unless (maxsize == maxalloc);

  /* can't set offset below 0 */
  ASSERT_CRITICAL (gst_memory_resize (mem, -1, 100));

  gst_memory_resize (mem, 50, 40);
  size = gst_memory_get_sizes (mem, &offset, &maxsize);
  fail_unless (size == 40);
  fail_unless (offset == 50);
  fail_unless (maxsize == maxalloc);

  gst_memory_resize (mem, -50, 100);
  size = gst_memory_get_sizes (mem, &offset, &maxsize);
  fail_unless (size == 100);
  fail_unless (offset == 0);
  fail_unless (maxsize == maxalloc);

  gst_memory_resize (mem, 0, 0);
  size = gst_memory_get_sizes (mem, &offset, &maxsize);
  fail_unless (size == 0);
  fail_unless (offset == 0);
  fail_unless (maxsize == maxalloc);

  gst_memory_resize (mem, 0, 100);
  size = gst_memory_get_sizes (mem, &offset, &maxsize);
  fail_unless (size == 100);
  fail_unless (offset == 0);
  fail_unless (maxsize == maxalloc);

  gst_memory_resize (mem, 0, 100);
  size = gst_memory_get_sizes (mem, &offset, &maxsize);
  fail_unless (size == 100);
  fail_unless (offset == 0);
  fail_unless (maxsize == maxalloc);

  gst_memory_unref (mem);
}

GST_END_TEST;

GST_START_TEST (test_map)
{
  GstMemory *mem;
  gsize maxalloc;
  gsize size, maxsize, offset;
  gpointer data;

  /* one memory block */
  mem = gst_allocator_alloc (NULL, 100, 0);

  size = gst_memory_get_sizes (mem, &offset, &maxalloc);
  fail_unless (size == 100);
  fail_unless (offset == 0);
  fail_unless (maxalloc >= 100);

  /* see if simply mapping works */
  data = gst_memory_map (mem, &size, &maxsize, GST_MAP_READ);
  fail_unless (data != NULL);
  fail_unless (size == 100);
  fail_unless (maxsize == maxalloc);
  gst_memory_unmap (mem, data, size);

  /* make smaller by unmapping less */
  data = gst_memory_map (mem, &size, &maxsize, GST_MAP_READ);
  fail_unless (data != NULL);
  fail_unless (size == 100);
  fail_unless (maxsize == maxalloc);
  gst_memory_unmap (mem, data, size - 1);

  size = gst_memory_get_sizes (mem, &offset, &maxalloc);
  fail_unless (size == 99);
  fail_unless (offset == 0);
  fail_unless (maxalloc >= 100);

  /* make bigger by unmapping more */
  data = gst_memory_map (mem, &size, &maxsize, GST_MAP_READ);
  fail_unless (data != NULL);
  fail_unless (size == 99);
  fail_unless (maxsize == maxalloc);
  gst_memory_unmap (mem, data, size + 1);

  size = gst_memory_get_sizes (mem, &offset, &maxalloc);
  fail_unless (size == 100);
  fail_unless (offset == 0);
  fail_unless (maxalloc >= 100);

  /* resize beyond the maxsize */
  data = gst_memory_map (mem, &size, &maxsize, GST_MAP_READ);
  fail_unless (data != NULL);
  fail_unless (size == 100);
  fail_unless (maxsize == maxalloc);
  ASSERT_CRITICAL (gst_memory_unmap (mem, data, maxsize + 1));
  gst_memory_unmap (mem, data, maxsize);

  gst_memory_unref (mem);
}

GST_END_TEST;

static Suite *
gst_memory_suite (void)
{
  Suite *s = suite_create ("GstMemory");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_submemory);
  tcase_add_test (tc_chain, test_submemory_writable);
  tcase_add_test (tc_chain, test_writable);
  tcase_add_test (tc_chain, test_is_span);
  tcase_add_test (tc_chain, test_copy);
  tcase_add_test (tc_chain, test_try_new_and_alloc);
  tcase_add_test (tc_chain, test_resize);
  tcase_add_test (tc_chain, test_map);

  return s;
}

GST_CHECK_MAIN (gst_memory);
