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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <gst/check/gstcheck.h>

GST_START_TEST (test_submemory)
{
  GstMemory *memory, *sub;
  GstMapInfo info, sinfo;

  memory = gst_allocator_alloc (NULL, 4, NULL);

  /* check sizes, memory starts out empty */
  fail_unless (gst_memory_map (memory, &info, GST_MAP_WRITE));
  fail_unless (info.size == 4, "memory has wrong size");
  fail_unless (info.maxsize >= 4, "memory has wrong size");
  memset (info.data, 0, 4);
  gst_memory_unmap (memory, &info);

  fail_unless (gst_memory_map (memory, &info, GST_MAP_READ));

  sub = gst_memory_share (memory, 1, 2);
  fail_if (sub == NULL, "share of memory returned NULL");

  fail_unless (gst_memory_map (sub, &sinfo, GST_MAP_READ));
  fail_unless (sinfo.size == 2, "submemory has wrong size");
  fail_unless (memcmp (info.data + 1, sinfo.data, 2) == 0,
      "submemory contains the wrong data");
  ASSERT_MINI_OBJECT_REFCOUNT (sub, "submemory", 1);
  gst_memory_unmap (sub, &sinfo);
  gst_memory_unref (sub);

  /* create a submemory of size 0 */
  sub = gst_memory_share (memory, 1, 0);
  fail_if (sub == NULL, "share memory returned NULL");
  fail_unless (gst_memory_map (sub, &sinfo, GST_MAP_READ));
  fail_unless (sinfo.size == 0, "submemory has wrong size");
  fail_unless (memcmp (info.data + 1, sinfo.data, 0) == 0,
      "submemory contains the wrong data");
  ASSERT_MINI_OBJECT_REFCOUNT (sub, "submemory", 1);
  gst_memory_unmap (sub, &sinfo);
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

  gst_memory_unmap (memory, &info);

  /* test write map + share failure */
  fail_unless (gst_memory_map (memory, &info, GST_MAP_WRITE));
  sub = gst_memory_share (memory, 0, 4);
  fail_unless (sub == NULL, "share with a write map succeeded");

  gst_memory_unmap (memory, &info);
  gst_memory_unref (memory);
}

GST_END_TEST;

GST_START_TEST (test_is_span)
{
  GstMemory *memory, *sub1, *sub2;

  memory = gst_allocator_alloc (NULL, 4, NULL);

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
      (gpointer) ro_memory, sizeof (ro_memory), 0, sizeof (ro_memory), NULL,
      NULL);
  fail_unless (GST_MEMORY_IS_READONLY (mem));

  return mem;
}

GST_START_TEST (test_writable)
{
  GstMemory *mem, *mem2;
  GstMapInfo info;

  /* create read-only memory and try to write */
  mem = create_read_only_memory ();

  fail_if (gst_memory_map (mem, &info, GST_MAP_WRITE));

  /* Make sure mapping anxd unmapping it doesn't change it's locking state */
  fail_unless (gst_memory_map (mem, &info, GST_MAP_READ));
  gst_memory_unmap (mem, &info);

  fail_if (gst_memory_map (mem, &info, GST_MAP_WRITE));

  mem2 = gst_memory_copy (mem, 0, -1);
  fail_unless (GST_MEMORY_IS_READONLY (mem));
  fail_if (GST_MEMORY_IS_READONLY (mem2));

  fail_unless (gst_memory_map (mem2, &info, GST_MAP_WRITE));
  info.data[4] = 'a';
  gst_memory_unmap (mem2, &info);

  gst_memory_ref (mem2);
  fail_if (gst_memory_map (mem, &info, GST_MAP_WRITE));
  gst_memory_unref (mem2);

  fail_unless (gst_memory_map (mem2, &info, GST_MAP_WRITE));
  info.data[4] = 'a';
  gst_memory_unmap (mem2, &info);
  gst_memory_unref (mem2);

  gst_memory_unref (mem);
}

GST_END_TEST;

GST_START_TEST (test_submemory_writable)
{
  GstMemory *mem, *sub_mem;
  GstMapInfo info;

  /* create sub-memory of read-only memory and try to write */
  mem = create_read_only_memory ();

  sub_mem = gst_memory_share (mem, 0, 8);
  fail_unless (GST_MEMORY_IS_READONLY (sub_mem));

  fail_if (gst_memory_map (mem, &info, GST_MAP_WRITE));
  fail_if (gst_memory_map (sub_mem, &info, GST_MAP_WRITE));

  gst_memory_unref (sub_mem);
  gst_memory_unref (mem);
}

GST_END_TEST;

GST_START_TEST (test_copy)
{
  GstMemory *memory, *copy;
  GstMapInfo info, sinfo;

  memory = gst_allocator_alloc (NULL, 4, NULL);
  ASSERT_MINI_OBJECT_REFCOUNT (memory, "memory", 1);

  copy = gst_memory_copy (memory, 0, -1);
  ASSERT_MINI_OBJECT_REFCOUNT (memory, "memory", 1);
  ASSERT_MINI_OBJECT_REFCOUNT (copy, "copy", 1);
  /* memorys are copied and must point to different memory */
  fail_if (memory == copy);

  fail_unless (gst_memory_map (memory, &info, GST_MAP_READ));
  fail_unless (gst_memory_map (copy, &sinfo, GST_MAP_READ));

  /* NOTE that data is refcounted */
  fail_unless (info.size == sinfo.size);

  gst_memory_unmap (copy, &sinfo);
  gst_memory_unmap (memory, &info);

  gst_memory_unref (copy);
  gst_memory_unref (memory);

  memory = gst_allocator_alloc (NULL, 0, NULL);
  fail_unless (gst_memory_map (memory, &info, GST_MAP_READ));
  fail_unless (info.size == 0);
  gst_memory_unmap (memory, &info);

  /* copying a 0-sized memory should not crash */
  copy = gst_memory_copy (memory, 0, -1);
  fail_unless (gst_memory_map (copy, &info, GST_MAP_READ));
  fail_unless (info.size == 0);
  gst_memory_unmap (copy, &info);

  gst_memory_unref (copy);
  gst_memory_unref (memory);
}

GST_END_TEST;

GST_START_TEST (test_try_new_and_alloc)
{
  GstMemory *mem;
  GstMapInfo info;
  gsize size;

  mem = gst_allocator_alloc (NULL, 0, NULL);
  fail_unless (mem != NULL);
  fail_unless (gst_memory_map (mem, &info, GST_MAP_READ));
  fail_unless (info.size == 0);
  gst_memory_unmap (mem, &info);
  gst_memory_unref (mem);

  /* normal alloc should still work */
  size = 640 * 480 * 4;
  mem = gst_allocator_alloc (NULL, size, NULL);
  fail_unless (mem != NULL);
  fail_unless (gst_memory_map (mem, &info, GST_MAP_WRITE));
  fail_unless (info.data != NULL);
  fail_unless (info.size == (640 * 480 * 4));
  info.data[640 * 479 * 4 + 479] = 0xff;
  gst_memory_unmap (mem, &info);

  gst_memory_unref (mem);
}

GST_END_TEST;

GST_START_TEST (test_resize)
{
  GstMemory *mem;
  gsize maxalloc;
  gsize size, maxsize, offset;

  /* one memory block */
  mem = gst_allocator_alloc (NULL, 100, NULL);

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
  GstMapInfo info;
  gsize maxalloc;
  gsize size, offset;

  /* one memory block */
  mem = gst_allocator_alloc (NULL, 100, NULL);

  size = gst_memory_get_sizes (mem, &offset, &maxalloc);
  fail_unless (size == 100);
  fail_unless (offset == 0);
  fail_unless (maxalloc >= 100);

  /* see if simply mapping works */
  fail_unless (gst_memory_map (mem, &info, GST_MAP_READ));
  fail_unless (info.data != NULL);
  fail_unless (info.size == 100);
  fail_unless (info.maxsize == maxalloc);

  gst_memory_unmap (mem, &info);
  gst_memory_unref (mem);
}

GST_END_TEST;

GST_START_TEST (test_map_nested)
{
  GstMemory *mem;
  GstMapInfo info1, info2;

  mem = gst_allocator_alloc (NULL, 100, NULL);

  /* nested mapping */
  fail_unless (gst_memory_map (mem, &info1, GST_MAP_READ));
  fail_unless (info1.data != NULL);
  fail_unless (info1.size == 100);

  fail_unless (gst_memory_map (mem, &info2, GST_MAP_READ));
  fail_unless (info2.data == info1.data);
  fail_unless (info2.size == 100);

  /* unmap */
  gst_memory_unmap (mem, &info2);
  gst_memory_unmap (mem, &info1);

  fail_unless (gst_memory_map (mem, &info1, GST_MAP_READ));
  /* not allowed */
  fail_if (gst_memory_map (mem, &info2, GST_MAP_WRITE));
  fail_if (gst_memory_map (mem, &info2, GST_MAP_READWRITE));
  fail_unless (gst_memory_map (mem, &info2, GST_MAP_READ));
  gst_memory_unmap (mem, &info2);
  gst_memory_unmap (mem, &info1);

  fail_unless (gst_memory_map (mem, &info1, GST_MAP_WRITE));
  /* not allowed */
  fail_if (gst_memory_map (mem, &info2, GST_MAP_READ));
  fail_if (gst_memory_map (mem, &info2, GST_MAP_READWRITE));
  fail_unless (gst_memory_map (mem, &info2, GST_MAP_WRITE));
  gst_memory_unmap (mem, &info1);
  gst_memory_unmap (mem, &info2);
  /* nothing was mapped */
  ASSERT_CRITICAL (gst_memory_unmap (mem, &info2));

  fail_unless (gst_memory_map (mem, &info1, GST_MAP_READWRITE));
  fail_unless (gst_memory_map (mem, &info2, GST_MAP_READ));
  gst_memory_unmap (mem, &info2);
  fail_unless (gst_memory_map (mem, &info2, GST_MAP_WRITE));
  gst_memory_unmap (mem, &info2);
  gst_memory_unmap (mem, &info1);
  /* nothing was mapped */
  ASSERT_CRITICAL (gst_memory_unmap (mem, &info1));

  gst_memory_unref (mem);
}

GST_END_TEST;

GST_START_TEST (test_map_resize)
{
  GstMemory *mem;
  GstMapInfo info;
  gsize size, maxalloc, offset;

  mem = gst_allocator_alloc (NULL, 100, NULL);

  /* do mapping */
  fail_unless (gst_memory_map (mem, &info, GST_MAP_READ));
  fail_unless (info.data != NULL);
  fail_unless (info.size == 100);

  /* resize the buffer */
  gst_memory_resize (mem, 1, info.size - 1);
  size = gst_memory_get_sizes (mem, &offset, &maxalloc);
  fail_unless (size == 99);
  fail_unless (offset == 1);
  fail_unless (maxalloc >= 100);
  gst_memory_unmap (mem, &info);

  size = gst_memory_get_sizes (mem, &offset, &maxalloc);
  fail_unless (size == 99);
  fail_unless (offset == 1);
  fail_unless (maxalloc >= 100);

  fail_unless (gst_memory_map (mem, &info, GST_MAP_READ));
  fail_unless (info.data != NULL);
  fail_unless (info.size == 99);
  fail_unless (info.maxsize >= 100);
  gst_memory_unmap (mem, &info);

  /* and larger */
  fail_unless (gst_memory_map (mem, &info, GST_MAP_READ));
  gst_memory_resize (mem, -1, 100);
  gst_memory_unmap (mem, &info);

  size = gst_memory_get_sizes (mem, &offset, &maxalloc);
  fail_unless (size == 100);
  fail_unless (offset == 0);
  fail_unless (maxalloc >= 100);

  fail_unless (gst_memory_map (mem, &info, GST_MAP_READ));
  gst_memory_unmap (mem, &info);
  gst_memory_unref (mem);
}

GST_END_TEST;

GST_START_TEST (test_alloc_params)
{
  GstMemory *mem;
  GstMapInfo info;
  gsize size, offset, maxalloc;
  GstAllocationParams params;
  guint8 arr[10];

  memset (arr, 0, 10);

  gst_allocation_params_init (&params);
  params.padding = 10;
  params.prefix = 10;
  params.flags = GST_MEMORY_FLAG_ZERO_PREFIXED | GST_MEMORY_FLAG_ZERO_PADDED;
  mem = gst_allocator_alloc (NULL, 100, &params);

  /*Checking size and offset */
  size = gst_memory_get_sizes (mem, &offset, &maxalloc);
  fail_unless (size == 100);
  fail_unless (offset == 10);
  fail_unless (maxalloc >= 120);

  fail_unless (GST_MEMORY_FLAG_IS_SET (mem, GST_MEMORY_FLAG_ZERO_PREFIXED));
  fail_unless (GST_MEMORY_FLAG_IS_SET (mem, GST_MEMORY_FLAG_ZERO_PADDED));

  fail_unless (gst_memory_map (mem, &info, GST_MAP_READ));
  fail_unless (info.data != NULL);
  fail_unless (info.size == 100);

  /*Checking prefix */
  fail_unless (memcmp (info.data - 10, arr, 10) == 0);

  /*Checking padding */
  fail_unless (memcmp (info.data + 100, arr, 10) == 0);


  gst_memory_unmap (mem, &info);
  gst_memory_unref (mem);
}

GST_END_TEST;

GST_START_TEST (test_lock)
{
  GstMemory *mem;

  mem = gst_allocator_alloc (NULL, 10, NULL);
  fail_unless (mem != NULL);

  /* test exclusivity */
  fail_unless (gst_memory_lock (mem,
          GST_LOCK_FLAG_WRITE | GST_LOCK_FLAG_EXCLUSIVE));
  fail_if (gst_memory_lock (mem, GST_LOCK_FLAG_EXCLUSIVE));
  fail_unless (gst_memory_lock (mem, GST_LOCK_FLAG_WRITE));
  gst_memory_unlock (mem, GST_LOCK_FLAG_WRITE | GST_LOCK_FLAG_EXCLUSIVE);
  gst_memory_unlock (mem, GST_LOCK_FLAG_WRITE);

  /* no lock here */

  fail_unless (gst_memory_lock (mem,
          GST_LOCK_FLAG_READ | GST_LOCK_FLAG_EXCLUSIVE));
  fail_unless (gst_memory_lock (mem,
          GST_LOCK_FLAG_READ | GST_LOCK_FLAG_EXCLUSIVE));
  gst_memory_unlock (mem, GST_LOCK_FLAG_READ | GST_LOCK_FLAG_EXCLUSIVE);
  gst_memory_unlock (mem, GST_LOCK_FLAG_READ | GST_LOCK_FLAG_EXCLUSIVE);

  /* no lock here */

  fail_unless (gst_memory_lock (mem,
          GST_LOCK_FLAG_READWRITE | GST_LOCK_FLAG_EXCLUSIVE));
  fail_unless (gst_memory_lock (mem, GST_LOCK_FLAG_READ));
  fail_if (gst_memory_lock (mem, GST_LOCK_FLAG_READ | GST_LOCK_FLAG_EXCLUSIVE));
  fail_if (gst_memory_lock (mem, GST_LOCK_FLAG_EXCLUSIVE));
  fail_unless (gst_memory_lock (mem, GST_LOCK_FLAG_WRITE));
  gst_memory_unlock (mem, GST_LOCK_FLAG_WRITE);
  gst_memory_unlock (mem, GST_LOCK_FLAG_READ);
  gst_memory_unlock (mem, GST_LOCK_FLAG_READWRITE | GST_LOCK_FLAG_EXCLUSIVE);

  gst_memory_unref (mem);
}

GST_END_TEST;

typedef struct
{
  GstMemory mem;
  gpointer data;
} MyOpaqueMemory;

static GstMemory *
_my_opaque_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  MyOpaqueMemory *mem = g_slice_new (MyOpaqueMemory);
  gsize maxsize = size + params->prefix + params->padding;

  gst_memory_init (GST_MEMORY_CAST (mem), params->flags, allocator, NULL,
      maxsize, params->align, params->prefix, size);

  mem->data = g_malloc (maxsize);

  return (GstMemory *) mem;
}

static void
_my_opaque_free (GstAllocator * allocator, GstMemory * mem)
{
  MyOpaqueMemory *mmem = (MyOpaqueMemory *) mem;

  g_free (mmem->data);
  g_slice_free (MyOpaqueMemory, mmem);
}

static gpointer
_my_opaque_mem_map (MyOpaqueMemory * mem, gsize maxsize, GstMapFlags flags)
{
  /* the subclass is reponsible for logging any error, by design choice and for
   * testing purpose MyOpaqueMemory never logs any trace */
  return NULL;
}

static gboolean
_my_opaque_mem_unmap (MyOpaqueMemory * mem)
{
  return FALSE;
}

static MyOpaqueMemory *
_my_opaque_mem_share (MyOpaqueMemory * mem, gssize offset, gsize size)
{
  return NULL;
}

typedef struct
{
  GstAllocator parent;
} MyOpaqueMemoryAllocator;

typedef struct
{
  GstAllocatorClass parent_class;
} MyOpaqueMemoryAllocatorClass;

GType my_opaque_memory_allocator_get_type (void);
G_DEFINE_TYPE (MyOpaqueMemoryAllocator, my_opaque_memory_allocator,
    GST_TYPE_ALLOCATOR);

static void
my_opaque_memory_allocator_class_init (MyOpaqueMemoryAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class;

  allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = _my_opaque_alloc;
  allocator_class->free = _my_opaque_free;
}

static void
my_opaque_memory_allocator_init (MyOpaqueMemoryAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = "MyOpaqueMemory";
  alloc->mem_map = (GstMemoryMapFunction) _my_opaque_mem_map;
  alloc->mem_unmap = (GstMemoryUnmapFunction) _my_opaque_mem_unmap;
  alloc->mem_share = (GstMemoryShareFunction) _my_opaque_mem_share;
}

static void
my_opaque_memory_init (void)
{
  GstAllocator *allocator;

  allocator = g_object_new (my_opaque_memory_allocator_get_type (), NULL);

  gst_allocator_register ("MyOpaqueMemory", allocator);
}

static void
_custom_log_func (GstDebugCategory * category,
    GstDebugLevel level, const gchar * file, const gchar * function,
    gint line, GObject * object, GstDebugMessage * message, gpointer unused)
{
  const gchar *dbg_msg = gst_debug_message_get (message);
  fail_unless (dbg_msg == NULL);
}

#ifndef GST_DISABLE_GST_DEBUG
GST_START_TEST (test_no_error_and_no_warning_on_map_failure)
{
  GstAllocator *alloc;
  GstMemory *mem;
  GstMapInfo info;
  gsize maxalloc;
  gsize size, offset;
  GstDebugLevel prev_debug_threshold;
  gboolean prev_debug_is_active;

  my_opaque_memory_init ();
  alloc = gst_allocator_find ("MyOpaqueMemory");
  mem = gst_allocator_alloc (alloc, 100, NULL);

  size = gst_memory_get_sizes (mem, &offset, &maxalloc);
  fail_unless (size == 100);
  fail_unless (offset == 0);
  fail_unless (maxalloc >= 100);

  /* Prepare custom logging to capture any GST_ERROR and GST_WARNING. */
  prev_debug_threshold = gst_debug_get_default_threshold ();
  prev_debug_is_active = gst_debug_is_active ();
  gst_debug_set_active (TRUE);
  fail_unless (gst_debug_is_active ());
  gst_debug_set_default_threshold (GST_LEVEL_WARNING);
  fail_unless (gst_debug_get_default_threshold () == GST_LEVEL_WARNING);
  gst_debug_remove_log_function (gst_debug_log_default);
  gst_debug_add_log_function (_custom_log_func, NULL, NULL);

  /* Ensure that the map does not log any error on failure. It has to fail
   * because the custom opaque memory here is desgined to not be mappable. */
  fail_if (gst_memory_map (mem, &info, GST_MAP_READ));
  fail_if (info.data != NULL);
  fail_if (info.size != 0);
  fail_if (info.maxsize != 0);

  fail_if (gst_memory_map (mem, &info, GST_MAP_WRITE));
  fail_if (info.data != NULL);
  fail_if (info.size != 0);
  fail_if (info.maxsize != 0);

  gst_memory_unref (mem);
  gst_object_unref (alloc);

  /* Restore previous logging state. */
  gst_debug_set_default_threshold (prev_debug_threshold);
  gst_debug_add_log_function (gst_debug_log_default, NULL, NULL);
  gst_debug_remove_log_function (_custom_log_func);
  gst_debug_set_active (prev_debug_is_active);
}

GST_END_TEST;
#endif /* !GST_DISABLE_GST_DEBUG */

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
  tcase_add_test (tc_chain, test_map_nested);
  tcase_add_test (tc_chain, test_map_resize);
  tcase_add_test (tc_chain, test_alloc_params);
  tcase_add_test (tc_chain, test_lock);
#ifndef GST_DISABLE_GST_DEBUG
  tcase_add_test (tc_chain, test_no_error_and_no_warning_on_map_failure);
#endif

  return s;
}

GST_CHECK_MAIN (gst_memory);
