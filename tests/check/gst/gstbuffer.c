/* GStreamer
 *
 * unit test for GstBuffer
 *
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
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
# include "config.h"
#endif

#include <gst/check/gstcheck.h>

GST_START_TEST (test_subbuffer)
{
  GstBuffer *buffer, *sub;
  GstMapInfo info, sinfo;

  buffer = gst_buffer_new_and_alloc (4);

  /* check sizes, buffer starts out empty */
  fail_unless (gst_buffer_map (buffer, &info, GST_MAP_WRITE));
  fail_unless (info.size == 4, "buffer has wrong size");
  fail_unless (info.maxsize >= 4, "buffer has wrong size");
  memset (info.data, 0, 4);
  gst_buffer_unmap (buffer, &info);

  fail_unless (gst_buffer_map (buffer, &info, GST_MAP_READ));
  /* set some metadata */
  GST_BUFFER_TIMESTAMP (buffer) = 1;
  GST_BUFFER_DURATION (buffer) = 2;
  GST_BUFFER_OFFSET (buffer) = 3;
  GST_BUFFER_OFFSET_END (buffer) = 4;

  sub = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL, 1, 2);
  fail_if (sub == NULL, "copy region of buffer returned NULL");

  fail_unless (gst_buffer_map (sub, &sinfo, GST_MAP_READ));
  fail_unless (sinfo.size == 2, "subbuffer has wrong size");
  fail_unless (memcmp (info.data + 1, sinfo.data, 2) == 0,
      "subbuffer contains the wrong data");
  ASSERT_BUFFER_REFCOUNT (sub, "subbuffer", 1);
  fail_unless (GST_BUFFER_TIMESTAMP (sub) == -1,
      "subbuffer has wrong timestamp");
  fail_unless (GST_BUFFER_DURATION (sub) == -1, "subbuffer has wrong duration");
  fail_unless (GST_BUFFER_OFFSET (sub) == -1, "subbuffer has wrong offset");
  fail_unless (GST_BUFFER_OFFSET_END (sub) == -1,
      "subbuffer has wrong offset end");
  gst_buffer_unmap (sub, &sinfo);
  gst_buffer_unref (sub);

  /* create a subbuffer of size 0 */
  sub = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL, 1, 0);
  fail_if (sub == NULL, "copy_region of buffer returned NULL");
  fail_unless (gst_buffer_map (sub, &sinfo, GST_MAP_READ));
  fail_unless (sinfo.size == 0, "subbuffer has wrong size");
  fail_unless (memcmp (info.data + 1, sinfo.data, 0) == 0,
      "subbuffer contains the wrong data");
  ASSERT_BUFFER_REFCOUNT (sub, "subbuffer", 1);
  gst_buffer_unmap (sub, &sinfo);
  gst_buffer_unref (sub);

  /* test if metadata is coppied, not a complete buffer copy so only the
   * timestamp and offset fields are copied. */
  sub = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL, 0, 1);
  fail_if (sub == NULL, "copy_region of buffer returned NULL");
  fail_unless (gst_buffer_get_size (sub) == 1, "subbuffer has wrong size");
  fail_unless (GST_BUFFER_TIMESTAMP (sub) == 1,
      "subbuffer has wrong timestamp");
  fail_unless (GST_BUFFER_OFFSET (sub) == 3, "subbuffer has wrong offset");
  fail_unless (GST_BUFFER_DURATION (sub) == -1, "subbuffer has wrong duration");
  fail_unless (GST_BUFFER_OFFSET_END (sub) == -1,
      "subbuffer has wrong offset end");
  gst_buffer_unref (sub);

  /* test if metadata is coppied, a complete buffer is copied so all the timing
   * fields should be copied. */
  sub = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL, 0, 4);
  fail_if (sub == NULL, "copy_region of buffer returned NULL");
  fail_unless (gst_buffer_get_size (sub) == 4, "subbuffer has wrong size");
  fail_unless (GST_BUFFER_TIMESTAMP (sub) == 1,
      "subbuffer has wrong timestamp");
  fail_unless (GST_BUFFER_DURATION (sub) == 2, "subbuffer has wrong duration");
  fail_unless (GST_BUFFER_OFFSET (sub) == 3, "subbuffer has wrong offset");
  fail_unless (GST_BUFFER_OFFSET_END (sub) == 4,
      "subbuffer has wrong offset end");

  /* clean up */
  gst_buffer_unref (sub);

  gst_buffer_unmap (buffer, &info);
  gst_buffer_unref (buffer);
}

GST_END_TEST;

GST_START_TEST (test_span)
{
  GstBuffer *buffer, *sub1, *sub2, *span;
  GstMapInfo info;

  buffer = gst_buffer_new_and_alloc (4);

  fail_unless (gst_buffer_map (buffer, &info, GST_MAP_WRITE));
  memcpy (info.data, "data", 4);
  gst_buffer_unmap (buffer, &info);

  ASSERT_CRITICAL (gst_buffer_append (NULL, NULL));
  ASSERT_CRITICAL (gst_buffer_append (buffer, NULL));
  ASSERT_CRITICAL (gst_buffer_append (NULL, buffer));

  sub1 = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL, 0, 2);
  fail_if (sub1 == NULL, "copy_region of buffer returned NULL");

  sub2 = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL, 2, 2);
  fail_if (sub2 == NULL, "copy_region of buffer returned NULL");

  ASSERT_BUFFER_REFCOUNT (buffer, "parent", 1);
  ASSERT_BUFFER_REFCOUNT (sub1, "sub1", 1);
  ASSERT_BUFFER_REFCOUNT (sub2, "sub2", 1);

  /* span will create a new subbuffer from the parent */
  gst_buffer_ref (sub1);
  gst_buffer_ref (sub2);
  span = gst_buffer_append (sub1, sub2);
  fail_unless (gst_buffer_map (span, &info, GST_MAP_READ));
  fail_unless (info.size == 4, "spanned buffer is wrong size");
  ASSERT_BUFFER_REFCOUNT (buffer, "parent", 1);
  ASSERT_BUFFER_REFCOUNT (sub1, "sub1", 1);
  ASSERT_BUFFER_REFCOUNT (sub2, "sub2", 1);
  ASSERT_BUFFER_REFCOUNT (span, "span", 1);
  fail_unless (memcmp (info.data, "data", 4) == 0,
      "spanned buffer contains the wrong data");
  gst_buffer_unmap (span, &info);
  gst_buffer_unref (span);
  ASSERT_BUFFER_REFCOUNT (buffer, "parent", 1);

  /* span from non-contiguous buffers will create new buffers */
  gst_buffer_ref (sub1);
  gst_buffer_ref (sub2);
  span = gst_buffer_append (sub2, sub1);
  fail_unless (gst_buffer_map (span, &info, GST_MAP_READ));
  fail_unless (info.size == 4, "spanned buffer is wrong size");
  ASSERT_BUFFER_REFCOUNT (buffer, "parent", 1);
  ASSERT_BUFFER_REFCOUNT (sub1, "sub1", 1);
  ASSERT_BUFFER_REFCOUNT (sub2, "sub2", 1);
  ASSERT_BUFFER_REFCOUNT (span, "span", 1);
  fail_unless (memcmp (info.data, "tada", 4) == 0,
      "spanned buffer contains the wrong data");
  gst_buffer_unmap (span, &info);
  gst_buffer_unref (span);
  ASSERT_BUFFER_REFCOUNT (buffer, "parent", 1);

  /* clean up */
  gst_buffer_unref (sub1);
  gst_buffer_unref (sub2);
  gst_buffer_unref (buffer);
}

GST_END_TEST;


static const char ro_memory[] = "abcdefghijklmnopqrstuvwxyz";

static GstBuffer *
create_read_only_buffer (void)
{
  GstBuffer *buf;

  buf = gst_buffer_new ();

  /* assign some read-only data to the new buffer */
  gst_buffer_insert_memory (buf, -1,
      gst_memory_new_wrapped (GST_MEMORY_FLAG_READONLY,
          (gpointer) ro_memory, sizeof (ro_memory),
          0, sizeof (ro_memory), NULL, NULL));

  return buf;
}

GST_START_TEST (test_make_writable)
{
  GstBuffer *buf, *buf2;
  GstMapInfo info;

  /* create read-only buffer and make it writable */
  buf = create_read_only_buffer ();

  fail_unless (gst_buffer_map (buf, &info, GST_MAP_WRITE));
  info.data[4] = 'a';
  gst_buffer_unmap (buf, &info);
  gst_buffer_unref (buf);

  /* alloc'ed buffer with refcount 1 should be writable */
  buf = gst_buffer_new_and_alloc (32);
  buf2 = gst_buffer_make_writable (buf);
  fail_unless (buf == buf2,
      "_make_writable() should have returned same buffer");
  gst_buffer_unref (buf2);

  /* alloc'ed buffer with refcount >1 should be copied */
  buf = gst_buffer_new_and_alloc (32);
  gst_buffer_ref (buf);
  buf2 = gst_buffer_make_writable (buf);
  fail_unless (buf != buf2, "_make_writable() should have returned a copy!");
  gst_buffer_unref (buf2);
  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_subbuffer_make_writable)
{
  GstBuffer *buf, *sub_buf;
  GstMapInfo info;

  /* create sub-buffer of read-only buffer and make it writable */
  buf = create_read_only_buffer ();

  sub_buf = gst_buffer_copy_region (buf, GST_BUFFER_COPY_ALL, 0, 8);

  fail_unless (gst_buffer_map (sub_buf, &info, GST_MAP_WRITE));
  fail_if (info.data == NULL);
  info.data[4] = 'a';
  gst_buffer_unmap (sub_buf, &info);
  gst_buffer_unref (sub_buf);
  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_metadata_writable)
{
  GstBuffer *buffer, *sub1;

  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
  GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);

  /* Buffer with refcount 1 should have writable metadata */
  fail_unless (gst_buffer_is_writable (buffer) == TRUE);

  /* Check that a buffer with refcount 2 does not have writable metadata */
  gst_buffer_ref (buffer);
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 2);
  fail_unless (gst_buffer_is_writable (buffer) == FALSE);

  /* Check that make_metadata_writable produces a new sub-buffer with 
   * writable metadata. */
  sub1 = gst_buffer_make_writable (buffer);
  fail_if (sub1 == buffer);
  fail_unless (gst_buffer_is_writable (sub1) == TRUE);

  /* Check that make_metadata_writable() maintains the buffer flags */
  fail_unless (GST_BUFFER_FLAG_IS_SET (sub1, GST_BUFFER_FLAG_DISCONT));
  fail_unless (GST_BUFFER_FLAG_IS_SET (sub1, GST_BUFFER_FLAG_DELTA_UNIT));

  /* Unset flags on writable buffer, then make sure they're still
   * set on the original buffer */
  GST_BUFFER_FLAG_UNSET (sub1, GST_BUFFER_FLAG_DISCONT);
  GST_BUFFER_FLAG_UNSET (sub1, GST_BUFFER_FLAG_DELTA_UNIT);
  fail_unless (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT));
  fail_unless (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT));

  /* Drop the subbuffer and check that the metadata is now writable again */
  ASSERT_BUFFER_REFCOUNT (sub1, "sub1", 1);
  gst_buffer_unref (sub1);
  fail_unless (gst_buffer_is_writable (buffer) == TRUE);

  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);
  gst_buffer_unref (buffer);
}

GST_END_TEST;

GST_START_TEST (test_memcmp)
{
  GstBuffer *buffer;
  char buf[3] = { 0, 0, 0 };

  buffer = gst_buffer_new_and_alloc (2);
  gst_buffer_memset (buffer, 0, 0, 2);

  fail_unless (gst_buffer_memcmp (buffer, 0, buf, 2) == 0);
  fail_unless (gst_buffer_memcmp (buffer, 0, buf, 1) == 0);
  fail_unless (gst_buffer_memcmp (buffer, 1, buf, 1) == 0);
  fail_unless (gst_buffer_memcmp (buffer, 0, buf, 3) != 0);
  fail_unless (gst_buffer_memcmp (buffer, 2, buf, 1) != 0);
  fail_unless (gst_buffer_memcmp (buffer, 4, buf, 1) != 0);

  gst_buffer_memset (buffer, 0, 0x20, 2);
  fail_unless (gst_buffer_memcmp (buffer, 0, buf, 2) != 0);
  fail_unless (gst_buffer_memcmp (buffer, 0, buf, 1) != 0);
  fail_unless (gst_buffer_memcmp (buffer, 1, buf, 1) != 0);
  fail_unless (gst_buffer_memcmp (buffer, 0, buf, 3) != 0);
  fail_unless (gst_buffer_memcmp (buffer, 2, buf, 1) != 0);

  gst_buffer_unref (buffer);
}

GST_END_TEST;

GST_START_TEST (test_copy)
{
  GstBuffer *buffer, *copy;
  GstMapInfo info, sinfo;

  buffer = gst_buffer_new_and_alloc (4);
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);

  copy = gst_buffer_copy (buffer);
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);
  ASSERT_BUFFER_REFCOUNT (copy, "copy", 1);
  /* buffers are copied and must point to different memory */
  fail_if (buffer == copy);

  fail_unless (gst_buffer_map (buffer, &info, GST_MAP_READ));
  fail_unless (gst_buffer_map (copy, &sinfo, GST_MAP_READ));

  /* NOTE that data is refcounted */
  fail_unless (info.size == sinfo.size);
  /* GstBuffer was copied but the underlying GstMemory should be the same */
  fail_unless (info.data == sinfo.data);

  gst_buffer_unmap (copy, &sinfo);
  gst_buffer_unmap (buffer, &info);

  gst_buffer_unref (copy);
  gst_buffer_unref (buffer);

  /* a 0-sized buffer has NULL data as per docs */
  buffer = gst_buffer_new_and_alloc (0);
  fail_unless (gst_buffer_map (buffer, &info, GST_MAP_READ));
  fail_unless (info.data == NULL);
  gst_buffer_unmap (buffer, &info);

  /* copying a 0-sized buffer should not crash and also set
   * the data member NULL. */
  copy = gst_buffer_copy (buffer);
  fail_unless (gst_buffer_map (copy, &info, GST_MAP_READ));
  fail_unless (info.data == NULL);
  gst_buffer_unmap (copy, &info);

  gst_buffer_unref (copy);
  gst_buffer_unref (buffer);

  /* check if copy is an independent copy when written to */
  buffer = gst_buffer_new_and_alloc (4);
  gst_buffer_memset (buffer, 0, 0, 4);
  copy = gst_buffer_copy (buffer);
  fail_unless (gst_buffer_is_writable (copy));
  gst_buffer_memset (copy, 0, 0x80, 4);
  gst_buffer_map (buffer, &info, GST_MAP_READ);
  fail_if (gst_buffer_memcmp (copy, 0, info.data, info.size) == 0);
  gst_buffer_unmap (buffer, &info);

  gst_buffer_unref (copy);
  gst_buffer_unref (buffer);

  /* copy should still be independent if copied when mapped */
  buffer = gst_buffer_new_and_alloc (4);
  gst_buffer_memset (buffer, 0, 0, 4);
  fail_unless (gst_buffer_map (buffer, &info, GST_MAP_WRITE));
  copy = gst_buffer_copy (buffer);
  fail_unless (gst_buffer_is_writable (copy));
  gst_buffer_memset (copy, 0, 0x80, 4);
  gst_buffer_unmap (buffer, &info);
  fail_unless (gst_buffer_map (buffer, &info, GST_MAP_READ));
  fail_if (gst_buffer_memcmp (copy, 0, info.data, info.size) == 0);
  gst_buffer_unmap (buffer, &info);

  gst_buffer_unref (copy);
  gst_buffer_unref (buffer);

  /* check if a writable clone of a buffer is independent when written to */
  buffer = gst_buffer_new_and_alloc (4);
  gst_buffer_memset (buffer, 0, 0, 4);
  copy = gst_buffer_ref (buffer);
  copy = gst_buffer_make_writable (copy);
  fail_unless (gst_buffer_is_writable (copy));
  gst_buffer_memset (copy, 0, 0x80, 4);
  gst_buffer_map (buffer, &info, GST_MAP_READ);
  fail_if (gst_buffer_memcmp (copy, 0, info.data, info.size) == 0);
  gst_buffer_unmap (buffer, &info);

  gst_buffer_unref (copy);
  gst_buffer_unref (buffer);
}

GST_END_TEST;

GST_START_TEST (test_copy_deep)
{
  GstBuffer *buffer, *copy;
  GstMapInfo info, sinfo;

  buffer = gst_buffer_new_and_alloc (4);
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);

  copy = gst_buffer_copy_deep (buffer);
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);
  ASSERT_BUFFER_REFCOUNT (copy, "copy", 1);
  /* buffers are copied and must point to different memory */
  fail_if (buffer == copy);

  fail_unless (gst_buffer_map (buffer, &info, GST_MAP_READ));
  fail_unless (gst_buffer_map (copy, &sinfo, GST_MAP_READ));

  /* NOTE that data is refcounted */
  fail_unless (info.size == sinfo.size);
  /* copy_deep() forces new GstMemory to be used */
  fail_unless (info.data != sinfo.data);

  gst_buffer_unmap (copy, &sinfo);
  gst_buffer_unmap (buffer, &info);

  gst_buffer_unref (copy);
  gst_buffer_unref (buffer);
}

GST_END_TEST;

GST_START_TEST (test_try_new_and_alloc)
{
  GstBuffer *buf;
  GstMapInfo info;

  /* special case: alloc of 0 bytes results in new buffer with NULL data */
  buf = gst_buffer_new_and_alloc (0);
  fail_unless (buf != NULL);
  fail_unless (GST_IS_BUFFER (buf));
  fail_unless (gst_buffer_map (buf, &info, GST_MAP_READ));
  fail_unless (info.data == NULL);
  gst_buffer_unmap (buf, &info);
  gst_buffer_unref (buf);

  /* normal alloc should still work */
  buf = gst_buffer_new_and_alloc (640 * 480 * 4);
  fail_unless (buf != NULL);
  fail_unless (GST_IS_BUFFER (buf));
  fail_unless (gst_buffer_map (buf, &info, GST_MAP_WRITE));
  fail_unless (info.data != NULL);
  fail_unless (info.size == (640 * 480 * 4));
  info.data[640 * 479 * 4 + 479] = 0xff;
  gst_buffer_unmap (buf, &info);

  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_size)
{
  GstBuffer *buf;
  GstMemory *mem;
  gsize maxalloc, maxalloc2, maxalloc3, maxalloc4;
  gsize size, maxsize, offset;

  /* one memory block */
  buf = gst_buffer_new_allocate (NULL, 100, NULL);

  size = gst_buffer_get_sizes (buf, &offset, &maxalloc);
  fail_unless (size == 100);
  fail_unless (offset == 0);
  fail_unless (maxalloc >= 100);

  mem = gst_buffer_get_memory (buf, 0);
  gst_memory_resize (mem, 10, 70);
  gst_memory_unref (mem);

  size = gst_buffer_get_sizes (buf, &offset, &maxsize);
  fail_unless (size == 70);
  fail_unless (offset == 10);
  fail_unless (maxsize == maxalloc);

  /* new memory */
  mem = gst_allocator_alloc (NULL, 100, NULL);
  size = gst_memory_get_sizes (mem, &offset, &maxalloc2);
  fail_unless (size == 100);
  fail_unless (offset == 0);
  fail_unless (maxalloc2 >= 100);

  gst_memory_resize (mem, 20, 60);
  size = gst_memory_get_sizes (mem, &offset, &maxsize);
  fail_unless (size == 60);
  fail_unless (offset == 20);
  fail_unless (maxsize == maxalloc2);

  /* append */
  gst_buffer_insert_memory (buf, -1, mem);

  size = gst_buffer_get_sizes (buf, &offset, &maxalloc);
  fail_unless (size == 130);
  fail_unless (offset == 10);
  /* the maxsize is the size of the first buffer plus the remaining size in the
   * second buffer */
  fail_unless (maxalloc == 80 + (maxalloc2 - 20));

  /* appending an empty block */
  mem = gst_allocator_alloc (NULL, 100, NULL);
  size = gst_memory_get_sizes (mem, &offset, &maxalloc3);
  gst_memory_resize (mem, 0, 0);
  gst_buffer_insert_memory (buf, -1, mem);

  size = gst_buffer_get_sizes (buf, &offset, &maxalloc);
  fail_unless (size == 130);
  fail_unless (offset == 10);
  /* the maxsize is the size of the first buffer plus the remaining size in the
   * second buffer and the last empty buffer*/
  fail_unless (maxalloc == 80 + (maxalloc2 - 20) + maxalloc3);

  /* prepending an empty block */
  mem = gst_allocator_alloc (NULL, 100, NULL);
  size = gst_memory_get_sizes (mem, &offset, &maxalloc4);
  gst_memory_resize (mem, 0, 0);
  gst_buffer_insert_memory (buf, 0, mem);

  size = gst_buffer_get_sizes (buf, &offset, &maxalloc);
  fail_unless (size == 130);
  /* empty buffer maxsize can be used as offset */
  fail_unless (offset == 10 + maxalloc4);
  /* the maxsize is the size of the first buffer plus the remaining size in the
   * second buffer and the last empty buffer*/
  fail_unless (maxalloc == 80 + (maxalloc2 - 20) + maxalloc3 + maxalloc4);

  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_resize)
{
  GstBuffer *buf;
  gsize maxalloc;
  gsize size, maxsize, offset;

  /* one memory block */
  buf = gst_buffer_new_allocate (NULL, 100, NULL);

  size = gst_buffer_get_sizes (buf, &offset, &maxalloc);
  fail_unless (size == 100);
  fail_unless (offset == 0);
  fail_unless (maxalloc >= 100);

  ASSERT_CRITICAL (gst_buffer_resize (buf, 200, 50));
  ASSERT_CRITICAL (gst_buffer_resize (buf, 0, 150));
  ASSERT_CRITICAL (gst_buffer_resize (buf, 1, maxalloc));
  ASSERT_CRITICAL (gst_buffer_resize (buf, maxalloc, 1));

  /* this does nothing */
  gst_buffer_resize (buf, 0, 100);

  /* nothing should have changed */
  size = gst_buffer_get_sizes (buf, &offset, &maxsize);
  fail_unless (size == 100);
  fail_unless (offset == 0);
  fail_unless (maxsize == maxalloc);

  gst_buffer_resize (buf, 0, 50);
  size = gst_buffer_get_sizes (buf, &offset, &maxsize);
  fail_unless (size == 50);
  fail_unless (offset == 0);
  fail_unless (maxsize == maxalloc);

  gst_buffer_resize (buf, 0, 100);
  size = gst_buffer_get_sizes (buf, &offset, &maxsize);
  fail_unless (size == 100);
  fail_unless (offset == 0);
  fail_unless (maxsize == maxalloc);

  gst_buffer_resize (buf, 1, 99);
  size = gst_buffer_get_sizes (buf, &offset, &maxsize);
  fail_unless (size == 99);
  fail_unless (offset == 1);
  fail_unless (maxsize == maxalloc);

  ASSERT_CRITICAL (gst_buffer_resize (buf, 1, maxalloc - 1));

  gst_buffer_resize (buf, 0, 99);
  size = gst_buffer_get_sizes (buf, &offset, &maxsize);
  fail_unless (size == 99);
  fail_unless (offset == 1);
  fail_unless (maxsize == maxalloc);

  gst_buffer_resize (buf, -1, 100);
  size = gst_buffer_get_sizes (buf, &offset, &maxsize);
  fail_unless (size == 100);
  fail_unless (offset == 0);
  fail_unless (maxsize == maxalloc);

  ASSERT_CRITICAL (gst_buffer_resize (buf, -1, 100));

  gst_buffer_resize (buf, 50, 40);
  size = gst_buffer_get_sizes (buf, &offset, &maxsize);
  fail_unless (size == 40);
  fail_unless (offset == 50);
  fail_unless (maxsize == maxalloc);

  gst_buffer_resize (buf, -50, 100);
  size = gst_buffer_get_sizes (buf, &offset, &maxsize);
  fail_unless (size == 100);
  fail_unless (offset == 0);
  fail_unless (maxsize == maxalloc);

  gst_buffer_resize (buf, 0, 0);
  size = gst_buffer_get_sizes (buf, &offset, &maxsize);
  fail_unless (size == 0);
  fail_unless (offset == 0);
  fail_unless (maxsize == maxalloc);

  gst_buffer_resize (buf, 0, 100);
  size = gst_buffer_get_sizes (buf, &offset, &maxsize);
  fail_unless (size == 100);
  fail_unless (offset == 0);
  fail_unless (maxsize == maxalloc);

  gst_buffer_resize (buf, 0, 100);
  size = gst_buffer_get_sizes (buf, &offset, &maxsize);
  fail_unless (size == 100);
  fail_unless (offset == 0);
  fail_unless (maxsize == maxalloc);

  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_map)
{
  GstBuffer *buf;
  GstMapInfo map, map2;
  gsize maxalloc;
  gsize size, offset;

  buf = gst_buffer_new ();
  gst_buffer_insert_memory (buf, -1, gst_allocator_alloc (NULL, 50, NULL));
  gst_buffer_insert_memory (buf, -1, gst_allocator_alloc (NULL, 50, NULL));

  size = gst_buffer_get_sizes (buf, &offset, &maxalloc);
  fail_unless (size == 100);
  fail_unless (offset == 0);
  fail_unless (maxalloc >= 100);
  fail_unless (gst_buffer_n_memory (buf) == 2);

  /* make readonly */
  gst_buffer_ref (buf);
  /* map should merge */
  gst_buffer_map (buf, &map, GST_MAP_READ);
  /* merged memory is not stored */
  fail_unless (gst_buffer_n_memory (buf) == 2);
  gst_buffer_unmap (buf, &map);

  fail_unless (gst_buffer_n_memory (buf) == 2);

  /* can't map write on readonly buffer */
  ASSERT_CRITICAL (gst_buffer_map (buf, &map, GST_MAP_WRITE));
  /* make writable again */
  gst_buffer_unref (buf);

  /* should merge and store */
  gst_buffer_map (buf, &map, GST_MAP_READ);
  fail_unless (gst_buffer_n_memory (buf) == 1);
  gst_buffer_unmap (buf, &map);

  gst_buffer_map (buf, &map, GST_MAP_WRITE);
  gst_buffer_unmap (buf, &map);

  /* mapping same kind should be ok using same memory */
  gst_buffer_map (buf, &map, GST_MAP_WRITE);
  fail_unless (gst_buffer_map (buf, &map2, GST_MAP_WRITE));
  fail_unless (map.memory == map2.memory);
  gst_buffer_unmap (buf, &map2);
  gst_buffer_unmap (buf, &map);

  /* ... but different kind should give temporary memory */
  gst_buffer_map (buf, &map, GST_MAP_WRITE);
  fail_unless (gst_buffer_map (buf, &map2, GST_MAP_READ));
  fail_if (map.memory == map2.memory);
  gst_buffer_unmap (buf, &map2);
  gst_buffer_unmap (buf, &map);

  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_map_range)
{
  GstBuffer *buf;
  GstMapInfo map;
  gsize maxalloc;
  gsize size, offset;

  buf = gst_buffer_new ();
  gst_buffer_insert_memory (buf, -1, gst_allocator_alloc (NULL, 50, NULL));
  gst_buffer_insert_memory (buf, -1, gst_allocator_alloc (NULL, 50, NULL));
  gst_buffer_insert_memory (buf, -1, gst_allocator_alloc (NULL, 50, NULL));

  size = gst_buffer_get_sizes (buf, &offset, &maxalloc);
  fail_unless (size == 150);
  fail_unless (offset == 0);
  fail_unless (maxalloc >= 150);
  fail_unless (gst_buffer_n_memory (buf) == 3);

  gst_buffer_ref (buf);
  /* map should merge */
  gst_buffer_map_range (buf, 1, 2, &map, GST_MAP_READ);
  /* merged memory is not stored */
  fail_unless (gst_buffer_n_memory (buf) == 3);
  fail_unless (map.size == 100);
  gst_buffer_unmap (buf, &map);

  fail_unless (gst_buffer_n_memory (buf) == 3);

  gst_buffer_unref (buf);

  /* map should merge */
  gst_buffer_map_range (buf, 1, 2, &map, GST_MAP_READ);
  /* merged memory is stored */
  fail_unless (gst_buffer_n_memory (buf) == 2);
  fail_unless (map.size == 100);
  gst_buffer_unmap (buf, &map);

  fail_unless (gst_buffer_n_memory (buf) == 2);

  /* should merge and store */
  gst_buffer_map (buf, &map, GST_MAP_READ);
  fail_unless (gst_buffer_n_memory (buf) == 1);
  gst_buffer_unmap (buf, &map);

  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_find)
{
  GstBuffer *buf;
  gsize maxalloc;
  gsize size, offset;
  guint idx, length;

  buf = gst_buffer_new ();
  gst_buffer_append_memory (buf, gst_allocator_alloc (NULL, 0, NULL));
  gst_buffer_append_memory (buf, gst_allocator_alloc (NULL, 10, NULL));
  gst_buffer_append_memory (buf, gst_allocator_alloc (NULL, 15, NULL));
  gst_buffer_append_memory (buf, gst_allocator_alloc (NULL, 0, NULL));

  size = gst_buffer_get_sizes (buf, &offset, &maxalloc);
  fail_unless (size == 25);
  fail_unless (offset >= 0);
  fail_unless (maxalloc >= 25);
  fail_unless (gst_buffer_n_memory (buf) == 4);

  fail_unless (gst_buffer_find_memory (buf, 0, 5, &idx, &length, &offset));
  fail_unless (idx == 1);
  fail_unless (length == 1);
  fail_unless (offset == 0);

  fail_unless (gst_buffer_find_memory (buf, 0, 10, &idx, &length, &offset));
  fail_unless (idx == 1);
  fail_unless (length == 1);
  fail_unless (offset == 0);

  fail_unless (gst_buffer_find_memory (buf, 5, 4, &idx, &length, &offset));
  fail_unless (idx == 1);
  fail_unless (length == 1);
  fail_unless (offset == 5);

  fail_unless (gst_buffer_find_memory (buf, 5, 5, &idx, &length, &offset));
  fail_unless (idx == 1);
  fail_unless (length == 1);
  fail_unless (offset == 5);

  fail_unless (gst_buffer_find_memory (buf, 5, 6, &idx, &length, &offset));
  fail_unless (idx == 1);
  fail_unless (length == 2);
  fail_unless (offset == 5);

  fail_unless (gst_buffer_find_memory (buf, 10, 6, &idx, &length, &offset));
  fail_unless (idx == 2);
  fail_unless (length == 1);
  fail_unless (offset == 0);

  fail_unless (gst_buffer_find_memory (buf, 10, 15, &idx, &length, &offset));
  fail_unless (idx == 2);
  fail_unless (length == 1);
  fail_unless (offset == 0);

  fail_unless (gst_buffer_find_memory (buf, 11, 14, &idx, &length, &offset));
  fail_unless (idx == 2);
  fail_unless (length == 1);
  fail_unless (offset == 1);

  fail_unless (gst_buffer_find_memory (buf, 0, 25, &idx, &length, &offset));
  fail_unless (idx == 1);
  fail_unless (length == 2);
  fail_unless (offset == 0);

  fail_unless (gst_buffer_find_memory (buf, 24, 0, &idx, &length, &offset));
  fail_unless (idx == 2);
  fail_unless (length == 1);
  fail_unless (offset == 14);

  fail_if (gst_buffer_find_memory (buf, 11, 15, &idx, &length, &offset));
  fail_if (gst_buffer_find_memory (buf, 0, 26, &idx, &length, &offset));
  fail_if (gst_buffer_find_memory (buf, 25, 0, &idx, &length, &offset));

  fail_unless (gst_buffer_find_memory (buf, 1, -1, &idx, &length, &offset));
  fail_unless (idx == 1);
  fail_unless (length == 3);
  fail_unless (offset == 1);

  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_fill)
{
  GstBuffer *buf;
  guint8 data[1024], data2[25];
  gint i;

  buf = gst_buffer_new ();
  gst_buffer_append_memory (buf, gst_allocator_alloc (NULL, 0, NULL));
  gst_buffer_append_memory (buf, gst_allocator_alloc (NULL, 10, NULL));
  gst_buffer_append_memory (buf, gst_allocator_alloc (NULL, 15, NULL));
  gst_buffer_append_memory (buf, gst_allocator_alloc (NULL, 0, NULL));

  for (i = 0; i < G_N_ELEMENTS (data); ++i)
    data[i] = i & 0xff;

  /* a NULL src pointer should be ok if the src length is 0 bytes */
  fail_unless_equals_int (gst_buffer_fill (buf, 0, NULL, 0), 0);
  fail_unless_equals_int (gst_buffer_fill (buf, 20, NULL, 0), 0);
  fail_unless_equals_int (gst_buffer_fill (buf, 0, data, 0), 0);

  fail_unless_equals_int (gst_buffer_fill (buf, 0, data, 1), 1);
  fail_unless_equals_int (gst_buffer_fill (buf, 0, data, 11), 11);
  fail_unless_equals_int (gst_buffer_fill (buf, 0, data, 15), 15);
  fail_unless_equals_int (gst_buffer_fill (buf, 0, data, 25), 25);
  fail_unless_equals_int (gst_buffer_fill (buf, 0, data, 26), 25);
  fail_unless_equals_int (gst_buffer_fill (buf, 1, data, 26), 24);
  fail_unless_equals_int (gst_buffer_fill (buf, 10, data, 100), 15);
  fail_unless_equals_int (gst_buffer_fill (buf, 11, data, 100), 14);
  fail_unless_equals_int (gst_buffer_fill (buf, 25, data, 100), 0);

  fail_unless_equals_int (gst_buffer_fill (buf, 0, data + 10, 25), 25);
  fail_unless_equals_int (gst_buffer_extract (buf, 0, data2, 25), 25);
  fail_unless (memcmp (data2, data + 10, 25) == 0);

  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_parent_buffer_meta)
{
  GstBuffer *buf, *parent;
  GstParentBufferMeta *meta;

  buf = gst_buffer_new ();
  parent = gst_buffer_new ();

  gst_buffer_add_parent_buffer_meta (buf, parent);
  meta = gst_buffer_get_parent_buffer_meta (buf);
  fail_unless (meta);
  fail_unless (parent == meta->buffer);

  gst_buffer_unref (buf);
  gst_buffer_unref (parent);
}

GST_END_TEST;


static Suite *
gst_buffer_suite (void)
{
  Suite *s = suite_create ("GstBuffer");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_subbuffer);
  tcase_add_test (tc_chain, test_subbuffer_make_writable);
  tcase_add_test (tc_chain, test_make_writable);
  tcase_add_test (tc_chain, test_span);
  tcase_add_test (tc_chain, test_metadata_writable);
  tcase_add_test (tc_chain, test_memcmp);
  tcase_add_test (tc_chain, test_copy);
  tcase_add_test (tc_chain, test_copy_deep);
  tcase_add_test (tc_chain, test_try_new_and_alloc);
  tcase_add_test (tc_chain, test_size);
  tcase_add_test (tc_chain, test_resize);
  tcase_add_test (tc_chain, test_map);
  tcase_add_test (tc_chain, test_map_range);
  tcase_add_test (tc_chain, test_find);
  tcase_add_test (tc_chain, test_fill);
  tcase_add_test (tc_chain, test_parent_buffer_meta);

  return s;
}

GST_CHECK_MAIN (gst_buffer);
